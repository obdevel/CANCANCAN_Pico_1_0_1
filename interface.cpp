#include "WiFiClass.h"

/*
  Copyright (C) Duncan Greenwood 2026 (duncan_greenwood@hotmail.com)

  This work is licensed under the:
      Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
   To view a copy of this license, visit:
      http://creativecommons.org/licenses/by-nc-sa/4.0/
   or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.

   License summary:
    You are free to:
      Share, copy and redistribute the material in any medium or format
      Adapt, remix, transform, and build upon the material

    The licensor cannot revoke these freedoms as long as you follow the license terms.

    Attribution : You must give appropriate credit, provide a link to the license,
                  and indicate if changes were made. You may do so in any reasonable manner,
                  but not in any way that suggests the licensor endorses you or your use.

    NonCommercial : You may not use the material for commercial purposes. **(see note below)

    ShareAlike : If you remix, transform, or build upon the material, you must distribute
                 your contributions under the same license as the original.

    No additional restrictions : You may not apply legal terms or technological measures that
                                 legally restrict others from doing anything the license permits.

   ** For commercial use, please contact the original copyright holder(s) to agree licensing terms

    This software is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE
*/

#include <stdint.h>

#include <CBUSconfig.h>
#include <CBUSLED.h>

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"
#include "interface.h"

#include <sys/_types.h>

#if defined(IS_NETWORK_DEVICE)
#include <WiFi.h>
#include <W5500lwIP.h>
#include <LEAmDNS.h>
#include <WebServer.h>
#endif

/// global variables defined in other source files

extern bool enable_serial_debug;
extern CBUSConfig module_config;
extern network_config_t netcfg;

/// globals

#if defined(IS_NETWORK_DEVICE)
IPAddress udp_broadcast_address(255, 255, 255, 255);
WebServer *webserver;

// network status names lookup
static const char *net_status_names[] = {
  "WL_IDLE_STATUS",
  "WL_NO_SSID_AVAIL",
  "WL_SCAN_COMPLETED",
  "WL_CONNECTED",
  "WL_CONNECT_FAILED",
  "WL_CONNECTION_LOST",
  "WL_DISCONNECTED",
  "WL_AP_LISTENING",
  "WL_AP_CONNECTED",
  "WL_AP_FAILED",
  "UNKNOWN"
};
#endif

static const char *permitted_gc_chars = "SXNR0123456789ABCDEF";

//
/// abstract interface base class constructor
//

abstract_interface::abstract_interface(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led)
  : num(num), interface_type(interface_type), activity_led(activity_led), error_led(error_led) {
}

/// network interface
/// handles all wifi and ethernet interface types

network::network(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led)
  : abstract_interface(num, interface_type, activity_led, error_led) {

  VLOG("network/%u: constructor, type = %u", num, interface_type);
  enabled = false;

  // create input queue
  queue_init(&input_queue, sizeof(can_msg_wrapper_t), NET_QUEUE_SIZE);

  bridge_connected = false;
  network_connection_state = NETWORK_STATE_DISCONNECTED;
  last_network_status = 0;
  reconnect_required = false;
  num_current_connections = 0;
  udp_sequence_num_rx = 0;
  udp_sequence_num_tx = 0;
}

