#include <string>
#include <string_view>
#include <vector>

#include "marlin_protocol.hpp"
#include "test_suites.hpp"
#include "unity.h"

namespace {

using marlin::Checksum;
using marlin::LineKind;
using marlin::Numbering;
using marlin::Protocol;

class FakePrinter : public marlin::PrinterPort {
 public:
  void Write(std::string_view bytes) override { writes.emplace_back(bytes); }
  std::vector<std::string> writes;
};

class RecordingListener : public marlin::Listener {
 public:
  void OnLine(const marlin::Line& line) override {
    kinds.push_back(line.kind);
    texts.emplace_back(line.text);
  }
  void OnDropped(std::string_view command) override { dropped.emplace_back(command); }
  std::vector<LineKind> kinds;
  std::vector<std::string> texts;
  std::vector<std::string> dropped;
};

void AssertWrites(const std::vector<std::string>& expected, const FakePrinter& printer) {
  TEST_ASSERT_EQUAL_size_t(expected.size(), printer.writes.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), printer.writes[i].c_str());
  }
}

// Reference values: XOR of the bytes before '*'.
void TestChecksum() {
  TEST_ASSERT_EQUAL_UINT8(125, Checksum("N0 M110 N0"));
  TEST_ASSERT_EQUAL_UINT8(18, Checksum("N1 G28"));
  TEST_ASSERT_EQUAL_UINT8(40, Checksum("N2 G1 X10 Y20"));
}

void TestSplitsLinesAcrossChunksAndIgnoresCr() {
  FakePrinter printer;
  RecordingListener listener;
  Protocol protocol(printer);
  protocol.AddListener(listener);
  protocol.Receive("echo:busy: pro");
  protocol.Receive("cessing\nok\r\n\n T:20.00 /0.00 B:21.00 /0.00 @:0 B@:0\n");
  TEST_ASSERT_EQUAL_size_t(3, listener.texts.size());
  TEST_ASSERT_EQUAL_STRING("echo:busy: processing", listener.texts[0].c_str());
  TEST_ASSERT_EQUAL_STRING("ok", listener.texts[1].c_str());
  TEST_ASSERT_TRUE(listener.kinds[1] == LineKind::kOk);
  TEST_ASSERT_TRUE(listener.kinds[2] == LineKind::kTemperature);
}

void TestOneCommandInFlightUntilOk() {
  FakePrinter printer;
  Protocol protocol(printer);
  TEST_ASSERT_TRUE(protocol.Enqueue("G28"));
  TEST_ASSERT_TRUE(protocol.Enqueue("G1 X10"));
  TEST_ASSERT_TRUE(protocol.Enqueue("M105"));
  AssertWrites({"G28\n"}, printer);
  TEST_ASSERT_EQUAL_size_t(3, protocol.Outstanding());

  protocol.Receive("echo:busy: processing\n");
  protocol.Receive(" T:20.00 /0.00 B:21.00 /0.00 @:0 B@:0\n");
  AssertWrites({"G28\n"}, printer);

  protocol.Receive("ok\n");
  AssertWrites({"G28\n", "G1 X10\n"}, printer);
  protocol.Receive("ok\n");
  AssertWrites({"G28\n", "G1 X10\n", "M105\n"}, printer);
  protocol.Receive("ok T:20.00 /0.00 B:21.00 /0.00 @:0 B@:0\n");
  TEST_ASSERT_EQUAL_size_t(0, protocol.Outstanding());
}

void TestUnsolicitedOkSendsNothing() {
  FakePrinter printer;
  Protocol protocol(printer);
  protocol.Receive("ok\n");
  TEST_ASSERT_EQUAL_size_t(0, printer.writes.size());
}

void TestStripsCommentsAndWhitespace() {
  FakePrinter printer;
  Protocol protocol(printer);
  TEST_ASSERT_TRUE(protocol.Enqueue(";LAYER:0"));
  TEST_ASSERT_EQUAL_size_t(0, protocol.Outstanding());
  TEST_ASSERT_TRUE(protocol.Enqueue("  G1 X10 Y20 ; travel "));
  AssertWrites({"G1 X10 Y20\n"}, printer);
}

void TestRejectsLineBreaksAndOverlongLines() {
  FakePrinter printer;
  Protocol protocol(printer);
  TEST_ASSERT_FALSE(protocol.Enqueue("G28\nG1 X10"));
  TEST_ASSERT_FALSE(protocol.Enqueue(std::string(Protocol::kMaxCommandLength + 1, 'G')));
  TEST_ASSERT_TRUE(protocol.Enqueue(std::string(Protocol::kMaxCommandLength, 'G')));
  // "N1 " and "*nnn" count against the printer's line buffer.
  TEST_ASSERT_FALSE(protocol.Enqueue(std::string(Protocol::kMaxCommandLength - 4, 'G'),
                                     Numbering::kNumbered));
}

void TestNumberedCommandsCarryLineNumberAndChecksum() {
  FakePrinter printer;
  Protocol protocol(printer);
  protocol.Enqueue("G28", Numbering::kNumbered);
  protocol.Enqueue("M105");
  protocol.Enqueue("G1 X10 Y20", Numbering::kNumbered);
  protocol.Receive("ok\nok T:20.00 /0.00 B:21.00 /0.00 @:0 B@:0\nok\n");
  AssertWrites({"N1 G28*18\n", "M105\n", "N2 G1 X10 Y20*40\n"}, printer);
}

