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
// include picture header
#include "gamebg.h"
#include "menubg.h"
// include dac header
#include "amplitude_envelope_piano.h"
#include "amplitude_envelope_mario.h"

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

// max, min
#define max(a, b) ((a > b) ? a : b)
#define min(a, b) ((a < b) ? a : b)

// uS per frame
#define FRAME_RATE 33000

// DAC Pins
#define PIN_CS 5
#define PIN_SCK 6
#define PIN_MOSI 7
#define SPI_PORT spi0

// Built in LED Pin
#define LED 25

// VGA display parameters
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

// ================================================================================================================
// =================================== BEGIN AUDIO SYNTHESIS CODE =================================================
// ================================================================================================================

// Number of samples per period in sine table
#define sine_table_size 492868

// Sine table
int raw_sin[sine_table_size];

// Table of values to be sent to DAC
// unsigned short DAC_data[sine_table_size];

// Pointer to the address of the DAC data table
const unsigned short *address_pointer2 = &DAC_data[0];

// A-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

// Number of DMA transfers per event
const uint32_t transfer_count = sine_table_size;

// initializing dma channels
int data_chan;
int ctrl_chan;
dma_channel_config c2;

void play_sound()
{
    // *thunk*
    if (!(dma_channel_is_busy(data_chan) || dma_channel_is_busy(ctrl_chan)))
    {
        // Wait for the DMA channel to finish before starting a new transfer
        // dma_channel_wait_for_finish_blocking(data_chan);
        // dma_channel_wait_for_finish_blocking(ctrl_chan);
        dma_start_channel_mask(1u << ctrl_chan);
    }
}

void play_mario_death()
{
    dma_channel_abort(data_chan); // abort the current transfer
    dma_channel_abort(ctrl_chan); // abort the current transfer

    // stop ping ponging
    channel_config_set_chain_to(&c2, data_chan); // Chain to control channel COMMENT OUT TO PREVENT LOOPING
    // reconfigure Dma channel to play mario instead
    dma_channel_configure(
        data_chan,                 // Channel to be configured
        &c2,                       // The configuration we just created
        &spi_get_hw(SPI_PORT)->dr, // write address (SPI data register)
        DAC_data_mario,                  // The initial read address
        59806,                       // Number of transfers
        false                      // Don't start immediately.
    );

    // Start the DMA channel
    dma_start_channel_mask(1u << data_chan);

    // wait for the DMA channel to finish
    dma_channel_wait_for_finish_blocking(data_chan);

    // write stuff to screen
    fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RED); // clear the screen

    // bring back original config
    // stop ping ponging
    channel_config_set_chain_to(&c2, ctrl_chan); // Chain to control channel COMMENT OUT TO PREVENT LOOPING
    dma_channel_configure(
        data_chan,                 // Channel to be configured
        &c2,                       // The configuration we just created
        &spi_get_hw(SPI_PORT)->dr, // write address (SPI data register)
        DAC_data,                  // The initial read address
        transfer_count,            // Number of transfers
        false                      // Don't start immediately.
    );
}

// ================================================================================================================
// ====================================== END AUDIO SYNTHESIS CODE ================================================
// ================================================================================================================

// ================================================================================================================
// ======================================  BEGIN ANIMATION CODE ===================================================
// ================================================================================================================

// ===============================
// =======  menu code ============
// ===============================
static PT_THREAD(protothread_animation_loop(struct pt *pt));

// Menu state variables
int menu_state = 0; // 0 = main menu, 1 = game, 2 = credits
int menu_selection = 0; // 0 = play endless, 1 = play song with lives, 2 = play song with no lives, 3 = credits
int current_menu_selection = 0; // current menu selection
int lives = -1;

// draw the main menu
void draw_menu()
{
    // fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, BLACK); // clear the screen
    drawPicture(0, 0, (unsigned short *)vga_menu_image, 640, 480); // Draw the picture on the screen
    setTextColor2(WHITE, BLACK);
    setTextSize(2);
    setCursor(100, 320);
    writeString("Play Endless");
    setCursor(100, 360);
    writeString("Play Song with Lives");
    setCursor(100, 400);
    writeString("Play Song with all 12 lanes");
    setCursor(100, 440);
    writeString("Credits");
}

// draw cursor on the menu given the current menu selection
void draw_cursor(int erase)
{
    // Draw the cursor on the screen
    int x = 80; // x position of the cursor
    int y = 320 + (menu_selection * 40); // y position of the cursor
    if (erase)
    {
        fillRect(x, y, 10, 10, BLACK); // erase the cursor on the screen
    }
    else
    {
        fillRect(x, y, 10, 10, WHITE); // draw the cursor on the screen
    }
}

// ===========================
// ===== menu loop ===========
// ===========================

void draw_credits()
{
    // Draw the credits on the screen
    fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, BLACK); // clear the screen
    setTextColor2(WHITE, BLACK);
    setTextSize(2);
    setCursor(50, 100);
    writeString("Credits");
    setCursor(50, 140);
    writeString("Made by: Spike Hofflich, Paige Shelton, Edwin Chen");
    setCursor(50, 180);
    writeString("Music by: erm we gotta find that out");
    setCursor(50, 220);
    writeString("Press any button to go back to the main menu");
}