bool network::begin([[maybe_unused]] const bool force_enumeration) {

  bool ok = true;

#if defined(IS_NETWORK_DEVICE)

  char hostname[32], service[32];

  /// set device configuration

  if (strlen(netcfg.hostname) > 0) {
    strcpy(hostname, netcfg.hostname);
  } else {
    strcpy(hostname, DEFAULT_HOSTNAME);
  }

  if (strlen(netcfg.servicename) > 0) {
    strcpy(service, netcfg.servicename);
  } else {
    strcpy(service, DEFAULT_SERVICENAME);
  }

  /// set hardware-specific configuration

  switch (interface_type) {

    case INTERFACE_TYPE_ETHERNET_TCP_SERVER:
    case INTERFACE_TYPE_ETHERNET_TCP_CLIENT:
    case INTERFACE_TYPE_ETHERNET_UDP_NODE:

      VLOG("network/%u: starting ethernet interface", num);

      // SPI pins for Wiznet W5500-EVB-Pico2
      SPI.setRX(ETHERNET_MISO_PIN);
      SPI.setCS(ETHERNET_CS_PIN);
      SPI.setSCK(ETHERNET_SCK_PIN);
      SPI.setTX(ETHERNET_MOSI_PIN);

      eth = new Wiznet5500lwIP(ETHERNET_CS_PIN, SPI, ETHERNET_INT_PIN);

      if (module_config.readNV(35)) {  // first octect of fixed IP address is non-zero
        VLOG("network/%u: using fixed IP configuration", num);
        IPAddress ip(module_config.readNV(35), module_config.readNV(36), module_config.readNV(37), module_config.readNV(38));
        IPAddress subnet(module_config.readNV(39), module_config.readNV(40), module_config.readNV(41), module_config.readNV(42));
        IPAddress gateway(module_config.readNV(43), module_config.readNV(44), module_config.readNV(45), module_config.readNV(46));
        IPAddress dns(8, 8, 8, 8);
        eth->config(ip, dns, gateway, subnet);
      } else {  // get dynamic address from DHCP server
        VLOG("network/%u: using dynamic IP configuration", num);
        eth->setDNS(IPAddress(8, 8, 8, 8));  // just in case the DHCP server didn't supply this
      }

      ok = eth->begin();
      eth->hostname(hostname);

      if (!ok) {
        VLOG("network/%u: *** no ethernet hardware detected", num);
        ok = false;
        error_led.on();
      } else {
        VLOG("network/%u: ethernet interface initialised ok", num);
      }

      last_network_status = eth->status();
      break;

    case INTERFACE_TYPE_WIFI_TCP_SERVER:
    case INTERFACE_TYPE_WIFI_TCP_CLIENT:
    case INTERFACE_TYPE_WIFI_UDP_NODE:

      VLOG("network/%u: starting wifi interface", num);

      WiFi.hostname(hostname);
      WiFi.setDNS(IPAddress(8, 8, 8, 8));

      if (netcfg.wifi_mode == WIFI_MODE_AP || strlen(netcfg.ssid) == 0) {
        VLOG("network/%u: starting wifi access point", num);

        // start wifi in access-point mode
        IPAddress apIP(172, 217, 28, 1);
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

        if (strlen(netcfg.ap_ssid) == 0) {
          VLOG("network/%u: creating wifi access point with default settings - %s/%s", num, DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD);
          // WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD, netcfg.ap_channel, netcfg.hide_ap);
          WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD);
        } else {
          VLOG("network/%u: creating wifi access point with configured settings - %s/%s", num, netcfg.ap_ssid, netcfg.ap_password);
          // WiFi.softAP(netcfg.ap_ssid, netcfg.ap_password, netcfg.ap_channel, netcfg.hide_ap);
          WiFi.softAP(netcfg.ap_ssid, netcfg.ap_password);
        }

        VLOG("network/%u: created access point ok");
      } else {

        // start wifi in station mode
        VLOG("network/%u: initiating connection to wifi as STA: %s/%s", num, netcfg.ssid, netcfg.password);
        WiFi.mode(WIFI_STA);
        WiFi.begin(netcfg.ssid, netcfg.password);
        VLOG("network/%u: initiated connection to wifi, status = %u/%s", num, WiFi.status(), net_status_names[WiFi.status()]);
      }

      last_network_status = WiFi.status();
      break;

    default:
      VLOG("network/%u: *** unknown server type %u", num, interface_type);
      error_led.pulse();
      return false;
      break;
  }

  last_network_connection_attempt = millis();

  /// start webserver

  webserver = new WebServer(80);
  webserver->on("/", handle_root);
  webserver->on("/postform", handle_form);
  webserver->onNotFound(handle_not_found);
  webserver->begin();
  VLOG("network/%u: started webserver on port 80", num);

  /// determine server port number

  port = netcfg.port;

  if (port == 0) {
    VLOG("network/%u: using default port", num);
    port = DEFAULT_IP_PORT;
  } else {
    VLOG("network/%u: using configured port %u", num, port);
  }

  /// start MDNS name server and announce DNS-SD services

  if (MDNS.begin(hostname)) {
    VLOG("network/%u: started MDNS server, hostname = %s.local", num, hostname);
    VLOG("network/%u: announcing DNS-SD services", num);
    MDNS.addService(service, "tcp", port);  // layout service
    MDNS.addService("http", "tcp", 80);     // web server
  }

  /// start NTP client
  // 131.111.8.28 sntp.cam.ac.uk

  NTP.begin(IPAddress(131, 111, 8, 28));
  VLOG("network/%u: started NTP client", num);

  if (interface_type == INTERFACE_TYPE_ETHERNET_UDP_NODE || interface_type == INTERFACE_TYPE_WIFI_UDP_NODE) {

    /// start UDP
    udp = new WiFiUDP;
    udp->begin(port + 1);
    VLOG("network/%u: started UDP client on port %u", num, udp->localPort());
  } else {

    /// start network server
    server = new WiFiServer(port);
    server->begin();
    server->setNoDelay(true);
    VLOG("network/%u: started Gridconnect server on port %u", num, port);

    if (interface_type == INTERFACE_TYPE_WIFI_TCP_CLIENT || interface_type == INTERFACE_TYPE_ETHERNET_TCP_CLIENT) {
      bridge_client = new WiFiClient;
      VLOG("network/%u: created bridge client", num);
    }
  }

  // blink activity LED whilst network connection is in progress
  activity_led.blink();

  // init connections table
  memset(connection_tab, 0, sizeof(connection_tab));

  VLOG("network/%u: server initialised ok", num);
