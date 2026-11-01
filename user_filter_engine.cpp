
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

#include <can2040.h>

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"
#include "filter_engine.h"
#include "discoverer.h"

/// global variables defined in other source file

extern CBUSConfig module_config;

/// global variables


//
/// user filter engine class - forward all by default
//

user_filter_engine::user_filter_engine() {

  /// constructor
}

user_filter_engine::~user_filter_engine() {

  /// destructor
}

bool user_filter_engine::begin([[maybe_unused]] void *argptr1, [[maybe_unused]] void *argptr2) {

  /// begin
  discoverer = (can_bus_discoverer *)(argptr1);
  return true;
}

byte user_filter_engine::process_message([[maybe_unused]] const byte source, [[maybe_unused]] struct can2040_msg *msg,
                                         [[maybe_unused]] filter_context_t *context, [[maybe_unused]] interface_list_t *destination_interfaces) {

  /// process a single message

  context->more_rules_exist = false;
  return RULE_RESULT_FORWARD_MESSAGE;
}

void user_filter_engine::process_command([[maybe_unused]] const byte r) {

  /// process a single command
}

///
