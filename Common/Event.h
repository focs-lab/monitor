#ifndef MONITOR_EVENT_H
#define MONITOR_EVENT_H

#include <atomic>
#include <cassert>
#include <cstring>
#include <optional>

#include <utility>
#include "Types.h"

namespace Monitor {

enum EventType : u8 {
  CLEAR = 0,    // cannot use 0 for events, this is used to mark a cleared entry in the channel

  READ = 1,
  WRITE = 2,

// These are basically loads and stores
//  VPTRUPDATE = 3,
//  VPTRLOAD = 4,

  MEMSET = 5,
  MEMCPY = 6,

  ATOMICLOAD = 7,
  ATOMICSTORE = 8,
  ATOMICRMW = 9,
  ATOMICCAS = 10,
  ATOMICFENCE = 11,

  RETURN = 12,
  ATEXIT = 13,

  ACQUIRE = 14,
  RELEASE = 15,

  IGNOREBEGIN = 0xfe,
  IGNOREEND = 0xff
};


typedef union {
  u64 raw;
  struct {      // remember little-endian byte order
    u64 addr : 48;
    u8 lap_num : 4;
    u8 _ : 4;
    EventType event_type : 8;
  };
} LoggedEvent;

typedef std::atomic<LoggedEvent> AMEvent;

constexpr LoggedEvent RawEvent(u64 v) { return { .raw = v }; }

constexpr LoggedEvent kEvClear = RawEvent(0);
constexpr LoggedEvent kEvMonitorReady = RawEvent(0xcafebeef);
constexpr LoggedEvent kEvProgramEnded = RawEvent(0xdeaddead);

const char* eventtype_to_string(EventType evt);
bool HasProgramEnded(LoggedEvent ev);

/** Signatures for events.  */
enum LoggedEventArgType {
  ADDRESS,
  READVALUE,
  WRITEVALUE,
  LOCKCOUNTER,
  DEST,
  SOURCE,
  COUNT,
};

typedef struct LoggedEventSig {
public:
  EventType type;
  LoggedEventArgType* args;
} LoggedEventSig;

// One could do some metaprogramming to make all these computed in compile time and with less code duplication.
constexpr LoggedEventSig kReadEventSig = { .type=READ, .args=(LoggedEventArgType[]){ ADDRESS, READVALUE }};
constexpr LoggedEventSig kWriteEventSig = { .type=WRITE, .args=(LoggedEventArgType[]){ ADDRESS, WRITEVALUE }};
constexpr LoggedEventSig kAtomicLoadSig = { .type=ATOMICLOAD, .args=(LoggedEventArgType[]){ ADDRESS, LOCKCOUNTER, READVALUE }};
constexpr LoggedEventSig kAtomicStoreSig = { .type=ATOMICSTORE, .args=(LoggedEventArgType[]){ ADDRESS, LOCKCOUNTER, WRITEVALUE }};
constexpr LoggedEventSig kAtomicRMWSig = { .type=ATOMICRMW, .args=(LoggedEventArgType[]){ ADDRESS, LOCKCOUNTER, READVALUE, WRITEVALUE }};
constexpr LoggedEventSig kAtomicCASSig = { .type=ATOMICCAS, .args=(LoggedEventArgType[]){ ADDRESS, LOCKCOUNTER, READVALUE, WRITEVALUE }};
constexpr LoggedEventSig kReturnSig = { .type=RETURN, .args=(LoggedEventArgType[]){ ADDRESS }};
constexpr LoggedEventSig kAtExitSig = { .type=ATEXIT, .args=(LoggedEventArgType[]){ }};
constexpr LoggedEventSig kIgnoreBeginSig = { .type=IGNOREBEGIN, .args=(LoggedEventArgType[]){ }};
constexpr LoggedEventSig kIgnoreEndSig = { .type=IGNOREEND, .args=(LoggedEventArgType[]){ }};
constexpr LoggedEventSig kAcquireSig = { .type=ACQUIRE, .args=(LoggedEventArgType[]){ ADDRESS, LOCKCOUNTER }};
constexpr LoggedEventSig kReleaseSig = { .type=RELEASE, .args=(LoggedEventArgType[]){ ADDRESS, LOCKCOUNTER }};
constexpr LoggedEventSig kMemsetSig = { .type=MEMSET, .args=(LoggedEventArgType[]){ DEST, SOURCE, COUNT  }};
constexpr LoggedEventSig kMemcpySig = { .type=MEMCPY, .args=(LoggedEventArgType[]){ DEST, SOURCE, COUNT  }};

int EventNumArgs(EventType evt) {
  switch (evt) {
  case CLEAR:
  case ATEXIT:
  case IGNOREBEGIN:
  case IGNOREEND:
  case ATOMICFENCE:
    return 0;
  case RETURN:
    return 1;
  case READ:
  case WRITE:
  case ACQUIRE:
  case RELEASE:
    return 2;
  case ATOMICLOAD:
  case ATOMICSTORE:
  case MEMSET:
  case MEMCPY:
    return 3;
  case ATOMICRMW:
  case ATOMICCAS:
    return 4;
  default:
    __builtin_unreachable();
  }
}

constexpr int kEventMaxArgs = 4;

class Chunk {
public:
  static constexpr u32 kChunkSize = kCacheLineSize * 128;
  static constexpr u32 kChunkNumEvents = kChunkSize / sizeof(LoggedEvent);

