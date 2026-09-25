# Project context

ESP32-S3 firmware for a desk-side Claude Code usage monitor. Each supported
board lives in its own `firmware/src/boards/<name>/` folder and is selected
via PlatformIO's `build_src_filter`. Adding a board means dropping in a new
folder + a new `[env:...]` block — `main.cpp`, `ui.cpp`, and `splash.cpp`
never see board-specific code. See [`docs/porting/adding-a-board.md`](docs/porting/adding-a-board.md).

Three reference ports today:

- `boards/waveshare_amoled_216/` — original Waveshare ESP32-S3-Touch-AMOLED-2.16 (CO5300, 480×480 square, CST9220 touch, IMU rotation). Build env: `waveshare_amoled_216`.
- `boards/waveshare_amoled_18/` — Waveshare ESP32-S3-Touch-AMOLED-1.8 (368×448 portrait, XCA9554 IO expander). Build env: `waveshare_amoled_18`. **Two panel revisions are auto-detected at boot** (`board_rev()` in `board_init.cpp`, enum in `board_rev.h`): original = SH8601 display + FT3168 touch (0x38); later = CO5300 display + CST816 touch (0x15). One binary drives both.
- `boards/waveshare_amoled_216_c6/` — ESP32-C6 sibling of the AMOLED-2.16 (same CO5300 panel/touch/PMU/IMU). Build env: `waveshare_amoled_216_c6`. Single-core RISC-V, **no PSRAM** (shared code gates on `BOARD_HAS_PSRAM` to fall back to `MALLOC_CAP_INTERNAL` and shrink LVGL/splash buffers), BLE 5.3 only (no classic BT), 16 MB flash with a custom partition table. Screenshot capture is unsupported on this board (no room for a full RGB565 framebuffer in internal SRAM).

The shared code calls a small HAL (`firmware/src/hal/`) that each board implements: display, touch, input, power, IMU. Optional features are guarded by `BoardCaps` (runtime) and `BOARD_HAS_*` (compile-time) rather than `#ifdef BOARD_*`.

Connects to a host daemon over BLE; daemon polls Anthropic API for usage data. This file is for future Claude Code sessions to bootstrap quickly. Read this first.

## Hardware (critical pins)

### AMOLED-2.16 (original)
- Display: **CO5300** AMOLED via QSPI (CS=12, SCLK=38, SDIO0..3=4..7, RST=2)
- Touch: **CST9220** via I2C (SDA=15, SCL=14, INT=11, addr=0x5A)
- PMU: **AXP2101** on same I2C bus (addr=0x34) — battery, USB VBUS, PWR button IRQ
- IMU: **QMI8658** on same I2C bus (addr=0x6B) — accelerometer for auto-rotation
- Buttons: GPIO 0 (left → Space/voice-mode), GPIO 18 (right → Shift+Tab/mode-toggle), AXP PKEY (middle → cycle screens; on splash → cycle animations)

