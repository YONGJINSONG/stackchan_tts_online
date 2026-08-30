#pragma once

#include <Arduino.h>

bool music_action_request_play(const char* path, const char* displayName);
void music_action_process_pending();
bool music_action_take_result(String& result);
void music_action_abandon();
void music_action_stop();
bool music_action_is_playing();
