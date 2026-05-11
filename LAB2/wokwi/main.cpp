/**
 * @file main.cpp
 * @brief RUS Lab 2 - Variant 2: Environment Datalogger (Wokwi demo with real Deep Sleep)
 *
 * Wokwi-friendly variant of the periodic-wake environment datalogger.
 * The matching pure-hardware sketch (true ESP32 Deep Sleep, longer
 * interval, no emulated phase) lives in src/archive/main1.cpp.
 *
 * What the firmware does, end-to-end:
 *   1. Read temperature + humidity from a DHT22 on GPIO15, append the
 *      sample to a ring buffer of size BUFFER_CAPACITY (10, per spec).
 *   2. When the buffer fills, print all 10 samples in a single table
 *      dump and clear the buffer.
 *   3. Run an "emulated sleep" window (SLEEP_INTERVAL_MS via delay()):
 *      AWAKE LED off, serial quiet. This is the part the audience sees.
 *   4. Call the real esp_deep_sleep_start() for a brief
 *      REAL_DEEP_SLEEP_MS at the end of every cycle. The chip actually
 *      enters Deep Sleep, the simulator resets it on wake, and the
 *      cycle repeats from setup(). RTC memory (RTC_DATA_ATTR) carries
 *      the buffer + counters across the reset, exactly as on real
 *      hardware.
 *
 * Why hybrid (emulated + real)?
 *   Calling esp_deep_sleep_start() for the full visible interval would
 *   make the demo noisy because Wokwi reprints the boot banner on every
 *   wake. Calling only delay() never exercises the real Deep Sleep API.
 *   The hybrid keeps the audience-visible "sleep" phase clean while
 *   still proving the real ESP32 power-management path works in
 *   simulation: every cycle ends with a genuine esp_deep_sleep_start()
 *   call, a chip reset, and a one-line [BOOT #n via TIMER] message
 *   followed immediately by the next reading.
 *
 *   Light Sleep is deliberately NOT used: Arduino-ESP32 ships without
 *   CONFIG_PM_ENABLE, so esp_light_sleep_start() from a user task
 *   desyncs the FreeRTOS idle-task spinlocks on the other core and
 *   reliably asserts inside spinlock_acquire() on the second cycle.
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
#define SLEEP_INTERVAL_MS    3000    // visible emulated-sleep window per cycle
#define REAL_DEEP_SLEEP_MS   1000    // brief actual Deep Sleep at end of each cycle
#define BUFFER_CAPACITY        10    // matches the lab spec
#define ACTIVITY_PULSE_MS     200    // visible blink length per sensor read
#define DHT_RETRY_DELAY_MS    100    // settle time between DHT read retries

/* ------------------------------------------------------------------------- */
/*  Storage                                                                  */
/* ------------------------------------------------------------------------- */
struct Reading {
    uint32_t timestampMs;    // monotonically-increasing-across-cycles timestamp
    float    temperatureC;
    float    humidityPct;
};

DHT dht(DHT_PIN, DHT_TYPE);

// RTC_DATA_ATTR places these in the RTC slow memory which is retained
// across the Deep Sleep reset at the end of every cycle. Without this
// qualifier the buffer and counters would be reset on every wake.
RTC_DATA_ATTR static Reading  buffer[BUFFER_CAPACITY];
RTC_DATA_ATTR static uint8_t  bufferCount      = 0;   // valid entries currently in buffer
RTC_DATA_ATTR static uint32_t totalReadings    = 0;   // accepted samples ever
RTC_DATA_ATTR static uint32_t dumpsCompleted   = 0;   // number of times the buffer was flushed
RTC_DATA_ATTR static uint32_t bootCount        = 0;   // CPU boots since power-on
RTC_DATA_ATTR static uint32_t accumulatedMs    = 0;   // cumulative cycle time across boots

/* ------------------------------------------------------------------------- */
/*  Helpers                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief Acquire a single (temperature, humidity) sample from the DHT22.
 *
 * DHT22 transactions are flaky by design (single-wire timing-sensitive
 * protocol over a slow bus). On a NaN result we wait DHT_RETRY_DELAY_MS
 * and try once more; two failures in a row are reported and skipped.
 *
 * @return true if a valid sample was written through the out parameters.
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
 *
 * Called automatically once the buffer reaches BUFFER_CAPACITY. The
 * print routine is deliberately self-contained so the lab report's
 * serial_output.txt can be produced by copying a single contiguous
 * block of serial text.
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

    // Timestamp the sample with a monotonically-increasing counter that
    // survives the Deep Sleep reset; millis() alone resets every cycle.
    buffer[bufferCount] = { accumulatedMs + millis(), t, h };
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
 * @brief Emulated sleep window with audience-visible cues.
 *
 * Drives the AWAKE LED low, prints a [SLEEP] line, and blocks for the
 * requested duration via delay(). On return, drives the AWAKE LED
 * back high and prints a [WAKE] line. The CPU is technically not in a
 * low-power state - see the file header for why this is acceptable for
 * a Wokwi demo.
 */
