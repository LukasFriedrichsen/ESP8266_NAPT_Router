// user_config.h
// Copyright 2017 Lukas Friedrichsen
// License: Apache License Version 2.0
//
// 2017-06-28

#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

/*-------- user configurable ---------*/

// Router settings:

#define WIFI_AP_SSID_PREFIX "ESP_ROUTER"  // SSID-prefix of the router; the full
                                          // SSID consists of this prefix with
                                          // the soft access-point's MAC-address
                                          // attached to create a (theoretically)
                                          // unique string, thus significantly
                                          // reducing the chance of having two
                                          // access-points with the same SSID up
                                          // at the same time

#define WIFI_AP_PASSWORD "S20_SmartSocket-WiFi_NAPT_Router" // Password needed
                                                            // by other devices
                                                            // to be able to
                                                            // connect to the
                                                            // router

#define MAX_CLIENTS 8 // Maximum number of clients allowed to connect to the
                      // router at once (limited at 8)

#define WIFI_AP_OPEN 0  // If set to 1, the access-point is open and no password
                        // is needed to connect to it. Per default, the router
                        // is WPA/WPA2-secured and can only be connected to with
                        // WIFI_AP_PASSWORD.

#define WIFI_AP_HIDDEN 0; // If set to 1, the access-point is hidden (the SSID
                          // isn't broadcasted)
                          // Annotation: This doesn't add any security to the
                          // access-point at all!)

#define WIFI_AP_NETWORK_ADDR "192.168.13.1" // IP-address of the router in the
                                            // access-point's sub-network

#define WIFI_AP_NETWORK_NETMASK "255.255.255.0" // All devices with IP-addresses
                                                // within the range of
                                                // WIFI_AP_NETWORK_ADDR &
                                                // WIFI_AP_NETWORK_NETMASK can
                                                // be reached via
                                                // WIFI_AP_NETWORK_GW

#define WIFI_AP_NETWORK_GW "192.168.13.1" // IP-address of the Gateway from
                                          // which devices within the address-
                                          // range of WIFI_AP_NETWORK_ADDR &
                                          // WIFI_AP_NETWORK_NETMASK can be
                                          // reached; since this is the router,
                                          // it should usually be the same as
                                          // WIFI_AP_NETWORK_ADDR

// DHCP:

#define DHCP_START_ADDR "192.168.13.2"  // Start IP-address assigned by the DHCP-
                                        // server
                                        // Attention: DHCP_START_ADDR must be in
                                        // the range of WIFI_AP_NETWORK_ADDR &
                                        // WIFI_AP_NETWORK_NETMASK, or the
                                        // devices won't be reachable from the
                                        // router! Furthermore, DHCP_START_ADDR
                                        // must be AT MAX 100 addresses away
                                        // from DHCP_STOP_ADDR

#define DHCP_STOP_ADDR  "192.168.13.64" // Start IP-address assigned by the DHCP-
                                        // server
                                        // Attention: DHCP_STOP_ADDR must be in
                                        // the range of WIFI_AP_NETWORK_ADDR &
                                        // WIFI_AP_NETWORK_NETMASK, or the
                                        // devices won't be reachable from the
                                        // router! Furthermore, DHCP_STOP_ADDR
                                        // must be AT MAX 100 addresses away
                                        // from DHCP_START_ADDR

// DNS-server:

#define DNS_SERVER_IP 0 // IP-address of the DNS-server to use for domain name
                        // resolution (0 = use Google's DNS-server (8.8.8.8))

// Port mapping:

// Annotation: The following section allows to define up to eight portmap entries,
// which are then automatically loaded when the router is enabled. Each portmap
// entry consists of six parts:
//
//  Protocol            - Communication protocol, which the packages have to be
//                        in to be mapped (cf. lwip/ip.h)
//  Mapping address     - Address of the router in the external network (station
//                        network interface)
//                        Annotation: Since this has to align with the station
//                        network interface's IP-address all the time, it is set
//                        automatically and doesn't have to be configured here.
//  Mapping port        - Port of the router, which is reachable from the
//                        external network (station network interface); upon
//                        receiving a package on this port, the router will map
//                        the message to the correlating destination address
//                        and port in the internal network (soft access-point
//                        network interface) if a respective portmap entry is
//                        present (has to be set to the destination port of the
//                        device in the exterior network, if the direction is
//                        access-point -> station)
//  Destination address - Address, which the mapped packages are sent to
//  Destination port    - Port, which the mapped packages are sent to
//  Direction           - Determines, if the connection can be initiated from
//                        the exterior to the interior network or the other way
//                        around (1 = station -> access-point; 2 = access-point
//                        -> station)
//
// If one of the elements is set to 0, the whole entry will not be loaded.
// Furthermore, each entry can be enabled or disabled seperately by setting
// PORTMAP_ENABLE_X to 1 resp. 0.
//
// Attention: Broadcasted messages won't be mapped!

#define PORTMAP_ENABLE_1 1

  #define PORTMAP_PROTO_1 6
  #define PORTMAP_MPORT_1 8883
  #define PORTMAP_DADDR_1 "192.168.13.37"
  #define PORTMAP_DPORT_1 8883
  #define PORTMAP_DIR_1 2

