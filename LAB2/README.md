# Lab 2: Power Management with Sleep Modes

**Course:** RUS - Computer Control Systems (Racunalni upravljacki sustavi) - TVZ
**Author:** Ivan Bencic
**Platform:** ESP32 DOIT DevKit V1 (PlatformIO + Arduino Framework)
**Simulator:** [Wokwi](https://wokwi.com/)
**Lab variant:** **2 - Environment Datalogger (periodic wake)**

---

## Project Description

This project implements **Variant 2** of the RUS Lab 2 assignment: a periodic-wake **environment datalogger**. The ESP32 spends almost all of its time in a low-power sleep mode and only wakes on a timer to acquire a single temperature + humidity sample from a DHT22 sensor. Samples are stored in a 10-entry circular buffer; once the buffer fills, all 10 readings are dumped to the serial port in tabular form and the buffer is reset.

### Key Features

- **Periodic timer-driven wake-up** — `esp_sleep_enable_timer_wakeup()` on real hardware
- **Deep Sleep on real silicon** — `esp_deep_sleep_start()` (deepest mode supported by ESP32)
- **10-entry ring buffer of `Reading { uptimeS, temperatureC, humidityPct }`** as required by the spec
- **Persistence across sleep cycles** via `RTC_DATA_ATTR` (RTC slow memory survives Deep Sleep)
- **Dump-and-reset** of the buffer once it fills, with a serial table that maps directly to the lab report's `serial_output.txt`
- **DHT22 sensor** with retry-on-NaN handling (single-wire timing-sensitive bus)
- **Two source variants** for two platforms (see below)
- **Doxygen documentation** with PlantUML state machine and program-flow diagrams

---

## Two Source Variants

The project ships **two implementations of the same firmware** because the live-demo platform (Wokwi) and the target deployment platform (real silicon) impose different constraints.

| File                       | Sleep mechanism                                                       | Storage                       | Built by default |
|----------------------------|-----------------------------------------------------------------------|-------------------------------|------------------|
| `src/main.cpp`             | Hybrid: 3 s emulated visible sleep (`delay`) + 1 s real `esp_deep_sleep_start()` | `RTC_DATA_ATTR` ring buffer   | Yes (Wokwi demo) |
| `src/archive/main1.cpp`    | Full-duration real `esp_deep_sleep_start()` + timer wake (60 s)       | `RTC_DATA_ATTR` ring buffer   | No - `pio run -e esp32-realhw` |

Both variants actually call the real ESP32 Deep Sleep API; the difference is how much of each cycle is spent in real Deep Sleep vs. in a visible emulated phase.

- **`src/main.cpp` (Wokwi demo)** trims the real Deep Sleep to ~1 second per cycle. The visible 3-second `delay()` window keeps the serial monitor clean and easy to follow on a projector, while the brief real Deep Sleep at the end of every cycle resets the chip and proves that the ESP32 power-management path actually works in simulation. The boot banner reprints on every cycle, but compactly (`[BOOT #n via TIMER]`).
- **`src/archive/main1.cpp` (real HW)** drops the visible phase and goes straight into a 60-second real Deep Sleep per cycle. This is the firmware you would flash to an actual ESP32 deployed in the field.

Both variants store the buffer + counters in RTC slow memory (`RTC_DATA_ATTR`) so they survive the Deep Sleep reset, exactly as on real hardware.

### Why two files?

> The Wokwi/Arduino-ESP32 stack cannot run the real Deep Sleep path cleanly in a way that is suitable for a live demonstration. See the **Wokwi Limitations** section below for the technical reasons.

`src/main.cpp` is what you flash to Wokwi for a demo; `src/archive/main1.cpp` is what you would flash to a physical board to actually achieve low-power operation.

---

## Hardware Setup

### Pin Map

| Component       | GPIO | Direction | Notes                                       |
|-----------------|------|-----------|---------------------------------------------|
| DHT22 DATA      | 15   | bidir     | Single-wire bus, internal pull-up in Wokwi  |
| ACTIVITY LED    | 26   | OUTPUT    | Red, pulses on each sensor read             |
| AWAKE LED       | 14   | OUTPUT    | Yellow, on while CPU is awake               |

The DHT22 takes power from the ESP32 `3V3` rail and ground from `GND.1`; both LEDs return through `GND.2`.

### Wokwi Diagram Layout

- **DHT22** placed top-left, wired with three lines (VCC red, GND black, SDA green)
- **ACTIVITY LED** + **AWAKE LED** stacked on the right side of the ESP32

(See `diagram.json` for exact positions.)

---

## Build & Run

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE plugin)
- [Wokwi](https://wokwi.com/) for simulation (or the Wokwi for VS Code extension)
- For the real-hardware build: an ESP32 DevKit V1, a DHT22, two LEDs, ~470 ohm series resistors

### Wokwi Demo Build (default)

```bash
pio run                           # compiles src/main.cpp
# then start Wokwi (VS Code extension or wokwi-cli)
```

The default environment is `esp32doit-devkit-v1`, which compiles `src/main.cpp` and excludes anything under `src/archive/`. This is the build that ships to Wokwi.

### Real-Hardware Build

```bash
pio run -e esp32-realhw           # compiles src/archive/main1.cpp
pio run -e esp32-realhw -t upload # flash a physical board
```

The `esp32-realhw` environment swaps the source filter to include only `src/archive/main1.cpp`. The produced firmware uses real Deep Sleep.

---

## Operational Summary

| Phase                  | What happens                                                                                                | Visible indicators                                |
|------------------------|-------------------------------------------------------------------------------------------------------------|---------------------------------------------------|
| Cold boot              | full banner, RTC counters reset, DHT22 settling delay (real HW only)                                        | banner on serial, AWAKE LED on                    |
| Acquisition            | one DHT22 read with retry, append to buffer, log `[READ n/10] t=... h=...`                                  | ACTIVITY LED pulses red ~200 ms                   |
| Dump (if full)         | print 10-row table, reset `bufferCount`                                                                     | banner + table on serial                          |
| Emulated sleep (demo)  | `delay(3 s)` with AWAKE LED off                                                                             | AWAKE LED off, serial quiet for 3 s               |
| Real Deep Sleep        | `esp_deep_sleep_start()` - 1 s (Wokwi demo) or 60 s (real HW)                                               | serial silent, chip actually in low-power state   |
| Wake (timer)           | chip resets, `setup()` reruns; one-line `[BOOT #n via TIMER]` message; RTC buffer intact                    | compact wake message, AWAKE LED back on           |

---

## Wokwi Limitations

The lab brief explicitly requires the report to call out what *cannot* be done in the simulator. For variant 2 the limitations are substantial:

1. **No power measurement.** Wokwi does not model current draw. None of the metrics that justify Deep Sleep on a real device (microamps in sleep, milliamps awake, theoretical battery life on a 2500 mAh cell) can be obtained from simulation. Real-circuit measurement requires a current meter or a dedicated tool such as the [ARM Energy Profiler](https://developer.arm.com/documentation/102732/1910/Energy-profiling).
2. **Light Sleep is unusable under Arduino-ESP32.** Calling `esp_light_sleep_start()` from the loop task asserts on the second cycle inside `spinlock_acquire()`. Root cause: Arduino-ESP32 ships without `CONFIG_PM_ENABLE`, so the SMP-aware sleep-entry path that ESP-IDF normally runs is bypassed; the FreeRTOS idle-task spinlocks held by the other core desync. This is a framework limitation, not a Wokwi one, but Wokwi makes it easy to observe. There is no application-level workaround.
3. **Deep Sleep works but resets the chip.** This is *correct* Deep Sleep behaviour. The Wokwi demo variant keeps the visible sleep window (3 s) as an emulated `delay()` so the audience-visible cycle is clean (AWAKE LED off, serial silent, `[SLEEP]`/`[WAKE]` markers), and then briefly enters real Deep Sleep (1 s) before the chip resets. The reset is announced as a compact `[BOOT #n via TIMER]` line rather than the full banner. RTC memory carries the buffer + counters across the reset, exactly as on real silicon.
4. **DHT22 readings are synthetic.** Wokwi's `wokwi-dht22` part returns the `temperature` / `humidity` values configured in `diagram.json`, not real environmental data. The logic of the datalogger (buffering, dumping, timestamping) is fully testable; the *content* of the readings is not realistic. To exercise the retry path you can briefly set an invalid value in the part configuration.

The conclusion required by the spec applies directly to this work:

> *"The implementation demonstrates the power-management logic, but does not allow real estimation of energy consumption."*

For an actual energy analysis the firmware would have to be flashed to the physical board (via `pio run -e esp32-realhw -t upload`) and measured externally.

---

## Theoretical Battery Life Analysis

The lab brief asks for an analytical estimate of battery life using the formula:

```
I_avg = (I_active * t_active + I_sleep * t_sleep) / t_total
```

Wokwi cannot measure current, so the numbers below are *datasheet-derived
estimates* for the **real-hardware variant** (`src/archive/main1.cpp`, 60 s
period, full Deep Sleep between cycles). They are not measurements.

### Assumed per-cycle timings (real-HW variant)

| Phase                         | Duration   | Notes                                                                 |
|-------------------------------|------------|-----------------------------------------------------------------------|
| Boot from Deep Sleep + setup  | ~0.3 s     | Bootloader + Arduino-ESP32 init                                       |
| DHT22 settling + read + retry | ~0.4 s     | DHT22 requires ~250 ms idle between reads                             |
| Append + (every 10th) dump    | ~0.3 s     | Serial transmit at 115200 baud                                        |
| **`t_active` (total)**        | **~1.0 s** | Per 60 s cycle                                                        |
| `t_sleep`                     | ~59 s      | `esp_deep_sleep_start()` until timer wake                             |
| `t_total`                     | 60 s       |                                                                       |

### Assumed current draws

| Quantity        | Bare ESP32-WROOM-32 module | DOIT DevKit V1 (board as-is)   | Source / rationale                            |
|-----------------|----------------------------|--------------------------------|-----------------------------------------------|
| `I_active`      | ~80 mA                     | ~100 mA                        | CPU 240 MHz, radios off, +DHT22 + 2 LEDs      |
| `I_sleep`       | ~10 µA                     | ~20 mA                         | DevKit's CP2102 + AMS1117 LDO dominate sleep  |
| Battery         | 2500 mAh                   | 2500 mAh                       | Per spec                                      |

The DOIT DevKit value for `I_sleep` is dominated by the on-board CP2102 USB-UART
bridge (~10 mA quiescent) and the AMS1117-3.3 LDO quiescent (~5-10 mA). These
parts are why "ESP32 dev boards drain a battery in days" is a common pitfall;
on a stripped-down custom PCB the figure drops to ~10 µA.

### Calculation - bare ESP32-WROOM module

```
I_avg = (80 mA * 1 s + 0.010 mA * 59 s) / 60 s
      = (80 + 0.59) / 60
      = 80.59 / 60
      ~= 1.343 mA

Battery life = 2500 mAh / 1.343 mA ~= 1862 h ~= 77.6 days (~2.5 months)
```

### Calculation - DOIT DevKit V1 (no hardware mods)

```
I_avg = (100 mA * 1 s + 20 mA * 59 s) / 60 s
      = (100 + 1180) / 60
      = 1280 / 60
      ~= 21.33 mA

Battery life = 2500 mAh / 21.33 mA ~= 117 h ~= 4.9 days
```

### What the gap means

The ~15x gap between the two figures is the cost of running on a dev board
rather than a custom PCB. The firmware achieves the same logical sleep cycle in
both cases - what changes is the supporting circuitry. For a real deployment
either remove the USB chip and use a more efficient LDO/regulator, or move to a
purpose-built board around the ESP32-WROOM module.

### What this analysis does *not* prove

- It is not a measurement. Real `I_active` varies with peripheral activity,
  Wi-Fi/BT (disabled here, but worth ~120 mA when on), and CPU frequency.
- It assumes the timer wake fires exactly on schedule and that no error retry
  loops extend `t_active`. A DHT22 NaN-retry burst would raise `t_active`.
- Battery self-discharge (typ. 2-3 % / month for Li-ion) is ignored - it
  becomes the dominant loss in the bare-module case.

As required by the spec, the firmware demonstrates the *logic* of power
management; a real consumption figure requires hardware measurement (e.g.
[ARM Energy Profiler](https://developer.arm.com/documentation/102732/1910/Energy-profiling)
or a low-side current shunt).

---

## Lab Report Summary Table

| Item               | Answer                                                                                                                            |
|--------------------|-----------------------------------------------------------------------------------------------------------------------------------|
| Platform           | ESP32 DOIT DevKit V1 (Arduino-ESP32 / PlatformIO)                                                                                 |
| Variant            | 2 - Environment Datalogger (periodic timer wake)                                                                                  |
| Sleep mode         | Deep Sleep (`esp_deep_sleep_start`) - 1 s brief Deep Sleep per cycle in Wokwi demo; 60 s real Deep Sleep on real HW               |
| Wake               | Timer (`esp_sleep_enable_timer_wakeup`)                                                                                           |
| State storage      | `RTC_DATA_ATTR` ring buffer of 10 `Reading` entries + counters (both variants)                                                    |
| Debouncing         | n/a for variant 2 (no mechanical button), addressed in variant 1                                                                  |
| Wokwi link         | _(replace with your published Wokwi project URL)_                                                                                 |

---

## Project Layout

```
LAB2/
├── Lab2.md                    - lab document required by the assignment spec
├── README.md                  - this file (full project documentation)
├── requirements.txt           - lab assignment text (Croatian)
├── platformio.ini             - two PlatformIO envs: Wokwi demo + real HW
├── diagram.json               - Wokwi circuit (ESP32 + DHT22 + 2 LEDs)
├── wokwi.toml                 - Wokwi firmware paths (used by VS Code ext)
├── Doxyfile                   - Doxygen + PlantUML config
├── docs/
│   ├── mainpage.dox           - Doxygen mainpage + embedded PlantUML
│   ├── state_machine.puml     - standalone state machine
│   └── program_flow.puml      - standalone program flow (HW variant)
├── src/                       - PlatformIO source root (authoritative)
│   ├── main.cpp               - Wokwi demo (emulated sleep, RTC buffer)
│   └── archive/
│       └── main1.cpp          - Real HW (Deep Sleep, RTC_DATA_ATTR buffer)
├── wokwi/                     - spec-mandated simulator bundle (mirror)
│   ├── README.md              - explains this is a mirror of root files
│   ├── diagram.json           - copy of root diagram.json
│   ├── wokwi.toml             - copy of root wokwi.toml (rel. paths fixed)
│   ├── main.cpp               - copy of src/main.cpp
│   └── archive/
│       └── main1.cpp          - copy of src/archive/main1.cpp
└── results/                   - proof-of-execution artefacts
    └── serial_output.txt      - captured serial log (sleep / wake / read)
```

> The `wokwi/` folder is required by `requirements.txt`'s mandated structure.
> Its contents are *duplicates* of the authoritative files at the repository
> root and under `src/`, not the working copies; the PlatformIO build still
> consumes `src/main.cpp` / `src/archive/main1.cpp`.

---

## Generating the Doxygen Documentation

```bash
doxygen Doxyfile        # outputs docs/html/index.html
```

PlantUML diagrams are rendered automatically if `plantuml.jar` is available at the path configured in `Doxyfile`.

---

## See Also

- `requirements.txt` - original lab assignment text (Croatian)
- `../LAB1/` - prior lab (multi-interrupt traffic-light system), same documentation style
- [ESP32 Sleep Modes - Espressif Docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
