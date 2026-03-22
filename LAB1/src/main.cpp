/**
 * @file main.cpp
 * @brief Traffic light control system with multiple interrupts and priorities
 *
 * @details Demonstrates hardware interrupts, timer interrupts, and ultrasonic
 *          distance sensor for controlling two traffic lights at a simulated
 *          intersection.
 *          Traffic Light A (north-south) is driven by a low-priority timer interrupt.
 *          Traffic Light B (east-west) has an HC-SR04 sensor that triggers the
 *          highest-priority override. A button at medium priority toggles both
 *          traffic lights into blinking yellow mode. Serial commands provide
 *          emergency mode and status reporting.
 *
 *          Interrupt priorities (highest to lowest):
 *            1. Button           - blinking yellow for both (GPIO ISR)
 *            2. Distance sensor  - forces opposite state on TL-B (polled)
 *            3. Timer            - normal state machine cycle (hw timer ISR)
 *
 *          Project for RUS-TVZ.
 *
 * @author Ivan Benčić
 * @date 2026
 * @version 2.4
 */

#include <Arduino.h>

/* ========================================================================== */
/*                            PIN DEFINITIONS                                 */
/* ========================================================================== */

/**
 * @defgroup pins GPIO Pin Definitions
 * @brief Pin assignments for LEDs, button, and sensor
 * @{
 */

/** @name Traffic Light A - main light (north-south) */
///@{
#define TL_A_RED    14  ///< GPIO pin for Traffic Light A red LED
#define TL_A_YELLOW 27  ///< GPIO pin for Traffic Light A yellow LED
#define TL_A_GREEN  26  ///< GPIO pin for Traffic Light A green LED
///@}

/** @name Traffic Light B - secondary light with sensor (east-west) */
///@{
#define TL_B_RED    19  ///< GPIO pin for Traffic Light B red LED
#define TL_B_YELLOW 23  ///< GPIO pin for Traffic Light B yellow LED
#define TL_B_GREEN  13  ///< GPIO pin for Traffic Light B green LED
///@}

/** @name Button and sensor */
///@{
#define BUTTON_PIN   25  ///< GPIO pin for blinking yellow toggle button
#define TRIG_PIN      4  ///< GPIO pin for HC-SR04 TRIG
#define ECHO_PIN      5  ///< GPIO pin for HC-SR04 ECHO
///@}

/** @} */ // end defgroup pins

/* ========================================================================== */
/*                        CONFIGURATION PARAMETERS                            */
/* ========================================================================== */

/**
 * @defgroup config Configuration Parameters
 * @brief Constants for debounce, distance threshold, state durations, etc.
 * @{
 */
#define DEBOUNCE_DELAY         200   ///< Button debounce time in ms
#define ALARM_DISTANCE          20   ///< Distance threshold for sensor alarm (cm)
#define BLINK_INTERVAL         500   ///< Blinking yellow toggle interval (ms)
#define DISTANCE_OVERRIDE_MS  5000   ///< Duration of sensor override (ms)
#define DISTANCE_PRINT_INTERVAL 1000 ///< Interval lenght between prints (ms)
/** @} */

/* ========================================================================== */
/*                           STATE ENUMERATION                                */
/* ========================================================================== */

/**
 * @enum TrafficLightState
 * @brief Traffic light state machine states (European cycle)
 *
 * Cycle: RED -> RED_YELLOW -> GREEN -> YELLOW -> RED ...
 */
enum TrafficLightState {
    STATE_RED,         ///< Red light - stop
    STATE_RED_YELLOW,  ///< Red + yellow - preparing for green
    STATE_GREEN,       ///< Green light - go
    STATE_YELLOW       ///< Yellow light - preparing for red
};

/**
 * @brief Duration of each state in timer ticks (1 tick = 1 second)
 */
static const int STATE_DURATION[] = {5, 2, 5, 2};

/* ========================================================================== */
/*                         TRAFFIC LIGHT CLASS                                */
/* ========================================================================== */

/**
 * @class TrafficLight
 * @brief Represents a single traffic light with three LEDs
 *
 * @details Encapsulates GPIO pins for red, yellow, and green LEDs and
 *          implements a state machine for traffic light cycle control.
 *          Each traffic light has its own tick counter and state.
 */
