#include "Event.h"

namespace Monitor {
const char* eventtype_to_string(EventType evt) {
  switch (evt) {
  case CLEAR:
    return "CLEAR";
  case READ:
    return "READ";
  case WRITE:
    return "WRITE";
  case VPTRUPDATE:
    return "VPTRUPDATE";
  case VPTRLOAD:
    return "VPTRLOAD";
  case MEMSET:
    return "MEMSET";
  case MEMCPY:
    return "MEMCPY";
  case ATOMICLOAD:
    return "ATOMICLOAD";
  case ATOMICSTORE:
    return "ATOMICSTORE";
  case ATOMICRMW:
    return "ATOMICRMW";
  case ATOMICCAS:
    return "ATOMICCAS";
  case ATOMICFENCE:
    return "ATOMICFENCE";
  case RETURN:
    return "RETURN";
  case ATEXIT:
    return "ATEXIT";

  default:
    return "UNKNOWN";
  }
}

bool HasProgramEnded(LoggedEvent ev) { return ev.raw == kEvProgramEnded.raw; }

}   // namespace Monitor
