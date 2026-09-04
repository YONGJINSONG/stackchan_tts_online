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

    // Seed the SD-only pet reaction asset on devices that were flashed before
    // blush_pet.png was added to Copy-to-SD.
    .align 4
    .global blush_pet_png_start
    .global blush_pet_png_end
blush_pet_png_start:
    .incbin "../Copy-to-SD/face/blush/blush_pet.png"
blush_pet_png_end:
