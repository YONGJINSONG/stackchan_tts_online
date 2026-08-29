# Session-win and shutter WAV clips

Convert repo-root `stagewin.mp3` / `shutterclick.mp3` to 16 kHz mono PCM16
WAV and embed them (`src/assets/*.wav` + `wav_blobs.S`). Playback copies
samples into PSRAM and uses `M5.Speaker.playRaw`.

## KidsTutor

After the last question, `finishSessionIfNeeded` speaks the daily
completion line (잘했어) then `wav_clip_play_stagewin`. Do not restore the
mic; tutoring is button-only.

## Photo countdown

On each 3 / 2 / 1 tick in `camera_action_process_pending`, play
`shutterclick` with the mic left stopped so camera I2S/DMA can init after
the last click.

## Verify

- 「공부하자」 10문제 후 잘했어 다음 승리 징글(~8 s).
- 「사진 찍어줘」 3·2·1마다 셔터 클릭 후 촬영. 재부팅 없음.