class TrafficLight {
private:
    int _pinR;          ///< GPIO pin for red LED
    int _pinY;          ///< GPIO pin for yellow LED
    int _pinG;          ///< GPIO pin for green LED
    const char *_name;  ///< Name for serial output identification

public:
    TrafficLightState state;  ///< Current state machine state
    int tickCounter;          ///< Tick counter for tracking state duration

    /**
     * @brief Constructor - defines LED pins and name
     * @param pinR GPIO pin for red LED
     * @param pinY GPIO pin for yellow LED
     * @param pinG GPIO pin for green LED
     * @param name Name for serial monitor output
     */
    TrafficLight(int pinR, int pinY, int pinG, const char *name)
        : _pinR(pinR), _pinY(pinY), _pinG(pinG), _name(name),
          state(STATE_RED), tickCounter(0) {}

    /**
     * @brief Initialize GPIO pins as digital outputs
     */
    void init() {
        pinMode(_pinR, OUTPUT);
        pinMode(_pinY, OUTPUT);
        pinMode(_pinG, OUTPUT);
        applyState();
    }

    /**
     * @brief Apply current state to the LEDs
     * @details Turns on/off appropriate LEDs according to current state
     */
    void applyState() {
        switch (state) {
            case STATE_RED:
                digitalWrite(_pinR, HIGH);
                digitalWrite(_pinY, LOW);
                digitalWrite(_pinG, LOW);
                break;
            case STATE_RED_YELLOW:
                digitalWrite(_pinR, HIGH);
                digitalWrite(_pinY, HIGH);
                digitalWrite(_pinG, LOW);
                break;
            case STATE_GREEN:
                digitalWrite(_pinR, LOW);
                digitalWrite(_pinY, LOW);
                digitalWrite(_pinG, HIGH);
                break;
            case STATE_YELLOW:
                digitalWrite(_pinR, LOW);
                digitalWrite(_pinY, HIGH);
                digitalWrite(_pinG, LOW);
                break;
        }
    }

    /**
     * @brief Advance to the next state in the cycle
     * @details Cycles: RED -> RED_YELLOW -> GREEN -> YELLOW -> RED ...
     *          Resets the tick counter and prints the new state.
     */
    void nextState() {
        state = static_cast<TrafficLightState>((state + 1) % 4);
        tickCounter = 0;
        applyState();
        Serial.printf("[%s] New state: %s\r\n", _name, stateToString());
    }

    /**
     * @brief Set the traffic light to a specific state
     * @param s Desired state
     */
    void setState(TrafficLightState s) {
        state = s;
        tickCounter = 0;
        applyState();
    }

    /**
     * @brief Turn off all LEDs
     */
    void allOff() {
        digitalWrite(_pinR, LOW);
        digitalWrite(_pinY, LOW);
        digitalWrite(_pinG, LOW);
    }

    /**
     * @brief Control only the yellow LED (for blinking mode)
     * @param on true = yellow on, false = yellow off
     */
    void setYellow(bool on) {
        digitalWrite(_pinR, LOW);
        digitalWrite(_pinY, on ? HIGH : LOW);
        digitalWrite(_pinG, LOW);
    }

    /**
     * @brief Get text representation of current state
     * @return C string with state name
     */
    const char *stateToString() const {
        switch (state) {
            case STATE_RED:        return "RED";
            case STATE_RED_YELLOW: return "RED+YELLOW";
            case STATE_GREEN:      return "GREEN";
            case STATE_YELLOW:     return "YELLOW";
            default:               return "UNKNOWN";
        }
    }

    /**
     * @brief Check if the current state duration has expired
     * @return true if tick counter >= defined state duration
     */
    bool isStateExpired() const {
        return tickCounter >= STATE_DURATION[state];
    }

    /**
     * @brief Get the traffic light name
     * @return Pointer to name string
     */
    const char *getName() const { return _name; }
};

/* ========================================================================== */
/*                          GLOBAL VARIABLES                                  */
/* ========================================================================== */

/**
 * @name Traffic light instances
 * @{
 */
TrafficLight trafficLightA(TL_A_RED, TL_A_YELLOW, TL_A_GREEN, "TL-A");
TrafficLight trafficLightB(TL_B_RED, TL_B_YELLOW, TL_B_GREEN, "TL-B");
/** @} */

