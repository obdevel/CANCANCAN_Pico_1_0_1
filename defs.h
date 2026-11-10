
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

#include <SPI.h>

///

#if defined(ARDUINO_WIZNET_5500_EVB_PICO) || defined(ARDUINO_WIZNET_5500_EVB_PICO2) || defined(ARDUINO_RASPBERRY_PI_PICO_W) || defined(ARDUINO_RASPBERRY_PI_PICO_2W)
#define IS_NETWORK_DEVICE
#endif

#if defined(ARDUINO_WIZNET_5500_EVB_PICO) || defined(ARDUINO_WIZNET_5500_EVB_PICO2)
#define IS_ETHERNET_DEVICE
#endif

#if defined(ARDUINO_RASPBERRY_PI_PICO_W) || defined(ARDUINO_RASPBERRY_PI_PICO_2W)
#define IS_WIFI_DEVICE
#endif

/// pin designations for x4 PCB rev B

#define CAN_RX_PIN_0 14
#define CAN_TX_PIN_0 15

#define CAN_RX_PIN_1 1
#define CAN_TX_PIN_1 0

#define CAN_RX_PIN_2 27
#define CAN_TX_PIN_2 26

#define SLIM_LED_PIN 2
#define FLIM_LED_PIN 3

#define ACTIVITY_LED_PIN_0 4
#define ACTIVITY_LED_PIN_1 5
#define ACTIVITY_LED_PIN_2 6
#define ACTIVITY_LED_PIN_3 7

#define ERROR_LED_PIN 8

#define CBUS_SWITCH_PIN 9

#define CONFIG_RESET_PIN 21
#define USB_POWER_DETECT_PIN 24

/// SD card SPI interface

#define SD_MOSI_PIN 19
#define SD_MISO_PIN 16
#define SD_SCK_PIN 18
#define SD_CS_PIN 17
#define SD_SPI SPI

/// WizNet ethernet SPI interface

#define ETHERNET_MOSI_PIN 19
#define ETHERNET_MISO_PIN 16
#define ETHERNET_SCK_PIN 18
#define ETHERNET_CS_PIN 17
#define ETHERNET_INT_PIN 21

/// MCP2518 and external MCP2515 SPI interfaces

#define HAS_CAN_FD 0
#define CAN_FD_BUS_FREQUENCY (1000 * 1000)
#define CAN_FD_RECEIVE_FIFO_SIZE 128

#define MCP2518_MOSI_PIN 11
#define MCP2518_MISO_PIN 12
#define MCP2518_SCK_PIN 10
#define MCP2518_CS_PIN 13
#define MCP2518_INT_PIN 22
#define MCP2518_SPI_INSTANCE SPI1

#define HAS_EXT_MCP2515 0
#define CAN_EXT_2515_BUS_FREQUENCY (125 * 1000)
#define CAN_EXT_2515_BACKBONE_BUS_FREQUENCY (500 * 1000)
#define CAN_EXT_2515_XTAL_FREQUENCY (16 * 1000 * 1000)
#define CAN_EXT_2515_RECEIVE_FIFO_SIZE 128
#define CAN_EXT_2515_TRANSMIT_FIFO_SIZE 128

#define MCP2515_MOSI_PIN 11
#define MCP2515_MISO_PIN 12
#define MCP2515_SCK_PIN 10
#define MCP2515_CS_PIN 13
#define MCP2515_INT_PIN 22
#define MCP2515_SPI_INSTANCE SPI1

#define HAS_EXT_MCP2518 0
#define MCP2518_BUS_FREQUENCY (125 * 1000)
#define MCP2518_RECEIVE_FIFO_SIZE 128
#define MCP2518_TRANSMIT_FIFO_SIZE 128

///

#define VER_MAJ 1            // code major version
#define VER_MIN 'a'          // code minor version
#define VER_BETA 1           // code beta sub-version
#define MODULE_ID 240        // CBUS module type - change as required
#define SERIAL_TIMEOUT 5000  // wait for USB serial connection at startup

#define SERIAL_BAUDRATE 115200