const int trackWidth = SCREEN_WIDTH / 3; // total width of the track
const int whiteHeight = 120;            // height of the white key
const int hitHeight = whiteHeight + 80;               // top height of the hit line from above the bottom of the screen
const int hitWidth = 40;                 // how tall the hit line is (hittable area)
int combo = 0;                           // combo counter for the number of notes hit in a row
int maxCombo = 0;                       // max combo counter for the number of notes hit in a row

typedef struct note
{
    int lane;     // which lane number it is in i.e. || 1 || 2  || 3 ||
    float y;      // y position of the note (top edge of rectangle)
    int height;   // height of the note -- used for sustaning notes
    int color;    // ye (in form 0-15)
    bool hit;     // if the note has been hit or not  - used for erasing the note as its hit
    bool sustain; // if the note is a sustained note or not
    // Maybe add start time and
} note;

#define numLanes 4                                                 // number of lanes
volatile note notes[numLanes][50];                                   // 3 lanes of notes, 50 is the max number of notes in each lane at a single time (arbitary large number)
volatile int activeNotesInLane[numLanes];                            // number of notes in each lane
const int gravity = 10;                                              // The speed at which the notes fall -- can be changed to make it harder or easier
const int noteSkinniness = 2;                                        // offset for the notes to make them look better and be in the center of the lane
volatile int numNotesHit = 0;                                        // number of notes hit
volatile int numNotesMissed = 0;                                     // number of notes missed
const bool pianoKeyTypes[13] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1}; // 1 is a white key 0 is black -- used for drawing the piano keys on the screen
bool pianoKeysPressed[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool setup = false; // flag to check if the setup has been done

void draw_piano(int lane, bool outline);

/**
 * @brief Initializes the VGA display -- draws background.
 */
void draw_background()
{
    // Draw vertical lines on the screen equally spaced away from the center
    float offset = trackWidth / 2;
    float singleTrackWidth = trackWidth / numLanes;
    // drawVLine(SCREEN_WIDTH/2 + offset, 0, SCREEN_HEIGHT, WHITE);
    // drawVLine(SCREEN_WIDTH / 2 - trackWidth / 2, 0, SCREEN_HEIGHT, WHITE);
    // Draws the track lines -- lines in between the two outer lines dictated by numLines
    for (int i = 0; i <= numLanes; i++)
    {
        drawVLine(SCREEN_WIDTH / 2 - trackWidth / 2 + (i * trackWidth / numLanes), 0, SCREEN_HEIGHT, WHITE);
    }
    // Draw the number of hit and unhit notes on the screen
    setCursor(10, 10);
    setTextColor2(WHITE, BLACK);
    setTextSize(2);
    writeString("Notes Hit: ");
    setCursor(10, 25);
    writeString("Notes Missed: ");
    setCursor(10, 40);
    writeString("Combo: ");
    setCursor(10, 55);
    writeString("Max Combo: ");

    // Draw a piano diagram on the screen
    for (int i = 0; i < numLanes; i++)
    {
        draw_piano(i, 0); // draw the piano keys on the screen
    }

    // Draw black keys on the screen
    // for (int i = 0; i < 12; i++)
    // {
    //     if (!pianoKeyTypes[i])
    //     {
    //         fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (i * trackWidth / numLanes), SCREEN_HEIGHT - hitHeight, trackWidth / numLanes, hitHeight / 2, BLACK); // draw the bottom of the note that moved down
    //     }
    // }
}

void draw_piano(int lane, bool outline)
{
    // Draw the piano keys on the screen
    int blackHeight = whiteHeight / 2; // height of the black key
    char keyColor;
    char outlineColor;
    if (activeNotesInLane[lane] > 0)
    {
        outlineColor = MAGENTA;
    }
    else
    {
        outlineColor = GREEN;
    }
    if (pianoKeyTypes[lane]) // white key
    {
        if (pianoKeysPressed[lane])
        {
            keyColor = RED;
        }
        else
        {
            keyColor = WHITE;
        }
        if (((lane != (numLanes - 1)) && (lane != 0)) && (!(pianoKeyTypes[lane + 1]) && !(pianoKeyTypes[lane - 1]))) // black key on the right and left
        {
            if (!outline) {
                fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (lane * trackWidth / numLanes) - 1, SCREEN_HEIGHT - whiteHeight, (trackWidth / numLanes) + 2, whiteHeight, keyColor);                                 // draw the note and bleed it into the next lane a bit
                fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + ((lane + 1) * trackWidth / numLanes) - 2, SCREEN_HEIGHT - whiteHeight + blackHeight, (trackWidth / numLanes) * 1 / 2 + 2,  blackHeight, keyColor); // draw the note and bleed it into the next lane a bit
                fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + ((lane - 1) * trackWidth / numLanes) + trackWidth /(2*numLanes) - 2, SCREEN_HEIGHT - whiteHeight + blackHeight, (trackWidth / numLanes) * 1 / 2 + 2, blackHeight, keyColor); // draw the note and bleed it into the next lane a bit
            }
            drawRect (SCREEN_WIDTH / 2 - trackWidth / 2 + ((lane - 1) * trackWidth / numLanes) + trackWidth /(2*numLanes), SCREEN_HEIGHT - whiteHeight, (trackWidth / numLanes) * 2, whiteHeight, outlineColor);                     // draw the outline
            char rightCOlor = (pianoKeysPressed[lane + 1]) ? GREEN : BLACK;
            drawVLine(SCREEN_WIDTH / 2 - trackWidth / 2 + ((lane - 1) * trackWidth / numLanes) + trackWidth /(2*numLanes) + (trackWidth / numLanes) * 2 - 1, SCREEN_HEIGHT - whiteHeight, blackHeight, rightCOlor);           // draw the outline
            char leftCOlor = (pianoKeysPressed[lane - 1]) ? GREEN : BLACK;
            drawVLine(SCREEN_WIDTH / 2 - trackWidth / 2 + ((lane - 1) * trackWidth / numLanes) + trackWidth /(2*numLanes), SCREEN_HEIGHT - whiteHeight, blackHeight, leftCOlor);

        }                                                         
        else if (lane != (numLanes - 1) && !(pianoKeyTypes[lane + 1])) // black key on the right
        {
            if (!outline) {
                fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (lane * trackWidth / numLanes) -1, SCREEN_HEIGHT - whiteHeight, (trackWidth / numLanes) + 2, whiteHeight, keyColor);                                 // draw the note and bleed it into the next lane a bit
                fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + ((lane + 1) * trackWidth / numLanes) - 2, SCREEN_HEIGHT - whiteHeight + blackHeight, (trackWidth / numLanes) * 1 / 2 + 2, blackHeight, keyColor); // draw the note and bleed it into the next lane a bit
            }
            drawRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (lane * trackWidth / numLanes), SCREEN_HEIGHT - whiteHeight, (trackWidth / numLanes) * 3 / 2 + 1, whiteHeight, outlineColor);                     // draw the outline
            char rightCOlor = (pianoKeysPressed[lane + 1]) ? GREEN : BLACK;
            drawVLine(SCREEN_WIDTH / 2 - trackWidth / 2 + (lane * trackWidth / numLanes) + (trackWidth / numLanes) * 3 / 2, SCREEN_HEIGHT - whiteHeight, blackHeight, rightCOlor);
        }
        else if (lane != 0 && !(pianoKeyTypes[lane - 1])) // black key on the left
        {
            if (!outline) {
                fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (lane * trackWidth / numLanes) -2 , SCREEN_HEIGHT - whiteHeight, (trackWidth / numLanes) + 2, whiteHeight, keyColor);                                 // draw the note and bleed it into the next lane a bit
                fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + ((lane - 1) * trackWidth / numLanes) + trackWidth /(2*numLanes) - 2, SCREEN_HEIGHT - whiteHeight + blackHeight, (trackWidth / numLanes) * 1 / 2 + 2, blackHeight, keyColor); // draw the note and bleed it into the next lane a bit
            }
            drawRect (SCREEN_WIDTH / 2 - trackWidth / 2 + ((lane - 1) * trackWidth / numLanes) + trackWidth /(2*numLanes) - 1, SCREEN_HEIGHT - whiteHeight, (trackWidth / numLanes) * 3 / 2 + 3, whiteHeight, outlineColor);               // draw the outline
            char leftCOlor = (pianoKeysPressed[lane - 1]) ? GREEN : BLACK;
            drawVLine(SCREEN_WIDTH / 2 - trackWidth / 2 + ((lane - 1) * trackWidth / numLanes) + trackWidth /(2*numLanes) - 1, SCREEN_HEIGHT - whiteHeight, blackHeight, leftCOlor);
        } 
        else // no black key on either side
        {
            if (!outline) {
                fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (lane * trackWidth / numLanes) -1, SCREEN_HEIGHT - whiteHeight, (trackWidth / numLanes) + 2, whiteHeight, keyColor);
            }
            drawRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (lane * trackWidth / numLanes), SCREEN_HEIGHT - whiteHeight, (trackWidth / numLanes) + 1, whiteHeight, outlineColor);
        }
    }
    else // black key
    {
        if (pianoKeysPressed[lane])
        {
            keyColor = GREEN;
        }
        else
        {
            keyColor = BLACK;
        }
        fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (lane * trackWidth / numLanes), SCREEN_HEIGHT - whiteHeight, trackWidth / numLanes, blackHeight, keyColor);     // draw the bottom of the note that moved down
        drawRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (lane * trackWidth / numLanes), SCREEN_HEIGHT - whiteHeight, trackWidth / numLanes, blackHeight, outlineColor); // draw the bottom of the note that moved down
    }
}

