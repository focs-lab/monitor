#include "Monitor.h"

#include <future>
#include <stdlib.h>
#include <thread>

namespace Monitor {
Monitor::Monitor(int pid) : stopped(false), num_events(0), shm(new SharedMemory(pid)) {
  TraceId trace_ids[kTracesPerCollector];
  collectors.reserve(kNumCollectors);
  for (int i = 0; i < kNumCollectors; ++i) {
    for (int j = 0; j < kTracesPerCollector; ++j) {
      trace_ids[j] = j * kTracesPerWorker + i;
    }
    collectors.emplace_back(shm, trace_ids, kTracesPerCollector);
  }

  ingestors.reserve(kNumIngestors);
  for (int i = 0; i < kNumIngestors; ++i) {
    ingestors.emplace_back();
  }
}

Monitor::~Monitor() {
  delete shm;
}

void Monitor::Start() {
  std::thread collector_threads[kNumCollectors];
  std::thread ingestor_threads[kNumCollectors];

   // Spawn threads
  for (int i = 0; i < kNumCollectors; i++) {
    collector_threads[i] = std::thread([this, i] { collectors[i].Run(); });
  }

  for (int i = 0; i < kNumIngestors; i++) {
    ingestor_threads[i] = std::thread([this, i] {
      while (!stopped) {
        Chunk& chunk = collectors[i].Take();
        // TODO: Need to design it such that the chunk boundary is transparent to the ingestor
      }

      // TODO: Create a promise for returning the number of events processed
    });
  }

  // Wait for all threads to complete
  for (int i = 0; i < kNumCollectors; i++) {
    collector_threads[i].join();
  }
  for (int i = 0; i < kNumIngestors; i++) {
    ingestor_threads[i].join();
  }

  // TODO: Resolve promises to tally number of events processed
}

void Monitor::worker(int wid) {
  u64 slot_counts[SLOTS_PER_WORKER];

  // Open shared memory for each slot
  for (u8 i = 0; i < SLOTS_PER_WORKER; ++i) {
    Sid sid = i*NUM_WORKERS+wid;
    shm->Open(sid);
    slot_counts[i] = 0;
  }

  // Tell the program it can start.
  if (wid == 0) shm->Ready();

  // Dequeue loop
  while (!stop.load()) {
    // for peak performance, we might want to have SLOTS_PER_WORKER=1
    // but maybe the computer doesn't have that many cpus, then we don't want to spawn too many threads
    for (u8 slot_i = 0; slot_i < SLOTS_PER_WORKER; ++slot_i) {
      Sid sid = slot_i*NUM_WORKERS+wid;
      if (!shm->IsOpened(sid)) continue;
      shm->ConsumeChunk(sid);
      bool should_stop = shm->ProcessChunk(sid, [this, &slot_counts, slot_i](Event event) {
        if (HasProgramEnded(event)) return true;
        ingestor->handle_event(event);
        slot_counts[slot_i]++;
        return false;
      });
      if (should_stop) stop = true;
    }
  }

  // Tally number of events
  u64 total_count = 0;
  for (u8 i = 0; i < SLOTS_PER_WORKER; ++i) {
    total_count += slot_counts[i];
  }
  num_events.fetch_add(total_count);

  // Cleanup
  for (Sid sid = 0; sid < SLOTS_PER_WORKER; ++sid) {
    shm->Close(sid);
  }
}

}   // namespace Monitor
