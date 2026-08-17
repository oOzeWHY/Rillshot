#include "core/Json.h"

namespace rillshot::core {

std::string escapeJsonString(const std::string& input) {
    static constexpr char hexDigits[] = "0123456789abcdef";

    std::string output;
    output.reserve(input.size());
    for (const char rawCh : input) {
        const auto ch = static_cast<unsigned char>(rawCh);
        switch (ch) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (ch < 0x20U) {
                output += "\\u00";
                output.push_back(hexDigits[(ch >> 4U) & 0x0FU]);
                output.push_back(hexDigits[ch & 0x0FU]);
            } else {
                output.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return output;
}

} // namespace rillshot::core
