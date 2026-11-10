
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

#include "hardware/pio.h"

#include <EEPROM.h>
#include <SPI.h>

#include <ACAN2040.h>
#include <CBUSconfig.h>
#include <CBUSLED.h>

#include "can2040.h"

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"
#include "interface.h"

#include "pico/util/queue.h"

#if HAS_CAN_FD
#include <ACAN2517FD.h>
static ACAN2517FD *canfdptr;
#endif

#if HAS_EXT_MCP2518
#include <ACAN2517.h>
static ACAN2517 *can2518ptr;
#endif

#if HAS_EXT_MCP2515
#include <ACAN2515.h>
static ACAN2515 *can2515ptr;
#endif

/// can2040 callback for secondary can buses - cannot be a class member as called from interrupt handler

static void can2040_callback(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg);

/// globals variables defined in other source files

extern CBUSConfig module_config;

/// global variables

volatile bool can_error = false;
queue_t canbus1_input_queue, canbus2_input_queue;  // globals for callback functions - connected to can interface input queue in object constructor

/// non-class functions

/// can2040 callback for CAN secondary buses
/// *** this function is called in interrupt context, so do as little work as possible

static void can2040_callback(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg) {

  can_msg_wrapper_t wmsg;

  switch (notify) {

    case CAN2040_NOTIFY_RX:
      wmsg.msg = *msg;
      wmsg.retries = 0;
      wmsg.timestamp = micros();

      switch (cd->pio_num) {
        case 1:
          queue_try_add(&canbus1_input_queue, &wmsg);
          break;

        case 2:
          queue_try_add(&canbus2_input_queue, &wmsg);
          break;
      }

      break;

    case CAN2040_NOTIFY_TX:
      break;

    case CAN2040_NOTIFY_ERROR:
      can_error = true;
      break;

    default:
      break;
  }

  return;
}

/// cbus interface shim class - provides an interface-like api to the base cbus functionality
/// always enabled

cbus_interface::cbus_interface(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led)
  : abstract_interface(num, interface_type, activity_led, error_led) {

  VLOG("cbus_interface: constructor, num = %u. interface type = %u", num, interface_type);
}

bool cbus_interface::begin([[maybe_unused]] const bool force_enumeration) {

  VLOG("cbus_interface/%u: begin", num);
  enabled = true;
  return true;
}

void cbus_interface::enable([[maybe_unused]] const bool new_state) {

  // always enabled
  return;
}

void cbus_interface::run(void) {

  // intentionally empty
  // handled by loop1()

  return;
}

bool cbus_interface::forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) {

  unsigned long m1 = micros() - wmsg->timestamp;

  VLOG("cbus_interface/%u: forwarding message, t = %u, rewrite = %u", num, m1, rewrite);

  if (rewrite) {
    rewrite_can_id(wmsg, module_config.CANID);
  }

  queue_add_blocking(coq, wmsg);
  return true;
}

bool cbus_interface::available(void) {

  return (!(queue_is_empty(ciq)));
}

bool cbus_interface::get_message(can_msg_wrapper_t *wmsg) {

  return queue_try_remove(ciq, wmsg);
}

void cbus_interface::discover_nodes(void) {

  VLOG("cbus_interface/%u: discover nodes", num);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 1;
  wmsg.msg.data[0] = OPC_QNN;

  wmsg.timestamp = micros();
  wmsg.retries = 0;

  forward_message(&wmsg, true);
  return;
}

void cbus_interface::query_node(const uint16_t node_number) {

  VLOG("cbus_interface/%u: query node %u", num, node_number);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 3;
  wmsg.msg.data[0] = OPC_NERD;
  wmsg.msg.data[1] = node_number >> 8;
  wmsg.msg.data[2] = node_number & 0x7f;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);
  return;
}

byte cbus_interface::get_status(void) {

  return INTERFACE_STATUS_OK;
}

/// secondary CAN bus interfaces

canbus::canbus(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led)
  : abstract_interface(num, interface_type, activity_led, error_led) {

  VLOG("can/%u: constructor, interface type = %u", num, interface_type);
  enabled = false;

  // check PIO state machine claim states

#ifdef PICO_RP2040
  PIO pio = pio1;
#else
  PIO pio = (interface_type == INTERFACE_TYPE_CAN_1 ? pio1 : pio2);
#endif

  uint sms_claimed = 0;

  for (uint sm = 0; sm < 4; sm++) {
    if (pio_sm_is_claimed(pio, sm)) {
      ++sms_claimed;
    }
  }

  if (sms_claimed > 0) {
    VLOG("can/%u: *** pio %u, %u sm resources are already claimed", num, interface_type - 1, sms_claimed);
    error_led.pulse();
    startup_ok = false;
  }

  //

  switch (interface_type) {

    case INTERFACE_TYPE_CAN_1:

      VLOG("can/%u: creating message queues", num);

      queue_init(&canbus1_input_queue, sizeof(can_msg_wrapper_t), CAN_QUEUE_SIZE);
      ciq = &canbus1_input_queue;
      queue_init(&tx_retry_queue, sizeof(can_msg_wrapper_t), CAN_TX_RETRY_QUEUE_SIZE);

      VLOG("can/%u: creating ACAN2040 object using pio instance 1", num);
      can = new ACAN2040(1, CAN_TX_PIN_1, CAN_RX_PIN_1, CAN_BITRATE, F_CPU, can2040_callback);
      break;

    case INTERFACE_TYPE_CAN_2:

      VLOG("can/%u: creating message queues", num);

      queue_init(&canbus2_input_queue, sizeof(can_msg_wrapper_t), CAN_QUEUE_SIZE);
      ciq = &canbus2_input_queue;
      queue_init(&tx_retry_queue, sizeof(can_msg_wrapper_t), CAN_TX_RETRY_QUEUE_SIZE);

      VLOG("can/%u: creating ACAN2040 object using pio instance 2", num);
      can = new ACAN2040(2, CAN_TX_PIN_2, CAN_RX_PIN_2, CAN_BITRATE, F_CPU, can2040_callback);
      break;
  }
}

