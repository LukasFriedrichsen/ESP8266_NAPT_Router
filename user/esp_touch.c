// esp_touch.c
// Copyright 2017 Lukas Friedrichsen
// License: Apache License Version 2.0
//
// 2017-05-04
//
// Description: This class implements ESP-TOUCH (cf. http://espressif.com/en/products/software/esp-touch/overview),
// a mechanism that allows a device to connect to a WiFi-network through configuration on a WiFi-enabled device
// like a smartphone. Therefore the SSID and the password of the network are encoded in the length field of a
// sequence of UDP-packets. Those information can be decrypted by the ESP which can then connect to the
// corresponding router. A smartphone-application by Espressif to handle the intermediary-part can be found at
// http://espressif.com/en/products/software/esp-touch/resources.
//
// This class is based on https://github.com/espressif/ESP8266_MESH_DEMO/tree/master/mesh_demo/demo/esp_touch.c

#include "mem.h"
#include "osapi.h"
#include "user_interface.h"
#include "smartconfig.h"
#include "esp_touch.h"
#include "user_config.h"

/*------------------------------------*/

// Definition of functions (so there won't be any complications because the
// compiler resolves the scope top-down):

// Status-functions:
bool esptouch_is_running(void);
bool esptouch_was_successful(void);

// Callback-functions:
bool esptouch_is_running(void);
bool esptouch_was_successful(void);
static void esptouch_success_cb(void *arg);
static void esptouch_start_cb(void *arg);
void esptouch_status_cb(sc_status status, void *arg);

// Timer-functions:
static void esptouch_fail_cb(void *arg);

// Initialization and configuration resp. termination:
void esptouch_disable(void);
void esptouch_init(void);

/*------------------------------------*/

// Declaration and initialization of variables:

static sc_type smartconfig_type;
struct esptouch_cb esptouch_func;

static bool esptouch_running = false, esptouch_success = false;
static uint8_t esptouch_attempt_count = 1;

static os_timer_t *esptouch_timeout_timer = NULL;

/*------------------------------------*/

// Status-functions:

// Get the current status of ESP-TOUCH
bool ICACHE_FLASH_ATTR esptouch_is_running(void) {
  return esptouch_running;
}

// Return, if ESP-TOUCH was successful
bool ICACHE_FLASH_ATTR esptouch_was_successful(void) {
  return esptouch_success;
}

/*------------------------------------*/

// Callback-functions:

// Callback-function, that is executed on successful establishing a connection
// to the router via ESP-TOUCH
static void ICACHE_FLASH_ATTR esptouch_success_cb(void *arg) {
  os_printf("esptouch_success_cb: Success! Stopping ESP-TOUCH now!\n");

  if (esptouch_timeout_timer) {
    os_timer_disarm(esptouch_timeout_timer);
    os_free(esptouch_timeout_timer);  // Free occupied resources
    esptouch_timeout_timer = 0;
  }

  esptouch_running = false;
  esptouch_success = true;
}

// Callback-function, that is executed on the start of ESP-TOUCH; increase the
// attempt-count, print out the current status
static void ICACHE_FLASH_ATTR esptouch_start_cb(void *arg) {
  os_printf("esptouch_start_cb: Starting ESP-TOUCH!\n");
}

// Callback-function, that is executed on status-changes of ESP-TOUCH
void ICACHE_FLASH_ATTR esptouch_status_cb(sc_status status, void *arg) {
  switch (status) {
    // Waiting for a connection to the intermediary-device
    case SC_STATUS_WAIT:
      os_printf("esptouch_status_cb: Waiting...\n");
      break;
    // Scanning channels to communicate with the intermediary-device
    case SC_STATUS_FIND_CHANNEL:
      os_printf("esptouch_status_cb: Searching channel!\n");

      // Execute the start-callback
      if (esptouch_func.esptouch_start_cb) {
        esptouch_func.esptouch_start_cb(NULL);
      }
      break;
    // Receiving SSID and password from the intermediary-device
    case SC_STATUS_GETTING_SSID_PSWD:
      os_printf("esptouch_status_cb: Receiving SSID and PSWD!\n");

      // Arm the timer that executes the timeout-callback, if the station-
      // configuration couldn't be obtained until the defined threshold (cf.
      // ESP_TOUCH_RECV_TIMEOUT_THRESHOLD)
      if (esptouch_timeout_timer && esptouch_func.esptouch_fail_cb) {
        os_timer_disarm(esptouch_timeout_timer);
        os_timer_setfn(esptouch_timeout_timer, esptouch_func.esptouch_fail_cb, NULL);
        os_timer_arm(esptouch_timeout_timer, ESP_TOUCH_RECV_TIMEOUT_THRESHOLD, false);
      }
      break;
    // Connecting to the router whose SSID and password have been obtained from
    // the intermediary-device
    case SC_STATUS_LINK:
      os_printf("esptouch_status_cb: Setting station_config and trying to connect to the router!\n");

      // Connect to the router whose station_config has been obtained
      struct station_config *station_conf = arg;

      // Set the obtained SSID and password as mesh-router-configuration and try
      // to connect to the access-point
      wifi_station_disconnect();  // Clear all currently established connections
      if (wifi_station_set_config(station_conf)) {
        wifi_station_connect(); // Try to connect to the access-point
      }
      else {
        os_printf("esptouch_status_cb: Error while setting station-configuration! Aborting ESP-TOUCH!");
        esptouch_disable();
      }

      // Arm the timer that executes the timeout-callback, if no connection to the
      // router could be established until the defined threshold (cf.
      // ESP_TOUCH_CONNECTION_TIMEOUT_THRESHOLD)
      if (esptouch_timeout_timer && esptouch_func.esptouch_fail_cb) {
        os_timer_disarm(esptouch_timeout_timer);
        os_timer_setfn(esptouch_timeout_timer, esptouch_func.esptouch_fail_cb, NULL);
        os_timer_arm(esptouch_timeout_timer, ESP_TOUCH_CONNECTION_TIMEOUT_THRESHOLD, false);
      }
      break;
    // Connection successfully established; stopping smartconfiguration-mode and
    // executing success-callback
    case SC_STATUS_LINK_OVER:
      os_printf("esptouch_status_cb: Connection to the router established!\n");

      // Stop the smartconfiguration-mode and execute the success-callback (if
      // existing)
      smartconfig_stop();
      if (esptouch_func.esptouch_suc_cb) {
        esptouch_func.esptouch_suc_cb(NULL);
      }

      break;
  }
}