#endif

  return ok;
}

void network::run(void) {

#if defined(IS_NETWORK_DEVICE)

  byte i;
  size_t r;
  can_msg_wrapper_t wmsg;

  static unsigned long btimer = 0;

  /// check for network status change

  switch (interface_type) {
    case INTERFACE_TYPE_ETHERNET_TCP_SERVER:
    case INTERFACE_TYPE_ETHERNET_TCP_CLIENT:
    case INTERFACE_TYPE_ETHERNET_UDP_NODE:
      network_status = eth->status();
      break;

    case INTERFACE_TYPE_WIFI_TCP_SERVER:
    case INTERFACE_TYPE_WIFI_TCP_CLIENT:
    case INTERFACE_TYPE_WIFI_UDP_NODE:
      network_status = WiFi.status();
      break;

    default:
      network_status = 0;
      break;
  }

  /// handle change in network connection state

  if (network_status != last_network_status) {
    VLOG("network/%u: network status changed from %u/%s to %u/%s",
         num, last_network_status, net_status_names[last_network_status], network_status, net_status_names[network_status]);
    last_network_status = network_status;

    switch (network_status) {

      case WL_CONNECTED:
        VLOG("network/%u: network is connected", num);
        network_connection_state = NETWORK_STATE_CONNECTED;
        num_current_connections = 0;
        reconnect_required = false;
        error_led.off();
        activity_led.off();
        break;

      default:
        VLOG("network/%u: *** network is not connected", num);
        network_connection_state = NETWORK_STATE_DISCONNECTED;
        reconnect_required = true;
        error_led.blink();
        break;
    }
  }

  /// attempt to reconnect wifi if required

  if (netcfg.wifi_mode == WIFI_MODE_STA && network_connection_state == NETWORK_STATE_DISCONNECTED && reconnect_required && (millis() - last_network_connection_attempt > WIFI_RECONNECT_INTERVAL)) {
    VLOG("network/%u: reconnecting to wifi", num);
    WiFi.begin(netcfg.ssid, netcfg.password);
    network_connection_state = NETWORK_STATE_CONNECTING;
    last_network_connection_attempt = millis();
    error_led.off();
    activity_led.blink();
  }

  /// proceed if network connection is established

  if (network_connection_state == NETWORK_STATE_CONNECTED) {

    if (interface_type == INTERFACE_TYPE_ETHERNET_UDP_NODE || interface_type == INTERFACE_TYPE_WIFI_UDP_NODE) {

      /// UDP network interface types

      size_t r;
      udp_can_message_t udpmsg;

      if (udp->parsePacket()) {
        if (udp->available() == sizeof(udp_can_message_t)) {
          if ((r = udp->read((uint8_t *)&udpmsg, sizeof(udp_can_message_t))) == sizeof(udp_can_message_t)) {
            VLOG("network/%u: received udp packet, size = %u, source = %s, seq = %u", num, r, udp->remoteIP().toString().c_str(), udpmsg.sequence_num);

            if (udp_sequence_num_rx > 0 && udpmsg.sequence_num != udp_sequence_num_rx) {
              VLOG("network/%u: unexpected sequence number, expected = %u, received = %u", num, udp_sequence_num_rx, udpmsg.sequence_num);
              error_led.pulse();
            }

            queue_try_add(&input_queue, &udpmsg.wmsg);
            udp_sequence_num_rx = udpmsg.sequence_num++;
            activity_led.pulse();
          }
        }
      }

    } else {

      /// TCP netwotk interface types

      /// (re)connect to remote bridge if required

      if ((interface_type == INTERFACE_TYPE_ETHERNET_TCP_CLIENT || interface_type == INTERFACE_TYPE_WIFI_TCP_CLIENT) && !bridge_connected && millis() - btimer >= BRIDGE_RECONNECT_INTERVAL) {
        btimer = millis();

        char bridge_server[64];
        uint16_t bridge_port;

        /// try MDNS to locate service host

        VLOG("network/%u: reconnecting to bridge server", num);
        VLOG("network/%u: trying MDNS", num);

        uint32_t num_svcs = MDNS.queryService(netcfg.servicename, "tcp", MDNS_QUERY_TIMEOUT);

        if (num_svcs > 0) {
          VLOG("net: DNS-SD service = %s/tcp, got %u query response(s)", netcfg.servicename, num_svcs);

          for (byte j = 0; j < num_svcs; j++) {
            VLOG("  - hostname = %s, ip = %s, port = %u", MDNS.answerHostname(j), MDNS.answerIP(j).toString().c_str(), MDNS.answerPort(j));
          }

          strncpy(bridge_server, MDNS.answerIP(0).toString().c_str(), sizeof(bridge_server));
          bridge_port = MDNS.answerPort(0);

        } else {

          VLOG("network/%u: using saved config for bridge server", num);

          if (isdigit(netcfg.servername[0])) {
            strncpy(bridge_server, netcfg.servername, sizeof(bridge_server));
          } else {
            if (String(netcfg.servername).indexOf(".local") == -1) {
              snprintf(bridge_server, sizeof(bridge_server), "%s.local", netcfg.servername);
            } else {
              strncpy(bridge_server, netcfg.servername, sizeof(bridge_server));
            }
          }

          bridge_port = netcfg.port;
        }

        VLOG("network/%u: connecting to remote node, server = %s, port = %u", num, bridge_server, bridge_port);
        bridge_client->setNoDelay(true);

        if (bridge_client->connect(bridge_server, bridge_port)) {
          VLOG("network/%u: connected as client to remote node", num);
          bridge_connected = true;
          activity_led.pulse();
        } else {
          VLOG("network/%u: *** connection to remote node failed", num);
          bridge_connected = false;
          error_led.pulse();
        }
      }

      /// check for new incoming client connections

      WiFiClient new_client = server->accept();

      if (new_client) {
        VLOG("network/%u: new client connection from %s", num, new_client.remoteIP().toString().c_str());

        for (i = 0; i < MAX_CLIENTS; i++) {
          if (connection_tab[i].status == SLOT_IDLE) {
            connection_tab[i].client = new WiFiClient(new_client);
            connection_tab[i].client->setNoDelay(true);
            strcpy(connection_tab[i].ipaddress, connection_tab[i].client->remoteIP().toString().c_str());
            connection_tab[i].status = SLOT_CONNECTED;
            connection_tab[i].idx = 0;
            ++num_current_connections;
            activity_led.pulse();
            VLOG("network/%u: added new client connection at slot %u, count = %u", num, i, num_current_connections);
            break;
          }
        }

        if (i == MAX_CLIENTS) {
          VLOG("network/%u: *** no free client connection slots", num);
          error_led.pulse();
        }
      }

      /// check for activity on existing client connections

      for (i = 0; i < MAX_CLIENTS; i++) {

        if (connection_tab[i].status == SLOT_CONNECTED && connection_tab[i].client != nullptr) {

          // has client disconnected ?

          if (!connection_tab[i].client->connected()) {
            --num_current_connections;
            connection_tab[i].client->stop();
            delete connection_tab[i].client;
            memset(&connection_tab[i], 0, sizeof(client_connection_t));
            error_led.pulse();
            VLOG("network/%u: client at slot %u has disconnected, count = %u", num, i, num_current_connections);
            continue;
          }

          // read and process available data from this client

          while (connection_tab[i].client->available()) {

            // read a chunk of data

            r = connection_tab[i].client->read(connection_tab[i].input_buffer, sizeof(connection_tab[i].input_buffer) - 1);
            connection_tab[i].input_buffer[r] = 0;
            activity_led.pulse();

            // process the received data chunk

            for (byte j = 0; j < r; j++) {
              char c = connection_tab[i].input_buffer[j];

              switch (c) {
                case ':':
                  connection_tab[i].work_buffer[0] = c;
                  connection_tab[i].idx = 1;
                  break;

                case ';':
                  connection_tab[i].work_buffer[connection_tab[i].idx++] = c;
                  connection_tab[i].work_buffer[connection_tab[i].idx] = 0;

                  forward_message_to_net_clients(i, connection_tab[i].work_buffer);

                  wmsg.retries = 0;
                  wmsg.timestamp = micros();

                  if (!(GCtoCAN(connection_tab[i].work_buffer, &wmsg.msg) && queue_try_add(&input_queue, &wmsg))) {
                    error_led.pulse();
                  }

                  connection_tab[i].idx = 0;
                  connection_tab[i].work_buffer[connection_tab[i].idx] = 0;
                  break;

                default:
                  connection_tab[i].work_buffer[connection_tab[i].idx++] = c;
                  break;
              }

              if (connection_tab[i].idx >= sizeof(connection_tab[i].work_buffer)) {
                VLOG("network/%u: *** input buffer overflow for ciient %u", num, i);
                connection_tab[i].work_buffer[0] = 0;
                connection_tab[i].idx = 0;
                error_led.pulse();
              }
            }
          }
        }
      }

      /// receive data from remote server, if connected

      if ((interface_type == INTERFACE_TYPE_ETHERNET_TCP_CLIENT || interface_type == INTERFACE_TYPE_WIFI_TCP_CLIENT) && bridge_connected) {

        if (!bridge_client->connected()) {
          VLOG("network/%u: *** connection to server node has dropped", num);
          error_led.pulse();
          bridge_connected = false;
        } else {

          while (bridge_client->available()) {

            // read a chunk of data

            r = bridge_client->read(bridge_input_buffer, sizeof(bridge_input_buffer) - 1);
            bridge_input_buffer[r] = 0;
            VLOG("network/%u: read %u bytes from server node = %s", num, r, bridge_input_buffer);
            activity_led.pulse();

            // process the received data chunk

            for (byte j = 0; j < r; j++) {
              char c = bridge_input_buffer[j];

              switch (c) {
                case ':':
                  bridge_work_buffer[0] = c;
                  bridge_work_buffer_idx = 1;
                  break;

                case ';':
                  bridge_work_buffer[bridge_work_buffer_idx++] = c;
                  bridge_work_buffer[bridge_work_buffer_idx] = 0;

                  forward_message_to_net_clients(FROM_REMOTE, bridge_work_buffer);

                  wmsg.timestamp = micros();
                  wmsg.retries = 0;

                  if ((GCtoCAN(bridge_work_buffer, &wmsg.msg) && queue_try_add(&input_queue, &wmsg))) {
                    error_led.pulse();
                  }

                  bridge_work_buffer_idx = 0;
                  bridge_work_buffer[bridge_work_buffer_idx] = 0;
                  break;

                default:
                  bridge_work_buffer[bridge_work_buffer_idx++] = c;
                  break;
              }

              if (bridge_work_buffer_idx >= sizeof(bridge_work_buffer)) {
                VLOG("network/%u: *** client input buffer overflow", num);
                bridge_work_buffer[0] = 0;
                bridge_work_buffer_idx = 0;
                error_led.pulse();
              }
            }
          }
        }
      }

      /// run supporting infrastructure services

      MDNS.update();
      webserver->handleClient();
    }
  }
#endif

  return;
}

