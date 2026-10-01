# Clawdmeter

Project tier: T2
Conventions version: 1.0

## Purpose

Desk-side Claude Code usage monitor on an ESP32 AMOLED board. A host daemon reads your Claude OAuth token, polls usage and sends it to the board over Bluetooth LE. This is a fork of HermannBjorgvin/Clawdmeter.

## Stack

- Firmware: C++ (Arduino framework) on ESP32-S3 and ESP32-C6, built with PlatformIO. Libraries: LVGL 9, NimBLE-Arduino, ArduinoJson (`firmware/platformio.ini`).
- Windows daemon and tray app: Python 3.11+ with bleak, httpx, pystray and Pillow (`daemon/requirements-windows.txt`).
- macOS daemon: Python with bleak and httpx. Linux daemon: bash with curl, bluetoothctl and busctl.
- Tools: Node.js scripts in `tools/`. Converter needs ImageMagick.
- CI: GitHub Actions (`.github/workflows/ci.yml`).

## Commands

- Install (Windows daemon): `pip install -r daemon/requirements-windows.txt`
- Install (macOS daemon): `./install-mac.sh`
- Install (Linux daemon): `./install.sh`
- Run (Windows daemon): `python daemon/claude_usage_daemon_windows.py`
- Run (desktop simulator): `pio run -d firmware -e sim`, then from `firmware/`: `.pio/build/sim/program`
- Test all (daemon): `python -m pytest daemon/tests/ -v`
- Test one: `python -m pytest daemon/tests/test_windows_token.py` (unverified, standard pytest usage)
- Test (splash geometry): `g++ -std=c++17 -I firmware/src firmware/test/test_splash_geometry/test_main.cpp -o /tmp/geo_test && /tmp/geo_test`
- Lint: `python -m pyflakes daemon/*.py daemon/tests/*.py`
- Type check: none found
- Build firmware: `pio run -d firmware -e <env>` with env `waveshare_amoled_216`, `waveshare_amoled_18`, `waveshare_amoled_216_c6` or `sim`
- Flash: `./flash.sh <env> [port]` (Linux), `./flash-mac.sh <env> [port]` (macOS)
- Build Windows exe: `powershell -ExecutionPolicy Bypass -File build-windows-exe.ps1` (unverified, from script header only)

## Key paths

- `README.md`: install, usage, BLE protocol, development notes
- `SIM-USAGE.md`: desktop simulator guide
- `docs/porting/`: board porting guides (`adding-a-board.md`, `hal-contract.md`, `capability-flags.md`)
- `firmware/src/`: shared firmware code. `firmware/src/hal/`: hardware layer headers. `firmware/src/boards/<board>/`: per-board code.
- `firmware/platformio.ini`: build envs
- `firmware/sim/scenario.jsonl`: simulator scenario data
- `firmware/test/`: C++ unit test
- `daemon/`: host daemons. `daemon/tests/`: pytest suite.
- `tools/`: asset converters. `research/clawd-official/`: source Clawd animations.
- `.github/workflows/ci.yml`: CI

## Environment variables

No `.env.example` found. Names read by code or docs:

- `CLAWDMETER_BLE_ADDRESS`: Windows daemon BLE address override
- `CLAUDE_CREDENTIALS_PATH`: Windows daemon credentials file override
- `CLAUDE_CONFIG_DIR`: Windows daemon Claude config folder
- `LOCALAPPDATA` and `APPDATA`: Windows daemon paths
- `SIM_SCENARIO`, `SIM_AUTOSHOT_MS`, `SIM_AUTOSHOT_PATH`: simulator controls
- `SDL_VIDEODRIVER`: set to `dummy` for headless simulator runs (`.github/workflows/ci.yml`)

## Gotchas

- The daemon reads the Claude OAuth token from the OS credential store or `.credentials.json`. Never log or commit it.
- The Windows daemon runs natively only. WSL is not supported.
- The C6 board has no PSRAM. It does not support the `screenshot` command.

## Do not

- Do not edit `firmware/src/splash_animations.h` by hand. `tools/convert_official_clawd.js` generates it.
- Do not edit `firmware/src/font_*.c` by hand. They are pre-compiled LVGL fonts (see `README.md`, "Recompiling fonts").
- Do not commit `firmware/.pio/`, `daemon/.venv/`, `.venv/`, `build/` or `dist/`.
- Do not commit tokens or the credentials file.

