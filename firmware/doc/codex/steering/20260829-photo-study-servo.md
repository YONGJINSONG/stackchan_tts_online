# Photo reboot, study freeze, EspNow servo pins

## Photo reboot

Do not force `cam_hal` `psram_mode` at 20 MHz XCLK. Keep I2S free, GC0308
`0x28=0x32`, `cam_stop`/`cam_start`, and an 8-slot EOF queue. See
`20260829-camera-eof-ovf.md`.

## Study freeze

`kids_tutor_process_pending` used to return forever while Realtime
`isSpeaking()` stayed true after a function-only turn. Wait at most 2 s,
then `change_mod`. KidsTutor `init` / `ensureReady` must `Speaker.end` /
`Mic.end` before `beginVoice`. VoiceLite failure still shows the menu.

Serial: `[kids] voice start queued` then `[kids] opening queued study program`.

## Pomodoro

Do not `add_mod(PomodoroMod)` in `main.cpp`. Source stays; the mode cycle
skips it.

## EspNow servos

A/B/C probe must restore the original `pinX`/`pinY` (head is Port A GPIO
1/2). GPIO 2 shares camera XCLK: `ledcDetachPin(2)` before PWM attach.
`setExtOutput(true)` stays. Serial `extOut=` should be 1.

## Verify

`m5stack-cores3-realtime-camera` on COM3. 「사진 찍어줘」 no reboot.
「공부하자」 menu/question, no freeze. Mode cycle has no Pomodoro. EspNow
stick moves the head after probe restore.
