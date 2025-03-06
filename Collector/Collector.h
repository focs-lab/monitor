#ifndef MONITOR_COLLECTOR_H
#define MONITOR_COLLECTOR_H

#include <atomic>
#include <vector>

#include "Common/Constants.h"
#include "Common/Types.h"
#include "Common/Event.h"
#include "SharedMemory.h"


namespace Monitor {
class Collector {
public:
  static constexpr int kMaxChunksInMem = 0x2000;
  Collector(SharedMemory& shm, TraceId* trace_ids, int num_traces);
  void Run();
  Chunk& Take();
  void Stop();
private:
  SharedMemory& shm;
  int cursor;   // only used by a single ingestor when it calls `Take`, so it is data-race-free
  int num_traces;
  std::atomic<bool> stopped;
  TraceId trace_ids[kNumTraces];
  Chunk chunks[kMaxChunksInMem];
  std::atomic<bool> available[kMaxChunksInMem];
};

}   // namespace Monitor

#endif