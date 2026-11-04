
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
  CANCANCAN 1 0 1

  EEPROM usage:

  12, 13 - CAN ID for secondary can2040 CBUS interfaces
  14     - CANID for secondary 2515 or 2518 CBUS interface

  NV usage: (30)

  1   - interface type 1
  2   - interface type 2
  3   - interface type 3
  4   - interface type 4 - for network interface types
  5   - interface type 5

  6   - enable secondary interfaces at startup
  7   - enumerate secondary buses at startup
  8   - pass all messages from secondary interfaces to the CBUS processor
  9   - filter engine type
  10  - ruleset file number for configurable and hybrid filter engines
  11  - reload ruleset files from SD card at startup
  12  - enable serial debug
  13  - CAN-FD or CAN-backbone arbitration ID
  14  - first dynamic discovery delay - in seconds
  15  - dynamic bus discovery repeat interval - in minutes - 0 to disable

  20  - default action for messages arriving on interface 0, if no explicit rule matches, when source
  21  - ... 1
  22  - ... 2
  23  - ... 3
  24  - ... 4
  25  - ... 5

  26  - default action for messages for interface 0, if no explicit rule matches, when destination
  27  - ... 1
  28  - ... 2
  29  - ... 3
  30  - ... 4
  31  - ... 5

  35-46 - fixed IP addresses for ethernet interfaces (12 NVs)

  EV usage;

  1 -  0 = consumer event, 1 = taught producer event

  EV2 usage, for consumer events:

    99  - reboot device
    100 - 104 - enable/disable bus
    105 - 109 - enumerate bus
    110 - 114 - probe bus
    115 - 120 - query bus
    130 - enable/disable router
    140 - enable/disable discoverer

  EV2 usage, for producer events:

*/

/// core Arduino libraries

#include <EEPROM.h>

/// CBUS library header files

#include <CBUSACAN2040.h>  // CAN controller and CBUS class
#include <CBUSswitch.h>    // pushbutton switch
#include <CBUSLED.h>       // CBUS LEDs
#include <CBUSconfig.h>    // module configuration
#include <CBUSParams.h>    // CBUS parameters
#include <cbusdefs.h>      // MERG CBUS constants

/// local header files

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"
#include "router.h"

/// https://arduino-pico.readthedocs.io/en/latest/multicore.html

bool core1_separate_stack = true;

/// CBUS objects

CBUSConfig module_config;           // configuration object
CBUSACAN2040 CBUS(&module_config);  // CBUS object
CBUSLED ledGrn, ledYlw;             // two LED objects
CBUSSwitch pb_switch;               // switch object

/// global variables

unsigned char mname[7] = { 'C', 'A', 'N', 'C', 'A', 'N', ' ' };
bool enable_serial_debug = true;

/// application objects

message_router *rtr;
queue_t bus0_input_queue, bus0_output_queue;
queue_t router_command_queue;
network_config_t netcfg;

CBUSLED error_led, activity_leds[4], heartbeat_led;

/// globals defined in other source files

extern queue_t canbus1_input_queue, canbus2_input_queue;  // in can_bus.cpp - for testing - set in can interface constructor

/// forward function declarations

void setupCBUS(void);
void eventhandler(byte index, CANFrame *msg);
void framehandler(CANFrame *msg);
void transmithandler(CANFrame *msg);
void process_serial_input(void);
void execute_serial_command(char *serial_command);
bool find_taught_producer_event(const byte ev2, byte *index);
void send_producer_event_by_ev2(const byte ev2, const bool is_on_polarity = true);

/// configure CBUS and start CAN bus message processing

