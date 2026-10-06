#include "StitchTestFixtures.h"
#include "TestSupport.h"
#include "session/SessionCaptureSupport.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

using namespace rillshot::session;
using rillshot::capture::CaptureFrame;
using rillshot::capture::ICaptureBackend;
using rillshot::core::Status;
using Clock = std::chrono::steady_clock;

class SequenceBackend final : public ICaptureBackend {
public:
    int calls = 0;
    int changeOnCall = 0;
    int slowOnCall = 0;
    bool* cancelOnThird = nullptr;
    std::vector<Clock::time_point> starts;
    std::vector<Clock::time_point> finishes;
    std::string name() const override { return "Sequence"; }
    Status capture(const rillshot::core::RectI&, CaptureFrame& frame) override {
        starts.push_back(Clock::now());
        ++calls;
        if (calls == slowOnCall) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
        frame.image = makeDocument(64, 48);
        if (changeOnCall != 0 && calls >= changeOnCall) {
            for (auto& byte : frame.image.bytes()) byte ^= 127;
        }
        if (cancelOnThird && calls == 3) *cancelOnThird = true;
        finishes.push_back(Clock::now());
        return Status::success();
    }
};

static int runCase(int scenario) {
    std::vector<std::unique_ptr<ICaptureBackend>> backends;
    auto backend = std::make_unique<SequenceBackend>();
    auto* sequence = backend.get();
    bool cancelled = false;
    CaptureSessionOptions options;
    options.region = {0, 0, 64, 48};
    options.minWaitMs = 0;
    options.maxWaitMs = 2000;
    options.sampleIntervalMs = 10;
    options.stableSamplesRequired = 2;
    options.shouldStop = [&] { return cancelled; };
    if (scenario == 1) sequence->changeOnCall = 3;
    if (scenario == 2) {
        options.maxWaitMs = 40;
        options.sampleIntervalMs = 100;
    }
    if (scenario == 3) sequence->cancelOnThird = &cancelled;
    if (scenario == 4) {
        options.maxWaitMs = 40;
        sequence->slowOnCall = 2;
    }
    backends.push_back(std::move(backend));
    const auto outputPath = std::filesystem::temp_directory_path() /
        (L"rillshot-stabilization-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(scenario) + L".png");
    CaptureFrame frame;
    Status status;
    {
        detail::JsonlLogger logger(outputPath.wstring(), false);
        if (!logger.ok()) return fail("could not create stabilization test log");
        status = detail::waitForStableFrame(backends, options, frame, logger, "test");
    }
    std::filesystem::remove(outputPath.wstring() + L".jsonl");
    if (scenario == 3) {
        if (status.ok || status.code != "session-cancelled")
            return fail("cancellation during final capture must win over stability");
    } else if (!status.ok) {
        return fail("sequence capture failed");
    } else if (scenario == 2 || scenario == 4) {
        if (!frame.unstable || sequence->calls != (scenario == 2 ? 1 : 2))
            return fail("timeout started an extra capture or accepted a late frame");
    } else if (frame.unstable || sequence->calls != (scenario == 1 ? 5 : 3)) {
        return fail("stability must require two consecutive unchanged comparisons");
    }
    for (std::size_t index = 1; index < sequence->starts.size(); ++index) {
        if (sequence->starts[index] - sequence->finishes[index - 1] <
                std::chrono::milliseconds(options.sampleIntervalMs))
            return fail("sampling compressed the minimum observation interval");
    }
    return EXIT_SUCCESS;
}

int main() {
    for (int scenario = 0; scenario < 5; ++scenario) {
        if (runCase(scenario) != EXIT_SUCCESS) return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
