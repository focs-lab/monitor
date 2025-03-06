#include "Collector.h"


namespace Monitor {
Collector::Collector(SharedMemory& shm, TraceId* trace_ids, int num_traces) :
  shm(shm), num_traces(num_traces), stopped(false), cursor(0) {
  for (int i = 0; i < num_traces; ++i) this->trace_ids[i] = trace_ids[i];
  for (int i = 0; i < kMaxChunksInMem; ++i) available[i] = false;
}

void Collector::Run() {
  int trace_idx = 0, chunk_idx = 0;
  while (!stopped) {
    bool success = shm.MaybeConsumeChunk(trace_ids[trace_idx], &chunks[chunk_idx]);
    trace_idx = (trace_idx + 1) % kTracesPerWorker;
    if (!success) return;
    available[chunk_idx] = true;
    chunk_idx = (chunk_idx + 1) % kMaxChunksInMem;

    // We have consumed kMaxChunksInMem number of chunks. Cooldown.
    if (chunk_idx == 0) while (!available[kMaxChunksInMem-1]);
  }

  for (int i = 0; i < num_traces; ++i) {
    shm.Close(trace_ids[i]);
  }
}

Chunk& Collector::Take() {
  while (!available[cursor]);
  cursor = (cursor + 1) % kMaxChunksInMem;
  return chunks[cursor];
}

void Collector::Stop() {
  stopped = true;
}

}   // namespace Monitor
