#include "../src/qhy_capture_state.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct FakeCamera
{
    std::vector<std::string> calls;
    bool stopOk = true;
    bool exposureOk = true;
    bool startOk = true;
    int configuredExposure = -1;
    int integratingExposure = -1;

    bool Stop()
    {
        calls.push_back("stop");
        if (stopOk)
            integratingExposure = -1;
        return stopOk;
    }

    bool SetExposure(int ms)
    {
        calls.push_back("set:" + std::to_string(ms));
        if (exposureOk)
            configuredExposure = ms;
        // A setting change does not alter the exposure already in progress.
        return exposureOk;
    }

    bool Start()
    {
        calls.push_back("start");
        if (startOk)
            integratingExposure = configuredExposure;
        return startOk;
    }

    QhyCaptureState::Error Prepare(QhyCaptureState& state, int duration)
    {
        return state.PrepareExposure(
            duration, [&]() { return Stop(); }, [&](int ms) { return SetExposure(ms); }, [&]() { return Start(); });
    }

    void Expect(std::initializer_list<std::string> expected, const char *message)
    {
        require(calls == std::vector<std::string>(expected), message);
        calls.clear();
    }
};

static void pendingModeDoesNotChangeActiveConnection()
{
    QhyCaptureState state;
    FakeCamera camera;
    state.BeginConnection(false);
    state.SetPendingLiveMode(true);
    require(state.PendingLiveMode(), "reopened property dialog must show pending live mode");
    require(!state.LiveMode(), "enabling pending mode must preserve single-frame connection");
    require(camera.Prepare(state, 100) == QhyCaptureState::Error::None, "single-frame exposure");
    camera.Expect({ "set:100" }, "pending live mode must not start a live stream");

    state.EndConnection();
    state.BeginConnection(state.PendingLiveMode());
    require(state.LiveMode(), "reconnect must apply pending live mode");
    require(state.ExposureMs() == -1, "reconnect must discard exposure cache");
    require(camera.Prepare(state, 100) == QhyCaptureState::Error::None, "live exposure after reconnect");
    camera.Expect({ "set:100", "start" }, "reconnect must configure exposure before live capture");

    state.SetPendingLiveMode(false);
    require(!state.PendingLiveMode(), "reopened property dialog must retain pending single mode");
    require(state.LiveMode() && state.Streaming(), "disabling pending mode must keep active live mode");
    require(camera.Prepare(state, 100) == QhyCaptureState::Error::None, "unchanged live exposure");
    camera.Expect({}, "pending single mode must not alter running stream");
    state.EndConnection();
    state.BeginConnection(state.PendingLiveMode());
    require(!state.LiveMode(), "reconnect must apply pending single mode");
}

static void exposureChangesRestartOnlyWhenNeeded()
{
    QhyCaptureState state;
    FakeCamera camera;
    state.BeginConnection(true);
    // The real connection initially starts the stream at the SDK default.
    require(state.StartLive([&]() { return camera.Start(); }), "connect stream start");
    camera.Expect({ "start" }, "connection should start once");
    require(camera.Prepare(state, 10000) == QhyCaptureState::Error::None, "first exposure");
    camera.Expect({ "stop", "set:10000", "start" }, "first frame must discard SDK-default integration");
    require(camera.integratingExposure == 10000, "first frame exposure must match request");

    require(camera.Prepare(state, 10) == QhyCaptureState::Error::None, "long-to-short exposure");
    camera.Expect({ "stop", "set:10", "start" }, "long-to-short must stop old integration before restart");
    require(camera.integratingExposure == 10, "new timeout must wait on new exposure, not old 10-second frame");
    for (int i = 0; i != 100; ++i)
        require(camera.Prepare(state, 10) == QhyCaptureState::Error::None, "stable exposure");
    camera.Expect({}, "stable fast path must make zero SDK calls");

    require(camera.Prepare(state, 3000) == QhyCaptureState::Error::None, "short-to-long exposure");
    camera.Expect({ "stop", "set:3000", "start" }, "short-to-long must discard old short frame");
    require(camera.integratingExposure == 3000, "short-to-long frame must use requested exposure");

    // The driver stops the stream for an ROI change, then invalidates exposure.
    require(state.StopLive([&]() { return camera.Stop(); }), "stop for ROI change");
    state.InvalidateExposure();
    require(camera.Prepare(state, 3000) == QhyCaptureState::Error::None, "exposure after ROI change");
    camera.Expect({ "stop", "set:3000", "start" }, "ROI restart must follow exposure write without a second stop");
}

static void failuresDoNotPermitCapture()
{
    QhyCaptureState state;
    FakeCamera camera;
    state.BeginConnection(true);
    require(camera.Prepare(state, 10000) == QhyCaptureState::Error::None, "initial live exposure");
    camera.calls.clear();

    camera.stopOk = false;
    require(camera.Prepare(state, 10) == QhyCaptureState::Error::Stop, "stop failure must reject capture");
    camera.Expect({ "stop" }, "failed stop must not write exposure or restart");
    require(state.Streaming() && state.ExposureMs() == 10000, "failed stop must preserve known active state");

    camera.stopOk = true;
    camera.exposureOk = false;
    require(camera.Prepare(state, 10) == QhyCaptureState::Error::Exposure, "exposure failure must reject capture");
    camera.Expect({ "stop", "set:10" }, "failed exposure must not restart stream");
    require(!state.Streaming() && state.ExposureMs() == -1, "failed exposure must not be cached");

    camera.exposureOk = true;
    camera.startOk = false;
    require(camera.Prepare(state, 10) == QhyCaptureState::Error::Start, "restart failure must reject capture");
    camera.Expect({ "set:10", "start" }, "restart failure must leave stream stopped");
    require(!state.Streaming(), "failed restart must not claim streaming");

    camera.startOk = true;
    require(camera.Prepare(state, 10) == QhyCaptureState::Error::None, "retry restart");
    camera.Expect({ "start" }, "retry must start without rewriting successful exposure");

    state.EndConnection();
    state.BeginConnection(true);
    require(camera.Prepare(state, 10) == QhyCaptureState::Error::None, "same exposure after reconnect");
    camera.Expect({ "set:10", "start" }, "reconnect must send the exposure again even if unchanged");
}

int main()
{
    try
    {
        pendingModeDoesNotChangeActiveConnection();
        exposureChangesRestartOnlyWhenNeeded();
        failuresDoNotPermitCapture();
        std::cout << "QHY capture regression tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