void draw_hitLine()
{
    // Draw the hit line -- the line that the notes must hit
    int singleTrackWidth = trackWidth / numLanes;
    drawHLine(SCREEN_WIDTH / 2 - trackWidth / 2, SCREEN_HEIGHT - hitHeight, trackWidth, WHITE);
    drawHLine(SCREEN_WIDTH / 2 - trackWidth / 2, SCREEN_HEIGHT - hitHeight + hitWidth, trackWidth, WHITE);
}

/**
 * @brief Spawns a note in the given lane
 * @param lane The lane to spawn the note in
 * @param color The color of the note
 * @param height The height of the note
 * @note Assumes that the lane is valid and that there is space in the lane
 */
void spawn_note(int lane, int color, int height, int sustain)
{
    // Spawn a note in the given lane
    if (activeNotesInLane[lane] < 50) // check if there is space in the lane
    {
        notes[lane][activeNotesInLane[lane]].lane = lane;
        notes[lane][activeNotesInLane[lane]].y = -height; // spawn at the top of the screen
        notes[lane][activeNotesInLane[lane]].height = height;
        notes[lane][activeNotesInLane[lane]].color = color;
        notes[lane][activeNotesInLane[lane]].hit = false;       // not hit yet
        notes[lane][activeNotesInLane[lane]].sustain = sustain; // not a sustained note (long press note)
        activeNotesInLane[lane]++;
        draw_piano(lane, 1); // draw the key on the screen
    }
}