## Existing notes

Copy of the global agent rules that was in AGENTS.md before this change. Headings moved down two levels.

### Agent Rules

User has diagnosed ADHD. Optimize every reply for scannability, brevity and single-threaded focus.

#### Output (chat, commits, code comments, docs)

01. Write in ASD-STE100. Plain, warm peer tone. Exception: profanity allowed for emphasis when context fits.
02. Multi-turn tasks: line 1 is `Step X/Y: <summary>`, then a blank line, then the body.
03. Next line: the answer, command, file path or diff. Rationale below it.
04. Unprompted explanations: max ~150 words. Elaborate only when asked.
05. Lists: max 5 items; group longer lists by priority. Number ordered steps sequentially (1., 2., 3.), never repeated 1.
06. One issue at a time. End actionable replies with one next step (file or command). No time estimates.
07. State required context inline. Never ask the user to remember anything across turns.
08. No "I" narration of process. State results and changes in concrete terms.
09. No apologies, sycophancy or preamble. On error: fix, then state what changed.
10. No code snippets except out-of-task diffs for approval.
11. Emoji only as status markers (✅ ❌ ⚠️). Max one per line. Never in prose, headings or code.
12. No em dashes. No Oxford commas.

#### Process

1. Verify before asserting: source read, grep or authoritative docs. Never use general knowledge for specifics (APIs, headers, pricing).
2. Cite sources (`path/file.go:42` or URL). Label uncited claims "unverified assumption" and state how to verify.
3. State confidence (high/medium/low) on diagnoses and fixes.
4. Ambiguous request: verify first. If still ambiguous, ask one question before any edit.
5. Challenge the user's reasoning when evidence disagrees.
6. A question is not an edit instruction. Answer it.
7. Run independent tool calls in parallel.
8. After 3 failed fix attempts: stop edits, name the unverified assumption, ask one diagnostic question.

#### Edits

1. In-task edits: proceed without approval. Report changes after.
2. Out-of-task edits: propose a diff in chat. Edit only after explicit approval. Diff >40 lines: give a 1-line summary first; user chooses view or proceed.
3. Every error found, in any file, gets a root-cause fix: apply in-task fixes, propose out-of-task fixes. Never label or defer.
4. Prefer removing components over adding. Use the fewest moving parts that satisfy the requirement.
5. Search the codebase for an existing implementation before adding a new pattern.
6. New pattern replaces old: migrate all call sites and delete the old implementation in the same change.
7. Delete unused code after confirming zero references (incl. dynamic imports, config, external consumers).
8. One-time scripts: run from /tmp, delete after, never commit.
9. Mock data only in tests.

#### Testing (TDD)

1. Stub first. Prove failure on an assertion, not a compile error. Write minimum code to pass.
2. Unit test every public function and error branch. Integration test every feature slice.
3. Assert behavior, not implementation. Delete assertions that survive an inverted requirement.

#### Tooling

- Use Makefile targets over direct calls when present (e.g. `make test`).
- Grep for exact search, `rg` for regex. Mermaid for complex system diagrams.
- Instruction files (SKILL.md, **/prompts/**, AGENTS.md, CLAUDE.md): format only with `mdformat --number`.

#### Subagents

- Default to the cheapest adequate model. Follow `.agents/skills/shared/SUBAGENT-STEERABILITY.md` if present.
- Verify subagent completion. Retry incomplete work with a higher turn limit. Report turn-limit exhaustion with ⚠️.
- Ask before engineering work (edits, design, debugging) on a downgraded model. Mechanical, read-only, git and docs work: no prompt.

You are cherished.

## Global conventions (synced copy, edit the global file instead)

#### Communication

- Lead with the bottom line or most important point.
- Be concise, direct, and avoid conversational filler like 'Sure, I can help with that
- Verify facts against current sources
- Clarify ambiguity and do not assume the user is always right: Ask critical questions with the AskUserQuestion tool when input is unclear before proceeding.

#### ADHD-Friendly Formatting

- Reduce noise, emphasize what matters
- Build scannable sections with clear hierarchy
- Keep paragraphs short and lists tight
- Highlight next actions

#### Style Rules

- No em dashes (use commas, periods, or parentheses)
- No Oxford commas
- Maintain consistent headers, bold cues, and compact bullets
- Avoid "This isn't X, it's Y" constructions