/*------------------------------------*/

// Timer-functions:

// Callback-function, that is executed on a timeout; print out the current status
// and restart ESP-TOUCH until the defined limit of attempts has been reached
static void ICACHE_FLASH_ATTR esptouch_fail_cb(void *arg) {
  os_printf("esptouch_fail_cb: Timeout occured at the %d. attempt!\n", esptouch_attempt_count);

  // Stop ESP-TOUCH, disable WiFi and disarm the timeout-timer
  smartconfig_stop();
  wifi_station_disconnect();
  if (esptouch_timeout_timer) {
    os_timer_disarm(esptouch_timeout_timer);
  }

  if (esptouch_attempt_count < ESP_TOUCH_ATTEMPTS_LIMIT) {
    os_printf("esptouch_fail_cb: Retrying...\n");

    // Increase the attempt-count
    esptouch_attempt_count++;

    // Initialize and arm the timer that executes the timeout-callback, if no
    // configuration-packages are received until the defined threshold (cf.
    // ESP_TOUCH_CONFIG_TIMEOUT_THRESHOLD)
    if (esptouch_timeout_timer) {
      os_timer_setfn(esptouch_timeout_timer, esptouch_func.esptouch_fail_cb, NULL);
      os_timer_arm(esptouch_timeout_timer, ESP_TOUCH_CONFIG_TIMEOUT_THRESHOLD, false);
    }

    // Restart ESP-TOUCH
    smartconfig_start(esptouch_status_cb);
  }
  else {
    os_printf("esptouch_fail_cb: Reached attempt-limit! Aborting ESP-TOUCH!\n");

    if (esptouch_timeout_timer) {
      os_free(esptouch_timeout_timer);  // Free occupied resources
      esptouch_timeout_timer = 0;
    }

    esptouch_running = false;
  }
}

/*------------------------------------*/

// Initialization and configuration resp. termination:

// Stop the smartconfiguration-mode and free all occupied resources
void ICACHE_FLASH_ATTR esptouch_disable(void) {
  os_printf("esptouch_disable: Disabling ESP-TOUCH!\n");

  // Stop the smartconfiguration-mode
  smartconfig_stop();

  if (esptouch_timeout_timer) {
    os_timer_disarm(esptouch_timeout_timer);  // Disarm timeout-timer
    os_free(esptouch_timeout_timer);  // Free occupied resouces
    esptouch_timeout_timer = NULL;
  }

  esptouch_running = false;
}

// Set callbacks, initialize timer and start ESP-TOUCH
void ICACHE_FLASH_ATTR esptouch_init(void) {
  os_printf("esptouch_init: Initializing ESP-TOUCH!\n");

  // Set ESP-TOUCH to running and not (yet) successful and initialize the
  // attempt-count
  esptouch_running = true;
  esptouch_success = false;
  esptouch_attempt_count = 1;

  // Clear possible connections before trying to set up a new connection
  wifi_station_disconnect();

  // Set WiFi to station mode to be able to receive the router's SSID and
  // password from the intermediary-device
  os_printf("esptouch_init: Set WiFi to station mode!\n");
  wifi_set_opmode(STATION_MODE);

  // Assign callback-functions and set the smartconfiguration-type to
  // SC_TYPE_ESPTOUCH
  esptouch_func.esptouch_fail_cb = esptouch_fail_cb;
  esptouch_func.esptouch_start_cb = esptouch_start_cb;
  esptouch_func.esptouch_suc_cb = esptouch_success_cb;
  smartconfig_type = SC_TYPE_ESPTOUCH;

  // Initialize the timeout-timer
  esptouch_timeout_timer = (os_timer_t *) os_zalloc(sizeof(os_timer_t));
  if (!esptouch_timeout_timer) {
    os_printf("Failed to initialize the timeout-timer! Continuing without!\n");
  }

  // Configure and arm the timer that executes the timeout-callback, if no
  // configuration-packages are received until the defined threshold (cf.
  // ESP_TOUCH_CONFIG_TIMEOUT_THRESHOLD)
  if (esptouch_timeout_timer && esptouch_func.esptouch_fail_cb) {
    os_timer_disarm(esptouch_timeout_timer);
    os_timer_setfn(esptouch_timeout_timer, esptouch_func.esptouch_fail_cb, NULL);
    os_timer_arm(esptouch_timeout_timer, ESP_TOUCH_CONFIG_TIMEOUT_THRESHOLD, false);
  }

  // Start ESP-TOUCH
  if (esptouch_status_cb) {
    smartconfig_start(esptouch_status_cb);
  }
  else {
    os_printf("esptouch_init: Failed to start smartconfiguration-mode!\n");
    esptouch_disable(); // Free all occupied resources and set esptouch_running to false
  }
}
