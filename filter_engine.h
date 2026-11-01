
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

#include "defs.h"
#include "enums.h"
#include "types.h"
#include "funcs.h"
#include "discoverer.h"

/// abstract base class, must derive

class abstract_filter_engine {
public:

  virtual bool begin(void *argptr = nullptr, void *argptr2 = nullptr) = 0;
  virtual byte process_message(const byte source, struct can2040_msg *msg, filter_context_t *context, interface_list_t *destination_interfaces) = 0;
  virtual void process_command(const byte r) = 0;

  ruleset_t *ruleset;

private:
};

/// pass-all engine

class pass_all_filter_engine final : public abstract_filter_engine {
public:
  pass_all_filter_engine();
  ~pass_all_filter_engine();

  bool begin(void *argptr = nullptr, void *argptr2 = nullptr) override;
  byte process_message(const byte source, struct can2040_msg *msg, filter_context_t *context, interface_list_t *destination_interfaces) override;
  void process_command(const byte r) override;

private:
};

/// simple, hardcoded engine: rewrites can ids, drops enumeration traffic

class can_filter_engine final : public abstract_filter_engine {
public:
  can_filter_engine();
  ~can_filter_engine();

  bool begin(void *argptr = nullptr, void *argptr2 = nullptr) override;
  byte process_message(const byte source, struct can2040_msg *msg, filter_context_t *context, interface_list_t *destination_interfaces) override;
  void process_command(const byte r) override;

private:
};

/// configurable rules engine, using rules file

class configurable_filter_engine final : public abstract_filter_engine {
public:
  configurable_filter_engine();
  ~configurable_filter_engine();

  bool begin(void *argptr = nullptr, void *argptr2 = nullptr) override;
  byte process_message(const byte source, struct can2040_msg *msg, filter_context_t *context, interface_list_t *destination_interfaces) override;
  void process_command(const byte r) override;

private:
  byte rules_file_version;
};

/// inference-based filter engine based on a dynamically-discovered map database

class dynamic_filter_engine final : public abstract_filter_engine {
public:
  dynamic_filter_engine();
  ~dynamic_filter_engine();

  bool begin(void *argptr = nullptr, void *argptr2 = nullptr) override;
  byte process_message(const byte source, struct can2040_msg *msg, filter_context_t *context, interface_list_t *destination_interfaces) override;
  void process_command(const byte r) override;

private:
  can_bus_discoverer *discoverer;
};

/// hybrid filter engine uses dynamic mapping for events and rules for non-event messages

class hybrid_filter_engine final : public abstract_filter_engine {
public:
  hybrid_filter_engine();
  ~hybrid_filter_engine();

  bool begin(void *argptr = nullptr, void *argptr2 = nullptr) override;
  byte process_message(const byte source, struct can2040_msg *msg, filter_context_t *context, interface_list_t *destination_interfaces) override;
  void process_command(const byte r) override;

  can_bus_discoverer *discoverer;

private:
  byte rules_file_version;
};

/// user-definable filter engine framework

class user_filter_engine final : public abstract_filter_engine {
public:
  user_filter_engine();
  ~user_filter_engine();

  bool begin(void *argptr = nullptr, void *argptr2 = nullptr) override;
  byte process_message(const byte source, struct can2040_msg *msg, filter_context_t *context, interface_list_t *destination_interfaces) override;
  void process_command(const byte r) override;

private:
  can_bus_discoverer *discoverer;
};

///
