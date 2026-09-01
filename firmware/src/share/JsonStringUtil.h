#pragma once

#include <Arduino.h>

inline String json_escape_string_content(const String& input) {
    static const char hex[] = "0123456789ABCDEF";
    String output;
    output.reserve(input.length() + 16);
    for (size_t i = 0; i < input.length(); ++i) {
        uint8_t c = static_cast<uint8_t>(input[i]);
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (c < 0x20) {
                output += "\\u00";
                output += hex[(c >> 4) & 0x0F];
                output += hex[c & 0x0F];
            } else {
                output += static_cast<char>(c);
            }
            break;
        }
    }
    return output;
}
