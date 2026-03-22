#include "Logger.h"
#include <esp_log.h>

UILogger UI_LOG(200);

// Bridge ESP-IDF log output into UI_LOG
extern "C" int ui_vprintf(const char* fmt, va_list ap) {
  static String line;
  char buf[256];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n <= 0) return n;
  for (int i = 0; i < n; ++i) {
    char c = buf[i];
    if (c == '\n') { UI_LOG.pushLine(line); line = String(); }
    else if (c != '\r') line += c;
  }
  return n;
}

void loggerInit() {
  Serial.begin(115200);
  UI_LOG.attachMirror(&Serial);
  esp_log_set_vprintf(&ui_vprintf);
}
