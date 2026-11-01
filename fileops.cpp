#include <cstring>

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

/*
  Rule definition:

  5 space-delimted tokens

  source-interfaces message-class pattern action destination-interfaces

  for every message
    from a source interfaces ?
      matches message class ?
        matches nn:en or opcode pattern ? (events & opcode only)
          execute action - forward, drop, rewrite
            copy in destintion interfaces
              return stop-on-match

  e.g.

  [01234] EVENT *:* F [01234] Y     - from all interfaces, event message class, match all, forward, to all interfaces

  [0] DCC * D [0] N                 - from interface 0, DCC message class, match all, drop

  [1] OPCODE 23,24 D [1] N         - from interface 1, opcode class, match specific, drop

*/

#include <ArduinoJson.h>

#include <cstdlib>
#include <cstddef>

#include <SPI.h>
#include <SDFS.h>
#include <LittleFS.h>
#include <FS.h>

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"

extern CBUSConfig module_config;
extern CBUSLED error_led;

static bool filesystem_mounted = false;

//

bool mount_local_filesystem(void) {

  VLOG("mounting flash filesystem");

  /// configure filesystem driver

  LittleFSConfig cfg;
  cfg.setAutoFormat(true);
  LittleFS.setConfig(cfg);

  /// mount local flash filesystem

  if (!LittleFS.begin()) {
    VLOG("*** unable to mount filesystem");
    error_led.pulse();
    return false;
  }

  /// filesystem info

  struct FSInfo fsinfo;

  if (LittleFS.info(fsinfo)) {
    VLOG("filesystem: total bytes = %llu, used = %llu", fsinfo.totalBytes, fsinfo.usedBytes);
  }

  /// list current contents

  VLOG("current directory contents - ");
  Dir dir = LittleFS.openDir("/");

  while (dir.next()) {
    VLOG(" * %14s = %u bytes", dir.fileName().c_str(), dir.fileSize());
  }

  return true;
}

/// copy configuration files from SD card to local filesystem

bool copy_files_from_sd_card(void) {

  char filename[32];
  byte buffer[FILE_COPY_BUFFER_SIZE];
  int files_copied = 0;
  size_t r, bytes_copied = 0, total_bytes = 0;
  File f1, f2;

  VLOG("copy_files_from_sd_card: copying ruleset files from SD card");

  /// SD card SPI bus configuration

  SPI.setTX(SD_MOSI_PIN);
  SPI.setRX(SD_MISO_PIN);
  SPI.setSCK(SD_SCK_PIN);
  SPI.setCS(SD_CS_PIN);
  SPI.begin();

  /// SD driver configuration

  SDFSConfig cfg;
  cfg.setSPI(SPI);
  cfg.setCSPin(SD_CS_PIN);
  cfg.setAutoFormat(false);
  SDFS.setConfig(cfg);

  /// check for SD card

  VLOG("copy_files_from_sd_card: mounting SD card ...");

  if (!SDFS.begin()) {
    VLOG("copy_files_from_sd_card: *** unable to mount SD card");
    error_led.pulse();
  } else {

    /// SD filesystem info

    struct FSInfo fsinfo;

    if (SDFS.info(fsinfo)) {
      VLOG("copy_files_from_sd_card: SD card: total = %llu, used = %llu", fsinfo.totalBytes, fsinfo.usedBytes);
    }

    VLOG("copy_files_from_sd_card: SD card mounted ok, checking for files ...");

    /// check for config file(s)

    for (byte i = 0; i < MAX_RULESET_FILES; i++) {

      for (byte ft = 0; ft < 2; ft++) {

        if (i == 0) {
          strcpy(filename, (ft == 0 ? RULESET_TEXT_FILENAME : RULESET_JSON_FILENAME));
        } else {
          sprintf(filename, (ft == 0 ? RULESET_TEXT_FILESPEC : RULESET_JSON_FILESPEC), i);
        }

        if (!SDFS.exists(filename)) {
          VLOG("copy_files_from_sd_card: *** SD card does not contain ruleset file = %s", filename);
          continue;

        } else {

          if (LittleFS.exists(filename)) {
            VLOG("copy_files_from_sd_card: destination file exists, will overwrite");
          }

          VLOG("copy_files_from_sd_card: copying file %s from SD card", filename);

          if (!(f1 = SDFS.open(filename, "r"))) {
            VLOG("copy_files_from_sd_card: *** unable to open source file");
            error_led.pulse();
          }

          if (!(f2 = LittleFS.open(filename, "w"))) {
            VLOG("copy_files_from_sd_card: *** unable to open destination file");
            f1.close();
            error_led.pulse();
          }

          bytes_copied = 0;

          VLOG("copy_files_from_sd_card: copying file, size = %u ...", f1.size());

          while ((r = f1.read(buffer, FILE_COPY_BUFFER_SIZE)) > 0) {
            f2.write(buffer, r);
            bytes_copied += r;
          }

          f1.close();
          f2.close();
          ++files_copied;

          VLOG("copy_files_from_sd_card: file copy complete, wrote %u bytes", bytes_copied);
          total_bytes += bytes_copied;
        }
      }
    }

    VLOG("copy_files_from_sd_card: copied %u files, %u bytes", files_copied, total_bytes);
    SDFS.end();
    VLOG("copy_files_from_sd_card: safe to remove SD card");
  }

  return (files_copied > 0);
}