bool canbus::begin(const bool force_enumeration) {

  VLOG("can/%u: begin, interface type = %u", num, interface_type);

  canid = EEPROM[interface_type == INTERFACE_TYPE_CAN_1 ? 12 : 13];  // load previously stored id, location 12 or 13
  can->begin();
  enumerate_bus(force_enumeration);

  VLOG("can/%u: initialisation complete, stored can id = %u", num, canid);
  return true;
}

void canbus::enable(const bool new_state) {

  can_msg_wrapper_t tmsg;

  if (!enabled && new_state) {  // if enabling, empty queues to avoid stale messages
    while (queue_try_remove(ciq, &tmsg))
      ;
    while (queue_try_remove(&tx_retry_queue, &tmsg))
      ;
  }

  enabled = new_state;
  VLOG("can/%u: enabled = %u", num, enabled);

  return;
}

void canbus::run(void) {

  can_msg_wrapper_t wmsg;

  /// try to drain down the tx retry queue

  while (can->ok_to_send() && queue_try_remove(&tx_retry_queue, &wmsg)) {
    can->send_message(&wmsg.msg);
    activity_led.pulse();
  }

  if (can_error) {
    can_error = false;
    error_led.pulse();
  }

  return;
}

bool canbus::forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) {

  unsigned long m1 = micros() - wmsg->timestamp;

  VLOG("can/%u: forwarding message, t = %u, rewrite = %u", num, m1, rewrite);

  if (rewrite) {
    rewrite_can_id(wmsg, canid);
  }

  if (can->ok_to_send() && can->send_message(&wmsg->msg)) {
    activity_led.pulse();
  } else {
    VLOG("can/%u: *** unable to send message - queueing", num);
    queue_try_add(&tx_retry_queue, wmsg);
  }

  return true;
}

bool canbus::available(void) {

  return (!queue_is_empty(ciq));
}

///

bool canbus::get_message(can_msg_wrapper_t *wmsg) {

  return queue_try_remove(ciq, wmsg);
}

void canbus::enumerate_bus(const bool force) {

  /// *** NB this function blocks for 100ms ***

  byte enumeration_responses[16], rcanid;
  unsigned long enumeration_start_time = millis();
  can_msg_wrapper_t wmsg;

  VLOG("can/%u: enumerate_bus: current id = %u, force = %u", num, canid, force);

  if (enabled) {
    if (canid < 1 || canid > 99 || force) {

      VLOG("can/%u: enumerating bus", num);

      /// send enumeration request message

      wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;

      wmsg.msg.id |= CAN2040_ID_RTR;
      wmsg.msg.dlc = 0;

      wmsg.timestamp = micros();
      wmsg.retries = 0;

      forward_message(&wmsg, false);
      VLOG("can/%u: sent enumeration request message", num);

      /// read enum responses for 100mS

      VLOG("can/%u: receiving responses", num);

      while ((millis() - enumeration_start_time) <= BUS_ENUMERATION_WINDOW) {

        if (queue_try_remove(ciq, &wmsg)) {
          if (wmsg.msg.dlc == 0 && !(wmsg.msg.id & CAN2040_ID_RTR)) {

            rcanid = wmsg.msg.id & 0x7f;
            VLOG("enumeration response from id = %u", rcanid);

            if (rcanid > 0) {
              bitWrite(enumeration_responses[rcanid / 16], rcanid % 8, 1);
            }
          }
        }
      }

      /// take first unused can id

      VLOG("can/%u: processing responses", num);

      for (byte i = 0; i < 16; i++) {
        for (byte b = 0; b < 8; b++) {

          if (i == 0 && b == 0) {
            continue;
          }

          if (!bitRead(enumeration_responses[i], b)) {
            canid = (i * 8) + b;
            goto enum_done;
          }
        }
      }

enum_done:

      /// store it

      EEPROM[10 + interface_type] = canid;  // 12 or 13
      EEPROM.commit();
      VLOG("can/%u: canid is now %u", num, canid);

    } else {
      VLOG("can/%u: not enumerating - using previously stored can id = %u", num, canid);
    }
  } else {
    VLOG("can/%u: not enumerating - not enabled");
  }

  return;
}

void canbus::respond_to_enumeration_request(void) {

  can_msg_wrapper_t wmsg;

  VLOG("can/%u: responding to enumeration request, canid = %u", num, canid);

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 0;

  wmsg.timestamp = micros();
  wmsg.retries = 0;

  forward_message(&wmsg, false);
  return;
}

void canbus::discover_nodes(void) {

  VLOG("can/%u: discover nodes", num);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 1;
  wmsg.msg.data[0] = OPC_QNN;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);
  return;
}

