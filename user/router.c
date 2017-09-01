// router.c
// Copyright 2017 Lukas Friedrichsen
// License: Apache License Version 2.0
//
// 2017-06-28
//
// Description: This class provides the means necessary to set up the ESP8266 as
// a NAPT (Network Address and Port Translation) router. It handles the
// configuration and initialization of the different network interfaces as well
// as of the DNS- and DHCP-server. Furthermore, the class adds the possibility
// to pre-define up to eight portmap entries in user_config.h, which are then
// automatically loaded when the router is enabled.
//
/******************************************************************************/
// ATTENTION: This class relies on NeoCat's patch for the original lwip library
// (cf. https://github.com/NeoCat/esp8266-Arduino/commit/4108c8dbced7769c75bcbb9ed880f1d3f178bcbe).
/******************************************************************************/

#include "c_types.h"
#include "mem.h"
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_interface.h"
#include "lwip/err.h"
#include "lwip/dns.h"
#include "lwip/lwip_napt.h"
#include "router.h"
#include "user_config.h"

/*------------------------------------*/

// Definition of functions (so there won't be any complications because the
// compiler resolves the scope top-down):

// Status-functions:
bool is_connected(void);

// Callback-functions:
static void wifi_handle_event_cb(System_Event_t *evt);

// Network configuration:
static void portmap_update(ip_addr_t *station_ip_addr);
static void dns_set(void);
static bool softap_network_config(void);

// Initialization and configuration:
static bool softap_init(void);
static bool portmap_init(void);
void router_init(void);

/*------------------------------------*/

// Declaration and initialization of variables:

bool router_connected;

/*------------------------------------*/

// Status-functions:

// Return the current connection status of the station network interface
bool ICACHE_FLASH_ATTR is_connected(void) {
  return router_connected;
}

/*------------------------------------*/

// Callback-functions:

// Callback-function, that is executed on every change of the connection status
// of the station or the soft access-point network interface
static void ICACHE_FLASH_ATTR wifi_handle_event_cb(System_Event_t *evt) {
  switch (evt->event) {
    // Successfully connected to the host access-point
    case EVENT_STAMODE_CONNECTED:
      os_printf("wifi_handle_event_cb: Connected to %s (channel: %d)!\n", evt->event_info.connected.ssid, evt->event_info.connected.channel);
      break;
    // Disconnected from the host access-point
    case EVENT_STAMODE_DISCONNECTED:
      os_printf("wifi_handle_event_cb: Disconnected from %s (reason: %d)!\n", evt->event_info.disconnected.ssid, evt->event_info.disconnected.reason);
      router_connected = false;
      break;
    // Authentication mode of the host access-point changed
    case EVENT_STAMODE_AUTHMODE_CHANGE:
      os_printf("wifi_handle_event_cb: Authentication mode changed from %d to %d!\n", evt->event_info.auth_change.old_mode, evt->event_info.auth_change.new_mode);
      break;
    // Received an IP-address from the host access-point
    case EVENT_STAMODE_GOT_IP:
      os_printf("wifi_handle_event_cb: Got IP-address!\n");

      os_printf("IP-address: " IPSTR "\nNetmask: " IPSTR "\nGateway: " IPSTR "\n", IP2STR(&evt->event_info.got_ip.ip), IP2STR(&evt->event_info.got_ip.mask), IP2STR(&evt->event_info.got_ip.gw));

      // Update the mapping IP-address of the portmap table
      portmap_update(&evt->event_info.got_ip.ip);

      // Set the WiFi operation-mode to STATIONAP_MODE and enable the soft
      // access-point
      if (wifi_set_opmode(STATIONAP_MODE) && softap_init()) {
        // Set the defined network configuration  for the soft access-point and
        // the DHCP-server's lease range
        if (softap_network_config()) {
          // Set the DNS-server to use
          dns_set();

          // If an error occures while setting up the soft access-point,
          // router_connected is not set to true, so that the router will be
          // disabled by the watchdog-timer (cf. user_main.c).
          router_connected = true;
        }
      }
      break;
    // Device connected to the soft access-point
    case EVENT_SOFTAPMODE_STACONNECTED:
      os_printf("wifi_handle_event_cb: Station " MACSTR " connected (AID: %d)!\n", MAC2STR(evt->event_info.sta_connected.mac), evt->event_info.sta_connected.aid);
      break;
    //Device disconnect from the soft access-point
    case EVENT_SOFTAPMODE_STADISCONNECTED:
      os_printf("wifi_handle_event_cb: Station " MACSTR " disconnected (AID: %d)!\n", MAC2STR(evt->event_info.sta_disconnected.mac), evt->event_info.sta_disconnected.aid);
      break;
    default:
      break;
  }
}