/**
 * @brief Draws the falling notes
 * @param erase 1 to erase the notes at their top edge, 2 to erase the note at the bottom edge, 3 to erase the whole note, 0 to draw them
 */
void draw_notes(int erase)
{
    int singleTrackWidth = trackWidth / numLanes;
    // Draws the falling notes -- lines in between the two outer lines dictated by numLines
    for (int i = 0; i < numLanes; i++)
    {
        for (int j = 0; j < activeNotesInLane[i]; j++)
        {
            // Draw the note at its current position
            if (erase) // erase the note if needed
            {
                if (erase == 1)
                {                                                                                                                                                                      // erase only the top of the note that moved down
                    fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (i * trackWidth / numLanes) + noteSkinniness, notes[i][j].y, trackWidth / numLanes - noteSkinniness, gravity, BLACK); // erase the top of the note that moved down
                }
                else
                {                                                                                                                                                                                 // erase only the whole note
                    fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (i * trackWidth / numLanes) + noteSkinniness, notes[i][j].y, trackWidth / numLanes - noteSkinniness, notes[i][j].height, BLACK); // erase the whole note, subtract 10 to make it look better
                }
            }
            else
            {
                // FOR PIANO THIS IS FINE BUT FOR DRUM WE WILL NEED TO DELETE THE WHOLE NOTE ONCE IT IS HIT
                // if (!notes[i][j].hit) { // dont move the note down if its been hit
                // fillRect(SCREEN_WIDTH/2 - trackWidth/2 + (i*singleTrackWidth), notes[i][j].y, singleTrackWidth-5, notes[i][j].height, notes[i][j].color); // draw the whole note
                fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (i * trackWidth / numLanes) + noteSkinniness, notes[i][j].y + max(notes[i][j].height - gravity, 0), trackWidth / numLanes - noteSkinniness, gravity, notes[i][j].color); // draw the bottom of the note that moved down
                // }
                printf("Note %d in lane %d at y = %f, height = %d, hit_satus = %d\n", j, i, notes[i][j].y, notes[i][j].height, notes[i][j].hit); // print the note position for debugging
            }
        }
    }
}

/**
 * @brief Erase a single note from the screen as it falls
 * @param lane The lane the note is in
 * @param noteIndex The index of the note in the lane (in activeNotesInLane)
 */
void erase_note(int lane, int noteIndex)
{
    int singleTrackWidth = trackWidth / numLanes;
    // Erase the note at its current position
    // fillRect(SCREEN_WIDTH/2 - trackWidth/2 + (lane*singleTrackWidth) + noteSkinniness/2, notes[lane][noteIndex].y + notes[lane][noteIndex].height - gravity, singleTrackWidth-noteSkinniness, gravity, BLACK);  // erase the bottom of the note that moved down
    if (!notes[lane][noteIndex].sustain)
    {
        fillRect(SCREEN_WIDTH / 2 - trackWidth / 2 + (lane * trackWidth / numLanes) + noteSkinniness, notes[lane][noteIndex].y, trackWidth / numLanes - noteSkinniness, notes[lane][noteIndex].height, BLACK);
    } // erase the whole note if it is not a sustained note
    activeNotesInLane[lane]--;
    notes[lane][noteIndex] = notes[lane][activeNotesInLane[lane]]; // Move the last note to the current position
    if (activeNotesInLane[lane] <= 0) {
        draw_piano(lane, 0); // draw the key on the screen
    }
}

/**
 * @brief returns if the note is in the hit area
 */
bool check_hit(note noteObj)
{
    return ((noteObj.y + noteObj.height) > (SCREEN_HEIGHT - hitHeight)) && ((noteObj.y + noteObj.height) < (SCREEN_HEIGHT - hitHeight + hitWidth));
}

/**
 * @brief Updates the falling notes
 * Currently just moves them down the screen and deletes them once we hit the bottom
 */
