// Printer output in the formats Marlin 2.1.2.8 emits for this configuration
// (one hotend, heated bed, SDSUPPORT, HOST_KEEPALIVE_FEATURE, no ADVANCED_OK).

#include "marlin_line.hpp"
#include "test_suites.hpp"
#include "unity.h"

namespace {

using marlin::Classify;
using marlin::LineKind;

void TestOkIsOk() {
  const auto line = Classify("ok");
  TEST_ASSERT_TRUE(line.kind == LineKind::kOk);
  TEST_ASSERT_FALSE(line.temperatures.has_value());
}

// M105: "ok" then print_heater_states on the same line.
void TestM105AnswerIsOkWithTemperatures() {
  const auto line = Classify("ok T:210.00 /210.00 B:60.00 /60.00 @:127 B@:64");
  TEST_ASSERT_TRUE(line.kind == LineKind::kOk);
  TEST_ASSERT_TRUE(line.temperatures.has_value());
  TEST_ASSERT_EQUAL_FLOAT(210.0f, line.temperatures->tool.actual);
  TEST_ASSERT_EQUAL_FLOAT(210.0f, line.temperatures->tool.target);
  TEST_ASSERT_EQUAL_FLOAT(60.0f, line.temperatures->bed.actual);
  TEST_ASSERT_EQUAL_FLOAT(60.0f, line.temperatures->bed.target);
}

// M155 auto-report: print_heater_state starts every reading with a space.
void TestAutoReportIsTemperature() {
  const auto line = Classify(" T:25.31 /0.00 B:24.80 /0.00 @:0 B@:0");
  TEST_ASSERT_TRUE(line.kind == LineKind::kTemperature);
  TEST_ASSERT_TRUE(line.temperatures.has_value());
  TEST_ASSERT_EQUAL_FLOAT(25.31f, line.temperatures->tool.actual);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, line.temperatures->tool.target);
  TEST_ASSERT_EQUAL_FLOAT(24.80f, line.temperatures->bed.actual);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, line.temperatures->bed.target);
}

// M109 wait: residency countdown appended.
void TestHeatingWaitIsTemperature() {
  const auto line = Classify(" T:180.52 /210.00 B:60.01 /60.00 @:127 B@:0 W:?");
  TEST_ASSERT_TRUE(line.kind == LineKind::kTemperature);
  TEST_ASSERT_EQUAL_FLOAT(180.52f, line.temperatures->tool.actual);
  TEST_ASSERT_EQUAL_FLOAT(210.0f, line.temperatures->tool.target);
  TEST_ASSERT_EQUAL_FLOAT(60.01f, line.temperatures->bed.actual);
}

void TestNegativeReadingParses() {
  const auto line = Classify(" T:-14.00 /0.00 B:22.10 /0.00 @:0 B@:0");
  TEST_ASSERT_TRUE(line.kind == LineKind::kTemperature);
  TEST_ASSERT_EQUAL_FLOAT(-14.0f, line.temperatures->tool.actual);
}

void TestResendCarriesLineNumber() {
  const auto line = Classify("Resend: 57");
  TEST_ASSERT_TRUE(line.kind == LineKind::kResend);
  TEST_ASSERT_EQUAL_UINT32(57, line.resend_from);
}

void TestLineErrorsAreErrors() {
  TEST_ASSERT_TRUE(Classify("Error:checksum mismatch, Last Line: 56").kind == LineKind::kError);
  TEST_ASSERT_TRUE(Classify("Error:Line Number is not Last Line Number+1, Last Line: 56").kind ==
                   LineKind::kError);
  TEST_ASSERT_TRUE(Classify("Error:No Checksum with line number, Last Line: 56").kind ==
                   LineKind::kError);
  TEST_ASSERT_TRUE(Classify("Error:Printer halted. kill() called!").kind == LineKind::kError);
}

void TestSdPrintingByte() {
  const auto line = Classify("SD printing byte 1234/567890");
  TEST_ASSERT_TRUE(line.kind == LineKind::kSdProgress);
  TEST_ASSERT_TRUE(line.sd.printing);
  TEST_ASSERT_EQUAL_UINT32(1234, line.sd.position);
  TEST_ASSERT_EQUAL_UINT32(567890, line.sd.size);
}

void TestNotSdPrinting() {
  const auto line = Classify("Not SD printing");
  TEST_ASSERT_TRUE(line.kind == LineKind::kSdProgress);
  TEST_ASSERT_FALSE(line.sd.printing);
}

void TestEchoAndOtherPassThroughAsText() {
  const auto busy = Classify("echo:busy: processing");
  TEST_ASSERT_TRUE(busy.kind == LineKind::kOther);
  TEST_ASSERT_EQUAL_STRING_LEN("echo:busy: processing", busy.text.data(), busy.text.size());
  TEST_ASSERT_TRUE(Classify("echo:Unknown command: \"G999\"").kind == LineKind::kOther);
  TEST_ASSERT_TRUE(Classify("Writing to file: CUBE.GCO").kind == LineKind::kOther);
  TEST_ASSERT_TRUE(Classify("Done saving file.").kind == LineKind::kOther);
  TEST_ASSERT_TRUE(Classify("start").kind == LineKind::kOther);
}

}  // namespace

void RunMarlinLineTests() {
  RUN_TEST(TestOkIsOk);
  RUN_TEST(TestM105AnswerIsOkWithTemperatures);
  RUN_TEST(TestAutoReportIsTemperature);
  RUN_TEST(TestHeatingWaitIsTemperature);
  RUN_TEST(TestNegativeReadingParses);
  RUN_TEST(TestResendCarriesLineNumber);
  RUN_TEST(TestLineErrorsAreErrors);
  RUN_TEST(TestSdPrintingByte);
  RUN_TEST(TestNotSdPrinting);
  RUN_TEST(TestEchoAndOtherPassThroughAsText);
}
