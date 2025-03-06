#ifndef MONITOR_CONSTANTS_H
#define MONITOR_CONSTANTS_H

#include "Types.h"
#include "Event.h"

namespace Monitor {

constexpr u32 kNumTraces = 256;
constexpr u32 kNumWorkers = 8;
constexpr u32 kTracesPerWorker = kNumTraces / kNumWorkers;
static_assert(kNumSlots == kNumWorkers * kTracesPerWorker);

constexpr u32 kSpinCount = 0;
constexpr u32 kBufferNumEvents = 0x1000;
constexpr u32 kBufferIdxMask = 0xfff;
constexpr u32 kBufferSize = kBufferNumEvents * sizeof(Event);

constexpr u32 kCacheLineSize = 64;

}   // namespace Monitor

#endif
