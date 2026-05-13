/**
 * @file main1.cpp
 * @brief RUS Lab 2 - Variant 2: Environment Datalogger (Light Sleep, Wokwi demo)
 *
 * Periodic-wake environment datalogger using ESP32 Light Sleep. This
 * is the default-built variant (3-second cycle) tuned for live demos
 * under Wokwi; an identical-mechanism alternative with a 30-second
 * cycle for production-style cadence lives in src/archive/main2.cpp.
 *
 * What the firmware does, end-to-end:
 *   1. setup() runs once: disables Wi-Fi, brings up the DHT22 and the two
 *      indicator LEDs, prints the banner.
 *   2. loop() reads one (temperature, humidity) sample from the DHT22 on
 *      GPIO15, appends it to a ring buffer of size BUFFER_CAPACITY (10,
 *      per spec), then calls esp_light_sleep_start() with a timer wake
 *      configured for SLEEP_INTERVAL_MS.
 *   3. Light Sleep pauses the CPU, gates most peripheral clocks, but
 *      keeps SRAM powered and the RTC running. The function RETURNS on
 *      wake, exactly at the next instruction - no chip reset, no banner
 *      reprint, no need for RTC_DATA_ATTR storage.
 *   4. When the buffer fills, all 10 samples are dumped in a single
 *      table and the buffer is reset.
 *
 * Why Light Sleep instead of Deep Sleep:
 *   Deep Sleep resets the chip on wake, which forces every piece of
 *   cross-cycle state into RTC slow memory (RTC_DATA_ATTR) and makes
 *   the boot banner reprint on every cycle. On Wokwi the reset behaviour
 *   is also entangled with watchdog quirks. Light Sleep keeps SRAM
 *   powered, returns from the sleep call normally, and produces a
 *   clean serial trace - one banner at cold boot, then [READ] / [SLEEP]
 *   / [WAKE] lines forever.
 *
 *   Power-wise Light Sleep is a step down from Deep Sleep (~0.8 mA vs
 *   ~10 uA on a bare module), but for this lab the demonstrated
 *   mechanism is the periodic-wake control flow, not the absolute
 *   current figure. The theoretical battery-life analysis in README.md
 *   covers both numbers.
 *
 * Wi-Fi:
 *   Arduino-ESP32 leaves the Wi-Fi radio in STA mode by default even if
 *   the application never calls WiFi.begin(). The RF front-end still
 *   draws tens of mA. setup() explicitly calls WiFi.mode(WIFI_OFF) so
 *   the active phase only pays for the CPU + sensor + LEDs.
 *
 * Hardware (see diagram.json):
 *   - DHT22 on GPIO15 (data) - VCC / GND / SDA
 *   - AWAKE LED (yellow) on GPIO14 - on while CPU is awake
 *   - ACTIVITY LED (red) on GPIO26 - pulses on every sensor read
 *
 * @author Ivan Bencic
 * @date 2026
 */

#include <Arduino.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include <DHT.h>

/* ------------------------------------------------------------------------- */
/*  Pin map                                                                  */
/* ------------------------------------------------------------------------- */
#define DHT_PIN        15            // single-wire data line to the DHT22
#define DHT_TYPE       DHT22
#define LED_ACT_PIN    26            // brief pulse on every sensor read
#define LED_AWAKE_PIN  14            // lit while CPU is awake (off in sleep)

/* ------------------------------------------------------------------------- */
/*  Tunables                                                                 */
/* ------------------------------------------------------------------------- */
#define SLEEP_INTERVAL_MS    3000    // 3 s per cycle (Wokwi-demo cadence)
#define BUFFER_CAPACITY        10    // matches the lab spec
#define ACTIVITY_PULSE_MS     200    // visible blink length per sensor read
#define DHT_RETRY_DELAY_MS    100    // settle time between DHT read retries
#define DHT_BOOT_DELAY_MS     800    // DHT22 needs ~1 s after power-up

/* ------------------------------------------------------------------------- */
/*  Storage                                                                  */
/*                                                                           */
/*  Light Sleep preserves SRAM, so plain statics are sufficient - no         */
/*  RTC_DATA_ATTR qualifiers required. The chip never resets between         */
/*  cycles; setup() runs once, loop() runs forever.                          */
/* ------------------------------------------------------------------------- */
struct Reading {
    uint32_t timestampMs;
    float    temperatureC;
    float    humidityPct;
};

static Reading  buffer[BUFFER_CAPACITY];
static uint8_t  bufferCount     = 0;
static uint32_t totalReadings   = 0;
static uint32_t dumpsCompleted  = 0;

DHT dht(DHT_PIN, DHT_TYPE);

/* ------------------------------------------------------------------------- */
/*  Helpers                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief Acquire one (temperature, humidity) sample with a single retry.
 */
