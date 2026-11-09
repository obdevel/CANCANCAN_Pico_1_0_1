#include <cstddef>

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

#include "cbusdefs.h"

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"
#include "filter_engine.h"
#include "router.h"
#include "interface.h"

//
/// can bus discoverer
//

can_bus_discoverer::can_bus_discoverer() {

  VLOG("can_bus_discoverer: constructor");

  memset(can_nodes, 0, sizeof(can_nodes));
  memset(consumer_events, 0, sizeof(consumer_events));

  nodes_index = 0;
  events_index = 0;
}

// consume an incoming message and update the map database

void can_bus_discoverer::consume_message(const byte interface_num, const can_msg_wrapper_t *wmsg) {

  bool found = false;
  uint16_t nn;

  VLOG("can_bus_discoverer: consume_message: state = %u, interface = %u, opcode = %s", enabled, interface_num, lookup_opcode_by_value(wmsg->msg.data[0]));

  if (enabled) {

    switch (wmsg->msg.data[0]) {

      case OPC_PNN:

        nn = (wmsg->msg.data[1] << 8) + wmsg->msg.data[2];

        for (int i = 0; i < MAX_CAN_NODES && i < nodes_index; i++) {  // find existing entry for this node
          if (can_nodes[i].node_number == nn) {
            found = true;
            break;
          }
        }

        // add it to the database

        if (!found && nodes_index < MAX_CAN_NODES) {
          can_nodes[nodes_index].interface_num = interface_num;
          can_nodes[nodes_index].node_number = nn;
          can_nodes[nodes_index].manufacturer_id = wmsg->msg.data[3];
          can_nodes[nodes_index].module_id = wmsg->msg.data[4];
          can_nodes[nodes_index].flags = wmsg->msg.data[5];

          ++nodes_index;
          VLOG("can_bus_discoverer: consume_message: consumed OPC_PNN response from node = %u", nn);
        }

        break;

      case OPC_ENRSP:

        nn = (wmsg->msg.data[1] << 8) + wmsg->msg.data[2];

        for (int i = 0; i < MAX_CONSUMER_EVENTS && i < events_index; i++) {
          if (consumer_events[i].consumer_node_number == nn && consumer_events[i].b1 == wmsg->msg.data[3] && consumer_events[i].b2 == wmsg->msg.data[4] && consumer_events[i].b3 == wmsg->msg.data[5]
              && consumer_events[i].b4 == wmsg->msg.data[6]) {
            found = true;
            break;
          }
        }

        // add it to the database

        if (!found && events_index < MAX_CONSUMER_EVENTS) {
          consumer_events[events_index].interface_num = interface_num;
          consumer_events[events_index].consumer_node_number = nn;
          consumer_events[events_index].b1 = wmsg->msg.data[3];
          consumer_events[events_index].b2 = wmsg->msg.data[4];
          consumer_events[events_index].b3 = wmsg->msg.data[5];
          consumer_events[events_index].b4 = wmsg->msg.data[6];

          ++events_index;
          VLOG("can_bus_discoverer: consume_message: consumed OPC_ENRSP response from node = %u", nn);
        }

        break;
    }
  }

  return;
}

// return the bus interface for the given node number

bool can_bus_discoverer::get_interface_for_node(const uint16_t node_number, byte *interface_num) {

  bool found = false;

  for (size_t i = 0; i < nodes_index; i++) {
    if (can_nodes[i].node_number == node_number) {
      *interface_num = can_nodes[i].interface_num;
      found = true;
      break;
    }
  }

  return found;
}

// return a list of interfaces where there is a consumer of this event message

bool can_bus_discoverer::get_consumer_interfaces_for_event(const struct can2040_msg *msg, interface_list_t *interfaces) {

  struct can2040_msg msg_copy = *msg;  // take a copy to fiddle with

  VLOG("can_bus_discoverer: get_consumer_interfaces_for_event: ");

  // zero nn bytes for short events

  if (is_short_event_opcode(msg->data[0])) {
    msg_copy.data[1] = 0;
    msg_copy.data[2] = 0;
  }

  // default to no matching interfaces

  interfaces->num_interfaces = 0;

  // iterate over consumer events database

  for (size_t i = 0; i < MAX_CONSUMER_EVENTS && i < events_index; i++) {

    if (consumer_events[i].b1 == msg_copy.data[1] && consumer_events[i].b2 == msg_copy.data[2] && consumer_events[i].b3 == msg_copy.data[3] && consumer_events[i].b4 == msg_copy.data[4]) {

      // this entry matches the event message

      // have we already recorded this destination interface ?

      bool dupe = false;

      for (size_t j = 0; j < MAX_INTERFACES; j++) {
        if (interfaces->interface_list[j] == consumer_events[i].interface_num) {
          dupe = true;
          break;
        }
      }

      // if not, add the matching interface to the list of interfaces to be returned

      if (!dupe) {
        interfaces->interface_list[interfaces->num_interfaces] = consumer_events[i].interface_num;
        ++interfaces->num_interfaces;
      }
    }
  }

  return (interfaces->num_interfaces > 0);
}

void can_bus_discoverer::print_database(void) {

  VLOG("discoverer: nodes (%u)", nodes_index);

  for (int i = 0; i < nodes_index; i++) {
    VLOG("[%u] [%5u] [%3u] [%3u] [%3u]", can_nodes[i].interface_num, can_nodes[i].node_number,
         can_nodes[i].manufacturer_id, can_nodes[i].module_id, can_nodes[i].flags);
  }

  VLOG("discoverer: events (%u)", events_index);

  for (int i = 0; i < events_index; i++) {
    VLOG("[%u] [%5u] [%3u:%3u:%3u:%3u]", consumer_events[i].interface_num, consumer_events[i].consumer_node_number,
         consumer_events[i].b1, consumer_events[i].b2, consumer_events[i].b3, consumer_events[i].b4);
  }

  return;
}

///