/// load config from local file into memory structure

bool load_ruleset_file(const byte file_version, ruleset_t *ruleset, const bool reload_from_sdcard) {

  char filename[32];
  byte num_rules_processed = 0;
  File f;

  VLOG("load_ruleset_file: loading ruleset file ...");

  // check rules file encoding type

  byte rules_file_encoding_tyoe = module_config.readNV(16);

  switch (rules_file_encoding_tyoe) {

    case RULES_FILE_TYPE_TEXT:
      VLOG("load_ruleset_file: expecting text encoded rules file");
      break;

    case RULES_FILE_TYPE_JSON:
      VLOG("load_ruleset_file: expecting json encoded rules file");
      break;

    default:
      VLOG("load_ruleset_file: *** unknown rules file encoding type");
      return false;
      break;
  }

  // mount localfilesystem if required

  if (!filesystem_mounted) {
    VLOG("load_ruleset_file: mounting filesystem");

    if (!(filesystem_mounted = mount_local_filesystem())) {
      error_led.pulse();
      return false;
    }
  }

  /// attempt to load updated config files from SD card

  if (reload_from_sdcard) {
    if (copy_files_from_sd_card()) {
      VLOG("load_ruleset_file: new file(s) loaded from SD card");
    } else {
      VLOG("load_ruleset_file: no files loaded from SD card");
    }
  }

  /// determine filename version

  if (file_version == 0) {
    strcpy(filename, (rules_file_encoding_tyoe == RULES_FILE_TYPE_TEXT ? RULESET_TEXT_FILENAME : RULESET_JSON_FILENAME));
  } else {
    sprintf(filename, (rules_file_encoding_tyoe == RULES_FILE_TYPE_TEXT ? RULESET_TEXT_FILESPEC : RULESET_JSON_FILESPEC), file_version);
  }

  /// open config file from local filesystem

  if (!LittleFS.exists(filename)) {
    VLOG("load_ruleset_file: *** local ruleset file %s does not exist", filename);
    error_led.pulse();
    return false;
  }

  VLOG("load_ruleset_file: loading ruleset from file %s", filename);

  if (!(f = LittleFS.open(filename, "r"))) {
    VLOG("load_ruleset_file: *** unable to open %s for reading", filename);
    error_led.pulse();
    return false;
  }

  f.setTimeout(0);

  VLOG("load_ruleset_file: processing rules file");
  ruleset->file_version = file_version;

  // parse the contents of the open file into a ruleset structure, according to encoding

  if (rules_file_encoding_tyoe == RULES_FILE_TYPE_TEXT) {
    num_rules_processed = process_rules_file_as_text(f, ruleset);
  } else {
    num_rules_processed = process_rules_file_as_json(f, ruleset);
  }

  VLOG("load_ruleset_file: ruleset load complete, %u rules loaded", num_rules_processed);
  f.close();

  return true;
}

/// parse a rules file encoded as text

byte process_rules_file_as_text(File &f, ruleset_t *ruleset) {

  char line_buffer[MAX_RULE_LINE_LENGTH];
  unsigned int line_num = 0, rule_num = 0;
  size_t r;

  VLOG("process_rules_file_as_text: processing input file...");

  while ((r = f.readBytesUntil('\n', line_buffer, MAX_RULE_LINE_LENGTH - 1)) > 0) {

    line_buffer[r] = 0;  // terminate
    ++line_num;

    if (line_buffer[r - 1] == '\r') {  // remove trailing CR if present
      line_buffer[r - 1] = 0;
    }

    VLOG("load_ruleset_file: read line: [%3u] [%s]", r, line_buffer);

    /// discard comments and blank lines

    if (line_buffer[0] == '#' || line_buffer[0] == '/' || line_buffer[0] == 0) {
      continue;
    }

    /// parse this line

    if (parse_text_rule_definition(line_buffer, &ruleset->rules[rule_num])) {
      VLOG("load_ruleset_file: parsed rule %u at line %u ok", rule_num, line_num);
      ++rule_num;
    } else {
      VLOG("load_ruleset_file: failed to parse rule at line %u", line_num);
    }
  }

  return rule_num;
}

