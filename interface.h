
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

#pragma once

#include <ACAN2040.h>
#include <CBUSLED.h>

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"

#if HAS_CAN_FD
#include <ACAN2517FD.h>
#endif

#if HAS_EXT_MCP2515
#include <ACAN2515.h>
#endif

#if HAS_EXT_MCP2518
#include <ACAN2517.h>
#endif

#if defined(IS_NETWORK_DEVICE)
#include <WiFi.h>
#include <W5500lwIP.h>
#endif

/// interface class defintions

class abstract_interface {
public:
  abstract_interface(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led);

  // virtual methods - must be implemented by derived classes

  virtual bool begin(const bool force_enumeration) = 0;
  virtual void enable(const bool new_state) = 0;
  virtual void run(void) = 0;
  virtual bool forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) = 0;
  virtual bool available(void) = 0;
  virtual bool get_message(can_msg_wrapper_t *wmsg) = 0;
  virtual byte get_status(void) = 0;

  // default empty implementations provided so derived classes do not need to

  virtual void enumerate_bus([[maybe_unused]] const bool force = false){};
  virtual void respond_to_enumeration_request(void){};

  virtual void discover_nodes(void){};
  virtual void query_node([[maybe_unused]] const uint16_t node_number){};

  // member variables common to all derived classes

  bool enabled;
  byte num, interface_type, canid, default_action_when_source, default_action_when_destination, interface_status;
  queue_t input_queue, *ciq, *coq;

protected:
  CBUSLED &activity_led, &error_led;
  interface_stats_t interface_stats;
};

//

class cbus_interface final : public abstract_interface {
public:
  cbus_interface(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led);

  bool begin(const bool force_enumeration) override;
  void enable(const bool new_state) override;
  void run(void) override;
  bool forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) override;
  bool available(void) override;
  bool get_message(can_msg_wrapper_t *wmsg) override;
  byte get_status(void) override;

  void discover_nodes(void) override;
  void query_node(const uint16_t node_number) override;

private:
};

//

class canbus final : public abstract_interface {
public:
  canbus(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led);

  bool begin(const bool force_enumeration) override;
  void enable(const bool new_state) override;
  void run(void) override;
  bool forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) override;
  bool available(void) override;
  bool get_message(can_msg_wrapper_t *wmsg) override;
  byte get_status(void) override;

  void enumerate_bus(const bool force = false) override;
  void respond_to_enumeration_request(void) override;

  void discover_nodes(void) override;
  void query_node(const uint16_t node_number) override;

private:
  queue_t tx_retry_queue;
  ACAN2040 *can;
};

//

class can_backbone final : public abstract_interface {
public:
  can_backbone(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led);

  bool begin(const bool force_enumeration) override;
  void enable(const bool new_state) override;
  void run(void) override;
  bool forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) override;
  bool available(void) override;
  bool get_message(can_msg_wrapper_t *wmsg) override;
  byte get_status(void) override;

  void enumerate_bus(const bool force = false) override;
  void respond_to_enumeration_request(void) override;

  void discover_nodes(void) override;
  void query_node(const uint16_t node_number) override;

private:
  queue_t tx_retry_queue;
  ACAN2040 *can;
};

//
class canfdbus final : public abstract_interface {
public:
  canfdbus(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led);

  bool begin(const bool force_enumeration) override;
  void enable(const bool new_state) override;
  void run(void) override;
  bool forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) override;
  bool available(void) override;
  bool get_message(can_msg_wrapper_t *wmsg) override;
  byte get_status(void) override;

  void discover_nodes(void) override;
  void query_node(const uint16_t node_number) override;

private:
#if HAS_CAN_FD
  ACAN2517FD *can;
#endif
};

//

class canmcp2515 final : public abstract_interface {
public:
  canmcp2515(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led);

  bool begin(const bool force_enumeration) override;
  void enable(const bool new_state) override;
  void run(void) override;
  bool forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) override;
  bool available(void) override;
  bool get_message(can_msg_wrapper_t *wmsg) override;
  byte get_status(void) override;

  void enumerate_bus(const bool force = false) override;
  void respond_to_enumeration_request(void) override;

  void discover_nodes(void) override;
  void query_node(const uint16_t node_number) override;

private:
#if HAS_EXT_MCP2515
  ACAN2515 *can;
#endif
};

//

class canmcp2518 final : public abstract_interface {
public:
  canmcp2518(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led);

  bool begin(const bool force_enumeration) override;
  void enable(const bool new_state) override;
  void run(void) override;
  bool forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) override;
  bool available(void) override;
  bool get_message(can_msg_wrapper_t *wmsg) override;
  byte get_status(void) override;

  void enumerate_bus(const bool force = false) override;
  void respond_to_enumeration_request(void) override;

  void discover_nodes(void) override;
  void query_node(const uint16_t node_number) override;

private:
#if HAS_EXT_MCP2518
  ACAN2517 *can;
#endif
};

//

class canmcp2515backbone final : public abstract_interface {
public:
  canmcp2515backbone(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led);

  bool begin(const bool force_enumeration) override;
  void enable(const bool new_state) override;
  void run(void) override;
  bool forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) override;
  bool available(void) override;
  bool get_message(can_msg_wrapper_t *wmsg) override;
  byte get_status(void) override;

  void enumerate_bus(const bool force = false) override;
  void respond_to_enumeration_request(void) override;

  void discover_nodes(void) override;
  void query_node(const uint16_t node_number) override;

private:
#if HAS_EXT_MCP2515
  ACAN2515 *can;
#endif
};

///

class network final : public abstract_interface {
public:
  network(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led);

  bool begin(const bool force_enumeration) override;
  void enable(const bool new_state) override;
  void run(void) override;
  bool forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) override;
  bool available(void) override;
  bool get_message(can_msg_wrapper_t *wmsg) override;
  byte get_status(void) override;

  void enumerate_bus(const bool force = false) override;
  void respond_to_enumeration_request(void) override;

  void discover_nodes(void) override;
  void query_node(const uint16_t node_number) override;

private:
  bool bridge_connected;
  bool reconnect_required;
  char bridge_input_buffer[NET_BUFFER_SIZE];
  char bridge_work_buffer[GC_BUFFER_SIZE];
  byte bridge_work_buffer_idx;
  byte network_connection_state, network_status, last_network_status;
  byte num_current_connections;
  unsigned int udp_sequence_num_tx, udp_sequence_num_rx;
  uint16_t port;
  unsigned long last_network_connection_attempt;

#if defined(IS_NETWORK_DEVICE)
  Wiznet5500lwIP *eth;
  WiFiServer *server;
  WiFiClient *bridge_client;
  WiFiUDP *udp;

  client_connection_t connection_tab[MAX_CLIENTS];
#endif
  bool forward_message_to_net_clients(const byte source, const char *message);
};

///

class gridconnect_serial final : public abstract_interface {
public:
  gridconnect_serial(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led);

  bool begin(const bool force_enumeration) override;
  void enable(const bool new_state) override;
  void run(void) override;
  bool forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) override;
  bool available(void) override;
  bool get_message(can_msg_wrapper_t *wmsg) override;
  byte get_status(void) override;

private:
  char input_buffer[GC_BUFFER_SIZE];
  char message_buffer[GC_BUFFER_SIZE];
  byte message_buffer_index;

  HardwareSerial *serial_instance;
};

///
