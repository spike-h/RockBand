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

/**
 * @brief Initializes the VGA display -- draws background.
 */
void draw_background()
{
    // Draw vertical lines on the screen 50 pixels from center
    int offset = 50;
    drawVLine(SCREEN_WIDTH/2 + offset, 0, SCREEN_HEIGHT, WHITE);
    drawVLine(SCREEN_WIDTH/2 - offset, 0, SCREEN_HEIGHT, WHITE);
}

// ========================================
// ============ ANIMATION LOOP ============
// ========================================a
static PT_THREAD(protothread_animation_loop(struct pt *pt))
{
    PT_BEGIN(pt);

    draw_background();

    while (1)
    {
    draw_background();
    PT_YIELD_usec(100000);
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