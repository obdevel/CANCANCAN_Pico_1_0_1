#include <cstddef>
#include "can2040.h"

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

#include <EEPROM.h>

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"
#include "interface.h"

extern opcode_t opcode_lookup_table[];
extern bool enable_serial_debug;

///

/// general logging function

auto_init_mutex(serial_mutex);  // mutex to prevent interleaving of output from both cores

int vlog(const char *fmt, ...) {

  int num_chars = 0;

#if ENABLE_DEBUG

  if (enable_serial_debug) {  // don't output if we're using the serial/USB for GC message traffic, like CANTHERX/CANUSB4

    char tmpbuff[VLOG_MAX_MSG_LEN];
    va_list vptr;

    // no point proceeding if USB serial not connected to a host system
    if (!Serial) {
      return 0;
    }

    if (fmt[0] == 0) {
      mutex_enter_blocking(&serial_mutex);
      Serial.println();
    } else {
      va_start(vptr, fmt);
      num_chars = vsnprintf(tmpbuff, VLOG_MAX_MSG_LEN, fmt, vptr);
      va_end(vptr);
      mutex_enter_blocking(&serial_mutex);

#if ENABLE_MICRO_TIMESTAMPS
      float f = (float)(micros()) / 1000.0;
      Serial.printf("%10.3f [%u]: ", f, get_core_num());
#else
      Serial.printf("%10lu [%u]: ", millis(), get_core_num());
#endif
      Serial.println(tmpbuff);
    }

    Serial.flush();
    mutex_exit(&serial_mutex);
  }
#endif

  return num_chars;
}

/// format a CAN message

char *format_can_message(const CANFrame *msg) {

  static char buffer[64];
  char t[16];

  if (msg->ext) {
    sprintf(buffer, "[%010lu] [%u] [%s] [ ", msg->id, msg->len, ((msg->len > 0 && !msg->ext) ? lookup_opcode_by_value(msg->data[0]) : ""));
  } else {
    sprintf(buffer, "[%03lu] [%u] [%s] [ ", (msg->id & 0x7f), msg->len, ((msg->len > 0 && !msg->ext) ? lookup_opcode_by_value(msg->data[0]) : ""));
  }

  for (byte i = 0; i < msg->len; i++) {
    sprintf(t, "%02x ", msg->data[i]);
    strcat(buffer, t);
  }

  strcat(buffer, "] ");

  if (msg->rtr) {
    strcat(buffer, "R");
  }

  if (msg->ext) {
    strcat(buffer, "X");
  }

  return buffer;
}

///

char *format_can_message(const struct can2040_msg *msg) {

  static char buffer[64];
  char t[16];

  if (msg->id & CAN2040_ID_EFF) {
    sprintf(buffer, "[%010lu] [%lu] [%s] [ ", msg->id, msg->dlc, ((msg->dlc > 0 && !(msg->id & CAN2040_ID_EFF)) ? lookup_opcode_by_value(msg->data[0]) : ""));
  } else {
    sprintf(buffer, "[%03lu] [%lu] [%s] [ ", (msg->id & 0x7f), msg->dlc, ((msg->dlc > 0 && !(msg->id & CAN2040_ID_EFF)) ? lookup_opcode_by_value(msg->data[0]) : ""));
  }

  for (byte i = 0; i < msg->dlc; i++) {
    sprintf(t, "%02x ", msg->data[i]);
    strcat(buffer, t);
  }

  strcat(buffer, "] ");

  if (msg->id & CAN2040_ID_RTR) {
    strcat(buffer, "R");
  }

  if (msg->id & CAN2040_ID_EFF) {
    strcat(buffer, "X");
  }

  return buffer;
}

///

char *format_can_message(const can_msg_wrapper_t *wmsg) {

  return format_can_message(&wmsg->msg);
}

///

struct can2040_msg convert_cbus_to_can2040(const CANFrame *imsg) {

  struct can2040_msg omsg;

  omsg.id = imsg->id;
  omsg.dlc = imsg->len;

  if (imsg->rtr) {
    omsg.id |= CAN2040_ID_RTR;
  }

  if (imsg->ext) {
    omsg.id |= CAN2040_ID_EFF;
  }

  for (unsigned int i = 0; i < imsg->len; i++) {
    omsg.data[i] = imsg->data[i];
  }

  return omsg;
}

///