void setupCBUS(void) {

  /// set config layout parameters

  module_config.EE_NVS_START = 20;
  module_config.EE_NUM_NVS = 50;
  module_config.EE_EVENTS_START = 70;
  module_config.EE_MAX_EVENTS = 64;
  module_config.EE_NUM_EVS = 2;
  module_config.EE_BYTES_PER_EVENT = (module_config.EE_NUM_EVS + 4);

  /// initialise and load configuration

  module_config.setEEPROMtype(EEPROM_INTERNAL);
  module_config.begin();

  VLOG("setupc: CBUS mode = %s, CANID = %u, NN = %u", ((module_config.FLiM) ? "FLiM" : "SLiM"), module_config.CANID, module_config.nodeNum);

  /// set module parameters

  CBUSParams params(module_config);
  params.setVersion(VER_MAJ, VER_MIN, VER_BETA);
  params.setModuleId(MODULE_ID);
  params.setFlags(PF_FLiM | PF_COMBI);

  /// assign to CBUS

  CBUS.setParams(params.getParams());
  CBUS.setName(mname);

  /// set CBUS LED pins and assign to CBUS

  ledGrn.setPin(SLIM_LED_PIN);
  ledYlw.setPin(FLIM_LED_PIN);
  CBUS.setLEDs(ledGrn, ledYlw);

  /// initialise CBUS switch and assign to CBUS

  pb_switch.setPin(CBUS_SWITCH_PIN, LOW);
  pb_switch.run();
  CBUS.setSwitch(pb_switch);

  /// module reset - if CBUS switch is depressed at startup and module is in SLiM mode

  if (pb_switch.isPressed() && !module_config.FLiM) {
    VLOG("setupc: switch was pressed at startup in SLiM mode");
    module_config.resetModule(ledGrn, ledYlw, pb_switch);
  }

  /// one-time opportunity to configure default NVs and events after module reset

  if (module_config.isResetFlagSet()) {
    VLOG("setupc: module has been reset - setting default config");

    EEPROM[12] = 0;  // reset can interface can ids, eeprom commit not necessary
    EEPROM[13] = 0;

    /// no secondary buses configured

    module_config.writeNV(6, 1);  // enable secondary interfaces at startup
    module_config.writeNV(8, 1);  // allow module config from secondary buses
    module_config.writeNV(9, 1);  // CAN filter engine - drop enumeration traffic & rewrite can ids for all other messages

    module_config.writeNV(11, 1);   // load updated rule files from SD card
    module_config.writeNV(12, 1);   // enable serial debug
    module_config.writeNV(13, 1);   // CAN-FD or external MCP2515/8 arbitration ID, or SPE node ID
    module_config.writeNV(14, 30);  // first can bus discovery after, secs
    module_config.writeNV(15, 2);   // subsequent can bus discovery frequency, mins
    module_config.writeNV(16, 1);   // rules file format is json

    module_config.writeNV(20, RULE_RESULT_DROP_MESSAGE);  // default action for interface 0 if no matching rule, when source
    module_config.writeNV(21, RULE_RESULT_DROP_MESSAGE);
    module_config.writeNV(22, RULE_RESULT_DROP_MESSAGE);
    module_config.writeNV(23, RULE_RESULT_DROP_MESSAGE);
    module_config.writeNV(24, RULE_RESULT_DROP_MESSAGE);
    module_config.writeNV(25, RULE_RESULT_DROP_MESSAGE);

    module_config.writeNV(26, RULE_RESULT_DROP_MESSAGE);  // default action for interface 0 if no matching rule, when destination
    module_config.writeNV(27, RULE_RESULT_DROP_MESSAGE);
    module_config.writeNV(28, RULE_RESULT_DROP_MESSAGE);
    module_config.writeNV(29, RULE_RESULT_DROP_MESSAGE);
    module_config.writeNV(30, RULE_RESULT_DROP_MESSAGE);
    module_config.writeNV(31, RULE_RESULT_DROP_MESSAGE);

    module_config.clearResetFlag();
  }

  /// register our CBUS message callbacks

  CBUS.setEventHandler(eventhandler);
  CBUS.setFrameHandler(framehandler);
  CBUS.setTransmitHandler(transmithandler);

  /// set CBUS LEDs to indicate mode

  CBUS.indicateMode(module_config.FLiM);

  /// configure and start CAN bus and CBUS message processing

  CBUS.setNumBuffers(CAN_QUEUE_SIZE, CAN_QUEUE_SIZE);  // more buffers = more memory used, fewer = less
  CBUS.setPins(CAN_TX_PIN_0, CAN_RX_PIN_0);            // select pins for CAN tx and rx
  CBUS.setPIOInstance(0);                              // default = 0, secondary can interfaces use 1 and 2 (on RP2350)

  if (!CBUS.begin()) {
    VLOG("setupc: CBUS initialisation failed");
  } else {
    VLOG("setupc: CBUS initialisation ok");
  }

  /// *** temp

  // module_config.setFLiM(true);
  // module_config.setCANID(22);
  // module_config.setNodeNum(22);

  return;
}

