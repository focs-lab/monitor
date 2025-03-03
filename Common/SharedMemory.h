#ifndef MONITOR_SHAREDMEMORY_H
#define MONITOR_SHAREDMEMORY_H

#include <atomic>
#include <cassert>
#include <unistd.h>

#include "Constants.h"
#include "Event.h"


namespace Monitor {

class SharedMemory {
public:
  SharedMemory() = delete;
  SharedMemory(int pid) : pid(pid) {
    for (int i = 0; i < SLOTS_PER_WORKER; ++i) {
      fds[i] = -1;
      idxs[i] = 0;
    }
  }

  ~SharedMemory();

  void Open(Sid sid);
  Event Consume(Sid sid) {
    int idx = idxs[sid];
    AEvent* evp = &mems[sid][idx];
    idxs[sid] = (idx + 1) & SLOT_IDX_MASK;
    return evp->load();
  }
  void Ready() {
    assert(idxs[0] == 0);

    mems[0][0].store(kEvMonitorReady);
    idxs[0]++;
  }
  void Close(Sid sid) { close(fds[sid]); }
  bool IsOpened(Sid sid) { return mems[sid] != nullptr; }

private:
  int pid;
  AEvent* mems[SLOTS_PER_WORKER];
  int fds[SLOTS_PER_WORKER];
  int idxs[SLOTS_PER_WORKER];
};

}   // namespace Monitor

#endif
