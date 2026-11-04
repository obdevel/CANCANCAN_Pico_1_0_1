
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

#include <CBUSconfig.h>
#include <CBUSLED.h>
#include "can2040.h"

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"
#include "filter_engine.h"
#include "router.h"
#include "interface.h"

/// globals defined in other files

extern queue_t bus0_input_queue, bus0_output_queue;  // cbus input and output queues - created in setup() on core 0
extern queue_t router_command_queue;                 // incoming commands from cbus events
extern CBUSConfig module_config;

extern CBUSLED activity_leds[4], error_led, heartbeat_led;

/// constructor

message_router::message_router() {

  VLOG("router: constructor");
  message_forwarding_enabled = false;

  for (byte i = 0; i < MAX_INTERFACES; i++) {
    interfaces[i] = nullptr;
  }
}

/// initialise the router, engine and secondary interfaces

bool message_router::begin(void) {

  bool ok = true;
  byte rules_file_version;

  /// read configuration nvs

  do_bus_enumeration_at_startup = module_config.readNV(7);
  filter_engine_type = module_config.readNV(9);
  rules_file_version = module_config.readNV(10);

  VLOG("router: begin, can bus enum at start = %u, engine type = %u", do_bus_enumeration_at_startup, filter_engine_type);

  /// initialise can bus discoverer

  if ((filter_engine_type == FILTER_ENGINE_DYNAMIC || filter_engine_type == FILTER_ENGINE_HYBRID || filter_engine_type == FILTER_ENGINE_USER)
      && (node_discovery_frequency_mins = module_config.readNV(15)) > 0) {
    VLOG("router: initialising can bus discoverer, discovery freq = %u mins", node_discovery_frequency_mins);
    discoverer = new can_bus_discoverer();
    discoverer->enabled = true;
  } else {
    VLOG("router: can bus discoverer not configured");
    discoverer = nullptr;
  }

  /// initialise filter engine

  VLOG("router: creating filter engine");

  if (filter_engine_type == FILTER_ENGINE_UNKNOWN || filter_engine_type >= NUM_FILTER_ENGINES) {  // sane default if junk value provided
    VLOG("router: *** invalid filter engine type, defaulting to PASS_ALL");
    filter_engine_type = FILTER_ENGINE_PASS_ALL;
    error_led.pulse();
  }

  switch (filter_engine_type) {

    case FILTER_ENGINE_PASS_ALL:
      VLOG("router: selected pass all engine");
      engine = new pass_all_filter_engine();  // forwards all messages
      ok = engine->begin();
      engine->ruleset = nullptr;
      break;

    case FILTER_ENGINE_CAN:
      VLOG("router: selected can filter engine");
      engine = new can_filter_engine();  // just filters enumeration traffic and rewrites CAN IDs
      ok = engine->begin();
      engine->ruleset = nullptr;
      break;

    case FILTER_ENGINE_CONFIGURABLE:
      VLOG("router: selected configurable filter engine, rules file version = %u", rules_file_version);
      engine = new configurable_filter_engine();  // configurable rules
      engine->begin(&rules_file_version);
      break;

    case FILTER_ENGINE_DYNAMIC:
      VLOG("router: selected dynamic filter engine");
      engine = new dynamic_filter_engine();  // dynamic engine
      ok = engine->begin(discoverer);        // pass in bus discoverer instance
      engine->ruleset = nullptr;
      break;

    case FILTER_ENGINE_HYBRID:
      VLOG("router: selected hybrid filter engine, rules file version = %u", rules_file_version);
      engine = new hybrid_filter_engine();  // hybrid engine
      ok = engine->begin(&rules_file_version, discoverer);
      break;

    case FILTER_ENGINE_USER:
      VLOG("router: selected user filter engine");
      engine = new user_filter_engine();  // user engine
      ok = engine->begin(discoverer);
      engine->ruleset = nullptr;
      break;
  }

  if (!ok) {
    VLOG("router: *** error initialising filter engine");
  }

  /// initialise the interfaces

  /// interface 0 is the primary cbus interface and is always configured and enabled
  /// it acts as a proxy shim for the the main cbus library interface

  VLOG("router: creating primary CBUS proxy interface");
  interfaces[0] = new cbus_interface(0, INTERFACE_TYPE_PRIMARY_CBUS, activity_leds[0], error_led);  // proxy primary cbus interface
  interfaces[0]->ciq = &bus0_input_queue;                                                           // these queues were already created by setup() on core 0
  interfaces[0]->coq = &bus0_output_queue;
  interfaces[0]->enabled = true;

  /// initialise secondary interfaces

  byte num_configured_interfaces = 0;

  VLOG("router: creating secondary interface(s)");

  /// configure the secondary interfaces ... 1, 2, 3, 4, 5

  num_configured_interfaces = 0;

  for (byte i = 1; i < MAX_INTERFACES; i++) {

    byte interface_type = module_config.readNV(i);  // nvs 1, 2, 3, 4
    VLOG("router: creating secondary interface %u, type = %u", i, interface_type);

    switch (interface_type) {

      case INTERFACE_TYPE_NOT_CONFIGURED:
        VLOG("router: interface %u is not configured", i);
        continue;
        break;

      case INTERFACE_TYPE_CAN_1:
        VLOG("router: interface %u type = CAN_1", i);
        interfaces[i] = new canbus(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_CAN_2:
#if defined(PICO_RP2350)
        VLOG("router: interface %u type = CAN_2", i);
        interfaces[i] = new canbus(i, interface_type, activity_leds[i - 1], error_led);
#else
        VLOG("router: *** interface %u type = CAN_2 is not available on RP2040", i);
        continue;
#endif
        break;

      case INTERFACE_TYPE_CAN_BACKBONE_1:
        VLOG("router: interface %u type = CAN_BACKBONE_1", i);
        interfaces[i] = new can_backbone(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_CAN_BACKBONE_2:
#if defined(PICO_RP2350)
        VLOG("router: interface %u type = CAN_BACKBONE_2", i);
        interfaces[i] = new can_backbone(i, interface_type, activity_leds[i - 1], error_led);
#else
        VLOG("router: *** interface %u type = CAN_BACKBONE_2 is not available on RP2040", i);
        continue;
#endif
        break;

      case INTERFACE_TYPE_CAN_FD_BACKBONE:
        VLOG("router: interface %u type = CAN_FD_BACKBONE", i);
        interfaces[i] = new canfdbus(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_EXT_MCP2515_CBUS:
        VLOG("router: interface %u type = EXT_MCP2515_CBUS", i);
        interfaces[i] = new canmcp2515(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_EXT_MCP2515_BACKBONE:
        VLOG("router: interface %u type = EXT_MCP2515_BACKBONE", i);
        interfaces[i] = new canmcp2515backbone(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_MCP2518_CBUS:
        VLOG("router: interface %u type = MCP2518_CBUS", i);
        interfaces[i] = new canmcp2518(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_WIFI_TCP_SERVER:
        VLOG("router: interface %u type = WIFI_TCP_SERVER", i);
        interfaces[i] = new network(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_WIFI_TCP_CLIENT:
        VLOG("router: interface %u type = WIFI_TCP_CLIENT", i);
        interfaces[i] = new network(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_WIFI_UDP_NODE:
        VLOG("router: interface %u type = WIFI_UDP_NODE", i);
        interfaces[i] = new network(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_ETHERNET_TCP_SERVER:
        VLOG("router: interface %u type = ETHERNET_TCP_SERVER", i);
        interfaces[i] = new network(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_ETHERNET_TCP_CLIENT:
        VLOG("router: interface %u type = ETHERNET_TCP_CLIENT", i);
        interfaces[i] = new network(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_ETHERNET_UDP_NODE:
        VLOG("router: interface %u type = ETHERNET_UDP_NODE", i);
        interfaces[i] = new network(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_GRIDCONNECT_USB:
        VLOG("router: interface %u type = GRIDCONNECT_USB", i);
        interfaces[i] = new gridconnect_serial(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_GRIDCONNECT_SERIAL_1:
        VLOG("router: interface %u type = GRIDCONNECT_SERIAL_1", i);
        interfaces[i] = new gridconnect_serial(i, interface_type, activity_leds[i - 1], error_led);
        break;

      case INTERFACE_TYPE_GRIDCONNECT_SERIAL_2:
        VLOG("router: interface %u type = GRIDCONNECT_SERIAL_2", i);
        interfaces[i] = new gridconnect_serial(i, interface_type, activity_leds[i - 1], error_led);
        break;

      default:
        VLOG("router: interface %u type %u is unknown", i, interface_type);
        continue;
        break;
    }

    ++num_configured_interfaces;
  }

  VLOG("router: %u secondary interfaces created", num_configured_interfaces);
  VLOG("router: initialising active interfaces");

  /// initialise secondary interfaces

  bool enable_secondary_interfaces_at_startup = module_config.readNV(6);

  for (byte i = 1; i < MAX_INTERFACES; i++) {
    if (interfaces[i] != nullptr) {

      VLOG("router: initialising secondary interface %u, type = %u", i, interfaces[i]->interface_type);

      if (!interfaces[i]->startup_ok) {
        VLOG("router: *** interface %u is in error and deconfigured", i);
        interfaces[i] = nullptr;
        continue;
      }

      interfaces[i]->default_action_when_source = module_config.readNV(20 + i);
      interfaces[i]->default_action_when_destination = module_config.readNV(26 + i);
      interfaces[i]->enable(enable_secondary_interfaces_at_startup);

      if (!(interfaces[i]->begin(do_bus_enumeration_at_startup))) {
        VLOG("router: begin failed for interface %u", i);
        interfaces[i] = nullptr;
      }
    }
  }

  // node discovery timers

  last_node_discovery_at = 0;
  node_discovery_delay_secs = module_config.readNV(14);
  node_query_required = false;

  // configure master interface list to default values ... 012345

  for (byte i = 0; i < MAX_INTERFACES; i++) {  // initialise to all
    default_interface_list.interface_list[i] = i;
  }

  default_interface_list.num_interfaces = MAX_INTERFACES;

  // complete

  VLOG("router: initialisation complete");

  return ok;
}

//
/// enable/disable message forwarding
//

void message_router::enable(const bool enabled) {

  message_forwarding_enabled = enabled;
  VLOG("router: enabled = %u", message_forwarding_enabled);

  return;
}

//
/// main run loop - called from loop()
//

void message_router::run(void) {

  static bool first_time = true;
  can_msg_wrapper_t wmsg;

  /// process commands received from core 0 thread

  router_command_t r;

  if (queue_try_remove(&router_command_queue, &r)) {
    VLOG("router: received command = %u/%u", r.state, r.command_num);
    process_command(r);
  }

  /// if router and discoverer are enabled, run a periodic bus discovery and query

  if (message_forwarding_enabled && discoverer != nullptr && discoverer->enabled) {

    /// run a periodic bus discovery - QNN requests PNN responses, which are consumed by the discoverer

    if ((first_time && millis() >= (node_discovery_delay_secs * 1000)) || (millis() - last_node_discovery_at >= (node_discovery_frequency_mins * 60000))) {

      VLOG("router: running bus discovery");

      first_time = false;
      last_node_discovery_at = millis();
      node_query_required = true;

      for (byte i = 0; i < MAX_INTERFACES; i++) {
        if (interfaces[i] != nullptr && interfaces[i]->enabled) {
          interfaces[i]->discover_nodes();
        }
      }
    }

    /// run a bus query after the discovery has completed - NERD requests ENRSP responses, which are consumed by the discoverer

    if (node_query_required && discoverer != nullptr && discoverer->enabled && (millis() - last_node_discovery_at >= BUS_QUERY_DELAY_MS)) {

      node_query_required = false;

      // send NERD event query to each discovered can node

      VLOG("router: querying discovered nodes");

      for (int i = 0; i < discoverer->nodes_index; i++) {

        if (interfaces[discoverer->can_nodes[i].interface_num] != nullptr && interfaces[discoverer->can_nodes[i].interface_num]->enabled) {
          VLOG("router: querying [%u] [%5u]", discoverer->can_nodes[i].interface_num, discoverer->can_nodes[i].node_number);

          interfaces[discoverer->can_nodes[i].interface_num]->query_node(discoverer->can_nodes[i].node_number);
          delay(2);
        }
      }
    }
  }

  /// process input message queues from all secondary interfaces

  for (byte i = 0; i < MAX_INTERFACES; i++) {

    if (interfaces[i] != nullptr) {

      /// allow the interface some time to run

      interfaces[i]->run();

      /// get next message from interface input queue, even if disabled, so as to drain queues

      if (interfaces[i]->get_message(&wmsg)) {

        VLOG("router: received message from interface %u: %s", i, format_can_message(&wmsg.msg));

        /// handle bus enumeration on secondary CAN interfaces - CBUS library handles primary interface

        if (interfaces[i]->interface_type == INTERFACE_TYPE_CAN_1 || interfaces[i]->interface_type == INTERFACE_TYPE_CAN_2
            || interfaces[i]->interface_type == INTERFACE_TYPE_EXT_MCP2515_CBUS || interfaces[i]->interface_type == INTERFACE_TYPE_MCP2518_CBUS) {

          /// respond to enumeration requests

          if (wmsg.msg.id & CAN2040_ID_RTR) {
            VLOG("router: interface %u: responding to enumeration request");
            interfaces[i]->respond_to_enumeration_request();
          }

          /// check for and resolve can id clash between this interface and another node on the same bus

          if ((!(wmsg.msg.id & CAN2040_ID_EFF)) && ((wmsg.msg.id & 0x7f) == interfaces[i]->canid)) {
            VLOG("router: interface %u: resolving canid clash for id %u", i, interfaces[i]->canid);
            interfaces[i]->enumerate_bus(true);
          }
        }

        /// pass various messages on to the can bus discoverer to populate its database

        if (discoverer != nullptr) {

          switch (wmsg.msg.data[0]) {
            case OPC_PNN:
            case OPC_ENRSP:
              discoverer->consume_message(i, &wmsg);
              break;
          }
        }

        /// route this message via filter engine to other interfaces if message forwardng and source interface are enabled

        if (message_forwarding_enabled && interfaces[i]->enabled) {
          route_single_message(i, &wmsg);
        }
      }
    }
  }

  return;
}

/// route a single message received from a source interface

void message_router::route_single_message(const byte source, can_msg_wrapper_t *wmsg) {

  char tmpb[16];         // scratch buffer
  unsigned long m1, m2;  // timestamps

  interface_list_t destination_interfaces;  // interfaces to forward to - may or may not be changed by the filter engine
  struct can2040_msg pmsg;                  // message copy
  filter_context_t context;                 // persistent state data passed to and from the filter engine

  VLOG("router: route_single_message: begins");

  destination_interfaces = default_interface_list;  // default the destination to all interfaces e.g. [01234]
  destination_interfaces.default_action_overrides = false;

  /// set the filter engine context

  context.process_all_matching_rules = true;  // in -> process all rules, not just the first match
  context.more_rules_exist = true;            // out -> whether there are more rules to process
  context.next_rule_to_process = 0;           // out/in -> the last rule processed - start at 0

  /// pass the message to the filter engine for rules processing

  byte rule_result = RULE_RESULT_UNKNOWN;

  while (context.more_rules_exist) {

    pmsg = wmsg->msg;  // make a copy of the input message for the filter engine to possibly rewrite

    /// call the filter engine

    m1 = micros();
    rule_result = engine->process_message(source, &pmsg, &context, &destination_interfaces);
    m2 = micros() - m1;

    VLOG("router: filter engine returns %u, in %u us, destination interfaces = %s",
         rule_result, m2, convert_interfaces_type_to_interface_list_string(&destination_interfaces, tmpb));

    /// apply source interface default action if no rule matches

    if (rule_result == RULE_RESULT_NO_MATCH) {
      rule_result = interfaces[source]->default_action_when_source;
      destination_interfaces.default_action_overrides = true;
      VLOG("router: no rule matches - applying source interface default rule = %u", rule_result);
    }

    /// process the returned action from the filter engine

    switch (rule_result) {

      case RULE_RESULT_DROP_MESSAGE:  // do nothing
        VLOG("router: drop message");
        break;

      case RULE_RESULT_FORWARD_MESSAGE:  // forward this message - but don't touch its can id
        VLOG("router: forward message without rewrite");
        wmsg->msg = pmsg;
        forward_message_to_interfaces(source, wmsg, &destination_interfaces, false);
        break;

      case RULE_RESULT_REWRITE_AND_FORWARD_MESSAGE:  // rewrite can id and forward this message
        VLOG("router: rewrite message and forward");
        wmsg->msg = pmsg;
        forward_message_to_interfaces(source, wmsg, &destination_interfaces, true);
        break;
    }
  }

  VLOG("router: route_single_message: complete");
  return;
}

/// forward an incoming message to a list of destination interfaces, excluding the source and inactive interfaces
/// optionally, get the target interfaces to rewrite the can id

void message_router::forward_message_to_interfaces(const byte source, can_msg_wrapper_t *wmsg, const interface_list_t *destination_interfaces, bool rewrite_id) {

  char tmpb[16];

  VLOG("router: forwarding message from source %u to destination interfaces %s, rewrite = %u",
       source, convert_interfaces_type_to_interface_list_string(destination_interfaces, tmpb), rewrite_id);

  for (byte i = 0; i < destination_interfaces->num_interfaces; i++) {  // loop over destination interfaces array, with checking

    if (destination_interfaces->interface_list[i] >= MAX_INTERFACES) {
      VLOG("router: *** invalid interface number = %u", destination_interfaces->interface_list[i]);
      continue;
    }

    if (destination_interfaces->interface_list[i] == source) {  // don't forward back to the source interface
      VLOG("router: not forwarding back to source interface %u", source);
      continue;
    }

    if (interfaces[destination_interfaces->interface_list[i]] == nullptr) {  // unconfigured interface
      VLOG("router: interface %u is not configured", destination_interfaces->interface_list[i]);
      continue;
    }

    if (!(interfaces[destination_interfaces->interface_list[i]]->enabled)) {  // only if the interface is currently enabled
      VLOG("router: interface %u is not enabled", destination_interfaces->interface_list[i]);
      continue;
    }

    if (destination_interfaces->default_action_overrides) {  // apply the default action for this destination interface

      byte destination_default_action = module_config.readNV(26 + i);

      switch (destination_default_action) {

        case RULE_RESULT_DROP_MESSAGE:
          VLOG("router: destination interface %u default action is to drop message", i);
          continue;
          break;

        case RULE_RESULT_FORWARD_MESSAGE:
          VLOG("router: destination interface %u default action is to forward message", i);
          rewrite_id = false;
          break;

        case RULE_RESULT_REWRITE_AND_FORWARD_MESSAGE:
          VLOG("router: destination interface %u default action is to rewrite and forward message", i);
          rewrite_id = true;
          break;

        default:
          VLOG("router: *** destination interface %u unknown default action %u", i, rewrite_id);
          continue;
          break;
      }
    }

    /// forward message to this interface

    VLOG("router: forwarding message via interface %u, num = %u, type = %u", destination_interfaces->interface_list[i],
         interfaces[destination_interfaces->interface_list[i]]->num, interfaces[destination_interfaces->interface_list[i]]->interface_type);

    if (!interfaces[destination_interfaces->interface_list[i]]->forward_message(wmsg, rewrite_id)) {
      VLOG("router: *** interface %u is unable to forward the message", destination_interfaces->interface_list[i]);
      error_led.pulse();
    } else {
      // VLOG("router: forwarded message ok");
    }
  }

  return;
}

///

void message_router::process_command(const router_command_t r) {

  VLOG("router: process_command: command = %u/%u", r.state, r.command_num);

  switch (r.command_num) {
    case 101 ... 105:  // enable/disable bus
      if (interfaces[r.command_num - 100] != nullptr) {
        interfaces[r.command_num - 100]->enable(r.state);
      }

      break;

    case 106 ... 110:  // enumerate bus
      if (interfaces[r.command_num - 105] != nullptr) {
        interfaces[r.command_num - 105]->enumerate_bus(r.state);
      }

      break;

    case 111 ... 115:  // discover nodes
      if (interfaces[r.command_num - 110] != nullptr) {
        interfaces[r.command_num - 110]->discover_nodes();
      }

      node_query_required = true;  // prompt node query
      break;

    case 116:  // enumerate all
      break;

    case 130:
      enable(r.state);  // enable/disable router
      break;

    case 140:  // enable/disable discoverer
      if (discoverer != nullptr) {
        discoverer->enabled = r.state;
      }

      break;
  }

  return;
}

///

void message_router::enumerate_all(void) {

  VLOG("router: enumerating all secondary buses");

  for (byte i = 1; i < MAX_INTERFACES; i++) {
    if (interfaces[i] != nullptr && interfaces[i]->enabled) {
      interfaces[i]->enumerate_bus();
    }
  }

  return;
}

///