void canbus::query_node(const uint16_t node_number) {

  VLOG("can/%u: query node %u", num, node_number);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 3;
  wmsg.msg.data[0] = OPC_NERD;
  wmsg.msg.data[1] = node_number >> 8;
  wmsg.msg.data[2] = node_number & 0x7f;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);
  return;
}

byte canbus::get_status(void) {

  return INTERFACE_STATUS_OK;
}

//
/// CAN backbone interface - point-to-point high-speed CAN 2.0b link
//

can_backbone::can_backbone(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led)
  : abstract_interface(num, interface_type, activity_led, error_led) {

  VLOG("can_backbone/%u: constructor, interface type = %u", num, interface_type);
  enabled = false;

  switch (interface_type) {

    case INTERFACE_TYPE_CAN_BACKBONE_1:

      VLOG("can_backbone/%u: creating message queues", num);

      if (!queue_init(&canbus1_input_queue, sizeof(can_msg_wrapper_t), CAN_QUEUE_SIZE)) {
        VLOG("can_backbone/%u: *** error creating message queue");
      }

      queue_init(&tx_retry_queue, sizeof(can_msg_wrapper_t), CAN_TX_RETRY_QUEUE_SIZE);
      ciq = &canbus1_input_queue;

      VLOG("can_backbone/%u: creating ACAN2040 object using pio instance 1", num);
      can = new ACAN2040(1, CAN_TX_PIN_1, CAN_RX_PIN_1, CAN_BACKBONE_BITRATE, F_CPU, can2040_callback);
      break;

    case INTERFACE_TYPE_CAN_BACKBONE_2:

      VLOG("can_backbone/%u: creating message queues", num);
      queue_init(&canbus2_input_queue, sizeof(can_msg_wrapper_t), CAN_QUEUE_SIZE);
      queue_init(&tx_retry_queue, sizeof(can_msg_wrapper_t), CAN_TX_RETRY_QUEUE_SIZE);
      ciq = &canbus2_input_queue;

      VLOG("can_backbone/%u: creating ACAN2040 object using pio instance 2", num);
      can = new ACAN2040(2, CAN_TX_PIN_2, CAN_RX_PIN_2, CAN_BACKBONE_BITRATE, F_CPU, can2040_callback);
      break;
  }
}

bool can_backbone::begin([[maybe_unused]] const bool force_enumeration) {

  VLOG("can_backbone/%u: begin, interface type = %u", num, interface_type);

  canid = module_config.readNV(13);
  can->begin();

  VLOG("can_backbone/%u: initialisation complete, stored can id = %u", num, canid);
  return true;
}

void can_backbone::enable(const bool new_state) {

  can_msg_wrapper_t tmsg;

  if (!enabled && new_state) {  // if enabling, empty queues to avoid stale messages
    while (queue_try_remove(ciq, &tmsg))
      ;
    while (queue_try_remove(&tx_retry_queue, &tmsg))
      ;
  }

  enabled = new_state;
  VLOG("can_backbone/%u: enabled = %u", num, enabled);

  return;
}

void can_backbone::run(void) {

  can_msg_wrapper_t wmsg;

  /// try to drain down the tx retry queue
  /// *** TODO *** elevate priority based on latency and retries
  /// *** TODO *** do error checking
  /// *** TODO *** collect stats

  while (can->ok_to_send() && queue_try_remove(&tx_retry_queue, &wmsg)) {
    can->send_message(&wmsg.msg);
    activity_led.pulse();
  }

  if (can_error) {
    can_error = false;
    error_led.pulse();
  }

  return;
}

bool can_backbone::forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) {

  VLOG("can_backbone/%u: forwarding message, rewrite = %u", num, rewrite);

  if (rewrite) {
    rewrite_can_id(wmsg, canid);
  }

  if (can->ok_to_send() && can->send_message(&wmsg->msg)) {
    activity_led.pulse();
  } else {
    VLOG("can_backbone/%u: *** unable to send message - queueing", num);
    queue_try_add(&tx_retry_queue, wmsg);
  }

  return true;
}

bool can_backbone::available(void) {

  return (!queue_is_empty(ciq));
}

bool can_backbone::get_message(can_msg_wrapper_t *wmsg) {

  return queue_try_remove(ciq, wmsg);
}

void can_backbone::enumerate_bus([[maybe_unused]] const bool force) {

  return;
}

void can_backbone::respond_to_enumeration_request(void) {

  return;
}

void can_backbone::discover_nodes(void) {

  VLOG("can_backbone/%u: discover nodes", num);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 1;
  wmsg.msg.data[0] = OPC_QNN;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);

  return;
}

void can_backbone::query_node(const uint16_t node_number) {

  VLOG("can_backbone/%u: query node %u", num, node_number);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 3;
  wmsg.msg.data[0] = OPC_NERD;
  wmsg.msg.data[1] = node_number >> 8;
  wmsg.msg.data[2] = node_number & 0x7f;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);

  return;
}

byte can_backbone::get_status(void) {

  return INTERFACE_STATUS_OK;
}

///
/// CAN-FD high speed point-to-point backbone link
///

