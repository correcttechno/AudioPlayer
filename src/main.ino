#include <driver/i2s.h>

// --- PİNLER ---
#define PTT_BUTTON_PIN    4    // Bas-Konuş Düğmesi (GND'ye çekilmeli)
#define DAC_PIN           25   // Hoparlör Çıkışı
#define I2S_SCK           32
#define I2S_WS            26
#define I2S_SD            33
#define HC12_RX_PIN       16
#define HC12_TX_PIN       17

// --- AYARLAR ---
#define SAMPLE_RATE       16000 // Mikrofon okuma hızı
#define BUFFER_SIZE       2048
#define MIC_BUFFER_SIZE   128

// --- DEĞİŞKENLER ---
uint8_t receiveBuffer[BUFFER_SIZE];
volatile int writeIndex = 0;
volatile int readIndex = 0;
int16_t micBuffer[MIC_BUFFER_SIZE];
uint8_t sendBuffer[MIC_BUFFER_SIZE];
hw_timer_t *timer = NULL;

// --- u-Law ENCODE (Sıkıştırma) ---
uint8_t encode_uLaw(int16_t pcm_val) {
    #define BIAS 0x84
    #define CLIP 32635
    int mask, exponent, mantissa, seg;
    uint8_t uval;
    pcm_val = (pcm_val < 0) ? (mask = 0x7F, -pcm_val) : (mask = 0xFF, pcm_val);
    if (pcm_val > CLIP) pcm_val = CLIP;
    pcm_val += BIAS;
    for (exponent = 0, seg = 0x100; pcm_val > seg; exponent++, seg <<= 1);
    mantissa = (pcm_val >> (exponent + 3)) & 0x0F;
    uval = (exponent << 4) | mantissa;
    return (uval ^ mask);
}

// --- u-Law DECODE (Çözme) ---
int16_t decode_uLaw(uint8_t u_val) {
    u_val = ~u_val;
    int t = ((u_val & 0x0F) << 3) + 0x84;
    t <<= (u_val & 0x70) >> 4;
    return ((u_val & 0x80) ? (0x84 - t) : (t - 0x84));
}

// --- TIMER INTERRUPT (Ses Çalma) ---
void IRAM_ATTR onTimer() {
    // Sadece PTT'ye basılmıyorsa (yani dinleme modundaysak) çal
    if (digitalRead(PTT_BUTTON_PIN) == HIGH) {
        int availableData = (writeIndex >= readIndex) ? (writeIndex - readIndex) : (BUFFER_SIZE - readIndex + writeIndex);
        if (availableData > 150) { 
            int16_t decodedSample = decode_uLaw(receiveBuffer[readIndex]);
            uint8_t dacVal = (decodedSample >> 8) + 128;
            dacWrite(DAC_PIN, dacVal);
            readIndex = (readIndex + 1) % BUFFER_SIZE;
        } else {
            dacWrite(DAC_PIN, 128); // Sessiz
        }
    } else {
        dacWrite(DAC_PIN, 128); // Konuşurken hoparlörü sustur (Eko önleme)
    }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, HC12_RX_PIN, HC12_TX_PIN);
  
  pinMode(PTT_BUTTON_PIN, INPUT_PULLUP); // Dahili Pull-up ile düğme girişi

  // I2S Mikrofon Başlatma
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 128,
    .use_apll = false
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);

  // DAC Zamanlayıcı Başlatma (8kHz Çalma)
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 125, true); 
  timerAlarmEnable(timer);

  Serial.println("Telsiz Hazir! Dinleme modunda...");
}

void loop() {
  // --- MOD SEÇİMİ ---
  if (digitalRead(PTT_BUTTON_PIN) == LOW) {
    // KONUŞMA MODU (VERİCİ)
    size_t bytesRead = 0;
    esp_err_t result = i2s_read(I2S_NUM_0, micBuffer, sizeof(micBuffer), &bytesRead, 0);

    if (result == ESP_OK && bytesRead > 0) {
      int samples = bytesRead / sizeof(int16_t);
      int j = 0;
      for (int i = 0; i < samples; i += 2) { // Downsampling (8kHz'e düşür)
        sendBuffer[j++] = encode_uLaw(micBuffer[i]);
      }
      Serial2.write(sendBuffer, j); // HC-12 ile gönder
    }
    // Konuşurken gelen veriyi temizle ki düğmeyi bırakınca eski sesler çalmasın
    while(Serial2.available()) Serial2.read();
    readIndex = writeIndex; 
  } 
  else {
    // DİNLEME MODU (ALICI)
    while (Serial2.available()) {
      uint8_t incoming = Serial2.read();
      int next = (writeIndex + 1) % BUFFER_SIZE;
      if (next != readIndex) {
        receiveBuffer[writeIndex] = incoming;
        writeIndex = next;
      }
    }
  }
  yield();
}