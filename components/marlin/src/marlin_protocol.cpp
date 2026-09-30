#include "marlin_protocol.hpp"

#include <charconv>

namespace marlin {
namespace {

// Bounds memory when the printer emits bytes without a line feed (boot noise).
constexpr size_t kMaxInputLine = 256;

constexpr std::string_view kWhitespace = " \t\r";

// Drops the ';' comment and surrounding whitespace. Marlin discards everything
// from ';' before it checks the checksum, so a comment would hide '*'.
std::string_view Strip(std::string_view command) {
  command = command.substr(0, command.find(';'));
  const size_t first = command.find_first_not_of(kWhitespace);
  if (first == std::string_view::npos) return {};
  const size_t last = command.find_last_not_of(kWhitespace);
  return command.substr(first, last - first + 1);
}

// The command word is `word`, not a longer code (M112 vs M1120).
bool IsCommand(std::string_view command, std::string_view word) {
  return command.starts_with(word) &&
         (command.size() == word.size() || command[word.size()] == ' ');
}

// M110 N<n>: the printer's last line number becomes n.
std::optional<uint32_t> M110LineNumber(std::string_view command) {
  if (!IsCommand(command, "M110")) return std::nullopt;
  const size_t n = command.find('N', 4);
  if (n == std::string_view::npos) return std::nullopt;
  uint32_t value = 0;
  const char* begin = command.data() + n + 1;
  if (std::from_chars(begin, command.data() + command.size(), value).ec != std::errc()) {
    return std::nullopt;
  }
  return value;
}

std::string NumberedLine(uint32_t number, std::string_view command) {
  std::string line = "N" + std::to_string(number) + " ";
  line += command;
  const uint8_t checksum = Checksum(line);
  line += "*" + std::to_string(checksum);
  return line;
}

}  // namespace

uint8_t Checksum(std::string_view bytes) {
  uint8_t checksum = 0;
  for (const char c : bytes) checksum ^= static_cast<uint8_t>(c);
  return checksum;
}

Protocol::Protocol(PrinterPort& port) : port_(port) {}

void Protocol::AddListener(Listener& listener) { listeners_.push_back(&listener); }

bool Protocol::Enqueue(std::string_view command, Numbering numbering) {
  if (command.find('\n') != std::string_view::npos) return false;
  command = Strip(command);
  if (command.empty()) return true;
  if (IsCommand(command, "M112")) {
    EmergencyStop();
    return true;
  }
  // Numbered wire form adds "N<number> " and "*<checksum>"; the number is at most
  // next_number_ + queue_.size() and the checksum at most three digits.
  const size_t overhead = numbering == Numbering::kNumbered
                              ? std::to_string(next_number_ + queue_.size()).size() + 6
                              : 0;
  if (command.size() + overhead > kMaxCommandLength) return false;
  queue_.push_back({std::string(command), numbering});
  Pump();
  return true;
}

void Protocol::ResetLineNumbers() { Enqueue("M110 N0", Numbering::kNumbered); }

void Protocol::Receive(std::string_view bytes) {
  for (const char c : bytes) {
    if (c == '\r') continue;
    if (c != '\n') input_.push_back(c);
    if (c == '\n' || input_.size() >= kMaxInputLine) {
      if (!input_.empty()) {
        std::string line;
        line.swap(input_);  // a listener may Reset(), which clears input_
        HandleLine(line);
      }
    }
  }
}

void Protocol::Reset() {
  queue_.clear();
  in_flight_.reset();
  next_number_ = 1;
  awaiting_resend_ok_ = false;
  resend_in_flight_ = false;
  input_.clear();
}

size_t Protocol::Outstanding() const { return queue_.size() + (in_flight_ ? 1 : 0); }

void Protocol::HandleLine(std::string_view text) {
  const Line line = Classify(text);
  if (line.kind == LineKind::kOk) OnOk();
  if (line.kind == LineKind::kResend) OnResend(line.resend_from);
  for (Listener* listener : listeners_) listener->OnLine(line);
}

// gcode_line_error answers a rejected line with "Resend: <last_N+1>" then an
// "ok" that acknowledges nothing; the rejected line is written again after it.
void Protocol::OnResend(uint32_t line) {
  awaiting_resend_ok_ = true;
  if (!in_flight_) return;
  if (in_flight_->number == line) {
    resend_in_flight_ = true;
    return;
  }
  const Sent dropped = std::move(*in_flight_);
  in_flight_.reset();
  for (Listener* listener : listeners_) listener->OnDropped(dropped.text);
}

void Protocol::OnOk() {
  if (awaiting_resend_ok_) {
    awaiting_resend_ok_ = false;
    if (resend_in_flight_) {
      resend_in_flight_ = false;
      port_.Write(in_flight_->wire);
      return;
    }
  } else {
    in_flight_.reset();
  }
  Pump();
}

void Protocol::Pump() {
  if (in_flight_ || awaiting_resend_ok_ || queue_.empty()) return;
  Command command = std::move(queue_.front());
  queue_.pop_front();

  Sent sent;
  sent.text = std::move(command.text);
  const std::optional<uint32_t> m110 = M110LineNumber(sent.text);
  if (command.numbering == Numbering::kNumbered) {
    sent.number = m110.value_or(next_number_);
    sent.wire = NumberedLine(*sent.number, sent.text);
    next_number_ = *sent.number + 1;
  } else {
    sent.wire = sent.text;
  }
  if (m110) next_number_ = *m110 + 1;
  sent.wire += '\n';
  in_flight_ = std::move(sent);
  port_.Write(in_flight_->wire);
}

// The emergency parser acts on M112 as it arrives, ahead of the printer's queue,
// and kill() never answers: nothing queued or in flight will be acknowledged.
void Protocol::EmergencyStop() {
  port_.Write("M112\n");
  queue_.clear();
  in_flight_.reset();
  awaiting_resend_ok_ = false;
  resend_in_flight_ = false;
}

}  // namespace marlin
