/**
 * @file main.cpp
 * @brief RUS Lab 2 - Variant 1: Pametni postanski sanducic (event-driven)
 *
 * Scenario:
 *   The device sleeps almost all the time. When "mail" arrives (modeled
 *   as a pushbutton press) an external interrupt wakes the ESP32 from
 *   Deep Sleep. Once awake the firmware:
 *     1) logs the event to the serial monitor,
 *     2) performs a short processing step (increment counter stored in
 *        RTC memory, brief LED pulse),
 *     3) immediately returns to Deep Sleep.
 *
 *   Sleep mode  : ESP32 Deep Sleep (deepest mode required by the brief)
 *   Wake source : External interrupt EXT0 on the button pin (active LOW)
 *   Debouncing  : (a) on wake, sample pin until stably LOW for DEBOUNCE_MS;
 *                 (b) min-gap timer rejects events too close to previous;
 *                 (c) before re-arming sleep, wait until pin is stably HIGH
 *                     so a still-held button cannot re-trigger us.
 *
 * @author Ivan Bencic
 * @date 2026
 */

#include <Arduino.h>
#include <esp_sleep.h>
#include "driver/rtc_io.h"

/* ------------------------------------------------------------------------- */
/*  Pin map                                                                  */
/* ------------------------------------------------------------------------- */
// GPIO33 is RTC_GPIO_8, so it is allowed as an EXT0 wake source.
// EXT0 only works on RTC-capable GPIOs, hence the strict pin choice.
#define BTN_PIN        GPIO_NUM_33   // mail-arrival switch (active LOW)
#define LED_MAIL_PIN   GPIO_NUM_26   // pulses briefly to acknowledge an event
#define LED_AWAKE_PIN  GPIO_NUM_14   // lit while CPU is awake -> visualises sleep/wake

/* ------------------------------------------------------------------------- */
/*  Tunables                                                                 */
/* ------------------------------------------------------------------------- */
#define DEBOUNCE_MS                30   // how long the line must hold one level
#define DEBOUNCE_TIMEOUT_MS        80   // give up if it never settles
#define BUTTON_RELEASE_GUARD_MS   100   // must be HIGH this long before re-sleep
#define BUTTON_RELEASE_TIMEOUT_MS 5000  // safety cap if user holds the button
#define EVENT_LED_PULSE_MS        400   // visible blink length after each event
#define MIN_GAP_BETWEEN_EVENTS_MS 300   // ignore wakes too close to the previous one

/* ------------------------------------------------------------------------- */
/*  State that survives deep sleep                                           */
/* ------------------------------------------------------------------------- */
// RTC_DATA_ATTR places the variable in the RTC slow memory, which stays
// powered during Deep Sleep. After every wake the firmware boots from
// scratch (setup() runs again), so anything we want to remember across
// sleep cycles MUST live here.
RTC_DATA_ATTR uint32_t mailCount        = 0;   // total accepted mail events
RTC_DATA_ATTR uint32_t bootCount        = 0;   // total CPU boots (incl. wake-ups)
RTC_DATA_ATTR uint64_t lastEventEpochUs = 0;   // timestamp of previous event (us)

/* ------------------------------------------------------------------------- */
/*  Helpers                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief Print why the chip just woke up.
 *
 * Useful for the lab report's serial_output.txt: every line in the log
 * starts with the wake cause so the SLEEP -> WAKE -> EXEC -> SLEEP
 * cycle is clearly visible to the reviewer.
 */
void logWakeReason() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.print("[WAKE] reason: ");
    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0:     Serial.println("EXT0 (button)"); break;
        case ESP_SLEEP_WAKEUP_EXT1:     Serial.println("EXT1");          break;
        case ESP_SLEEP_WAKEUP_TIMER:    Serial.println("TIMER");         break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("TOUCHPAD");      break;
        case ESP_SLEEP_WAKEUP_ULP:      Serial.println("ULP");           break;
        default:                        Serial.println("POWER-ON / RESET (cold boot)"); break;
    }
}

/**
 * @brief Software debounce: poll the button until it has held one level
 *        continuously for stableMs (or timeoutMs elapses).
 *
 * Mechanical contacts emit dozens of fast edges during a single press.
 * By waiting for the line to stay at the requested level for a continuous
 * window we collapse that burst into a single logical event and prevent
 * the system from waking, processing and re-sleeping many times for one
 * physical action - which is exactly the "multiple interrupts" failure
 * mode described in the brief.
 */
bool waitForStableLevel(int level, uint32_t stableMs, uint32_t timeoutMs) {
    uint32_t start      = millis();
    uint32_t lastChange = start;
    int      lastRead   = digitalRead(BTN_PIN);
    while (millis() - start < timeoutMs) {
        int now = digitalRead(BTN_PIN);
        if (now != lastRead) {            // an edge: restart the stable window
            lastRead   = now;
            lastChange = millis();
        }
        if (now == level && (millis() - lastChange) >= stableMs) return true;
        delay(2);
    }
    return false;
}

/**
 * @brief Active phase: log the event and pulse the mail LED.
 *
 * Kept intentionally short (a few hundred milliseconds) because the
 * whole point of an event-driven design is to be awake for as little
 * time as possible. The longer we stay in active mode, the more energy
 * we waste on real hardware.
 */
void processMailEvent() {
    mailCount++;
    Serial.printf("[EVENT] mail #%u logged (boot #%u)\r\n",
                  (unsigned)mailCount, (unsigned)bootCount);

    // Brief visible feedback - this is the only "real work" we do.
    digitalWrite(LED_MAIL_PIN, HIGH);
    delay(EVENT_LED_PULSE_MS);
    digitalWrite(LED_MAIL_PIN, LOW);
}

