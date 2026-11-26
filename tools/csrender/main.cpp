// csrender: offline renders for the ColliderScope plugin suite.
// Phase 1 turns a note list or two input WAVs into a WAV plus a PNG of the
// XY picture. Phase 0 only proves the tool builds and links the core.
#include <cstdio>
#include <cstring>

#include "cscore/cscore.h"

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("csrender %s\n", cs::version());
    return 0;
  }
  std::fprintf(stderr,
               "csrender %s\n"
               "usage: csrender --version\n"
               "Rendering arrives in Phase 1.\n",
               cs::version());
  return argc == 1 ? 0 : 2;
}
