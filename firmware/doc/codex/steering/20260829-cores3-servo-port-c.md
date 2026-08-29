# CoreS3 camera servo on Port C

GPIO2 is GC0308 XCLK. Yaml still has Core2 Port A 32/33, which we used to
remap to Port A 1/2. PWM then went to the camera pin and the head (Port C
18/17) never moved. EspNow logs showed `pins=1/2` with no motion.

## Fix

On `ENABLE_CAMERA` CoreS3, default and yaml remap to GPIO **18/17**. EspNow
probes Port C only (do not PWM GPIO2). After attach, set the pin to output
so `gpio_reset_pin` does not leave LEDC disconnected.

## Verify

Boot: `[servo] begin PWM on pinX=18 pinY=17`. EspNow: `pins=18/17` and a
visible Port C hardSweep. Stick moves the head. `extOut=1`.
