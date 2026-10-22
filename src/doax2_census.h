#pragma once
// Diagnostic-only guest call census (DOAX2_FUNC_CENSUS builds). Every
// recompiled guest function registers one counter on first entry and
// increments it on each call; doax2_60fps.cpp dumps per-window deltas.

#include <atomic>
#include <cstdint>

struct Doax2CensusSlot {
  std::atomic<uint64_t> calls{0};
  const char* name = nullptr;
};

Doax2CensusSlot* Doax2CensusRegister(const char* name);

#define REX_FUNC_PROLOGUE()                                              \
  static Doax2CensusSlot* const doax2_census_slot =                      \
      Doax2CensusRegister(__func__);                                     \
  doax2_census_slot->calls.fetch_add(1, std::memory_order_relaxed)
