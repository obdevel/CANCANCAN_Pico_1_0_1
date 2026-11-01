
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

///

enum {
  FILTER_ENGINE_UNKNOWN = 0,
  FILTER_ENGINE_PASS_ALL,
  FILTER_ENGINE_CAN,
  FILTER_ENGINE_CONFIGURABLE,
  FILTER_ENGINE_DYNAMIC,
  FILTER_ENGINE_HYBRID,
  FILTER_ENGINE_USER
};

//

enum {
  INTERFACE_STATUS_ERROR = 0,
  INTERFACE_STATUS_OK
};

//

enum {
  RULE_RESULT_UNKNOWN = 0,
  RULE_RESULT_DROP_MESSAGE,
  RULE_RESULT_NO_MATCH,
  RULE_RESULT_FORWARD_MESSAGE,
  RULE_RESULT_REWRITE_AND_FORWARD_MESSAGE
};

//

enum {
  RULES_FILE_TYPE_TEXT = 0,
  RULES_FILE_TYPE_JSON
};

//

enum {
  MESSAGE_CLASS_UNKNOWN = 0,
  MESSAGE_CLASS_ALL,
  MESSAGE_CLASS_OPCODE,
  MESSAGE_CLASS_EVENT,
  MESSAGE_CLASS_LONG_EVENT,
  MESSAGE_CLASS_SHORT_EVENT,
  MESSAGE_CLASS_ENUM,
  MESSAGE_CLASS_DISCOVERY,
  MESSAGE_CLASS_CONFIG,
  MESSAGE_CLASS_DCC,
  MESSAGE_CLASS_REQ_RESP,
  MESSAGE_CLASS_EXTENDED,
  MESSAGE_CLASS_LONG_MESSAGE,
  MESSAGE_CLASS_DATA_ONLY,
  MESSAGE_CLASS_MISC,
  MESSAGE_CLASS_DEBUG
};

//

enum {
  ACTION_UNKNOWN = 0,
  ACTION_DROP,       // drop the message
  ACTION_FORWARD,    // forward unchanged
  ACTION_REWRITE,    // forward with can id rewritten
  ACTION_INCREMENT,  // increment NN or EN part by given value
  ACTION_DECREMENT,  // decrement NN or EN part by given value
  ACTION_EQUALS,     // set NN or EN part to given value
  ACTION_ERROR
};

//

enum {
  DEFAULT_ACION_DROP,
  DEFAULT_ACION_FORWARD
};

//

enum {
  EVENT_PART_NN,
  EVENT_PART_EN
};

//

enum {
  PATTERN_TYPE_NONE = 0,
  PATTERN_TYPE_OPCODE_LIST,
  PATTERN_TYPE_EVENT
};

//

enum {
  INTERFACE_TYPE_NOT_CONFIGURED = 0,
  INTERFACE_TYPE_PRIMARY_CBUS,
  INTERFACE_TYPE_CAN_1,
  INTERFACE_TYPE_CAN_2,
  INTERFACE_TYPE_CAN_BACKBONE_1,
  INTERFACE_TYPE_CAN_BACKBONE_2,
  INTERFACE_TYPE_CAN_FD_BACKBONE,
  INTERFACE_TYPE_EXT_MCP2515_CBUS,
  INTERFACE_TYPE_EXT_MCP2515_BACKBONE,
  INTERFACE_TYPE_MCP2518_CBUS,
  INTERFACE_TYPE_WIFI_TCP_SERVER,
  INTERFACE_TYPE_WIFI_TCP_CLIENT,
  INTERFACE_TYPE_WIFI_UDP_NODE,
  INTERFACE_TYPE_ETHERNET_TCP_SERVER,
  INTERFACE_TYPE_ETHERNET_TCP_CLIENT,
  INTERFACE_TYPE_ETHERNET_UDP_NODE,
  INTERFACE_TYPE_GRIDCONNECT_USB,
  INTERFACE_TYPE_GRIDCONNECT_SERIAL_1,
  INTERFACE_TYPE_GRIDCONNECT_SERIAL_2,
  INTERFACE_TYPE_SPE_NODE,
};

//

enum {
  NETWORK_STATE_DISCONNECTED,
  NETWORK_STATE_CONNECTING,
  NETWORK_STATE_CONNECTED
};

enum {
  SLOT_IDLE,
  SLOT_CONNECTED,
  SLOT_ERROR
};

enum {
  WIFI_MODE_AP = 0,
  WIFI_MODE_STA = 1
};

enum {
  FROM_SERIAL = 250,
  FROM_REMOTE = 251,
  FROM_EXT_CAN_BUS = 252,
  FROM_CBUS_INT = 253,
  FROM_ROUTER = 254
};

///
