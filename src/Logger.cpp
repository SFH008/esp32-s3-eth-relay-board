#include "Logger.h"
#include <esp_log.h>
#include <algorithm>

UILogger UI_LOG(200);

// Never wait for a USB host. The web log retains complete messages even when
// serial is disconnected or its TX buffer is full.
class SerialMirror : public Print {
public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (!Serial) return length;
    int room = Serial.availableForWrite();
    if (room > 0) Serial.write(data, std::min(length, (size_t)room));
    return length;
  }
};
static SerialMirror mirror;

extern "C" int ui_vprintf(const char* fmt, va_list ap) {
  char buf[256];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n <= 0) return n;
  // vsnprintf returns the untruncated length: never read n bytes from buf.
  String text(buf);
  text.replace("\r", "");
  int start = 0;
  for (unsigned int i = 0; i < text.length(); ++i) {
    if (text[i] == '\n') {
      UI_LOG.pushLine(text.substring(start, i)); start = i + 1;
    }
  }
  if ((unsigned int)start < text.length()) UI_LOG.pushLine(text.substring(start));
  return n;
}

void loggerInit() {
  UI_LOG.begin();
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  UI_LOG.attachMirror(&mirror);
  esp_log_set_vprintf(&ui_vprintf);
}