canfdbus::canfdbus(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led)
  : abstract_interface(num, interface_type, activity_led, error_led) {

  VLOG("canfd/%u: constructor, interface type = %u", num, interface_type);
  enabled = false;

#if HAS_CAN_FD
  VLOG("canfd/%u: creating ACAN2517FD object", num);

  MCP2518_SPI_INSTANCE.setTX(MCP2518_MOSI_PIN);
  MCP2518_SPI_INSTANCE.setRX(MCP2518_MISO_PIN);
  MCP2518_SPI_INSTANCE.setSCK(MCP2518_SCK_PIN);
  MCP2518_SPI_INSTANCE.setCS(MCP2518_CS_PIN);
  MCP2518_SPI_INSTANCE.begin();

  can = new ACAN2517FD(MCP2518_CS_PIN, MCP2518_SPI_INSTANCE, MCP2518_INT_PIN);
  canfdptr = can;
#endif
}

bool canfdbus::begin([[maybe_unused]] const bool force_enumeration) {

  VLOG("canfd/%u: begin: interface type = %u", num, interface_type);

  canid = module_config.readNV(13);  // load can arbitrationid from nv 13

#if HAS_CAN_FD
  // settings for 4MHz crystal oscillator, 1Mb/s bus frequency

  ACAN2517FDSettings settings(ACAN2517FDSettings::OSC_4MHz10xPLL, CAN_FD_BUS_FREQUENCY, DataBitRateFactor::x8);
  settings.mDriverReceiveFIFOSize = CAN_FD_RECEIVE_FIFO_SIZE;
  settings.mRequestedMode = ACAN2517FDSettings::NormalFD;

  const uint32_t error_code = canfdptr->begin(settings, [] {
    canfdptr->isr();
  });

  if (error_code != 0) {
    VLOG("canfd/%u: begin: *** error initialising MCP2518 = %u", num, error_code);
    return false;
  }

  enumerate_bus(force_enumeration);
#endif

  VLOG("canfd/%u: initialisation complete, stored can id = %u", num, canid);
  return true;
}

void canfdbus::enable(const bool new_state) {

  //  can_msg_wrapper_t tmsg;

  //  if (!enabled && new_state) {                          // if enabling, empty queues to avoid stale messages
  //    // VLOG("canfd/%u: emptying queues", num);
  //    while (queue_try_remove(&input_queue, &tmsg));
  //  }

  enabled = new_state;
  VLOG("canfd/%u: enabled = %u", num, enabled);

  return;
}

void canfdbus::run(void) {

#if HAS_CAN_FD

  static uint8_t last_opmode = 0;
  static uint32_t last_error_counters = 0;
  static uint32_t last_diaginfo_0 = 0, last_diaginfo_1 = 0;

  uint8_t opmode = can->currentOperationMode();

  if (opmode != last_opmode) {
    last_opmode = opmode;
    VLOG("canfd/%u: operations mode changed to %u", opmode);
  }

  uint32_t error_counters = can->errorCounters();

  if (error_counters != last_error_counters) {
    last_error_counters = error_counters;
    VLOG("canfd/%u: error counters = %u", error_counters);
  }

  uint32_t diaginfo_0 = can->diagInfos(0);

  if (diaginfo_0 != last_diaginfo_0) {
    last_diaginfo_0 = diaginfo_0;
    VLOG("canfd/%u: diag info 0 = %u", diaginfo_0);
  }

  uint32_t diaginfo_1 = can->diagInfos(1);

  if (diaginfo_1 != last_diaginfo_1) {
    last_diaginfo_1 = diaginfo_1;
    VLOG("canfd/%u: diag info 1 = %u", diaginfo_1);
  }
#endif

  return;
}

bool canfdbus::forward_message(can_msg_wrapper_t *wmsg, const bool rewrite) {

  bool ok = false;

  VLOG("canfd/%u: forwarding message, rewrite = %u", num, rewrite);

#if HAS_CAN_FD
  CANFDMessage frame;

  frame.id = canid;
  frame.len = sizeof(can_msg_wrapper_t);
  memcpy(&frame.data, wmsg, sizeof(can_msg_wrapper_t));
  frame.pad();

  if (!frame.isValid()) {
    VLOG("canfd/%u: forward_message: *** frame failed validation");
  }

  ok = can->tryToSend(frame);
#endif

  return ok;
}

bool canfdbus::available(void) {

#if HAS_CAN_FD
  return can->available();
#else
  return false;
#endif
}

bool canfdbus::get_message(can_msg_wrapper_t *wmsg) {

  bool ok = false;

#if HAS_CAN_FD
  CANFDMessage frame;

  if (can->available() && can->receive(frame)) {
    if (frame.len >= sizeof(can_msg_wrapper_t)) {
      memcpy(wmsg, &frame.data, sizeof(can_msg_wrapper_t));
      wmsg->retries = 0;
      wmsg->timestamp = micros();
      ok = true;
    }

    return ok;
  }
#endif

  return ok;
}

byte canfdbus::get_status(void) {

  return (INTERFACE_STATUS_OK);
}

void canfdbus::discover_nodes(void) {

  VLOG("canfdbus/%u: discover nodes", num);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 1;
  wmsg.msg.data[0] = OPC_QNN;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);
  return;
}

void canfdbus::query_node(const uint16_t node_number) {

  VLOG("canfdbus/%u: query node %u", num, node_number);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 3;
  wmsg.msg.data[0] = OPC_NERD;
  wmsg.msg.data[1] = node_number >> 8;
  wmsg.msg.data[2] = node_number & 0x7f;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, false);
  return;
}

