# Remove unused X6H / NimBLE after print UI was dropped

## Purpose

Photo save-only and KidsTutor no longer call the printer. Delete the dead driver and NimBLE deps. Do not touch KidsTutor exit, night brightness, or text datum.

## Changes

- Delete `src/driver/X6hPrinter.cpp` and `.h`.
- Drop `h2zero/NimBLE-Arduino@1.4.2` from camera PlatformIO envs.
- Mark KidsTutor receipt steering as removed.
- Keep `take_photo` `print` mode as a spoken error so the tool enum still answers.

## Verify

- `m5stack-cores3-realtime-camera` builds without X6h/NimBLE.
