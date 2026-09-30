#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace marlin {

// One line of printer output, classified. Formats are Marlin 2.1.2.8's.
enum class LineKind {
  kOk,           // "ok", or "ok T:..." from M105
  kTemperature,  // " T:a /t B:a /t @:p B@:p", from M155 auto-report and M109/M190 waits
  kResend,       // "Resend: <n>"
  kError,        // "Error:..."
  kSdProgress,   // "SD printing byte <pos>/<size>" or "Not SD printing" (M27)
  kOther,        // echo: and everything else, as text
};

struct HeaterReading {
  float actual = 0;
  float target = 0;
};

struct Temperatures {
  HeaterReading tool;
  HeaterReading bed;
};

struct SdProgress {
  bool printing = false;
  uint32_t position = 0;
  uint32_t size = 0;
};

struct Line {
  LineKind kind = LineKind::kOther;
  std::string_view text;  // without the line terminator
  std::optional<Temperatures> temperatures;  // kTemperature; kOk when M105 answered
  uint32_t resend_from = 0;                   // kResend
  SdProgress sd;                              // kSdProgress
};

// text is one line without its terminator; the result views it.
Line Classify(std::string_view text);

}  // namespace marlin