static void emulatedSleep(uint32_t ms) {
    Serial.printf("[SLEEP] entering simulated sleep for %u ms...\r\n", (unsigned)ms);
    Serial.flush();
    digitalWrite(LED_AWAKE_PIN, LOW);
    delay(ms);
    digitalWrite(LED_AWAKE_PIN, HIGH);
    Serial.println("[WAKE]  timer expired");
}

/* ------------------------------------------------------------------------- */
/*  setup() - re-runs on every Deep Sleep wake; banner only on cold boot     */
/* ------------------------------------------------------------------------- */
void setup() {
    Serial.begin(115200);
    delay(200);

    bootCount++;
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const bool fromDeepSleep = (cause == ESP_SLEEP_WAKEUP_TIMER);

    pinMode(LED_AWAKE_PIN, OUTPUT);
    pinMode(LED_ACT_PIN,   OUTPUT);
    digitalWrite(LED_AWAKE_PIN, HIGH);
    digitalWrite(LED_ACT_PIN,   LOW);

    dht.begin();

    if (fromDeepSleep) {
        // Compact re-wake message - the audience already saw the full
        // banner on the cold boot and does not need it on every cycle.
        Serial.printf("\r\n[BOOT #%u via TIMER] buffer %u/%u, total samples %u, dumps %u\r\n",
                      (unsigned)bootCount,
                      (unsigned)bufferCount, (unsigned)BUFFER_CAPACITY,
                      (unsigned)totalReadings, (unsigned)dumpsCompleted);
    } else {
        // Cold boot - reset cross-cycle counters and print the full banner.
        accumulatedMs = 0;
        Serial.println();
        Serial.println("=====================================================================");
        Serial.println(" Environment Datalogger - Wokwi demo variant (Variant 2)");
        Serial.println(" [build: emulated visible sleep + brief real Deep Sleep]");
        Serial.println("=====================================================================");
        Serial.printf (" Visible (emulated) sleep : %u ms per cycle (delay)\r\n",
                       (unsigned)SLEEP_INTERVAL_MS);
        Serial.printf (" Real Deep Sleep at end   : %u ms (chip resets, RTC memory survives)\r\n",
                       (unsigned)REAL_DEEP_SLEEP_MS);
        Serial.printf (" Buffer capacity          : %u readings\r\n", (unsigned)BUFFER_CAPACITY);
        Serial.println(" Sensor                   : DHT22 on GPIO15 (single-wire)");
        Serial.println(" Hardware-only Deep Sleep variant: src/archive/main1.cpp.");
        Serial.println("=====================================================================");
    }
}

/* ------------------------------------------------------------------------- */
/*  loop() - one read + visible sleep + real Deep Sleep                      */
/*                                                                           */
/*  esp_deep_sleep_start() never returns, so loop() effectively runs once    */
/*  per boot cycle: on Deep Sleep wake the chip resets and the Arduino       */
/*  runtime calls setup() -> loop() once again. RTC_DATA_ATTR storage        */
/*  carries the buffer + counters across the reset.                          */
/* ------------------------------------------------------------------------- */
void loop() {
    performReading();
    emulatedSleep(SLEEP_INTERVAL_MS);

    // Account for the time this cycle consumed so timestamps in the
    // table dump are monotonic across the Deep Sleep reset.
    accumulatedMs += SLEEP_INTERVAL_MS + REAL_DEEP_SLEEP_MS;

    Serial.printf("[POWER] entering real Deep Sleep for %u ms (chip will reset on wake)...\r\n",
                  (unsigned)REAL_DEEP_SLEEP_MS);
    Serial.flush();
    digitalWrite(LED_AWAKE_PIN, LOW);

    esp_sleep_enable_timer_wakeup((uint64_t)REAL_DEEP_SLEEP_MS * 1000ULL);
    esp_deep_sleep_start();   // -- never returns; setup() runs again on wake --
}
