#include <M5Unified.h>

void setup() {
  auto config = M5.config();
  config.serial_baudrate = 115200;
  M5.begin(config);

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(4);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("Hello World", M5.Display.width() / 2,
                        M5.Display.height() / 2);
}

void loop() {
  Serial.println("Hello World");
  delay(1000);
}
