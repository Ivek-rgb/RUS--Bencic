# Lab 1: Interrupts in Embedded Systems

**Course:** RUS - Computer Control Systems (Racunalni upravljacki sustavi) - TVZ
**Author:** Ivan Bencic
**Platform:** ESP32 DOIT DevKit V1 (PlatformIO + Arduino Framework)
**Simulator:** [Wokwi](https://wokwi.com/)

---

## Project Description

This project demonstrates multi-interrupt handling on an ESP32 microcontroller through a **traffic light intersection simulation**. Two traffic lights (TL-A and TL-B) are controlled by a combination of hardware timer interrupts, GPIO button interrupts, and an ultrasonic distance sensor — each operating at a different priority level.

### Key Features

- **TrafficLight class** — encapsulates GPIO pin control and state machine logic for each traffic light
- **State machine** — European traffic light cycle (RED -> RED+YELLOW -> GREEN -> YELLOW -> RED)
- **Three interrupt/event sources** with defined priority handling
- **Simultaneous interrupt detection and resolution** — when multiple events fire at the same time, `loop()` processes them in strict priority order
- **Critical sections** (`portMUX`) for safe shared data access between ISRs and main loop
- **Nested interrupt support** — documented ESP-IDF implementation for real hardware (Level 1 timer / Level 3 button)
- **Doxygen documentation** with PlantUML diagrams

---

## Interrupt Priority Scheme

| Priority | Source | Type | Description |
|----------|--------|------|-------------|
| 1 (HIGHEST) | **Button** (GPIO 25) | Hardware ISR (FALLING edge) | Toggles both traffic lights into blinking yellow mode. Cancels all lower-priority modes. |
| 2 (MEDIUM) | **HC-SR04 Sensor** (GPIO 4/5) | Polled in `loop()` | Object detected < 20 cm: sets TL-B to opposite state of TL-A. Override lasts 5s with re-check. |
| 3 (LOWEST) | **Timer** (1 Hz hardware timer) | Hardware ISR (1s period) | Advances TL-A through the state machine cycle. Skipped when blink or override is active. |

### Simultaneous Interrupt Resolution

When multiple interrupts fire at the same time (e.g., timer tick + button press), the flags are checked in `loop()` in **fixed priority order**:

```
1. buttonFlag   -> processButtonInterrupt()    // checked first
2. sensor poll  -> processDistanceSensor()     // checked second
3. timerFlag    -> processTimerInterrupt()     // checked last
```

Higher-priority handlers guard against lower ones — `processTimerInterrupt()` returns immediately if `blinkingMode` or `distanceOverride` is active.

### Nested Interrupts (Real Hardware)

On real ESP32 hardware, true nested interrupts can be achieved using ESP-IDF's interrupt allocator:

- **Timer ISR** at Level 1 (lowest) — can be preempted
- **Button ISR** at Level 3 (highest) — preempts timer mid-execution

The implementation code is documented in `setup()` comments. Wokwi simulator does not support interrupt priority levels, so Arduino API is used for simulation compatibility.

---

## Resource Management

| Mechanism | Purpose |
|-----------|---------|
| `portMUX_TYPE timerMux` | Critical section protecting `timerFlag` between timer ISR and `loop()` |
| `portMUX_TYPE buttonMux` | Critical section protecting `buttonFlag` between button ISR and `loop()` |
| `volatile` flags | Ensure compiler doesn't optimize away ISR-modified variables |
| `IRAM_ATTR` | Places ISR functions in instruction RAM for deterministic execution |
| Software debounce (200ms) | Prevents spurious button triggers |

---

## Hardware Setup

### Pin Map

| Component | GPIO | Direction |
|-----------|------|-----------|
| TL-A Red LED | 14 | OUTPUT |
| TL-A Yellow LED | 27 | OUTPUT |
| TL-A Green LED | 26 | OUTPUT |
| TL-B Red LED | 19 | OUTPUT |
| TL-B Yellow LED | 23 | OUTPUT |
| TL-B Green LED | 13 | OUTPUT |
| Button (BLINK) | 25 | INPUT_PULLUP |
| HC-SR04 TRIG | 4 | OUTPUT |
| HC-SR04 ECHO | 5 | INPUT |

### Wokwi Diagram Layout

- **TL-A** (vertical stack, left side) — north-south road
- **TL-B** (horizontal row, top) — east-west road, perpendicular to TL-A
- **HC-SR04** — next to TL-B, detects vehicles on east-west road
- **Button** — above ESP32, toggles blinking yellow mode

---

## Build & Run

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE plugin)
- [Wokwi](https://wokwi.com/) for simulation

### Wokwi Simulation

The project includes `diagram.json` and `wokwi.toml` for Wokwi simulation. Firmware binary path: `.pio/build/esp32doit-devkit-v1/firmware.bin`.

---

## Documentation

### Doxygen

Generate HTML documentation:

```bash
doxygen Doxyfile
```

Output: `docs/html/index.html`

### PlantUML Diagrams

PlantUML source files are in `docs/`:

| Diagram | File | Description |
|---------|------|-------------|
| Program Flow | [`program_flow.puml`](docs/program_flow.puml) | Main `setup()` and `loop()` execution flow |
| ISR Timeline | [`isr_flow.puml`](docs/isr_flow.puml) | Timing diagram showing ISR fires and state changes |
| State Machine | [`state_machine.puml`](docs/state_machine.puml) | Traffic light states, blinking mode, and distance override |
| Interrupt Priority | [`interrupt_priority.puml`](docs/interrupt_priority.puml) | Simultaneous interrupt detection and resolution flow |

To render PlantUML diagrams:

```bash
# Using PlantUML JAR
java -jar plantuml.jar docs/*.puml

# Or using Docker
docker run --rm -v $(pwd)/docs:/data plantuml/plantuml /data/*.puml
```

To enable PlantUML in Doxygen, set `PLANTUML_JAR_PATH` in the `Doxyfile` to point to your `plantuml.jar` location.

---

## Project Structure

```
LAB1/
├── src/
│   └── main.cpp            # Main firmware (TrafficLight class, ISRs, loop)
├── docs/
│   ├── program_flow.puml   # Program flow diagram
│   ├── isr_flow.puml       # ISR timing diagram
│   ├── state_machine.puml  # State machine diagram
│   └── interrupt_priority.puml  # Priority resolution diagram
├── diagram.json            # Wokwi simulator circuit
├── wokwi.toml              # Wokwi configuration
├── platformio.ini          # PlatformIO project config
├── Doxyfile                # Doxygen configuration
├── CLAUDE.md               # AI assistant instructions
└── README.md               # This file
```

---

## Serial Commands

| Command | Action |
|---------|--------|
| `EMERGENCY` | Activates emergency mode (all red) |
| `NORMAL` | Returns to normal operation |
| `STATUS` | Prints interrupt counters and current states |

---