// forward a CAN message from the router - this interface is not CAN so id rewrite is ignored

bool network::forward_message(can_msg_wrapper_t *wmsg, [[maybe_unused]] const bool rewrite) {

  bool ok = true;
  unsigned long t = (micros() - wmsg->timestamp);

  VLOG("network/%u: forwarding message, t = %u, rewrite = %u", num, t, rewrite);

#if defined(IS_NETWORK_DEVICE)
  char buffer[GC_BUFFER_SIZE];

  if (interface_type == INTERFACE_TYPE_ETHERNET_UDP_NODE || interface_type == INTERFACE_TYPE_WIFI_UDP_NODE) {
    udp_can_message_t udpmsg;

    udpmsg.sequence_num = udp_sequence_num_tx++;
    udpmsg.wmsg = *wmsg;

    udp->beginPacket(udp_broadcast_address, netcfg.port + 1);
    udp->write((uint8_t *)&udpmsg, sizeof(udp_can_message_t));
    udp->endPacket();
  } else {

    if (!(CANtoGC(&wmsg->msg, buffer) && forward_message_to_net_clients(FROM_ROUTER, buffer))) {
      error_led.pulse();
      return false;
    }
  }
#endif

  return ok;
}

bool network::forward_message_to_net_clients(const byte source, const char *message) {

  bool ok = true;

#if defined(IS_NETWORK_DEVICE)

  size_t r, slen = strlen(message);

  // send to each connected net client in turn

  for (byte i = 0; i < MAX_CLIENTS; i++) {
    if (i != source && connection_tab[i].status == SLOT_CONNECTED && connection_tab[i].client != nullptr) {
      VLOG("network/%u: sending to client %u", num, i);
      r = connection_tab[i].client->write((uint8_t *)message, slen);

      if (r != slen) {
        VLOG("network/%u: *** error writing to client %u, wrote = %u, wanted = %u, errno = %d", num, i, r, slen, errno);
        error_led.pulse();
      } else {
        activity_led.pulse();
      }
    }
  }

  /// forward to remote server device if from another source

  if (interface_type == INTERFACE_TYPE_WIFI_TCP_CLIENT || interface_type == INTERFACE_TYPE_ETHERNET_TCP_CLIENT) {
    if (bridge_connected && source != FROM_REMOTE) {
      VLOG("network/%u: forwarding to remote server", num);
      r = bridge_client->write((uint8_t *)message, slen);
      VLOG("network/%u: wrote %u bytes to remote server", num, r);

      if (r != slen) {
        VLOG("network/%u: *** error writing to remote server, wrote = %u, wanted = %u, errno = %d", num, r, slen, errno);
        error_led.pulse();
      } else {
        activity_led.pulse();
      }
    }
  }

#endif
  return ok;
}

