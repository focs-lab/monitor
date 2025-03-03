#ifndef MONITOR_EVENT_H
#define MONITOR_EVENT_H

#include <atomic>

#include "Types.h"

namespace Monitor {

enum EventType : u8 {
  Clear = 0,    // cannot use 0 for events, this is used to mark a cleared entry in the channel

  Read = 1,
  Write = 2,

  VptrUpdate = 3,
  VptrLoad = 4,

  Memset = 5,
  Memcpy = 6,

  AtomicLoad = 7,
  AtomicStore = 8,
  AtomicRMW = 9,
  AtomicCAS = 10,
  AtomicFence = 11,

  Return = 12,
  AtExit = 13,
  Ignore = 0xff
};

const char* eventtype_to_string(EventType evt) {
  switch (evt) {
  case Clear:
    return "CLEAR";
  case Read:
    return "READ";
  case Write:
    return "WRITE";
  case VptrUpdate:
    return "VPTRUPDATE";
  case VptrLoad:
    return "VPTRLOAD";
  case Memset:
    return "MEMSET";
  case Memcpy:
    return "MEMCPY";
  case AtomicLoad:
    return "ATOMICLOAD";
  case AtomicStore:
    return "ATOMICSTORE";
  case AtomicRMW:
    return "ATOMICRMW";
  case AtomicCAS:
    return "ATOMICCAS";
  case AtomicFence:
    return "ATOMICFENCE";
  case Return:
    return "RETURN";
  case AtExit:
    return "ATEXIT";

  default:
    return "UNKNOWN";
  }
}

typedef union {
  u64 raw;
  struct {      // remember little-endian byte order
    u64 addr : 48;
    u8 lap_num : 4;
    u8 _ : 4;
    EventType event_type : 8;
  };
} Event;

typedef std::atomic<Event> AEvent;

constexpr Event raw_event(u64 v) { return { .raw = v }; }

constexpr Event kEvClear = raw_event(0);
constexpr Event kEvMonitorReady = raw_event(0xcafebeef);
constexpr Event kEvProgramEnded = raw_event(0xdeaddead);

bool HasProgramEnded(Event ev) { return ev.raw == kEvProgramEnded.raw; }

}   // namespace Monitor

#endif