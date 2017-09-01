// user_main.c
// Copyright 2017 Lukas Friedrichsen
// License: Apache License Version 2.0
//
// 2017-06-27
//
// Description: This implementation of the a minimalistic ESP8266 WiFi NAPT
// (Network Address and Port Translation) router is designed for the
//
//       "S20 Smart Socket" (cf. itead.cc/smart-socket-eu/html) by ITEAD,
//
// a WiFi-enabled smart socket based on the ESP8266-microcontroller. The router
// is activated by actuating the pushbutton. The device then enters
// smart-configuration-mode and tries to connect to a network, whose
// authentication credentials it obtains via ESP-TOUCH from a nearby intermediary-
// device (e.g. a smartphone). Once connected, the router-functionality is enabled.
// Up to eight devices can connect to the router's access-point at once and a
// maximum bitrate of about 5 Mbps in both directions can be achieved.
//
// Furthermore, the router periodically broadcasts a vital sign to enable
// automated availability-monitoring. The device's meta-data can be requested
// via an UDP-message to the router.
// For the whole time, the device's status is displayed by the LEDs:
//
//  green (blinking):         smart-configuration-mode (ESP-TOUCH)
//  green (steady):           successfully connected, router enabled
//  blue:                     output power turned on
//
// The configuration of the router (including the settings concerning the port
// mapping) can be modified in user_config.h.
//
/******************************************************************************/
// ATTENTION: Tested and compiled with ESP-NONOS-SDK version 2.0.0_16_08_10!
/******************************************************************************/

#include "mem.h"
#include "osapi.h"
#include "user_interface.h"
#include "device_info.h"
#include "esp_touch.h"
#include "router.h"
#include "user_config.h"

/*------------------------------------*/

// Definition of functions (so there won't be any complications because the
// compiler resolves the scope top-down):

// Callback-functions:
static void router_disable_cb(void);

// Timer- and interrupt-handler-functions:
static void button_actuated_interrupt_handler(void *arg);
static void router_conn_timeout_wdtfunc(void *arg);
static void esptouch_over_timerfunc(os_timer_t *timer);
static void led_blink_timerfunc(void *arg);

// GPIO control:
static void status_led_on(void);
static void status_led_off(void);
static void output_power_on(void);
static void output_power_off(void);

// Initialization and configuration:
static bool router_enable(void);
static void gpio_pins_init(void);

void user_init(void);

// Radio frequency configuration
uint32 user_rf_cal_sector_set(void);
void user_rf_pre_init(void);

/*------------------------------------*/

// Declaration and initialization of variables:

static os_timer_t *led_blink_timer = NULL, *router_conn_timeout_wdt = NULL;

/*------------------------------------*/

// Callback-functions:

// Callback-function, that disables the router; restores the initial state of
// the program, so that the device  is ready to be re-activated via the pushbutton
static void ICACHE_FLASH_ATTR router_disable_cb(void) {
  // Disable all further communication- and interaction-functionalities,
  // including the periodical vital sign broadcasts as well as the possibility
  // to request the devices meta-data
  device_info_disable();

  // Clear possible connections, set the operation-mode to NULL_MODE and reset
  // the WiFi-event-handler-function
  wifi_station_disconnect();
  wifi_set_opmode(NULL_MODE);
  wifi_set_event_handler_cb(NULL);

  if (led_blink_timer) {
    os_timer_disarm(led_blink_timer);
    os_free(led_blink_timer);
    led_blink_timer = NULL;
  }
  if (router_conn_timeout_wdt) {
    os_timer_disarm(router_conn_timeout_wdt);
    os_free(router_conn_timeout_wdt);
    router_conn_timeout_wdt = NULL;
  }

  // Turn off the status-LED (the state of the smart plug's power outlet isn't
  // changed, so connected peripheral equipment doesn't get damaged or shut down
  // by accident)
  status_led_off();

  // Re-enable the interrupt so that the device is ready to be re-initialized
  // via actuation of the pushbutton
  ETS_GPIO_INTR_DISABLE();  // Disable interrupts before changing the current configuration
  GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(BUTTON_INTERRUPT_GPIO));  // Clear the interrupt-mask (otherwise, the interrupt will be masked because the interrupt-handler-funciton has already been executed)
  ETS_GPIO_INTR_ENABLE(); // Re-enable the interrupts
}

/*------------------------------------*/

// Timer- and interrupt-handler-functions:

// Interrupt-handler-function, that is called on the actuation of the pushbutton;
// disable the interrupt and initialize the router
static void ICACHE_FLASH_ATTR button_actuated_interrupt_handler(void *arg) {
  // Disable the interrupt whilst the device is activated
  ETS_GPIO_INTR_DISABLE();

  // Try to initialize the router
  router_enable();
}