void update_notes()
{
    // Update the falling notes -- move them down the screen
    for (int i = 0; i < numLanes; i++)
    {
        for (int j = activeNotesInLane[i] - 1; j >= 0; j--)
        {
            // Move the note down the screen
            notes[i][j].y += gravity;

            // if the bottom of the note is outside the hit area, make it smaller
            if ((notes[i][j].y + notes[i][j].height) > (SCREEN_HEIGHT - hitHeight + hitWidth))
            {
                notes[i][j].height -= gravity; // make the note smaller
            }

            // If the note is off the screen, remove it from the lane
            if (notes[i][j].y > SCREEN_HEIGHT - hitHeight + hitWidth)
            {
                // Remove the note from the lane
                erase_note(i, j);
                numNotesMissed++; // increment the number of notes missed
                if (maxCombo < combo)
                {
                    maxCombo = combo; // update the max combo counter
                }
                combo = 0; // reset the combo counter
                if (lives != -1) // if we are playing a song with lives
                {
                    lives--; // decrement the number of lives
                    // erase the last heart on the screen 
                    drawCharBig(10 + ((lives) * 20), 70, 0x14, WHITE, BLACK); // erase the last heart
                    
                    if (lives == 0)
                    {
                        play_mario_death(); // play the death sound
                        menu_state = 0; // go back to the main menu
                        setup = false; // reset the setup flag
                        // stop dma channel
                        dma_channel_abort(data_chan); // abort the channel
                        dma_channel_abort(ctrl_chan); // abort the channel
                        return;         // return to the main menu
                    }
                }

                // write perfect on the screen
                setCursor(SCREEN_WIDTH-100, 10);
                setTextColor2(WHITE, BLACK);
                setTextSize(2);
                writeString("MISS!!!");

                continue;         // skip the rest of the loop
            }

            // If the note has been hit, make it smaller
            if (notes[i][j].hit)
            {
                numNotesHit++;                                         // increment the number of notes hit
                notes[i][j].height -= gravity;                         // make the note smaller
                if (notes[i][j].height <= 0 || (!notes[i][j].sustain)) // if the note is gone, remove it from the lane
                {
                    erase_note(i, j);
                }
            }
        }
    }
}

// ========================================
// ============ Spawning thread ==========
// ========================================
static PT_THREAD(protothread_spawn_notes(struct pt *pt))
{
    PT_BEGIN(pt);
    const int maxHeight = SCREEN_HEIGHT / 4; // max height of the note
    while (1)
    {
        while (menu_state != 1)
        {
            PT_YIELD_usec(100000); // Yield for 100ms
        }
        // Spawn notes every 100ms
        int lane = rand() % numLanes; // Random lane
        int color = rand() % 16;      // Random color
        // int height = rand() % (maxHeight); // Random height
        int sustain = rand() % 2; // Random sustain (0 or 1)
        int height = hitWidth;    // Fixed height for now
        if (sustain)
        {
            color = YELLOW;
            height = 2 * hitWidth;
        }
        spawn_note(lane, color, height, sustain);
        PT_YIELD_usec(750000); // Yield for 100ms
    }

    PT_END(pt);
}

// ========================================
// ============ ANIMATION LOOP ============
// ========================================
static PT_THREAD(protothread_animation_loop(struct pt *pt))
{
    PT_BEGIN(pt);

    if (menu_state == 0)
    {
        if (!setup) {
            setup = true; // set the setup flag to true
            draw_menu(); // Draw the menu on the screen
            draw_cursor(0); // Draw the cursor on the screen
        }
    }
    else if (menu_state == 2)
    {
        if (!setup) {
            setup=true;
            draw_credits(); // Draw the credits on the screen
        }
    }
    else if (menu_state == 1)
    {
        if (!setup) {
            setup=true;
            pt_add_thread(protothread_spawn_notes);
            drawPicture(0, 0, (unsigned short *)vga_image, 640, 480); // Draw the picture on the screen
            draw_background();
            // Spawn notes
            // spawn_note(0, RED, 50);
            // spawn_note(1, GREEN, 50);
            // spawn_note(2, BLUE, 50);

            if (lives != -1) // if we are playing a song with lives
            {
                for (int i = 0; i < lives; i++)
                {
                    drawCharBig(10 + (i * 20), 70, 0x14, RED, RED); // draw the heart on the screen
                }
            }
        }
            draw_notes(1);
            update_notes();
            draw_notes(0);
            draw_hitLine();

            char notesTextBuffer[4];
            setCursor(130, 10);
            sprintf(notesTextBuffer, "%d", numNotesHit);
            writeString(notesTextBuffer);

            setCursor(170, 25);
            sprintf(notesTextBuffer, "%d", numNotesMissed);
            writeString(notesTextBuffer);

            setCursor(80, 40);
            sprintf(notesTextBuffer, "%d  ", combo);
            writeString(notesTextBuffer);

            setCursor(130, 55);
            sprintf(notesTextBuffer, "%d  ", maxCombo);
            writeString(notesTextBuffer);

            PT_YIELD_usec(30000); // Yield for 30ms
    }
    PT_END(pt);
}

