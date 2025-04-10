/**
 * Hunter Adams (vha3@cornell.edu)
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

// the color of the boid
char color = WHITE;

// gravity value
const fix15 gravity = float2fix15(0.75);

// bounciness
fix15 bounciness = float2fix15(0.5);

// const int layers = 16;
// total_pegs = layers*(layers+1)/2; (for 16 layers its 136 pegs)
// pegs[i][0] = x, pegs[i][1] = y
fix15 pegs[136][2];
int hitPegs[136];

// initializing dma channels
int data_chan;
int ctrl_chan;

// Creating ball struct and array
struct ball
{
  fix15 x;
  fix15 y;
  fix15 vx;
  fix15 vy;
  fix15 last_x;
  fix15 last_y;
  char color;
  int last_peg;
};

const int max_balls = 3000;
struct ball balls[3000]; // creating a an array of balls with an arbitrarily large size
// SIZE OF balls HAS TO MATCH max_balls

volatile int num_balls = 2100; // keep track of total balls that we want to render
volatile int numBalls = 0;     // keep track of total fallen balls

// used to optimize text drawing
int prev_num_balls = 0;
int prev_numBalls = -1;
fix15 prev_bounciness = -1;
int prev_flag = -1;

// Used for button press (prevents holding down button and rapidly switching states)
int prev_pin13 = -1;

// used to keep track of how many balls are currently on the screen
volatile int current_balls = 0;

// Time elapsed since beginning of program
int time = 0;

// used for histogram (size must be 1 + num layers)
int histogram[17];
int prev_heights[17];

// potentiometer value
uint16_t pot_value;
uint16_t prev_pot_value;

// used for button press and switching between balls and bounciness parameters
bool flag = false;

// Random Uniform Function created by Bruce Lands
// Used in generating a random x velocity for each ball
// ==========================================================
//  fixed s15x16 uniform -1 to 1
// ==========================================================
// Excution time is 5 uSec
// ==========================================================
#define s15x16_one 0x00010000
// AND one pass through rand() function
fix15 uniform_rand(void)
{
  int k, random;
  int random_bit1;
  fix15 uniform;
  volatile uint32_t *rnd_reg = (uint32_t *)(ROSC_BASE + ROSC_RANDOMBIT_OFFSET);
  //
  for (k = 0; k < 32; k++)
  {
    // at least one nop need to be here for timing
    asm("nop");
    asm("nop"); // adds one microsec to execution
    // asm("nop") ; asm("nop") ;
    random_bit1 = 0x00000001 & (*rnd_reg);
    random = (random << 1) | random_bit1;
  }
  srand(random);
  random = rand();
  uniform = (fix15)(random & 0x1ffff) - s15x16_one;
  return uniform;
}

// Spawn balls from top of screen unitl we reach the desired number of balls (num_balls)
void spawnBoids()
{
  // Spawn num_balls boids
  for (int i = current_balls; i < num_balls; i++)
  {
    // Start in center of screen
    struct ball boid;
    // Height = 480, Width = 640
    boid.x = int2fix15(320);
    boid.y = int2fix15(0);
    boid.vx = uniform_rand();
    boid.vy = int2fix15(2);
    boid.last_x = boid.x;
    boid.last_y = boid.y;
    boid.last_peg = -1;

    if (current_balls % 4 == 0)
    {
      boid.color = GREEN;
    }
    else if (current_balls % 4 == 1)
    {
      boid.color = BLUE;
    }
    else if (current_balls % 4 == 2)
    {
      boid.color = YELLOW;
    }
    else
    {
      boid.color = RED;
    }

    balls[current_balls] = boid;
    current_balls++;
  }
}

// Create the pegs array with the locations of each peg
void intializePegs(int num_layers)
{
  // layer 1 has index 0
  num_layers--;
  int ind = 0;
  for (int i = 0; i <= num_layers; i++)
  {
    for (int j = -i; j <= i; j += 2)
    {
      // Each peg has a horizontal and vertical spacing 19 pixels from center to center
      pegs[ind][0] = int2fix15(320 + 19 * j);
      pegs[ind][1] = int2fix15(40 + 19 * i);
      ind++;
    }
  }
}

// Setting the color of each peg
// Creating a christmas tree peg board
char rgb_values[136];
int pegColor()
{
  // Array to hold RGB values for 136 balls

  int index = 0;

  // Loop through each row (16 rows total)
  for (int row = 0; row < 16; row++)
  {
    // Number of balls in this row
    int num_balls = row + 1;

    // Assign colors based on row
    for (int ball = 0; ball < num_balls; ball++)
    {
      if (row == 0)
      {
        // Top of the tree (star)
        rgb_values[index] = YELLOW;
      }
      else if (row >= 13)
      {
        // Last few rows (tree trunk)
        // Calculate the middle position(s)
        int middle = num_balls / 2;

        // Set middle ball(s) to brown
        if (ball % 2 == 1)
        {
          // Odd number of balls: one middle ball
          if (ball == middle)
          {
            rgb_values[index] = DARK_GREEN;
          }
          else
          {
            rgb_values[index] = WHITE;
          }
        }
        else
        {
          // Even number of balls: two middle balls
          if (ball == middle - 1 || ball == middle)
          {
            rgb_values[index] = DARK_GREEN;
          }
          else
          {
            rgb_values[index] = WHITE;
          }
        }
      }
      else
      {
        //  (green and red ornaments)
        if ((row + ball) % 3 == 0)
        {
          rgb_values[index] = GREEN;
        }
        else if ((row + ball) % 3 == 1)
        {
          rgb_values[index] = BLUE;
        }
        else
        {
          rgb_values[index] = RED;
        }
      }
      index++;
    }
  }
}

// Draw all pegs
void drawAllPegs(fix15 pegs[][2], int num_pegs)
{
  for (int i = 0; i < num_pegs; i++)
  {
    fillCircle(fix2int15(pegs[i][0]), fix2int15(pegs[i][1]), 6, rgb_values[i]);
  }
}

// Draw the hit pegs
void drawHitPegs(int hitpegs[], int num_pegs)
{
  for (int i = 0; i < num_pegs; i++)
  {
    if (hitpegs[i] != -1)
    {
      fillCircle(fix2int15(pegs[i][0]), fix2int15(pegs[i][1]), 6, rgb_values[i]);
    }
  }
}

// Draw the boundaries
void drawArena()
{

  // Write number of currently rendered balls to screen
  if (prev_num_balls != current_balls)
  {
    fillRect(150, 10, 50, 10, BLACK);
    setCursor(150, 10);
    char buffer[32];
    sprintf(buffer, "%d", current_balls);
    writeString(buffer);
    prev_num_balls = current_balls;
  }

  // Write number of balls fallen to screen
  if (prev_numBalls != numBalls)
  {
    setCursor(150, 20);
    char buffer2[32];
    sprintf(buffer2, "%d", numBalls);
    writeString(buffer2);
    prev_numBalls = numBalls;
  }

  // Write time elapsed to screen
  setCursor(150, 30);
  char buffer3[32];
  sprintf(buffer3, "%d", time);
  writeString(buffer3);

  // Write bounciness value to screen
  if (prev_bounciness != bounciness)
  {
    setCursor(150, 40);
    char buffer4[32];
    sprintf(buffer4, "%.3f", fix2float15(bounciness));
    writeString(buffer4);
    prev_bounciness = bounciness;
  }

  // Write the currently changing parameter to screen
  if (prev_flag != flag)
  {
    setCursor(150, 50);
    char buffer5[32];
    if (flag)
    {
      sprintf(buffer5, "Num Balls ");
    }
    else
    {
      sprintf(buffer5, "Bounciness");
    }
    writeString(buffer5);
    prev_flag = flag;
  }
}

// Add this function to draw the histogram
void drawHistogram()
{
  const int BASE_Y = 380; // Bottom of screen
  const int PEG_SPACING = 38;
  const int NUM_BINS = 17;
  const int NUM_LAYERS = 16;
  const int LAST_LAYER_IDX = NUM_LAYERS - 1;
  const int max_height = 50;
  const int screen_height = 480;
  int min = 320 - 19 * LAST_LAYER_IDX;

  // Find max value in histogram
  int max = 0;
  for (int i = 0; i < NUM_BINS; i++)
  {
    if (histogram[i] > max)
    {
      max = histogram[i];
    }
  }
  fix15 max_fix = int2fix15(max);

  // Draw the changes in each histogram bin
  for (int i = 0; i < NUM_BINS; i++)
  {
    int width = PEG_SPACING;
    int x = (i - 1) * PEG_SPACING + min;

    // Make the first bin extend to the end of the screen
    if (i == 0)
    {
      width = min;
      x = 0;
    }

    // TODO SWITCH TO FIX15
    float scale = fix2float15(divfix(int2fix15(histogram[i]), max_fix));
    float height = (short)(scale * max_height); // calculate height of histogram bar
    // Only draw a change if the height has changed & only draw the difference in height
    if (prev_heights[i] != height)
    {
      if (prev_heights[i] > height)
      {
        fillRect(x, screen_height - prev_heights[i], width, prev_heights[i] - height, BLACK);
      }
      else
      {
        fillRect(x, screen_height - height, width, height - prev_heights[i], PINK);
      }
      prev_heights[i] = height;
    }
  }
}

// Quake inverse square root (1/sqrt(x))
fix15 Q_rsqrt(float number)
{
  long i;
  float x2, y;
  const float threehalfs = 1.5F;

  x2 = number * 0.5F;
  y = number;
  i = *(long *)&y;           // evil floating point bit level hacking
  i = 0x5f3759df - (i >> 1); // what the fuck?
  y = *(float *)&i;
  y = y * (threehalfs - (x2 * y * y)); // 1st iteration
                                       //	y  = y * ( threehalfs - ( x2 * y * y ) );   // 2nd iteration, this can be removed

  return float2fix15(y);
}

// Detect wallstrikes, update velocity and position
void wallsAndEdges(void)
{
  int last_current = current_balls;
  // loop through all balls
  for (int j = last_current - 1; j >= 0; j--)
  {
    balls[j].last_x = balls[j].x;
    balls[j].last_y = balls[j].y;
    for (int i = 0; i < 136; i++)
    {
      // clear hit peg
      hitPegs[i] == -1;

      fix15 xDiff = balls[j].x - pegs[i][0];
      fix15 yDiff = balls[j].y - pegs[i][1];
      // Check if the ball is within ball+peg radius distance to see if it could have collided
      if (absfix15(xDiff) < int2fix15(8) && absfix15(yDiff) < int2fix15(8))
      {

        // fix15 dist = float2fix15(sqrt(fix2float15(multfix15(xDiff, xDiff) + multfix15(yDiff, yDiff))));
        fix15 dist = divfix(int2fix15(1), Q_rsqrt(fix2float15(multfix15(xDiff, xDiff) + multfix15(yDiff, yDiff))));
        fix15 normX = divfix(xDiff, dist);
        fix15 normY = divfix(yDiff, dist);

        // -2 * (V dot N)
        fix15 intTerm = multfix15(int2fix15(-2), multfix15(balls[j].vx, normX) + multfix15(balls[j].vy, normY));

        // If intTerm is positive, the ball is moving towards the peg so its a collision
        if (intTerm > int2fix15(0))
        {
          // teleport ball outside of the peg
          balls[j].x = pegs[i][0] + multfix15(normX, dist + int2fix15(1));
          balls[j].y = pegs[i][1] + multfix15(normY, dist + int2fix15(1));

          // Reflect balls velocity in direction of normal -2(v dot n)n + v
          balls[j].vx = balls[j].vx + multfix15(normX, intTerm);
          balls[j].vy = balls[j].vy + multfix15(normY, intTerm);

          // store hit peg
          hitPegs[i] = i;

          if (balls[j].last_peg != i)
          {
            // *thunk*
            if (dma_channel_is_busy(data_chan))
            {
              dma_channel_wait_for_finish_blocking(ctrl_chan);
            }
            dma_start_channel_mask(1u << ctrl_chan);

            // Scale down velocity -- loses energy as it bounces
            balls[j].vx = multfix15(balls[j].vx, bounciness);
            balls[j].vy = multfix15(balls[j].vy, bounciness);
            balls[j].last_peg = i;
          }
        }
      }
    }

    if (hitBottom(balls[j].y))
    {

      // histogram logic

      // Determine which column the ball landed in
      const int NUM_LAYERS = 16;
      const int LAST_LAYER_IDX = NUM_LAYERS - 1;
      int ball_x_screen = fix2int15(balls[j].x);

      int bin_idx;

      // Find corresponding bin (0-15)
      // Peg 0 = 320 - 19*15
      // Peg 15 = 320 + 19*15
      int min = 320 - 19 * LAST_LAYER_IDX;
      int max = 320 + 19 * LAST_LAYER_IDX;

      if (ball_x_screen < min)
      {
        bin_idx = 0;
      }
      else if (ball_x_screen > max)
      {
        bin_idx = 16;
      }
      else
      {
        bin_idx = ((ball_x_screen - min) / 38) + 1; // 38 comes from the spacing between pegs being 19px
      }

      // Update histogram
      histogram[bin_idx]++;

      // respawn a ball
      if (current_balls <= num_balls)
      {
        balls[j].x = int2fix15(320);
        balls[j].y = int2fix15(0);
        balls[j].vx = uniform_rand();
        balls[j].vy = int2fix15(2);
      }

      // despawn a ball
      else
      {
        // drawCircle(fix2int15(balls[j].x), fix2int15(balls[j].y), 2, BLACK);
        drawRect(fix2int15(balls[j].x), fix2int15(balls[j].y), 2, 2, BLACK);

        // replace despawned ball's place with the last ball
        balls[j] = balls[current_balls - 1];
        current_balls--;
      }
      numBalls++;
    }

    // add gravity
    balls[j].vy = balls[j].vy + gravity;

    // Update position using velocity
    balls[j].x = balls[j].x + balls[j].vx;
    balls[j].y = balls[j].y + balls[j].vy;
  }
}

// ========================================
// ======== DMA CHANNEL SOUND CODE ========
// ========================================

// Number of samples per period in sine table
#define sine_table_size 256

// Sine table
int raw_sin[sine_table_size];

// Table of values to be sent to DAC
unsigned short DAC_data[sine_table_size];

// Pointer to the address of the DAC data table
unsigned short *address_pointer2 = &DAC_data[0];

// A-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

// SPI configurations
#define PIN_MISO 4
#define PIN_CS 5
#define PIN_SCK 6
#define PIN_MOSI 7
#define SPI_PORT spi0

// Number of DMA transfers per event
const uint32_t transfer_count = sine_table_size;

// ========================================
// === HISTROGRAM CODE ====================
// ========================================
static PT_THREAD(protothread_holo(struct pt *pt))
{
  PT_BEGIN(pt);
  // Vertical lines for last row of pegs
  const int NUM_LAYERS = 16; // Match the value passed to intializePegs()
  const int LAST_LAYER_IDX = NUM_LAYERS - 1;

  while (1)
  {
    drawHitPegs(hitPegs, 136);
    drawHistogram();
    PT_YIELD_usec(100000);
  }

  PT_END(pt);
}

// ==================================================
// === Potentiometer input thread ===================
// ==================================================
static PT_THREAD(protothread_pot(struct pt *pt))
{
  PT_BEGIN(pt);
  // Stores the potentiometer valuea
  // unit16_t zero = 0;

  while (1)
  {
    // Read the potentiometer value
    // The potentiometer value is between 0 and 4096
    pot_value = adc_read();
    pot_value = pot_value >> 8; // 12 bit to 4 bit to reduce noise
    pot_value = pot_value * 256;

    // Check if the potentiometer value has changed
    if (pot_value != prev_pot_value)
    {
      // If flag is true then we are changing num balls
      if (flag)
      {
        // The potentiometer was fickle towards the 0 end, so set a threshold for having 0 balls
        int min = 50;

        if (pot_value < min)
        {
          num_balls = 1;
          pot_value = 0;
        }
        // If the potentiometer value is greater than the max number of balls, set the number of balls to the max
        else if (pot_value > max_balls)
        {
          num_balls = max_balls - min;
        }
        // Otherwise, set the number of balls to the potentiometer value
        else
        {
          num_balls = pot_value - min;
        }
      }
      // If flag is false then we are changing bounciness
      else
      {
        // Set bounciness to the potentiometer value/4096 (sets bouncnniness between 0 and 1)
        bounciness = divfix(int2fix15(pot_value), int2fix15(4096));
      }

      // clear histogram whenever the potentiometer value changes
      if (pot_value != prev_pot_value)
      {
        for (int i = 0; i < 17; i++)
        {
          histogram[i] = 0;
        }
        prev_pot_value = pot_value;
      }
    }

    // Yield for 0.5 seconds
    PT_YIELD_usec(500000);
  }
  PT_END(pt);
}

// Incrementing our seconds elapsed since beginning of program
static PT_THREAD(protothread_time(struct pt *pt))
{
  PT_BEGIN(pt);
  PT_YIELD_usec(1000000);
  time++;
  PT_END(pt);
} // animation thread

// Animation on core 0
static PT_THREAD(protothread_anim(struct pt *pt))
{
  // Mark beginning of thread
  PT_BEGIN(pt);

  // Variables for maintaining frame rate
  static int begin_time;
  static int spare_time;

  // Initialize pegs
  intializePegs(16);

  // spawn a peg
  drawAllPegs(pegs, 136);

  // initialize the peg colors
  pegColor();

  while (1)
  {
    // Measure time at start of thread
    begin_time = time_us_32();

    // update boid's position and velocity
    wallsAndEdges();

    // draw the boid at its new position
    for (int i = 0; i < current_balls; i++)
    {
      drawRect(fix2int15(balls[i].last_x), fix2int15(balls[i].last_y), 2, 2, BLACK);
      drawRect(fix2int15(balls[i].x), fix2int15(balls[i].y), 2, 2, balls[i].color);
      // drawCircle(fix2int15(balls[i].last_x), fix2int15(balls[i].last_y), 2, BLACK);
      // drawCircle(fix2int15(balls[i].x), fix2int15(balls[i].y), 2, color);
    }

    // spawn balls if we have less than num_balls
    if (current_balls < num_balls)
    {
      spawnBoids();
    }

    // draw the boundaries
    drawArena();

    // detect button press for switching between changning num_balls or bounciness
    if (gpio_get(13) == 1 && prev_pin13 != gpio_get(13)) // using prev_pin to detect holding down a button press/debouncing
    {
      flag = !flag;
    }
    prev_pin13 = gpio_get(13);

    // delay in accordance with frame rate
    spare_time = FRAME_RATE - (time_us_32() - begin_time);

    // set LED on if spare time is negative
    if (spare_time < 0)
    {
      gpio_put(25, 1);
    }
    else
    {
      gpio_put(25, 0);
    }

    // yield for necessary amount of time
    PT_YIELD_usec(spare_time);
    // NEVER exit while
  } // END WHILE(1)
  PT_END(pt);
} // animation thread

// ========================================
// === main
// ========================================
// USE ONLY C-sdk library
int main()
{
  // set_sys_clock_khz(250000, true);

  // initialize stio
  stdio_init_all();

  // initialize VGA
  initVGA();

  // initialize DMA
  // --------------------------------------------------------
  // Initidalize stdio
  stdio_init_all();

  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SPI_PORT, 20000000);

  // Format SPI channel (channel, data bits per transfer, polarity, phase, order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);

  // Map SPI signals to GPIO ports, acts like framed SPI with this CS mapping
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

  // Build sine table and DAC data table
  int i;
  for (i = 0; i < (sine_table_size); i++)
  {
    raw_sin[i] = (int)(2047 * sin((float)i * 6.283 / (float)sine_table_size) + 2047); // 12 bit
    DAC_data[i] = DAC_config_chan_B | (raw_sin[i] & 0x0fff);
  }

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
  dma_channel_config c2 = dma_channel_get_default_config(data_chan); // Default configs
  channel_config_set_transfer_data_size(&c2, DMA_SIZE_16);           // 16-bit txfers
  channel_config_set_read_increment(&c2, true);                      // yes read incrementing
  channel_config_set_write_increment(&c2, false);                    // no write incrementing
  // (X/Y)*sys_clk, where X is the first 16 bytes and Y is the second
  // sys_clk is 125 MHz unless changed in code. Configured to ~44 kHz
  dma_timer_set_fraction(0, 0x0017, 0xffff);
  // 0x3b means timer0 (see SDK manual)
  channel_config_set_dreq(&c2, 0x3b); // DREQ paced by timer 0
  // chain to the controller DMA channel
  // channel_config_set_chain_to(&c2, ctrl_chan); // Chain to control channel

  dma_channel_configure(
      data_chan,                 // Channel to be configured
      &c2,                       // The configuration we just created
      &spi_get_hw(SPI_PORT)->dr, // write address (SPI data register)
      DAC_data,                  // The initial read address
      sine_table_size,           // Number of transfers
      false                      // Don't start immediately.
  );
  // --------------------------------------------------------

  // DRAWING TEXT
  // ------------------------------------
  setTextColor2(WHITE, BLACK);
  setTextSize(1);

  setCursor(65, 10);
  writeString("Current Balls: ");

  setCursor(65, 20);
  writeString("Total Balls: ");

  setCursor(65, 30);
  writeString("Time Elapsed: ");

  setCursor(65, 40);

  // detect button press that is connected to gpio 10 and 13
  gpio_init(10);
  gpio_set_dir(10, GPIO_OUT);
  gpio_put(10, 1);

  gpio_init(13);
  gpio_set_dir(13, GPIO_IN);

  // add threads
  pt_add_thread(protothread_anim);
  pt_add_thread(protothread_holo);
  pt_add_thread(protothread_pot);
  pt_add_thread(protothread_time);

  // start scheduler
  pt_schedule_start;
}