#include "zwo_capture_state.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>
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

    int GetExposure(long& value)
    {
        int error = Call(Operation::GetExposure);
        if (!error)
            value = exposure;
        return error;
    }

    int GetGain(long& value)
    {
        int error = Call(Operation::GetGain);
        if (!error)
            value = gain;
        return error;
    }

    int SetExposure(long value)
    {
        int error = Call(Operation::SetExposure);
        if (!error)
            exposure = value;
        // Changing a setting does not change the exposure already in flight.
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
    auto result = zwo_capture::Configure(camera, true, capturing, camera.exposure, true, camera.gain);
    Require(result.Ok() && capturing && camera.streaming, "unchanged settings keep video active");
    Require(zwo_capture::StartVideo(camera, capturing).Ok(), "starting an active stream is a no-op");
    RequireCalls(camera, { Operation::GetExposure, Operation::GetGain }, "steady frames must not stop or rewrite settings");
}

void ChangedSettingsDiscardTheInFlightFrame()
{
    for (int change = 1; change <= 3; ++change)
    {
        FakeCamera camera;
        bool capturing = true;
        long exposure = (change & 1) ? 3000000 : camera.exposure;
        long gain = (change & 2) ? 40 : camera.gain;
        auto result = zwo_capture::Configure(camera, true, capturing, exposure, true, gain);
        Require(result.Ok() && !capturing && !camera.streaming, "changed settings stop the old stream");
        Require(camera.inFlightExposure == -1, "the old-parameter exposure must be cancelled");
        Require(zwo_capture::StartVideo(camera, capturing).Ok(), "changed settings can start a fresh stream");
        Require(capturing && camera.inFlightExposure == exposure && camera.inFlightGain == gain,
                "the next frame must use the requested exposure and gain");
        std::vector<Operation> expected = { Operation::GetExposure, Operation::GetGain, Operation::StopVideo };
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
        auto result = zwo_capture::Configure(camera, videoMode, capturing, 2000000, true, 40);
        Require(result.Ok() && !capturing, "configuration must not start a camera");
        RequireCalls(camera, { Operation::GetExposure, Operation::GetGain, Operation::SetExposure, Operation::SetGain },
                     "an inactive camera needs no stop call");
    }
}

void LongToShortExposureDoesNotReturnTheOldFrame()
{
    FakeCamera camera;
    camera.exposure = camera.inFlightExposure = 10000000;
    bool capturing = true;
    Require(zwo_capture::Configure(camera, true, capturing, 10000, true, camera.gain).Ok(),
            "ten-second to ten-millisecond exposure change");
    Require(!capturing && camera.inFlightExposure == -1, "the old ten-second integration must be cancelled");
    Require(zwo_capture::StartVideo(camera, capturing).Ok(), "start the ten-millisecond integration");
    Require(camera.inFlightExposure == 10000, "the new short-exposure request must not return an old long frame");
    RequireCalls(
        camera,
        { Operation::GetExposure, Operation::GetGain, Operation::StopVideo, Operation::SetExposure, Operation::StartVideo },
        "long-to-short transition must stop, configure, then restart");
}

void CamerasWithoutGainDoNotReadIt()
{
    FakeCamera camera;
    camera.fail = Operation::GetGain;
    bool capturing = true;
    auto result = zwo_capture::Configure(camera, true, capturing, camera.exposure, false, 99);
    Require(result.Ok() && capturing, "gain is optional");
    RequireCalls(camera, { Operation::GetExposure }, "unsupported gain must not be accessed");
}

void ReadFailuresDoNotChangeTheCamera()
{
    for (Operation failure : { Operation::GetExposure, Operation::GetGain })
    {
        FakeCamera camera;
        camera.fail = failure;
        bool capturing = true;
        auto result = zwo_capture::Configure(camera, true, capturing, 3000000, true, 40);
        Require(!result.Ok() && result.operation == failure && result.error == 17, "read failure must propagate");
        Require(capturing && camera.streaming && camera.exposure == 1000000 && camera.gain == 20,
                "read failure must not mutate settings or local stream state");
        if (failure == Operation::GetExposure)
            RequireCalls(camera, { Operation::GetExposure }, "exposure read failure must stop configuration");
        else
            RequireCalls(camera, { Operation::GetExposure, Operation::GetGain }, "gain read failure must stop configuration");
    }
}

void StopFailurePreventsParameterChanges()
{
    FakeCamera camera;
    camera.fail = Operation::StopVideo;
    bool capturing = true;
    auto result = zwo_capture::Configure(camera, true, capturing, 3000000, true, 40);
    Require(!result.Ok() && result.operation == Operation::StopVideo, "stop failure must propagate");
    Require(capturing && camera.streaming, "a failed stop must not claim capture has stopped");
    RequireCalls(camera, { Operation::GetExposure, Operation::GetGain, Operation::StopVideo },
                 "failed stop must prevent writes");
}

void WriteFailuresLeaveTheStreamStopped()
{
    for (Operation failure : { Operation::SetExposure, Operation::SetGain })
    {
        FakeCamera camera;
        camera.fail = failure;
        bool capturing = true;
        auto result = zwo_capture::Configure(camera, true, capturing, 3000000, true, 40);
        Require(!result.Ok() && result.operation == failure && result.error == 17, "write failure must propagate");
        Require(!capturing && !camera.streaming, "a failed update must not resume a partly configured stream");
        if (failure == Operation::SetExposure)
            RequireCalls(camera, { Operation::GetExposure, Operation::GetGain, Operation::StopVideo, Operation::SetExposure },
                         "failed exposure write must prevent the gain write and restart");
        else
            RequireCalls(camera,
                         { Operation::GetExposure, Operation::GetGain, Operation::StopVideo, Operation::SetExposure,
                           Operation::SetGain },
                         "failed gain write must prevent restart");
    }
}

void StartFailureDoesNotMarkCaptureActive()
{
    FakeCamera camera;
    bool capturing = true;
    Require(zwo_capture::Configure(camera, true, capturing, 3000000, true, 40).Ok(), "setup for failed start");
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
    CamerasWithoutGainDoNotReadIt();
    ReadFailuresDoNotChangeTheCamera();
    StopFailurePreventsParameterChanges();
    WriteFailuresLeaveTheStreamStopped();
    StartFailureDoesNotMarkCaptureActive();
    StoppingIsIdempotent();
    std::cout << "ZWO capture state regression tests passed\n";
    return EXIT_SUCCESS;
}