CANFrame convert_can2040_to_cbus(const struct can2040_msg *imsg) {

  CANFrame omsg;

  omsg.id = imsg->id;
  omsg.len = imsg->dlc;

  omsg.rtr = imsg->id & CAN2040_ID_RTR;
  omsg.ext = imsg->id & CAN2040_ID_EFF;

  memcpy(&omsg.data[0], &imsg->data[0], imsg->dlc);

  return omsg;
}

///

#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#ifndef CYW43_WL_GPIO_VBUS_PIN
#define CYW43_WL_GPIO_VBUS_PIN 2
#endif

bool usb_power_detected(void) {

#if defined(IS_WIFI_DEVICE)
  return cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN);
#else
  pinMode(USB_POWER_DETECT_PIN, INPUT);
  return (bool)digitalRead(USB_POWER_DETECT_PIN);
#endif
}

///
/// opcode related functions
///

bool is_opcode_of_message_class(const byte opcode, const byte message_class) {

  bool match = false;

  for (byte i = 0; i < NUM_OPCODES; i++) {
    if (opcode_lookup_table[i].opcode == opcode) {
      match = (opcode_lookup_table[i].message_class == message_class);
      break;
    }
  }

  return match;
}

bool is_event_opcode(const struct can2040_msg *msg) {

  return (((msg->data[0] & EVENT_SET_MASK) == EVENT_SET_MASK) && ((~(msg->data[0]) & EVENT_CLR_MASK) == EVENT_CLR_MASK) ? true : false);
}

bool is_short_event_opcode(const struct can2040_msg *msg) {

  return (is_event_opcode(msg) && (msg->data[0] & EVENT_SHORT_MASK));
}

bool is_long_event_opcode(const struct can2040_msg *msg) {

  return (!is_short_event_opcode(msg));
}

/// lookup an opcode name, return the matching OPC_ value

int lookup_opcode_by_name(const char *opcode_name) {

  for (byte i = 0; i < NUM_OPCODES; i++) {
    if (strcmp(opcode_lookup_table[i].opcode_name, opcode_name) == 0) {
      return i;
    }
  }

  return -1;
}

/// lookup an opcode OPC_ value, return the matching name char string

const char *lookup_opcode_by_value(const byte opcode) {

  for (byte i = 0; i < NUM_OPCODES; i++) {
    if (opcode_lookup_table[i].opcode == opcode) {
      return opcode_lookup_table[i].opcode_name;
    }
  }

  return nullptr;
}

/// does this opcode contain a node number

bool message_has_nn(const struct can2040_msg *msg) {

  bool match = false;

  for (byte i = 0; i < NUM_OPCODES; i++) {
    if (opcode_lookup_table[i].opcode == msg->data[0]) {
      match = opcode_lookup_table[i].has_nn;
      break;
    }
  }

  return match;
}

/// modify an event message according to a rule action
/// modifies one part, either the NN or EN/DN

void modify_event_message_with_action(struct can2040_msg *msg, byte which_part, byte action, uint32_t value) {

  uint32_t msg_part_value;

  // take the current value

  switch (which_part) {
    case EVENT_PART_NN:
      msg_part_value = (msg->data[1] << 8) + msg->data[2];
      break;

    case EVENT_PART_EN:
      msg_part_value = (msg->data[3] << 8) + msg->data[4];
      break;

    default:
      return;
      break;
  }

  // apply the action

  switch (action) {
    case ACTION_INCREMENT:
      msg_part_value += value;
      break;
    case ACTION_DECREMENT:
      msg_part_value -= value;
      break;
    case ACTION_EQUALS:
      msg_part_value = value;
      break;
    default:
      return;
      break;
  }

  // modify the message

  switch (which_part) {
    case EVENT_PART_NN:
      msg->data[1] = msg_part_value >> 8;
      msg->data[2] = msg_part_value & 0xff;
      break;

    case EVENT_PART_EN:
      msg->data[3] = msg_part_value >> 8;
      msg->data[4] = msg_part_value & 0xff;

      break;
    default:
      return;
      break;
  }

  return;
}

/// modify the node number in a non-event message

void modify_non_event_message_with_action(struct can2040_msg *msg, byte action, uint32_t value) {

  uint32_t msg_part_value;

  if (message_has_nn(msg)) {
    msg_part_value = (msg->data[1] << 8) + msg->data[2];

    switch (action) {
      case ACTION_INCREMENT:
        msg_part_value += value;
        break;
      case ACTION_DECREMENT:
        msg_part_value -= value;
        break;
      case ACTION_EQUALS:
        msg_part_value = value;
        break;
      default:
        return;
        break;
    }

    msg->data[1] = msg_part_value >> 8;
    msg->data[2] = msg_part_value & 0xff;
  }

  return;
}

