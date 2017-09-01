// device_info.c
// Copyright 2017 Lukas Friedrichsen
// License: Apache License Version 2.0
//
// 2017-05-19
//
// Description: This class provides functions for communication- and interaction-
// purposes. It adds meta-data to the device, therewith affixing an unique identity
// to it. Other members of the same network can request this information via UDP.
// Furthermore, the possibility to periodically broadcast a vital sign is
// implemented, thus allowing an automated availability-monitoring of the mesh-
// nodes.
//
// This class is based on https://github.com/espressif/ESP8266_MESH_DEMO/tree/master/mesh_performance/scenario/devicefind.c

#include "mem.h"
#include "osapi.h"
#include "ets_sys.h"
#include "os_type.h"
#include "espconn.h"
#include "user_interface.h"
#include "device_info.h"
#include "user_config.h"

/*------------------------------------*/

// Definition of functions (so there won't be any complications because the
// compiler resolves the scope top-down):

// Callback-functions:
static void udp_info_recv_cb(void *arg, char *data, unsigned short len);

// Timer-functions:
static void vital_sign_broadcast(void);

// Initialization and configuration resp. termination:
void vital_sign_bcast_stop(void);
void vital_sign_bcast_start(void);
void device_info_init(void);
void device_info_init(void);

/*------------------------------------*/

// Declaration and initialization of variables:

const static char *meta_data_request_string = META_DATA_REQUEST_STRING; // Local copy of META_DATA_REQUEST_STRING

static struct espconn *udp_com_socket = NULL;

static os_timer_t *vital_sign_timer = NULL;

static char msg_buffer[64]; // Buffer to store the device info

/*------------------------------------*/

// Callback-functions:

// Check the content of the received UDP-message and forward the nodes meta-data
// to the sender in case of a valid request
static void ICACHE_FLASH_ATTR udp_info_recv_cb(void *arg, char *data, unsigned short len) {
  if (!arg || !data || len == 0) {
    os_printf("udp_info_recv_cb: Invalid transfer parameters!\n");
    return;
  }

  // Print the received message
  os_printf("udp_info_recv_cb: %s\n", data);

  // Check, if the message is a valid information-request
  if (len == os_strlen(meta_data_request_string) && os_memcmp(data, meta_data_request_string, len) == 0) {
    uint8_t resp_len = 0, op_mode = 0;
    struct ip_info ipconfig;
    uint8_t mac_addr[6];  // Refrain from using mesh_device_mac_type from mesh_device.h at this point to keep this class seperated from the mesh-application and therewith independent
    remot_info *con_info = NULL;

    // Check for the operation-mode of the device and get the respective IP- and
    // MAC-address
    op_mode = wifi_get_opmode();
    if (op_mode == SOFTAP_MODE || op_mode == STATION_MODE || op_mode == STATIONAP_MODE) { // Prevent errors resulting from runtime-conditions concerning the WiFi-operation-mode (e.g. if the device is switched into sleep-mode)
      if (op_mode == SOFTAP_MODE) {
        wifi_get_ip_info(SOFTAP_IF, &ipconfig);
        wifi_get_macaddr(SOFTAP_IF, mac_addr);
      }
      else {
        wifi_get_ip_info(STATION_IF, &ipconfig);
        wifi_get_macaddr(STATION_IF, mac_addr);
      }

      // Clear the Buffer
      os_memset(msg_buffer, 0, sizeof(msg_buffer));

      // Print the devices meta-data into the buffer and obtain the actual length
      // of the resulting String
      // Structure: PURPOSE,MAC,IP (allows easy CSV-parsing)
      resp_len = os_sprintf(msg_buffer, "%s," MACSTR "," IPSTR "\n", DEVICE_PURPOSE, MAC2STR(mac_addr), IP2STR(&ipconfig.ip));

      // Get the connection information
      if (espconn_get_connection_info(udp_com_socket, &con_info, 0) == ESPCONN_OK) {
        os_memcpy(udp_com_socket->proto.udp->remote_ip, con_info->remote_ip, sizeof(struct ip_addr));
        udp_com_socket->proto.udp->remote_port = con_info->remote_port;

        // Return the devices meta-data to the sender
        if (espconn_sendto(udp_com_socket, msg_buffer, resp_len) == ESPCONN_OK) {
          os_printf("udp_info_recv_cb: Sent meta-data to " IPSTR ":%d!\n", IP2STR(udp_com_socket->proto.udp->remote_ip), udp_com_socket->proto.udp->remote_port);
        }
        else {
          os_printf("udp_info_recv_cb: Error while sending meta-data!\n");
        }
      }
      else {
        os_printf("udp_info_recv_cb: Failed to retrieve connection info!\n");
      }
    }
    else {
      os_printf("udp_info_recv_cb: Wrong WiFi-operation-mode!\n");
    }
  }
}

/*------------------------------------*/

// Timer-functions:

