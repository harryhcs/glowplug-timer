# Glow Plug Controller — SPEC

An ESP32 firmware that drives the glow plugs of a Toyota 1HZ diesel engine through a temperature-keyed pre-glow + cold-only after-glow sequence, exposes a mobile-friendly web UI for in-vehicle tuning, and self-updates from GitHub Releases when at home.

Pinned vocabulary lives in [`CONTEXT.md`](./CONTEXT.md). Hard-to-reverse architectural decisions live in [`ADRs.md`](./ADRs.md).

## Goals

1. **Temperature-keyed pre-glow** — on power-on, the controller samples the [[temperature-reading]] once, selects the matching [[pre-glow-band]] from the saved [[pre-glow-profile]], and energises the [[glow-relay]] for that band's duration.
2. **Driver-facing pre-glow signal** — the [[dash-light]] is lit from power-on until the [[pre-glow-phase]] ends, then turns off.
3. **Cold-only after-glow** — if the boot-time [[temperature-reading]] was ≥ 600 ADC, hold the [[glow-relay]] for 5 seconds after the alternator goes high; otherwise skip after-glow entirely and de-energise the relay the moment pre-glow ends.
4. **In-vehicle tuning over Wi-Fi** — the controller hosts a `GlowPlugController` SoftAP that any phone can join, exposing a mobile-friendly web form to view the live [[temperature-reading]] + selected [[pre-glow-band]] and edit + save all 7 bands of the [[pre-glow-profile]] plus home Wi-Fi credentials.
5. **Profile survives power-off** — saved [[pre-glow-profile]] values persist across power cycles via NVS so the truck behaves the same way next start.
6. **OTA firmware updates from GitHub** — once `glowComplete == true` and home Wi-Fi has associated, the controller polls the GitHub Releases API for `harryhcs/glowplug-timer`, applies any newer release automatically, and signals progress via the [[dash-light]] and the web UI.

## Non-goals

1. **No °C calibration** — the [[temperature-reading]] stays in raw ADC counts. No sensor model, no conversion to °C.
2. **No after-glow tuning** — the 5-second duration and ≥ 600 ADC cold threshold are firmware constants, not exposed in the web UI.
3. **No authentication on the web UI** — anyone joined to the SoftAP can change the [[pre-glow-profile]]. Acceptable because the AP itself is password-gated and the device is physically in the vehicle.
4. **No fault detection / telemetry** — no current sensing on the glow circuit, no logging of starts, no error reporting. If a glow plug is open-circuit, the controller doesn't know.
5. **No "skip this update" mechanism** — if an update exists and the device can reach GitHub, it will be applied. A debug-only "check for update now" button exists on the web UI (added after v1.0.2 when opaque OTA failures made the boot-only timing unworkable to diagnose) that forces an immediate check; it can force a check sooner but cannot suppress one.

## Users

1. **Driver.** Turns the ignition. Doesn't open the web UI; just watches the [[dash-light]] and cranks when it goes out. Technical level: anyone with a driver's license. Environment: behind the wheel. Frequency: every start.

2. **Tuner.** Connects a phone to the `GlowPlugController` SoftAP, opens the web form, edits one or more [[pre-glow-band]] durations and/or the home Wi-Fi credentials, hits save. Technical level: comfortable joining a Wi-Fi network and using a web form. Environment: standing next to the vehicle with the ignition on accessories. Frequency: rare — initial setup, then seasonal tweaks before winter.

3. **Maintainer.** Pushes new firmware by tagging a release on `harryhcs/glowplug-timer` with a `.bin` asset. The vehicle picks it up on its next boot while in range of home Wi-Fi. No laptop involved beyond `git tag` + `gh release create`. USB flashing remains the bootstrap path for a brand-new ESP32. Technical level: developer comfortable with the Arduino toolchain + GitHub. Frequency: very rare.

## Architecture

- **Board:** Arduino Nano ESP32 (ABX00083, ESP32-S3, USB-C, Nano form factor with D0–D13 / A0–A7 aliases). This is the deployed board; firmware must not assume any other.
- **Toolchain:** Arduino IDE / arduino-cli with the official `arduino:esp32` core.
- **Pin map (already deployed — do not change):**
  - `D2` → `ALT_PIN`, INPUT. Reads alternator-running signal (HIGH = engine running).
  - `D3` → `DASH_LIGHT`, OUTPUT. Drives the driver-facing wait indicator (HIGH = lit). Also repurposed during OTA flash for a rapid blink pattern (250 ms on / 250 ms off).
  - `D9` → `GLOW_RELAY`, OUTPUT. Energises the glow plugs (HIGH = energised).
  - `A0` → `TEMP_PIN`, INPUT. Analog sample for the [[temperature-reading]].
