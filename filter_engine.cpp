
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

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"
#include "filter_engine.h"

#include "can2040.h"

extern CBUSConfig module_config;

//
/// pass-all engine - filters nothing
//

pass_all_filter_engine::pass_all_filter_engine() {
}

pass_all_filter_engine::~pass_all_filter_engine() {
}

bool pass_all_filter_engine::begin([[maybe_unused]] void *argptr1, [[maybe_unused]] void *argptr2) {

  VLOG("pass_all_filter_engine: begin");
  return true;
}

byte pass_all_filter_engine::process_message([[maybe_unused]] const byte source, [[maybe_unused]] struct can2040_msg *msg, filter_context_t *context, [[maybe_unused]] interface_list_t *destination_interfaces) {

  context->more_rules_exist = false;   // no more rules - don't call me again with this message
  return RULE_RESULT_FORWARD_MESSAGE;  // forward all messages unchanged
}

void pass_all_filter_engine::process_command(const byte r) {

  VLOG("pass_all_filter_engine: process_command: %u", r);
  return;
}

//
/// simple derived class - hardcoded forwarding rules, filters enumeration traffic, forwards everything else
//

can_filter_engine::can_filter_engine() {
}

can_filter_engine::~can_filter_engine() {
}

bool can_filter_engine::begin([[maybe_unused]] void *argptr1, [[maybe_unused]] void *argptr2) {  // this engine ignores the argument

  VLOG("can_filter_engine: begin");
  return true;
}

byte can_filter_engine::process_message([[maybe_unused]] const byte source, struct can2040_msg *msg, filter_context_t *context, [[maybe_unused]] interface_list_t *destination_interfaces) {

  byte retval;

  /// this engine does not update the destination interfaces

  context->more_rules_exist = false;  // no more rules - don't call me again with this message

  if (msg->id & CAN2040_ID_EFF) {
    retval = RULE_RESULT_FORWARD_MESSAGE;  // extended messages have no can id to rewrite and should not be touched
  } else if (msg->dlc == 0 || msg->id & CAN2040_ID_RTR) {
    retval = RULE_RESULT_DROP_MESSAGE;  // filter out bus enumeration traffic
  } else {
    retval = RULE_RESULT_REWRITE_AND_FORWARD_MESSAGE;  // otherwise, request rewrite of the can id for each target interface and forward the message
  }

  // VLOG("can_filter_engine: process_message: return = %u", retval);

  return retval;
}

void can_filter_engine::process_command([[maybe_unused]] const byte r) {

  VLOG("can_filter_engine: process_command: %u", r);
  return;
}

//
/// configurable derived class
/// uses a loadable ruleset file
//

configurable_filter_engine::configurable_filter_engine() {
}

configurable_filter_engine::~configurable_filter_engine() {
}

///

bool configurable_filter_engine::begin(void *argptr1, [[maybe_unused]] void *argptr2) {

  bool ok = true;

  rules_file_version = (*(byte *)argptr1);

  VLOG("configurable_filter_engine: begin, ruleset file version = %u", rules_file_version);

  ruleset = new ruleset_t;
  memset(ruleset, 0, sizeof(ruleset_t));  // reset and mark all inactive

  if (load_ruleset_file(rules_file_version, ruleset, module_config.readNV(11))) {
    VLOG("configurable_filter_engine: loaded ruleset");
  } else {
    VLOG("configurable_filter_engine: *** error loading ruleset");
    ok = false;
  }

  return ok;
}