// Timer-function to periodically check, if the connection to the router has
// has been lost or if a timeout occured; restore the device's initial state in
// that case
static void ICACHE_FLASH_ATTR router_conn_timeout_wdtfunc(void *arg) {
  if (!is_connected()) {
    os_printf("router_conn_timeout_wdtfunc: Connection got lost or a timeout occured!\n");
    router_disable_cb(); // Disable the router
  }
}

// Timer-function, to periodically check, if ESP-TOUCH is still running and enable
// the watchdog-timer as well as the periodical vital-sign-broadcasts and further
// communication- and interaction-functionalities if it was successful or reset
// the device in case it failed
static void ICACHE_FLASH_ATTR esptouch_over_timerfunc(os_timer_t *timer) {
  // Check, if ESP-TOUCH was successful
  if (!esptouch_is_running()) {
    // Disarm the timer and free the occupied resources
    if (timer) {
      os_timer_disarm(timer);
      os_free(timer);
    }

    if (esptouch_was_successful()) {
      // Disarm and free the led_blink_timer and switch the green status-LED
      // on to signalize, that ESP-TOUCH was successful and the router is now
      // enabled
      if (led_blink_timer) {
        os_timer_disarm(led_blink_timer);
        os_free(led_blink_timer);
        led_blink_timer = NULL;
      }
      status_led_on();

      // Arm the watchdog-timer to periodically check on possible connection-
      // losses or timeouts
      os_timer_disarm(router_conn_timeout_wdt);
      os_timer_setfn(router_conn_timeout_wdt, (os_timer_func_t *) router_conn_timeout_wdtfunc, NULL);
      os_timer_arm(router_conn_timeout_wdt, ROUTER_CONN_TIMEOUT_WDT_INTERVAL, true);

      // Initialize further communication- and interaction-functionalities (e.g.
      // the possibility for other devices to request's meta-dat via an
      // UDP-message)
      device_info_init();

      // Start periodical vital-sign-broadcasts
      vital_sign_bcast_start();
    }
    else {
      // Call router_disable_cb to free all further occupied resources and
      // restore the device's initial state
      router_disable_cb();
    }
  }
}

// Timer-function, that toggles the status-LED
static void ICACHE_FLASH_ATTR led_blink_timerfunc(void *arg) {
  // Get the current state of the status-LED
  if (!(GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT(STATUS_LED_GPIO))) {
    // Set the output-value to high
    status_led_off();
  }
  else {
    // Set the output-value to low
    status_led_on();
  }
}

/*------------------------------------*/

// GPIO control:

// Switch the status-LED on and set it the corresponding pin to output-mode
static void ICACHE_FLASH_ATTR status_led_on(void) {
  gpio_output_set(0, BIT(STATUS_LED_GPIO), BIT(STATUS_LED_GPIO), 0);
}

// Switch the status-LED off and set it the corresponding pin to output-mode
static void ICACHE_FLASH_ATTR status_led_off(void) {
  gpio_output_set(BIT(STATUS_LED_GPIO), 0, BIT(STATUS_LED_GPIO), 0);
}

// Turn the smart plug's output power and the red LED on
static void ICACHE_FLASH_ATTR output_power_on(void) {
  gpio_output_set(BIT(OUTPUT_POWER_RELAY_GPIO), 0, BIT(OUTPUT_POWER_RELAY_GPIO), 0);
}

// Turn the smart plug's output power and the red LED off
static void ICACHE_FLASH_ATTR output_power_off(void) {
  gpio_output_set(0, BIT(OUTPUT_POWER_RELAY_GPIO), BIT(OUTPUT_POWER_RELAY_GPIO), 0);
}

/*------------------------------------*/

// Initialization and configuration:

