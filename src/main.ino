#include <Arduino.h>
#define RXD2 16
#define TXD2 17
#define SET_PIN 4

HardwareSerial HC12(2);

// ROLE seçimi:
// 1 = Gönderici
// 0 = Alıcı
#define ROLE 0

unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);

  pinMode(SET_PIN, OUTPUT);
  digitalWrite(SET_PIN, HIGH); // normal mod (AT değil)

  HC12.begin(1200, SERIAL_8N1, RXD2, TXD2);

  Serial.println("HC-12 test basladi");

  delay(500);
}

void loop() {

  // =======================
  // GÖNDERİCİ MODU
  // =======================
#if ROLE == 1
  if (millis() - lastSend > 2000) {
    lastSend = millis();

    String msg = "Merhaba HC-12 -> " + String(millis());
    HC12.println(msg);

    Serial.println("Gonderildi: " + msg);
  }
#endif

  // =======================
  // ALICI MODU (ORTAK)
  // =======================
  if (HC12.available()) {
    String incoming = HC12.readString();

    Serial.print("Gelen veri: ");
    Serial.println(incoming);
  }
}