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


typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
// typedef u64 Event;

typedef u32 Sid;
typedef u32 Tid;


constexpr u32 NUM_SLOTS = 256;
constexpr u32 NUM_WORKERS = 8;
constexpr u32 SLOTS_PER_WORKER = NUM_SLOTS / NUM_WORKERS;
static_assert(NUM_SLOTS == NUM_WORKERS * SLOTS_PER_WORKER);

constexpr u32 SPIN_COUNT = 0;
constexpr u32 BUF_SIZE = 0x1000;
constexpr u32 SLOT_IDX_MASK = 0xfff;

u64 num_events = 0;
std::atomic_uint8_t stop;

int pid;

enum EventType : u8 {
  kEventClear = 0,    // cannot use 0 for events, this is used to mark a cleared entry in the channel

  kEventRead = 1,
  kEventWrite = 2,

  kEventVptrUpdate = 3,
  kEventVptrLoad = 4,

  kEventMemset = 5,
  kEventMemcpy = 6,

  kEventAtomicLoad = 7,
  kEventAtomicStore = 8,
  kEventAtomicRMW = 9,
  kEventAtomicCAS = 10,
  kEventAtomicFence = 11,

  kEventReturn = 12,
  kEventAtExit = 13,
  kEventIgnore = 0xff
};

typedef union {
  u64 raw;
  struct {      // remember little-endian byte order
    u64 addr : 48;
    u8 lap_num : 4;
    u8 _ : 4;
    EventType event_type : 8;
  };
} Event;

constexpr Event raw_event(u64 v) { return { .raw = v }; }

constexpr Event kClear = raw_event(0);
constexpr Event kMonitorReady = raw_event(0xcafebeef);
constexpr Event kProgramEnded = raw_event(0xdeaddead);

void handle_sigint(int sig) {
  stop.store(1);
  // printf("Num events: %llu\n", num_events);
  // exit(0);
}

std::atomic<Event>* OpenSlotFile(Sid sid, int* out_fd=nullptr) {
	// Open a new file descriptor, creating the file if it does not exist
	// 0666 = read + write access for user, group and world
  char file_name[64];
  snprintf(file_name, 64, "/tmp/tsan.slots.%d/%d", pid, sid);
	int fd = open(file_name, O_RDWR | O_CREAT, 0666);

	if (fd < 0) {
		// printf("Error opening file %s!\n", file_name);
    return nullptr;
	}

	// Ensure that the file will hold enough space
	lseek(fd, BUF_SIZE*sizeof(Event), SEEK_SET);
	if (write(fd, "", 1) < 1) {
		// printf("Error writing a single byte to file.\n");
    return nullptr;
	}
	lseek(fd, 0, SEEK_SET);

  std::atomic<Event>* mem = reinterpret_cast<std::atomic<Event>*>(mmap(
    NULL,
    BUF_SIZE*sizeof(Event),
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    fd,
    0
  ));

  // printf("[MONITOR] Opened and reading from %s\n", file_name);

  if (out_fd) *out_fd = fd;

	return mem;
}

const char* eventtype_to_string(EventType evt) {
  switch (evt) {
  case kEventClear:
    return "CLEAR";
  case kEventRead:
    return "READ";
  case kEventWrite:
    return "WRITE";
  case kEventVptrUpdate:
    return "VPTRUPDATE";
  case kEventVptrLoad:
    return "VPTRLOAD";
  case kEventMemset:
    return "MEMSET";
  case kEventMemcpy:
    return "MEMCPY";
  case kEventAtomicLoad:
    return "ATOMICLOAD";
  case kEventAtomicStore:
    return "ATOMICSTORE";
  case kEventAtomicRMW:
    return "ATOMICRMW";
  case kEventAtomicCAS:
    return "ATOMICCAS";
  case kEventAtomicFence:
    return "ATOMICFENCE";
  case kEventReturn:
    return "RETURN";
  case kEventAtExit:
    return "ATEXIT";

  default:
    return "UNKNOWN";
  }
}