#define CAN_BITRATE 125000
#define CAN_QUEUE_SIZE 1024
#define CAN_TX_RETRY_QUEUE_SIZE 128

#define CAN_BACKBONE_BITRATE 500000

#define MAX_INTERFACES 6
#define BUS_ENUMERATION_WINDOW 100

#define ENABLE_MICRO_TIMESTAMPS 0  // enable microsecond timestamps in debug output
#define VLOG_MAX_MSG_LEN 256

#define ENABLE_DEBUG 1  // enable debug information sent to serial port

#if ENABLE_DEBUG == 1
#define VLOG(...) vlog(__VA_ARGS__)
#else
#define VLOG(...) \
  {}
#endif

/// filter engine rulesets

#define RULESET_JSON_FILENAME "/ruleset.json"
#define RULESET_JSON_FILESPEC "/ruleset%1u.json"

#define MAX_RULESET_FILES 8
#define FILE_COPY_BUFFER_SIZE 256

#define MAX_RULE_ENTRIES 64
#define MAX_OPCODES_PER_RULE 32
#define MAX_PATTERN_LEN 32
#define MAX_ACTION_TEXT_LEN 32
#define NUM_ACTIONS 2
#define NUM_OPCODES 135
#define MAX_CLASSES_PER_RULE 16

#define NUM_FILTER_ENGINES 6

/// network

#define GC_BUFFER_SIZE 64
#define GC_QUEUE_SIZE 32
#define NET_QUEUE_SIZE 256

#define SERIAL_BUFFER_SIZE 64
#define NET_BUFFER_SIZE 128
#define WORK_BUFFER_SIZE 64

#define MAX_CLIENTS 8
#define WIFI_RECONNECT_INTERVAL 10000
#define BRIDGE_RECONNECT_INTERVAL 5000

/// discoverer

#define MAX_CAN_NODES 300
#define MAX_CONSUMER_EVENTS 1024
#define BUS_QUERY_DELAY_MS 2000

/// for fnmatch() globbing

#define FNMATCH_FLAGS 0
#define FNMATCH_OK 0

#define EOS '\0'

#define RANGE_MATCH 1
#define RANGE_NOMATCH 0
#define RANGE_ERROR (-1)

#define FNM_NOMATCH 1 /* Match failed. */

#define FNM_NOESCAPE 0x01 /* Disable backslash escaping. */
#define FNM_PATHNAME 0x02 /* Slash must be matched by slash. */
#define FNM_PERIOD 0x04   /* Period must be matched by period. */

#define FNM_LEADING_DIR 0x08 /* Ignore /<tail> after Imatch. */
#define FNM_CASEFOLD 0x10    /* Case insensitive search. */
#define FNM_IGNORECASE FNM_CASEFOLD
#define FNM_FILE_NAME FNM_PATHNAME

#define GLOB_METACHARS "*?[]^!"

/// network config

#define NETCFG_EEPROM_SAVE_OFFSET 1024
#define NETCFG_CHECK_VALUE 99

#define DEFAULT_HOSTNAME "cancancan"
#define DEFAULT_SERVICENAME "mergcbus"
#define DEFAULT_AP_SSID "cancancan"
#define DEFAULT_AP_PASSWORD "mergmergmerg"
#define DEFAULT_IP_PORT 5550
#define MDNS_QUERY_TIMEOUT 250

/// event opcode identifiers

#define EVENT_SET_MASK 0b10010000    ///< OPC bit mask to test for event.
#define EVENT_CLR_MASK 0b00000110    ///< OPC bit mask to test for event.
#define EVENT_ON_MASK 0b00000001     ///< OPC bit mask to test for ON events.
#define EVENT_SHORT_MASK 0b00001000  ///< OPC bit mask to test for short events.

/// CBUS priority bits

#define CBUS_HIGH_PRIORITY_BITS (0b0011 << 7)
#define CBUS_MEDIUM_PRIORITY_BITS (0b0111 << 7)
#define CBUS_LOW_PRIORITY_BITS (0b1011 << 7)

///
