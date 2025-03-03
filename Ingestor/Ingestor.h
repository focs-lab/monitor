#ifndef MONITOR_INGESTOR_H
#define MONITOR_INGESTOR_H

#include "Event.h"

namespace Monitor {

class Ingestor {
public:
  int handle_event(Event);
};

}   // namespace Monitor

#endif