static bool readDht(float& outT, float& outH) {
    outT = dht.readTemperature();
    outH = dht.readHumidity();
    if (!isnan(outT) && !isnan(outH)) return true;

    delay(DHT_RETRY_DELAY_MS);
    outT = dht.readTemperature();
    outH = dht.readHumidity();
    return !isnan(outT) && !isnan(outH);
}

/**
 * @brief Print the buffered samples as a table and clear the buffer.
 */
static void dumpAndReset() {
    Serial.println();
    Serial.println("================== BUFFER FULL: dumping 10 samples ==================");
    Serial.println(" idx | uptime (ms) | temperature (C) | humidity (%) ");
    Serial.println("-----+-------------+-----------------+--------------");
    for (uint8_t i = 0; i < BUFFER_CAPACITY; i++) {
        Serial.printf(" %3u | %11u | %15.1f | %12.1f\r\n",
                      (unsigned)i,
                      (unsigned)buffer[i].timestampMs,
                      buffer[i].temperatureC,
                      buffer[i].humidityPct);
    }
    dumpsCompleted++;
    Serial.printf(" total samples since boot: %u   |   dumps completed: %u\r\n",
                  (unsigned)totalReadings, (unsigned)dumpsCompleted);
    Serial.println("=====================================================================");
    Serial.println();
    bufferCount = 0;
}

/**
 * @brief One acquisition cycle: pulse ACTIVITY LED, read DHT, append.
 */
static void performReading() {
    digitalWrite(LED_ACT_PIN, HIGH);

    float t, h;
    if (!readDht(t, h)) {
        Serial.println("[ERROR] DHT22 read failed twice in a row, skipping this cycle");
        delay(ACTIVITY_PULSE_MS);
        digitalWrite(LED_ACT_PIN, LOW);
        return;
    }

    buffer[bufferCount] = { millis(), t, h };
    bufferCount++;
    totalReadings++;

    Serial.printf("[READ %u/%u] t=%.1f C, h=%.1f %%  (sample #%u since boot)\r\n",
                  (unsigned)bufferCount, (unsigned)BUFFER_CAPACITY,
                  t, h, (unsigned)totalReadings);

    delay(ACTIVITY_PULSE_MS);
    digitalWrite(LED_ACT_PIN, LOW);

    if (bufferCount >= BUFFER_CAPACITY) {
        dumpAndReset();
    }
}

/**
 * @brief Enter ESP32 Light Sleep for the given duration, then resume.
 *
 * Drives the AWAKE LED low, flushes the UART, arms the RTC timer, and
 * calls esp_light_sleep_start(). The call returns on timer wake with
 * SRAM intact and millis() still ticking; no chip reset occurs.
 */
static void lightSleep(uint32_t ms) {
    Serial.printf("[SLEEP] entering Light Sleep for %u ms...\r\n", (unsigned)ms);
    Serial.flush();
    digitalWrite(LED_AWAKE_PIN, LOW);

    esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
    esp_light_sleep_start();   // returns on wake; SRAM and millis() preserved

    digitalWrite(LED_AWAKE_PIN, HIGH);
    Serial.println("[WAKE]  timer expired");
}

/* ------------------------------------------------------------------------- */
/*  setup() - one-time init (Light Sleep does not reset the chip)            */
/* ------------------------------------------------------------------------- */
void setup() {
    Serial.begin(115200);
    delay(200);

    // Wi-Fi defaults to STA mode under Arduino-ESP32 even without WiFi.begin();
    // disabling the radio removes the RF current from the active phase.
    WiFi.mode(WIFI_OFF);

    pinMode(LED_AWAKE_PIN, OUTPUT);
    pinMode(LED_ACT_PIN,   OUTPUT);
    digitalWrite(LED_AWAKE_PIN, HIGH);
    digitalWrite(LED_ACT_PIN,   LOW);

    dht.begin();
    delay(DHT_BOOT_DELAY_MS);   // DHT22 settling time after power-up

    Serial.println();
    Serial.println("=====================================================================");
    Serial.println(" Environment Datalogger - Light Sleep, Wokwi-demo cadence (Variant 2)");
    Serial.println("=====================================================================");
    Serial.printf (" Sleep mode               : esp_light_sleep_start (timer wake)\r\n");
    Serial.printf (" Sleep interval per cycle : %u ms\r\n", (unsigned)SLEEP_INTERVAL_MS);
    Serial.printf (" Buffer capacity          : %u readings\r\n", (unsigned)BUFFER_CAPACITY);
    Serial.println(" Sensor                   : DHT22 on GPIO15 (single-wire)");
    Serial.println(" Wi-Fi radio              : disabled (WiFi.mode(WIFI_OFF))");
    Serial.println("=====================================================================");
}

/* ------------------------------------------------------------------------- */
/*  loop() - one read per cycle, then Light Sleep until the next             */
/* ------------------------------------------------------------------------- */
void loop() {
    performReading();
    lightSleep(SLEEP_INTERVAL_MS);
}
