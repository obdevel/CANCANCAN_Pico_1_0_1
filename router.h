
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

#include <CBUS.h>
#include <CBUSACAN2040.h>
#include <CBUSLED.h>

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"
#include "interface.h"
#include "filter_engine.h"
#include "discoverer.h"

/// message router

class message_router final {
public:
  message_router();

  bool begin(void);
  void run(void);
  void enable(const bool enabled);

  can_bus_discoverer *discoverer;
  abstract_filter_engine *engine;

private:
  void route_single_message(const byte source, can_msg_wrapper_t *wmsg);
  void forward_message_to_interfaces(const byte source, can_msg_wrapper_t *wmsg, const interface_list_t *destination_interfaces, bool rewrite_id);
  void enumerate_all(void);
  void process_command(const router_command_t r);

  bool do_bus_enumeration_at_startup;
  bool run_bus_discovery_at_start;
  bool message_forwarding_enabled;
  bool node_query_required;

  byte filter_engine_type;
  byte node_discovery_delay_secs;
  byte node_discovery_frequency_mins;

  unsigned long last_node_discovery_at;

  abstract_interface *interfaces[MAX_INTERFACES];
  interface_list_t default_interface_list;
};

///
