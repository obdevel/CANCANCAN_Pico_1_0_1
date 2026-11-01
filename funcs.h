
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

//
/// utils.h
//

#pragma once

#include <CBUS.h>
#include <can2040.h>

#include "defs.h"
#include "enums.h"
#include "types.h"

///

/// forward function declarations

int VLOG(const char *fmt, ...);

// bool create_message_queue(queue_t *queue, const size_t element_size, const unsigned int num_elements);

char *format_can_message(const CANFrame *msg);
char *format_can_message(const struct can2040_msg *msg);
char *format_can_message(const can_msg_wrapper_t *wmsg);

char *can2040_message_to_string(const struct can2040_msg *msg);

struct can2040_msg convert_cbus_to_can2040(const CANFrame *imsg);
CANFrame convert_can2040_to_cbus(const struct can2040_msg *imsg);

bool usb_power_detected(void);

int fnmatch(const char *pattern, const char *string, int flags);

bool is_opcode_of_message_class(const byte opcode, const byte message_class);
bool is_event_opcode(const struct can2040_msg *msg);
bool is_short_event_opcode(const struct can2040_msg *msg);
bool is_long_event_opcode(const struct can2040_msg *msg);

int lookup_opcode_by_name(const char *opcode_name);
const char *lookup_opcode_by_value(const byte opcode);
bool message_has_nn(const byte opcode);

bool mount_local_filesystem(void);
bool load_ruleset_file(const byte file_version, ruleset_t *ruleset, const bool reload_from_sdcard);
bool parse_rule_definition(char *line, rule_t *rule);
bool copy_files_from_sd_card(void);

bool load_ruleset_file(const byte file_version, ruleset_t *ruleset, const bool reload_from_sdcard = false);
bool parse_text_rule_definition(char *line, rule_t *rule);
void print_ruleset(void);
byte process_rules_file_as_text(File &f, ruleset_t *ruleset);
byte process_rules_file_as_json(File &f, ruleset_t *ruleset);

void modify_event_message_with_action(struct can2040_msg *msg, byte which_part, byte action, uint32_t value);
void modify_non_event_message_with_action(struct can2040_msg *msg, byte action, uint32_t value);

void convert_interface_list_string_to_interfaces_type(const char *string, interface_list_t *interfaces);
char *convert_interfaces_type_to_interface_list_string(const interface_list_t *interfaces, char *string);

byte convert_opcode_list_to_array(char *opcode_list, byte *arr);
bool convert_action(const char *action_text, action_t *action);
bool convert_message_class_list(const char *message_class_text, message_class_list_t *message_class_list);

void rewrite_can_id(can_msg_wrapper_t *wmsg, const byte canid);
void rewrite_can_id(CANFrame *msg, const byte canid);

///

void load_network_config(network_config_t *cfg);
void save_network_config(network_config_t *cfg);
void default_network_config(network_config_t *cfg);
void print_network_config(network_config_t *cfg);

///

bool CANtoGC(const struct can2040_msg *cf, char buffer[GC_BUFFER_SIZE]);
bool GCtoCAN(const char buffer[GC_BUFFER_SIZE], struct can2040_msg *cf);

///

void handle_root(void);
void handle_form(void);
void handle_not_found(void);

///

bool initialise_wifi(void);
void wifi_run(void);

///
