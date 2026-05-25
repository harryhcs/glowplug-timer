# Architecture Decisions

## ADR-0001: Diverge from the factory 1HZ after-glow (T2) curve

**Context.** The Toyota 1HZ workshop manual specifies an after-glow duration (T2) of 120 s below 20 °C coolant temperature, tapering to ~45 s around 40 °C and ~0 above 70 °C. Following T2 literally would require running the glow relay for up to two minutes after every cold start.

**Decision.** Ignore T2. After-glow runs only when boot-time [[temperature-reading]] ≥ 700 ADC ("really cold"), and is capped at 3 seconds when it does run. The T1 (pre-glow) curve from the same manual *is* honored — it informs the default [[pre-glow-profile]] of 5/4/3/2/1/1/1 seconds.

**Consequences.** Glow plugs see dramatically less total energised time per start than the factory specifies, which is the explicit goal — Herman's prior experience with this engine is that the factory-length after-glow shortens plug life. Trade-off: cold-start emissions and combustion smoothness may be marginally worse for the first few seconds after start versus a 120 s after-glow vehicle. In the South African climate where this vehicle lives, very cold starts (≥ 700 ADC) are the minority of starts anyway.

**Alternatives considered.** (a) Follow T2 literally — rejected, harms plug life per Herman's experience. (b) Make after-glow duration user-tunable per band like pre-glow — rejected to keep the "less after-glow, not more" intent load-bearing; tunability invites future-Herman to dial it back up. (c) Skip after-glow entirely in all conditions — rejected, the ≥ 700 ADC cold case benefits from a brief 3 s dab.

**Revision (2026-05-25, v1.1.1).** Threshold lowered 700 → 600 ADC and duration extended 3 s → 5 s after observing rough idle on cold morning starts that weren't crossing the 700 ADC bar. Intent unchanged — still far below the factory 120 s and still cold-only — just a wider "cold enough" window and a longer dab to settle combustion. If plug life turns out to suffer, the next revision walks one or both numbers back.

## ADR-0002: Simultaneous AP+STA networking, not STA-only or AP-only

**Context.** The controller needs two networking behaviors at once: (a) always-available in-vehicle access to the web UI for tuning the [[pre-glow-profile]] regardless of where the vehicle is parked, and (b) opportunistic internet access for the boot-time GitHub OTA check while parked at home.

**Decision.** Run ESP32 in `WIFI_AP_STA` mode. The `GlowPlugController` SoftAP is always on with the same password regardless of STA state. The STA side attempts to associate with the saved home Wi-Fi whenever `wifi_ssid` is non-empty in NVS; failure is silent and never blocks anything else.

**Consequences.** Slightly higher RF and power use than a single-mode device. The Tuner can always reach the web UI from any location. STA failures (out of range, wrong password, ISP down) degrade gracefully to "no OTA this boot" with no driver-visible effect.

**Alternatives considered.** (a) STA-only with the AP only spun up when STA fails — rejected because tuning at a remote location (e.g. on a trip) would require manually forcing AP mode. (b) AP-only with manual `.bin` upload via web UI — rejected, that's the "no auto-update" world the user explicitly wanted to leave. (c) STA-first with a button to toggle AP — rejected, no physical button on the Nano ESP32 wiring as installed, and adding one means a hardware change.

## ADR-0003: OTA strictly gated on `glowComplete == true`

**Context.** OTA download + flash on the Nano ESP32 takes 20–60 s, blocks the main loop while the new image is written, and ends in a forced reboot. If any of that overlaps the [[pre-glow-phase]] or [[after-glow-phase]], the driver gets unpredictable dash-light timing or — worse — a vehicle that won't start because the controller rebooted into a half-flashed image mid-sequence.

**Decision.** The OTA check (GitHub API call, version compare, download, flash) is *unconditionally* gated on `glowComplete == true`. Until that flag flips, the controller may associate with home Wi-Fi but does not initiate the GitHub call, does not download a `.bin`, and does not apply any update. STA association itself is non-blocking so it doesn't delay the start of pre-glow.

**Consequences.** Glow-plug operation is provably never delayed or interrupted by OTA. Trade-off: if the driver shuts the engine off before after-glow finishes (`glowComplete` never flips this boot), no update happens this trip — that's fine, it'll happen next boot. Also implies the boot-time OTA window is actually "end-of-glow-time OTA window" — the name "boot-time" in casual conversation hides this nuance.

**Alternatives considered.** (a) Run OTA opportunistically on its own task/thread in parallel with glow — rejected, ESP32 OTA is intrusive enough (flash writes, watchdog, pre-reboot teardown) that "in parallel" doesn't really exist safely. (b) Defer OTA to a separate "maintenance mode" entered by a long press of some signal — rejected, no physical input and complicates the user model. (c) Allow OTA after pre-glow but before after-glow — rejected, breaks the cold-start case where after-glow still has the relay energised.

## ADR-0004: GitHub Releases as the sole OTA source

**Context.** "Auto-update from a remote source" requires picking *where* the controller fetches new firmware from. The realistic options for a hobbyist single-vehicle project were a self-hosted update server, an MQTT-triggered push, a managed IoT cloud (ESP RainMaker etc.), or polling a public GitHub repo's releases API.

**Decision.** On boot (after `glowComplete`), the controller calls `https://api.github.com/repos/harryhcs/glowplug-timer/releases/latest`, parses the tag, and if newer than the compiled-in `FIRMWARE_VERSION`, downloads the attached `.bin` and applies it via `HTTPUpdate`. No auth, no API token, no separate update channel.

**Consequences.** Zero infrastructure to maintain — the same `git tag && gh release create` workflow that publishes the source also ships new firmware to the truck. ESP32's dual OTA partitions handle "new image doesn't boot" automatically. Trade-off: the repo must remain public (or the controller has to ship an API token, which we don't want); GitHub's unauthenticated API has a 60 req/hour rate limit per IP (fine — one boot per drive cycle is well under that); and the controller's TLS root store has to trust GitHub's certificate chain.

**Alternatives considered.** (a) Self-hosted update server — rejected, adds infrastructure to run and a domain to maintain. (b) MQTT broker pushing a "new version available" message — rejected, requires a broker and is overkill for one device. (c) ArduinoOTA over LAN — rejected, that's manual push from a laptop, not auto-update. (d) ESP RainMaker / managed cloud — rejected, vendor lock-in for a hobby project.