/*------------------------------------*/

//Miscellaneous:

// Update the mapping IP-address of the portmap table (e.g. if a new IP-address
// for the station network interface is received from the DHCP-server of the
// host router)
// Annotation: ip_portmap_table is defined in lwip_napt.h
static void ICACHE_FLASH_ATTR portmap_update(ip_addr_t *station_ip_addr) {
  if (!station_ip_addr) {
    os_printf("portmap_update: Invalid transfer parameter!\n");
    return;
  }
  if (!ip_portmap_table) {
    os_printf("portmap_update: Please initialize ip_portmap_table first!\n");
    return;
  }

  os_printf("portmap_update: Updating portmap!\n");

  uint16_t idx = 0;

  for (idx = 0; idx < IP_PORTMAP_MAX; idx++) {
    if(ip_portmap_table[idx].valid) {
      ip_portmap_table[idx].maddr = (*station_ip_addr).addr;
    }
  }
}

// Set the DNS-server to use
static void ICACHE_FLASH_ATTR dns_set(void) {
  os_printf("dns_set: Setting the DNS-server!\n");

  ip_addr_t dns_server_ip;

  // Check, if a static server has been defined
  if (DNS_SERVER_IP) {
    // Set the defined address
    dns_server_ip.addr = ipaddr_addr(DNS_SERVER_IP);
  }
  else {
    // Set Google's DNS-server as default, if no other source has been defined
    IP4_ADDR(&dns_server_ip, 8, 8, 8, 8);
  }
  dhcps_set_DNS((struct ip_addr *) &dns_server_ip);

  os_printf("DNS-server: " IPSTR "\n", IP2STR(&dns_server_ip));
}

// Set the defined network configuration and the DHCP-server's lease range
static bool ICACHE_FLASH_ATTR softap_network_config(void) {
  os_printf("softap_network_config: Setting the defined network configuration and the DHCP-server's lease range!\n");

  struct ip_info softap_info;
  struct dhcps_lease dhcp_lease;

  // Stop the DHCP-server before setting the defined network configuration as
  // well as the DHCP-server's lease range and enable NAPT for the soft access-
  // point network interface
  if (wifi_softap_dhcps_stop()) {
    // Set the defined network configuration
    softap_info.ip.addr = ipaddr_addr(WIFI_AP_NETWORK_ADDR);
    ip4_addr4(&softap_info.ip) = 1; // The router will always have the address X.X.X.1!
    softap_info.netmask.addr = ipaddr_addr(WIFI_AP_NETWORK_NETMASK);
    softap_info.gw.addr = ipaddr_addr(WIFI_AP_NETWORK_GW);
    if (wifi_set_ip_info(SOFTAP_IF, &softap_info)) {
      // Configure the DHCP-server's lease range
      const	char*	start_ip	=	DHCP_START_ADDR;
      const	char*	end_ip	=	DHCP_STOP_ADDR;
      dhcp_lease.start_ip.addr = ipaddr_addr(start_ip);
      dhcp_lease.end_ip.addr = ipaddr_addr(end_ip);
      if (wifi_softap_set_dhcps_lease(&dhcp_lease)) {
        // Re-enable the DHCP-server
        if (wifi_softap_dhcps_start()) {
          // Enable NAPT for the soft access-point network interface
          ip_napt_enable(softap_info.ip.addr, 1);

          // Allow broadcasts also in SOFTAP_MODE
          wifi_set_broadcast_if(STATIONAP_MODE);

          return true;
        }
        else {
          os_printf("softap_network_config: Failed to re-enable the DHCP-server!\n");
        }
      }
      else {
        os_printf("softap_network_config: Failed to set the DHCP-server's lease range!\n");
      }
    }
    else {
      os_printf("softap_network_config: Failed to set the soft access-point's network configuration!\n");
    }
  }
  else {
    os_printf("softap_network_config: Failed to stop the DHCP-server!\n");
  }
  return false;
}

