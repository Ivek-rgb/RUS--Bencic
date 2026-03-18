#include <Arduino.h>

// ESP32-safe pin mapping (avoid GPIO6-11 which are connected to flash)
#define LED_INT0 16
#define LED_INT1 17
#define LED_INT2 18
#define LED_ALERT 19
#define LED_TIMER 23

#define BUTTON0 25
#define BUTTON1 26
#define BUTTON2 27

#define TRIG_PIN 4
#define ECHO_PIN 5

const unsigned long BLINK_INTERVAL = 200;
const unsigned long DEBOUNCE_DELAY = 50;
const int ALARM_DISTANCE = 100;

volatile bool intFlag[3] = {false, false, false};
volatile unsigned long lastInterruptTime[3] = {0, 0, 0};
volatile bool timerFlag = false;
volatile bool distanceAlert = false;
volatile bool interruptInProgress = false;

hw_timer_t *timer0 = nullptr;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

void handleInterrupt(int index, const char *message);
void handleInterrupts();
void handleTimerInterrupt();
void blinkLed(int ledPin);
float measureDistance();
void triggerDistanceAlert();

void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  timerFlag = true;
  portEXIT_CRITICAL_ISR(&timerMux);
}

void IRAM_ATTR ISR_INT0() {
  if (!interruptInProgress) {
    handleInterrupt(0, "INT0 (VISOKI prioritet) aktiviran");
  }
}

void IRAM_ATTR ISR_INT1() {
  if (!interruptInProgress) {
    handleInterrupt(1, "INT1 (SREDNJI prioritet) aktiviran");
  }
}

void IRAM_ATTR ISR_INT2() {
  if (!interruptInProgress) {
    handleInterrupt(2, "INT2 (NISKI prioritet) aktiviran");
  }
}

void setup() {
  pinMode(LED_INT0, OUTPUT);
  pinMode(LED_INT1, OUTPUT);
  pinMode(LED_INT2, OUTPUT);
  pinMode(LED_ALERT, OUTPUT);
  pinMode(LED_TIMER, OUTPUT);

  pinMode(BUTTON0, INPUT_PULLUP);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(BUTTON0), ISR_INT0, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON1), ISR_INT1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON2), ISR_INT2, FALLING);

  // 80 MHz APB / 80 = 1 MHz timer tick => 1,000,000 ticks = 1s
  timer0 = timerBegin(0, 80, true);
  timerAttachInterrupt(timer0, &onTimer, true);
  timerAlarmWrite(timer0, 1000000, true);
  timerAlarmEnable(timer0);

  Serial.begin(115200);
  Serial.println("ESP32 inicijalizacija zavrsena.");
}

void loop() {
  bool localTimerFlag = false;
  portENTER_CRITICAL(&timerMux);
  if (timerFlag) {
    timerFlag = false;
    localTimerFlag = true;
  }
  portEXIT_CRITICAL(&timerMux);

  if (localTimerFlag) {
    handleTimerInterrupt();
  }

  float distance = measureDistance();
  distanceAlert = (distance > 0 && distance < ALARM_DISTANCE);

  handleInterrupts();

  if (distanceAlert && !interruptInProgress) {
    triggerDistanceAlert();
  }

  delay(10);
}

void handleTimerInterrupt() {
  interruptInProgress = true;
  digitalWrite(LED_TIMER, HIGH);
  Serial.println("TIMER INTERRUPT (NAJVISI PRIORITET)");
  delay(200);
  digitalWrite(LED_TIMER, LOW);
  interruptInProgress = false;
}

void handleInterrupt(int index, const char *message) {
  unsigned long now = millis();
  if (now - lastInterruptTime[index] < DEBOUNCE_DELAY) {
    return;
  }

  lastInterruptTime[index] = now;
  intFlag[index] = true;
  Serial.println(message);
}

void handleInterrupts() {
  if (!intFlag[0] && !intFlag[1] && !intFlag[2]) {
    return;
  }

  interruptInProgress = true;

  if (intFlag[0]) {
    intFlag[0] = false;
    blinkLed(LED_INT0);
  } else if (intFlag[1]) {
    intFlag[1] = false;
    blinkLed(LED_INT1);
  } else if (intFlag[2]) {
    intFlag[2] = false;
    blinkLed(LED_INT2);
  }

  interruptInProgress = false;
}

void blinkLed(int ledPin) {
  unsigned long startTime = millis();
  while (millis() - startTime < 1000) {
    digitalWrite(ledPin, !digitalRead(ledPin));
    delay(BLINK_INTERVAL);
  }
  digitalWrite(ledPin, LOW);
}

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

void triggerDistanceAlert() {
  digitalWrite(LED_ALERT, HIGH);
  Serial.println("ALARM UDALJENOSTI AKTIVIRAN!");
  delay(500);
  digitalWrite(LED_ALERT, LOW);
}