/// canmcp2515 - external SPI MCP2515 bus interface

canmcp2515::canmcp2515(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led)
  : abstract_interface(num, interface_type, activity_led, error_led) {

  VLOG("canmcp2515/%u: constructor, interface type = %u", num, interface_type);
  enabled = false;

#if HAS_EXT_MCP2515
  VLOG("canmcp2515/%u: creating ACAN2515 object", num);

  MCP2515_SPI_INSTANCE.setTX(MCP2515_MOSI_PIN);
  MCP2515_SPI_INSTANCE.setRX(MCP2515_MISO_PIN);
  MCP2515_SPI_INSTANCE.setSCK(MCP2515_SCK_PIN);
  MCP2515_SPI_INSTANCE.setCS(MCP2515_CS_PIN);
  MCP2515_SPI_INSTANCE.begin();

  can = new ACAN2515(MCP2515_CS_PIN, MCP2515_SPI_INSTANCE, MCP2515_INT_PIN);
  can2515ptr = can;
#else
  VLOG("canmcp2515/%u: support not compiled", num);
#endif
}

bool canmcp2515::begin([[maybe_unused]] const bool force_enumeration) {

  VLOG("canmcp2515/%u: begin: interface type = %u", num, interface_type);

  canid = EEPROM[14];

#if HAS_EXT_MCP2515
  ACAN2515Settings settings(CAN_EXT_2515_XTAL_FREQUENCY, CAN_EXT_2515_BUS_FREQUENCY);

  settings.mRequestedMode = ACAN2515Settings::NormalMode;
  settings.mReceiveBufferSize = CAN_EXT_2515_RECEIVE_FIFO_SIZE;
  settings.mTransmitBuffer0Size = CAN_EXT_2515_TRANSMIT_FIFO_SIZE;
  settings.mTransmitBuffer1Size = 0;
  settings.mTransmitBuffer2Size = 0;

  const uint32_t error_code = can->begin(settings, [] {
    can2515ptr->isr();
  });

  if (error_code != 0) {
    VLOG("canmcp2515/%u: begin: *** error initialising MCP2515 = %u", num, error_code);
    return false;
  }

  enumerate_bus(force_enumeration);
#endif

  VLOG("canmcp2515/%u: initialisation complete, stored can id = %u", num, canid);
  return true;
}

void canmcp2515::enable(const bool new_state) {

  can_msg_wrapper_t tmsg;

  if (!enabled && new_state) {  // if enabling, empty queues to avoid stale messages
    // VLOG("canmcp2515/%u: emptying queues", num);
    while (queue_try_remove(&input_queue, &tmsg))
      ;
  }

  enabled = new_state;
  VLOG("canmcp2515/%u: enabled = %u", num, enabled);

  return;
}

void canmcp2515::run(void) {

  return;
}

bool canmcp2515::forward_message([[maybe_unused]] can_msg_wrapper_t *wmsg, [[maybe_unused]] const bool rewrite) {

  bool ok = false;

  VLOG("canmcp2515/%u: forwarding message, rewrite = %u", num, rewrite);

#if HAS_EXT_MCP2515
  CANMessage frame;

  frame.id = canid;
  frame.len = wmsg->msg.dlc;
  frame.rtr = wmsg->msg.id & CAN2040_ID_RTR;
  frame.ext = wmsg->msg.id & CAN2040_ID_EFF;
  memcpy(&frame.data, wmsg->msg.data, frame.len);

  ok = can->tryToSend(frame);
#endif

  return ok;
}

bool canmcp2515::available(void) {

#if HAS_EXT_MCP2515
  return can->available();
#endif

  return false;
}

bool canmcp2515::get_message([[maybe_unused]] can_msg_wrapper_t *wmsg) {

  bool ok = false;

#if HAS_EXT_MCP2515
  CANMessage frame;

  if (can->available() && can->receive(frame)) {
    wmsg->msg.id = frame.id;
    wmsg->msg.dlc = frame.len;

    if (frame.rtr) {
      wmsg->msg.id |= CAN2040_ID_RTR;
    }

    if (frame.ext) {
      wmsg->msg.id |= CAN2040_ID_EFF;
    }

    memcpy(wmsg->msg.data, &frame.data, frame.len);
    wmsg->retries = 0;
    wmsg->timestamp = micros();
    ok = true;
  }
#endif

  return ok;
}

