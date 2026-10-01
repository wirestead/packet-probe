// Linked into every test on MSVC: report failed asserts on stderr and exit,
// instead of opening a modal dialog that hangs CI until the ctest timeout.
#include <crtdbg.h>
#include <cstdlib>
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
  }
} const no_dialogs;

}  // namespace