///

void convert_interface_list_string_to_interfaces_type(const char *string, interface_list_t *interfaces) {

  /// parse an interface range char string into an array
  /// e.g. [0], [12], [01234], [0,1,2] or even [] or empty string
  /// enclosing brackets are optional
  /// digits may be delimited if desired

  VLOG("convert_interface_list_string_to_interfaces_type: %s", string);

  interfaces->num_interfaces = 0;

  for (size_t i = 0; string[i] > 0; i++) {
    if (isdigit((int)string[i])) {
      interfaces->interface_list[interfaces->num_interfaces] = string[i] - 48;
      ++interfaces->num_interfaces;
    }
  }

  VLOG("convert_interface_list_string_to_interfaces_type: found %u", interfaces->num_interfaces);
  return;
}

///

char *convert_interfaces_type_to_interface_list_string(const interface_list_t *interfaces, char *string) {

  /// create a string like: [0], [12], [01234] or even []

  byte i;
  string[0] = '[';

  for (i = 0; i < interfaces->num_interfaces && i < MAX_INTERFACES; i++) {
    string[i + 1] = interfaces->interface_list[i] + 48;
  }

  string[i + 1] = ']';
  string[i + 2] = 0;

  return string;
}

///

byte convert_opcode_list_to_array(char *opcode_list, byte *arr) {

  /// parse a comma-separated list of opcodes to an array of OPC_ constant values
  /// e.g. [0x90,OPC_ACON,4,22]
  /// return number of opcodes found

  char *ptr;
  byte index = 0;
  int opcode = 0;

  VLOG("convert_opcode_list_to_array: %s", opcode_list);

  char *dupe = strdup(opcode_list);  // take a copy as strtok changes it

  ptr = strtok(dupe + 1, ",[]");  // find first comma-delimited token, ignore brackets

  while (ptr && index < MAX_OPCODES_PER_RULE) {

    if (ptr[0] == '0' && toupper(ptr[1]) == 'X') {  // a hex number, e.g. 0x51 or 0X51
      arr[index++] = (byte)strtoul(ptr, NULL, 16);

    } else if (isdigit(ptr[0])) {  // a decimal number, e.g. 23
      arr[index++] = (byte)strtoul(ptr, NULL, 10);

    } else if (strncmp(ptr, "OPC_", 4) == 0) {  // a string beginning with OPC_, e.g. OPC_ACON
      if ((opcode = lookup_opcode_by_name(ptr)) > -1) {
        arr[index++] = opcode;
      }
    } else {  // invalid token
      VLOG("convert_opcode_list_to_array: *** invalid token = %s", ptr);
    }

    ptr = strtok(NULL, ",[]");
  }

  free(dupe);

  return index;
}

///

char *convert_class_list_to_string(const message_class_list_t *classes, char *string) {

  char t[8];

  string[0] = 0;

  for (byte i = 0; i < classes->num_classes; i++) {
    sprintf(t, "%u", classes->class_list[i]);
    strcat(string, t);
    strcat(string, ",");
  }

  return string;
}

///

void print_rules(ruleset_t *ruleset) {

  char t0[64], t1[64], t2[64], t3[64], t4[64], t5[64];
  byte r;

  VLOG("--");
  VLOG("ruleset:");

  for (r = 0; r < MAX_RULE_ENTRIES; r++) {
    if (!ruleset->rules[r].active) {
      break;
    }

    convert_interfaces_type_to_interface_list_string(&ruleset->rules[r].source_interfaces, t0);
    convert_class_list_to_string(&ruleset->rules[r].message_classes, t1);
    sprintf(t2, "%u %u:%u %u", ruleset->rules[r].actions[0].action_type, ruleset->rules[r].actions[0].value, ruleset->rules[r].actions[1].action_type, ruleset->rules[r].actions[1].value);
    convert_interfaces_type_to_interface_list_string(&ruleset->rules[r].destination_interfaces, t3);

    VLOG(" * [%u] S: %s | C: %8s | P: %u:%s:%s | A: %s | %s |", r, t0, t1, ruleset->rules[r].pattern_type, ruleset->rules[r].pattern_parts[0], ruleset->rules[r].pattern_parts[1], t2, t3);
  }

  VLOG("--");
  return;
}

/// rewrite a message header with the can id of the target bus, preserving the upper bits
/// we don't touch extended messages