byte configurable_filter_engine::process_message(const byte source, struct can2040_msg *msg, filter_context_t *context, interface_list_t *destination_interfaces) {

  bool match_flag = false;
  byte r;
  byte return_value = RULE_RESULT_NO_MATCH;

  bool msg_is_event = !(msg->id & CAN2040_ID_EFF) && !(msg->id & CAN2040_ID_RTR) && msg->dlc >= 5 && is_event_opcode(msg);

  /// check each rule in turn until a match is found (class, pattern) or none is matched

  VLOG("configurable_filter_engine: process_message begins at rule %u", context->next_rule_to_process);

  for (r = context->next_rule_to_process; r < MAX_RULE_ENTRIES; r++) {

    VLOG("configurable_filter_engine: + process_message: rule %u", r);

    if (!ruleset->rules[r].active) {  // no more rules
      VLOG("configurable_filter_engine: process_message: end of rules");
      break;
    }

    match_flag = false;

    /// 0. match source interface

    VLOG("configurable_filter_engine: matching source %u", source);

    for (byte i = 0; i < ruleset->rules[r].source_interfaces.num_interfaces; i++) {
      if (ruleset->rules[r].source_interfaces.interface_list[i] == source) {
        match_flag = true;
        break;
      }
    }

    if (!match_flag) {
      continue;
    }

    VLOG("configurable_filter_engine: source interface %u matched", source);

    /// 1. check against message class specifier(s)

    VLOG("configurable_filter_engine: matching message class");

    for (byte j = 0; j < ruleset->rules->message_classes.num_classes && j < MAX_CLASSES_PER_RULE; j++) {

      switch (ruleset->rules[r].message_classes.class_list[j]) {

        case MESSAGE_CLASS_ALL:
          break;

        case MESSAGE_CLASS_OPCODE:

          match_flag = false;

          // match the rule's opcode list

          for (byte i = 0; i < ruleset->rules[r].num_opcodes; i++) {
            if (ruleset->rules[r].opcodes[i] == msg->data[0]) {
              match_flag = true;
              break;
            }
          }

          if (!match_flag) {
            continue;
          }

          break;

        case MESSAGE_CLASS_EVENT:
          if (!msg_is_event) {
            match_flag = false;
            continue;
          }

          break;

        case MESSAGE_CLASS_LONG_EVENT:
          if (!!is_long_event_opcode(msg)) {
            match_flag = false;
            continue;
          }

          break;

        case MESSAGE_CLASS_SHORT_EVENT:
          if (!is_short_event_opcode(msg)) {
            match_flag = false;
            continue;
          }

          break;

        case MESSAGE_CLASS_ENUM:
          if (msg->dlc == 0 || msg->id & CAN2040_ID_RTR || is_opcode_of_message_class(msg->data[0], MESSAGE_CLASS_ENUM)) {
            match_flag = true;
          } else {
            match_flag = false;
            continue;
          }

          break;

        case MESSAGE_CLASS_EXTENDED:
          if (!(msg->id & CAN2040_ID_EFF)) {
            match_flag = false;
            continue;
          }

          break;

        default:
          match_flag = false;

          for (byte k = 0; k < ruleset->rules[r].message_classes.num_classes; k++) {
            if (!is_opcode_of_message_class(msg->data[0], ruleset->rules[r].message_classes.class_list[k])) {
              match_flag = true;
              break;
            }
          }

          break;
      }
    }

    if (!match_flag) {
      continue;
    }

    /// message class matched

    VLOG("configurable_filter_engine: message class matched");

    /// 2. match the event pattern - for events and non-event type messages containing an nn only

    VLOG("configurable_filter_engine: matching pattern");

    if (msg_is_event || message_has_nn(msg)) {

      bool matches[2] = { false, false };                   // not matched by default
      byte action_parts_to_check = (msg_is_event ? 2 : 1);  // only check first pattern part if non-event type message

      for (byte a = 0; a < action_parts_to_check; a++) {

        // * matches everything, so no need to check further

        if (strcmp(ruleset->rules[r].pattern_parts[a], "*") == 0) {
          matches[a] = true;
          continue;
        } else {
          break;
        }

        // if pattern part contains no metachars, compare using numeric values

        if (strpbrk(ruleset->rules[a].pattern_parts[a], GLOB_METACHARS) == NULL) {

          uint16_t val;

          if (a == 0) {
            val = (msg->data[1] << 8) + msg->data[2];  // the nn part
          } else {
            val = (msg->data[3] << 8) + msg->data[4];  // the en/dn part
          }

          if (val == atoi(ruleset->rules[r].pattern_parts[a])) {
            matches[a] = true;
            continue;
          } else {
            break;
          }
        }

        // convert message nn or en to string for glob pattern matching

        char msgpart[8];

        if (a == 0) {
          snprintf(msgpart, sizeof(msgpart), "%u", (msg->data[1] << 8) + msg->data[2]);
        } else {
          snprintf(msgpart, sizeof(msgpart), "%u", (msg->data[3] << 8) + msg->data[4]);
        }

        // check glob pattern match

        if (fnmatch(ruleset->rules[r].pattern_parts[a], msgpart, FNMATCH_FLAGS) == 0) {
          matches[a] = true;
          continue;
        } else {
          break;
        }
      }

      match_flag = true;

      for (byte a = 0; a < action_parts_to_check; a++) {
        if (!matches[a]) {
          match_flag = false;
        }
      }

      if (!match_flag) {
        continue;
      }
    }

    /// event message pattern matched

    VLOG("configurable_filter_engine: pattern was matched");

    /// execute the action(s)

    VLOG("configurable_filter_engine: executing action(s)");

    bool rule_has_event_message_class = false;

    for (byte a = 0; a < NUM_ACTIONS; a++) {
      if (ruleset->rules[r].actions[a].active) {

        switch (ruleset->rules[r].actions[a].action_type) {

          case ACTION_DROP:
            return_value = RULE_RESULT_DROP_MESSAGE;
            VLOG("configurable_filter_engine: action %u is drop", a);
            break;

          case ACTION_FORWARD:
            return_value = RULE_RESULT_FORWARD_MESSAGE;
            VLOG("configurable_filter_engine: action %u is forward", a);
            break;

          case ACTION_REWRITE:
            return_value = RULE_RESULT_REWRITE_AND_FORWARD_MESSAGE;
            VLOG("configurable_filter_engine: action %u is rewrite & forward", a);
            break;

          case ACTION_INCREMENT:
          case ACTION_DECREMENT:
          case ACTION_EQUALS:

            VLOG("configurable_filter_engine: action %u is modify and forward", a);

            for (byte k = 0; k < ruleset->rules[r].message_classes.num_classes; k++) {
              if (ruleset->rules[r].message_classes.class_list[k] == MESSAGE_CLASS_EVENT
                  || ruleset->rules[r].message_classes.class_list[k] == MESSAGE_CLASS_LONG_EVENT
                  || ruleset->rules[r].message_classes.class_list[k] == MESSAGE_CLASS_SHORT_EVENT) {
                rule_has_event_message_class = true;
                break;
              }
            }

            if (msg_is_event) {
              if (rule_has_event_message_class) {
                modify_event_message_with_action(msg, (a == 0 ? EVENT_PART_NN : EVENT_PART_EN),
                                                 ruleset->rules[r].actions[a].action_type, ruleset->rules[r].actions[a].value);
              }
            } else {
              modify_non_event_message_with_action(msg, ruleset->rules[r].actions[a].action_type, ruleset->rules[r].actions[a].value);
            }

            return_value = RULE_RESULT_REWRITE_AND_FORWARD_MESSAGE;
            break;

          default:
            return_value = RULE_RESULT_NO_MATCH;
            VLOG("configurable_filter_engine: *** action %u is unknown", a);
            continue;
            break;
        }  // switch action type
      }    // if action is active
    }      // for each action

    /// copy the rule's destination interfaces

    *destination_interfaces = ruleset->rules[r].destination_interfaces;

    /// update the filter context

    context->next_rule_to_process = r;

    // process more rules ?

    context->more_rules_exist = !(ruleset->rules[r].stop_on_match);

    /// return to router with disposition value

    VLOG("configurable_filter_engine: returning %u", return_value);
    return return_value;
  }  // for each rule

  /// if we reach here, no rule has matched the message

  VLOG("configurable_filter_engine: no more rules");

  context->more_rules_exist = false;  // no more rules
  return return_value;
}