/// parse a rules file encoded as json

byte process_rules_file_as_json(File &f, ruleset_t *ruleset) {

  uint8_t *json_text;
  byte num_rules = 0;
  size_t s, r;

  JsonDocument json_doc;  // the parsed json document
  JsonArray arr;          // representation of the rules list

  s = min(MAX_JSON_DATA_SIZE, f.size());

  if ((json_text = (uint8_t *)malloc(s)) == nullptr) {
    VLOG("process_rules_file_as_json: *** out of memory");
    return 0;
  }

  VLOG("process_rules_file_as_json: processing input file...");

  r = f.read(json_text, MAX_JSON_DATA_SIZE);

  VLOG("process_rules_file_as_json: read %u bytes", r);

  DeserializationError json_error = deserializeJson(json_doc, json_text);

  if (json_error) {
    VLOG("process_rules_file_as_json: *** json deserialisation error %u/%s", json_error.code(), json_error.c_str());
    goto bailout;
  }

  // arr = json_doc.as<JsonArray>();
  arr = json_doc["rules"].as<JsonArray>();

  for (JsonVariant value : arr) {
    VLOG(" * %s", value["source"].as<const char *>());
  }

  VLOG("process_rules_file_as_json: deserialised ok");

bailout:
  free(json_text);
  return num_rules;
}

/// parse a text-encoded line from a ruleset file into a rule_t structure

bool parse_text_rule_definition(char *line, rule_t *rule) {

  bool convert_ok = true;
  bool rule_has_event_message_class = false, rule_has_opcode_message_class = false;
  char tokens[5][32], *ptr, *dupe, *dupe2, *dupe3;
  byte part = 0;
  byte token_count = 0;

  VLOG("parse_text_rule_definition: parsing line [%s]", line);

  /// split into 5 space- or tab-delimited tokens
  /// source-interfaces message-class(s) pattern action destination-interfaces

  dupe = strdup(line);
  ptr = strtok(dupe, " \t");

  while (ptr && token_count < TOKENS_PER_RULE) {
    VLOG(" * %u -> %s", token_count, ptr);
    strncpy(tokens[token_count++], ptr, sizeof(tokens[0]));
    ptr = strtok(NULL, " \t");
  }

  VLOG("parse_text_rule_definition: read %u tokens", token_count);

  if (token_count != TOKENS_PER_RULE) {
    VLOG("parse_text_rule_definition: *** wrong number of rule tokens, expected %u", TOKENS_PER_RULE);
    convert_ok = false;
    goto cleanup;
  }

  /// parse this rule and populate rule_t structure
  /// process each token in turn

  // 0. convert the input interface list to an array, e.g. [0123] -> ...

  convert_interface_list_string_to_interfaces_type(tokens[0], &rule->source_interfaces);

  // 1. convert the message class list, e.g. DCC,MISC -> ...

  if (!convert_message_class_list(tokens[1], &rule->message_classes)) {
    VLOG("parse_text_rule_definition: *** error in message class list");
    convert_ok = false;
    goto cleanup;
  }

  rule->pattern_type = PATTERN_TYPE_NONE;

  for (byte k = 0; k < rule->message_classes.num_classes; k++) {
    if (rule->message_classes.class_list[k] == MESSAGE_CLASS_EVENT) {
      rule_has_event_message_class = true;
      rule->pattern_type = PATTERN_TYPE_EVENT;
      break;
    }
  }

  for (byte k = 0; k < rule->message_classes.num_classes; k++) {
    if (rule->message_classes.class_list[k] == MESSAGE_CLASS_OPCODE) {
      rule_has_opcode_message_class = true;
      rule->pattern_type = PATTERN_TYPE_OPCODE_LIST;
      break;
    }
  }

  // 2. pattern - only meaningful for opcodes or event message filters

  // doesn't make sense to specify both opcode and event messages classes
  // because we can't provide patterns for both

  if (rule_has_event_message_class && rule_has_opcode_message_class) {
    VLOG("parse_text_rule_definition: *** cannot specify both opcode and event message classes");
    convert_ok = false;
    goto cleanup;
  }

  strncpy(rule->pattern, tokens[2], MAX_PATTERN_LEN);

  // opcode list pattern - message opcode must match one member of this list
  // any combination of comma-seperated decimal or hex integer, or OPC_ constant, e.g. [32,0x9,OPC_ACON]

  if (rule_has_opcode_message_class) {
    if ((rule->num_opcodes = convert_opcode_list_to_array(rule->pattern, rule->opcodes)) == 0) {
      VLOG("parse_text_rule_definition: *** invalid opcodes pattern");
      convert_ok = false;
      goto cleanup;
    }
  }

  // event pattern - event type messages must match this pattern
  // split two parts, one for each of both the nn and en/dn parts, e.g. *10?:?20*

  if (rule_has_event_message_class) {
    dupe2 = strdup(rule->pattern);
    byte p = 0;

    rule->pattern_parts[0][0] = 0;
    rule->pattern_parts[1][0] = 0;

    ptr = strtok(dupe2, ":");

    while (ptr && p < 2) {
      strcpy(rule->pattern_parts[p++], ptr);
      ptr = strtok(NULL, ":");
    }

    free(dupe2);

    if (p != 2) {
      VLOG("parse_text_rule_definition: *** error parsing event pattern");
      convert_ok = false;
      goto cleanup;
    }
  }

  // 3. action(s) - drop, forward, event rewrite spec
  // one or two parts, e.g. D, R, +20:-30, etc

  strncpy(rule->action_text, tokens[3], MAX_ACTION_TEXT_LEN);

  dupe3 = strdup(rule->action_text);
  ptr = strtok(dupe3, ":");

  while (ptr && part < 2) {

    if (!convert_action(ptr, &rule->actions[part])) {
      VLOG("parse_text_rule_definition: *** error parsing action");
      convert_ok = false;
      free(dupe3);
      goto cleanup;
    }

    ptr = strtok(NULL, ":");
  }

  free(dupe3);

  // 4. convert output interface range to array

  convert_interface_list_string_to_interfaces_type(tokens[4], &rule->destination_interfaces);

  // 5. convert stop-on-match

  rule->stop_on_match = (ptr[0] == 'Y' || ptr[0] == 'y');

  // end of rule processing

cleanup:  // common exit point for all processing failures

  free(dupe);
  rule->active = true;

  return convert_ok;
}

