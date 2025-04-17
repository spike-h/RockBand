/**
 * Spike Hofflich
 * Paige Shelton
 * Edwin Chen
 *
 * This demonstration animates two balls bouncing about the screen.
 * Through a serial interface, the user can change the ball color.
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 470 ohm resistor ---> VGA Green
 *  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - GPIO 21 ---> 330 ohm resistor ---> VGA Red
 *  - RP2040 GND ---> VGA GND
 * #define DAC PIN_MISO 4
 * #define DAC PIN_CS 5
 * #define DAC PIN_SCK 6
 * #define DAC PIN_MOSI 7
 * # LDAC to gnd
 * # button on GPIO 3 and GND
 * 
 * 
 *
 * RESOURCES USED
 *  - PIO state machines 0, 1, and 2 on PIO instance 0
 *  - DMA channels (2, by claim mechanism)
 *  - 153.6 kBytes of RAM (for pixel color data)
 *
 */

// Include the VGA grahics library
#include "vga16_graphics.h"
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
// Include Pico libraries
#include "pico/stdlib.h"
#include "pico/divider.h"
#include "pico/multicore.h"
// Include hardware libraries
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/spi.h"
#include "hardware/regs/rosc.h"
#include "hardware/adc.h"
// Include protothreads
#include "pt_cornell_rp2040_v1_3.h"

// === the fixed point macros ========================================
typedef signed int fix15;
#define multfix15(a, b) ((fix15)((((signed long long)(a)) * ((signed long long)(b))) >> 15))
#define float2fix15(a) ((fix15)((a) * 32768.0)) // 2^15
#define fix2float15(a) ((float)(a) / 32768.0)
#define absfix15(a) abs(a)
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a, b) (fix15)(div_s64s64((((signed long long)(a)) << 15), ((signed long long)(b))))

// Wall detection
#define hitBottom(b) (b > int2fix15(360))
#define hitTop(b) (b < int2fix15(100))
#define hitLeft(a) (a < int2fix15(100))
#define hitRight(a) (a > int2fix15(540))

// uS per frame
#define FRAME_RATE 33000

// DAC Pins
#define PIN_MISO 4
#define PIN_CS 5
#define PIN_SCK 6
#define PIN_MOSI 7
#define SPI_PORT spi0

// Built in LED Pin
#define LED 25

// Keypad pin configurations
#define BASE_KEYPAD_PIN 9
#define KEYROWS 4
#define NUMKEYS 12
unsigned int keycodes[12] = {0x28, 0x11, 0x21, 0x41, 0x12, 0x22, 0x42, 0x14, 0x24, 0x44, 0x18, 0x48};
unsigned int scancodes[4] = {0x01, 0x02, 0x04, 0x08};
unsigned int button = 0x70;
int prev_key = -1;

// ================================================================================================================
// =================================== BEGIN AUDIO SYNTHESIS CODE =================================================
// ================================================================================================================

// ================================================================================================================
// ====================================== END AUDIO SYNTHESIS CODE ================================================
// ================================================================================================================


// ================================================================================================================
// ========================================== BEGIN INPUT CODE ====================================================
// ================================================================================================================

// ===========================================
// ============= KEYPAD CODE ================
// ===========================================
static PT_THREAD(protothread_keypad_scan(struct pt *pt)) {
    // Initialize protothread and parameters
    PT_BEGIN(pt);
    static int i;
    static uint32_t keypad;

    // Main loop to handle keypad input
    while (1) {
        // Scan the keypad for a keypress
        for (i = 0; i < KEYROWS; i++) {
            gpio_put_masked((0xF << BASE_KEYPAD_PIN), (scancodes[i] << BASE_KEYPAD_PIN));
            sleep_us(1);
            keypad = ((gpio_get_all() >> BASE_KEYPAD_PIN) & 0x7F);
            if (keypad & button) break;
        }
        // If a key is pressed, find the key code
        if (keypad & button) {
            for (i = 0; i < NUMKEYS; i++) {
                if (keypad == keycodes[i]) break;
            }
            if (i == NUMKEYS) (i = -1);
        } 
        // If no key is pressed, set i to -1
        else {
            i = -1;
        }

        // If a new key is pressed, "i" will be different than "prev_key"
        if (i != prev_key) {
            // Update "prev_key"
            prev_key = i;

            // DO A THING FOR KEY PRESS
            printf("Key %d pressed\n", i);
        }
        // Yield for 30ms
        PT_YIELD_usec(30000);
    }
    // End the protothread
    PT_END(pt);
}

// ================================================================================================================
// ========================================== END INPUT CODE ======================================================
// ================================================================================================================

// ================================================================================================================
// ======================================  BEGIN ANIMATION CODE ===================================================
// ================================================================================================================

const int SCREEN_WIDTH = 640;
const int SCREEN_HEIGHT = 480;
int trackWidth = SCREEN_WIDTH/3; // total width of the track

struct note {
    int lane;   // which lane number it is in i.e. || 1 || 2  || 3 ||
    int y;      // y position of the note
    int height; // height of the note -- used for sustaning notes
    int color;  // ye (in form 0-15)
    // Maybe add start time and 
};

int numLanes = 3; // number of lanes
struct note notes[3][50]; // 3 lanes of notes, 50 is the max number of notes in each lane at a single time (arbitary large number)
int activeNotesInLane[3]; // number of notes in each lane

/**
 * @brief Initializes the VGA display -- draws background.
 */
