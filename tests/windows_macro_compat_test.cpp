#include <cstdlib>
#include <iostream>
#include <limits>

// 模拟标准库头文件载入后，第三方头文件重新暴露 Windows 风格 max 宏的场景。
// WinUI 工程同时使用 NOMINMAX 从源头阻止 Windows SDK 定义该宏。
#define max(a, b) windows_max_macro_must_not_expand
#include "core/Types.h"
#undef max

int main() {
    const rillshot::core::RectI valid{0, 0, 100, 100};
    if (!valid.isValid()) {
        std::cerr << "a normal rectangle should remain valid under a Windows max macro\n";
        return EXIT_FAILURE;
    }

    const rillshot::core::RectI overflowing{
        (std::numeric_limits<int>::max)() - 10,
        0,
        20,
        100};
    if (overflowing.isValid()) {
        std::cerr << "an overflowing rectangle should remain invalid\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