/**
 * @name Interrupt flags (volatile - accessed from ISRs)
 * @{
 */
volatile bool timerFlag = false;            ///< Timer interrupt triggered
volatile bool buttonFlag = false;           ///< Button pressed
volatile unsigned long lastButtonTime = 0;  ///< Last button press time (debounce)
/** @} */

/**
 * @name Interrupt counters (volatile - accessed from ISRs)
 * @{
 */
volatile uint32_t timerIntCount = 0;   ///< Total timer interrupt count
volatile uint32_t buttonIntCount = 0;  ///< Total button press count
uint32_t distanceAlertCount = 0;       ///< Total distance sensor activations
uint32_t serialCmdCount = 0;           ///< Total serial commands received
/** @} */

/**
 * @name Synchronization mechanisms
 * @{
 */
hw_timer_t *timer0 = nullptr;                            ///< Hardware timer (1 Hz, Level 1)
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;    ///< Mutex for timer flag
portMUX_TYPE buttonMux = portMUX_INITIALIZER_UNLOCKED;   ///< Mutex for button flag
/** @} */

/**
 * @name System save state
 * @{
 */
bool blinkingMode = false;           ///< Blinking yellow mode active
bool distanceOverride = false;       ///< Distance sensor override active
unsigned long overrideStartTime = 0; ///< Sensor override start timestamp
unsigned long lastBlinkToggle = 0;   ///< Last blink toggle timestamp
bool blinkState = false;             ///< Current blink state (on/off)
bool emergencyMode = false;          ///< Emergency mode active (serial command)
/** @} */

/* ========================================================================== */
/*                      INTERRUPT SERVICE ROUTINES                            */
/* ========================================================================== */

/**
 * @brief Timer ISR - Level 1 (lowest priority, nested interrupt)
 *
 * @details Called every second by the hardware timer (Arduino API).
 *          Arduino's timerAttachInterrupt uses esp_intr_alloc internally
 *          with default Level 1 priority, so this ISR CAN be preempted
 *          by the button ISR running at Level 3.
 *          Only sets a flag - minimal execution time.
 *          Uses portMUX critical section for atomic flag access.
 *
 * @note IRAM_ATTR places the function in instruction RAM for faster execution
 */
void IRAM_ATTR onTimer() {
    portENTER_CRITICAL_ISR(&timerMux);
    timerFlag = true;
    timerIntCount++;
    portEXIT_CRITICAL_ISR(&timerMux);
}

/**
 * @brief Button ISR - highest priority interrupt
 *
 * @details Triggered on falling edge of the button pin.
 *          Implements software debounce via time threshold.
 *          Uses portMUX critical section for atomic flag access.
 *
 * @note On real hardware, nested interrupts can be enabled by using
 *       ESP-IDF's gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3) instead
 *       of Arduino's attachInterrupt. This would allow the button ISR
 *       (Level 3) to preempt the timer ISR (Level 1) mid-execution.
 *       Wokwi simulator does not support this, so Arduino API is used.
 */
void IRAM_ATTR ISR_Button() {
    unsigned long now = millis();
    portENTER_CRITICAL_ISR(&buttonMux);
    if (now - lastButtonTime > DEBOUNCE_DELAY) {
        lastButtonTime = now;
        buttonFlag = true;
        buttonIntCount++;
    }
    portEXIT_CRITICAL_ISR(&buttonMux);
}

/* ========================================================================== */
/*                          HELPER FUNCTIONS                                  */
/* ========================================================================== */

/**
 * @brief Measure distance using HC-SR04 ultrasonic sensor
 *
 * @details Sends a 10us pulse on TRIG pin and measures ECHO pulse duration.
 *          Uses speed of sound (0.0343 cm/us) to calculate distance.
 *
 * @return Distance in centimeters, 0 if measurement failed or timed out
 */
float measureDistance() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    float distance = (duration / 2.0f) * 0.0343f;
    return (duration > 0 && duration < 30000) ? distance : 0;
}

/* ========================================================================== */
/*                        INTERRUPT PROCESSING                                */
/* ========================================================================== */

/**
 * @brief Process timer interrupt - drive the traffic light state machine
 *
 * @details Lowest priority in the system. Increments tick counters for both
 *          traffic lights and checks if current state duration has expired.
 *          If so, calls nextState() to advance the state machine.
 *          Does not execute if blink mode, distance override, or emergency mode is active.
 */
