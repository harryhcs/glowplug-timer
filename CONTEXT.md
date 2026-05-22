# Glossary

## Glow Plug Controller

The device this project builds: an ESP32 that reads engine temperature, drives the glow plug relay through a pre-glow + after-glow sequence, and exposes a configuration web UI over a SoftAP. _Avoid:_ "glow plug timer" (undersells what it does — it branches on a sensor and watches the alternator, not just a timer).

## Temperature reading

The value sampled from the analog temperature sensor on pin `A0`, expressed in raw ADC counts (0–1023). Used to pick which [[pre-glow-band]] applies on boot. _Avoid:_ "A0 reading" (implementation leak), "temp" (too casual), "coolant temp in °C" (we never convert — there is no calibration).

## Pre-glow profile

The full user-configurable table of 7 [[pre-glow-band]] entries that maps temperature ranges to pre-glow durations. Persisted in NVS. _Avoid:_ "settings table", "timer config".

## Pre-glow band

One row in the [[pre-glow-profile]]: an ADC threshold (e.g. `> 800`) paired with a pre-glow duration in seconds. The controller picks the highest-threshold band the current [[temperature-reading]] satisfies. _Avoid:_ "target" (too generic), "bucket" (engineering jargon, not domain), "timer setting" (collides with after-glow).

## Pre-glow phase

The interval from power-on until the selected [[pre-glow-band]] duration elapses. During this phase the glow relay is energised and the dash light is lit to tell the driver to wait.

## After-glow phase

The fixed 15-second interval that starts when the alternator pin goes high (engine running) and ends when the glow relay is de-energised. Runs only after the [[pre-glow-phase]] has completed.

## Dash light

The driver-facing indicator (on pin `D3`) that signals "wait, glow plugs heating". Goes off at the end of the [[pre-glow-phase]] to tell the driver they can crank.

## Glow relay

The output (on pin `D9`) that energises the glow plugs. Held active from power-on until the end of the [[after-glow-phase]].