u64 off(u32 idx, u32 offset) { return (idx + offset) & SLOT_IDX_MASK; }

u64 handle_event(Tid tid, Event ev, std::atomic<Event>* mem, int idx) {
  int steps = 0;

  u64 value = 0, counter = 0, old_value = 0, new_value = 0;

  switch (ev.event_type) {
  case kEventWrite:
  case kEventRead:
    steps = 1;
    value = mem[idx].load().raw;
    printf("[MONITOR] #%u/%u: %s %p %#llx\n", tid, ev.lap_num, eventtype_to_string(ev.event_type), ev.addr, value);
    break;
  case kEventAtomicLoad:
  case kEventAtomicStore:
    steps = 2;
    counter = mem[idx].load().raw;
    value = mem[off(idx, 1)].load().raw;
    printf("[MONITOR] #%u/%u: %s %p %#llx (#%u)\n", tid, ev.lap_num, eventtype_to_string(ev.event_type), ev.addr, value, counter);
    break;
  case kEventAtomicRMW:
  case kEventAtomicCAS:
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

u64 has_program_ended(Event ev) {
  return ev.raw == kProgramEnded.raw;
}

void clear_event(std::atomic<Event>* ev) {
  ev->store(raw_event(0));
}

void cleanup_files() {
  char dir_name[64], file_name[64];
  snprintf(dir_name, 64, "/tmp/tsan.slots.%d", pid);

  for (int i = 0; i < NUM_WORKERS * SLOTS_PER_WORKER; ++i) {
    snprintf(file_name, 64, "/tmp/tsan.slots.%d/%d", pid, i);
    unlink(file_name);
  }

  rmdir(dir_name);
}

// Thread function
void *worker(void *arg) {
  u64* data = (u64*)arg;
  Tid tid = *data;

  std::atomic<Event>* mems[SLOTS_PER_WORKER];
  u64 slot_counts[SLOTS_PER_WORKER];
  u64 slot_idxs[SLOTS_PER_WORKER];
  int fds[SLOTS_PER_WORKER];

  for (u8 i = 0; i < SLOTS_PER_WORKER; ++i) {
    Sid sid = i*NUM_WORKERS+tid;
    mems[i] = OpenSlotFile(sid, &fds[i]);
    slot_counts[i] = 0;
    slot_idxs[i] = 0;
  }

  // Tell the program it can start.
  if (tid == 0) {
    mems[0][0].store(kMonitorReady, std::memory_order_release);
    slot_idxs[0]++;
  }

  while (!stop.load()) {
    for (u8 i = 0; i < SLOTS_PER_WORKER; ++i) {
      if (!mems[i]) continue;

      Sid sid = i*NUM_WORKERS+tid;
      do {
        std::atomic<Event> *evp = mems[i] + (slot_idxs[i] & SLOT_IDX_MASK);
        Event ev = evp->load(std::memory_order_acquire);
        if (ev.raw != kClear.raw) {
          // Handle event
          int steps = handle_event(i*NUM_WORKERS+tid, ev, mems[i], slot_idxs[i]+1);
          clear_event(evp);
          slot_idxs[i] += 1+steps;
          slot_counts[i]++;

          if (has_program_ended(ev)) {
            slot_idxs[i] = 0;
            mems[i] = nullptr;
            stop.store(1);
            printf("[MONITOR] #%u Exit!\n", sid);
          }
        }
        else break;   // if data is not ready we dont want to loop
      } while (slot_idxs[i] % 16 != 0 && !stop);  // read up to cache line alignment to maximise throughput
    }
  }

  u64 total_count = 0;
  for (u8 i = 0; i < SLOTS_PER_WORKER; ++i) {
    total_count += slot_counts[i];
  }

  *data = total_count;

  for (int i = 0; i < SLOTS_PER_WORKER; ++i) {
    close(fds[i]);
  }

  return 0;
}


int main(int argc, char** argv)
{
  if (argc < 2) {
    printf("[!] Usage: %s <pid>\n", argv[0]);
  }
  pid = atoi(argv[1]);
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

  cleanup_files();

  return 0;
}

