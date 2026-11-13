#include "zwo_capture_state.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <vector>

namespace
{
using zwo_capture::Operation;

void Require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct FakeCamera
{
    long exposure = 1000000;
    long gain = 20;
    bool streaming = true;
    long inFlightExposure = exposure;
    long inFlightGain = gain;
    Operation fail = Operation::None;
    std::vector<Operation> calls;

    int Call(Operation operation)
    {
        calls.push_back(operation);
        return operation == fail ? 17 : 0;
    }

    int SetExposure(long value)
    {
        int error = Call(Operation::SetExposure);
        if (!error)
            exposure = value;
        return error;
    }

    int SetGain(long value)
    {
        int error = Call(Operation::SetGain);
        if (!error)
            gain = value;
        return error;
    }

    int StopVideo()
    {
        int error = Call(Operation::StopVideo);
        if (!error)
        {
            streaming = false;
            inFlightExposure = inFlightGain = -1;
        }
        return error;
    }

    int StartVideo()
    {
        int error = Call(Operation::StartVideo);
        if (!error)
        {
            streaming = true;
            inFlightExposure = exposure;
            inFlightGain = gain;
        }
        return error;
    }
};

void RequireCalls(const FakeCamera& camera, std::initializer_list<Operation> expected, const char *message)
{
    Require(camera.calls == std::vector<Operation>(expected), message);
}

void UnchangedSettingsKeepTheStream()
{
    FakeCamera camera;
    bool capturing = true;
    long lastExposure = camera.exposure;
    long lastGain = camera.gain;
    auto result = zwo_capture::Configure(camera, true, capturing, lastExposure, lastGain, true, camera.exposure, camera.gain);
    Require(result.Ok() && capturing && camera.streaming, "unchanged settings keep video active");
    RequireCalls(camera, {}, "steady frames must not touch the SDK at all");
    Require(zwo_capture::StartVideo(camera, capturing).Ok(), "starting an active stream is a no-op");
    RequireCalls(camera, {}, "starting an active stream must not call the SDK");
}

void ChangedSettingsDiscardTheInFlightFrame()
{
    for (int change = 1; change <= 3; ++change)
    {
        FakeCamera camera;
        bool capturing = true;
        long lastExposure = camera.exposure;
        long lastGain = camera.gain;
        long const exposure = (change & 1) ? 3000000 : camera.exposure;
        long const gain = (change & 2) ? 40 : camera.gain;
        auto result = zwo_capture::Configure(camera, true, capturing, lastExposure, lastGain, true, exposure, gain);
        Require(result.Ok() && !capturing && !camera.streaming, "changed settings stop the old stream");
        Require(camera.inFlightExposure == -1, "the old-parameter exposure must be cancelled");
        Require(lastExposure == exposure && lastGain == gain, "successful writes update the cached settings");
        Require(zwo_capture::StartVideo(camera, capturing).Ok(), "changed settings can start a fresh stream");
        Require(capturing && camera.inFlightExposure == exposure && camera.inFlightGain == gain,
                "the next frame must use the requested exposure and gain");
        std::vector<Operation> expected = { Operation::StopVideo };
        if (change & 1)
            expected.push_back(Operation::SetExposure);
        if (change & 2)
            expected.push_back(Operation::SetGain);
        expected.push_back(Operation::StartVideo);
        Require(camera.calls == expected, "stop must precede every changed setting and restart must follow them");
    }
}

void InactiveAndSnapshotCamerasDoNotStop()
{
    for (bool videoMode : { false, true })
    {
        FakeCamera camera;
        camera.streaming = false;
        bool capturing = false;
        long lastExposure = -1;
        long lastGain = -1;
        auto result = zwo_capture::Configure(camera, videoMode, capturing, lastExposure, lastGain, true, 2000000, 40);
        Require(result.Ok() && !capturing, "configuration must not start a camera");
        RequireCalls(camera, { Operation::SetExposure, Operation::SetGain }, "an inactive camera needs no stop call");
    }
}

void LongToShortExposureDoesNotReturnTheOldFrame()
{
    FakeCamera camera;
    camera.exposure = camera.inFlightExposure = 10000000;
    bool capturing = true;
    long lastExposure = 10000000;
    long lastGain = camera.gain;
    Require(zwo_capture::Configure(camera, true, capturing, lastExposure, lastGain, true, 10000, camera.gain).Ok(),
            "ten-second to ten-millisecond exposure change");
    Require(!capturing && camera.inFlightExposure == -1, "the old ten-second integration must be cancelled");
    Require(zwo_capture::StartVideo(camera, capturing).Ok(), "start the ten-millisecond integration");
    Require(camera.inFlightExposure == 10000, "the new short-exposure request must not return an old long frame");
    RequireCalls(camera, { Operation::StopVideo, Operation::SetExposure, Operation::StartVideo },
                 "long-to-short transition must stop, configure, then restart");
}

void CamerasWithoutGainDoNotWriteIt()
{
    FakeCamera camera;
    bool capturing = true;
    long lastExposure = 999; // force an exposure write
    long lastGain = 20;
    auto result = zwo_capture::Configure(camera, true, capturing, lastExposure, lastGain, false, camera.exposure, 99);
    Require(result.Ok() && !capturing, "gain is optional");
    Require(lastGain == 20, "unsupported gain must not update the gain cache");
    RequireCalls(camera, { Operation::StopVideo, Operation::SetExposure }, "unsupported gain must not be written");
}

void ReadBackMismatchCannotForceARestart()
{
    // The body may report a clamped/rounded value for what we wrote; the cache
    // compares against the requested values, so a steady loop stays on the
    // no-restart fast path.
    FakeCamera camera;
    camera.exposure = 999999; // simulated clamp of the written 1000000 us
    camera.gain = 20;
    bool capturing = true;
    long lastExposure = 1000000;
    long lastGain = 20;
    auto result = zwo_capture::Configure(camera, true, capturing, lastExposure, lastGain, true, 1000000, 20);
    Require(result.Ok() && capturing && camera.streaming, "a read-back mismatch must not restart the stream");
    RequireCalls(camera, {}, "steady settings must not reissue SDK calls even if read-back would differ");
}

void StopFailurePreventsParameterChanges()
{
    FakeCamera camera;
    camera.fail = Operation::StopVideo;
    bool capturing = true;
    long lastExposure = camera.exposure;
    long lastGain = camera.gain;
    auto result = zwo_capture::Configure(camera, true, capturing, lastExposure, lastGain, true, 3000000, 40);
    Require(!result.Ok() && result.operation == Operation::StopVideo, "stop failure must propagate");
    Require(capturing && camera.streaming, "a failed stop must not claim capture has stopped");
    Require(lastExposure == camera.exposure && lastGain == camera.gain, "failed stop must not update the caches");
    RequireCalls(camera, { Operation::StopVideo }, "failed stop must prevent writes");
}

void WriteFailuresLeaveTheStreamStopped()
{
    for (Operation failure : { Operation::SetExposure, Operation::SetGain })
    {
        FakeCamera camera;
        camera.fail = failure;
        bool capturing = true;
        long lastExposure = camera.exposure;
        long lastGain = camera.gain;
        auto result = zwo_capture::Configure(camera, true, capturing, lastExposure, lastGain, true, 3000000, 40);
        Require(!result.Ok() && result.operation == failure && result.error == 17, "write failure must propagate");
        Require(!capturing && !camera.streaming, "a failed update must not resume a partly configured stream");
        if (failure == Operation::SetExposure)
        {
            Require(lastExposure == 1000000 && lastGain == 20, "failed exposure write must not update either cache");
            RequireCalls(camera, { Operation::StopVideo, Operation::SetExposure },
                         "failed exposure write must prevent the gain write and restart");
        }
        else
        {
            Require(lastExposure == 3000000, "successful exposure write updates its cache before the failed gain");
            Require(lastGain == 20, "failed gain write must not update the gain cache");
            RequireCalls(camera, { Operation::StopVideo, Operation::SetExposure, Operation::SetGain },
                         "failed gain write must prevent restart");
        }
    }
}

void StartFailureDoesNotMarkCaptureActive()
{
    FakeCamera camera;
    bool capturing = true;
    long lastExposure = camera.exposure;
    long lastGain = camera.gain;
    Require(zwo_capture::Configure(camera, true, capturing, lastExposure, lastGain, true, 3000000, 40).Ok(),
            "setup for failed start");
    camera.fail = Operation::StartVideo;
    auto result = zwo_capture::StartVideo(camera, capturing);
    Require(!result.Ok() && result.operation == Operation::StartVideo && result.error == 17, "start failure must propagate");
    Require(!capturing && !camera.streaming, "a failed start must leave capture inactive");
    camera.fail = Operation::None;
    Require(zwo_capture::StartVideo(camera, capturing).Ok() && capturing, "a later successful start can recover");
    Require(camera.inFlightExposure == 3000000 && camera.inFlightGain == 40, "recovery must use the new settings");
}

void StoppingIsIdempotent()
{
    FakeCamera camera;
    bool capturing = true;
    Require(zwo_capture::StopVideo(camera, capturing).Ok() && !capturing, "successful stop clears local state");
    Require(zwo_capture::StopVideo(camera, capturing).Ok(), "repeated stop succeeds");
    RequireCalls(camera, { Operation::StopVideo }, "repeated stop must not reissue the SDK call");
}
} // namespace

int main()
{
    UnchangedSettingsKeepTheStream();
    ChangedSettingsDiscardTheInFlightFrame();
    LongToShortExposureDoesNotReturnTheOldFrame();
    InactiveAndSnapshotCamerasDoNotStop();
    CamerasWithoutGainDoNotWriteIt();
    ReadBackMismatchCannotForceARestart();
    StopFailurePreventsParameterChanges();
    WriteFailuresLeaveTheStreamStopped();
    StartFailureDoesNotMarkCaptureActive();
    StoppingIsIdempotent();
    std::cout << "ZWO capture state regression tests passed\n";
    return EXIT_SUCCESS;
}
