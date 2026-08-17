#include "input/ManualDriver.h"

#include <iostream>
#include <string>

namespace rillshot::input {

namespace detail {

rillshot::core::Status awaitManualAdvance(
    std::wistream& input,
    std::wostream& output) {
    output << L"Scroll the target manually, then press Enter. Type q and Enter to stop: "
           << std::flush;
    std::wstring line;
    if (!std::getline(input, line)) {
        return rillshot::core::Status::failure(
            "manual-input-closed",
            "manual input stream closed before confirmation");
    }
    if (line == L"q" || line == L"Q") {
        return rillshot::core::Status::failure("manual-user-stopped", "user stopped manual capture");
    }
    return rillshot::core::Status::success();
}

} // namespace detail

rillshot::core::Status ManualDriver::advance(const ScrollRequest&) {
    return detail::awaitManualAdvance(std::wcin, std::wcout);
}

} // namespace rillshot::input
