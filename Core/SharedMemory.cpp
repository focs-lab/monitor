#include "SharedMemory.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "Constants.h"


namespace Monitor {

// Should be thread-safe because the only shared state is fds and mems, but they
// will be accessed in different indices by different threads.
void SharedMemory::Open(TraceId trace_id) {
  assert(trace_id < kNumTraces);

  // Open a new file descriptor, creating the file if it does not exist
  // 0666 = read + write access for user, group and world
  char file_name[64];
  snprintf(file_name, 64, "/tmp/tsan.monitor.%d/%d", pid, trace_id);
  int fd = open(file_name, O_RDWR | O_CREAT, 0666);

  if (fd < 0) {
    // printf("Error opening file %s!\n", file_name);
    return;
  }

  // Ensure that the file will hold enough space
  lseek(fd, kBufferSize, SEEK_SET);
  if (write(fd, "", 1) < 1) {
    // printf("Error writing a single byte to file.\n");
    return;
  }
  lseek(fd, 0, SEEK_SET);

  std::atomic<Event>* mem = reinterpret_cast<std::atomic<Event>*>(mmap(
    NULL,
    kBufferSize,
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    fd,
    0
  ));

  // printf("[MONITOR] Opened and reading from %s\n", file_name);
  fds[trace_id] = fd;
  mems[trace_id] = mem;
  is_open[trace_id] = true;
}

SharedMemory::~SharedMemory() {
  char dir_name[64], file_name[64];
  snprintf(dir_name, 64, "/tmp/tsan.monitor.%d", pid);

  for (int i = 0; i < kNumWorkers * kTracesPerWorker; ++i) {
    snprintf(file_name, 64, "/tmp/tsan.monitor.%d/%d", pid, i);
    unlink(file_name);
  }

  rmdir(dir_name);
}

}   // namespace Monitor
