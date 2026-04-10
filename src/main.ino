#include <Arduino.h>
#include <driver/dac.h>
#include <driver/timer.h>

// ───────────── PİNLER ─────────────
#define UART_RX_PIN  16
#define UART_TX_PIN  17
#define SET_PIN       4
#define LED_PIN       2
#define DAC_PIN      25

// ───────────── PARAMETRELER ─────────────
#define SAMPLE_RATE  8000
#define HC12_BAUD    115200
#define RING_SIZE    4096
#define RING_MASK    (RING_SIZE - 1)

// ───────────── RING BUFFER ─────────────
volatile uint8_t  ring[RING_SIZE];
volatile uint16_t rHead = 0;
volatile uint16_t rTail = 0;

inline bool     ringEmpty()         { return rHead == rTail; }
inline uint16_t ringAvail()         { return (rHead - rTail) & RING_MASK; }
inline void     ringPush(uint8_t b) {
    uint16_t next = (rHead + 1) & RING_MASK;
    if (next != rTail) { ring[rHead] = b; rHead = next; }
}
inline uint8_t ringPop() {
    uint8_t b = ring[rTail];
    rTail = (rTail + 1) & RING_MASK;
    return b;
}

// ───────────── FİLTRE ─────────────
static int16_t dcX = 0, dcY = 0;
static float   lpY = 128.0f;
static uint8_t m1  = 128, m2 = 128;

IRAM_ATTR uint8_t dcBlock(uint8_t in) {
    int16_t x = (int16_t)in - 128;
    int16_t y = x - dcX + (int16_t)(0.995f * dcY);
    dcX = x; dcY = y;
    int16_t o = y + 128;
    if (o < 0)   o = 0;
    if (o > 255) o = 255;
    return (uint8_t)o;
}

IRAM_ATTR uint8_t median3(uint8_t a, uint8_t b, uint8_t c) {
    if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
    if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
    return c;
}

IRAM_ATTR uint8_t process(uint8_t raw) {
    uint8_t s = dcBlock(raw);
    s  = median3(m2, m1, s);
    m2 = m1; m1 = s;
    lpY = 0.35f * s + 0.65f * lpY;
    return (uint8_t)(lpY + 0.5f);
}

// ───────────── TIMER ISR — 125µs ─────────────
bool IRAM_ATTR onTimer(void*) {
    if (!ringEmpty())
        dac_output_voltage(DAC_CHANNEL_1, process(ringPop()));
    else
        dac_output_voltage(DAC_CHANNEL_1, (uint8_t)lpY);
    return false;
}

// ───────────── HC-12 BAUD AYARI ─────────────
void setupHC12() {
    Serial.println("HC-12 ayarlaniyor...");

    pinMode(SET_PIN, OUTPUT);
    digitalWrite(SET_PIN, LOW);   // AT modu
    delay(500);

    // Önce 9600 ile bağlan (fabrika değeri)
    HardwareSerial tmp(2);
    tmp.begin(9600, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    delay(200);

    tmp.print("AT+B115200");
    delay(500);

    String resp = "";
    while (tmp.available()) resp += (char)tmp.read();

    if (resp.indexOf("115200") >= 0) {
        Serial.println("✅ HC-12 baud 115200 ayarlandi!");
    } else {
        Serial.println("⚠️  HC-12 zaten 115200 veya cevap gelmedi.");
    }

    tmp.end();
    digitalWrite(SET_PIN, HIGH);  // Normal mod
    delay(300);
}

// ───────────── TIMER BAŞLATMA ─────────────
void initTimer() {
    timer_config_t cfg = {};
    cfg.alarm_en    = TIMER_ALARM_EN;
    cfg.counter_en  = TIMER_PAUSE;
    cfg.intr_type   = TIMER_INTR_LEVEL;
    cfg.counter_dir = TIMER_COUNT_UP;
    cfg.auto_reload = TIMER_AUTORELOAD_EN;
    cfg.divider     = 80;  // 80MHz / 80 = 1MHz → 1µs/tik

    timer_init(TIMER_GROUP_0, TIMER_0, &cfg);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 1000000UL / SAMPLE_RATE);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, onTimer, NULL, 0);
    timer_start(TIMER_GROUP_0, TIMER_0);
}

// ───────────── DURUM ─────────────
volatile bool          receiving = false;
volatile unsigned long lastDataMs = 0;
unsigned long          lastPrintMs = 0;

HardwareSerial hc12(2);

// ───────────── SETUP ─────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=== ESP32 HC-12 Alici ===");

    // 1. HC-12 baud ayarla
    setupHC12();

    // 2. LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // 3. DAC
    dac_output_enable(DAC_CHANNEL_1);
    dac_output_voltage(DAC_CHANNEL_1, 128);

    // 4. UART — begin'den ÖNCE buffer ayarla
    hc12.setRxBufferSize(2048);
    hc12.begin(HC12_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

    // 5. Timer ISR
    initTimer();

    Serial.printf("Baud: %d | Sample: %d Hz | Ring: %d B\n",
                  HC12_BAUD, SAMPLE_RATE, RING_SIZE);
    Serial.println("Hazir — gonderici bekleniyor...\n");
}

// ───────────── LOOP ─────────────
void loop() {
    // HC-12 → ring buffer
    while (hc12.available()) {
        ringPush(hc12.read());
        lastDataMs = millis();

        if (!receiving) {
            receiving = true;
            digitalWrite(LED_PIN, HIGH);
            Serial.println(">>> SES AKISI BASLADI");
        }
    }

    unsigned long now = millis();

    // Her 2sn durum yaz
    if (receiving && now - lastPrintMs > 2000) {
        uint16_t av = ringAvail();
        Serial.printf("Buffer: %u/%u B (%u%%) | DAC: %u\n",
                      av, RING_SIZE,
                      (av * 100U) / RING_SIZE,
                      (uint8_t)lpY);
        if (av < 128)
            Serial.println("UYARI: Buffer bos — ses kesilir!");
        lastPrintMs = now;
    }

    // 3sn sessizlik → akış bitti
    if (receiving && now - lastDataMs > 3000) {
        receiving = false;
        digitalWrite(LED_PIN, LOW);
        dcX = 0; dcY = 0; lpY = 128.0f; m1 = m2 = 128;
        Serial.println(">>> Akis durdu. Bekleniyor...\n");
        lastPrintMs = now;
    }

    // Bekleme göstergesi
    if (!receiving && now - lastPrintMs > 3000) {
        Serial.printf("Bekleniyor... (Ring: %u B)\n", ringAvail());
        lastPrintMs = now;
        digitalWrite(LED_PIN, HIGH);
        delay(30);
        digitalWrite(LED_PIN, LOW);
    }

    delay(1);
}