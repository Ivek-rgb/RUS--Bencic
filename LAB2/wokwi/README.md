# wokwi/ - simulator bundle

This folder exists to satisfy the lab assignment's mandated repository layout
(`requirements.txt` requires `wokwi/diagram.json`, `wokwi/wokwi.toml`, and a
top-level `main.cpp`-equivalent sketch under `wokwi/`).

The authoritative copies that the PlatformIO build actually consumes are at
the repository root and under `src/`:

| File here                | Authoritative copy           | Notes                                                       |
|--------------------------|------------------------------|-------------------------------------------------------------|
| `main1.cpp`              | `../src/main1.cpp`           | Default build - Light Sleep, 3-second Wokwi-demo cadence    |
| `archive/main2.cpp`      | `../src/archive/main2.cpp`   | Real-hardware reference - Deep Sleep, 60-second cycle       |
| `diagram.json`           | `../diagram.json`            | Wokwi circuit (ESP32 DevKit V1 + DHT22 + 2 LEDs)            |
| `wokwi.toml`             | `../wokwi.toml`              | Points at `.pio/build/.../firmware.{bin,elf}`               |

The `wokwi.toml` here uses relative paths (`../.pio/build/...`) so the bundle
remains usable if someone copies just this folder. The root `wokwi.toml` uses
the in-tree relative path expected by the Wokwi VS Code extension when the
project root is opened in the editor.

If you edit firmware or wiring, edit the authoritative copy
(`src/main1.cpp`, `src/archive/main2.cpp`, `diagram.json`) and then sync this
folder.
