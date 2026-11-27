#include "../test_framework.hpp"

#ifdef _WIN32
#include <crtdbg.h>
#endif

int main() {
#ifdef _DEBUG
  // A Debug CRT assertion normally opens a modal dialog. On a headless CI
  // runner that dialog never gets dismissed, so the test binary appears to
  // hang indefinitely. Send assertions/errors to stderr instead so CTest can
  // report the failure.
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
  return phantomx::test::run_all();
}