///

void configurable_filter_engine::process_command(const byte r) {

  VLOG("configurable_filter_engine: process_command: %u", r);

  switch (r) {
    case 110 ... 118:
      // select rules file version
      rules_file_version = r - 110;
      break;

    case 120:
      // reload rules file
      if (load_ruleset_file(rules_file_version, ruleset, module_config.readNV(11))) {
        VLOG("configurable_filter_engine: loaded ruleset %u", rules_file_version);
      } else {
        VLOG("configurable_filter_engine: *** error loading ruleset");
      }

      break;
  }

  return;
}

///

void configurable_filter_engine::print_ruleset(void) {

  VLOG("configurable_filter_engine: print_ruleset");
  print_rules(ruleset);

  return;
}

//
/// dynamc filter engine - uses bus discovery to identify interfaces event messages should be forwarded to
//

dynamic_filter_engine::dynamic_filter_engine() {
}

//

dynamic_filter_engine::~dynamic_filter_engine() {
}

//

bool dynamic_filter_engine::begin(void *argptr1, [[maybe_unused]] void *argptr2) {

  discoverer = (can_bus_discoverer *)(argptr1);
  VLOG("dynamic_filter_engine: begin");

  return true;
}

//

byte dynamic_filter_engine::process_message([[maybe_unused]] const byte source, struct can2040_msg *msg, filter_context_t *context, interface_list_t *destination_interfaces) {

  VLOG("dynamic_filter_engine: process_message");

  byte return_value = RULE_RESULT_REWRITE_AND_FORWARD_MESSAGE;
  context->more_rules_exist = false;

  if (discoverer != nullptr) {
    if (!(msg->id & CAN2040_ID_EFF) && is_event_opcode(msg)) {
      if (discoverer->get_consumer_interfaces_for_event(msg, destination_interfaces)) {
        VLOG("dynamic_filter_engine: process_message: event message found in discoverer database");
      } else {
        VLOG("dynamic_filter_engine: process_message: event message is not in discoverer database");
        return_value = RULE_RESULT_NO_MATCH;
      }
    }
  }

  return return_value;
}

