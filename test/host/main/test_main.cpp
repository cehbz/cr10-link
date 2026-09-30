#include <cstdlib>

#include "unity.h"

namespace {

void TestHarnessRuns() { TEST_ASSERT_EQUAL_INT(4, 2 + 2); }

}  // namespace

extern "C" void setUp() {}
extern "C" void tearDown() {}

extern "C" void app_main() {
  UNITY_BEGIN();
  RUN_TEST(TestHarnessRuns);
  std::exit(UNITY_END());
}
