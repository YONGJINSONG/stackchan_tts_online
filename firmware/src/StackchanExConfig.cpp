#include "StackchanExConfig.h"
#include "share/SDUtil.h"

StackchanExConfig::StackchanExConfig() {};
StackchanExConfig::~StackchanExConfig() {};

void StackchanExConfig::loadConfig(fs::FS& fs, const char *app_yaml_filename, uint32_t app_yaml_filesize,
                                   const char* secret_yaml_filename, uint32_t secret_yaml_filesize,
                                   const char* basic_yaml_filename, uint32_t basic_yaml_filesize)
{
    // A zero secret size makes the upstream loader skip its unredacted secret
    // dump. Load the same Wi-Fi/API fields plus Agent Bridge below instead.
    StackchanSystemConfig::loadConfig(fs, app_yaml_filename, app_yaml_filesize,
                                     secret_yaml_filename, 0,
                                     basic_yaml_filename, basic_yaml_filesize);
    if (secret_yaml_filesize > 0) {
        loadSecretConfigSafe(fs, secret_yaml_filename, secret_yaml_filesize);
    }
}

void StackchanExConfig::loadSecretConfigSafe(fs::FS& fs, const char* yaml_filename, uint32_t yaml_size)
{
    M5_LOGI("----- StackchanExConfig::loadSecretConfigSafe:%s", yaml_filename);
    File file = fs.open(yaml_filename);
    if (!file) {
        secretConfigNotFoundCallback();
        return;
    }

    DynamicJsonDocument doc(yaml_size);
    DeserializationError err = deserializeYml(doc, file);
    file.close();
    if (err) {
        M5_LOGE("secret yaml read error: %s", err.c_str());
        secretConfigNotFoundCallback();
        return;
    }

    // Preserve the upstream Wi-Fi and AI service key behavior.
    setSecretConfig(doc);

    JsonObject bridge = doc["agentBridge"];
    agent_bridge_s& cfg = _ex_parameters.llm.agentBridge;
    cfg.enabled = bridge["enabled"] | false;
    cfg.host = bridge["host"] | "";
    int port = bridge["port"] | 8765;
    cfg.tls = bridge["tls"] | false;
    cfg.profile = bridge["profile"] | "kids";
    cfg.deviceId = bridge["deviceId"] | "roni";
    cfg.key = bridge["key"] | "";

    cfg.host.trim();
    cfg.profile.trim();
    cfg.deviceId.trim();
    cfg.key.trim();
    if (cfg.deviceId.length() == 0) cfg.deviceId = "roni";

    bool validProfile = cfg.profile == "kids" || cfg.profile == "adult";
    bool validPort = port > 0 && port <= 65535;
    if (!validProfile || !validPort) {
        cfg.enabled = false;
        if (!validProfile) M5_LOGE("agentBridge profile must be kids or adult");
        if (!validPort) M5_LOGE("agentBridge port is invalid");
    }
    cfg.port = validPort ? static_cast<uint16_t>(port) : 8765;

    M5_LOGI("wifi ssid configured: %s", _secret_config.wifi_info.ssid.length() ? "yes" : "no");
    M5_LOGI("AI service keys configured: stt=%s ai=%s tts=%s",
            _secret_config.api_key.stt.length() ? "yes" : "no",
            _secret_config.api_key.ai_service.length() ? "yes" : "no",
            _secret_config.api_key.tts.length() ? "yes" : "no");
    M5_LOGI("agentBridge: enabled=%s host=%s port=%u tls=%s profile=%s deviceId=%s key=%s",
            cfg.enabled ? "true" : "false",
            cfg.host.c_str(),
            static_cast<unsigned>(cfg.port),
            cfg.tls ? "true" : "false",
            cfg.profile.c_str(),
            cfg.deviceId.c_str(),
            cfg.key.length() ? "configured" : "missing");
}


