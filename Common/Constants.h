#ifndef MONITOR_CONSTANTS_H
#define MONITOR_CONSTANTS_H

#include "Types.h"

namespace Monitor {

constexpr u32 NUM_SLOTS = 256;
constexpr u32 NUM_WORKERS = 8;
constexpr u32 SLOTS_PER_WORKER = NUM_SLOTS / NUM_WORKERS;
static_assert(NUM_SLOTS == NUM_WORKERS * SLOTS_PER_WORKER);

constexpr u32 SPIN_COUNT = 0;
constexpr u32 BUF_SIZE = 0x1000;
constexpr u32 SLOT_IDX_MASK = 0xfff;

}   // namespace Monitor

#endif
