
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

#include <cstring>

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

      if (i == 0) {
        strcpy(filename, RULESET_JSON_FILENAME);
      } else {
        sprintf(filename, RULESET_JSON_FILESPEC, i);
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

    VLOG("copy_files_from_sd_card: copied %u files, %u bytes", files_copied, total_bytes);
    SDFS.end();
    VLOG("copy_files_from_sd_card: safe to remove SD card");
  }

  return (files_copied > 0);
}

/// load json ruleset data from local file into memory structure

bool load_ruleset_file(const byte file_version, ruleset_t *ruleset, const bool reload_from_sdcard) {

  char filename[32];
  byte num_rules_processed = 0;
  File f;

  VLOG("load_ruleset_file: loading ruleset file");

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
      VLOG("load_ruleset_file: new file(s) copied from SD card");
    } else {
      VLOG("load_ruleset_file: no files copied from SD card");
    }
  } else {
    VLOG("load_ruleset_file: not copying updated files from SD card");
  }

  /// determine filename version

  if (file_version == 0) {
    strcpy(filename, RULESET_JSON_FILENAME);
  } else {
    sprintf(filename, RULESET_JSON_FILESPEC, file_version);
  }

  /// open config file from local filesystem

  if (!LittleFS.exists(filename)) {
    VLOG("load_ruleset_file: *** local ruleset file %s does not exist", filename);
    error_led.pulse();
    return false;
  }

  VLOG("load_ruleset_file: loading ruleset from file = %s", filename);

  if (!(f = LittleFS.open(filename, "r"))) {
    VLOG("load_ruleset_file: *** unable to open %s for reading", filename);
    error_led.pulse();
    return false;
  }

  f.setTimeout(0);

  VLOG("load_ruleset_file: processing rules file");
  ruleset->file_version = file_version;

  // parse the contents of the open file into a ruleset structure, according to encoding

  num_rules_processed = process_rules_file_as_json(f, ruleset);

  VLOG("load_ruleset_file: ruleset load complete, %u rules loaded", num_rules_processed);
  f.close();

  return true;
}

/// parse a rules file encoded as json