void processTimerInterrupt() {
    if (blinkingMode || emergencyMode) return;

    trafficLightA.tickCounter++;

    if (trafficLightA.isStateExpired()) {
        trafficLightA.nextState();
        if (distanceOverride)
        {
            trafficLightB.nextState();
        }
    }
}

/**
 * @brief Process button interrupt - toggle blinking yellow mode
 *
 * @details Highest priority. Toggle logic:
 *          - First press: saves current states, activates blinking yellow
 *          - Second press: restores saved states, deactivates blinking
 *          Cancels distance override if active.
 */
void processButtonInterrupt() {
    blinkingMode = !blinkingMode;

    if (blinkingMode) {
        // Cancel distance override if active and save it's state
        distanceOverride = false;

        Serial.println(">>> BUTTON [HIGH PRIORITY]: Blinking yellow ACTIVATED <<<");
        trafficLightA.allOff();
        trafficLightB.allOff();
        lastBlinkToggle = millis();
        blinkState = false;
    } else {

        if (measureDistance() < ALARM_DISTANCE)
        {
            trafficLightA.setState(STATE_GREEN);
            trafficLightB.setState(STATE_RED);
        }
        else
        {
            trafficLightA.setState(STATE_GREEN);
            trafficLightB.allOff();
        }

        Serial.println(">>> BUTTON [HIGH PRIORITY]: Returning to normal operation <<<");
        Serial.printf("TL-A: %s, TL-B: %s\r\n", trafficLightA.stateToString(), trafficLightB.stateToString());
    }
}

/**
 * @brief Process distance sensor - medium priority override
 *
 * @details Measures distance with HC-SR04 sensor. When an object is detected
 *          closer than ALARM_DISTANCE cm, sets TL-B to the opposite state of
 *          TL-A (simulates: vehicle on side road requests passage).
 *
 *          Override lasts DISTANCE_OVERRIDE_MS, then checks if the object is
 *          still present. If so, extends override; otherwise restores normal state.
 *
 *          Has preemption over timer interrupt but not over button (blink mode).
 */
void processDistanceSensor() {
    if (blinkingMode || emergencyMode) return;

    float distance = measureDistance();

    // Activate override if object detected and override not already active
    if (distance > 0 && distance < ALARM_DISTANCE && !distanceOverride) {
        distanceOverride = true;
        overrideStartTime = millis();
        distanceAlertCount++;

        // Commment here
        trafficLightB.setState(static_cast<TrafficLightState>(trafficLightA.state + 2 % 4));

        Serial.printf(">>> SENSOR [MEDIUM PRIORITY]: Object at %.1f cm! <<<\r\n", distance);
        Serial.println("TL-A -> RED, TL-B -> GREEN");
    }

    // Check override timeout
    if (distanceOverride && (millis() - overrideStartTime > DISTANCE_OVERRIDE_MS)) {
        distance = measureDistance();
        if (distance > 0 && distance < ALARM_DISTANCE) {
            // Object still present - extend override
            overrideStartTime = millis();
            Serial.println(">>> SENSOR: Object still present, extending override <<<");
        } else {
            
            // Wait until traffic light B turns red
            if (trafficLightB.state != STATE_RED)
            {
                return;
            }

            // Object removed - restore normal state
            distanceOverride = false;
            
            // Set initial states
            trafficLightA.setState(STATE_GREEN);
            trafficLightB.allOff();

            Serial.println(">>> SENSOR: Override ended, returning to normal <<<");
            Serial.printf("TL-A: %s, TL-B: %s\r\n", trafficLightA.stateToString(), trafficLightB.stateToString());

        }
    }
}

/**
 * @brief Handle blinking yellow light animation
 *
 * @details Periodically toggles yellow LED on both traffic lights
 *          at the configured BLINK_INTERVAL rate.
 */
void handleBlinking() {
    unsigned long now = millis();
    if (now - lastBlinkToggle >= BLINK_INTERVAL) {
        lastBlinkToggle = now;
        blinkState = !blinkState;
        trafficLightA.setYellow(blinkState);
        trafficLightB.setYellow(blinkState);
    }
}

/* ========================================================================== */
/*                           SETUP AND LOOP                                   */
/* ========================================================================== */

