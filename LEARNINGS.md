# Learnings

Things this project taught us the hard way. Each entry is a thing that cost real time or shipped a real bug, plus how to avoid it next time.

## ESP32 `HTTPUpdate` does not follow redirects by default

GitHub release downloads (`https://github.com/<owner>/<repo>/releases/download/<tag>/<asset>`) always 302-redirect to `objects.githubusercontent.com/...`. The ESP32 Arduino core's `HTTPUpdate` library defaults to `HTTPC_DISABLE_FOLLOW_REDIRECTS`, so the 302 response body is consumed as the firmware image — which is empty/invalid, and `HTTPUpdate` reports a generic flash failure with no useful detail.

This was the root cause of every OTA failure between v1.0.0 and v1.0.2. Fixed in v1.0.3 with:

```cpp
httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
```

Any time HTTPUpdate is pointed at a URL that 302s — GitHub, S3 signed URLs, CDN-fronted hosts — this is required.

## Look up the asset URL from the API, don't construct it

v1.0.0/v1.0.1 hardcoded the OTA download URL as `.../releases/download/v<tag>/firmware.bin`, but `arduino-cli compile` emits `<sketch>.ino.bin` (so for us, `glowplug-timer.ino.bin`). The hardcoded filename 404'd every time.

Better pattern: parse the `/releases/latest` JSON response, find the first asset whose name ends in `.bin`, and use its `browser_download_url`. Bonus: tolerates whatever name the build system picks. See `ota.cpp:otaFetchLatest()`.

## NVS survives both firmware updates and USB re-flashes

NVS lives in a separate flash partition (`nvs` in the default ESP32 partition table). The sketch upload only overwrites the app partition; NVS keys are untouched. `Preferences::getInt("key", default)` returns the stored value, not the default, unless the key has never been written.

Consequences:
- After upgrading firmware that changed defaults (e.g. our `t8` default went from 12 → 5), existing devices still report the old saved value because the key was already written under the old firmware.
- "Reset to factory" via USB re-flash doesn't work. Need `esptool.py erase_flash` or a targeted NVS partition erase.

This is by design (acceptance #5: "profile survives power-off"). Just be aware when you see a value that doesn't match the documented default.

## Browser caches `/` aggressively

`handleRoot()` doesn't set cache-control headers. iOS Safari and Chrome will both happily serve stale HTML after a firmware upgrade — so the *new* firmware is running but the UI looks like the old one. Hard refresh or a private/incognito window cuts through it. `GET /status` is JSON and is a reliable way to confirm what version is actually running.

If this becomes a recurring pain, add `server.sendHeader("Cache-Control", "no-store")` before `server.send` in `handleRoot`.

## Arduino Nano ESP32 FQBN is `arduino:esp32:nano_nora`

Not `nano_esp32`, not `esp32s3`, not `nano-esp32`. The `nora` refers to the NORA-W106 module on the board. Easy to get wrong because the board name is "Nano ESP32".

## OTA gated on `glowComplete` makes bench testing fiddly

OTA can't run until the full pre-glow + after-glow sequence completes, which requires `ALT_PIN` (D2) to be pulled HIGH (alternator-running signal). On a bench with no engine you have to jumper D2 to 3.3 V *after* pre-glow ends to trip the gate.

This is the main reason v1.0.3 added the manual `POST /ota` button + "CHECK FOR UPDATE NOW" UI — debugging the OTA path one full glow sequence at a time was a 30+ second loop per attempt. The button trades off the SPEC's original "no manual OTA controls" non-goal for a usable debug loop (now documented in SPEC.md).

## Verbose `HTTPUpdate` errors are available — surface them

`httpUpdate.getLastError()` returns an int code (`HTTP_UPDATE_FAILED` etc.) and `httpUpdate.getLastErrorString()` returns a human-readable string. The first three OTA-fix attempts (v1.0.0 → v1.0.2) ran in a loop of "what failed? unknown. try again. unknown. try again." because we were only reporting "check failed: flash failed".

Always surface these via whatever debug channel you have (web UI, Serial, status JSON). Saved us hours on the fourth attempt.

## `git add -A` is dangerous in repos without a `.gitignore`

A commit during this session swept in `build/` (arduino-cli output: bootloader, partition table, ELF with symbols, .bin) plus untracked screenshot + spec files because no `.gitignore` existed yet. Reset and recommitted with explicit files.

Lesson: either add `.gitignore` before the first commit that produces build artifacts, or always stage explicit paths (`git add file1 file2`) — never `-A` until the ignore rules are established. We now have one ignoring `build/` and `*.bin`.

## When fixing an OTA bug, bridge the asset name on the new release

If the buggy firmware in the field looks for `X.bin` and the fixed firmware will look for `Y.bin`, the *first* release after the fix needs to ship both names. Otherwise field devices running the buggy firmware can't pull the fix because they look for an asset that doesn't exist.

We did this in v1.0.2 (shipped both `firmware.bin` for v1.0.1 compatibility and `glowplug-timer.ino.bin` as the natural name). From v1.0.3 onward, only the natural name is needed.

Generalize: any change to the OTA download path needs a release that's reachable from both the old and new firmware's perspective.
