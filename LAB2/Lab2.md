# Lab 2 - Power Management with Sleep Modes (Variant 2)

**Course:** RUS - Racunalni upravljacki sustavi (TVZ)
**Author:** Ivan Bencic
**Platform:** ESP32 DOIT DevKit V1 (PlatformIO + Arduino framework)
**Variant:** 2 - Environment Datalogger (periodic timer wake)

## Short description

An ESP32-based environment datalogger that spends most of its time in a
low-power sleep mode and wakes periodically via timer to read a DHT22
(temperature and humidity). The last 10 readings are kept in a ring
buffer; once it fills, all 10 readings are printed in a table and the
buffer is reset.

Two firmware variants are provided, exercising **two different ESP32
sleep modes**:

- `src/main1.cpp` - **Light Sleep**, 3-second cycle. Default Wokwi-demo
  build. `esp_light_sleep_start()` returns on wake with SRAM and
  `millis()` preserved, so the ring buffer lives in ordinary statics and
  no `RTC_DATA_ATTR` is needed. Wi-Fi is disabled at boot via
  `WiFi.mode(WIFI_OFF)`.
- `src/archive/main2.cpp` - **Deep Sleep**, 60-second cycle. Hardware
  reference build (`-e esp32-realhw`). `esp_deep_sleep_start()` resets
  the chip on every wake; the buffer + counters live in
  `RTC_DATA_ATTR`-tagged variables that survive the reset.

The full report, hardware setup, operational summary, Wokwi limitations,
and the theoretical battery-life calculation are in `README.md`.

## Wokwi link

_(replace with your published Wokwi project URL)_

The Wokwi files needed to reproduce the simulation are also included in this
repository under `wokwi/` (`diagram.json`, `wokwi.toml`, `main1.cpp`,
`archive/main2.cpp`) and at the repository root, in line with the assignment's
required structure.

## Where to look

- `README.md` - full project documentation (hardware, build, operation,
  Wokwi limitations, battery-life calculation, lab summary table)
- `src/main1.cpp` - Light Sleep Wokwi-demo firmware (default build)
- `src/archive/main2.cpp` - Deep Sleep real-hardware reference (`-e esp32-realhw`)
- `wokwi/` - spec-mandated simulator bundle (mirror of root files)
- `docs/` - Doxygen mainpage and PlantUML state-machine / flow diagrams
- `results/serial_output.txt` - captured serial log showing sleep, wake,
  read, and dump cycles