#define PORTMAP_ENABLE_2 0

  #define PORTMAP_PROTO_2 0
  #define PORTMAP_MPORT_2 0
  #define PORTMAP_DADDR_2 0
  #define PORTMAP_DPORT_2 0
  #define PORTMAP_DIR_2 0

#define PORTMAP_ENABLE_3 0

  #define PORTMAP_PROTO_3 0
  #define PORTMAP_MPORT_3 0
  #define PORTMAP_DADDR_3 0
  #define PORTMAP_DPORT_3 0
  #define PORTMAP_DIR_3 0

#define PORTMAP_ENABLE_4 0

  #define PORTMAP_PROTO_4 0
  #define PORTMAP_MPORT_4 0
  #define PORTMAP_DADDR_4 0
  #define PORTMAP_DPORT_4 0
  #define PORTMAP_DIR_4 0

#define PORTMAP_ENABLE_5 0

  #define PORTMAP_PROTO_5 0
  #define PORTMAP_MPORT_5 0
  #define PORTMAP_DADDR_5 0
  #define PORTMAP_DPORT_5 0
  #define PORTMAP_DIR_5 0

#define PORTMAP_ENABLE_6 0

  #define PORTMAP_PROTO_6 0
  #define PORTMAP_MPORT_6 0
  #define PORTMAP_DADDR_6 0
  #define PORTMAP_DPORT_6 0
  #define PORTMAP_DIR_6 0

#define PORTMAP_ENABLE_7 0

  #define PORTMAP_PROTO_7 0
  #define PORTMAP_MPORT_7 0
  #define PORTMAP_DADDR_7 0
  #define PORTMAP_DPORT_7 0
  #define PORTMAP_DIR_7 0

#define PORTMAP_ENABLE_8 0

  #define PORTMAP_PROTO_8 0
  #define PORTMAP_MPORT_8 0
  #define PORTMAP_DADDR_8 0
  #define PORTMAP_DPORT_8 0
  #define PORTMAP_DIR_8 0

/*------------------------------------*/

// General settings:

// Annotation: Please make sure to also adapt the correlating function-assignment
// in gpio_pins_init (cf. user_main.c) if the addresses of the GPIO-pins are
// modified!

#define ROUTER_CONN_TIMEOUT_WDT_INTERVAL 300000 // Time-interval, in which the
                                                // router's connection state is
                                                // checked by a software-watchdog-
                                                // timer, which restores the
                                                // device's initial state, if no
                                                // connection to the host access-
                                                // point is established (in ms)

#define OUTPUT_POWER_RELAY_GPIO 12  // GPIO-pin, that is connected to the red LED
                                    // as well as to the relay, which controls
                                    // the smart plug's output power; the blue
                                    // LED is activated when the output power is
                                    // turned on

#define BUTTON_INTERRUPT_GPIO 0 // GPIO-pin of the pushbutton, whose actuation
                                // activates the mesh-node and with which the
                                // device's operation mode can be chosen (root
                                // or sub-node)

#define STATUS_LED_GPIO 13  // GPIO-pin of the green LED, which is used to
                            // signalize the node's connection status

#define LED_BLINK_INTERVAL 2000 // Blink-interval for the status-LED; used to
                                // signalize, that the node is currently in
                                // smartconfiguration-mode (in ms)

/*------------------------------------*/

// Meta-data:

#define DEVICE_PURPOSE "WiFi NAPT Router"  // Description of the devices purpose

#define META_DATA_REQUEST_STRING "DEVICE_INFO\n"  // The device will return its
                                                  // meta-data to the sender if
                                                  // this String is received via
                                                  // an UDP-message

/*------------------------------------*/

// Communication and interaction:

#define DEVICE_COM_PORT 49152 // First non-well-known nor registered port; this
                              // port is used for communication and interaction
                              // with other devices in the network (e.g. the
                              // router's meta-data can be requested via this
                              // port)

// Vital sign broadcast:

#define VITAL_SIGN_PORT 49153 // Second non-well-known nor registered port; the
                              // device cyclically broadcasts a vital sign on
                              // this port

#define VITAL_SIGN_TIME_INTERVAL 300000 // Time-interval, in which the vital
                                        // sign is broadcasted (in ms)

/*------------------------------------*/

// ESP-TOUCH:

#define ESP_TOUCH_ATTEMPTS_LIMIT 3  // Maximum number of attempts to connect to
                                    // the router via ESP-TOUCH before aborting

#define ESP_TOUCH_CONFIG_TIMEOUT_THRESHOLD 30000  // Time limit to receive the
                                                  // configuration via ESP-TOUCH

#define ESP_TOUCH_RECV_TIMEOUT_THRESHOLD 30000  // Time limit to receive the
                                                // station-configuration from the
                                                // intermediary-device

#define ESP_TOUCH_CONNECTION_TIMEOUT_THRESHOLD 60000  // Time limit to connect to
                                                      // the router after
                                                      // obtaining SSID & PSWD
                                                      // via ESP-TOUCH

#endif
