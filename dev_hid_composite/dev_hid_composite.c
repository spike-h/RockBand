/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

 #include <stdlib.h>
 #include <stdio.h>
 #include <string.h>
 
 #include "bsp/board_api.h"
 #include "tusb.h"
 #include "hardware/gpio.h"
 
 #include "usb_descriptors.h"
 
 //--------------------------------------------------------------------+
 // MACRO CONSTANT TYPEDEF PROTYPES
 //--------------------------------------------------------------------+
 
 /* Blink pattern
  * - 250 ms  : device not mounted
  * - 1000 ms : device mounted
  * - 2500 ms : device is suspended
  */
 enum  {
   BLINK_NOT_MOUNTED = 250,
   BLINK_MOUNTED = 1000,
   BLINK_SUSPENDED = 2500,
 };
 
 static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
 
 void led_blinking_task(void);
 void hid_task(void);
 
 // Button setup
 #define BUTTON_PIN 19
 static bool last_button_state = false;
 
 // Initialize button GPIO
 void button_init(void) {
     gpio_init(BUTTON_PIN);
     gpio_set_dir(BUTTON_PIN, GPIO_IN);
     gpio_pull_up(BUTTON_PIN); // Use pull-up so button connects to ground
 }
 
 // Read button state (active low)
 bool button_read(void) {
     return !gpio_get(BUTTON_PIN); // Invert because we're using pull-up
 }
 
 /*------------- MAIN -------------*/
 int main(void)
 {
   board_init();
   button_init(); // Initialize our button GPIO
 
   // init device stack on configured roothub port
   tud_init(BOARD_TUD_RHPORT);
 
   if (board_init_after_tusb) {
     board_init_after_tusb();
   }
 
   while (1)
   {
     tud_task(); // tinyusb device task
    //  led_blinking_task();
 
     hid_task();

     //blink if button is pressed
        if (button_read()) {
            board_led_write(1); // Turn on LED
        } else {
            board_led_write(0); // Turn off LED
        }
   }
 }
 
 //--------------------------------------------------------------------+
 // Device callbacks
 //--------------------------------------------------------------------+
 
 // Invoked when device is mounted
 void tud_mount_cb(void)
 {
   blink_interval_ms = BLINK_MOUNTED;
 }
 
 // Invoked when device is unmounted
 void tud_umount_cb(void)
 {
   blink_interval_ms = BLINK_NOT_MOUNTED;
 }
 
 // Invoked when usb bus is suspended
 // remote_wakeup_en : if host allow us  to perform remote wakeup
 // Within 7ms, device must draw an average of current less than 2.5 mA from bus
 void tud_suspend_cb(bool remote_wakeup_en)
 {
   (void) remote_wakeup_en;
   blink_interval_ms = BLINK_SUSPENDED;
 }
 
 // Invoked when usb bus is resumed
 void tud_resume_cb(void)
 {
   blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
 }
 
 //--------------------------------------------------------------------+
 // USB HID
 //--------------------------------------------------------------------+
 
 static void send_hid_report(uint8_t report_id, bool btn_pressed)
 {
   // skip if hid is not ready yet
   if ( !tud_hid_ready() ) return;
 
   switch(report_id)
   {
     case REPORT_ID_CONSUMER_CONTROL:
     {
       static bool has_consumer_key = false;
 
       // Only send report on button press (not release)
       if (btn_pressed && !last_button_state)
       {
         // Send play/pause command
         uint16_t play_pause = HID_USAGE_CONSUMER_PLAY_PAUSE;
         tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &play_pause, 2);
         has_consumer_key = true;
       }
       else if (!btn_pressed && has_consumer_key)
       {
         // send empty key report (release key)
         uint16_t empty_key = 0;
         tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &empty_key, 2);
         has_consumer_key = false;
       }
     }
     break;
 
     default: break;
   }
 }
 
 void hid_task(void)
 {
   // Poll every 10ms
   const uint32_t interval_ms = 10;
   static uint32_t start_ms = 0;
 
   if ( board_millis() - start_ms < interval_ms) return; // not enough time
   start_ms += interval_ms;
 
   bool current_button_state = button_read();
 
   // Remote wakeup
   if ( tud_suspended() && current_button_state )
   {
     // Wake up host if we are in suspend mode
     // and REMOTE_WAKEUP feature is enabled by host
     tud_remote_wakeup();
   }
   else
   {
     // Send the consumer control report
     send_hid_report(REPORT_ID_CONSUMER_CONTROL, current_button_state);
   }
 
   last_button_state = current_button_state;
 }
 
 // Invoked when sent REPORT successfully to host
 void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
 {
   (void) instance;
   (void) len;
 }
 
 uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
 {
   (void) instance;
   (void) report_id;
   (void) report_type;
   (void) buffer;
   (void) reqlen;
   return 0;
 }
 
 void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
 {
   (void) instance;
   (void) report_id;
   (void) report_type;
   (void) buffer;
   (void) bufsize;
 }
 
 //--------------------------------------------------------------------+
 // BLINKING TASK
 //--------------------------------------------------------------------+
 void led_blinking_task(void)
 {
   static uint32_t start_ms = 0;
   static bool led_state = false;
 
   // blink is disabled
   if (!blink_interval_ms) return;
 
   // Blink every interval ms
   if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
   start_ms += blink_interval_ms;
 
   board_led_write(led_state);
   led_state = 1 - led_state; // toggle
 }