### AMOLED-1.8 (newer port)
**Two hardware revisions ship under this name; the firmware probes I2C at boot and picks drivers automatically (`board_rev()`):**
- Display: **SH8601** (original) or **CO5300** (later rev) AMOLED via QSPI (CS=12, **SCLK=11** ← different!, SDIO0..3=4..7, RST routed via XCA9554 EXIO1). Both are `Arduino_OLED` subclasses held behind one base pointer in `display.cpp`. The CO5300's 368-wide active area starts at GRAM column 16, so it gets `CO5300_COL_OFFSET 16` to center; SH8601 needs none.
- Touch: **FT3168** @ 0x38 (original) or **CST816** @ 0x15 (later rev), via I2C (SDA=15, SCL=14, INT=21). Both expose the same FocalTech-style data layout at regs 0x02..0x06, so one inline reader in `touch.cpp` serves both — only the address differs. Avoids vendoring the GPLv3 `Arduino_DriveBus` library. Revision is detected by which touch address ACKs (CST816 present ⇒ CO5300 panel).
- PMU: AXP2101 @ 0x34 (same chip as 2.16 — `XPowersLib` reused; battery is an optional kit add-on but PMU + charging circuitry are populated)
- IMU: QMI8658 @ 0x6B (same chip — initialized for I2C bus health, rotation logic disabled)
- IO expander: **XCA9554 / PCA9554** @ I2C 0x20. Gates LCD_RST, TP_RST, audio amp enable, and reads the PWR button. **`io_expander_init()` MUST run before `gfx->begin()` or `ft3168_init()`** — otherwise display/touch stay in reset and silently fail. PWR button is on EXIO4, active HIGH (verified empirically with the deleted `iox` serial debug command).
- Orientation: **fixed at 0°**. IMU auto-rotation is disabled; `rotate_strip()` / `handle_rotation_change()` are excluded via `#ifndef BOARD_AMOLED_18`.
- Buttons: GPIO 0 (BOOT → Space/voice-mode), XCA9554 EXIO4 (PWR → cycle screens; on splash → cycle animations). **No third button** (GPIO 18 button doesn't exist on this board).

## Architecture

```text
firmware/src/
  hal/                      — board-agnostic interfaces shared code calls into
    board_caps.h            — runtime BoardCaps struct (W, H, button_count, has_* flags)
    display_hal.h           — init / begin / set_brightness / draw_bitmap / tick / round_area
    touch_hal.h             — init / read(&x, &y, &pressed)
    input_hal.h             — init / is_held(PRIMARY|SECONDARY)
    power_hal.h             — init / tick / battery_pct / is_charging / pwr_pressed (edge)
    imu_hal.h               — init / tick / rotation_quadrant
  boards/
    waveshare_amoled_216/   — CO5300 + CST9220 + AXP PKEY + QMI8658 rotation
    waveshare_amoled_18/    — SH8601 + FT3168 + AXP + XCA9554 (PWR via EXIO4), no rotation
    waveshare_amoled_216_c6/ — ESP32-C6 sibling of amoled_216, no PSRAM, BLE 5.3 only
    template/               — copy this to bootstrap a new port
  main.cpp                  — setup() + loop(): HAL calls only, zero #ifdef BOARD_*
  ui.{h,cpp}                — 2-screen UI (splash, usage — the bluetooth pairing screen was folded into usage). compute_layout() picks fonts/positions from board_caps() (responsive — current breakpoint: H >= 460 → large, else compact)
  splash.{h,cpp}            — 60×60-stage art engine + corner mascot. Cell = min(W,H)/60, centered.
  splash_geometry.h         — pure-C cell/scale math, unit-tested standalone (firmware/test/test_splash_geometry/)
  clawd_still.h             — static idle-pose bitmap, C6/no-PSRAM fallback for the animated mascot
  flipcard.{h,cpp}          — split-flap digit card for the "Limit reached" HH:MM countdown. Flip = height-animated flaps about the hinge (no LVGL transforms, so no ARGB layer from the 64 KB LVGL heap)
  ble.{h,cpp}               — NimBLE peripheral: custom data service + HID keyboard
  data.h                    — UsageData struct
  icons.h                   — icon arrays. Battery (5×) are RGB565A8 with alpha; rest are raw RGB565.
  logo.h                    — 80×80 RGB565 logo. No longer used by firmware (ui.cpp uses clawd_still.h) — kept only because `daemon/icon_assets.py::load_logo_rgba()` parses it at runtime to build the Windows tray icon.
  font_*.c                  — pre-compiled LVGL 9 bitmap fonts (Tiempos 56/44/34, Styrene 48/28/24/20/16/14/12, Mono 32/18, DejaVu Mono digits-only 112/88 for the flip cards)
  splash_animations.h       — generated, do not hand-edit
docs/porting/               — adding-a-board.md, hal-contract.md, capability-flags.md
```

Each board folder contains: `board.h` (pins, I2C addresses, `BOARD_HAS_*` flags),
`board_init.cpp` (Wire.begin + any IO expander), `display.cpp`, `touch.cpp`,
`input.cpp`, `power.cpp`, `imu.cpp`, `caps.cpp` (the `BoardCaps` instance), plus
any board-private hardware drivers (e.g. `io_expander.{h,cpp}` on AMOLED-1.8).
PlatformIO's `build_src_filter` includes shared code + one board's folder per env.

## Build / flash

```bash
pio run -d firmware -e waveshare_amoled_216                                     # build 2.16 (default original)
pio run -d firmware -e waveshare_amoled_18                                      # build 1.8 (new port)
pio run -d firmware -e waveshare_amoled_18 -t upload --upload-port /dev/cu.usbmodem101   # flash 1.8 on macOS
pio run -d firmware -e waveshare_amoled_216 -t upload --upload-port /dev/ttyACM0         # flash 2.16 on Linux
```

If `pio` isn't on PATH: try `~/.platformio/penv/bin/pio` (Linux/macOS pio install) or `brew install platformio` on macOS.

Device path differs by OS: `/dev/cu.usbmodem*` on macOS, `/dev/ttyACM0` on Linux. Both expose the ESP32-S3 native USB-JTAG (no boot-mode dance needed).

There's a 4th env, `[env:sim]` (native desktop SDL2 simulator — see below); `platformio.ini` has no `default_envs`, so a bare `pio run -d firmware` with no `-e` now also attempts `sim` and fails without `libsdl2-dev` installed. `flash.sh`/`flash-mac.sh` always pass `-e "$BOARD"` so they're unaffected.

## Desktop simulator

`pio run -d firmware -e sim` builds a native SDL2 desktop binary (`platform = native`, no ESP-IDF toolchain) — the exact same `main.cpp`/`ui.cpp`/`splash.cpp` running against an SDL2-backed HAL instead of real hardware: mouse as touch, keyboard as buttons, a JSONL-scenario-playback stub in place of `ble.cpp`. See [`SIM-USAGE.md`](SIM-USAGE.md) for the full control map, scenario format, and headless-screenshot recipe (`SDL_VIDEODRIVER=dummy SIM_AUTOSHOT_MS=...`). Requires `libsdl2-dev` (`apt install libsdl2-dev` / `brew install sdl2`). Good for fast UI iteration; it does **not** exercise panel-specific behavior (CO5300 rotation remapping, C6's no-PSRAM direct-draw path, real flush timing) — always confirm panel-related changes on real hardware too.

## CI

`.github/workflows/ci.yml` runs on every push/PR to `main`: `daemon-tests` (pytest + pyflakes over `daemon/`, seconds) and `firmware-build` (matrix `pio run` across all 3 hardware board envs + `sim`, cached `~/.platformio`). GitHub-hosted runners have normal internet access, so the firmware job can reach `dl.espressif.com` for the ESP-IDF toolchain — a sandboxed Claude Code session usually can't (see its egress policy) and must rely on this CI to verify firmware compiles. The `sim` matrix entry additionally installs `libsdl2-dev` and runs a headless smoke test (`SIM_AUTOSHOT_MS`) that actually exercises `setup()` + real `loop()` iterations, uploading the resulting screenshot as a build artifact.

## QA your own UI changes — don't ask the user

The firmware ships a `screenshot` serial command that dumps the LVGL framebuffer. A second serial command, `fakelimit [mins]` / `fakelimit off` (`main.cpp`), fakes a 100% session so the "Limit reached" split-flap screen can be checked on hardware without a real exhausted quota; while on, real BLE usage is ACKed but not shown, and the fake payload is re-sent every 30s so the view stays fresh (the usage view still needs a live BLE link). `./screenshot.sh out.png [port]` captures a PNG sized to the active display (480×480 or 368×448). **Use this on every UI iteration** — Read the PNG with the Read tool, verify the change visually, iterate. Script auto-picks the macOS/Linux default port and falls back to pio's bundled Python if pyserial isn't on the system Python. If no hardware is attached (e.g. a sandboxed session), `pio run -d firmware -e sim` + `SDL_VIDEODRIVER=dummy SIM_AUTOSHOT_MS=<ms> .pio/build/sim/program` gets the same kind of screenshot without any board at all — see `SIM-USAGE.md`.

The boot screen is `SCREEN_SPLASH` and only advances on a physical button press, so a fresh flash will sit on the splash. To screenshot the screen you're actually editing without asking the user to press a button, **temporarily change the default boot screen** in `main.cpp` (search for `ui_show_screen(SCREEN_SPLASH);`) to `SCREEN_USAGE` (the only other screen — `ui.h`'s `screen_t` is just `SCREEN_SPLASH` / `SCREEN_USAGE` / `SCREEN_COUNT`; pairing/bluetooth state now renders as a status line on the usage screen, not a separate screen), do your iteration, then revert before committing.

## Critical gotchas

1. **CO5300 cannot rotate.** Its MADCTL only supports axis flips, not column/row exchange. Rotation is done by **CPU pixel remapping inside `display_hal_draw_bitmap`** in `boards/waveshare_amoled_216/display.cpp`. We use **PARTIAL render mode with strip rotation** (small 480×40 strips, fast). On rotation change → AMOLED brightness flash → force redraw (handled inside `display_hal_tick`).
2. **OPI PSRAM** required: `board_build.arduino.memory_type = qio_opi` in platformio.ini. Without this, `MALLOC_CAP_SPIRAM` returns NULL and the screen is black.
3. **pioarduino platform required.** GFX Library for Arduino needs Arduino Core 3.x (`esp32-hal-periman.h`), not the 2.x that standard `espressif32` ships. We pin `pioarduino/platform-espressif32` 55.03.38-1.
4. **LVGL 9 font patching.** `lv_font_conv` outputs LVGL 8 format. Must remove `#if LVGL_VERSION_MAJOR >= 8` guards, drop `.cache` field, add `.release_glyph`, `.kerning`, `.static_bitmap`, `.fallback`, `.user_data`. Without patching, fonts render invisible.
5. **Touch reading is centralized inside each board's `touch.cpp`.** The HAL `touch_hal_read()` is called once per loop from `my_touch_cb`; the board's implementation owns its latched `touch_pressed/x/y` state. Don't call the underlying controller from anywhere else — CST9220's `getPoint()` etc. do a full I2C transaction and concurrent callers consume each other's data.
6. **Even-aligned flush regions.** `display_hal_round_area` (called from `rounder_cb`) is what each board uses to enforce this. Required on CO5300, harmless on SH8601.
7. **Touch axis swap/mirror is per-board.** The 2.16's CST9220 needs `setSwapXY(true)` + `setMirrorXY(true, false)` — applied inside `boards/waveshare_amoled_216/touch.cpp::touch_hal_init()`. New ports apply their own.
8. **LVGL RGB565A8 is planar.** `w*h` RGB565 pixels followed by `w*h` alpha bytes; `data_size = w*h*3`, `stride = w*2`. Use `init_icon_dsc_rgb565a8()` for icons that overlap non-uniform backgrounds (e.g. battery over splash). Lucide source PNGs are black-on-transparent — converter must tint to white or icons render invisible. See `tools/png_to_lvgl.js`.
9. **Per-board pre-init is `board_init()`.** Each board's `board_init.cpp` brings up `Wire` and any reset-gating IO expander BEFORE `display_hal_init()`. Skipping the IO expander release on AMOLED-1.8 leaves SH8601 + FT3168 in reset and they silently fail to probe.
10. **No `#ifdef BOARD_*` in shared code.** The whole point of the refactor — if you're about to add one, you probably want a `BoardCaps` field or a per-board file instead. See `docs/porting/capability-flags.md`.
11. **BLE is single-owner-locked.** `ble.cpp` persists the bonded identity address of the first machine to pair (NVS namespace `"clawd"`) and rejects/un-bonds any other machine that tries to pair or write usage data — otherwise any nearby BLE central could write fake usage data or steal the display. To hand the board to a different machine, hold PWR ~3s then release (`pair_tick()` in `main.cpp` → `ble_clear_bonds()`), which clears bonds **and** releases ownership.
12. **Windows clamps the BLE supervision timeout to 2s** once the daemon's GATT session is active (vs. 9.6s while only the OS HID driver holds the link), and this board's antenna sees RF nulls that don't survive a 2s window — the cause of "ERROR_CANCELLED / Device disconnected" churn seen only on Windows. Fixed via PPCP build flags (`MYNEWT_VAL_BLE_SVC_GAP_PPCP_*` in `platformio.ini`) plus a deferred one-shot connection-parameter request in `ble.cpp` (`onConnParamsUpdate` / `onAuthenticationComplete` arm it, `ble_tick()` sends it ~2s later so it doesn't race Windows' own update transaction).

## Icons

`tools/png_to_lvgl.js <input.png> <symbol> [W_MACRO] [H_MACRO] [--tint=RRGGBB | --no-tint]` converts an alpha PNG to RGB565A8. Default tint is white (`0xFFFFFF`) — necessary for Lucide PNGs. Splice output into `firmware/src/icons.h` and use `init_icon_dsc_rgb565a8()` in ui.cpp. Currently only the 5 battery icons use this format; the rest are still raw RGB565 baked over the panel background, fine because they live inside opaque zones.

## Splash animations

17 official Anthropic "Clawd" mascot animations (GIF/Lottie source), archived
with provenance notes in [`research/clawd-official/`](research/clawd-official/CLAUDE.md).
Pipeline:

```bash
node tools/convert_official_clawd.js   # research/clawd-official/* → firmware/src/splash_animations.h
```

Requires ImageMagick (`convert`/`identify` on PATH). Each animation carries its
own crop size, stage offset, and ≤16-color palette (cell values index it) — see
`firmware/src/splash_geometry.h`/`splash.cpp` for the 60×60-stage compositing,
intro→loop→outro playback, and the corner-mascot state machine that reuses this
same data. Default boot screen. The pre-generated header is checked in — you
don't need ImageMagick unless you're re-running the converter, e.g. after
updating a source asset in `research/clawd-official/`.

**Provenance / licensing**: this uses Anthropic's own copyrighted "Clawd"
mascot art without an explicit license grant — see the "Licensing gray area
warning" in the root [`README.md`](README.md) and the sourcing notes in
[`research/clawd-official/CLAUDE.md`](research/clawd-official/CLAUDE.md).

## User profile / preferences

See `~/.claude/projects/.../memory/` files for persistent context (user is an embedded-beginner senior dev, brand-conscious, prefers iterative UI refinement, dislikes me authoring my own art when third-party assets are intended). Always read those memory files at session start.

## Recent session highlights

- **Split-flap "Limit reached" screen (2026-09-25).** When the 5h session hits 100%, the usage view swaps to a "Limit reached" headline (Tiempos 56, or 44 on the 1.8 where 56 doesn't fit) over an HH:MM countdown on 4 red split-flap cards (`flipcard.{h,cpp}`, colors `THEME_FLAP_HI/LO` in `theme.h`). The "Usage" title hides on this sub-view. Cards snap on entry, flip only the digits that change (per-minute local tick + re-anchor on each poll), and hold at 00:00. Layout values live in `compute_layout()` (`limit_title_y`, `flip_*`). The sim scenario has a "limit reached" pair of states to watch a flip.
- **Official Clawd splash art + corner mascot (2026-09-01, ported from upstream `HermannBjorgvin/Clawdmeter`).** Replaced the scraped claudepix.vercel.app fan-art splash engine (20×20 grid, `tools/scrape_claudepix.js`) with Anthropic's own official "Clawd" mascot art on a new 60×60-stage engine (`splash_geometry.h`, rewritten `splash.cpp`) with intro→loop→outro playback and foot-locked walk translation. PSRAM boards also gained an animated corner mascot on the usage screen (idles, plays rate-scaled acts, walks off/lurks/walks back — `splash_mascot_*` in `splash.cpp`); the no-PSRAM path (C6) was rewritten to bypass the LVGL canvas entirely (dirty-cell diff + direct `display_hal_draw_bitmap()`) since the old canvas+scale approach cost 100-220ms/frame at the new 60-grid size, and falls back to a static `clawd_still.h` bitmap in place of the mascot. `firmware/src/logo.h` is no longer used by firmware (kept only for `daemon/icon_assets.py`'s Windows tray icon parser). Landed alongside a native SDL2 desktop simulator (`[env:sim]`, see `SIM-USAGE.md`) so this kind of UI work can be iterated and screenshotted without flashing hardware.
- **Windows BLE reliability + owner-lock security fix (2026-09-01, ported from upstream `HermannBjorgvin/Clawdmeter`).** Windows-specific supervision-timeout churn fixed via PPCP build flags + deferred conn-param request in `ble.cpp` (see gotcha #12). BLE writes now require a bonded+encrypted link from a single NVS-persisted owner machine (gotcha #11) — previously any nearby BLE central could write usage data. Windows daemon gained a bonded-address PnP fallback (device stops advertising once Windows holds it connected) and hardened WinRT bare-fault handling with a crash-supervised restart loop in the tray. This repo was a point-in-time copy of upstream (not a live fork) and had drifted ~90 commits behind; these were the highest-value fixes identified by comparing the two. `waveshare_amoled_216_c6` board and the 2-screen (splash/usage) UI were already present but undocumented — now reflected above.
- **Device-abstraction refactor (2026-05-18).** All board-conditional code moved out of shared files into `boards/<name>/` and behind a HAL in `hal/`. ~30 `#ifdef BOARD_*` blocks went to zero. UI is responsive via `compute_layout()` driven by `board_caps()`. New ports add a folder + a PlatformIO env — no shared file edits.
- Added second board port: Waveshare AMOLED-1.8 (368×448 portrait, SH8601, FT3168, XCA9554 IO expander).
- Migrated from Panlee SC01 Plus (480×320 IPS) to Waveshare 2.16" AMOLED (480×480 square). Full hardware/library swap.
- Added IMU auto-rotation, battery indicator, USB-state-aware screen switching.
- Added splash screen with scraped pixel-art animations and 3-button physical input layout.
- Fonts and icons re-scaled ~1.9× for the higher-DPI panel.
- All UI margins widened to 20px to clear the rounded display corners.
- Battery icons converted to RGB565A8 alpha so they blend cleanly over the splash animations.

## Daemon / host side

Bash daemon (`daemon/claude-usage-daemon.sh`) reads OAuth token, polls Anthropic API, sends JSON over BLE GATT. Run with `systemctl --user start claude-usage-daemon`. The unit file's `ExecStart` is the absolute path to the script — repoint it when switching between the worktree and the main checkout.

**Discovery & resilience:**

- Connects by name (`"Clawdmeter"`) on first run, caches resolved MAC at `~/.config/claude-usage-monitor/ble-address`. ESP32 BLE addresses are factory-burned per-chip, so swapping any board invalidates the cache.
- On connect failure: cache is dropped AND device is removed from bluez (`bluetoothctl remove`) so the next scan won't re-pick a dead MAC. Multi-candidate scans pick `head -1` and let the failure cycle converge.
- `POLL_INTERVAL=60`, `TICK=5`. Inner loop wakes every 5s to detect disconnects fast; polls Anthropic when 60s elapsed OR when ESP fires a refresh request.
- **Windows (`daemon/claude_usage_daemon_windows.py`, run via `tray_windows.py`)**: once paired, Windows keeps the device connected as a bonded HID keyboard, so it *stops advertising* and a plain scan can never find it again. `acquire_target()` scans first (works on a fresh boot) then falls back to `discover_bonded_address()`, which recovers the MAC from the Windows PnP table (`Get-PnpDevice`) and hands `BleakClient` a `BLEDevice` built from it — a bare address string forces WinRT through a scan that will never succeed for a non-advertising bonded device. Override with `CLAWDMETER_BLE_ADDRESS` to skip PnP lookup. `tray_windows.py`'s `_run_daemon` supervises `daemon_main()` and auto-restarts it with capped backoff on crash (bleak's WinRT backend can raise bare `AssertionError`/`OSError` that aren't wrapped as `BleakError`) — a clean stop (Quit) sets `_quit_requested` first so the supervisor never resurrects it.

**GATT characteristics on service `4c41555a-...0001`:**

- `...0002` RX — daemon writes JSON usage payload here.
- `...0003` TX — firmware notifies ack/nack (daemon doesn't subscribe).
- `...0004` REQ — firmware fires `0x01` notify in `onSubscribe` if `has_received_data` is false. Daemon subscribes via `setsid bash -c "stdbuf -oL dbus-monitor … | awk …"`; awk drops a flag file the inner loop picks up. See the `feedback_dbus_monitor_pipe` memory for the three subtle gotchas (pipe buffering, busctl-exits race, `wait` blocking on pipeline jobs).