void network::enable(const bool new_state) {

  can_msg_wrapper_t tmsg;

  if (new_state && !enabled) {  // if enabling
    while (queue_try_remove(&input_queue, &tmsg))
      ;  // empty the input queue
  }

  enabled = new_state;
  VLOG("network/%u: enabled = %u", num, enabled);

  return;
}

bool network::available(void) {

  return (!queue_is_empty(&input_queue));
}

//

bool network::get_message(can_msg_wrapper_t *wmsg) {

  return queue_try_remove(&input_queue, wmsg);
}

//

byte network::get_status(void) {

  return (network_connection_state == NETWORK_STATE_CONNECTED ? INTERFACE_STATUS_OK : INTERFACE_STATUS_ERROR);
}

//

void network::enumerate_bus([[maybe_unused]] const bool force) {

  return;
}

void network::respond_to_enumeration_request(void) {

  return;
}

void network::discover_nodes(void) {

  VLOG("network/%u: discover nodes", num);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 1;
  wmsg.msg.data[0] = OPC_QNN;

  wmsg.timestamp = micros();
  wmsg.retries = 0;

  forward_message(&wmsg, false);
  return;
}

void network::query_node(const uint16_t node_number) {

  VLOG("network/%u: query node %u", num, node_number);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 3;
  wmsg.msg.data[0] = OPC_NERD;
  wmsg.msg.data[1] = node_number >> 8;
  wmsg.msg.data[2] = node_number & 0x7f;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, false);
  return;
}

