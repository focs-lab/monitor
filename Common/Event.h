#ifndef MONITOR_EVENT_H
#define MONITOR_EVENT_H

#include <atomic>
#include <cstring>
#include <optional>

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

const char* eventtype_to_string(EventType evt);
bool HasProgramEnded(Event ev);

class Chunk {
public:
  static constexpr u32 kChunkSize = kCacheLineSize * 128;
  static constexpr u32 kChunkNumEvents = kChunkSize / sizeof(Event);

  // Only for preallocation
  Chunk() {}

  Chunk(TraceId trace_id, AEvent* buffer, int idx)
    : trace_id(trace_id) {
    for (int i = 0; i < kChunkNumEvents; ++i)
      events[i] = buffer[idx + i];
  }

  inline std::optional<Event> next() {
    if (cursor >= kChunkNumEvents) return std::nullopt;
    return std::optional(events[cursor++]);
  }

private:
  Event events[kChunkNumEvents];
  TraceId trace_id;
  int cursor;
};

}   // namespace Monitor

#endif