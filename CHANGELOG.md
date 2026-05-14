# Changelog

All notable changes to Hydra are recorded here. Version numbers follow
the convention documented in [`version.h`](version.h). New entries are
added by the `/bump-hydra` Claude skill (see
`.claude/commands/bump-hydra.md`) at the same time the version macros
are bumped.

## [Unreleased]

## [0.1.0] — 2026-05-14

- **New SubGHz feature: `Record .sub` (submenu idx 8).** Captures a raw
  OOK signal off the air on a user-selected WardriveConfig frequency,
  saves it to SD as a Flipper-compatible `/hydra/sub_files/rec_NNN.sub`
  file. SubReplay (idx 6) can play the result back without modification
  — closes the capture-then-replay loop on-device without needing a
  Flipper or other external tool to seed the SD card.
- Implementation in `sub_record.{h,cpp}`. CC1101 in OOK 650 async RX,
  ESP32 polls GDO0 for edges, records signed-microsecond timing samples
  into a 2000-entry buffer (~8 KB DRAM, shared with SubReplay via
  `subSampleBuf`). Stops on silence (250 ms after last edge),
  buffer-full, listen-timeout (30 s with no signal), or SELECT.
- Auto-incrementing `rec_NNN.sub` filenames so repeated captures don't
  collide. Discard option re-listens on the same freq.
- **rc-switch protocol decode runs in parallel with the raw capture.**
  When the signal matches one of rc-switch's 12 supported protocols
  (PT2262/Princeton, HT12/EV1527, HT6/HT8, Conrad RS-200, several short-
  pulse variants), the captured screen shows the protocol name + code
  + bit length, and the saved .sub file gets `# Hydra_RCSwitch_*`
  comment headers. Stock Flipper still treats it as RAW; future Hydra
  tooling can pick up the decoded form to skip bit-banging on replay.
- **New SubGHz feature: `Send Code` (submenu idx 9).** Manual
  transmitter for known fixed-code signals. Phase-based UI walks the
  user through Protocol (1..12) → Frequency (WardriveConfig channels) →
  Code (8-digit hex entry with cursor) → Bit length (4..32, defaults to
  24) → Ready. UP transmits, LEFT steps back, SELECT exits. Useful for
  resending codes you already know without first having to capture
  them. Implementation in `sub_sendcode.{h,cpp}`.

## [0.0.3] — 2026-05-14

- **Pin map correction (verified against v1 shield schematic
  `Previous versions/ESP32-DIV v1/Schematic/ESP32DIV-SHIELD.jpg`)**:
  - **CC1101 CSN is GPIO 27, not GPIO 5.** Hydra v0.0.2 documented
    GPIO 5 based on the library's ESP32 default — but that default
    was never correct for this board. The v0.0.2 docs are corrected
    in `Hydra/README.md` and `plans/01-hardware-divv1.md`.
  - **NRF24 numbering corrected:** NRF1 (U2) = GPIO 4/5; NRF2 (U3) =
    26/27; NRF3 (U4) = 16/17. Older docs had NRF1 and NRF3 swapped.
- **CC1101 silent-failure bug fixed.** Every SubGHz feature was calling
  `ELECHOUSE_cc1101.Init()` with no `setSpiPin()` override, so the
  driver drove the library default SS=5 — a pin where no CC1101 chip
  exists on DIV v1. The actual CC1101 CSN (GPIO 27) was left floating,
  so every `setMHZ`, `setModulation`, `setRx`, `setSidle` call silently
  failed. Result: SubGHz features never actually worked correctly on
  any cifertech v1.1.0 build or Hydra v0.0.1/v0.0.2.
  - New `cc1101InitForDivV1()` helper in `sub_shared.h` calls
    `setSpiPin(18,19,23,27)` + `setGDO(26,16)` before `Init()`.
  - All 8 CC1101 init sites switched to the helper.
- **GPS pins remapped to shield-header-accessible GPIOs:**
  - Old: GPIO 32 (RX) / 25 (TX) — XPT2046 touchscreen lines; calling
    `Gps::begin()` killed touch for the rest of the session, and
    these pins are not on the exposed shield header.
  - New: GPIO 17 (RX) / 4 (TX) — both reachable from the silkscreened
    `IO17` and `IO4` pads on the 10×2 expansion header. Touch is
    preserved when GPS is in use.
  - README "Adding a GPS — ATGM336H" section rewritten with the new
    wiring table and the pin-sharing trade-offs (GPIO 17 = NRF24 #3
    CSN, currently dormant; GPIO 4 = TFT backlight + NRF24 #1 CE,
    backlight is passive HIGH so coexistence works).
- `subghzReleasePinsFromNrf()` now also releases GPIO 27 (added with
  the CSN fix).

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
