// Linked into every test on MSVC: report failed asserts on stderr and exit,
// instead of opening a modal dialog that hangs CI until the ctest timeout.
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdlib.h>

namespace {

struct NoDialogs {
  NoDialogs() {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    std::set_terminate([] {
      try {
        if (auto ex = std::current_exception()) std::rethrow_exception(ex);
        std::fputs("terminate called without an exception\n", stderr);
      } catch (std::exception const& e) {
        std::fprintf(stderr, "uncaught exception: %s\n", e.what());
      } catch (...) {
        std::fputs("uncaught non-std exception\n", stderr);
      }
      std::fflush(stderr);
      std::_Exit(3);
    });
  }
} const no_dialogs;

}  // namespace
