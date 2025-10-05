#include <Arduino.h>
#include <SPI.h>
#include <ACAN_ESP32.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ====== PINS ======
// CAN (TJA1050)
static const gpio_num_t CAN_TX = GPIO_NUM_17;  // -> TJA1050.TXD
static const gpio_num_t CAN_RX = GPIO_NUM_16;  // <- TJA1050.RXD via diviseur 5V->3.3V

// OLED SH1106 SPI
static const int OLED_SCK  = 18;
static const int OLED_MOSI = 23;
static const int OLED_DC   = 22;
static const int OLED_RST  = 21;
static const int OLED_CS   = 15;

// Bouton (vers GND)
static const uint8_t BTN_PIN = 14; // INPUT_PULLUP

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RST, OLED_CS);

// ====== CAN ======
static const uint32_t CAN_BITRATE = 500UL * 1000UL;
static const uint32_t CAN_ID_BTN  = 0x301;  // 1 byte: LED ON/OFF
static const uint32_t CAN_ID_POT  = 0x302;  // 2 bytes LE: 0..4095

// ====== État ======
static uint16_t potRaw = 0;    // reçu de B
static bool     ledOn  = false;

// Anti-rebond bouton
static const uint32_t DEBOUNCE_MS = 40;
static bool lastLevel = true, stableLevel = true;
static uint32_t lastChangeMs = 0;
static uint32_t lastRedrawMs = 0;

// ====== Fonctions ======
static void drawScreen() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Industrie 4.0 TP1");

  int percent = (int)((potRaw * 100UL + 2047) / 4095UL);
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print("Pot: ");
  display.print(potRaw);
  display.print(" (");
  display.print(percent);
  display.println("%)");

  display.setCursor(0, 40);
  display.print("LED: ");
  display.println(ledOn ? "ON " : "OFF");

  display.display();
}

static void sendLedCommand(uint8_t onOff) {
  CANMessage m;
  m.id = CAN_ID_BTN;
  m.len = 1;
  m.data[0] = onOff;
  ACAN_ESP32::can.tryToSend(m);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== A: OLED + Bouton ===");

  pinMode(BTN_PIN, INPUT_PULLUP);

  // OLED
  SPI.begin(OLED_SCK, -1, OLED_MOSI, OLED_CS);
  SPI.setFrequency(8000000);
  if (!display.begin()) {
    Serial.println("Erreur init OLED");
    while (true) delay(1000);
  }
  display.clearDisplay(); display.display();

  // CAN
  ACAN_ESP32_Settings settings(CAN_BITRATE);
  settings.mTxPin = CAN_TX;
  settings.mRxPin = CAN_RX;
  uint32_t err = ACAN_ESP32::can.begin(settings);
  if (err != 0) {
    Serial.print("CAN init error 0x"); Serial.println(err, HEX);
    while (true) delay(1000);
  }
  Serial.println("CAN up.");
}

void loop() {
  // RX : potentiomètre reçu
  CANMessage m;
  while (ACAN_ESP32::can.receive(m)) {
    if (m.id == CAN_ID_POT && m.len >= 2) {
      potRaw = (uint16_t)(m.data[0] | (m.data[1] << 8));
    }
  }

  // Lecture bouton
  bool level = digitalRead(BTN_PIN);
  uint32_t now = millis();
  if (level != lastLevel) { lastLevel = level; lastChangeMs = now; }
  if ((now - lastChangeMs) > DEBOUNCE_MS && level != stableLevel) {
    stableLevel = level;
    bool pressed = (stableLevel == LOW);
    ledOn = pressed;
    sendLedCommand(pressed ? 1 : 0);
    Serial.println(pressed ? "BTN -> LED ON" : "BTN -> LED OFF");
  }

  // Rafraîchissement OLED
  if (millis() - lastRedrawMs >= 100) {
    lastRedrawMs = millis();
    drawScreen();
  }
}