// ================================================================================================================
// =====================================  END ANIMATION CODE ======================================================
// ================================================================================================================

// ================================================================================================================
// ========================================== BEGIN INPUT CODE ====================================================
// ================================================================================================================

// Keypad pin configurations
#define BASE_KEYPAD_PIN 9
#define KEYROWS 4
#define NUMKEYS 12
unsigned int keycodes[12] = {0x28, 0x11, 0x21, 0x41, 0x12, 0x22, 0x42, 0x14, 0x24, 0x44, 0x18, 0x48};
unsigned int scancodes[4] = {0x01, 0x02, 0x04, 0x08};
unsigned int button = 0x70;
int prev_key = -1;
int key_pressed = 0;

void key_pressed_callback(int key); // forward declaration of the key callback function
void key_released_callback();       // forward declaration of the key released callback function

// ===========================================
// ============= KEYPAD CODE ================
// ===========================================

void key_pressed_callback_game(int key); // forward declaration of the key callback function
void key_released_callback_game(int key); // forward declaration of the key released callback function
void key_pressed_callback(int key); // forward declaration of the key callback function
void key_released_callback(int key); // forward declaration of the key released callback function

static PT_THREAD(protothread_keypad_scan(struct pt *pt))
{
    // Initialize protothread and parameters
    PT_BEGIN(pt);
    static int i;
    static uint32_t keypad;

    // Main loop to handle keypad input
    while (1)
    {
        // Scan the keypad for a keypress
        for (i = 0; i < KEYROWS; i++)
        {
            gpio_put_masked((0xF << BASE_KEYPAD_PIN), (scancodes[i] << BASE_KEYPAD_PIN));
            sleep_us(1);
            keypad = ((gpio_get_all() >> BASE_KEYPAD_PIN) & 0x7F);
            if (keypad & button)
                break;
        }
        // If a key is pressed, find the key code
        if (keypad & button)
        {
            for (i = 0; i < NUMKEYS; i++)
            {
                if (keypad == keycodes[i])
                    break;
            }
            if (i == NUMKEYS)
                (i = -1);
        }
        // If no key is pressed, set i to -1
        else
        {
            i = -1;
        }

        // If a new key is pressed, "i" will be different than "prev_key"
        // if (i != prev_key) {

        // blink the LED if a key is pressed
        if (i != -1 && prev_key == i)
        {
            key_pressed_callback(i); // Call the key callback function
            key_pressed = 1;
        }
        else if (prev_key != i && key_pressed)
        {
            key_released_callback(prev_key); // Call the key released callback function
            key_pressed = 0;
        }
        prev_key = i;
        // }
        // Yield for 30ms
        PT_YIELD_usec(30000);
    }
    // End the protothread
    PT_END(pt);
}


// Mux Pins
#define MUX_SEL0 2
#define MUX_SEL1 3
#define MUX_SEL2 4
#define MUX_1 27
#define MUX_2 28
#define SIZE 13
#define MUX_1_CHECK 8
#define MUX_2_CHECK 5