/// setup for core 0 -- main setup and message router

void setup(void) {

  uint32_t stimer = millis();

  /// initialise indicator LEDs - all on

  activity_leds[0].setPin(ACTIVITY_LED_PIN_0);  // configure activity LEDs
  activity_leds[0].on();
  activity_leds[1].setPin(ACTIVITY_LED_PIN_1);
  activity_leds[1].on();
  activity_leds[2].setPin(ACTIVITY_LED_PIN_2);
  activity_leds[2].on();
  activity_leds[3].setPin(ACTIVITY_LED_PIN_3);
  activity_leds[3].on();

  error_led.setPin(ERROR_LED_PIN);  // configure error LED
  error_led.on();

  heartbeat_led.setPin(LED_BUILTIN);  // use the onboard LED as a heartbeat indicator
  heartbeat_led.on();

  /// start serial

  Serial.begin(SERIAL_BAUDRATE);

  // delay(10000);

  /// pause for serial connection to be established if USB power is detected

  if (usb_power_detected()) {
    while (!Serial && millis() - stimer <= SERIAL_TIMEOUT) {
      // delay(50);
    }
  }

  VLOG("");
  VLOG("Pico CANCANCAN message router version %u%c%u", VER_MAJ, VER_MIN, VER_BETA);
  VLOG("(c) Duncan Greenwood M5767, 2026");
  VLOG("setup: begins");

#if defined(PICO_RP2350)
  VLOG("setup: processor is RP2350");
#elif defined(PICO_RP2040)
  VLOG("setup: processor is RP2040");
#endif

  VLOG("setup: is Pico W = %u", rp2040.isPicoW());
  VLOG("setup: USB power state = %u", usb_power_detected());

  /// reset network config

  /// mount filesystem and list contents
  /// mount_local_filesystem();

  /// create input and output queues for primary cbus interface
  /// command queue to router thread on core 1

  VLOG("setup: creating message queues");

  queue_init(&bus0_input_queue, sizeof(can_msg_wrapper_t), CAN_QUEUE_SIZE);
  queue_init(&bus0_output_queue, sizeof(can_msg_wrapper_t), CAN_QUEUE_SIZE);
  queue_init(&router_command_queue, sizeof(router_command_t), 16);

  /// release core 1
  /// must wait for CBUS to start so NVs are available to read

  VLOG("setup: waiting for core 1 setup to complete");
  rp2040.fifo.push_nb(0);
  rp2040.fifo.pop();

  /// whether to enable serial debug output - may be overriden by GC serial interface

  enable_serial_debug = module_config.readNV(12);

#if defined(IS_NETWORK_DEVICE)
  bool config_reset_requested = false;

  pinMode(CONFIG_RESET_PIN, INPUT_PULLUP);

  if (digitalRead(CONFIG_RESET_PIN) == LOW) {
    stimer = millis();
    config_reset_requested = true;

    VLOG("setup: config reset timer started");

    while (millis() - stimer <= 5000) {
      delay(50);

      if (digitalRead(CONFIG_RESET_PIN) == HIGH) {
        config_reset_requested = false;
        VLOG("setup: config reset timer stopped");
        break;
      }
    }
  }

  if (config_reset_requested) {
    VLOG("setup: setting network configuration to defaults");
    default_network_config(&netcfg);
    save_network_config(&netcfg);
  } else {
    VLOG("setup: network configuration reset not required");
  }

  /// load the saved network config, possibly setting default values if unconfigured

  load_network_config(&netcfg);
#endif

  /// all LEDs off

  activity_leds[0].off();
  activity_leds[1].off();
  activity_leds[2].off();
  activity_leds[3].off();
  error_led.off();
  heartbeat_led.off();

  /// initialise message router - after CBUS startup so that NVs are available

  VLOG("setup: initialising message router");

  rtr = new message_router();

  if (!rtr->begin()) {
    VLOG("setup: *** error starting message router");
  }

  rtr->enable(true);
  VLOG("setup: message router init complete");

  /// setup complete

  VLOG("setup: free memory = %u", module_config.freeSRAM());
  VLOG("setup: sizeof rule_t = %u", sizeof(rule_t));
  VLOG("setup: complete");
  VLOG("");

  return;
}