/*------------------------------------*/

// Initialization and configuration:

// Set up and initialize the soft access-point network interface
static bool ICACHE_FLASH_ATTR softap_init(void) {
  os_printf("softap_init: Setting up and initializing the soft access-point network interface!\n");

  // Check, if the correct WiFi operation-mode is enabled (SOFTAP_MODE or
  // STATIONAP_MODE)
  if (wifi_get_opmode() >= SOFTAP_MODE) {
    struct softap_config ap_conf;
    uint8_t softap_mac_addr[6];

    if (wifi_get_macaddr(SOFTAP_IF, softap_mac_addr)) {
      // Set up the soft access-point configuration
      os_memset(ap_conf, 0, sizeof(struct softap_config));
      os_sprintf(ap_conf.ssid, "%s_" MACSTR, WIFI_AP_SSID_PREFIX, MAC2STR(softap_mac_addr)); // Generate the access-point's actual (unique) SSID from SSID_PREFIX and the soft access-point's MAC-address
      ap_conf.ssid_len = os_strlen(ap_conf.ssid);
      if (!WIFI_AP_OPEN) {  // Set the authentication mode to WPA/WPA2 as well as the corresponding password, if WIFI_AP_OPEN is set to 0
        ap_conf.authmode = AUTH_WPA_WPA2_PSK;
        os_sprintf(ap_conf.password, "%s", WIFI_AP_PASSWORD); // os_memcpy doesn't automatically add a NULL-termination to a string, that's actually a define, so use os_sprintf or copy WIFI_AP_PASSWORD to a char *
      }
      else {  // Set the authentication mode to open, if WIFI_AP_OPEN is set to 1 (no authentication needed to connect to the router's access-point)
        ap_conf.authmode = AUTH_OPEN;
      }
      ap_conf.max_connection = MAX_CLIENTS;
      ap_conf.ssid_hidden = WIFI_AP_HIDDEN;

      // Initialize the soft access-point network interface
      if (wifi_softap_set_config(&ap_conf)) {
        return true;
      }
      else {
        os_printf("softap_init: Failed to set the soft access-point configuration!\n");
      }
    }
    else {
      os_printf("softap_init: Failed to obtain the soft access-point's MAC-address!\n");
    }
  }
  else {
    os_printf("softap_init: Wrong WiFi operation-mode!\n");
  }
  return false;
}