//

void dynamic_filter_engine::process_command(const byte r) {

  VLOG("dynamic_filter_engine: process_command: %u", r);
  return;
}

//
/// configurable derived class
/// uses a loadable ruleset file for non-event messages, and the discoverer for events
//

hybrid_filter_engine::hybrid_filter_engine() {
}

hybrid_filter_engine::~hybrid_filter_engine() {
}

///

bool hybrid_filter_engine::begin(void *argptr1, void *argptr2) {

  bool ok = true;

  rules_file_version = (*(byte *)argptr1);
  discoverer = (can_bus_discoverer *)argptr2;

  VLOG("hybrid_filter_engine: begin, ruleset file version = %u", rules_file_version);

  ruleset = new ruleset_t;
  memset(ruleset, 0, sizeof(ruleset_t));  // reset and mark all inactive

  if (load_ruleset_file(rules_file_version, ruleset, module_config.readNV(11))) {
    VLOG("hybrid_filter_engine: loaded ruleset");
  } else {
    VLOG("hybrid_filter_engine: *** error loading ruleset");
    ok = false;
  }

  return ok;
}

///

byte hybrid_filter_engine::process_message(const byte source, struct can2040_msg *msg, filter_context_t *context, interface_list_t *destination_interfaces) {

  bool match_flag = false;
  byte r;
  byte return_value = RULE_RESULT_NO_MATCH;

  bool msg_is_event = !(msg->id & CAN2040_ID_EFF) && is_event_opcode(msg);

  /// check each rule in turn until a match is received (class, pattern) or none is matched

  for (r = context->next_rule_to_process; r < MAX_RULE_ENTRIES; r++) {

    if (!ruleset->rules[r].active) {  // no more rules
      break;
    }

    match_flag = false;

    /// 0. match source interface

    for (byte i = 0; i < ruleset->rules[r].source_interfaces.num_interfaces; i++) {
      if (ruleset->rules[r].source_interfaces.interface_list[i] == source) {
        match_flag = true;
        break;
      }
    }

    if (!match_flag) {
      continue;
    }

    /// 1. check against message class specifier(s)

    for (byte j = 0; j < ruleset->rules->message_classes.num_classes && j < MAX_CLASSES_PER_RULE; j++) {

      switch (ruleset->rules[r].message_classes.class_list[j]) {

        case MESSAGE_CLASS_ALL:
          break;

        case MESSAGE_CLASS_OPCODE:

          match_flag = false;

          // match the rule's opcode list

          for (byte i = 0; i < ruleset->rules[r].num_opcodes; i++) {
            if (ruleset->rules[r].opcodes[i] == msg->data[0]) {
              match_flag = true;
              break;
            }
          }

          if (!match_flag) {
            continue;
          }

          break;

        case MESSAGE_CLASS_EVENT:
          if (!msg_is_event) {
            continue;
          }

          break;

        case MESSAGE_CLASS_ENUM:
          if (!(msg->dlc == 0 || msg->id & CAN2040_ID_RTR)) {
            continue;
          }

          break;

        case MESSAGE_CLASS_EXTENDED:
          if (!(msg->id & CAN2040_ID_EFF)) {
            continue;
          }

          break;

        default:
          match_flag = false;

          for (byte k = 0; k < ruleset->rules[r].message_classes.num_classes; k++) {
            if (!is_opcode_of_message_class(msg->data[0], ruleset->rules[r].message_classes.class_list[k])) {
              match_flag = true;
              break;
            }
          }

          break;
      }
    }

    /// message class matched

    /// 2. match the event pattern - for events only

    if (msg_is_event) {
      char nn[8], en[8];

      match_flag = false;

      if (ruleset->rules[r].pattern_parts[0][0] == '*' && ruleset->rules[r].pattern_parts[1][0] == '*') {  // *:* matches every event
        match_flag = true;

      } else {  // match glob pattern

        snprintf(nn, sizeof(nn), "%u", (msg->data[1] << 8) + msg->data[2]);  // convert nn and en to strings for matching
        snprintf(en, sizeof(en), "%u", (msg->data[3] << 8) + msg->data[4]);

        /// if the glob pattern parts aren't empty and match both nn and en ...

        if (ruleset->rules[r].pattern_parts[0][0] != 0 && fnmatch(ruleset->rules[r].pattern_parts[0], nn, FNMATCH_FLAGS) == 0) {
          if (ruleset->rules[r].pattern_parts[1][0] != 0 && fnmatch(ruleset->rules[r].pattern_parts[1], en, FNMATCH_FLAGS) == 0) {
            match_flag = true;
          }
        }
      }

      if (!match_flag) {
        continue;
      }
    }

    /// event message pattern matched

    /// apply the action(s)

    bool rule_has_event_message_class = false;

    for (byte a = 0; a < NUM_ACTIONS; a++) {
      if (ruleset->rules[r].actions[a].active) {

        switch (ruleset->rules[r].actions[a].action_type) {

          case ACTION_DROP:
            return_value = RULE_RESULT_DROP_MESSAGE;
            break;

          case ACTION_FORWARD:
            return_value = RULE_RESULT_REWRITE_AND_FORWARD_MESSAGE;
            break;

          case ACTION_INCREMENT:
          case ACTION_DECREMENT:
          case ACTION_EQUALS:

            for (byte k = 0; k < ruleset->rules[r].message_classes.num_classes; k++) {
              if (ruleset->rules[r].message_classes.class_list[k] == MESSAGE_CLASS_EVENT) {
                rule_has_event_message_class = true;
                break;
              }
            }

            if (msg_is_event && rule_has_event_message_class) {
              modify_event_message_with_action(msg, (a == 0 ? EVENT_PART_NN : EVENT_PART_EN),
                                               ruleset->rules[r].actions[a].action_type, ruleset->rules[r].actions[a].value);
            } else {
              modify_non_event_message_with_action(msg, ruleset->rules[r].actions[a].action_type, ruleset->rules[r].actions[a].value);
            }

            return_value = RULE_RESULT_REWRITE_AND_FORWARD_MESSAGE;
            break;

          default:
            return_value = RULE_RESULT_NO_MATCH;
            continue;
            break;
        }
      }
    }

    /// for event-type messages, use the discoverer for destination interfaces

    if (msg_is_event) {
      if (discoverer != nullptr) {
        if (discoverer->get_consumer_interfaces_for_event(msg, destination_interfaces)) {
          VLOG("hybrid_filter_engine: process_message: event message found in discoverer database");
          return_value = RULE_RESULT_REWRITE_AND_FORWARD_MESSAGE;
        } else {
          VLOG("hybrid_filter_engine: process_message: event message is not in discoverer database");
          return_value = RULE_RESULT_NO_MATCH;
        }
      }
    } else {
      /// copy the rule's destination interfaces
      *destination_interfaces = ruleset->rules[r].destination_interfaces;
    }

    /// update the rule context

    context->next_rule_to_process = r + 1;

    /// return with disposition value

    return return_value;
  }

  /// if we reach here, no rule has matched the message
  /// each interface will apply its default action

  context->more_rules_exist = false;  // no more rules to process
  return return_value;
}

///

void hybrid_filter_engine::process_command(const byte r) {

  VLOG("hybrid_filter_engine: process_command: %u", r);

  switch (r) {
    case 111 ... 117:
      /// *** TODO *** select rules file number
      break;

    case 120:
      /// *** TODO *** reload rules file
      break;
  }

  return;
}

///

void hybrid_filter_engine::print_ruleset(void) {

  VLOG("hybrid_filter_engine: print_ruleset");
  return;
}

///
