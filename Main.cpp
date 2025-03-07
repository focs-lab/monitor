
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

#include "Common/Constants.h"
#include "Common/SharedMemory.h"
#include "Common/Types.h"
#include "Common/Event.h"


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

u64 off(u32 idx, u32 offset) { return (idx + offset) & BUF_IDX_MASK; }

u64 handle_event(Sid sid, LoggedEvent ev, SharedMemory *shm) {
  int steps = 0;
  u64 value = 0, counter = 0, old_value = 0, new_value = 0;

  switch (ev.event_type) {
  case WRITE:
  case READ:
    steps = 1;
    value = shm->Consume(sid).raw;
    printf("[MONITOR] #%u/%u: %s %p %#llx\n", sid, ev.lap_num, eventtype_to_string(ev.event_type), ev.addr, value);
    break;
  case ATOMICLOAD:
  case ATOMICSTORE:
    steps = 2;
    counter = shm->Consume(sid).raw;
    value = shm->Consume(sid).raw;
    printf("[MONITOR] #%u/%u: %s %p %#llx (#%u)\n", sid, ev.lap_num, eventtype_to_string(ev.event_type), ev.addr, value, counter);
    break;
  case ATOMICRMW:
  case ATOMICCAS:
    steps = 3;
    counter = shm->Consume(sid).raw;
    old_value = shm->Consume(sid).raw;
    new_value = shm->Consume(sid).raw;
    printf("[MONITOR] #%u/%u: %s %p %#llx %#llx (#%u)\n", sid, ev.lap_num, eventtype_to_string(ev.event_type), ev.addr, old_value, new_value, counter);
    break;
  default:
    printf("[MONITOR] #%u/%u: %s %p\n", sid, ev.lap_num, eventtype_to_string(ev.event_type), ev.addr);
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
        LoggedEvent ev = shm->Consume(sid);
        if (ev.raw != kEvClear.raw) {
          // Handle event
          int steps = handle_event(sid, ev, shm);
          slot_counts[i]++;

          if (HasProgramEnded(ev)) {
            shm->Close(sid);
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
