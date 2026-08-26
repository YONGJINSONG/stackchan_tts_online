#pragma once

#include "TTSBase.h"

class GoogleCloudTTS : public TTSBase {
public:
    GoogleCloudTTS(tts_param_t param) : TTSBase(param) {};
    void stream(String text);
};