void canmcp2515::enumerate_bus(const bool force) {

  byte enumeration_responses[16], rcanid;
  unsigned long enumeration_start_time = millis();
  can_msg_wrapper_t wmsg;

  VLOG("canmcp2515/%u: enumerate_bus: current id = %u, force = %u", num, canid, force);

  if (enabled) {
    if (canid < 1 || canid > 99 || force) {

      VLOG("canmcp2515/%u: enumerating bus", num);

      /// send enumeration request message

      wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;

      wmsg.msg.id |= CAN2040_ID_RTR;
      wmsg.msg.dlc = 0;

      wmsg.timestamp = micros();
      wmsg.retries = 0;

      forward_message(&wmsg, false);
      VLOG("canmcp2515/%u: sent enumeration request message", num);

      /// read enum responses for 100mS

      VLOG("can/%u: receiving responses", num);

      while ((millis() - enumeration_start_time) <= BUS_ENUMERATION_WINDOW) {

        if (queue_try_remove(ciq, &wmsg)) {  // *** use receive method !! **
          if (wmsg.msg.dlc == 0 && !(wmsg.msg.id & CAN2040_ID_RTR)) {

            rcanid = wmsg.msg.id & 0x7f;
            VLOG("enumeration response from id = %u", rcanid);

            if (rcanid > 0) {
              bitWrite(enumeration_responses[rcanid / 16], rcanid % 8, 1);
            }
          }
        }
      }

      /// take first unused can id

      VLOG("canmcp2515/%u: processing responses", num);

      for (byte i = 0; i < 16; i++) {
        for (byte b = 0; b < 8; b++) {

          if (i == 0 && b == 0) {
            continue;
          }

          if (!bitRead(enumeration_responses[i], b)) {
            canid = (i * 8) + b;
            goto enum_done;
          }
        }
      }

enum_done:

      /// store it

      EEPROM[14] = canid;
      EEPROM.commit();
      VLOG("canmcp2515/%u: canid is now %u", num, canid);

    } else {
      VLOG("canmcp2515/%u: not enumerating - using previously stored can id = %u", num, canid);
    }
  } else {
    VLOG("canmcp2515/%u: not enumerating - not enabled");
  }

  return;
}

void canmcp2515::respond_to_enumeration_request(void) {

  can_msg_wrapper_t wmsg;

  VLOG("canmcp2515/%u: responding to enumeration request", num);

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 0;

  wmsg.timestamp = micros();
  wmsg.retries = 0;

  forward_message(&wmsg, false);
  return;
}

void canmcp2515::discover_nodes(void) {

  VLOG("canmcp2515/%u: discover nodes", num);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 1;
  wmsg.msg.data[0] = OPC_QNN;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);
  return;
}

void canmcp2515::query_node(const uint16_t node_number) {

  VLOG("canmcp2515/%u: query node %u", num, node_number);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 3;
  wmsg.msg.data[0] = OPC_NERD;
  wmsg.msg.data[1] = node_number >> 8;
  wmsg.msg.data[2] = node_number & 0x7f;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);
  return;
}

byte canmcp2515::get_status(void) {
  byte ret = 0;
#if HAS_EXT_MCP2515
  ret = can2515ptr->errorFlagRegister() == 0) ? INTERFACE_STATUS_OK : INTERFACE_STATUS_ERROR;
#endif
  return ret;
}

///  external SPI MCP2515 interface high-speed backbone link

canmcp2515backbone::canmcp2515backbone(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led)
  : abstract_interface(num, interface_type, activity_led, error_led) {

  VLOG("canmcp2515backbone/%u: constructor, interface type = %u", num, interface_type);
  enabled = false;

#if HAS_EXT_MCP2515
  VLOG("canmcp2515backbone/%u: creating ACAN2515 object", num);

  MCP2515_SPI_INSTANCE.setTX(MCP2515_MOSI_PIN);
  MCP2515_SPI_INSTANCE.setRX(MCP2515_MISO_PIN);
  MCP2515_SPI_INSTANCE.setSCK(MCP2515_SCK_PIN);
  MCP2515_SPI_INSTANCE.setCS(MCP2515_CS_PIN);
  MCP2515_SPI_INSTANCE.begin();

  can = new ACAN2515(MCP2515_CS_PIN, MCP2515_SPI_INSTANCE, MCP2515_INT_PIN);
  can2515ptr = can;
#else
  VLOG("canmcp2515backbone/%u: support not compiled", num);
#endif
}

bool canmcp2515backbone::begin([[maybe_unused]] const bool force_enumeration) {

  VLOG("canmcp2515backbone/%u: begin: interface type = %u", num, interface_type);

  canid = module_config.readNV(13);  // load can arbitration id from nv 13

#if HAS_EXT_MCP2515
  ACAN2515Settings settings(CAN_EXT_2515_XTAL_FREQUENCY, CAN_EXT_2515_BACKBONE_BUS_FREQUENCY);

  settings.mRequestedMode = ACAN2515Settings::NormalMode;
  settings.mReceiveBufferSize = CAN_EXT_2515_RECEIVE_FIFO_SIZE;
  settings.mTransmitBuffer0Size = CAN_EXT_2515_TRANSMIT_FIFO_SIZE;
  settings.mTransmitBuffer1Size = 0;
  settings.mTransmitBuffer2Size = 0;

  const uint32_t error_code = can->begin(settings, [] {
    can2515ptr->isr();
  });

  if (error_code != 0) {
    VLOG("canmcp2515backbone/%u: begin: *** error initialising MCP2515 = %u", num, error_code);
    return false;
  }

  enumerate_bus(force_enumeration);
#endif

  VLOG("canmcp2515backbone/%u: initialisation complete, stored can id = %u", num, canid);
  return true;
}

void canmcp2515backbone::enable(const bool new_state) {

  can_msg_wrapper_t tmsg;

  if (!enabled && new_state) {  // if enabling, empty queues to avoid stale messages
    // VLOG("canmcp2515backbone/%u: emptying queues", num);
    while (queue_try_remove(&input_queue, &tmsg))
      ;
  }

  enabled = new_state;
  VLOG("canmcp2515backbone/%u: enabled = %u", num, enabled);

  return;
}

void canmcp2515backbone::run(void) {

  return;
}