- **Libraries (existing, keep):** `WiFi.h`, `WebServer.h`, `Preferences.h` (all bundled with the ESP32 Arduino core).
- **Libraries (added for OTA):** `WiFiClientSecure.h` + `HTTPClient.h` (GitHub Releases HTTPS call), `HTTPUpdate.h` (download + apply `.bin`), `ArduinoJson` (parse release JSON).
- **Networking mode:** `WIFI_AP_STA`. SoftAP `GlowPlugController` / `password123` always on. STA attempts to join saved home Wi-Fi whenever `wifi_ssid` is non-empty in NVS; failure is silent and non-blocking. See `ADR-0002`.
- **Persistence:** `Preferences` on NVS namespace `glow_final`.
- **OTA gate:** the GitHub check + download + flash is unconditionally gated on `glowComplete == true`. See `ADR-0003`.

## Data model

### Persisted (NVS, namespace `glow_final`)

| Key | Type | Default | Purpose |
|---|---|---|---|
| `t8` | int (seconds) | 5 | Pre-glow duration when [[temperature-reading]] > 800 |
| `t7` | int (seconds) | 4 | Pre-glow duration when > 700 |
| `t6` | int (seconds) | 3 | Pre-glow duration when > 600 |
| `t5` | int (seconds) | 2 | Pre-glow duration when > 500 |
| `t4` | int (seconds) | 1 | Pre-glow duration when > 400 |
| `t3` | int (seconds) | 1 | Pre-glow duration when > 300 |
| `t2` | int (seconds) | 1 | Pre-glow duration when > 200 |
| `wifi_ssid` | string | `""` | Home Wi-Fi SSID. Empty = STA disabled, AP-only. |
| `wifi_pass` | string | `""` | Home Wi-Fi password. Never written into source; set via web form on first setup. |
| `prev_fw` | string | `""` | Firmware version seen on the previous boot. Used to detect "I was just updated" on the post-OTA reboot. |

Default seconds (`5/4/3/2/1/1/1`) approximate the T1 curve from the Toyota 1HZ factory workshop manual. Significantly shorter than the previous defaults (`12/10/8/6/4/2/0`). See `ADR-0001`.

### In-memory boot-and-loop state

| Var | Type | Notes |
|---|---|---|
| `activeTarget` | int (seconds) | Selected pre-glow duration; set once during `setup()` |
| `bootTempReading` | int (ADC) | The single boot-time sample. Retained so the loop can decide whether to run after-glow. |
| `preGlowStart` | `unsigned long` (ms) | `millis()` at start of pre-glow |
| `afterGlowStart` | `unsigned long` (ms) | `millis()` when alternator went HIGH |
| `preGlowFinished` | bool | |
| `afterGlowActive` | bool | |
| `glowComplete` | bool | OTA gate — GitHub call + download + flash forbidden while false |

### Firmware constants (not user-tunable, not persisted)

| Name | Value | Purpose |
|---|---|---|
| `AFTER_GLOW_DURATION_MS` | `5000` | Length of after-glow when it runs (5 s) |
| `AFTER_GLOW_COLD_THRESHOLD` | `600` | After-glow runs only if `bootTempReading >= 600` |
| `PRE_GLOW_BAND_THRESHOLDS` | `{800,700,600,500,400,300,200}` | ADC breakpoints between bands |
| `FIRMWARE_VERSION` | semver string baked at build time, e.g. `"1.0.0"` | Compared against the GitHub release tag |
| `UPDATE_CHECK_URL` | `https://api.github.com/repos/harryhcs/glowplug-timer/releases/latest` | GitHub Releases API endpoint |
| `AP_SSID` | `"GlowPlugController"` | SoftAP SSID (existing) |
| `AP_PASS` | `"password123"` | SoftAP password (existing) |

## API

All routes are served by the built-in `WebServer` on port 80, reachable at `http://192.168.4.1/` from a phone joined to the SoftAP. No authentication beyond the SoftAP password.

| Method | Path | Purpose | Body / params |
|---|---|---|---|
| GET | `/` | Render the config page — see UI section | none |
| POST | `/save` | Persist submitted band durations + Wi-Fi credentials to NVS, then 303-redirect to `/` | Form fields: `t8`, `t7`, `t6`, `t5`, `t4`, `t3`, `t2`, `wifi_ssid`, `wifi_pass` |
| GET | `/status` | JSON snapshot for debugging | none |