void rewrite_can_id(can_msg_wrapper_t *wmsg, const byte canid) {

  if (wmsg->msg.id & CAN2040_ID_EFF) {
    // VLOG("rewrite_can_id: not touching extended message");
    return;
  }

  uint32_t original_can_id = wmsg->msg.id;
  uint32_t upper_bits = (original_can_id >> 7);

  wmsg->msg.id = (upper_bits << 7) + canid;
  // VLOG("rewrite_can_id: %u -> %u", original_can_id & 0x7f, wmsg->msg.id & 0x7f);

  return;
}

//

void rewrite_can_id(CANFrame *msg, const byte canid) {

  if (msg->ext) {
    // VLOG("rewrite_can_id: not touching extended message");
    return;
  }

  uint32_t original_can_id = msg->id;
  uint32_t upper_bits = (original_can_id >> 7);

  msg->id = canid + (upper_bits << 7);
  // VLOG("rewrite_can_id: %u -> %u", original_can_id & 0x7f, msg->id & 0x7f);

  return;
}

///

// typedef struct {
//   byte wifi_mode;        // STA or AP
//   char hostname[32];     // DNS hostname
//   char ssid[32];         // wfi SSID
//   char password[32];     // wifi password
//   char ap_ssid[32];      // access point SSID
//   char ap_password[32];  // access point password
//   bool hide_ap;          // hide AP
//   byte ap_channel;       // channel in AP mode
//   char servicename[32];  // DNS-SD service name
//   char server[32];       // server (if net client)
//   uint16_t port;         // network port number
//   char servername[32];   // server name to connect to if client
//   byte check_value;      // set to 99 if config is valid
// } network_config_t;

void load_network_config(network_config_t *cfg) {

  VLOG("load_network_config: loading saved network config");
  EEPROM.get(NETCFG_EEPROM_SAVE_OFFSET, *cfg);

  if (cfg->check_value != NETCFG_CHECK_VALUE) {
    VLOG("load_network_config: invalid config");
    default_network_config(cfg);
    save_network_config(cfg);
  }

  print_network_config(cfg);
  return;
}

//

void default_network_config(network_config_t *cfg) {

  VLOG("default_network_config");

  memset(cfg, 0, sizeof(network_config_t));

  strcpy(cfg->hostname, DEFAULT_HOSTNAME);
  strcpy(cfg->servicename, DEFAULT_SERVICENAME);
  strcpy(cfg->ap_ssid, DEFAULT_AP_SSID);
  strcpy(cfg->ap_password, DEFAULT_AP_PASSWORD);

  cfg->wifi_mode = WIFI_MODE_AP;
  cfg->port = DEFAULT_IP_PORT;
  cfg->check_value = NETCFG_CHECK_VALUE;

  return;
}

//

void save_network_config(network_config_t *cfg) {

  VLOG("save_network_config");

  EEPROM.put(NETCFG_EEPROM_SAVE_OFFSET, *cfg);
  EEPROM.commit();
  return;
}

//

void print_network_config(network_config_t *cfg) {

  VLOG("saved network config:");
  VLOG(" * wifi mode = %u", cfg->wifi_mode);
  VLOG(" * hostname = %s", cfg->hostname);
  VLOG(" * wifi ssid = %s", cfg->ssid);
  VLOG(" * wifi password = %s", cfg->password);
  VLOG(" * ap ssid = %s", cfg->ap_ssid);
  VLOG(" * ap password = %s", cfg->ap_password);
  VLOG(" * ap channel = %u", cfg->ap_channel);
  VLOG(" * server = %s", cfg->servername);
  VLOG(" * service = %s", cfg->servicename);
  VLOG(" * port = %u", cfg->port);
  VLOG(" * check value = %u", cfg->check_value);

  return;
}

/// Gridconnect functions

// convert from CAN frame to Gridconnect string
// caller must allocate the correct destination buffer size (32 bytes is more than enough)

const char hex_chars[] = "0123456789ABCDEF";
const char *xfmt = "%08lX";
const char *sfmt = "%04lX";