///

//
/// gridconnect serial interface class
//

gridconnect_serial::gridconnect_serial(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led)
  : abstract_interface(num, interface_type, activity_led, error_led) {

  VLOG("gridconnect_serial/%u: constructor, type = %u", num, interface_type);
  VLOG("gridconnect_serial/%u: creating queues", num);

  input_buffer[0] = 0;
  message_buffer[0] = 0;
  message_buffer_index = 0;

  queue_init(&input_queue, sizeof(can_msg_wrapper_t), GC_QUEUE_SIZE);

  switch (interface_type) {

    case INTERFACE_TYPE_GRIDCONNECT_USB:
      serial_instance = &Serial;
      enable_serial_debug = false;  // *** FIXME *** disable serial debug output to avoid confusion
      break;

    case INTERFACE_TYPE_GRIDCONNECT_SERIAL_1:
      serial_instance = &Serial1;
      enable_serial_debug = true;
      break;

    case INTERFACE_TYPE_GRIDCONNECT_SERIAL_2:
      serial_instance = &Serial2;
      enable_serial_debug = true;
      break;
  }

  VLOG("gridconnect_serial/%u: complete", num);
}

bool gridconnect_serial::begin([[maybe_unused]] const bool force_enumeration) {

  VLOG("gridconnect_serial/%u: begin", num);

  /// init serial port

  serial_instance->begin(SERIAL_BAUDRATE);
  return true;
}

