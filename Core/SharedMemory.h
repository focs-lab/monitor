#ifndef MONITOR_SHAREDMEMORY_H
#define MONITOR_SHAREDMEMORY_H

#include <atomic>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <functional>

#include <unistd.h>

#include "Constants.h"
#include "Event.h"


namespace Monitor {

class SharedMemory {
public:
  SharedMemory() = delete;
  SharedMemory(int pid) : pid(pid) {
    for (int i = 0; i < kTracesPerWorker; ++i) {
      fds[i] = -1;
      idxs[i] = 0;
      is_open[i] = false;
      mems[i] = 0;
    }
  }

  ~SharedMemory();

  void Open(TraceId trace_id);
  // Avoid using this. Use ConsumeCheck instead.
  inline Event Consume(TraceId trace_id) {
    int idx = idxs[trace_id];
    AEvent* evp = &mems[trace_id][idx];
    idxs[trace_id] = (idx + 1) & kBufferIdxMask;
    return evp->load();
  }

  inline bool MaybeConsumeChunk(TraceId trace_id, Chunk* dest, int max_tries=8) {
    assert(is_open[trace_id]);

    AEvent* buf = mems[trace_id];
    int idx = idxs[trace_id];

    // Check that the next next chunk is already being written to
    int chunk_num = idx / Chunk::kChunkNumEvents;
    int next_idx = ((chunk_num + 2) * Chunk::kChunkNumEvents) % kBufferNumEvents;
    int tries = 0;
    while (buf[next_idx].load().raw == kEvClear.raw) {
      if (tries == max_tries) return false;
      tries++;
     }

    // Consume the whole chunk
    new (dest) Chunk(trace_id, buf, idx);

    // Clear just the first entry of the chunk
    buf[idx].store(kEvClear);
    idxs[trace_id] = (idx + Chunk::kChunkNumEvents) & kBufferIdxMask;
    return true;
  }

  void Ready() {
    assert(idxs[0] == 0);
    assert(mems[0] != nullptr);

    mems[0][0].store(kEvMonitorReady);
    idxs[0]++;
  }
  void Close(TraceId trace_id) {
    if (!is_open[trace_id]) return;
    mems[trace_id] = nullptr;
    close(fds[trace_id]);
    fds[trace_id] = 0;
    is_open[trace_id] = false;
  }
  bool IsOpened(TraceId trace_id) { return is_open[trace_id]; }

private:
  int pid;
  bool is_open[kNumTraces];
  AEvent* mems[kNumTraces];
  int fds[kNumTraces];
  int idxs[kNumTraces];
};

}   // namespace Monitor

#endif