// Broadcasts a vital sign to all other devices in the network
static void ICACHE_FLASH_ATTR vital_sign_broadcast(void) {
  uint8_t msg_len = 0, op_mode = 0;
  struct ip_info ipconfig;
  uint8_t mac_addr[6];  // Refrain from using mesh_device_mac_type from mesh_device.h at this point to keep this class seperated from the mesh-application and therewith independent

  // Check for the operation-mode of the device and get the respective IP- and
  // MAC-address
  op_mode = wifi_get_opmode();
  if (op_mode == SOFTAP_MODE || op_mode == STATION_MODE || op_mode == STATIONAP_MODE) { // Prevent errors resulting from runtime-conditions concerning the WiFi-operation-mode (e.g. if the device is switched into sleep-mode)
    if (op_mode == SOFTAP_MODE) {
      wifi_get_ip_info(SOFTAP_IF, &ipconfig);
      wifi_get_macaddr(SOFTAP_IF, mac_addr);
    }
    else {
      wifi_get_ip_info(STATION_IF, &ipconfig);
      wifi_get_macaddr(STATION_IF, mac_addr);
    }

    // Clear the Buffer
    os_memset(msg_buffer, 0, sizeof(msg_buffer));

    // Print the devices meta-data into the buffer and obtain the actual length
    // of the resulting String
    // Structure: MAC,TIMESTAMP (allows easy CSV-parsing)
    msg_len = os_sprintf(msg_buffer, MACSTR ",%d\n", MAC2STR(mac_addr), system_get_time());

    // Set broadcast-IP and port
    os_memcpy(udp_com_socket->proto.udp->remote_ip, &ipconfig, sizeof(struct ip_addr)-1);
    os_memset(udp_com_socket->proto.udp->remote_ip+sizeof(struct ip_addr)-1, 255, 1);
    udp_com_socket->proto.udp->remote_port = VITAL_SIGN_PORT;

    // Return the devices meta-data to the sender
    if (espconn_send(udp_com_socket, msg_buffer, msg_len) == ESPCONN_OK) {
      os_printf("vital_sign_broadcast: Broadcasting vital sign message!\n", IP2STR(udp_com_socket->proto.udp->remote_ip), udp_com_socket->proto.udp->remote_port);
    }
    else {
      os_printf("vital_sign_broadcast: Error while broadcasting the vital sign!\n");
    }
  }
  else {
    os_printf("vital_sign_broadcast: Wrong WiFi-operation-mode!\n");
  }
}

// Disable the periodical vital sign broadcasts
void ICACHE_FLASH_ATTR vital_sign_bcast_stop(void) {
  os_printf("vital_sign_bcast_stop: Disabling periodical vital sign broadcasts!\n");

  if (vital_sign_timer) {
    os_timer_disarm(vital_sign_timer);  // Disarm the timer for the periodical vital sign broadcasts
    os_free(vital_sign_timer);  // Free the occupied resources
    vital_sign_timer = NULL;
  }
}

/*------------------------------------*/

// Initialization and configuration resp. termination:

// Initialize a periodical vital sign broadcast
void ICACHE_FLASH_ATTR vital_sign_bcast_start(void) {
  if (!udp_com_socket) {
    os_printf("vital_sign_bcast_start: Please call device_info_init first!\n");
    return;
  }

  os_printf("vital_sign_bcast_start: Enabling periodical vital sign broadcasts!\n");

  // Allow broadcasts from all network-interfaces
  wifi_set_broadcast_if(STATIONAP_MODE);

  // Initialize the timer
  if (!vital_sign_timer) {
    vital_sign_timer = (os_timer_t *) os_zalloc(sizeof(os_timer_t));
  }
  if (!vital_sign_timer) {
    os_printf("vital_sign_init: Failed to initialize the timer for the periodical vital sign broadcasts!\n");
    return;
  }

  // Initialize the timer and assign the function to broadcast the device's
  // vital sign
  os_timer_disarm(vital_sign_timer);
  os_timer_setfn(vital_sign_timer, (os_timer_func_t *) vital_sign_broadcast, NULL);
  os_timer_arm(vital_sign_timer, VITAL_SIGN_TIME_INTERVAL, true);
}

// Disable the possibility to request the device's meta-data as well as the
// periodical vital sign broadcasts and free all occupied resources
void ICACHE_FLASH_ATTR device_info_disable(void) {
  os_printf("device_info_disable: Disabling device_info!\n");

  // Stop the periodical vital sign broadcasts
  vital_sign_bcast_stop();

  // Free the occupied resources
  if (udp_com_socket) {
    os_free(udp_com_socket);
    udp_com_socket = NULL;
  }
}

// Initialize the UDP-socket and set up it's configuration
void ICACHE_FLASH_ATTR device_info_init(void) {
  os_printf("device_info_init: Initializing device_info!\n");

  // Initialize the UDP-socket
  if (!udp_com_socket) {
    udp_com_socket = (struct espconn *) os_zalloc(sizeof(struct espconn));
    if (!udp_com_socket) {
      os_printf("device_info_init: Failed to initialize the UDP-socket!\n");
      return;
    }
  }

  // Initialize the socket's communication-protocol-configuration
  if (!udp_com_socket->proto.udp) {
    udp_com_socket->proto.udp = (esp_udp *) os_zalloc(sizeof(esp_udp));
    if (!udp_com_socket->proto.udp) {
      os_printf("device_info_init: Failed to initialize udp_com_socket->proto.udp!\n");
      device_info_disable();  // Free all occupied resources
    }
  }

  // Set up UDP-socket-configuration
  udp_com_socket->type = ESPCONN_UDP;
  udp_com_socket->state = ESPCONN_NONE;
  udp_com_socket->proto.udp->local_port = DEVICE_COM_PORT;

  // Create UDP-socket and register sent-callback
  if (!espconn_create(udp_com_socket)) {
    espconn_regist_recvcb(udp_com_socket, udp_info_recv_cb);
  }
  else {
    os_printf("device_info_init: Error while creating the UDP-socket!\n");
  }
}