void StackchanExConfig::basicConfigNotFoundCallback(void)
{
    char buf[128], data[128];
    char *endp;
    String SV_ON_OFF = "";

    Serial.printf("Cannot open YAML basic config file. Try to read legacy text file.\n");

    /// Servo
    if(read_sd_file("/servo.txt", buf, sizeof(buf))){
        read_line_from_buf(buf, data);
        SV_ON_OFF = String(data);
        if (SV_ON_OFF == "on" || SV_ON_OFF == "ON"){
            USE_SERVO_ST = true;
        }
        else{
            USE_SERVO_ST = false;
        }
        Serial.printf("Servo ON or OFF: %s\n",data);

        read_line_from_buf(nullptr, data);
        _servo[AXIS_X].pin = strtol(data, &endp, 10);
        Serial.printf("Servo pin X: %d\n", _servo[AXIS_X].pin);

        read_line_from_buf(nullptr, data);
        _servo[AXIS_Y].pin = strtol(data, &endp, 10);
        Serial.printf("Servo pin Y: %d\n", _servo[AXIS_Y].pin);
    }
    else{
        Serial.printf("Cannot open legacy text file. Set default value.\n");
        _servo[AXIS_X].pin = DEFAULT_SERVO_PIN_X;
        _servo[AXIS_Y].pin = DEFAULT_SERVO_PIN_Y;
        Serial.printf("Servo pin X: %d\n", _servo[AXIS_X].pin);
        Serial.printf("Servo pin Y: %d\n", _servo[AXIS_Y].pin);
    }
}

void StackchanExConfig::secretConfigNotFoundCallback(void)
{
    char buf[128], data[128];

    Serial.printf("Cannot open YAML secret config file. Try to read legacy text file.\n");

    /// wifi
    if(read_sd_file("/wifi.txt", buf, sizeof(buf))){
        read_line_from_buf(buf, data);
        _secret_config.wifi_info.ssid = String(data);
        Serial.printf("SSID: %s\n",data);

        read_line_from_buf(nullptr, data);
        _secret_config.wifi_info.password = String(data);
        Serial.printf("WiFi key configured: %s\n", data[0] ? "yes" : "no");
    }

    /// apikey
    if(read_sd_file("/apikey.txt", buf, sizeof(buf))){
        read_line_from_buf(buf, data);
        _secret_config.api_key.ai_service = String(data);
        Serial.printf("openai configured: %s\n", data[0] ? "yes" : "no");

        read_line_from_buf(nullptr, data);
        _secret_config.api_key.tts = String(data);
        Serial.printf("tts configured: %s\n", data[0] ? "yes" : "no");

        read_line_from_buf(nullptr, data);
        _secret_config.api_key.stt = String(data);
        Serial.printf("stt configured: %s\n", data[0] ? "yes" : "no");
    }
}

void StackchanExConfig::loadExtendConfig(fs::FS& fs, const char* yaml_filename, uint32_t yaml_size)
{
    M5_LOGI("----- StackchanExConfig::loadExtendConfig:%s\n", yaml_filename);
    File file = fs.open(yaml_filename);
    if (file) {
        DynamicJsonDocument doc(yaml_size);
        auto err = deserializeYml( doc, file);
        if (err) {
            M5_LOGE("yaml file read error: %s\n", yaml_filename);
            M5_LOGE("error%s\n", err.c_str());
            extendConfigNotFoundCallback();
        }
        else{
            setExtendSettings(doc);
        }

        serializeJsonPretty(doc, Serial);
        M5_LOGI("");
        printExtParameters();

    }
    else{
        //YAMLファイルオープン失敗の場合は、SDのディレクトリ直下に旧TXTファイルがあれば読み込む
        M5_LOGE("yaml file open error: %s\n", yaml_filename);
        extendConfigNotFoundCallback();
        printExtParameters();
    }
}

void StackchanExConfig::extendConfigNotFoundCallback(void)
{
    M5_LOGE("load extend config from txt files\n");

}

