#ifndef MONITOR_INGESTOR_H
#define MONITOR_INGESTOR_H

#include "Common/Event.h"
#include "Common/Types.h"

namespace Monitor {

class Ingestor {
  typedef IngestorEvent Event;
public:
  Ingestor(Collector& collector) : collector(collector) {}
  int handle_event(TraceId, Event&);
private:
  Collector &collector;
};

}   // namespace Monitor

#endif
