#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <atomic>

#include "Constants.h"
#include "SharedMemory.h"
#include "Types.h"
#include "Event.h"


using namespace Monitor;

SharedMemory* shm;

u64 num_events = 0;
std::atomic_uint8_t stop;
int pid;

void handle_sigint(int sig) {
  stop.store(1);
  // printf("Num events: %llu\n", num_events);
  // exit(0);
}

u64 off(u32 idx, u32 offset) { return (idx + offset) & SLOT_IDX_MASK; }

u64 handle_event(Tid tid, Event ev, std::atomic<Event>* mem, int idx) {
  int steps = 0;

  u64 value = 0, counter = 0, old_value = 0, new_value = 0;

  switch (ev.event_type) {
  case Write:
  case Read:
    steps = 1;
    value = mem[idx].load().raw;
    printf("[MONITOR] #%u/%u: %s %p %#llx\n", tid, ev.lap_num, eventtype_to_string(ev.event_type), ev.addr, value);
    break;
  case AtomicLoad:
  case AtomicStore:
    steps = 2;
    counter = mem[idx].load().raw;
    value = mem[off(idx, 1)].load().raw;
    printf("[MONITOR] #%u/%u: %s %p %#llx (#%u)\n", tid, ev.lap_num, eventtype_to_string(ev.event_type), ev.addr, value, counter);
    break;
  case AtomicRMW:
  case AtomicCAS:
    steps = 3;
    counter = mem[idx].load().raw;
    old_value = mem[off(idx, 1)].load().raw;
    new_value = mem[off(idx, 2)].load().raw;
    printf("[MONITOR] #%u/%u: %s %p %#llx %#llx (#%u)\n", tid, ev.lap_num, eventtype_to_string(ev.event_type), ev.addr, old_value, new_value, counter);
    break;
  default:
    printf("[MONITOR] #%u/%u: %s %p\n", tid, ev.lap_num, eventtype_to_string(ev.event_type), ev.addr);
    break;
  }

  return steps;
}

// Thread function
void *worker(void *arg) {
  u64* data = (u64*)arg;
  Tid tid = *data;

  u64 slot_counts[SLOTS_PER_WORKER];

  for (u8 i = 0; i < SLOTS_PER_WORKER; ++i) {
    Sid sid = i*NUM_WORKERS+tid;
    shm->Open(sid);
    slot_counts[i] = 0;
  }

  // Tell the program it can start.
  if (tid == 0) shm->Ready();

  while (!stop.load()) {
    for (u8 i = 0; i < SLOTS_PER_WORKER; ++i) {
      Sid sid = i*NUM_WORKERS+tid;
      if (!shm->IsOpened(sid)) continue;
      do {
        Event ev = shm->Consume(sid);
        if (ev.raw != kEvClear.raw) {
          // Handle event
          int steps = handle_event(i*NUM_WORKERS+tid, ev, mems[i], slot_idxs[i]+1);
          slot_idxs[i] += 1+steps;
          slot_counts[i]++;

          if (HasProgramEnded(ev)) {
            slot_idxs[i] = 0;
            mems[i] = nullptr;
            stop.store(1);
            printf("[MONITOR] #%u Exit!\n", sid);
          }
        }
        else break;   // if data is not ready we dont want to loop
      } while (slot_counts[i] % 16 != 0 && !stop);  // read up to cache line alignment to maximise throughput
    }
  }

  u64 total_count = 0;
  for (u8 i = 0; i < SLOTS_PER_WORKER; ++i) {
    total_count += slot_counts[i];
  }

  *data = total_count;

  for (Sid sid = 0; sid < SLOTS_PER_WORKER; ++sid) {
    shm->Close(sid);
  }

  return 0;
}


int main(int argc, char** argv)
{
  if (argc < 2) {
    printf("[!] Usage: %s <pid>\n", argv[0]);
  }

  pid = atoi(argv[1]);
  shm = new SharedMemory(pid);
  printf("[+] Monitor started on pid %d\n", pid);

  signal(SIGINT, handle_sigint);

  stop.store(0);

  pthread_t workers[NUM_WORKERS];
  u64 worker_data[NUM_WORKERS];

  // Spawn threads
  for (int i = 0; i < NUM_WORKERS; i++) {
    worker_data[i] = i;  // Assign a unique ID to each thread
    if (pthread_create(&workers[i], NULL, worker, &worker_data[i]) != 0) {
      perror("pthread_create");
      exit(EXIT_FAILURE);
    }
  }

  // Wait for all threads to complete
  for (int i = 0; i < NUM_WORKERS; i++) {
    if (pthread_join(workers[i], NULL) != 0) {
      perror("pthread_join");
      exit(EXIT_FAILURE);
    }
    printf("worker #%u: %lu\n", i, worker_data[i]);
    num_events += worker_data[i];
  }

  printf("Num events: %lu\n", num_events);

  delete shm;

  return 0;
}

