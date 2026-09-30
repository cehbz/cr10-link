#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marlin_line.hpp"

namespace marlin {

// Byte sink toward the printer.
class PrinterPort {
 public:
  virtual ~PrinterPort() = default;
  virtual void Write(std::string_view bytes) = 0;
};

// Consumer of printer output. Line views are valid only during the call.
class Listener {
 public:
  virtual ~Listener() = default;
  virtual void OnLine(const Line& line) = 0;
  // A sent command the printer rejected and the protocol cannot resend.
  virtual void OnDropped(std::string_view command) = 0;
};

enum class Numbering { kNone, kNumbered };

// XOR of every byte, as Marlin checks "N<n> <command>*<checksum>".
uint8_t Checksum(std::string_view bytes);

// Owns the conversation with Marlin: one command in flight, released by "ok";
// numbered commands resent on "Resend:"; M112 written ahead of the queue.
// Not thread-safe: one task calls every method.
class Protocol {
 public:
  // MAX_CMD_SIZE 96 in the printer's config: the line buffer holds 95 bytes.
  static constexpr size_t kMaxCommandLength = 95;

  explicit Protocol(PrinterPort& port);

  void AddListener(Listener& listener);

  // Queues one command line. Strips the ';' comment and surrounding whitespace;
  // a line left empty queues nothing. False when the line holds a line break or
  // its wire form exceeds kMaxCommandLength. M112 is written immediately and
  // abandons everything queued and in flight.
  bool Enqueue(std::string_view command, Numbering numbering = Numbering::kNone);

  // Queues "N0 M110 N0": the next numbered command is N1.
  void ResetLineNumbers();

  // Bytes from the printer.
  void Receive(std::string_view bytes);

  // The printer restarted: drops queued and in-flight commands and partial input,
  // and numbers from N1 again.
  void Reset();

  // Commands queued or in flight.
  size_t Outstanding() const;

 private:
  struct Command {
    std::string text;
    Numbering numbering;
  };
  struct Sent {
    std::string text;
    std::string wire;
    std::optional<uint32_t> number;
  };

  void HandleLine(std::string_view text);
  void OnOk();
  void OnResend(uint32_t line);
  void Pump();
  void EmergencyStop();

  PrinterPort& port_;
  std::vector<Listener*> listeners_;
  std::deque<Command> queue_;
  std::optional<Sent> in_flight_;
  uint32_t next_number_ = 1;
  bool awaiting_resend_ok_ = false;
  bool resend_in_flight_ = false;
  std::string input_;
};

}  // namespace marlin
