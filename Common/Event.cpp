#include "Event.h"

namespace Monitor {
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

bool HasProgramEnded(Event ev) { return ev.raw == kEvProgramEnded.raw; }

}   // namespace Monitor