/// setup for core 1 -- CBUS

void setup1(void) {

  /// wait to be released by core 0

  (void)rp2040.fifo.pop();

  VLOG("setup1: begin");

  /// configure CBUS and CAN

  VLOG("setup1: starting CBUS");
  setupCBUS();

  /// release core 0

  rp2040.fifo.push_nb(1);

  VLOG("setup1: complete");
  return;
}

/// loop for core 0

void loop(void) {

  /// run message router

  rtr->run();

  /// process serial monitor input (if Gridconnect USB/serial interface is not active)

  if (enable_serial_debug) {
    process_serial_input();
  }

  /// update heartbeat LED

  static unsigned long htimer = 0;

  if (millis() - htimer >= 1000) {
    htimer = millis();
    heartbeat_led.pulse(5);
  }

  /// update LED states

  activity_leds[0].run();
  activity_leds[1].run();
  activity_leds[2].run();
  activity_leds[3].run();
  error_led.run();
  heartbeat_led.run();
}

/// loop for core 1

void loop1(void) {

  can_msg_wrapper_t wmsg;

  /// process CBUS messages from the primary bus, and handle LEDs and switch

  CBUS.process();

  /// process the output queue for the primary bus - contains messages forwarded by the router from secondary interfaces
  /// message can id will have been rewritten by the interface shim, if required by the filter engine
  /// optionally forward to the CBUS library for processing, so this device can be managed and controlled from any interface

  if (queue_try_remove(&bus0_output_queue, &wmsg)) {

    CANFrame cbus_msg = convert_can2040_to_cbus(&wmsg.msg);  // convert can2040 message type to CBUS library type

    unsigned long t = micros() - wmsg.timestamp;
    VLOG("loop1: forwarding message to primary bus, t = %u", t);

    /// the sendMessageNoUpdate() function does not call the transmithandler, avoiding a message loop

    CBUS.sendMessageNoUpdate(&cbus_msg);

    /// optionally, forward the message to cbus processor (nv 8)
    /// ensure a CAN ID clash does not trigger a bus enumeration

    if (module_config.readNV(8)) {
      VLOG("loop1: forwarding message to cbus message processor");
      CBUS.process_single_message(&cbus_msg, true);
    }
  }
}

/// called by the CBUS library whenever a previously taught event is received - core 1

void eventhandler(byte index, CANFrame *msg) {

  byte ev1 = module_config.getEventEVval(index, 1);
  byte ev2 = module_config.getEventEVval(index, 2);
  router_command_t r;

  VLOG("eventhandler: event message = %s", format_can_message(msg));
  VLOG("eventhandler: index = %u, ev1 = %u, ev2 = %u", index, ev1, ev2);

  /// handle consumer events

  switch (ev1) {
    case 0:
      VLOG("eventhandler: consumer event");

      switch (ev2) {
        case 99:
          VLOG("eventhandler: reboot");
          rp2040.reboot();
          break;

        case 100 ... 199:
          r.state = !(msg->data[0] & EVENT_ON_MASK);
          r.command_num = ev2;
          queue_try_add(&router_command_queue, &r);
          break;

        default:
          break;
      }

      break;
  }

  return;
}

