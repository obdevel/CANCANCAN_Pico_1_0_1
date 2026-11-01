
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

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"

#include <can2040.h>

/// class to manage a database of discovered nodes and consumer events

class can_bus_discoverer final {
public:

  can_bus_discoverer();
  void consume_message(const byte interface_num, const can_msg_wrapper_t *wmsg);
  bool get_interface_for_node(const uint16_t node_number, byte *interface_num);
  bool get_consumer_interfaces_for_event(const struct can2040_msg *msg, interface_list_t *interfaces);
  void print_database(void);

  bool enabled;
  int nodes_index;
  int events_index;

  can_node_t can_nodes[MAX_CAN_NODES];                    // all nodes on all CAN buses, from QNN/PNN
  consumer_event_t consumer_events[MAX_CONSUMER_EVENTS];  // all consumer events on all nodes, from NERD/ENRSP

private:
};

///
