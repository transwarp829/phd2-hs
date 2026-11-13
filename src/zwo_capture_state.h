#ifndef PHD2_ZWO_CAPTURE_STATE_H
#define PHD2_ZWO_CAPTURE_STATE_H

// Keep the video-state transitions independent of the SDK so the same ordering
// can be checked with a fake camera. Api methods return zero on success.
namespace zwo_capture
{
enum class Operation
{
    None,
    GetExposure,
    GetGain,
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
    case Operation::GetExposure:
        return "get exposure";
    case Operation::GetGain:
        return "get gain";
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

template<typename Api>
Result Configure(Api& api, bool videoMode, bool& capturing, long exposureUs, bool hasGain, long gain)
{
    long currentExposure = 0;
    int error = api.GetExposure(currentExposure);
    if (error)
        return { Operation::GetExposure, error };

    long currentGain = 0;
    if (hasGain)
    {
        error = api.GetGain(currentGain);
        if (error)
            return { Operation::GetGain, error };
    }

    bool exposureChanged = currentExposure != exposureUs;
    bool gainChanged = hasGain && currentGain != gain;
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
        error = api.SetExposure(exposureUs);
        if (error)
            return { Operation::SetExposure, error };
    }
    if (gainChanged)
    {
        error = api.SetGain(gain);
        if (error)
            return { Operation::SetGain, error };
    }
    return { Operation::None, 0 };
}
} // namespace zwo_capture

#endif