/**
 * @brief Configure EXT0 wake source and enter Deep Sleep.
 *
 * Steps:
 *   1) Drain UART so the last log line actually leaves the chip.
 *   2) Turn off LEDs so they do not draw current during sleep.
 *   3) Keep the internal pull-up on the button line alive while sleeping
 *      (without it the line floats and can spuriously trigger EXT0).
 *   4) Tell the wake controller: "wake when BTN_PIN reads logic 0".
 *   5) Call esp_deep_sleep_start() - the CPU stops here. On the next
 *      wake the sketch restarts from setup().
 */
void goToDeepSleep() {
    Serial.println("[SLEEP] entering Deep Sleep, waiting for next event...");
    Serial.flush();

    digitalWrite(LED_AWAKE_PIN, LOW);   // visually mark "asleep"
    digitalWrite(LED_MAIL_PIN,  LOW);

    // Force the pull-up to be active during sleep on the wake pin.
    rtc_gpio_pullup_en(BTN_PIN);
    rtc_gpio_pulldown_dis(BTN_PIN);

    // EXT1: wake when BTN_PIN reads logic 0 (button pressed to GND).
    esp_sleep_enable_ext0_wakeup(BTN_PIN, 0);

    esp_deep_sleep_start();             // -- never returns --
}

/* ------------------------------------------------------------------------- */
/*  setup() = the whole program                                              */
/*                                                                           */
/*  Because Deep Sleep restarts the sketch on every wake, setup() acts as    */
/*  the main entry point for each cycle: handle wake -> run task -> sleep.   */
/* ------------------------------------------------------------------------- */
void setup() {
    /* STEP 1 - boot bookkeeping ------------------------------------------- */
    // Bring up serial and bump the boot counter (lives in RTC memory so it
    // accumulates across sleep cycles, giving us a free "how many wakes
    // since power-on" metric for the report).
    Serial.begin(115200);
    delay(100);                         // give USB-UART bridge time to settle
    bootCount++;
    Serial.println();
    Serial.println("===== Smart Mailbox (event-driven, Deep Sleep) =====");
    Serial.printf("Boot count: %u  |  Mail count so far: %u\r\n",
                  (unsigned)bootCount, (unsigned)mailCount);
    logWakeReason();

    /* STEP 2 - re-initialise GPIOs --------------------------------------- */
    // After Deep Sleep all non-RTC GPIO settings are lost, so pinMode()
    // must run on every boot. We also release any RTC-IO hold from the
    // previous sleep cycle in case it was applied.
    pinMode(LED_AWAKE_PIN, OUTPUT);
    pinMode(LED_MAIL_PIN,  OUTPUT);
    pinMode(BTN_PIN,       INPUT_PULLUP);
    digitalWrite(LED_AWAKE_PIN, HIGH);  // "I am awake" indicator
    digitalWrite(LED_MAIL_PIN,  LOW);

    /* STEP 3 - decide whether this wake is a real event ------------------ */
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool isButtonWake = (cause == ESP_SLEEP_WAKEUP_EXT0);

    if (isButtonWake) {
        /* STEP 3a - debounce: confirm the line is genuinely LOW.
         * If the wake was caused by contact bounce noise the line will
         * flicker back to HIGH within a few ms; we ignore those wakes. */
        if (!waitForStableLevel(LOW, DEBOUNCE_MS, DEBOUNCE_TIMEOUT_MS)) {
            Serial.println("[DEBOUNCE] noisy edge, ignoring this wake");
        } else {
            /* STEP 3b - min-gap filter: reject events that fall too close
             * to the previous one. esp_timer_get_time() returns micro-
             * seconds since boot; combined with the RTC-persisted last
             * timestamp this approximates a coarse cross-sleep clock. */
            uint64_t nowUs = esp_timer_get_time();
            if (nowUs - lastEventEpochUs >
                (uint64_t)MIN_GAP_BETWEEN_EVENTS_MS * 1000ULL) {
                lastEventEpochUs = nowUs;
                processMailEvent();     // -- the actual "task" --
            } else {
                Serial.println("[FILTER] event too close to previous, ignored");
            }
        }
    } else {
        // Cold start (power-on / reset). Nothing to log yet; just announce.
        Serial.println("[INIT] cold start, arming for first event");
    }

    /* STEP 4 - wait for the user to release the button -------------------- */
    // EXT0 wakes on level == LOW. If we re-enter sleep while the button
    // is still held, the chip wakes again immediately - effectively a
    // busy loop. So we block here until the line is stably HIGH (or the
    // safety timeout fires, in case something is shorted to GND).
    if (digitalRead(BTN_PIN) == LOW) {
        Serial.println("[GUARD] waiting for button release...");
        waitForStableLevel(HIGH, BUTTON_RELEASE_GUARD_MS,
                           BUTTON_RELEASE_TIMEOUT_MS);
    }

    esp_sleep_enable_timer_wakeup(10 * 1000000);

    /* STEP 5 - back to Deep Sleep ----------------------------------------- */
    goToDeepSleep();
}

/* ------------------------------------------------------------------------- */
/*  loop() is intentionally empty                                            */
/*                                                                           */
/*  setup() always ends in esp_deep_sleep_start(), so control never reaches  */
/*  here. The Arduino runtime still requires the symbol to exist.            */
/* ------------------------------------------------------------------------- */
void loop() { }