/// called by the CBUS library for every message received from the primary CAN bus - core 1

void framehandler(CANFrame *msg) {

  can_msg_wrapper_t wmsg;

  wmsg.msg = convert_cbus_to_can2040(msg);
  wmsg.timestamp = micros();
  wmsg.retries = 0;

  /// forward message to router input queue for processing and forwarding to other buses

  VLOG("");
  VLOG("framehandler: message = %s", format_can_message(msg));
  queue_try_add(&bus0_input_queue, &wmsg);

  return;
}

/// called for every message produced by the CBUS library sendMessage() function - core 1

void transmithandler(CANFrame *msg) {

  can_msg_wrapper_t wmsg;

  wmsg.msg = convert_cbus_to_can2040(msg);
  wmsg.timestamp = micros();
  wmsg.retries = 0;

  /// forward message to router input queue for processing and forwarding to other buses

  VLOG("transmithandler: message = %s", format_can_message(msg));
  queue_try_add(&bus0_input_queue, &wmsg);

  return;
}

/// find a producer event by its ev2 value

bool find_taught_producer_event(const byte ev2, byte *index) {

  bool found = false;

  for (byte i = 0; i < module_config.EE_MAX_EVENTS; i++) {
    if (module_config.evhashtbl[i] > 0 && module_config.getEventEVval(i, 1) == 1 && module_config.getEventEVval(i, 2) == ev2) {
      *index = i;
      found = true;
      break;
    }
  }

  return found;
}

/// send a taught producer event message by ev2, if it exists
/// it will be forwarded to other interfaces in the transmithandler

void send_producer_event_by_ev2(const byte ev2, const bool is_on_polarity) {

  byte index;
  can_msg_wrapper_t wmsg;

  if (find_taught_producer_event(ev2, &index)) {
    wmsg.msg.dlc = 5;
    wmsg.msg.data[0] = is_on_polarity ? OPC_ACON : OPC_ACOF;
    module_config.readEvent(index, &wmsg.msg.data[1]);

    queue_try_add(&bus0_output_queue, &wmsg);
  }

  return;
}

/// print formatted date and time

void print_date_and_time(void) {

  char tbuffer[32];

  // time_t now = time(nullptr) + (device_config.data.tzoffset * 60000);
  time_t now = time(nullptr);
  strftime(tbuffer, sizeof(tbuffer), "%c", gmtime(&now));
  // VLOG("current time: %s (%u), tz offset = %d mins", tbuffer, now, device_config.data.tzoffset);
  VLOG("current time: %s (%u)", tbuffer, now);

  return;
}

/// process input from the serial monitor

void process_serial_input(void) {

  static char serial_buffer[64];
  static byte serial_buffer_index = 0;

  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      serial_buffer[serial_buffer_index] = 0;
      execute_serial_command(serial_buffer);
      serial_buffer_index = 0;
    } else {
      serial_buffer[serial_buffer_index++] = c;
      serial_buffer[serial_buffer_index] = 0;
    }

    if (serial_buffer_index >= sizeof(serial_buffer)) {  // guard against input  buffer overrun
      serial_buffer_index = 0;
      serial_buffer[serial_buffer_index] = 0;
    }
  }

  return;
}

/// execute serial command