void draw_background()
{
    // Draw vertical lines on the screen equally spaced away from the center
    int offset = trackWidth/2;
    int singleTrackWidth = trackWidth/numLanes;
    drawVLine(SCREEN_WIDTH/2 + offset, 0, SCREEN_HEIGHT, WHITE);
    drawVLine(SCREEN_WIDTH/2 - offset, 0, SCREEN_HEIGHT, WHITE);
    // Draws the track lines -- lines in between the two outer lines dictated by numLines
    for (int i = 1; i < numLanes; i++)
    {
        drawVLine(SCREEN_WIDTH/2 - offset + (i * singleTrackWidth), 0, SCREEN_HEIGHT, WHITE);
    }
}

/**
 * @brief Spawns a note in the given lane
 * @param lane The lane to spawn the note in
 * @param color The color of the note
 * @param height The height of the note
 * @note Assumes that the lane is valid and that there is space in the lane
 */
void spawn_note(int lane, int color, int height)
{
    // Spawn a note in the given lane
    if (activeNotesInLane[lane] < 50) // check if there is space in the lane
    {
        notes[lane][activeNotesInLane[lane]].lane = lane;
        notes[lane][activeNotesInLane[lane]].y = SCREEN_HEIGHT - height; // spawn at the top of the screen
        notes[lane][activeNotesInLane[lane]].height = height;
        notes[lane][activeNotesInLane[lane]].color = color;
        activeNotesInLane[lane]++;
    }
}

/**
 * @brief Draws the falling notes
 */
void draw_notes()
{
    int singleTrackWidth = trackWidth/numLanes;
    // Draws the falling notes -- lines in between the two outer lines dictated by numLines
    for (int i = 0; i < numLanes; i++)
    {
        for (int j = 0; j < activeNotesInLane[i]; j++)
        {
            // Draw the note at its current position
            drawRect(SCREEN_WIDTH/2 - trackWidth + (i*singleTrackWidth), notes[i][j].y-notes[i][j].height, singleTrackWidth, notes[i][j].height, notes[i][j].color);
        }
    }
}

/**
 * @brief Updates the falling notes
 * Currently just moves them down the screen and deletes them once we hit the bottom
 */
void update_notes()
{
    int gravity = 1; // The speed at which the notes fall -- can be changed to make it harder or easier
    // Update the falling notes -- move them down the screen
    for (int i = 0; i < numLanes; i++)
    {
        for (int j = activeNotesInLane[i]-1; j >= 0; j--)
        {
            // Move the note down the screen
            notes[i][j].y += gravity;
            // If the note is off the screen, remove it from the lane
            if (notes[i][j].y > SCREEN_HEIGHT)
            {
                // Remove the note from the lane
                activeNotesInLane[i]--;
                notes[i][j] = notes[i][activeNotesInLane[i]]; // Move the last note to the current position
            }
        }
    }
}

// ========================================
// ============ ANIMATION LOOP ============
// ========================================a
static PT_THREAD(protothread_animation_loop(struct pt *pt))
{
    PT_BEGIN(pt);

    draw_background();
    // Spawn notes
    spawn_note(0, RED, 50);
    spawn_note(1, GREEN, 50);
    spawn_note(2, BLUE, 50);

    // // Spawn notes in random lanes
    // for (int i = 0; i < 50; i++)
    // {
    //     int lane = rand() % numLanes; // Random lane
    //     int color = rand() % 16; // Random color
    //     int height = rand() % (SCREEN_HEIGHT/2); // Random height
    //     spawn_note(lane, color, height);
    // }

    while (1)
    {
    draw_background();
    update_notes();
    draw_notes();
    PT_YIELD_usec(1000);
    }

    PT_END(pt);
}

// ================================================================================================================
// =====================================  END ANIMATION CODE ======================================================
// ================================================================================================================

// ===========================================
// =============== LED BLINKY ================
// ===========================================
static PT_THREAD(protothread_blinky(struct pt *pt)) {
    // Initialize protothread and parameters
    PT_BEGIN(pt);
    static int i = 0;

    // Main loop to blink the LED
    while (1) {
        // Blink the LED every 50ms
        gpio_put(LED, i % 2);
        i++;
        PT_YIELD_usec(50000);
    }
    // End the protothread
    PT_END(pt);
}

///////////////////////////////////////////////////////////////
// MAIN FUNCTION
///////////////////////////////////////////////////////////////
int main()
{

    // Initialize stdio/uart (printf won't work unless you do this!)
    stdio_init_all();
    printf("Hello, friends!\n");
    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000);
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);

    // initialize VGA
    initVGA();

    // Map LED to GPIO port, make it low
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    gpio_put(LED, 1);

    // Initialize the keypad GPIO's
    gpio_init_mask((0x7F << BASE_KEYPAD_PIN));
    // Set row-pins to output
    gpio_set_dir_out_masked((0xF << BASE_KEYPAD_PIN));
    // Set all output pins to low
    gpio_put_masked((0xF << BASE_KEYPAD_PIN), (0x0 << BASE_KEYPAD_PIN));
    // Turn on pulldown resistors for column pins (on by default)
    gpio_pull_down((BASE_KEYPAD_PIN + 4));
    gpio_pull_down((BASE_KEYPAD_PIN + 5));
    gpio_pull_down((BASE_KEYPAD_PIN + 6));

    // Add core 0 threads
    pt_add_thread(protothread_animation_loop);
    pt_add_thread(protothread_blinky);
    pt_add_thread(protothread_keypad_scan);
    // Start scheduling core 0 threads
    pt_schedule_start;
}