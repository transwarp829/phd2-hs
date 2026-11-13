#ifndef PHD2_ZWO_CAPTURE_STATE_H
#define PHD2_ZWO_CAPTURE_STATE_H

// Keep the video-state transitions independent of the SDK so the same ordering
// can be checked with a fake camera. Api methods return zero on success.
namespace zwo_capture
{
enum class Operation
{
    None,
    StopVideo,
    SetExposure,
    SetGain,
    StartVideo
};

struct Result
{
    Operation operation;
    int error;

    bool Ok() const { return error == 0; }
};

inline const char *OperationName(Operation operation)
{
    switch (operation)
    {
    case Operation::StopVideo:
        return "stop video";
    case Operation::SetExposure:
        return "set exposure";
    case Operation::SetGain:
        return "set gain";
    case Operation::StartVideo:
        return "start video";
    default:
        return "none";
    }
}

template<typename Api>
Result StopVideo(Api& api, bool& capturing)
{
    if (capturing)
    {
        int error = api.StopVideo();
        if (error)
            return { Operation::StopVideo, error };
        capturing = false;
    }
    return { Operation::None, 0 };
}

template<typename Api>
Result StartVideo(Api& api, bool& capturing)
{
    if (!capturing)
    {
        int error = api.StartVideo();
        if (error)
            return { Operation::StartVideo, error };
        capturing = true;
    }
    return { Operation::None, 0 };
}

// Compare and write against the values this driver last wrote successfully
// instead of reading them back from the SDK every frame. ASIGetControlValue can
// report a clamped/rounded value for a camera body, which would otherwise make
// every frame stop and restart the stream; a failed read must not disconnect the
// camera either. The caches are -1 while unknown (connect, first frame).
template<typename Api>
Result Configure(Api& api, bool videoMode, bool& capturing, long& lastExposureUs, long& lastGain, bool hasGain, long exposureUs,
                 long gain)
{
    bool const exposureChanged = lastExposureUs != exposureUs;
    bool const gainChanged = hasGain && lastGain != gain;
    if (!exposureChanged && !gainChanged)
        return { Operation::None, 0 };

    // Draining completed frames cannot discard an old-parameter exposure still
    // in flight. Stop that exposure before changing settings; the caller starts
    // a fresh stream after any ROI changes, immediately before fetching a frame.
    if (videoMode)
    {
        Result stopped = StopVideo(api, capturing);
        if (!stopped.Ok())
            return stopped;
    }

    if (exposureChanged)
    {
        int error = api.SetExposure(exposureUs);
        if (error)
            return { Operation::SetExposure, error };
        lastExposureUs = exposureUs;
    }
    if (gainChanged)
    {
        int error = api.SetGain(gain);
        if (error)
            return { Operation::SetGain, error };
        lastGain = gain;
    }
    return { Operation::None, 0 };
}
} // namespace zwo_capture

#endif