void execute_serial_command(char *serial_command) {

  char tokens[4][16], *ptr;
  byte num_tokens = 0;
  router_command_t r;
  can_msg_wrapper_t wmsg;

  VLOG("%s", serial_command);

  if (serial_command[0] == 0) {
    return;
  }

  ptr = strtok(serial_command, " ");

  while (ptr) {
    strcpy(tokens[num_tokens++], ptr);
    ptr = strtok(NULL, " ");
  }

  switch (tokens[0][0]) {

    case '*':
      VLOG("rebooting ...");
      rp2040.reboot();
      break;

    case 'r':
      module_config.resetModule();
      break;

    case 'e':
      VLOG("EEPROM:");

      for (byte i = 0; i < 20; i++) {
        VLOG(" %02u = %u", i, EEPROM[i]);
      }

      break;

    case 'f':
      if (GCtoCAN(tokens[1], &wmsg.msg)) {
        wmsg.retries = 0;
        wmsg.timestamp = micros();
        queue_try_add(&canbus1_input_queue, &wmsg);
      }

      break;

    case 'm':
      VLOG("free memory = %u", rp2040.getFreeHeap());
      break;

    case 'n':
      default_network_config(&netcfg);
      save_network_config(&netcfg);
      print_network_config(&netcfg);
      break;

    case 'p':
      if (num_tokens == 3) {
        module_config.writeNV(atoi(tokens[1]), atoi(tokens[2]));
      } else {
        VLOG("*** invalid syntax");
      }

      break;

    case 'v':
      VLOG("Node variables:");

      for (byte i = 0; i < module_config.EE_NUM_NVS; i++) {
        byte nvval = module_config.readNV(i + 1);
        VLOG(" * %2u: %3u, 0x%02X", i + 1, nvval, nvval);
      }

      break;

    case 'c':
      if (num_tokens == 3) {
        r.state = atoi(tokens[1]);
        r.command_num = atoi(tokens[2]);
        queue_try_add(&router_command_queue, &r);
      } else {
        VLOG("*** invalid syntax");
      }

      break;

    case 't':
      print_date_and_time();
      break;

    case 'w':  // push some test messages into the can1 input queue

      if (num_tokens == 2) {

        wmsg.retries = 0;
        wmsg.timestamp = micros();

        switch (atoi(tokens[1])) {

          case 0:  // event message
            wmsg.msg.id = 99 + CBUS_MEDIUM_PRIORITY_BITS;
            wmsg.msg.dlc = 5;
            wmsg.msg.data[0] = OPC_ASON;
            wmsg.msg.data[1] = 0;
            wmsg.msg.data[2] = 0;
            wmsg.msg.data[3] = 0;
            wmsg.msg.data[4] = 101;

            queue_try_add(&canbus1_input_queue, &wmsg);
            break;

          case 1:  // empty enumeration request
            wmsg.msg.id = 99 + CBUS_MEDIUM_PRIORITY_BITS;
            wmsg.msg.id |= CAN2040_ID_RTR;
            wmsg.msg.dlc = 0;

            queue_try_add(&canbus1_input_queue, &wmsg);
            break;

          case 2:  // empty enumeration response
            wmsg.msg.id = 99 + CBUS_MEDIUM_PRIORITY_BITS;
            wmsg.msg.dlc = 0;

            queue_try_add(&canbus1_input_queue, &wmsg);
            break;

          case 3:  // full extended message
            wmsg.msg.id = 99 + CBUS_MEDIUM_PRIORITY_BITS;
            wmsg.msg.dlc = 8;
            wmsg.msg.id |= CAN2040_ID_EFF;
            wmsg.msg.data[0] = 0;
            wmsg.msg.data[1] = 1;
            wmsg.msg.data[2] = 2;
            wmsg.msg.data[3] = 3;
            wmsg.msg.data[4] = 4;
            wmsg.msg.data[5] = 5;
            wmsg.msg.data[6] = 6;
            wmsg.msg.data[7] = 7;

            queue_try_add(&canbus1_input_queue, &wmsg);
            break;
        }
      } else {
        VLOG("*** invalid syntax");
      }

      break;

    case 'x':
      if (rtr->discoverer != nullptr) {
        rtr->discoverer->print_database();
      }

      break;

    case 'y':
      if (rtr->engine->ruleset != nullptr) {
        rtr->engine->print_ruleset();
      }

      break;

    case 'z':
      netcfg.wifi_mode = WIFI_MODE_STA;
      strcpy(netcfg.ssid, "HUAWEI-B311-E39A");
      strcpy(netcfg.password, "33260100");
      netcfg.ap_channel = 1;
      save_network_config(&netcfg);

      break;

    default:
      break;
  }

  memset(tokens, 0, sizeof(tokens));
  return;
}

///
