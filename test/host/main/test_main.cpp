#include <cstdlib>

#include "test_suites.hpp"
#include "unity.h"

extern "C" void setUp() {}
extern "C" void tearDown() {}

extern "C" void app_main() {
  UNITY_BEGIN();
  RunMarlinLineTests();
  RunMarlinProtocolTests();
  std::exit(UNITY_END());
}
