#pragma once
#include <Arduino.h>
#include <vector>

// One log entry
struct UILogLine {
  uint32_t id;
  uint32_t ms;
  String   txt;
};

// Ring-buffer logger that mirrors to a secondary Print sink (Serial).
// Use the LOG() / LOGf() macros below — they forward to the global
// instance without the fragile #define Serial trick.
class UILogger : public Print {
public:
  explicit UILogger(size_t maxLines = 200) : _maxLines(maxLines) {}

  void attachMirror(Print* p) { _mirror = p; }

  size_t write(uint8_t c) override {
    if (_mirror) _mirror->write(c);
    if (c == '\n') _flush(); else _cur += (char)c;
    return 1;
  }

  size_t write(const uint8_t* b, size_t s) override {
    if (_mirror) _mirror->write(b, s);
    for (size_t i = 0; i < s; ++i) {
      char c = (char)b[i];
      if (c == '\n') _flush(); else _cur += c;
    }
    return s;
  }

  // Push a complete line (no newline needed)
  void pushLine(const String& s) { _cur += s; _flush(); }

  void collectTail(size_t n, std::vector<UILogLine>& out) const {
    if (_buf.empty()) return;
    size_t start = (_buf.size() > n) ? (_buf.size() - n) : 0;
    out.insert(out.end(), _buf.begin() + start, _buf.end());
  }

  void clear() { _buf.clear(); _lastId = 0; }

private:
  void _flush() {
    if (_cur.length() == 0) return;
    UILogLine L{ ++_lastId, millis(), _cur };
    _cur = String();
    if (_buf.size() >= _maxLines) _buf.erase(_buf.begin());
    _buf.push_back(std::move(L));
  }

  Print*   _mirror  = nullptr;
  String   _cur;
  std::vector<UILogLine> _buf;
  size_t   _maxLines;
  uint32_t _lastId = 0;
};

// Global instance — defined in Logger.cpp
extern UILogger UI_LOG;

// Initialise Serial + mirror + ESP-IDF log bridge — call first in setup()
void loggerInit();

// Safe logging macros — no #define Serial override needed
#define LOG(msg)       UI_LOG.println(msg)
#define LOGf(fmt, ...) do { char _b[256]; snprintf(_b, sizeof(_b), fmt, ##__VA_ARGS__); UI_LOG.println(_b); } while(0)