static unsigned int prev_keys[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static unsigned int curr_keys[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static unsigned int new_keys[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static PT_THREAD(protothread_piano_scan(struct pt *pt))
{
    // Initialize protothread and parameters
    PT_BEGIN(pt);
    static int i;
    static int val_1;
    static int val_2;
    static int ind;

    gpio_init(MUX_SEL0);
    gpio_set_dir(MUX_SEL0, GPIO_OUT);
    gpio_put(MUX_SEL0, 0);

    gpio_init(MUX_SEL1);
    gpio_set_dir(MUX_SEL1, GPIO_OUT);
    gpio_put(MUX_SEL1, 0);

    gpio_init(MUX_SEL2);
    gpio_set_dir(MUX_SEL2, GPIO_OUT);
    gpio_put(MUX_SEL2, 0);

    gpio_init(MUX_1);
    gpio_set_dir(MUX_1, GPIO_IN);
    gpio_pull_down(MUX_1);

    gpio_init(MUX_2);
    gpio_set_dir(MUX_2, GPIO_IN);
    gpio_pull_down(MUX_2);

    // Main loop to handle keypad input
    while (1)
    {
        
        // scan through all of the options to see which one is pressed
        // see if the pressed ones are not pressed in previous check
        // if it is new, "key_pressed_callback(i);" on the new one
        // if it is not pressed anymore, "key_released_callback(i);" on it

        // Scan all of the keyboard keys if they are pressed
        for (int i = 0; i < 8; i++) {
            gpio_put(MUX_SEL0, (i >> 0) & 1);
            gpio_put(MUX_SEL1, (i >> 1) & 1);
            gpio_put(MUX_SEL2, (i >> 2) & 1);

            sleep_us(5);

            int val_1 = gpio_get(MUX_1); // read the value of the first mux pin
            curr_keys[i] = val_1; // store the value in the current keys array

            if (i < MUX_2_CHECK) {
                int val_2 = gpio_get(MUX_2); // read the value of the second mux pin
                int ind = i + 8;
                curr_keys[ind] = val_2; // store the value in the current keys array
            }

        }

        for (int i = 0; i < SIZE; i++) {
            if (curr_keys[i] != prev_keys[i]) {
                if (curr_keys[i] == 1) {
                    key_pressed_callback(i + 1); // Call the key callback function
                    key_pressed = 1;
                }
                else {
                    key_released_callback(i + 1); // Call the key released callback function
                    key_pressed = 0;
                }
            }
            prev_keys[i] = curr_keys[i]; // Copy the current keys to the previous keys for the next iteration
        }

        printf("Key pressed: %d %d %d %d %d %d %d %d %d %d %d %d %d\n", prev_keys[0], prev_keys[1], prev_keys[2], prev_keys[3], prev_keys[4], prev_keys[5], prev_keys[6], prev_keys[8], prev_keys[9], prev_keys[10], prev_keys[11], prev_keys[12]); // Print the key pressed for debugging

        PT_YIELD_usec(30000);
    }
    // End the protothread
    PT_END(pt);
}


/**
 * @brief callback for key release
 */
void key_released_callback(int key)
{
    // printf("Key released: %d\n", prev_key); // Print the key released for debugging
    if (menu_state == 1) // if we are in the game
    {
        key_released_callback_game(key); // Call the key released callback function
    }
    else if (menu_state == 0) // if we are in the menu
    {
        if (key == 1)
        {
            draw_cursor(1); // erase the cursor on the screen
            menu_selection = (menu_selection + 1) % 4; // Move down the menu
            draw_cursor(0); // draw the cursor on the screen
        }
        else if (key == 2)
        {
            draw_cursor(1); // erase the cursor on the screen
            menu_selection = (menu_selection - 1 + 4) % 4; // Move up the menu
            draw_cursor(0); // draw the cursor on the screen
        }
        else if (key == 3)
        {
            if (menu_selection == 0)
            {
                menu_state = 1; // Start the game
                draw_background(); // Draw the background for the game
                draw_hitLine();    // Draw the hit line for the game
                setup = false; // reset the setup flag
            }
            else if (menu_selection == 1)
            {
                menu_state = 1; // Start the game with lives
                lives = 3;
                draw_background(); // Draw the background for the game
                draw_hitLine();    // Draw the hit line for the game
                setup = false; // reset the setup flag
            }
            else if (menu_selection == 2)
            {
                menu_state = 1; // Start the game with 12 lanes
                draw_background(); // Draw the background for the game
                draw_hitLine();    // Draw the hit line for the game
                setup = false; // reset the setup flag
            }
            else if (menu_selection == 3)
            {
                menu_state = 2; // Show credits
                draw_credits(); // Draw the credits on the screen
                setup = false; // reset the setup flag
            }
        }
    }
    else if (menu_state == 2) // if we are in the credits
    {
        menu_state = 0; // Go back to the main menu
        draw_menu();    // Draw the main menu
        setup = false; // reset the setup flag
    }
}

/**
 * @brief callback for key press
 */
void key_pressed_callback(int key)
{
    if (menu_state == 1) // if we are in the game
    {
        key_pressed_callback_game(key); // Call the key callback function
    }
}

/**
 * @brief callback for key press while playing the game
 */
void key_pressed_callback_game(int key)
{
    key = key - 1; // convert to 0-indexed key
    // Check if the key pressed is valid
    if (key >= 0 && key < numLanes)
    {
        // Make key pressed true
        pianoKeysPressed[key] = true;
        // Draw the key pressed on the screen
        draw_piano(key, 0); // draw the piano keys on the screen
        
        // Check if there are any notes in the lane
        if (activeNotesInLane[key] > 0)
        {
            // printf ("key pos: %f\n", notes[key][activeNotesInLane[key]-1].y); // Print the position of the key pressed
            // printf ("Key is low enough %d\n", notes[key][activeNotesInLane[key]-1].y > (SCREEN_HEIGHT - hitHeight));
            // printf ("Key is high enough %d\n", notes[key][activeNotesInLane[key]-1].y < (SCREEN_HEIGHT - hitHeight + hitWidth)); // Print the position of the key pressed
            // Check if the note is in the correct position
            for (int i = 0; i < activeNotesInLane[key]; i++)
            {
                note noteKey = notes[key][i];
                // Check if the note is in the correct position
                if (check_hit(noteKey))
                {
                    notes[key][i].hit = true; // mark the note as hit
                    play_sound();             // play sound
                    combo++;                  // increment the combo counter
                    if (abs(notes[key][i].y + notes[key][i].height - (SCREEN_HEIGHT - hitHeight + hitWidth)) < 20) // if the note is hit perfectly
                    {
                        // write perfect on the screen
                        setCursor(SCREEN_WIDTH-100, 10);
                        setTextColor2(WHITE, BLACK);
                        setTextSize(2);
                        writeString("PERFECT!");
                    }
                    else if (abs(notes[key][i].y + notes[key][i].height - (SCREEN_HEIGHT - hitHeight + hitWidth)) < 30) // if the note is hit well
                    {
                        // write GOOD on the screen
                        setCursor(SCREEN_WIDTH-100, 10);
                        setTextColor2(WHITE, BLACK);
                        setTextSize(2);
                        writeString("GOOD!");
                    }
                    else // if the note is hit poorly
                    {
                        // write GOOD on the screen
                        setCursor(SCREEN_WIDTH-100, 10);
                        setTextColor2(WHITE, BLACK);
                        setTextSize(2);
                        writeString("BAD!");
                    }
                }
            }
        }
    }
}

/**
 * @brief callback for key release while playing the game
 */
void key_released_callback_game(int key)
{
    key = key - 1; // convert to 0-indexed key
    // Check if the key pressed is valid
    if (key >= 0 && key < numLanes)
    {
        // Make key pressed false
        pianoKeysPressed[key] = false;
        // Draw the key pressed on the screen
        draw_piano(key, 0); // draw the piano keys on the screen
        printf("Key released: %d\n", key); // Print the key released for debugging
        
        // Check if there are any notes in the lane
        if (activeNotesInLane[key] > 0)
        {
            // printf ("key pos: %f\n", notes[key][activeNotesInLane[key]-1].y); // Print the position of the key pressed
            // printf ("Key is low enough %d\n", notes[key][activeNotesInLane[key]-1].y > (SCREEN_HEIGHT - hitHeight));
            // printf ("Key is high enough %d\n", notes[key][activeNotesInLane[key]-1].y < (SCREEN_HEIGHT - hitHeight + hitWidth)); // Print the position of the key pressed
            // Check if the note is in the correct position
            for (int i = 0; i < activeNotesInLane[key]; i++)
            {
                note noteKey = notes[key][i];
                // Check if the note is in the correct position
                // if (check_hit(noteKey))
                // {
                notes[key][i].hit = false; // mark the note as hit
                // }
            }
        }
    }
}

// ================================================================================================================
// ========================================== END INPUT CODE ======================================================
// ================================================================================================================

// ===========================================
// =============== LED BLINKY ================
// ===========================================
static PT_THREAD(protothread_blinky(struct pt *pt))
{
    // used to tell if the program is running

    // Initialize protothread and parameters
    PT_BEGIN(pt);
    static int i = 0;

    // Main loop to blink the LED
    while (1)
    {
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
    // gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);

    // initialize the DAC
    // Build sine table and DAC data table
    // int i;
    // for (i = 0; i < (sine_table_size); i++)
    // {
    //     raw_sin[i] = (int)(2047 * sin((float)i * 6.283 / (float)sine_table_size) + 2047); // 12 bit
    //     DAC_data[i] = DAC_config_chan_B | (raw_sin[i] & 0x0fff);
    // }

    // // Select DMA channels
    data_chan = dma_claim_unused_channel(true);
    ;
    ctrl_chan = dma_claim_unused_channel(true);
    ;

    // Setup the control channel
    dma_channel_config c = dma_channel_get_default_config(ctrl_chan); // default configs
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);           // 32-bit txfers
    channel_config_set_read_increment(&c, false);                     // no read incrementing
    channel_config_set_write_increment(&c, false);                    // no write incrementing
    channel_config_set_chain_to(&c, data_chan);                       // chain to data channel

    dma_channel_configure(
        ctrl_chan,                        // Channel to be configured
        &c,                               // The configuration we just created
        &dma_hw->ch[data_chan].read_addr, // Write address (data channel read address)
        &address_pointer2,                // Read address (POINTER TO AN ADDRESS)
        1,                                // Number of transfers
        false                             // Don't start immediately
    );

    // Setup the data channel
    c2 = dma_channel_get_default_config(data_chan); // Default configs
    channel_config_set_transfer_data_size(&c2, DMA_SIZE_16);           // 16-bit txfers
    channel_config_set_read_increment(&c2, true);                      // yes read incrementing
    channel_config_set_write_increment(&c2, false);                    // no write incrementing
    // (X/Y)*sys_clk, where X is the first 16 bytes and Y is the second
    // sys_clk is 125 MHz unless changed in code. Configured to ~22 kHz
    dma_timer_set_fraction(0, 0x000B, 0xffff);
    // 0x3b means timer0 (see SDK manual)
    channel_config_set_dreq(&c2, 0x3b); // DREQ paced by timer 0
    // chain to the controller DMA channel

    // VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
    channel_config_set_chain_to(&c2, ctrl_chan); // Chain to control channel COMMENT OUT TO PREVENT LOOPING
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

    dma_channel_configure(
        data_chan,                 // Channel to be configured
        &c2,                       // The configuration we just created
        &spi_get_hw(SPI_PORT)->dr, // write address (SPI data register)
        DAC_data,                  // The initial read address
        transfer_count,            // Number of transfers
        false                      // Don't start immediately.
    );

    // initialize VGA
    initVGA();

    // Map LED to GPIO port, make it low
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    gpio_put(LED, 0);

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
    // pt_add_thread(protothread_menu_screen);
    pt_add_thread(protothread_blinky);
    pt_add_thread(protothread_keypad_scan);
    pt_add_thread(protothread_piano_scan);
    // Start scheduling core 0 threads
    pt_schedule_start;
}