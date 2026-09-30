#include "marlin_line.hpp"

#include <charconv>

namespace marlin {
namespace {

constexpr std::string_view kOk = "ok";
constexpr std::string_view kResend = "Resend:";
constexpr std::string_view kError = "Error:";
constexpr std::string_view kSdPrintingByte = "SD printing byte ";
constexpr std::string_view kNotSdPrinting = "Not SD printing";

void SkipSpaces(std::string_view& s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
}

bool TakePrefix(std::string_view& s, std::string_view prefix) {
  if (!s.starts_with(prefix)) return false;
  s.remove_prefix(prefix.size());
  return true;
}

// Parses a number at the front of s and advances past it.
template <typename T>
bool TakeNumber(std::string_view& s, T& value) {
  const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc()) return false;
  s.remove_prefix(end - s.data());
  return true;
}

// "<key><actual> /<target>" after optional spaces.
bool TakeReading(std::string_view& s, std::string_view key, HeaterReading& reading) {
  SkipSpaces(s);
  return TakePrefix(s, key) && TakeNumber(s, reading.actual) && TakePrefix(s, " /") &&
         TakeNumber(s, reading.target);
}

// print_heater_states for one hotend and a bed: " T:a /t B:a /t @:p B@:p".
std::optional<Temperatures> ParseTemperatures(std::string_view s) {
  Temperatures t;
  if (!TakeReading(s, "T:", t.tool) || !TakeReading(s, "B:", t.bed)) return std::nullopt;
  return t;
}

std::optional<uint32_t> ParseResend(std::string_view s) {
  uint32_t line = 0;
  if (!TakePrefix(s, kResend)) return std::nullopt;
  SkipSpaces(s);
  if (!TakeNumber(s, line)) return std::nullopt;
  return line;
}

// CardReader::report_status.
std::optional<SdProgress> ParseSdProgress(std::string_view s) {
  if (s == kNotSdPrinting) return SdProgress{};
  SdProgress progress;
  progress.printing = true;
  if (TakePrefix(s, kSdPrintingByte) && TakeNumber(s, progress.position) && TakePrefix(s, "/") &&
      TakeNumber(s, progress.size) && s.empty()) {
    return progress;
  }
  return std::nullopt;
}

}  // namespace

Line Classify(std::string_view text) {
  Line line;
  line.text = text;

  if (text == kOk || text.starts_with("ok ")) {
    line.kind = LineKind::kOk;
    line.temperatures = ParseTemperatures(text.substr(kOk.size()));
  } else if ((line.temperatures = ParseTemperatures(text))) {
    line.kind = LineKind::kTemperature;
  } else if (const auto resend = ParseResend(text)) {
    line.kind = LineKind::kResend;
    line.resend_from = *resend;
  } else if (text.starts_with(kError)) {
    line.kind = LineKind::kError;
  } else if (const auto sd = ParseSdProgress(text)) {
    line.kind = LineKind::kSdProgress;
    line.sd = *sd;
  }
  return line;
}

}  // namespace marlin