bool CANtoGC(const struct can2040_msg *cf, char buffer[GC_BUFFER_SIZE]) {

  uint8_t i, idx;
  uint32_t tid;

  // VLOG("CANtoGC: CAN header = %d, CANID = %d", cf->id, cf->id & 0x7f);

  buffer[0] = ':';
  buffer[1] = (cf->id & CAN2040_ID_EFF) ? 'X' : 'S';

  // shift header per CBUS spec - stolen from JMRI
  // https://github.com/JMRI/JMRI/blob/bc82fb13384fe3f30accbe5f388e60be9030496f/java/src/jmri/jmrix/can/adapters/gridconnect/canrs/MergMessage.java

  if (cf->id & CAN2040_ID_EFF) {
    tid = ((cf->id << 3) & 0xFFE00000) | 0x80000 | (cf->id & 0x3FFFF);
  } else {
    tid = cf->id << 5;
  }

  // insert formatted header as hex string - 4 or 8 chars for standard or extended messages

  idx = snprintf(&buffer[2], (GC_BUFFER_SIZE - 2), (buffer[1] == 'S' ? sfmt : xfmt), tid) + 2;

  // add normal or rtr flag

  buffer[idx++] = (cf->id & CAN2040_ID_RTR) ? 'R' : 'N';

  // data payload - cheaper than using sprintf

  for (i = 0; i < cf->dlc; i++) {
    buffer[idx++] = hex_chars[cf->data[i] >> 4];    // upper nibble
    buffer[idx++] = hex_chars[cf->data[i] & 0x0f];  // lower nibble
  }

  // terminate the string

  buffer[idx++] = ';';
  buffer[idx] = 0;

  // VLOG("CANtoGC: completed GC string = %s", buffer);

  return true;
}

// convert from Gridconnect string to CAN frame

bool GCtoCAN(const char buffer[GC_BUFFER_SIZE], struct can2040_msg *cf) {

  bool ext, rtr;
  uint8_t i, j;
  char hbuf[9];
  unsigned long header = 0UL;

  // check string begins with a colon

  if (buffer[0] != ':') {
    VLOG("GCtoCAN: invalid GC string |%s| does not begin with :", buffer);
    return false;
  }

  // next byte indicates standard or extended frame type

  switch (buffer[1]) {
    case 'S':
      ext = false;
      break;
    case 'X':
      ext = true;
      break;
    default:
      VLOG("GCtoCAN: invalid frame id type %c is not S or X", buffer[1]);
      return false;
  }

  // find the N or R indicator

  for (i = 2; buffer[i] > 0; i++) {
    if (buffer[i] == 'N' || buffer[i] == 'R') {
      break;
    }
  }

  if (buffer[i] == 0) {
    VLOG("GCtoCAN: string does not contain frame type N or R");
    return false;
  }

  if (i == 2) {
    VLOG("GCtoCAN: string has no header part");
    return false;
  }

  // i now points to the N or R indicator

  // set the remote request flag

  switch (buffer[i]) {
    case 'N':
      rtr = false;
      break;
    case 'R':
      rtr = true;
      break;
    default:
      VLOG("GCtoCAN: invalid frame type %c is not N or R", buffer[i]);
      return false;
      break;
  }

  // copy the frame header part - the variable number of hex bytes between :S/X and N/R
  // should be 4 for standard messages and 8 for extended messages, but we can't guarantee this
  // check for non-hex characters

  for (j = 2; j < i; j++) {
    if (!isxdigit(buffer[j])) {
      VLOG("GCtoCAN: error: non-hex digit '%c' in frame header at index = %d", buffer[j], j);
      return false;
    }

    hbuf[j - 2] = buffer[j];
  }

  hbuf[j - 2] = 0;  // terminate

  // convert hex representation to an unsigned 32 bit integer value

  header = strtoul(hbuf, (char **)0, 16);

  // shift the header per CBUS spec - stolen from JMRI
  // https://github.com/JMRI/JMRI/blob/bc82fb13384fe3f30accbe5f388e60be9030496f/java/src/jmri/jmrix/can/adapters/gridconnect/canrs/MergReply.java
  // update extended frame flag bit in header

  if (ext) {
    cf->id = ((header >> 3) & 0x1FFC0000) | (header & 0x3FFFF);
    cf->id |= CAN2040_ID_EFF;
  } else {
    cf->id = (header >> 5) & 0x07FF;
  }

  // update rtr flag bit in header

  if (rtr) {
    cf->id |= CAN2040_ID_RTR;
  }

  // extract up to eight bytes of message data

  ++i;  // i now points to the first char of the data payload in the input string

  cf->dlc = sscanf(&buffer[i], "%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
                   &cf->data[0], &cf->data[1], &cf->data[2], &cf->data[3],
                   &cf->data[4], &cf->data[5], &cf->data[6], &cf->data[7]);

  return true;
}

///
