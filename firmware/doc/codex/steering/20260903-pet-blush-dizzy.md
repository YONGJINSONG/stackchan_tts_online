# Pet Blush and Dizzy Reaction

## Goal

Show the existing shy-blush overlay whenever a touch or IMU pet reaction is
active, and show a short dizzy expression only for a deliberate strong shake.

## Design

- Keep all IMU reads in `pet_reaction_tick()` on the main loop.  The CoreS3
  internal I2C bus is therefore still serialized with `M5.update()`, camera,
  touch, and the other sensor ticks; no task is added.
- A pet/lift retains its existing sensitivity-dependent low-pass threshold.
  A shake requires raw gyro magnitude >= 500 deg/s for three consecutive
  40-ms samples and low-pass energy >= 300 deg/s.  This prevents ordinary
  pickup and gentle petting from producing the dizzy face.
- A shake has priority: it clears an active blush, sets `Expression::Doubt`
  for 2.5 seconds, suppresses pet re-entry, and has a 3-second cooldown.
  It restores the previous expression only when the reaction still owns the
  displayed expression.
- LayeredFace reuses the already loaded cropped `blush_shy` and
  `eye_puzzled` overlays.  `eye_puzzled` is selected only while the physical
  dizzy timer is active; ordinary AI or idle `Expression::Doubt` keeps the
  existing surprised eye.  Missing SD art follows the existing blush ellipse
  and surprised-eye fallbacks.  No new image allocation or persistent buffer
  is introduced.

## Verification

- Build `m5stack-cores3` and `m5stack-cores3-realtime-camera`.
- Confirm a touch stroke displays shy blush for about 2.5 seconds.
- Confirm a deliberate shake logs `[pet] dizzy`, shows puzzled/Doubt for about
  2.5 seconds, and does not also trigger the happy pet reaction.
- Confirm speaking, servo gestures, and camera capture remain excluded from
  IMU detection and that a newer AI expression is not overwritten on expiry.
