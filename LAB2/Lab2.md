# Lab 2 - Power Management with Sleep Modes (Variant 2)

**Course:** RUS - Racunalni upravljacki sustavi (TVZ)
**Author:** Ivan Bencic
**Platform:** ESP32 DOIT DevKit V1 (PlatformIO + Arduino framework)
**Variant:** 2 - Environment Datalogger (periodic timer wake)

## Short description

An ESP32-based environment datalogger that spends almost all of its time in
ESP32 Deep Sleep and wakes periodically via timer to read a DHT22 (temperature
and humidity). The last 10 readings are kept in an `RTC_DATA_ATTR` ring buffer
that survives the Deep Sleep reset; once the buffer fills, all 10 readings are
printed in a table and the buffer is reset. Two firmware variants are provided:
a Wokwi-demo build (`src/main.cpp`, ~1 s real Deep Sleep + emulated visible
sleep) and a real-hardware build (`src/archive/main1.cpp`, full 60 s Deep
Sleep). The full report, hardware setup, operational summary, Wokwi
limitations, and the theoretical battery-life calculation are in `README.md`.

## Wokwi link

_(replace with your published Wokwi project URL)_

The Wokwi files needed to reproduce the simulation are also included in this
repository under `wokwi/` (`diagram.json`, `wokwi.toml`, `main.cpp`,
`archive/main1.cpp`) and at the repository root, in line with the assignment's
required structure.

## Where to look

- `README.md` - full project documentation (hardware, build, operation,
  Wokwi limitations, battery-life calculation, lab summary table)
- `src/main.cpp` - Wokwi-demo firmware (PlatformIO authoritative copy)
- `src/archive/main1.cpp` - real-hardware firmware (full Deep Sleep)
- `wokwi/` - spec-mandated simulator bundle (mirror of root files)
- `docs/` - Doxygen mainpage and PlantUML state-machine / flow diagrams
- `results/serial_output.txt` - captured serial log showing sleep, wake,
  read, and dump cycles
- `requirements.txt` - original lab assignment text (Croatian)
