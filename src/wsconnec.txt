#include <WiFi.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>

#define DAC_PIN 25

// WiFi
const char* ssid = "NAA-TECHNO";
const char* password = "ZYSKQwz@#";

// WebSocket
WebSocketsClient webSocket;

// ====================== BUFFER ======================
#define BUFFER_SIZE 4096
uint8_t buffer[BUFFER_SIZE];        // Playback için (DAC)

volatile int writeIndex = 0;
volatile int readIndex = 0;

// ====================== I2S MIC ======================
#define I2S_PORT          I2S_NUM_0

// INMP441 pin bağlantısı (değiştirebilirsin)
#define I2S_SCK           32   // Serial Clock (BCLK)
#define I2S_WS            26   // Word Select  (LRCLK)  ← DAC_PIN ile çakışmasın!
#define I2S_SD            33   // Serial Data  (DOUT)

// Mikrofon için ayrı buffer
#define MIC_BUFFER_SIZE   512
int16_t micBuffer[MIC_BUFFER_SIZE];   // 16-bit okuma
uint8_t sendBuffer[MIC_BUFFER_SIZE];  // 8-bit gönderme

hw_timer_t *timer = NULL;

// ====================== TIMER (16kHz DAC) ======================
void IRAM_ATTR onTimer() {
  if (readIndex != writeIndex) {
    dacWrite(DAC_PIN, buffer[readIndex]);
    readIndex = (readIndex + 1) % BUFFER_SIZE;
  }
}

// ====================== WebSocket Event ======================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_BIN) {
    for (int i = 0; i < length; i++) {
      int next = (writeIndex + 1) % BUFFER_SIZE;
      if (next != readIndex) {
        buffer[writeIndex] = payload[i];
        writeIndex = next;
      }
    }
  }
}

// ====================== I2S Başlat ======================
void i2s_init() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,   // Mono (L/R pin GND'e bağlı olmalı)
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);

  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi baglandi");

  // WebSocket
  webSocket.begin("video.correcttechno.com", 2086, "/");
  webSocket.onEvent(webSocketEvent);

  // DAC Timer (16kHz)
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 62, true);   // ~16kHz
  timerAlarmEnable(timer);

  // I2S Mikrofon
  i2s_init();

  Serial.println("Ses gonderme ve alma basladi...");
}

// ====================== LOOP ======================
void loop() {
  webSocket.loop();

  // Mikrofondan veri oku
  size_t bytesRead = 0;
  esp_err_t result = i2s_read(I2S_PORT, micBuffer, sizeof(micBuffer), &bytesRead, portMAX_DELAY);

  if (result == ESP_OK && bytesRead > 0) {
    int samples = bytesRead / sizeof(int16_t);

    // 16-bit → 8-bit dönüşüm + WebSocket'e gönderme
    for (int i = 0; i < samples; i++) {
      // Basit dönüşüm: 16-bit'i 8-bit'e indir (orta seviyeyi 128 yap)
      sendBuffer[i] = (micBuffer[i] >> 8) + 128;
    }

    // Binary olarak gönder
    webSocket.sendBIN(sendBuffer, samples);
  }

  // Çok sık göndermemek için küçük gecikme (isteğe göre ayarla)
  delay(10);   // veya 0 yapıp test et
}