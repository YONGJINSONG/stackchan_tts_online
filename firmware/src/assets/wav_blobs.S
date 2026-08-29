    .section .rodata
    .align 4
    .global stagewin_wav_start
    .global stagewin_wav_end
stagewin_wav_start:
    .incbin "src/assets/stagewin.wav"
stagewin_wav_end:

    .align 4
    .global shutterclick_wav_start
    .global shutterclick_wav_end
shutterclick_wav_start:
    .incbin "src/assets/shutterclick.wav"
shutterclick_wav_end:
