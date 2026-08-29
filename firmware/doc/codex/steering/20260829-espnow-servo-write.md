# EspNow servo: immediate write, GPIO2 reset

YAML still prints Core2 pins 32/33. Robot remaps them to Port A GPIO 1/2.
GPIO2 is also camera XCLK. After a photo, leftover LEDC made attach look
OK while the motor got no PWM.

## Fix

- `dumpAndReattach` resets both servo GPIOs (`ledcDetachPin` + `gpio_reset_pin`)
  before attach. Treat only `INVALID_SERVO` as attach failure (channel 0 is OK).
- EspNow stick uses `writeOffset` (no easing). Re-enable ExtOutput on the
  first packet. After A/B/C probe, skip restoring yaml 32/33 — keep 1/2.

## Verify

`[EspNowRemote] servo x=.. y=.. pins=1/2`. Head moves. `extOut=1`.