// Initialize the router and start the smart-configuration-mode (ESP-TOUCH)
static bool ICACHE_FLASH_ATTR router_enable(void) {
  os_printf("router_enable: Initializing the router and starting ESP-TOUCH!\n");

  // Initialize the timer to toggle the status-LED while the smart-configuration-
  // mode is in progress
  if (!led_blink_timer) {
    led_blink_timer = (os_timer_t *) os_zalloc(sizeof(os_timer_t));
    if (!led_blink_timer) { // Won't cause the program to abort since this only affects the status-LED
      os_printf("router_enable: Failed to initialize led_blink_timer! Continuing without!\n");
    }
    else {
      // Start the timer to toggle the status-LED to signalize, that the device
      // is in smart-configuration-mode (short blink-interval)
      os_timer_disarm(led_blink_timer);
      os_timer_setfn(led_blink_timer, (os_timer_func_t *) led_blink_timerfunc, NULL);
      os_timer_arm(led_blink_timer, LED_BLINK_INTERVAL, true);
    }
  }

  // Initialize the watchdog-timer to continually check, if the connection to the
  // router has been lost
  if (!router_conn_timeout_wdt) {
    router_conn_timeout_wdt = (os_timer_t *) os_zalloc(sizeof(os_timer_t));
  }
  if (router_conn_timeout_wdt) {
    // Periodically check, whether ESP-TOUCH has been finished yet and, if yes, if it
    // was successful or not
    os_timer_t *esptouch_wait_timer = (os_timer_t *) os_zalloc(sizeof(os_timer_t));
    if (esptouch_wait_timer) {
      os_timer_disarm(esptouch_wait_timer);
      os_timer_setfn(esptouch_wait_timer, (os_timer_func_t *) esptouch_over_timerfunc, esptouch_wait_timer); // Assign the timer-function
      os_timer_arm(esptouch_wait_timer, 500, true);  // Arm the timer; check on ESP-TOUCH once every 500ms

      // Initialize the router
      router_init();

      // Initialize and start ESP-TOUCH
      esptouch_init();

      return true;
    }
    else {
      os_printf("router_enable: Failed to initialize esptouch_wait_timer!\n");
    }
  }
  else {
    os_printf("router_enable: Failed to initialize router_conn_timeout_wdt!\n");
  }
  // Call router_disable_cb to free all occupied resources and restore the
  // device's initial state
  router_disable_cb();
  return false;
}

// Initialize the GPIO-pins to function as intended
static void ICACHE_FLASH_ATTR gpio_pins_init(void) {
  os_printf("gpio_pins_init: Initializing GPIO-pins!\n");

  // Initialize the GPIO-subsystem
  gpio_init();

  // Set the defined pins' operation-mode to GPIO
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);

  // Enable the pull-up resistor of the status-LED's GPIO-pin (since the LED is
  // connected in reverse logic); that's also why status_led_on "disables" the
  // pin in order to turn on the light (resp. the other way around for
  // status_led_off)
  PIN_PULLUP_EN(PERIPHS_IO_MUX_MTCK_U);

  // Set the status-LED's GPIO-pin to output-mode and deactivate it
  status_led_off();

  // Set the output-power-relay's GPIO-pin to output-mode and energize it by
  // default, so that the outlet, which the smart plug is connected to, isn't
  // blocked and can still be used
  output_power_on();

  // Set the pushbutton's GPIO-pin to input-mode
  gpio_output_set(0, 0, 0, BIT(BUTTON_INTERRUPT_GPIO));

  // Initialize the pushbutton-pin to function as an interrupt
  ETS_GPIO_INTR_DISABLE();  // Disable interrupts before changing the current configuration
  ETS_GPIO_INTR_ATTACH(button_actuated_interrupt_handler, GPIO_ID_PIN(BUTTON_INTERRUPT_GPIO)); // Attach the corresponding function to be executed to the interrupt-pin
  gpio_pin_intr_state_set(GPIO_ID_PIN(BUTTON_INTERRUPT_GPIO), GPIO_PIN_INTR_POSEDGE); // Configure the interrupt-function to be executed on a positive edge
  GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(BUTTON_INTERRUPT_GPIO));  // Clear the interrupt-mask (otherwise, the interrupt might be masked because of random initialization-values in the corresponding interrupt-register)
  ETS_GPIO_INTR_ENABLE(); // Re-enable the interrupts
}

// Entry point in the program; start the initialization-process
void user_init(void) {
  os_printf("user_init: Starting the initialization-process!\n");

  // Clear possible connections, set the operation-mode to NULL_MODE and reset
  // the WiFi-event-handler-function
  wifi_station_disconnect();
  wifi_set_opmode(NULL_MODE);
  wifi_set_event_handler_cb(NULL);

  // Initialize the GPIO-pins
  gpio_pins_init();

  //router_enable();
}

/*------------------------------------*/

// Radio frequency configuration:

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABBBCDDD
 *                A : rf cal
 *                B : at parameters
 *                C : rf init data
 *                D : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void) {
    enum flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
      case FLASH_SIZE_4M_MAP_256_256:
        rf_cal_sec = 128 - 8;
        break;

      case FLASH_SIZE_8M_MAP_512_512:
        rf_cal_sec = 256 - 5;
        break;

      case FLASH_SIZE_16M_MAP_512_512:
      case FLASH_SIZE_16M_MAP_1024_1024:
        rf_cal_sec = 512 - 5;
        break;

      case FLASH_SIZE_32M_MAP_512_512:
      case FLASH_SIZE_32M_MAP_1024_1024:
        rf_cal_sec = 1024 - 5;
        break;

      default:
        rf_cal_sec = 0;
        break;
    }

    return rf_cal_sec;
}

void ICACHE_FLASH_ATTR user_rf_pre_init(void) {
  // Nothing to do...
}
