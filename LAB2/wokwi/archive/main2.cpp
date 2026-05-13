/**
 * @file main2.cpp
 * @brief RUS Lab 2 - Variant 2: Environment Datalogger (real ESP32 hardware)
 *
 * Hardware reference implementation of the periodic-wake environment
 * datalogger. This is the sketch that would be flashed to physical
 * ESP32 silicon for an actual deployment; the matching Wokwi-friendly
 * Light Sleep demo lives in src/main1.cpp.
 *
 * Architecture:
 *   - The chip spends almost all its time in Deep Sleep, woken only by
 *     an RTC timer every SLEEP_INTERVAL_S. Each wake performs exactly
 *     one DHT22 acquisition and then immediately re-arms Deep Sleep.
 *   - Because Deep Sleep restarts the sketch on every wake (SRAM is
 *     powered down, setup() runs again), state that must persist across
 *     cycles lives in RTC slow memory via RTC_DATA_ATTR. That includes:
 *       * the 10-entry ring buffer,
 *       * the buffer write index,
 *       * the total-reading and boot counters,
 *       * the cumulative-uptime accumulator used to timestamp samples.
 *   - When the buffer reaches BUFFER_CAPACITY, it is printed in one
 *     contiguous block and reset before the next sleep.
 *
 * Why setup() does all the work and loop() is empty:
 *   esp_deep_sleep_start() does not return - it triggers a chip reset
 *   on wake. The Arduino entry point runs setup() then loop(), but
 *   since we never reach the end of setup(), the loop() function is
 *   never called. Keeping all logic in setup() also makes the cold-
 *   boot path and the wake path identical, which simplifies reasoning.
 *
 * Wokwi note:
 *   This file is intentionally excluded from the default PlatformIO
 *   build (see platformio.ini -> build_src_filter). To compile it use
 *   the dedicated environment:
 *       pio run -e esp32-realhw
 *   Running it under Wokwi works but the simulator will reset the chip
 *   on every wake; the boot banner reprints every interval, which is
 *   correct behaviour but visually noisy. It is also impossible to
 *   measure the actual power draw - that is what real hardware and an
 *   external current meter (or a tool like the ARM Energy Profiler) is
 *   for. Use src/main1.cpp (Light Sleep) for live Wokwi demos.
 *
 * Hardware (same diagram.json as src/main1.cpp):
 *   - DHT22 on GPIO15 (data) - VCC / GND / SDA
 *   - AWAKE LED (yellow) on GPIO14
 *   - ACTIVITY LED (red) on GPIO26
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
#define DHT_PIN        15
#define DHT_TYPE       DHT22
#define LED_ACT_PIN    26
#define LED_AWAKE_PIN  14

/* ------------------------------------------------------------------------- */
/*  Tunables                                                                 */
/* ------------------------------------------------------------------------- */
#define SLEEP_INTERVAL_S       60                              // 1 reading / minute
#define BUFFER_CAPACITY        10                              // matches the lab spec
#define ACTIVITY_PULSE_MS     200                              // visible blink length
#define DHT_RETRY_DELAY_MS    100                              // settle time between retries
#define DHT_BOOT_DELAY_MS     800                              // DHT22 needs ~1s after power-on

#define SLEEP_INTERVAL_US ((uint64_t)SLEEP_INTERVAL_S * 1000000ULL)

/* ------------------------------------------------------------------------- */
/*  State that survives Deep Sleep                                           */
/* ------------------------------------------------------------------------- */
struct Reading {
    uint32_t timestampS;     // approx. seconds since first power-on
    float    temperatureC;
    float    humidityPct;
};

// RTC_DATA_ATTR places these in the RTC slow memory which stays powered
// during Deep Sleep. Every boot re-runs setup(), so anything we need to
// remember across sleep cycles MUST live here.
RTC_DATA_ATTR static Reading  buffer[BUFFER_CAPACITY];
RTC_DATA_ATTR static uint8_t  bufferCount      = 0;
RTC_DATA_ATTR static uint32_t totalReadings    = 0;
RTC_DATA_ATTR static uint32_t bootCount        = 0;
RTC_DATA_ATTR static uint32_t uptimeS          = 0;  // cumulative sleep time

DHT dht(DHT_PIN, DHT_TYPE);

/* ------------------------------------------------------------------------- */
/*  Helpers                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief Map the ESP wake cause to a short human label for the report.
 */
static const char* wakeReasonText(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:    return "TIMER";
        case ESP_SLEEP_WAKEUP_EXT0:     return "EXT0";
        case ESP_SLEEP_WAKEUP_EXT1:     return "EXT1";
        case ESP_SLEEP_WAKEUP_TOUCHPAD: return "TOUCHPAD";
        case ESP_SLEEP_WAKEUP_ULP:      return "ULP";
        default:                        return "POWER-ON / RESET (cold boot)";
    }
}

