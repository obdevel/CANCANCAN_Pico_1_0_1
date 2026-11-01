
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

#include "cbusdefs.h"

// #define NUM_OPCODES 135

// typedef struct {
//   const char *opcode_name;
//   byte opcode;
//   byte message_class;
//   bool has_nn;
// } opcode_t;

// enum {
//   MESSAGE_CLASS_UNKNOWN = 0,
//   MESSAGE_CLASS_ALL,
//   MESSAGE_CLASS_OPCODE,
//   MESSAGE_CLASS_EVENT,
//   MESSAGE_CLASS_ENUM,
//   MESSAGE_CLASS_DISCOVERY,
//   MESSAGE_CLASS_CONFIG,
//   MESSAGE_CLASS_DCC,
//   MESSAGE_CLASS_REQ_RESP,
//   MESSAGE_CLASS_EXTENDED,
//   MESSAGE_CLASS_LONG_MESSAGE,
//   MESSAGE_CLASS_DATA_ONLY,
//   MESSAGE_CLASS_MISC
// };

/// opcode lookup table

opcode_t opcode_lookup_table[] = {

  { "OPC_ACK", OPC_ACK, MESSAGE_CLASS_MISC, false },
  { "OPC_NAK", OPC_NAK, MESSAGE_CLASS_MISC, false },
  { "OPC_HLT", OPC_HLT, MESSAGE_CLASS_MISC, false },
  { "OPC_BON", OPC_BON, MESSAGE_CLASS_MISC, false },
  { "OPC_TOF", OPC_TOF, MESSAGE_CLASS_DCC, false },
  { "OPC_TON", OPC_TON, MESSAGE_CLASS_DCC, false },
  { "OPC_ESTOP", OPC_ESTOP, MESSAGE_CLASS_DCC, false },
  { "OPC_ARST", OPC_ARST, 0, false },
  { "OPC_RTOF", OPC_RTOF, MESSAGE_CLASS_DCC, false },
  { "OPC_RTON", OPC_RTON, MESSAGE_CLASS_DCC, false },
  { "OPC_RESTP", OPC_RESTP, MESSAGE_CLASS_DCC, false },
  { "OPC_RSTAT", OPC_RSTAT, MESSAGE_CLASS_DCC, false },
  { "OPC_QNN", OPC_QNN, MESSAGE_CLASS_DISCOVERY, false },
  { "OPC_RQNP", OPC_RQNP, MESSAGE_CLASS_CONFIG, false },
  { "OPC_RQMN", OPC_RQMN, MESSAGE_CLASS_CONFIG, false },
  { "OPC_KLOC", OPC_KLOC, MESSAGE_CLASS_DCC, false },
  { "OPC_QLOC", OPC_QLOC, MESSAGE_CLASS_DCC, false },
  { "OPC_DKEEP", OPC_DKEEP, MESSAGE_CLASS_DCC, false },
  { "OPC_DBG1", 0x30, MESSAGE_CLASS_DEBUG, false },
  { "OPC_EXTC", 0x3F, MESSAGE_CLASS_EXTENDED, false },
  { "OPC_RLOC", 0x40, MESSAGE_CLASS_DCC, false },
  { "OPC_QCON", 0x41, MESSAGE_CLASS_DCC, false },
  { "OPC_SNN", 0x42, MESSAGE_CLASS_CONFIG, true },
  { "OPC_ALOC", 0X43, MESSAGE_CLASS_DCC, false },
  { "OPC_STMOD", 0x44, MESSAGE_CLASS_DCC, false },
  { "OPC_PCON", 0x45, MESSAGE_CLASS_DCC, false },
  { "OPC_KCON", 0x46, MESSAGE_CLASS_DCC, false },
  { "OPC_DSPD", 0x47, MESSAGE_CLASS_DCC, false },
  { "OPC_DFLG", 0x48, 0, false },
  { "OPC_DFNON", 0x49, MESSAGE_CLASS_DCC, false },
  { "OPC_DFNOF", 0x4A, MESSAGE_CLASS_DCC, false },
  { "OPC_SSTAT", 0x4C, MESSAGE_CLASS_DCC, false },
  { "OPC_NNRSM", 0x4F, MESSAGE_CLASS_CONFIG, true },
  { "OPC_RQNN", 0x50, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NNREL", 0x51, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NNACK", 0x52, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NNLRN", 0x53, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NNULN", 0x54, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NNCLR", 0x55, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NNEVN", 0x56, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NERD", 0x57, MESSAGE_CLASS_CONFIG, true },
  { "OPC_RQEVN", 0x58, MESSAGE_CLASS_CONFIG, true },
  { "OPC_WRACK", 0x59, MESSAGE_CLASS_CONFIG, true },
  { "OPC_RQDAT", 0x5A, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_RQDDS", 0x5B, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_BOOT", 0x5C, MESSAGE_CLASS_CONFIG, true },
  { "OPC_ENUM", 0x5D, MESSAGE_CLASS_ENUM, true },
  { "OPC_NNRST", 0x5E, MESSAGE_CLASS_CONFIG, true },
  { "OPC_EXTC1", 0x5F, MESSAGE_CLASS_DEBUG, false },
  { "OPC_DFUN", 0x60, MESSAGE_CLASS_DCC, false },
  { "OPC_GLOC", 0x61, MESSAGE_CLASS_DCC, false },
  { "OPC_ERR", 0x63, 0, false },
  { "OPC_CMDERR", 0x6F, MESSAGE_CLASS_CONFIG, true },
  { "OPC_EVNLF", 0x70, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NVRD", 0x71, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NENRD", 0x72, MESSAGE_CLASS_CONFIG, true },
  { "OPC_RQNPN", 0x73, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NUMEV", 0x74, MESSAGE_CLASS_CONFIG, true },
  { "OPC_CANID", 0x75, MESSAGE_CLASS_CONFIG, true },
  { "OPC_EXTC2", 0x7F, MESSAGE_CLASS_EXTENDED, false },
  { "OPC_RDCC3", 0x80, MESSAGE_CLASS_DCC, false },
  { "OPC_WCVO", 0x82, MESSAGE_CLASS_DCC, false },
  { "OPC_WCVB", 0x83, MESSAGE_CLASS_DCC, false },
  { "OPC_QCVS", 0x84, MESSAGE_CLASS_DCC, false },
  { "OPC_PCVS", 0x85, MESSAGE_CLASS_DCC, false },
  { "OPC_ACON", 0x90, MESSAGE_CLASS_EVENT, true },
  { "OPC_ACOF", 0x91, MESSAGE_CLASS_EVENT, true },
  { "OPC_AREQ", 0x92, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_ARON", 0x93, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_AROF", 0x94, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_EVULN", 0x95, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NVSET", 0x96, MESSAGE_CLASS_CONFIG, true },
  { "OPC_NVANS", 0x97, MESSAGE_CLASS_CONFIG, true },
  { "OPC_ASON", 0x98, MESSAGE_CLASS_EVENT, true },
  { "OPC_ASOF", 0x99, MESSAGE_CLASS_EVENT, true },
  { "OPC_ASRQ", 0x9A, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_PARAN", 0x9B, MESSAGE_CLASS_CONFIG, true },
  { "OPC_REVAL", 0x9C, MESSAGE_CLASS_CONFIG, true },
  { "OPC_ARSON", 0x9D, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_ARSOF", 0x9E, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_EXTC3", 0x9F, MESSAGE_CLASS_EXTENDED, false },
  { "OPC_RDCC4", 0xA0, MESSAGE_CLASS_DCC, false },
  { "OPC_WCVS", 0xA2, MESSAGE_CLASS_DCC, false },
  { "OPC_ACON1", 0xB0, MESSAGE_CLASS_EVENT, true },
  { "OPC_ACOF1", 0xB1, MESSAGE_CLASS_EVENT, true },
  { "OPC_REQEV", 0xB2, MESSAGE_CLASS_CONFIG, true },
  { "OPC_ARON1", 0xB3, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_AROF1", 0xB4, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_NEVAL", 0xB5, MESSAGE_CLASS_CONFIG, true },
  { "OPC_PNN", 0xB6, MESSAGE_CLASS_DISCOVERY, true },
  { "OPC_ASON1", 0xB8, MESSAGE_CLASS_EVENT, true },
  { "OPC_ASOF1", 0xB9, MESSAGE_CLASS_EVENT, true },
  { "OPC_ARSON1", 0xBD, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_ARSOF1", 0xBE, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_EXTC4", 0xBF, MESSAGE_CLASS_EXTENDED, false },
  { "OPC_RDCC5", 0xC0, MESSAGE_CLASS_DCC, false },
  { "OPC_WCVOA", 0xC1, MESSAGE_CLASS_DCC, false },
  { "OPC_CABDAT", 0xC2, MESSAGE_CLASS_DCC, false },
  { "OPC_FCLK", 0xCF, MESSAGE_CLASS_MISC, false },
  { "OPC_ACON2", 0xD0, MESSAGE_CLASS_EVENT, true },
  { "OPC_ACOF2", 0xD1, MESSAGE_CLASS_EVENT, true },
  { "OPC_EVLRN", 0xd2, MESSAGE_CLASS_CONFIG, true },
  { "OPC_EVANS", 0xd3, MESSAGE_CLASS_CONFIG, true },
  { "OPC_ARON2", 0xD4, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_AROF2", 0xD5, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_ASON2", 0xD8, MESSAGE_CLASS_EVENT, true },
  { "OPC_ASOF2", 0xD9, MESSAGE_CLASS_EVENT, true },
  { "OPC_ARSON2", 0xDD, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_ARSOF2", 0xDE, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_EXTC5", 0xDF, MESSAGE_CLASS_EXTENDED, false },
  { "OPC_RDCC6", 0xE0, MESSAGE_CLASS_DCC, false },
  { "OPC_PLOC", 0xE1, MESSAGE_CLASS_DCC, false },
  { "OPC_NAME", 0xE2, MESSAGE_CLASS_CONFIG, false },
  { "OPC_STAT", 0xE3, MESSAGE_CLASS_CONFIG, false },
  { "OPC_PARAMS", 0xEF, MESSAGE_CLASS_CONFIG, false },
  { "OPC_ACON3", 0xF0, MESSAGE_CLASS_EVENT, true },
  { "OPC_ACOF3", 0xF1, MESSAGE_CLASS_EVENT, true },
  { "OPC_ENRSP", 0xF2, MESSAGE_CLASS_CONFIG, true },
  { "OPC_ARON3", 0xF3, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_AROF3", 0xF4, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_EVLRNI", 0xF5, MESSAGE_CLASS_CONFIG, true },
  { "OPC_ACDAT", 0xF6, MESSAGE_CLASS_EVENT, true },
  { "OPC_ARDAT", 0xF7, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_ASON3", 0xF8, MESSAGE_CLASS_EVENT, true },
  { "OPC_ASOF3", 0xF9, MESSAGE_CLASS_EVENT, true },
  { "OPC_DDES", 0xFA, MESSAGE_CLASS_DATA_ONLY, true },
  { "OPC_DDRS", 0xFB, MESSAGE_CLASS_DATA_ONLY, true },
  { "OPC_DDWS", 0xFC, MESSAGE_CLASS_DATA_ONLY, true },
  { "OPC_ARSON3", 0xFD, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_ARSOF3", 0xFE, MESSAGE_CLASS_REQ_RESP, true },
  { "OPC_EXTC6", 0xFF, MESSAGE_CLASS_EXTENDED, false },
  { "OPC_VCVS", 0xA4, 0, false },
  { "OPC_DTXC", 0xE9, MESSAGE_CLASS_LONG_MESSAGE, false },
  { "OPC_LM", 0xEA, MESSAGE_CLASS_LONG_MESSAGE, false },
  { "OPC_MODE", 0x76, MESSAGE_CLASS_CONFIG, false }
};

///