/**
 * @brief System initialization
 *
 * @details Configures:
 *          - Serial communication (115200 baud)
 *          - GPIO pins for traffic lights, button, and sensor
 *          - Hardware timer (1 Hz) for state machine
 *          - External interrupt for button (FALLING edge)
 *          - Initial state: TL-A = RED, TL-B = OFF
 *          See comments in code for nested interrupt implementation on real hardware.
 */
void setup() {
    Serial.begin(115200);
    Serial.println("=========================================");
    Serial.println("  ESP32 Traffic Light - Interrupt System");
    Serial.println("  RUS Lab 1 - Ivan Bencic");
    Serial.println("=========================================");

    // Initialize traffic lights
    trafficLightA.init();
    trafficLightB.init();

    // Initial state: opposite directions at intersection
    trafficLightA.setState(STATE_RED);
    // Note: this traffic light will turn on only if it's pathway detects objects
    trafficLightB.allOff(); 

    // HC-SR04 ultrasonic sensor
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    // --- Interrupt setup ---
    // Both use Arduino API (Level 1 by default via esp_intr_alloc).
    //
    // NOTE: On real ESP32 hardware, nested interrupts can be achieved by
    // replacing attachInterrupt with ESP-IDF's gpio_install_isr_service():
    //
    //   #include <driver/gpio.h>
    //   #include <esp_intr_alloc.h>
    //   gpio_config_t cfg = { .pin_bit_mask = (1ULL << BUTTON_PIN),
    //     .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_NEGEDGE };
    //   gpio_config(&cfg);
    //   gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3);
    //   gpio_isr_handler_add((gpio_num_t)BUTTON_PIN, ISR_Button, NULL);
    //
    // This would place the button ISR at Level 3, allowing it to preempt
    // the timer ISR (Level 1) mid-execution — true nested interrupt behavior.
    // Wokwi simulator does not support different interrupt priority levels.

    // Hardware timer: 80 MHz APB / 80 = 1 MHz, alarm at 1,000,000 = 1s
    timer0 = timerBegin(0, 80, true);
    timerAttachInterrupt(timer0, &onTimer, true);
    timerAlarmWrite(timer0, 1000000, true);
    timerAlarmEnable(timer0);

    // Button with internal pull-up resistor
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), ISR_Button, FALLING);

    Serial.println();
    Serial.println("Interrupt priorities (flag-based in loop):");
    Serial.println("  1. Button - blink    (HIGHEST)");
    Serial.println("  2. Distance sensor   (MEDIUM - polled)");
    Serial.println("  3. Timer - state FSM (LOWEST)");
    Serial.println();
    Serial.println("Serial commands: EMERGENCY, NORMAL, STATUS");
    Serial.println("=========================================");
    Serial.printf("Initial: TL-A=%s, TL-B=%s\r\n",trafficLightA.stateToString(), trafficLightB.stateToString());
}

/**
 * @brief Main program loop
 *
 * @details Processes interrupt flags in priority order:
 *          1. Button (highest) - toggle blinking yellow
 *          2. Distance sensor (medium) - check and override
 *          3. Timer (lowest) - advance state machine
 *
 *          When multiple interrupts fire simultaneously, loop() resolves
 *          them by checking flags in fixed priority order — highest first.
 *          ISR functions only set flags (minimal execution time),
 *          actual processing happens here in loop() context.
 */
void loop() {

    // === PRIORITY 1 (HIGHEST): Button ===
    bool localButtonFlag = false;
    portENTER_CRITICAL(&buttonMux);
    if (buttonFlag) {
        buttonFlag = false;
        localButtonFlag = true;
    }
    portEXIT_CRITICAL(&buttonMux);

    if (localButtonFlag) {
        processButtonInterrupt();
    }

    // === PRIORITY 2 (MEDIUM): Distance sensor ===
    processDistanceSensor();

    // === PRIORITY 3 (LOWEST): Timer - state machine ===
    bool localTimerFlag = false;
    portENTER_CRITICAL(&timerMux);
    if (timerFlag) {
        timerFlag = false;
        localTimerFlag = true;
    }
    portEXIT_CRITICAL(&timerMux);

    if (localTimerFlag) {
        processTimerInterrupt();
    }

    // Blinking yellow if activated
    if (blinkingMode) {
        handleBlinking();
    }

    delay(50);
}