  // Only for preallocation
  Chunk() {}

  Chunk(TraceId trace_id, AMEvent* buffer, int idx)
    : trace_id_(trace_id) {
    for (int i = 0; i < kChunkNumEvents; ++i)
      events_[i] = buffer[idx + i];
  }

  inline std::optional<LoggedEvent> Next() {
    if (cursor_ >= kChunkNumEvents) return std::nullopt;
    return std::optional(events_[cursor_++]);
  }

  TraceId GetTraceId() { return trace_id_; }

private:
  LoggedEvent events_[kChunkNumEvents];
  TraceId trace_id_;
  int cursor_;
};

/** Event classes that are given to the client. They shouldn't need to think about things like
 *  memory layout or lap numbers.
 */
typedef struct IngestorEvent {
  EventType type;

  // We use union to represent the arguments for the special cases memset and memcpy.
  union {
    std::optional<u64> addr;
    std::optional<u64> dest;
  };

  // Why distinguish between read_value and write_value? Mainly it is just for compound atomic operations
  // like RMW and CAS.
  union {
    std::optional<u64> read_value;
    std::optional<u64> source;
  };
  union {
    std::optional<u64> write_value;
    std::optional<u64> count;
  };

  // TODO: Maybe this shouldn't be exposed to the client
  // std::optional<u64> lock_counter;

  // Note: There is a performance cost in using optional. But we'll take that for now. Usability by the client is more important.
  // Principally, we just want to enforce syntactical restrictions for the client to not load the fields when they are not meant to do so.
  // Something like pattern matching.
} IngestorEvent;

// This function doesn't check the size of the array. Currently it is the responsibility
// of the caller to make sure the right number of arguments are passed in.
// Also, ideally we generated this using some metaprogramming from the sigs defined above.
// Declaring like this is error prone especially when we decide to change the format one day.
IngestorEvent MakeIngestorEvent(LoggedEvent* event_and_args) {
  LoggedEvent event = event_and_args[0];
  EventType type = event.event_type;
  switch (type) {
  case READ:
    return { .type = READ,
             .addr = event_and_args[1].raw,
             .read_value = event_and_args[2].raw,
             .write_value = std::nullopt};
  case WRITE:
    return { .type = WRITE,
             .addr = event_and_args[1].raw,
             .read_value = std::nullopt,
             .write_value = event_and_args[2].raw};
  case MEMSET:
    return { .type = MEMSET,
             .dest = event_and_args[1].raw,
             .source = event_and_args[2].raw,
             .count = event_and_args[3].raw};
  case MEMCPY:
    return { .type = MEMCPY,
             .dest = event_and_args[1].raw,
             .source = event_and_args[2].raw,
             .count = event_and_args[3].raw};
  case ATOMICLOAD:
    return { .type = ATOMICLOAD,
             .addr = event_and_args[1].raw,
             .read_value = event_and_args[3].raw};
  case ATOMICSTORE:
    return { .type = ATOMICSTORE,
             .addr = event_and_args[1].raw,
             .read_value = std::nullopt,
             .write_value = event_and_args[3].raw};
  case ATOMICRMW:
    return { .type = ATOMICRMW,
             .addr = event_and_args[1].raw,
             .read_value = event_and_args[3].raw,
             .write_value = event_and_args[4].raw};
  case ATOMICCAS:
    return { .type = ATOMICCAS,
             .addr = event_and_args[1].raw,
             .read_value = event_and_args[3].raw,
             .write_value = event_and_args[4].raw};
  case ATOMICFENCE:
    return { .type = ATOMICFENCE,
             .addr = std::nullopt,
             .read_value = std::nullopt,
             .write_value = std::nullopt};
  case RETURN:
    return { .type = RETURN,
             .addr = event_and_args[1].raw,
             .read_value = std::nullopt,
             .write_value = std::nullopt};
  case ATEXIT:
    return { .type = ATEXIT,
             .addr = std::nullopt,
             .read_value = std::nullopt,
             .write_value = std::nullopt};
  case CLEAR:
    return { .type = CLEAR,
             .addr = std::nullopt,
             .read_value = std::nullopt,
             .write_value = std::nullopt};
  case IGNOREBEGIN:
    return { .type = IGNOREBEGIN,
             .addr = std::nullopt,
             .read_value = std::nullopt,
             .write_value = std::nullopt};
  case IGNOREEND:
    return { .type = IGNOREEND,
             .addr = std::nullopt,
             .read_value = std::nullopt,
             .write_value = std::nullopt};
  case ACQUIRE:
    return { .type = ACQUIRE,
             .addr = event_and_args[1].raw,
             .read_value = std::nullopt,
             .write_value = std::nullopt};
  case RELEASE:
    return { .type = RELEASE,
             .addr = event_and_args[1].raw,
             .read_value = std::nullopt,
             .write_value = std::nullopt};
  default:
    assert(false && "Forgot to implement for some event type.");
 }
}

}   // namespace Monitor

#endif