bool canmcp2515backbone::forward_message([[maybe_unused]] can_msg_wrapper_t *wmsg, [[maybe_unused]] const bool rewrite) {

  bool ok = false;

  VLOG("canmcp2515backbone/%u: forwarding message, rewrite = %u", num, rewrite);

#if HAS_EXT_MCP2515
  CANMessage frame;

  frame.id = canid;
  frame.len = wmsg->msg.dlc;
  frame.rtr = wmsg->msg.id & CAN2040_ID_RTR;
  frame.ext = wmsg->msg.id & CAN2040_ID_EFF;
  memcpy(&frame.data, wmsg->msg.data, frame.len);

  ok = can->tryToSend(frame);
#endif

  return ok;
}

bool canmcp2515backbone::available(void) {

#if HAS_EXT_MCP2515
  return can->available();
#endif

  return false;
}

bool canmcp2515backbone::get_message([[maybe_unused]] can_msg_wrapper_t *wmsg) {

  bool ok = false;

#if HAS_EXT_MCP2515
  CANMessage frame;

  if (can->available() && can->receive(frame)) {
    wmsg->msg.id = frame.id;
    wmsg->msg.dlc = frame.len;

    if (frame.rtr) {
      wmsg->msg.id |= CAN2040_ID_RTR;
    }

    if (frame.ext) {
      wmsg->msg.id |= CAN2040_ID_EFF;
    }

    memcpy(wmsg->msg.data, &frame.data, frame.len);
    wmsg->retries = 0;
    wmsg->timestamp = micros();
    ok = true;
  }
#endif

  return ok;
}

void canmcp2515backbone::enumerate_bus([[maybe_unused]] const bool force) {

  return;
}

void canmcp2515backbone::respond_to_enumeration_request(void) {

  return;
}

void canmcp2515backbone::discover_nodes(void) {

  VLOG("canmcp2515backbone/%u: discover nodes", num);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 1;
  wmsg.msg.data[0] = OPC_QNN;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);
  return;
}

void canmcp2515backbone::query_node(const uint16_t node_number) {

  VLOG("canmcp2515backbone/%u: query node %u", num, node_number);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 3;
  wmsg.msg.data[0] = OPC_NERD;
  wmsg.msg.data[1] = node_number >> 8;
  wmsg.msg.data[2] = node_number & 0x7f;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);
  return;
}

byte canmcp2515backbone::get_status(void) {
  byte ret = 0;
#if HAS_EXT_MCP2515
  ret = can2515ptr->errorFlagRegister() == 0 ? INTERFACE_STATUS_OK : INTERFACE_STATUS_ERROR;
#endif

  return ret;
}

/// canmcp2518 - SPI MCP2518 in CAN 2.0b mode

canmcp2518::canmcp2518(const byte num, const byte interface_type, CBUSLED &activity_led, CBUSLED &error_led)
  : abstract_interface(num, interface_type, activity_led, error_led) {

  VLOG("canmcp2518/%u: constructor, interface type = %u", num, interface_type);
  enabled = false;

#if HAS_EXT_MCP2518
  // VLOG("canmcp2518/%u: creating ACAN2517 object", num);

  // MCP2518_SPI_INSTANCE.setTX(MCP2518_MOSI_PIN);
  // MCP2518_SPI_INSTANCE.setRX(MCP2518_MISO_PIN);
  // MCP2518_SPI_INSTANCE.setSCK(MCP2518_SCK_PIN);
  // MCP2518_SPI_INSTANCE.setCS(MCP2518_CS_PIN);
  // MCP2518_SPI_INSTANCE.begin();

  // can = new ACAN2517(MCP2518_CS_PIN, MCP2518_SPI_INSTANCE, MCP2518_INT_PIN);
  // can2518ptr = can;
#endif
}

bool canmcp2518::begin([[maybe_unused]] const bool force_enumeration) {

  VLOG("canmcp2518/%u: begin: interface type = %u", num, interface_type);

#if HAS_EXT_MCP2518
  canid = EEPROM[14];

  // ACAN2517Settings settings(ACAN2517Settings::OSC_4MHz10xPLL, MCP2518_BUS_FREQUENCY);
  // settings.mRequestedMode = ACAN2517Settings::Normal20B;
  // settings.mDriverTransmitFIFOSize = MCP2518_TRANSMIT_FIFO_SIZE;
  // settings.mDriverReceiveFIFOSize = MCP2518_RECEIVE_FIFO_SIZE;

  // const uint32_t error_code = can->begin(settings, [] {
  //   can2518ptr->isr();
  // });

  // if (error_code != 0) {
  //   VLOG("canmcp2518/%u: begin: *** error initialising MCP2518 = %u", num, error_code);
  //   return false;
  // }

  // enumerate_bus(force_enumeration);
#endif
  VLOG("canmcp2518/%u: initialisation complete, stored can id = %u", num, canid);

  return true;
}

void canmcp2518::enable([[maybe_unused]] const bool new_state) {

#if HAS_EXT_MCP2518
  // can_msg_wrapper_t tmsg;

  // if (!enabled && new_state) {  // if enabling, empty queues to avoid stale messages
  //   // VLOG("canmcp2518/%u: emptying queues", num);
  //   while (queue_try_remove(&input_queue, &tmsg))
  //     ;
  // }

  enabled = new_state;
  VLOG("canmcp2518/%u: enabled = %u", num, enabled);
#endif

  return;
}

