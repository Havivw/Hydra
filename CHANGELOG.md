# Changelog

All notable changes to Hydra are recorded here. Version numbers follow
the convention documented in [`version.h`](version.h). New entries are
added by the `/bump-hydra` Claude skill (see
`.claude/commands/bump-hydra.md`) at the same time the version macros
are bumped.

## [Unreleased]

## [0.0.2] — 2026-05-14

- SubGHz audit pass: documentation, bugs, and duplication cleanup.
- **Docs fix:** CC1101 CSN corrected from GPIO 27 → 5 in
  `Hydra/README.md` and `plans/01-hardware-divv1.md` — the library
  default (SS=5) is what the working firmware has always used, and
  GPIO 5 is therefore shared with SD card CS (now documented).
- **New shared module:** `sub_shared.h` / `sub_shared.cpp` centralise
  CC1101 pin macros, EEPROM profile layout, the `SubProfile` struct,
  the 17-entry default frequency table, and a single shared
  `RCSwitch subSwitch` instance. Replaces three duplicated frequency
  lists, two `Profile` structs, and two `RCSwitch` instances.
- **Bug fixes:**
  - `subjammer::SCREEN_HEIGHT` corrected from `64` to `320` (the
    `runUI()` body's `#define SCREENHEIGHT 320` workaround removed).
  - `replayat::ReplayAttackLoop` button-state variables renamed to
    match what they actually read (`btnSelectState` was reading
    `BTN_UP`; the missing `btnDownState` is now declared).
  - `SubReplay` SELECT abort magic `pcf.digitalRead(7)` replaced with
    the `BTN_SELECT` macro; SD mount path now re-pins VSPI and drives
    CS high before `SD.begin(5)` so it works after a CC1101 feature.
  - `SubGhzWardrive` auto-hop UP toggle now edge-latched (was
    flipping every 200 ms while button held); misleading "separate CS
    line" comment rewritten to describe actual GPIO-5 sharing.
  - Every SubGHz feature now calls `subghzReleasePinsFromNrf()` in
    setup() — the in-line `pinMode(26/16, INPUT)` calls that used to
    live in three Hydra.ino dispatch branches are gone.
- **Dead code removed:** `do_sampling()` FFT/waterfall overlay in
  `replayat` (redundant with Spectrum Analyzer, freed CPU during
  receive), the duplicate `MAX_PROFILES 5` define, the dead
  `ADDR_PROFILE_COUNT 0` macro.
- **Dispatch refactor:** `Hydra.ino` SubGHz handler collapsed
  ~250 lines of 8×-repeated state-machine boilerplate (across button
  and touch input paths) into a single `runSubghzFeature(idx, setup,
  loop)` helper called 16 times.
- **New:** `version.h` single source of truth + `/bump-hydra` skill
  for managing future version changes.
- Net change: −494 lines, 0 compile errors, 0 warnings against
  esp32 core 2.0.17, sketch 1.99 MB (63% of huge_app).

## [0.0.1] — 2026-05-12

- Initial Hydra release on the ESP32-DIV v1 board (plain ESP32 +
  ILI9341 TFT + PCF8574 buttons + 3× NRF24 + CC1101).
- Forked from cifertech ESP32-DIV v1.1.0 as the host UI shell;
  ~50 features grafted in across WiFi, Bluetooth, 2.4 GHz NRF24,
  Sub-GHz CC1101, IR, and Tools submenus (see `README.md` § Sub-features
  for the full list).
- Personal-use LICENSE, README with pin map / gotchas / ATGM336H GPS
  install guide.
- Hydra logo splash + About page.
