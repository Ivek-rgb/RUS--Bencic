# Lab 2: Power Management with Sleep Modes

**Course:** RUS - Computer Control Systems (Racunalni upravljacki sustavi) - TVZ
**Author:** Ivan Bencic
**Platform:** ESP32 DOIT DevKit V1 (PlatformIO + Arduino Framework)
**Simulator:** [Wokwi](https://wokwi.com/)
**Lab variant:** **2 - Environment Datalogger (periodic wake)**

---

## Project Description

This project implements **Variant 2** of the RUS Lab 2 assignment: a periodic-wake **environment datalogger**. The ESP32 spends most of its time in a low-power sleep mode and only wakes on a timer to acquire a single temperature + humidity sample from a DHT22 sensor. Samples are stored in a 10-entry circular buffer; once the buffer fills, all 10 readings are dumped to the serial port in tabular form and the buffer is reset.

### Key Features

- **Periodic timer-driven wake-up** — `esp_sleep_enable_timer_wakeup()`
- **Two ESP32 sleep modes demonstrated** — Light Sleep (Wokwi demo) and Deep Sleep (real-hardware reference)
- **Wi-Fi disabled at boot in the Light Sleep variant** — `WiFi.mode(WIFI_OFF)` removes the RF current contribution from the active phase
- **10-entry ring buffer** of `Reading { timestampMs/timestampS, temperatureC, humidityPct }` as required by the spec
- **Persistence across the Deep Sleep reset** via `RTC_DATA_ATTR` (RTC slow memory survives the wake reset)
- **DHT22 sensor** with retry-on-NaN handling (single-wire timing-sensitive bus)
- **Doxygen documentation** with PlantUML state machine and program-flow diagrams

---

## Two Source Variants

The project ships **two implementations of the same datalogger** because the live-demo platform (Wokwi) and the target deployment platform (real ESP32 silicon) impose different constraints on which sleep mode is practical.

| File                       | Sleep mode                          | Interval | Storage                            | Built by default                    |
|----------------------------|-------------------------------------|----------|------------------------------------|-------------------------------------|
| `src/main1.cpp`            | **Light Sleep** (`esp_light_sleep_start`) | 3 s      | plain static SRAM                  | Yes (`esp32doit-devkit-v1`)         |
| `src/archive/main2.cpp`    | **Deep Sleep** (`esp_deep_sleep_start`)   | 60 s     | `RTC_DATA_ATTR` ring buffer        | No (`-e esp32-realhw`)              |

- **`src/main1.cpp` (Wokwi demo, Light Sleep)** — `esp_light_sleep_start()` pauses the CPU but does NOT reset the chip. The function returns on timer wake with SRAM and `millis()` preserved, so the ring buffer lives in ordinary static storage and `setup()` runs only once. The boot banner prints once at cold boot, then `[READ]` / `[SLEEP]` / `[WAKE]` lines follow forever. Wi-Fi is explicitly turned off in `setup()` so the active phase only pays for CPU + sensor + LEDs.
- **`src/archive/main2.cpp` (real hardware, Deep Sleep)** — `esp_deep_sleep_start()` powers most of the chip down and triggers a reset on wake. `setup()` runs again every cycle; cross-cycle state survives only because the ring buffer, counters, and uptime accumulator are tagged `RTC_DATA_ATTR` and live in RTC slow memory. This is the firmware you would flash to an actual ESP32 in the field for the lowest possible average current.

### Why two files?

> The Wokwi/Arduino-ESP32 stack cannot run the real Deep Sleep path cleanly in a way that is suitable for a live demonstration. See the **Wokwi Limitations** section below for the technical reasons - banner reprints on every wake, watchdog-vs-sleep race on TG1WDT, no power measurement.

`src/main1.cpp` is what you flash to Wokwi for a demo (Light Sleep avoids the reset/banner noise and the watchdog race). `src/archive/main2.cpp` is what you would flash to a physical board to actually achieve low-power operation (Deep Sleep brings the bare-module sleep current down to single-digit microamps).

---

## Sleep-Mode Trade-Off

The two variants are not just "demo vs production" - they exercise genuinely different ESP32 power-management paths.

| Property                       | Light Sleep (`src/main1.cpp`)             | Deep Sleep (`src/archive/main2.cpp`)            |
|--------------------------------|-------------------------------------------|-------------------------------------------------|
| API call                       | `esp_light_sleep_start()`                 | `esp_deep_sleep_start()`                        |
| Returns from sleep call?       | Yes - execution resumes inline            | No - chip resets, `setup()` re-runs             |
| SRAM preserved?                | Yes                                       | No (lost; RTC slow memory survives)             |
| State storage needed           | plain static variables                    | `RTC_DATA_ATTR` for everything cross-cycle      |
| Banner reprint on wake?        | No (one banner at cold boot)              | Yes (boot ROM logs + banner every interval)     |
| `millis()` preserved?          | Yes                                       | No (resets to zero each wake)                   |
| Typical bare-module sleep `I`  | ~0.8 mA                                   | ~10 µA                                          |
| Wokwi-friendly?                | Yes - clean trace, no reset noise         | Builds but visually noisy + watchdog race risk  |

Light Sleep is dramatically more battery-friendly than running the CPU full-time, but Deep Sleep wins by another two orders of magnitude on bare hardware. The choice is genuinely application-dependent: short intervals + cross-cycle state + clean serial output favour Light Sleep; minute-scale wakes + minimum-current battery operation favour Deep Sleep.

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

### Wokwi Demo Build (default, Light Sleep)

```bash
pio run                           # compiles src/main1.cpp (Light Sleep, 3 s cycle)
# then start Wokwi (VS Code extension or wokwi-cli)
```

The default environment is `esp32doit-devkit-v1`, which compiles `src/main1.cpp` and excludes anything under `src/archive/`. This is the build that ships to Wokwi.

### Real-Hardware Build (Deep Sleep)

```bash
pio run -e esp32-realhw                  # compiles src/archive/main2.cpp (Deep Sleep, 60 s cycle)
pio run -e esp32-realhw -t upload        # flash a physical board
```

The `esp32-realhw` environment swaps the source filter to include only `src/archive/main2.cpp`. The produced firmware uses real Deep Sleep with a 60-second cycle.

---

## Operational Summary

### Light Sleep variant (`src/main1.cpp`)

| Phase                | What happens                                                                                                | Visible indicators                                  |
|----------------------|-------------------------------------------------------------------------------------------------------------|-----------------------------------------------------|
| Cold boot (once)     | full banner, `WiFi.mode(WIFI_OFF)`, DHT22 settling delay (800 ms)                                           | banner on serial, AWAKE LED on                      |
| Acquisition          | one DHT22 read with retry, append to buffer, log `[READ n/10] t=... h=...`                                  | ACTIVITY LED pulses red ~200 ms                     |
| Dump (if full)       | print 10-row table, reset `bufferCount`                                                                     | banner + table on serial                            |
| Light Sleep          | `esp_light_sleep_start()` for `SLEEP_INTERVAL_MS` (3 s)                                                     | AWAKE LED off, serial quiet, CPU in low-power state |
| Wake (timer)         | `esp_light_sleep_start()` returns; `loop()` continues normally                                              | `[WAKE]` line, AWAKE LED back on                    |

No chip reset, no `setup()` re-entry. The banner prints exactly once - at cold boot.

### Deep Sleep variant (`src/archive/main2.cpp`)

| Phase                | What happens                                                                                                | Visible indicators                                |
|----------------------|-------------------------------------------------------------------------------------------------------------|---------------------------------------------------|
| Cold boot            | full banner, RTC counters reset, DHT22 settling delay (800 ms)                                              | banner on serial, AWAKE LED on                    |
| Acquisition          | one DHT22 read with retry, append to RTC-resident buffer                                                    | ACTIVITY LED pulses red ~200 ms                   |
| Dump (if full)       | print 10-row table, reset `bufferCount` (still in RTC memory)                                               | banner + table on serial                          |
| Real Deep Sleep      | `esp_deep_sleep_start()` for 60 s - chip actually powers down                                               | serial silent, chip in lowest power state         |
| Wake (timer)         | chip resets, `setup()` reruns; full banner reprints; `cause = TIMER` distinguishes from cold boot           | boot ROM lines + banner every cycle               |

`loop()` is empty in this variant - `esp_deep_sleep_start()` never returns, so all work happens in `setup()`.

---

## Wokwi Limitations

The lab brief explicitly requires the report to call out what *cannot* be done in the simulator. For variant 2 the limitations are:

1. **No power measurement.** Wokwi does not model current draw. None of the metrics that justify any sleep mode on a real device (milliamps awake, microamps asleep, theoretical battery life on a 2500 mAh cell) can be obtained from simulation. Real-circuit measurement requires a current meter or a dedicated tool such as the [ARM Energy Profiler](https://developer.arm.com/documentation/102732/1910/Energy-profiling).
2. **Deep Sleep is visually noisy and races a watchdog.** On real ESP32 silicon `esp_deep_sleep_start()` resets the chip cleanly on timer wake (`DEEPSLEEP_RESET`). On Wokwi the reset still happens, but the boot ROM relogs every cycle and the TG1 watchdog stays armed across some reset paths, sometimes firing during the sleep window and producing `TG1WDT_SYS_RESET` instead - which on Wokwi also wipes the RTC slow memory the application is relying on. `src/main1.cpp` uses Light Sleep precisely to sidestep both issues; `src/archive/main2.cpp` keeps Deep Sleep because that is the correct mode on real silicon, where these Wokwi-specific quirks do not exist.
3. **DHT22 readings are synthetic.** Wokwi's `wokwi-dht22` part returns the `temperature` / `humidity` values configured in `diagram.json`, not real environmental data. The logic of the datalogger (buffering, dumping, timestamping) is fully testable; the *content* of the readings is not realistic. To exercise the retry path you can briefly set an invalid value in the part configuration.

The conclusion required by the spec applies directly:

> *"The implementation demonstrates the power-management logic, but does not allow real estimation of energy consumption."*

For an actual energy analysis the firmware would have to be flashed to the physical board (via `pio run -e esp32-realhw -t upload`) and measured externally.

---

## Theoretical Battery Life Analysis

The lab brief asks for an analytical estimate of battery life using the formula:

```
I_avg = (I_active * t_active + I_sleep * t_sleep) / t_total
```

Wokwi cannot measure current, so the numbers below are *datasheet-derived
estimates*. The Light Sleep numbers correspond to the default Wokwi build
(`src/main1.cpp`, 3 s cycle); the Deep Sleep numbers correspond to the real-
hardware reference (`src/archive/main2.cpp`, 60 s cycle). They are not
measurements.

### Light Sleep variant (src/main1.cpp, 3 s cycle)

| Phase                         | Duration   | Notes                                                          |
|-------------------------------|------------|----------------------------------------------------------------|
| DHT22 read + buffer append    | ~0.4 s     | DHT22 needs ~250 ms between reads + serial transmit            |
| **`t_active` (per cycle)**    | **~0.4 s** | Per 3 s cycle                                                  |
| `t_sleep`                     | ~2.6 s     | `esp_light_sleep_start()` until timer wake                     |
| `t_total`                     | 3 s        |                                                                |

### Deep Sleep variant (src/archive/main2.cpp, 60 s cycle)

| Phase                         | Duration   | Notes                                                          |
|-------------------------------|------------|----------------------------------------------------------------|
| Boot from Deep Sleep + setup  | ~0.3 s     | Bootloader + Arduino-ESP32 init                                |
| DHT22 settling + read + retry | ~0.4 s     | DHT22 requires ~250 ms idle between reads                      |
| Append + (every 10th) dump    | ~0.3 s     | Serial transmit at 115200 baud                                 |
| **`t_active` (per cycle)**    | **~1.0 s** | Per 60 s cycle                                                 |
| `t_sleep`                     | ~59 s      | `esp_deep_sleep_start()` until timer wake                      |
| `t_total`                     | 60 s       |                                                                |

### Assumed current draws

| Quantity              | Bare ESP32-WROOM-32 module | DOIT DevKit V1 (board as-is)   | Source / rationale                            |
|-----------------------|----------------------------|--------------------------------|-----------------------------------------------|
| `I_active`            | ~80 mA                     | ~100 mA                        | CPU 240 MHz, Wi-Fi off, +DHT22 + 2 LEDs       |
| `I_sleep` (Light)     | ~0.8 mA                    | ~21 mA                         | DevKit's CP2102 + AMS1117 LDO dominate sleep  |
| `I_sleep` (Deep)      | ~10 µA                     | ~20 mA                         | Same DevKit penalty as Light Sleep            |
| Battery               | 2500 mAh                   | 2500 mAh                       | Per spec                                      |

The DOIT DevKit values for `I_sleep` are dominated by the on-board CP2102 USB-UART
bridge (~10 mA quiescent) and the AMS1117-3.3 LDO quiescent (~5-10 mA). These
parts are why "ESP32 dev boards drain a battery in days" is a common pitfall;
on a stripped-down custom PCB the figures drop to the bare-module column.

### Calculation - bare ESP32-WROOM, Light Sleep (`src/main1.cpp`)

```
I_avg = (80 mA * 0.4 s + 0.8 mA * 2.6 s) / 3 s
      = (32 + 2.08) / 3
      = 34.08 / 3
      ~= 11.36 mA

Battery life = 2500 mAh / 11.36 mA ~= 220 h ~= 9.2 days
```

The Light-Sleep figure is poor because of the 3-second cycle - the
active phase dominates the average. Stretching the interval to 30-60 s
brings Light Sleep into the same ballpark as Deep Sleep on a bare
module; see the Deep Sleep calculation below for the lower bound.

### Calculation - bare ESP32-WROOM, Deep Sleep (`src/archive/main2.cpp`)

```
I_avg = (80 mA * 1 s + 0.010 mA * 59 s) / 60 s
      = (80 + 0.59) / 60
      = 80.59 / 60
      ~= 1.343 mA

Battery life = 2500 mAh / 1.343 mA ~= 1862 h ~= 77.6 days (~2.5 months)
```

### Calculation - DOIT DevKit V1, Deep Sleep (no hardware mods)

```
I_avg = (100 mA * 1 s + 20 mA * 59 s) / 60 s
      = (100 + 1180) / 60
      = 1280 / 60
      ~= 21.33 mA

Battery life = 2500 mAh / 21.33 mA ~= 117 h ~= 4.9 days
```

### What the gaps mean

- **Light vs Deep Sleep on a bare module:** ~6x difference at the chosen
  cadences (220 h vs 1860 h). Light Sleep loses both on its higher sleep
  current AND on its much shorter cycle, where the active phase eats a
  larger fraction of the total. On a 60-second cycle the gap would close
  considerably; on a 1-second cycle Deep Sleep barely helps because the
  reset overhead becomes significant.
- **Bare module vs DOIT DevKit:** ~15x gap for Deep Sleep, dominated by
  the dev board's USB bridge and LDO quiescent. The firmware achieves
  the same logical sleep cycle in both cases - what changes is the
  supporting circuitry. For a real deployment either remove the USB
  chip and use a more efficient LDO/regulator, or move to a
  purpose-built PCB around the ESP32-WROOM module.

### What this analysis does *not* prove

- It is not a measurement. Real `I_active` varies with peripheral activity,
  Wi-Fi/BT (disabled here, but worth ~120 mA when on), and CPU frequency.
- It assumes the timer wake fires exactly on schedule and that no error retry
  loops extend `t_active`. A DHT22 NaN-retry burst would raise `t_active`.
- Battery self-discharge (typ. 2-3 % / month for Li-ion) is ignored - it
  becomes the dominant loss in the bare-module Deep Sleep case.

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
| Sleep modes        | Light Sleep (`esp_light_sleep_start`, src/main1.cpp, 3 s cycle) and Deep Sleep (`esp_deep_sleep_start`, src/archive/main2.cpp, 60 s) |
| Wake               | Timer (`esp_sleep_enable_timer_wakeup`) in both variants                                                                          |
| State storage      | Plain static SRAM in the Light Sleep build; `RTC_DATA_ATTR` ring buffer in the Deep Sleep build                                   |
| Radio              | Wi-Fi explicitly disabled at boot in the Light Sleep build via `WiFi.mode(WIFI_OFF)`                                              |
| Debouncing         | n/a for variant 2 (no mechanical button), addressed in variant 1                                                                  |
| Wokwi link         | _(replace with your published Wokwi project URL)_                                                                                 |

---

## Project Layout

```
LAB2/
├── Lab2.md                    - lab document required by the assignment spec
├── README.md                  - this file (full project documentation)
├── platformio.ini             - two PlatformIO envs: Wokwi demo (Light Sleep) + real HW (Deep Sleep)
├── diagram.json               - Wokwi circuit (ESP32 + DHT22 + 2 LEDs)
├── wokwi.toml                 - Wokwi firmware paths (used by VS Code ext)
├── Doxyfile                   - Doxygen + PlantUML config
├── docs/
│   ├── mainpage.dox           - Doxygen mainpage + embedded PlantUML
│   ├── state_machine.puml     - standalone state machine
│   └── program_flow.puml      - standalone program flow
├── src/                       - PlatformIO source root (authoritative)
│   ├── main1.cpp              - Light Sleep, 3-second Wokwi-demo cadence (default build)
│   └── archive/
│       └── main2.cpp          - Deep Sleep, 60-second real-hardware reference (-e esp32-realhw)
├── wokwi/                     - spec-mandated simulator bundle (mirror)
│   ├── README.md              - explains this is a mirror of root files
│   ├── diagram.json           - copy of root diagram.json
│   ├── wokwi.toml             - copy of root wokwi.toml (rel. paths fixed)
│   ├── main1.cpp              - copy of src/main1.cpp
│   └── archive/
│       └── main2.cpp          - copy of src/archive/main2.cpp
└── results/                   - proof-of-execution artefacts
    └── serial_output.txt      - captured serial log (sleep / wake / read)
```

> The `wokwi/` folder is required by the lab assignment's mandated structure.
> Its contents are *duplicates* of the authoritative files at the repository
> root and under `src/`, not the working copies; the PlatformIO build still
> consumes `src/main1.cpp` / `src/archive/main2.cpp`.

---

## Generating the Doxygen Documentation

```bash
doxygen Doxyfile        # outputs docs/html/index.html
```

PlantUML diagrams are rendered automatically if `plantuml.jar` is available at the path configured in `Doxyfile`.

---

## See Also

- `../LAB1/` - prior lab (multi-interrupt traffic-light system), same documentation style
- [ESP32 Sleep Modes - Espressif Docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
