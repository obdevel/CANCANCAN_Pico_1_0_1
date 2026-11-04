
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

#include <Arduino.h>
#include <can2040.h>

#if defined(ARDUINO_WIZNET_5500_EVB_PICO) || defined(ARDUINO_WIZNET_5500_EVB_PICO2) || defined(ARDUINO_RASPBERRY_PI_PICO_W) || defined(ARDUINO_RASPBERRY_PI_PICO_2W)
#include <WiFi.h>
#endif

#include "defs.h"

///

typedef struct {  // can node info returned in PNN message
  byte interface_num;
  uint16_t node_number;
  byte manufacturer_id;
  byte module_id;
  byte flags;
} can_node_t;

//

typedef struct {  // consumer event info returned in ENRSP message
  byte interface_num;
  uint16_t consumer_node_number;
  byte b1, b2, b3, b4;
} consumer_event_t;

//

typedef struct {
  bool process_all_matching_rules;  // in
  bool more_rules_exist;            // out
  byte next_rule_to_process;        // in / out
} filter_context_t;

//

typedef struct {
  byte num_interfaces;
  byte interface_list[MAX_INTERFACES];
  bool default_action_overrides;
} interface_list_t;

//

typedef struct {
  byte num_classes;
  byte class_list[MAX_CLASSES_PER_RULE];
} message_class_list_t;

//

typedef struct {
  bool active;       // rule is active
  byte action_type;  // drop, forward, rewrite, equals, increment, decrement, etc
  uint32_t value;    // increment or decrement amount
} action_t;

//

typedef struct {
  bool active;                                                 // entry is valid and active
  interface_list_t source_interfaces, destination_interfaces;  // list of interfaces to accept from and forward to, 0, 1, 2, ...
  message_class_list_t message_classes;                        // event, message, CBUS opcode, etc
  byte opcodes[MAX_OPCODES_PER_RULE];                          // list of CBUS opcodes
  byte num_opcodes;                                            // the number of opcodes in the list
  byte pattern_type;                                           // whether opcode list or event pattern
  char pattern[MAX_PATTERN_LEN];                               // pattern expression to match
  char pattern_parts[16][2];                                   // the two parts of the event pattern broken out
  char action_text[MAX_ACTION_TEXT_LEN];                       // the action text
  byte num_actions;                                            // number of active parts, 0, 1 or 2
  action_t actions[2];                                         // an action for each of NN and EN/DN parts
  bool stop_on_match;                                          // whether to process further rules if this one matches
} rule_t;

//

typedef struct {
  byte file_version;               // the file version number that was loaded
  rule_t rules[MAX_RULE_ENTRIES];  // the list of rules read from the config file
} ruleset_t;

//

typedef struct {
  struct can2040_msg msg;
  uint32_t timestamp;
  uint32_t retries;
} can_msg_wrapper_t;

//

typedef struct {
  uint sequence_num;
  can_msg_wrapper_t wmsg;
} udp_can_message_t;

//

typedef struct {
  uint32_t num_msgs_received;
  uint32_t num_msgs_sent;
  uint32_t num_transmit_retries;
  uint32_t num_errors;
  uint32_t lowest_latency;
  uint32_t highest_latency;
  uint32_t average_latency;
} interface_stats_t;

//

typedef struct {
  const char *opcode_name;
  byte opcode;
  byte message_class;
  bool has_nn;
} opcode_t;

//

typedef struct {
  byte wifi_mode;        // STA or AP
  char hostname[32];     // DNS hostname
  char ssid[32];         // wfi SSID
  char password[32];     // wifi password
  char ap_ssid[32];      // access point SSID
  char ap_password[32];  // access point password
  bool hide_ap;          // hide AP
  byte ap_channel;       // channel in AP mode
  char servicename[32];  // DNS-SD service name
  char server[32];       // server (if net client)
  uint16_t port;         // network port number
  char servername[32];   // server name to connect to if client
  byte check_value;      // set to 99 if config is valid
} network_config_t;

//

typedef struct {  // a command from cbus event handler on core 0 to the router on core 1
  bool state;
  byte command_num;
} router_command_t;

//

#if defined(IS_NETWORK_DEVICE)
typedef struct {
  uint8_t status;
  WiFiClient *client;
  char ipaddress[24];
  char input_buffer[NET_BUFFER_SIZE];
  char work_buffer[GC_BUFFER_SIZE];
  byte idx;
} client_connection_t;
#endif

///
