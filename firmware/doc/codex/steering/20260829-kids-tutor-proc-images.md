# KidsTutor procedural math images (no SD PNG)

## Why

CoreS3 shares SD MISO with the LCD. `drawPngFile` during quiz UI could reset / break touch, so CoreS3 previously forced `hasImage = false`.

## Approach

- Analyze `/kids_tutor/images/*.png` (119 files, math_6yo only) into compact specs.
- Draw with LCD primitives (circle / square / triangle / bars / color pattern + `?`).
- Layout: question text → centered figure → choices.

## Files

- `firmware/scripts/gen_tutor_image_specs.py` — regenerate `TutorImageSpecs.inc`
- `firmware/src/kids_tutor/TutorImageDraw.{h,cpp}`
- `firmware/src/kids_tutor/TutorImageSpecs.inc`
- `firmware/src/kids_tutor/StackchanUI.cpp` — uses procedural draw on all boards when spec exists

## Verify

- Enter KidsTutor math 6yo pattern / count / shape / compare questions.
- Figure appears between question and choices; colors match palette (R/G/B/P/O/Y).
- No SD read for illustrations; touch footer still works.
