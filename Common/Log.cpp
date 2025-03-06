#include "Log.h"

#include "stdio.h"

namespace Log {
  void Info(const char* message) {
    printf("[+] %s\n", message);
  }

}   // namespace Log