void TestResetLineNumbersSendsM110() {
  FakePrinter printer;
  Protocol protocol(printer);
  protocol.Enqueue("G28", Numbering::kNumbered);
  protocol.Receive("ok\n");
  protocol.ResetLineNumbers();
  protocol.Receive("ok\n");
  protocol.Enqueue("G28", Numbering::kNumbered);
  AssertWrites({"N1 G28*18\n", "N0 M110 N0*125\n", "N1 G28*18\n"}, printer);
}

// gcode_line_error: "Error:<reason><last_N>", "Resend: <last_N+1>", "ok".
void TestResendRewritesTheRejectedLine() {
  FakePrinter printer;
  RecordingListener listener;
  Protocol protocol(printer);
  protocol.AddListener(listener);
  protocol.Enqueue("G28", Numbering::kNumbered);
  protocol.Enqueue("G1 X10 Y20", Numbering::kNumbered);
  protocol.Receive("Error:checksum mismatch, Last Line: 0\nResend: 1\n");
  AssertWrites({"N1 G28*18\n"}, printer);
  protocol.Receive("ok\n");
  AssertWrites({"N1 G28*18\n", "N1 G28*18\n"}, printer);
  protocol.Receive("ok\n");
  AssertWrites({"N1 G28*18\n", "N1 G28*18\n", "N2 G1 X10 Y20*40\n"}, printer);
  TEST_ASSERT_EQUAL_size_t(0, listener.dropped.size());
  TEST_ASSERT_TRUE(listener.kinds[0] == LineKind::kError);
  TEST_ASSERT_TRUE(listener.kinds[1] == LineKind::kResend);
}

// While M28 is saving, an unnumbered line other than M29 is refused.
void TestResendOfUnnumberedLineDropsIt() {
  FakePrinter printer;
  RecordingListener listener;
  Protocol protocol(printer);
  protocol.AddListener(listener);
  protocol.Enqueue("G28");
  protocol.Enqueue("M105");
  protocol.Receive("Error:No Checksum with line number, Last Line: 3\nResend: 4\nok\n");
  TEST_ASSERT_EQUAL_size_t(1, listener.dropped.size());
  TEST_ASSERT_EQUAL_STRING("G28", listener.dropped[0].c_str());
  AssertWrites({"G28\n", "M105\n"}, printer);
}

// The printer asks for a line the protocol never sent (it restarted unnoticed).
void TestResendOfUnknownLineDropsIt() {
  FakePrinter printer;
  RecordingListener listener;
  Protocol protocol(printer);
  protocol.AddListener(listener);
  protocol.Enqueue("G28", Numbering::kNumbered);
  protocol.Receive("ok\n");
  protocol.Enqueue("G1 X10 Y20", Numbering::kNumbered);
  protocol.Receive("Error:Line Number is not Last Line Number+1, Last Line: 0\nResend: 1\nok\n");
  TEST_ASSERT_EQUAL_size_t(1, listener.dropped.size());
  TEST_ASSERT_EQUAL_STRING("G1 X10 Y20", listener.dropped[0].c_str());
  TEST_ASSERT_EQUAL_size_t(0, protocol.Outstanding());
}

void TestM112BypassesQueueAndAbandonsIt() {
  FakePrinter printer;
  Protocol protocol(printer);
  protocol.Enqueue("M109 S210");
  protocol.Enqueue("G28");
  TEST_ASSERT_TRUE(protocol.Enqueue("M112"));
  AssertWrites({"M109 S210\n", "M112\n"}, printer);
  TEST_ASSERT_EQUAL_size_t(0, protocol.Outstanding());
  protocol.Receive("Error:Printer halted. kill() called!\n");
  protocol.Receive("ok\n");
  AssertWrites({"M109 S210\n", "M112\n"}, printer);
}

void TestM112IsNotM1120() {
  FakePrinter printer;
  Protocol protocol(printer);
  protocol.Enqueue("M109 S210");
  protocol.Enqueue("M1120");
  AssertWrites({"M109 S210\n"}, printer);
  TEST_ASSERT_EQUAL_size_t(2, protocol.Outstanding());
}

void TestResetDropsQueueAndRestartsNumbering() {
  FakePrinter printer;
  Protocol protocol(printer);
  protocol.Enqueue("G28", Numbering::kNumbered);
  protocol.Enqueue("G1 X10", Numbering::kNumbered);
  protocol.Receive("echo:partial");
  protocol.Reset();
  TEST_ASSERT_EQUAL_size_t(0, protocol.Outstanding());
  protocol.Enqueue("G28", Numbering::kNumbered);
  AssertWrites({"N1 G28*18\n", "N1 G28*18\n"}, printer);
}

}  // namespace

void RunMarlinProtocolTests() {
  RUN_TEST(TestChecksum);
  RUN_TEST(TestSplitsLinesAcrossChunksAndIgnoresCr);
  RUN_TEST(TestOneCommandInFlightUntilOk);
  RUN_TEST(TestUnsolicitedOkSendsNothing);
  RUN_TEST(TestStripsCommentsAndWhitespace);
  RUN_TEST(TestRejectsLineBreaksAndOverlongLines);
  RUN_TEST(TestNumberedCommandsCarryLineNumberAndChecksum);
  RUN_TEST(TestResetLineNumbersSendsM110);
  RUN_TEST(TestResendRewritesTheRejectedLine);
  RUN_TEST(TestResendOfUnnumberedLineDropsIt);
  RUN_TEST(TestResendOfUnknownLineDropsIt);
  RUN_TEST(TestM112BypassesQueueAndAbandonsIt);
  RUN_TEST(TestM112IsNotM1120);
  RUN_TEST(TestResetDropsQueueAndRestartsNumbering);
}