/**
 * @brief Acquire one (temperature, humidity) pair with a single retry.
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
 * @brief Dump the 10 buffered samples in table form and clear the buffer.
 */
static void dumpAndReset() {
    Serial.println();
    Serial.println("================== BUFFER FULL: dumping 10 samples ==================");
    Serial.println(" idx | uptime (s) | temperature (C) | humidity (%) ");
    Serial.println("-----+------------+-----------------+--------------");
    for (uint8_t i = 0; i < BUFFER_CAPACITY; i++) {
        Serial.printf(" %3u | %10u | %15.1f | %12.1f\r\n",
                      (unsigned)i,
                      (unsigned)buffer[i].timestampS,
                      buffer[i].temperatureC,
                      buffer[i].humidityPct);
    }
    Serial.printf(" total samples: %u   |   boots: %u\r\n",
                  (unsigned)totalReadings, (unsigned)bootCount);
    Serial.println("=====================================================================");
    Serial.println();
    bufferCount = 0;
}

/* ------------------------------------------------------------------------- */
/*  setup() = the whole program                                              */
/* ------------------------------------------------------------------------- */
void setup() {
    /* STEP 1 - boot bookkeeping ----------------------------------------- */
    Serial.begin(115200);
    delay(100);
    bootCount++;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool coldBoot = (cause != ESP_SLEEP_WAKEUP_TIMER);

    if (coldBoot) {
        // Cold start - reset the accumulated uptime so timestamps make sense.
        uptimeS = 0;
    } else {
        // Add the interval we just slept through to the cumulative uptime.
        // This is approximate (esp_sleep_get_wakeup_cause does not give us
        // the actual slept duration), but for a logger that wakes on a
        // strict timer the error is bounded by RTC clock drift.
        uptimeS += SLEEP_INTERVAL_S;
    }

    Serial.println();
    Serial.printf("===== Datalogger boot #%u (%s) =====\r\n",
                  (unsigned)bootCount, wakeReasonText(cause));
    Serial.printf("uptime ~%u s, samples so far: %u, buffer fill: %u/%u\r\n",
                  (unsigned)uptimeS, (unsigned)totalReadings,
                  (unsigned)bufferCount, (unsigned)BUFFER_CAPACITY);

    /* STEP 2 - re-initialise peripherals (lost during Deep Sleep) ------- */
    pinMode(LED_AWAKE_PIN, OUTPUT);
    pinMode(LED_ACT_PIN,   OUTPUT);
    digitalWrite(LED_AWAKE_PIN, HIGH);
    digitalWrite(LED_ACT_PIN,   LOW);

    dht.begin();
    // DHT22 needs roughly one second after power-up before it returns
    // valid data. On a cold boot the LED-driven 800 ms gives it enough
    // settling time; on wake from Deep Sleep VCC stays on so this delay
    // is only paranoia, but it costs little and ensures the first
    // reading after a cold start is not a wasted retry.
    if (coldBoot) delay(DHT_BOOT_DELAY_MS);

    /* STEP 3 - take exactly one reading -------------------------------- */
    digitalWrite(LED_ACT_PIN, HIGH);
    float t, h;
    if (readDht(t, h)) {
        buffer[bufferCount] = { uptimeS, t, h };
        bufferCount++;
        totalReadings++;
        Serial.printf("[READ %u/%u] t=%.1f C, h=%.1f %%\r\n",
                      (unsigned)bufferCount, (unsigned)BUFFER_CAPACITY, t, h);
    } else {
        Serial.println("[ERROR] DHT22 read failed twice, skipping this cycle");
    }
    delay(ACTIVITY_PULSE_MS);
    digitalWrite(LED_ACT_PIN, LOW);

    /* STEP 4 - dump the buffer if it just filled up -------------------- */
    if (bufferCount >= BUFFER_CAPACITY) {
        dumpAndReset();
    }

    /* STEP 5 - arm timer wake and enter Deep Sleep --------------------- */
    Serial.printf("[SLEEP] entering Deep Sleep for %u s...\r\n",
                  (unsigned)SLEEP_INTERVAL_S);
    Serial.flush();
    digitalWrite(LED_AWAKE_PIN, LOW);

    esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
    esp_deep_sleep_start();  // -- never returns; chip wakes via reset --
}

/* ------------------------------------------------------------------------- */
/*  loop() is intentionally empty - setup() never returns                    */
/* ------------------------------------------------------------------------- */
void loop() { }
