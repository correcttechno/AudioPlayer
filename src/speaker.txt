#define DAC_PIN           25
#define HC12_RX_PIN       16
#define HC12_TX_PIN       17

#define BUFFER_SIZE       2048
uint8_t buffer[BUFFER_SIZE];
volatile int writeIndex = 0;
volatile int readIndex = 0;

hw_timer_t *timer = NULL;

// 8kHz için Timer (1.000.000 / 8000 = 125)
// Alıcı onTimer fonksiyonu (PCM Çalma)
void IRAM_ATTR onTimer() {
  // Buffer'da yeterli PCM verisi var mı kontrol et
  int availableData = (writeIndex >= readIndex) ? 
                      (writeIndex - readIndex) : 
                      (BUFFER_SIZE - readIndex + writeIndex);

  // Buffer'da en az 100 byte birikmeden çalma (Cızırtıyı önlemek için önbellekleme)
  if (availableData > 100) { 
    dacWrite(DAC_PIN, buffer[readIndex]); // PCM değerini DAC'a bas[cite: 2]
    readIndex = (readIndex + 1) % BUFFER_SIZE;
  } else {
    // Veri yetersizse hoparlörü merkez değerde (sessiz) tut
    dacWrite(DAC_PIN, 128); 
  }
}

void setup() {
  Serial.begin(115200);
  // HC-12 hızını 115200'e çıkardık
  Serial2.begin(115200, SERIAL_8N1, HC12_RX_PIN, HC12_TX_PIN);

  // Timer ayarı: 80 prescaler (1MHz), 125 count = 8kHz
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 125, true); // 125 mikrosaniyede bir tetiklen
  timerAlarmEnable(timer);

  Serial.println("Alici Hazir (8kHz)");
}

void loop() {
  while (Serial2.available()) {
    uint8_t incoming = Serial2.read();
    int next = (writeIndex + 1) % BUFFER_SIZE;
    if (next != readIndex) {
      buffer[writeIndex] = incoming;
      writeIndex = next;
    }
  }
  // CPU'yu boğmamak için çok küçük bir bekleme
  yield(); 
}