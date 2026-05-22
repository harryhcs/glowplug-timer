# glowplug-timer

ESP32 firmware that drives the glow plugs of a Toyota 1HZ diesel engine through a temperature-keyed pre-glow + cold-only after-glow sequence, exposes a mobile-friendly web UI for in-vehicle tuning, and self-updates from GitHub Releases when at home.

The canonical specification lives in [`SPEC.md`](./SPEC.md). Pinned vocabulary is in [`CONTEXT.md`](./CONTEXT.md). Hard-to-reverse architectural decisions are in [`ADRs.md`](./ADRs.md). When the README and those documents disagree, those documents win.

## What it does

On every power-on the controller:

1. Samples the analog temperature sensor on `A0` once.
2. Picks the matching pre-glow band from the saved profile (see [Defaults](#defaults)) and energises the glow relay for that band's duration. The dash light is held HIGH for the same interval so the driver knows to wait.
3. After pre-glow finishes, the dash light goes LOW (driver can crank).
4. When the alternator signal on `D2` goes HIGH (engine running), runs a 3-second after-glow — **only** if the boot-time reading was ≥ 700 ADC ("really cold"). Otherwise the relay drops the moment pre-glow ends.
5. Once the full glow sequence is done, the controller checks GitHub Releases for a newer firmware build and self-updates if one is available.

The factory 1HZ T2 after-glow curve (up to 120 s) is deliberately **not** followed — see [`ADR-0001`](./ADRs.md). The T1 (pre-glow) curve is honored as the default profile.

## Hardware

- **Board:** Arduino Nano ESP32 (ABX00083, ESP32-S3, USB-C, Nano form factor). Firmware assumes this board specifically — pin aliases `D2`–`D9` and `A0` are required.
- **Pin map (deployed — do not change):**

  | Pin   | Role         | Direction | Notes                                                                                  |
  | ----- | ------------ | --------- | -------------------------------------------------------------------------------------- |
  | `D2`  | `ALT_PIN`    | INPUT     | Alternator-running signal. HIGH = engine running.                                      |
  | `D3`  | `DASH_LIGHT` | OUTPUT    | Driver-facing wait indicator. HIGH = lit. Rapid blink (250 ms on / 250 ms off) during OTA flash. |
  | `D9`  | `GLOW_RELAY` | OUTPUT    | Energises the glow plugs. HIGH = energised.                                            |
  | `A0`  | `TEMP_PIN`   | INPUT     | Analog temperature sample, raw ADC (0–1023). Not converted to °C.                      |

## Tuning over Wi-Fi

The controller always hosts a SoftAP. Join it from any phone:

- **SSID:** `GlowPlugController`
- **Password:** `password123`
- **URL:** `http://192.168.4.1/`

The web form shows the live A0 reading, the currently selected band, the firmware version, and home Wi-Fi status. From it you can edit any of the 7 pre-glow band durations and save home Wi-Fi credentials (so the controller can reach the internet for OTA updates). Settings persist in NVS and survive power-off.

There is no authentication beyond the SoftAP password and no °C calibration — temperature stays in raw ADC counts everywhere. After-glow is **not** user-tunable.

`GET /status` returns a JSON snapshot useful for debugging:

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

## Defaults

The default pre-glow profile approximates the Toyota 1HZ factory T1 curve:

| Boot A0 reading | Pre-glow seconds | NVS key |
| --------------- | ---------------- | ------- |
| > 800           | 5                | `t8`    |
| > 700           | 4                | `t7`    |
| > 600           | 3                | `t6`    |
| > 500           | 2                | `t5`    |
| > 400           | 1                | `t4`    |
| > 300           | 1                | `t3`    |
| > 200           | 1                | `t2`    |
| ≤ 200           | 0 (skip)         | —       |

After-glow is a 3-second constant, gated on boot A0 ≥ 700. Both the threshold and the duration are firmware constants, not exposed in the UI.

## OTA updates

Once the glow sequence is complete and home Wi-Fi has associated, the controller polls:

```
https://api.github.com/repos/harryhcs/glowplug-timer/releases/latest
```

If the release tag is newer than the compiled-in `FIRMWARE_VERSION`, the controller downloads the attached `.bin` asset and applies it via `HTTPUpdate`. The dash light blinks rapidly during the flash; the controller reboots into the new image automatically.

OTA is **unconditionally** gated on `glowComplete == true` so it can never interrupt a start sequence — see [`ADR-0003`](./ADRs.md). If the engine is shut off before after-glow finishes, the update simply happens on the next start. The repo must remain public; the controller has no API token.

To ship a new firmware build:

```bash
git tag v1.2.3
gh release create v1.2.3 firmware.bin
```

The next time the vehicle is parked at home and started, it picks up the new image.

## Building and flashing (bootstrap)

USB flashing is only needed once per ESP32 — after that, OTA takes over.

1. Install the [`arduino:esp32`](https://docs.espressif.com/projects/arduino-esp32/) core via the Arduino IDE or `arduino-cli`.
2. Select the **Arduino Nano ESP32** board.
3. Set `FIRMWARE_VERSION` for the build to match the release tag you intend to publish.
4. Compile and upload `main.ino` over USB-C.

Subsequent updates ship via GitHub Releases — no laptop required at the vehicle.

## Out of scope

- No °C calibration of the temperature reading.
- No after-glow tuning from the web UI.
- No authentication on the web UI (SoftAP password is the only gate).
- No fault detection, current sensing, or telemetry.
- No "update now" / "skip update" controls — the controller decides OTA timing.
- No NVS reset route — a corrupted profile requires USB re-flash.
- Only one firmware channel (GitHub `latest`); no stable/beta split.

See [`SPEC.md`](./SPEC.md) for the full acceptance criteria and non-goals.