void StackchanExConfig::setExtendSettings(DynamicJsonDocument doc)
{
    _ex_parameters.llm.type         = doc["llm"]["type"].as<int>();
    _ex_parameters.llm.model        = doc["llm"]["model"].as<String>();
    _ex_parameters.llm.nMcpServers  = doc["llm"]["mcpServers"].size();
    for(int i=0; i<_ex_parameters.llm.nMcpServers; i++){
        _ex_parameters.llm.mcpServer[i].name = doc["llm"]["mcpServers"][i]["name"].as<String>();
        _ex_parameters.llm.mcpServer[i].disabled = doc["llm"]["mcpServers"][i]["disabled"].as<bool>();
        _ex_parameters.llm.mcpServer[i].url = doc["llm"]["mcpServers"][i]["url"].as<String>();
        _ex_parameters.llm.mcpServer[i].port = doc["llm"]["mcpServers"][i]["port"].as<int>();
    }
    _ex_parameters.llm.enableMemory = doc["llm"]["enableMemory"].as<bool>();

    _ex_parameters.tts.type         = doc["tts"]["type"].as<int>();
    _ex_parameters.tts.model        = doc["tts"]["model"].as<String>();
    _ex_parameters.tts.voice        = doc["tts"]["voice"].as<String>();

    _ex_parameters.stt.type         = doc["stt"]["type"].as<int>();
    _ex_parameters.stt.model        = doc["stt"]["model"].as<String>();

    _ex_parameters.wakeword.type    = doc["wakeword"]["type"].as<int>();
    _ex_parameters.wakeword.keyword = doc["wakeword"]["keyword"].as<String>();

    _ex_parameters.audio.speaker_volume = doc["audio"]["speaker_volume"].as<int>();

    _ex_parameters.moduleLLM.rxPin  = doc["moduleLLM"]["rxPin"].as<int>();
    _ex_parameters.moduleLLM.txPin  = doc["moduleLLM"]["txPin"].as<int>();

}

void StackchanExConfig::printExtParameters(void)
{
    M5_LOGI("llm type: %d", _ex_parameters.llm.type);
    M5_LOGI("llm model: %s", _ex_parameters.llm.model.c_str());
    M5_LOGI("llm nMcpServers: %d", _ex_parameters.llm.nMcpServers);
    for(int i=0; i<_ex_parameters.llm.nMcpServers; i++){
        M5_LOGI("llm mcpServer[%d] name: %s", i, _ex_parameters.llm.mcpServer[i].name.c_str());
        M5_LOGI("llm mcpServer[%d] disabled: %s", i, _ex_parameters.llm.mcpServer[i].disabled ? "true":"false");
        M5_LOGI("llm mcpServer[%d] url: %s", i, _ex_parameters.llm.mcpServer[i].url.c_str());
        M5_LOGI("llm mcpServer[%d] port: %d", i, _ex_parameters.llm.mcpServer[i].port);
    }
    M5_LOGI("llm enableMemory: %s", _ex_parameters.llm.enableMemory ? "true":"false");
    M5_LOGI("llm agentBridge enabled: %s", _ex_parameters.llm.agentBridge.enabled ? "true":"false");


    M5_LOGI("tts type: %d", _ex_parameters.tts.type);
    M5_LOGI("tts model: %s", _ex_parameters.tts.model.c_str());
    M5_LOGI("tts voice: %s", _ex_parameters.tts.voice.c_str());

    M5_LOGI("stt type: %d", _ex_parameters.stt.type);
    M5_LOGI("stt model: %s", _ex_parameters.stt.model.c_str());

    M5_LOGI("wakeword type: %d", _ex_parameters.wakeword.type);
    M5_LOGI("wakeword keyword: %s", _ex_parameters.wakeword.keyword.c_str());

    M5_LOGI("audio speaker volume: %d", _ex_parameters.audio.speaker_volume);

    M5_LOGI("module llm rxPin: %d", _ex_parameters.moduleLLM.rxPin);
    M5_LOGI("module llm txPin: %d", _ex_parameters.moduleLLM.txPin);
    
}