// Load the pre-defined portmap entries (cf. user_config.h)
// Attention: Call portmap_update as soon as an IP-address is obtained on the
// station network interface! The port mapping won't work otherwise!
bool ICACHE_FLASH_ATTR portmap_init(void) {
  os_printf("portmap_init: Loading the pre-defined portmap entries!\n");

  ip_addr_t daddr;

  if (PORTMAP_ENABLE_1) {
    if (PORTMAP_PROTO_1 && PORTMAP_MPORT_1 && PORTMAP_DADDR_1 && PORTMAP_DPORT_1 && PORTMAP_DIR_1) {
      daddr.addr = ipaddr_addr(PORTMAP_DADDR_1);
      if (!ip_portmap_add(PORTMAP_PROTO_1, 0, PORTMAP_MPORT_1, daddr.addr, PORTMAP_DPORT_1, PORTMAP_DIR_1)) {
        os_printf("portmap_init: Failed to set portmap entry 1!\n");
        return false;
      }
    }
  }
  if (PORTMAP_ENABLE_2) {
    if (PORTMAP_PROTO_2 && PORTMAP_MPORT_2 && PORTMAP_DADDR_2 && PORTMAP_DPORT_2 && PORTMAP_DIR_2) {
      daddr.addr = ipaddr_addr(PORTMAP_DADDR_2);
      if (!ip_portmap_add(PORTMAP_PROTO_2, 0, PORTMAP_MPORT_2, daddr.addr, PORTMAP_DPORT_2, PORTMAP_DIR_2)) {
        os_printf("portmap_init: Failed to set portmap entry 2!\n");
        return false;
      }
    }
  }
  if (PORTMAP_ENABLE_3) {
    if (PORTMAP_PROTO_3 && PORTMAP_MPORT_3 && PORTMAP_DADDR_3 && PORTMAP_DPORT_3 && PORTMAP_DIR_3) {
      daddr.addr = ipaddr_addr(PORTMAP_DADDR_3);
      if (!ip_portmap_add(PORTMAP_PROTO_3, 0, PORTMAP_MPORT_3, daddr.addr, PORTMAP_DPORT_3, PORTMAP_DIR_3)) {
        os_printf("portmap_init: Failed to set portmap entry 3!\n");
        return false;
      }
    }
  }
  if (PORTMAP_ENABLE_4) {
    if (PORTMAP_PROTO_4 && PORTMAP_MPORT_4 && PORTMAP_DADDR_4 && PORTMAP_DPORT_4 && PORTMAP_DIR_4) {
      daddr.addr = ipaddr_addr(PORTMAP_DADDR_4);
      if (!ip_portmap_add(PORTMAP_PROTO_4, 0, PORTMAP_MPORT_4, daddr.addr, PORTMAP_DPORT_4, PORTMAP_DIR_4)) {
        os_printf("portmap_init: Failed to set portmap entry 4!\n");
        return false;
      }
    }
  }
  if (PORTMAP_ENABLE_5) {
    if (PORTMAP_PROTO_5 && PORTMAP_MPORT_5 && PORTMAP_DADDR_5 && PORTMAP_DPORT_5 && PORTMAP_DIR_5) {
      daddr.addr = ipaddr_addr(PORTMAP_DADDR_5);
      if (!ip_portmap_add(PORTMAP_PROTO_5, 0, PORTMAP_MPORT_5, daddr.addr, PORTMAP_DPORT_5, PORTMAP_DIR_5)) {
        os_printf("portmap_init: Failed to set portmap entry 5!\n");
        return false;
      }
    }
  }
  if (PORTMAP_ENABLE_6) {
    if (PORTMAP_PROTO_6 && PORTMAP_MPORT_6 && PORTMAP_DADDR_6 && PORTMAP_DPORT_6 && PORTMAP_DIR_6) {
      daddr.addr = ipaddr_addr(PORTMAP_DADDR_6);
      if (!ip_portmap_add(PORTMAP_PROTO_6, 0, PORTMAP_MPORT_6, daddr.addr, PORTMAP_DPORT_6, PORTMAP_DIR_6)) {
        os_printf("portmap_init: Failed to set portmap entry 6!\n");
        return false;
      }
    }
  }
  if (PORTMAP_ENABLE_7) {
    if (PORTMAP_PROTO_7 && PORTMAP_MPORT_7 && PORTMAP_DADDR_7 && PORTMAP_DPORT_7 && PORTMAP_DIR_7) {
      daddr.addr = ipaddr_addr(PORTMAP_DADDR_7);
      if (!ip_portmap_add(PORTMAP_PROTO_7, 0, PORTMAP_MPORT_7, daddr.addr, PORTMAP_DPORT_7, PORTMAP_DIR_7)) {
        os_printf("portmap_init: Failed to set portmap entry 7!\n");
        return false;
      }
    }
  }
  if (PORTMAP_ENABLE_8) {
    if (PORTMAP_PROTO_8 && PORTMAP_MPORT_8 && PORTMAP_DADDR_8 && PORTMAP_DPORT_8 && PORTMAP_DIR_8) {
      daddr.addr = ipaddr_addr(PORTMAP_DADDR_8);
      if (!ip_portmap_add(PORTMAP_PROTO_8, 0, PORTMAP_MPORT_8, daddr.addr, PORTMAP_DPORT_8, PORTMAP_DIR_8)) {
        os_printf("portmap_init: Failed to set portmap entry 8!\n");
        return false;
      }
    }
  }
  return true;
}

// Initialize the router
void ICACHE_FLASH_ATTR router_init() {
  os_printf("router_init: Initializing the router!\n");

  router_connected = false;

  // Load the pre-defined portmap entries
  if (!portmap_init()) {  // Don't abort the program, if there is an error while loading the pre-defined portmap entries since this only affects the availability of certain devices connected to the router and not the router functionaliy itself
    os_printf("router_init: Error while loading the pre-defined portmap entries!\n");
  }

  // Set the WiFi-event-handler-function
  wifi_set_event_handler_cb(wifi_handle_event_cb);
}
