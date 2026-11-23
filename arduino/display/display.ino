#include <Arduino.h>
#include <BluetoothSerial.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// OLED config
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_MOSI  22
#define OLED_CLK   23
#define OLED_DC    18
#define OLED_CS    19
#define OLED_RESET 5
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
  OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
// -----------------------------------

BluetoothSerial SerialBT;

const int MAX_CORES = 16;

float usages[MAX_CORES];
float temps[MAX_CORES];
int   coreCount = 0;
int   tempCount = 0;

String inputBuffer = "";

void parseLine(const String &line) {
  int sep = line.indexOf(';');
  if (sep < 0) return;

  String usagePart = line.substring(0, sep);
  String tempPart  = line.substring(sep + 1);

  // Core load
  coreCount = 0;
  int start = 0;
  while (start < usagePart.length() && coreCount < MAX_CORES) {
    int end = usagePart.indexOf(',', start);
    if (end < 0) end = usagePart.length();
    String val = usagePart.substring(start, end);
    val.trim();
    if (val.length() > 0) {
      usages[coreCount++] = val.toFloat();
    }
    start = end + 1;
  }

  // Core temp
  tempCount = 0;
  start = 0;
  while (start < tempPart.length() && tempCount < MAX_CORES) {
    int end = tempPart.indexOf(',', start);
    if (end < 0) end = tempPart.length();
    String val = tempPart.substring(start, end);
    val.trim();
    if (val.length() > 0) {
      temps[tempCount++] = val.toFloat();
    }
    start = end + 1;
  }
}

void drawScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(0, 0);
  display.println(F("Core  Load   Temp"));

  // Core rows
  int y = 12;
  int maxRows = (coreCount > 4) ? 4 : coreCount;
  for (int i = 0; i < maxRows && i < MAX_CORES; i++) {
    char buf[32];

    // Core num
    snprintf(buf, sizeof(buf), "C%d", i);
    display.setCursor(0, y);
    display.println(buf);

    // Load
    snprintf(buf, sizeof(buf), "%3.0f%%", usages[i]);
    display.setCursor(30, y);
    display.println(buf);

    // Temp
    if (i < tempCount) {
      snprintf(buf, sizeof(buf), "%3.0fC", temps[i]);
      display.setCursor(85, y);
      display.println(buf);

    } else {
      display.setCursor(85, y);
      display.println(F(" --"));
    }

    y += 12;
  }

  display.display();
}

void setup() {
  Serial.begin(115200);

  // Init OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED init failed"));
    for (;;);
  }

  display.display();
  delay(2000);

  SerialBT.begin("ESP32-CPU-Monitor");
  Serial.println("Bluetooth has been started. Waiting for connection...");

  display.clearDisplay();  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println(F("Waiting for data..."));
  display.display();
}

void loop() {
  while (SerialBT.available()) {
    char c = SerialBT.read();
    Serial.println(c);
    if (c == '\n') {
      if (inputBuffer.length() > 0) {
        parseLine(inputBuffer);
        drawScreen();
        inputBuffer = "";
      }
    } else if (c != '\r') {
      inputBuffer += c;
      // Buffer overflow
      if (inputBuffer.length() > 256) inputBuffer = "";
    }
  }

  delay(10);
}