/// convert an action string part, e.g. D, R, +200, =20, etc

bool convert_action(const char *action_text, action_t *action) {

  action->active = true;
  action->value = 0;

  switch (action_text[0]) {  // identify the action

    case '+':  // these modifiers require a value
    case '-':
    case '=':

      action->value = atol(&action_text[1]);  // capture action value, by which to increment, decrement, etc

      switch (action_text[0]) {
        case '+':
          action->action_type = ACTION_INCREMENT;
          break;
        case '-':
          action->action_type = ACTION_DECREMENT;
          break;
        case '=':
          action->action_type = ACTION_EQUALS;
          break;
      }

      break;

    case 'D':  // D, DROP
      action->action_type = ACTION_DROP;
      break;

    case 'F':  // F, FORWARD
      action->action_type = ACTION_FORWARD;
      break;

    case 'R':  // R, REWRITE
      action->action_type = ACTION_REWRITE;
      break;

    default:  // unknown action
      VLOG("convert_action: *** unknown action");
      action->active = false;
      break;
  }

  return action->active;
}

/// convert a comma-separated class list to a class list type

bool convert_message_class_list(const char *message_class_text, message_class_list_t *message_class_list) {

  bool ok = true;
  char *ptr, *dupe = strdup(message_class_text);

  message_class_list->num_classes = 0;
  ptr = strtok(dupe, ",[]");

  while (ptr && message_class_list->num_classes < MAX_CLASSES_PER_RULE) {

    if (strncmp(ptr, "ALL", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_ALL;

    } else if (strncmp(ptr, "EVENT", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_EVENT;

    } else if (strncmp(ptr, "LONG", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_LONG_EVENT;

    } else if (strncmp(ptr, "SHORT", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_SHORT_EVENT;

    } else if (strncmp(ptr, "OPCODES", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_OPCODE;

    } else if (strncmp(ptr, "ENUM", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_ENUM;

    } else if (strncmp(ptr, "DISCOVERY", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_DISCOVERY;

    } else if (strncmp(ptr, "CONFIG", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_CONFIG;

    } else if (strncmp(ptr, "DCC", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_DCC;

    } else if (strncmp(ptr, "REQ_RESP", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_REQ_RESP;

    } else if (strncmp(ptr, "EXTENDED", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_EXTENDED;

    } else if (strncmp(ptr, "LONG", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_LONG_MESSAGE;

    } else if (strncmp(ptr, "DATA", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_DATA_ONLY;

    } else if (strncmp(ptr, "MISC", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_MISC;

    } else if (strncmp(ptr, "DEBUG", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_DEBUG;

    } else {
      VLOG("parse_text_rule_definition: *** invalid message class specified");
      ok = false;
      break;
    }

    ptr = strtok(NULL, ",[]");
  }

  free(dupe);
  return ok;
}

///
