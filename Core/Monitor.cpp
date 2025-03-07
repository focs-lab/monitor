#include "Monitor.h"
#include "Common/Event.h"

#include <future>
#include <stdlib.h>
#include <thread>

namespace Monitor {
Monitor::Monitor(int pid) : stopped(false), num_events(0), shm(new SharedMemory(pid)) {
  TraceId trace_ids[kTracesPerWorker];
  collectors.reserve(kNumWorkers);
  for (int i = 0; i < kNumWorkers; ++i) {
    for (int j = 0; j < kTracesPerWorker; ++j) {
      trace_ids[j] = j * kTracesPerWorker + i;
    }
    collectors.emplace_back(shm, trace_ids, kTracesPerWorker);
  }

  ingestors.reserve(kNumWorkers);
  for (int i = 0; i < kNumWorkers; ++i) {
    ingestors.emplace_back();
  }
}

Monitor::~Monitor() {
  delete shm;
}

void Monitor::Start() {
  std::thread collector_threads[kNumWorkers];
  std::thread ingestor_threads[kNumWorkers];

   // Spawn threads
  for (int i = 0; i < kNumWorkers; i++) {
    collector_threads[i] = std::thread([this, i] { collectors[i].Run(); });
  }

  for (int ingestor_i = 0; ingestor_i < kNumWorkers; ingestor_i++) {
    ingestor_threads[ingestor_i] = std::thread([this, ingestor_i] {
      Ingestor& ingestor = ingestors[ingestor_i];
      // May be excessive memory allocation but allows for random access.
      // Cache the event and its args. kEventMaxArgs number of entries mean
      // the event itself + (kEventMaxArgs-1) args. This is enough, because if
      // kEventMaxArgs are seen, the event would have had enough args to proceed with handling.
      LoggedEvent cached_events[kNumTraces][kEventMaxArgs];
      int has_cached[kNumTraces];
      for (int i = 0; i < kNumTraces; ++i) has_cached[i] = 0;
      while (!stopped) {
        Chunk& chunk = collectors[ingestor_i].Take();
        TraceId trace_id = chunk.GetTraceId();
        // Need to design it such that the chunk boundary is transparent to the ingestor.
        // First, check the number of args for the event. Then, consume that amount.
        // If the end of the chunk is met. Save these events, and move on to the next chunk.
        // When we see a chunk for the same trace id, restore those events and continue.

        // Continue taking args for the last event seen in the previous chunk, if any.
        if (int num_cached = has_cached[trace_id] > 0) {
          LoggedEvent event = cached_events[trace_id][0];
          int expected_num_args = EventNumArgs(event.event_type);
          int remaining_num_args = expected_num_args - (num_cached - 1);
          for (int arg_i = 0; arg_i < remaining_num_args; ++arg_i) {
            // We are at the start of a new chunk. There must necessarily be enough events.
            auto arg = chunk.Next().value();
            cached_events[trace_id][num_cached + arg_i] = arg;
          }
          has_cached[trace_id] = 0;
          IngestorEvent ingestor_event = MakeIngestorEvent(cached_events[trace_id]);
          ingestor.handle_event(trace_id, ingestor_event);
        }

        // Handle all events in the chunk
        while (true) {
          auto maybe_event = chunk.Next();
          if (maybe_event.has_value()) {
            LoggedEvent& event = maybe_event.value();
            int num_args = EventNumArgs(event.event_type);
            int should_break = false;

            // Collect the args for this event.
            for (int arg_i = 0; arg_i < num_args; ++arg_i) {
              auto maybe_arg = chunk.Next();
              if (maybe_arg.has_value()) cached_events[trace_id][arg_i+1] = maybe_arg.value();
              else {
                has_cached[trace_id] = arg_i;
                should_break = true;
                break;
              }
            }
            if(should_break) break;

            // Handle the event.
            has_cached[trace_id] = 0;
            IngestorEvent ingestor_event = MakeIngestorEvent(cached_events[trace_id]);
            ingestor.handle_event(trace_id, ingestor_event);
          }
          else break;
        }
      }
      // TODO: Create a promise for returning the number of events processed
    });
  }

  // Wait for all threads to complete
  for (int i = 0; i < kNumWorkers; i++) {
    collector_threads[i].join();
  }
  for (int i = 0; i < kNumWorkers; i++) {
    ingestor_threads[i].join();
  }

  // TODO: Resolve promises to tally number of events processed
}

}   // namespace Monitor
