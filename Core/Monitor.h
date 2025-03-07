#ifndef MONITOR_MONITOR_H
#define MONITOR_MONITOR_H

#include <atomic>
#include <pthread.h>
#include <vector>

#include "Core/SharedMemory.h"
#include "Collector/Collector.h"
#include "Ingestor/Ingestor.h"


namespace Monitor {
class Monitor {
public:
  Monitor(int pid);
  ~Monitor();
  void Start();
private:
  // static constexpr int kNumCollectors = kNumWorkers;
  // static constexpr int kNumIngestors = kNumWorkers;
  // static constexpr int kTracesPerCollector = kNumTraces / kNumCollectors;
  // static constexpr int kTracesPerIngestor = kNumTraces / kNumIngestors;
  // static_assert(kTracesPerCollector * kNumCollectors == kNumTraces);
  // static_assert(kTracesPerIngestor * kNumIngestors == kNumTraces);

  SharedMemory *shm;
  std::vector<Collector> collectors;
  std::vector<Ingestor> ingestors;
  std::atomic_bool stopped;
  void worker(int wid);
  std::atomic_uint64_t num_events;
};
}   // namespace Monitor

#endif