void gridconnect_serial::enable(const bool new_state) {

  can_msg_wrapper_t wmsg;

  VLOG("gridconnect_serial/%u: enabled = %u", num, new_state);

  if (!enabled && new_state) {
    while (queue_try_remove(&input_queue, &wmsg))
      ;
  }

  enabled = new_state;
  return;
}

void gridconnect_serial::run(void) {

  can_msg_wrapper_t wmsg;

  /// read data from serial port
  /// convert complete message and place on input queue for router to consume

  if (enabled) {

    if (serial_instance->available()) {

      size_t r = serial_instance->readBytes(input_buffer, sizeof(input_buffer) - 1);
      input_buffer[r] = 0;

      for (byte i = 0; i < r; i++) {

        char c = input_buffer[i];

        switch (input_buffer[i]) {

          case ':':
            message_buffer[0] = c;
            message_buffer_index = 1;
            break;

          case ';':
            message_buffer[message_buffer_index++] = c;
            message_buffer[message_buffer_index] = 0;

            wmsg.timestamp = micros();
            wmsg.retries = 0;

            if (!(GCtoCAN(message_buffer, &wmsg.msg) && queue_try_add(&input_queue, &wmsg))) {
              error_led.pulse();
            }

            message_buffer[0] = 0;
            message_buffer_index = 0;
            break;

          default:
            if (strchr(permitted_gc_chars, toupper(c))) {
              message_buffer[message_buffer_index++] = c;
              message_buffer[message_buffer_index] = 0;
            }

            break;
        }

        if (message_buffer_index >= sizeof(input_buffer) - 1) {  // buffer overflow
          message_buffer[0] = 0;
          message_buffer_index = 0;
        }
      }
    }
  }

  return;
}

bool gridconnect_serial::forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) {

  bool ok = false;
  char message_buffer[GC_BUFFER_SIZE];

  VLOG("gridconnect_serial/%u: forward_message: forwarding message, rewrite = %u", num, rewrite);

  /// convert to GC and write to serial port

  if (enabled) {
    if ((ok = CANtoGC(&wmsg->msg, message_buffer))) {
      serial_instance->write(message_buffer);
      serial_instance->flush();
    }
  }

  return ok;
}

bool gridconnect_serial::available(void) {

  return !queue_is_empty(&input_queue);
}

bool gridconnect_serial::get_message(can_msg_wrapper_t *wmsg) {

  return queue_try_remove(&input_queue, wmsg);
}

byte gridconnect_serial::get_status(void) {

  return INTERFACE_STATUS_OK;
}

///
