# CoreS3 Realtime audio, touch, reactions, and camera recovery

## Goal

Restore the user-facing Camera Realtime build while reducing robot-output gaps,
making touch usable during connection startup, and making pet/shake feedback
reliable without weakening HTTPS certificate validation or the PSRAM TLS
allocator policy.

## Decisions

- Deploy only `m5stack-cores3-realtime-camera` to the user's CoreS3. Keep the
  non-camera Realtime environment for build diagnostics.
- Feed Realtime 24 kHz PCM into fixed speaker channel 0 with its two queue
  slots. Alternate the two decode buffers only after a slot is free and drain
  them at response completion.
- Keep input VAD threshold 0.35, 700 ms silence, and response interruption off.
- Schedule NTP without waiting in `setup()`. Play the boot SFX in a background
  task through the existing audio mutex and preserve pre-session touch requests.
- Log PMIC type, battery level/voltage/current, VBUS voltage, and charge state at
  boot. An off-state power key cannot be repaired by application code that has
  not started; USB-only startup is diagnosed first as a depleted/disconnected
  battery or PMIC/power-key path fault.
- Add backward-compatible `shakeSensitivity` (default 6). A shake requires two
  dominant-axis sign reversals inside 800 ms, with a below-45-percent release
  between peaks; speaking, gestures, and camera work reset the classifier.
- Prefer `eye_puzzled` over blink during the 2.5-second shake reaction and log a
  surprised-eye fallback if the asset is absent.
- Use `/face/blush/blush_pet.png` for the three-second touch/pet reaction and
  fall back to `blush_shy` on older SD cards.
- Retain CA-verified HTTPS and the hybrid CoreS3 mbedTLS allocator unchanged.
- OpenAI Realtime and Funnel HTTPS share the official ISRG Root X1/X2 bundle.
  This follows the current `api.openai.com` Let's Encrypt chain while keeping
  hostname/CA verification enabled; insecure TLS remains prohibited.

## Validation

- Run the host shake-classifier test.
- Build both Realtime CoreS3 environments.
- Flash only the Camera environment to COM3 and place `blush_pet.png` plus
  `eye_puzzled.png` on the SD card.
- Hardware validation covers five long answers, pre-session touch, pet stroke,
  deliberate shake, and the complete photo countdown/preview/save-discard flow.