void canmcp2518::run(void) {

  return;
}

bool canmcp2518::forward_message([[maybe_unused]] can_msg_wrapper_t *wmsg, [[maybe_unused]] const bool rewrite) {

  bool ok = false;

#if HAS_EXT_MCP2518
  VLOG("canmcp2518/%u: forwarding message, rewrite = %u", num, rewrite);

  // CANMessage frame;

  // frame.id = canid;
  // frame.len = wmsg->msg.dlc;
  // frame.rtr = wmsg->msg.id & CAN2040_ID_RTR;
  // frame.ext = wmsg->msg.id & CAN2040_ID_EFF;
  // memcpy(&frame.data, wmsg->msg.data, frame.len);

  // ok = can->tryToSend(frame);
#endif

  return ok;
}

bool canmcp2518::available(void) {

#if HAS_EXT_MCP2518
  // return can->available();
#endif

  return false;
}

bool canmcp2518::get_message([[maybe_unused]] can_msg_wrapper_t *wmsg) {

  bool ok = false;

#if HAS_EXT_MCP2518
  // CANMessage frame;

  // if (can->available() && can->receive(frame)) {
  //   wmsg->msg.id = frame.id;
  //   wmsg->msg.dlc = frame.len;

  //   if (frame.rtr) {
  //     wmsg->msg.id |= CAN2040_ID_RTR;
  //   }

  //   if (frame.ext) {
  //     wmsg->msg.id |= CAN2040_ID_EFF;
  //   }

  //   memcpy(wmsg->msg.data, &frame.data, frame.len);
  //   wmsg->retries = 0;
  //   wmsg->timestamp = micros();
  //   ok = true;
  // }
#endif

  return ok;
}

void canmcp2518::enumerate_bus([[maybe_unused]] const bool force) {

#if HAS_EXT_MCP2518
  byte enumeration_responses[16], rcanid;
  unsigned long enumeration_start_time = millis();
  can_msg_wrapper_t wmsg;

  VLOG("canmcp2518/%u: enumerate_bus: current id = %u, force = %u", num, canid, force);

  if (enabled) {
    if (canid < 1 || canid > 99 || force) {

      VLOG("canmcp2518/%u: enumerating bus", num);

      /// send enumeration request message

      wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;

      wmsg.msg.id |= CAN2040_ID_RTR;
      wmsg.msg.dlc = 0;

      wmsg.timestamp = micros();
      wmsg.retries = 0;

      forward_message(&wmsg, false);
      VLOG("canmcp2518/%u: sent enumeration request message", num);

      /// read enum responses for 100mS

      VLOG("can/%u: receiving responses", num);

      while ((millis() - enumeration_start_time) <= BUS_ENUMERATION_WINDOW) {

        if (queue_try_remove(ciq, &wmsg)) {  // *** use receive method !! **
          if (wmsg.msg.dlc == 0 && !(wmsg.msg.id & CAN2040_ID_RTR)) {

            rcanid = wmsg.msg.id & 0x7f;
            VLOG("enumeration response from id = %u", rcanid);

            if (rcanid > 0) {
              bitWrite(enumeration_responses[rcanid / 16], rcanid % 8, 1);
            }
          }
        }
      }

      /// take first unused can id

      VLOG("canmcp2518/%u: processing responses", num);

      for (byte i = 0; i < 16; i++) {
        for (byte b = 0; b < 8; b++) {

          if (i == 0 && b == 0) {
            continue;
          }

          if (!bitRead(enumeration_responses[i], b)) {
            canid = (i * 8) + b;
            goto enum_done;
          }
        }
      }

enum_done:

      /// store it

      EEPROM[14] = canid;
      EEPROM.commit();
      VLOG("canmcp2518/%u: canid is now %u", num, canid);

    } else {
      VLOG("canmcp2518/%u: not enumerating - using previously stored can id = %u", num, canid);
    }
  } else {
    VLOG("canmcp2518/%u: not enumerating - not enabled");
  }
#endif

  return;
}

void canmcp2518::respond_to_enumeration_request(void) {

  can_msg_wrapper_t wmsg;

  VLOG("canmcp2518/%u: responding to enumeration request", num);

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 0;

  wmsg.timestamp = micros();
  wmsg.retries = 0;

  forward_message(&wmsg, false);

  return;
}

void canmcp2518::discover_nodes(void) {

#if HAS_EXT_MCP2518
  VLOG("canmcp2518/%u: discover nodes", num);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 1;
  wmsg.msg.data[0] = OPC_QNN;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);
#endif

  return;
}

void canmcp2518::query_node(const uint16_t node_number) {

  VLOG("canmcp2518/%u: query node %u", num, node_number);

  can_msg_wrapper_t wmsg;

  wmsg.msg.id = canid + CBUS_MEDIUM_PRIORITY_BITS;
  wmsg.msg.dlc = 3;
  wmsg.msg.data[0] = OPC_NERD;
  wmsg.msg.data[1] = node_number >> 8;
  wmsg.msg.data[2] = node_number & 0x7f;

  wmsg.retries = 0;
  wmsg.timestamp = micros();

  forward_message(&wmsg, true);
  return;
}

byte canmcp2518::get_status(void) {

  // library doc p.42
  return INTERFACE_STATUS_OK;
}

///
