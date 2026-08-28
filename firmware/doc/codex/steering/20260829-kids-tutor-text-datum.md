# KidsTutor text clipped by avatar text datum

## Cause

Avatar `Balloon` sets `M5.Lcd.setTextDatum(MC_DATUM)`. KidsTutor draws status and questions with top-left coordinates and never restored the datum, so glyphs were centered on (8, 5) / (8, 32) and clipped at the top and left.

## Fix

Call `setTextDatum(top_left)`, `efontKR_16`, and `setTextSize(1)` at the start of every KidsTutor screen draw.

## Verify

- Enter KidsTutor after a Realtime utterance (balloon on screen).
- Status line and question text should start fully visible at the left, not cut at the top edge.