`/status` response shape:

```json
{
  "temp": 762,
  "target_s": 4,
  "pre_glow_done": true,
  "after_glow_active": false,
  "glow_complete": true,
  "firmware": "1.0.0",
  "sta_connected": true,
  "last_ota": "up to date"
}
```

## UI

Single page at `/`, dark themed mobile card layout. Unchanged from the deployed firmware except for the additions listed under "New" below.

### Read-only block (top of card)

- `A0 Reading: <adc>` — live [[temperature-reading]], re-sampled on each request.
- `Target: <N>s` — currently selected [[pre-glow-band]] duration.
- **New:** `Firmware: vX.Y.Z` — value of `FIRMWARE_VERSION`.
- **New:** `Home Wi-Fi: connected | not connected | not configured` — STA state.
- **New:** `Last OTA: <state>` — one of:
  - `"up to date"`
  - `"updated from vA.B.C → vX.Y.Z"` (shown on the first `/` render after an OTA reboot)
  - `"check failed: <reason>"`
  - `"deferred — glow sequence active"`
  - `"skipped — STA not connected"`

### Form (POST to `/save`)

- 7 number inputs labelled `A0 > 800`, `A0 > 700`, …, `A0 > 200`, each in seconds.
- **New:** `Home Wi-Fi SSID` — text input.
- **New:** `Home Wi-Fi password` — password-type input (masked).
- Single `SAVE SETTINGS` submit button.

### Styling

Unchanged. `#121212` background, `#1e1e1e` card, `#e67e22` orange accents, max-width 400 px, viewport meta tag for mobile. No redesign — the deployed look works.

### Dash light during OTA flash

While `HTTPUpdate` is downloading + writing a new image, the `DASH_LIGHT` blinks 250 ms on / 250 ms off. Safe because OTA only runs after `glowComplete == true`, by which time the dash light is otherwise idle. The new firmware reboots into its normal pre-glow behavior.

## Acceptance

1. **Pre-glow timing per band.** With a pot on `A0`, dial to ADC > 800 and power on: `GLOW_RELAY` reads HIGH, `DASH_LIGHT` reads HIGH for exactly `t8` seconds (default 5), then `DASH_LIGHT` goes LOW. Repeat for each band — each duration matches its saved value.

2. **Dash light signals "wait, then crank".** `DASH_LIGHT` is HIGH from power-on for exactly the band's pre-glow duration, then LOW.

3. **Cold-only after-glow fires for 5 s.** With `A0` ≥ 600 ADC at boot: after pre-glow finishes, pulling `D2` HIGH causes `GLOW_RELAY` to stay HIGH for exactly 5 s, then drop LOW. With `A0` < 600 ADC at boot: pulling `D2` HIGH drops `GLOW_RELAY` LOW *immediately* — no after-glow.

4. **Web UI tuning round-trip.** Join `GlowPlugController` SoftAP, load `http://192.168.4.1/`, change `t8` from 5 to 7, save, reload — new value displays in the form *and* on the next boot at A0 > 800 the dash light stays lit for 7 s instead of 5.

5. **Profile survives power-off.** Change any band, save, power-cycle the controller, reload `/` — saved value still shown. Same for `wifi_ssid` / `wifi_pass`.

6. **OTA happy path.** With home Wi-Fi credentials saved and the controller in range: tag a new release on `harryhcs/glowplug-timer` with a `.bin` asset. Power-cycle the controller. After `glowComplete` flips true, the dash light blinks rapidly (download + flash), the controller reboots, `/` now shows the new `Firmware: vX.Y.Z` and `Last OTA: "updated from vA.B.C → vX.Y.Z"`.

7. **OTA never interrupts the glow sequence.** With an available update *and* STA connected at boot, `/` shows `Last OTA: "deferred — glow sequence active"` while pre-glow + after-glow are running. The dash light blinks only after `glowComplete == true`. The pre-glow + after-glow durations measured in #1–#3 are unchanged whether or not an OTA is pending.

## Out of scope

1. **Multiple firmware channels (stable vs beta).** GitHub `latest` is the only channel.
2. **NVS reset from the web UI.** No `POST /reset` route; corrupted state requires a USB re-flash.
3. **Per-vehicle / per-driver profiles.** One [[pre-glow-profile]] per controller.
