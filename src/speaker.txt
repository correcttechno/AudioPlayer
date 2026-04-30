#define DAC_PIN           25
#define HC12_RX_PIN       16
#define HC12_TX_PIN       17

#define BUFFER_SIZE       2048
uint8_t buffer[BUFFER_SIZE];
volatile int writeIndex = 0;
volatile int readIndex = 0;

hw_timer_t *timer = NULL;


// 8-bit u-Law'u 16-bit PCM'e geri çeviren tablo (Hız için)
const int16_t decode_uLaw_Table[256] = {
    -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956, // ... (Tablo devamı mantığı)
    // Sadeleştirme adına çalışma anında hesaplayan fonksiyonu kullanalım:
};

int16_t decode_uLaw(uint8_t u_val) {
    u_val = ~u_val;
    int t = ((u_val & 0x0F) << 3) + 0x84;
    t <<= (u_val & 0x70) >> 4;
    return ((u_val & 0x80) ? (0x84 - t) : (t - 0x84));
}

void IRAM_ATTR onTimer() {
    int availableData = (writeIndex >= readIndex) ? (writeIndex - readIndex) : (BUFFER_SIZE - readIndex + writeIndex);

    if (availableData > 150) { // Buffer eşiğini biraz artırdık
        int16_t decodedSample = decode_uLaw(buffer[readIndex]);
        // 16-bit veriyi DAC'ın anlayacağı 8-bit (0-255) aralığına çekiyoruz
        uint8_t dacVal = (decodedSample >> 8) + 128;
        dacWrite(DAC_PIN, dacVal);
        readIndex = (readIndex + 1) % BUFFER_SIZE;
    } else {
        dacWrite(DAC_PIN, 128); // Sessizlik hali
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