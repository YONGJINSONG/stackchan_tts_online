#ifndef _WAV_CLIP_H
#define _WAV_CLIP_H

// Embedded PCM16 WAV clips (converted from repo-root mp3). Playback copies
// samples into PSRAM then uses M5.Speaker.playRaw so flash is never DMA'd.

bool wav_clip_play_stagewin(bool restoreMic);
bool wav_clip_play_shutter(bool restoreMic);

#endif
