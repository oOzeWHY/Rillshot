#pragma once

#include <cstdlib>
#include <cstdint>
#include <iostream>

inline int fail(const char* message) {
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

inline int expectNear(
    int actual,
    int expected,
    int tolerance,
    const char* label) {
    const auto difference = static_cast<std::int64_t>(actual) -
        static_cast<std::int64_t>(expected);
    const auto absoluteDifference = difference < 0 ? -difference : difference;
    if (tolerance < 0 ||
        absoluteDifference > static_cast<std::int64_t>(tolerance)) {
        std::cerr << label << '=' << actual << " expected=" << expected
                  << " tolerance=" << tolerance << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