byte process_rules_file_as_json(File &f, ruleset_t *ruleset) {

  bool rule_has_event_message_class, rule_has_opcode_message_class;
  char *ptr, *dupe2, *dupe3;
  byte num_rules = 0;

  VLOG("process_rules_file_as_json: processing input file");

  JsonDocument json_doc;
  DeserializationError json_error = deserializeJson(json_doc, f);

  if (json_error) {
    VLOG("process_rules_file_as_json: *** json deserialisation error %u/%s", json_error.code(), json_error.c_str());
    return 0;
  }

  VLOG("process_rules_file_as_json: json data deserialised ok");

  for (JsonObject rule : json_doc.as<JsonArray>()) {

    VLOG("--");
    VLOG("process_rules_file_as_json: processing rule %u", num_rules);

    const char *source = rule["source"];
    const char *cls = rule["class"];
    const char *pattern = rule["pattern"];
    const char *action = rule["action"];
    const char *destination = rule["destination"];
    const char *stop = rule["stop"];

    VLOG(" * %2u: %s, %s, %s, %s, %s, %s", num_rules, source, cls, pattern, action, destination, stop);

    // 0. convert the input interface list to an array, e.g. [0123] -> ...

    convert_interface_list_string_to_interfaces_type(rule["source"], &ruleset->rules[num_rules].source_interfaces);

    // 1. convert the message class list, e.g. DCC,MISC -> ...

    if (!convert_message_class_list(rule["class"], &ruleset->rules[num_rules].message_classes)) {
      VLOG("process_rules_file_as_json: %u: *** error in message class list", num_rules);
      goto bailout;
    }

    ruleset->rules[num_rules].pattern_type = PATTERN_TYPE_NONE;

    rule_has_event_message_class = false;

    for (byte k = 0; k < ruleset->rules[num_rules].message_classes.num_classes; k++) {
      if (ruleset->rules[num_rules].message_classes.class_list[k] == MESSAGE_CLASS_EVENT
          || ruleset->rules[num_rules].message_classes.class_list[k] == MESSAGE_CLASS_LONG_EVENT
          || ruleset->rules[num_rules].message_classes.class_list[k] == MESSAGE_CLASS_SHORT_EVENT) {
        rule_has_event_message_class = true;
        ruleset->rules[num_rules].pattern_type = PATTERN_TYPE_EVENT;
        break;
      }
    }

    rule_has_opcode_message_class = false;

    for (byte k = 0; k < ruleset->rules[num_rules].message_classes.num_classes; k++) {
      if (ruleset->rules[num_rules].message_classes.class_list[k] == MESSAGE_CLASS_OPCODE) {
        rule_has_opcode_message_class = true;
        ruleset->rules[num_rules].pattern_type = PATTERN_TYPE_OPCODE_LIST;
        break;
      }
    }

    // 2. pattern - only meaningful for opcodes, events and other messages with an nn part in bytes 1 and 2

    // doesn't make sense to specify both opcode and event messages classes
    // because we can't provide patterns for both

    if (rule_has_event_message_class && rule_has_opcode_message_class) {
      VLOG("process_rules_file_as_json: %u: *** cannot specify both opcode and event message classes", num_rules);
      goto bailout;
    }

    strncpy(ruleset->rules[num_rules].pattern, rule["pattern"], MAX_PATTERN_LEN);

    // opcode list pattern - message opcode must match one member of this list
    // any combination of comma-seperated decimal or hex integer, or OPC_ constant, e.g. [32,0x9,OPC_ACON]

    if (rule_has_opcode_message_class) {
      if ((ruleset->rules[num_rules].num_opcodes = convert_opcode_list_to_array(ruleset->rules[num_rules].pattern, ruleset->rules[num_rules].opcodes)) == 0) {
        VLOG("process_rules_file_as_json: %u: *** invalid opcodes pattern", num_rules);
        goto bailout;
      }

    } else {

      // event pattern - event type messages and other messages with an nn part must match this pattern
      // split two parts, one for each of both the nn and en/dn parts, e.g. *10?:?20*
      // default entry should be "*:*" for events or a single "*" for all other message types

      dupe2 = strdup(ruleset->rules[num_rules].pattern);
      byte p = 0;

      ruleset->rules[num_rules].pattern_parts[0][0] = 0;
      ruleset->rules[num_rules].pattern_parts[1][0] = 0;

      ptr = strtok(dupe2, ":[]");

      while (ptr && p < 2) {
        strcpy(ruleset->rules[num_rules].pattern_parts[p], ptr);
        ++p;
        ptr = strtok(NULL, ":[]");
      }

      free(dupe2);

      if (p == 0) {
        VLOG("process_rules_file_as_json: %u: *** error parsing event pattern %s", num_rules, ruleset->rules[num_rules].pattern);
        goto bailout;
      } else {
        VLOG("process_rules_file_as_json: parsed %u pattern parts", p);
      }
    }

    // 3. action(s) - drop, forward, event modify spec
    // one or two parts, e.g. D, R, +20:-30, etc

    strncpy(ruleset->rules[num_rules].action_text, rule["action"], MAX_ACTION_TEXT_LEN);
    ruleset->rules[num_rules].num_actions = 0;

    dupe3 = strdup(ruleset->rules[num_rules].action_text);
    ptr = strtok(dupe3, ":[]");

    while (ptr && ruleset->rules[num_rules].num_actions < 2) {

      if (!convert_action_part(ptr, &ruleset->rules[num_rules].actions[ruleset->rules[num_rules].num_actions])) {
        VLOG("process_rules_file_as_json: %u: *** error parsing action", num_rules);
        free(dupe3);
        goto bailout;
      }

      ++ruleset->rules[num_rules].num_actions;
      ptr = strtok(NULL, ":[]");
    }

    free(dupe3);

    VLOG("process_rules_file_as_json: parsed %u action parts", ruleset->rules[num_rules].num_actions);

    // 4. convert output interface range to array

    convert_interface_list_string_to_interfaces_type(rule["destination"], &ruleset->rules[num_rules].destination_interfaces);

    // 5. convert stop-on-match flag

    char flagval[2];
    strncpy(flagval, rule["stop"], 1);
    ruleset->rules[num_rules].stop_on_match = (toupper(flagval[0]) == 'Y');
    VLOG("process_rules_file_as_json: stop on match = %u", ruleset->rules[num_rules].stop_on_match);

    // end of rule processing

    ruleset->rules[num_rules].active = true;
    ++num_rules;
  }

bailout:

  VLOG("--");
  return num_rules;
}

/// convert an action string part, e.g. D, R, +200, =20, etc

bool convert_action_part(const char *action_text, action_t *action) {

  action->active = true;
  action->value = 0;

  VLOG("convert_action_part: %s", action_text);

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
      VLOG("convert_action_part: *** unknown action");
      action->active = false;
      break;
  }

  return action->active;
}

/// convert a comma-separated class list to a class list type

bool convert_message_class_list(const char *message_class_text, message_class_list_t *message_class_list) {

  bool ok = true;
  char *ptr, *dupe = strdup(message_class_text);

  VLOG("convert_message_class_list: %s", message_class_text);

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

    } else if (strncmp(ptr, "LMSG", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_LONG_MESSAGE;

    } else if (strncmp(ptr, "DATA", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_DATA_ONLY;

    } else if (strncmp(ptr, "MISC", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_MISC;

    } else if (strncmp(ptr, "DEBUG", 3) == 0) {
      message_class_list->class_list[message_class_list->num_classes++] = MESSAGE_CLASS_DEBUG;

    } else {
      VLOG("convert_message_class_list: *** invalid message class specified");
      ok = false;
      break;
    }

    ptr = strtok(NULL, ",[]");
  }

  VLOG("convert_message_class_list: found %u classes", message_class_list->num_classes);
  free(dupe);
  return ok;
}

///
