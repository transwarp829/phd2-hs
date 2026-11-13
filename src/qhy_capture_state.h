#ifndef QHY_CAPTURE_STATE_H
#define QHY_CAPTURE_STATE_H

// Capture state shared by the QHY driver and its hardware-independent tests.
// The callbacks return true only when the corresponding SDK operation succeeds.
class QhyCaptureState
{
    bool m_live = false;
    bool m_pendingLive = false;
    bool m_streaming = false;
    int m_exposureMs = -1;

public:
    enum class Error
    {
        None,
        Stop,
        Exposure,
        Start
    };

    void BeginConnection(bool live)
    {
        m_live = m_pendingLive = live;
        m_streaming = false;
        m_exposureMs = -1;
    }

    void EndConnection()
    {
        m_live = false;
        m_streaming = false;
        m_exposureMs = -1;
    }

    bool LiveMode() const { return m_live; }
    bool PendingLiveMode() const { return m_pendingLive; }
    bool Streaming() const { return m_streaming; }
    int ExposureMs() const { return m_exposureMs; }
    void SetPendingLiveMode(bool live) { m_pendingLive = live; }
    void InvalidateExposure() { m_exposureMs = -1; }

    template<typename Stop>
    bool StopLive(Stop stop)
    {
        if (m_streaming)
        {
            if (!stop())
                return false;
            m_streaming = false;
        }
        return true;
    }

    template<typename Start>
    bool StartLive(Start start)
    {
        if (m_live && !m_streaming)
        {
            if (!start())
                return false;
            m_streaming = true;
        }
        return true;
    }

    template<typename Stop, typename SetExposure, typename Start>
    Error PrepareExposure(int durationMs, Stop stop, SetExposure setExposure, Start start)
    {
        if (durationMs != m_exposureMs)
        {
            // A live camera may still be integrating with the previous duration.
            // Restart only on a real parameter change, never on the stable path.
            if (m_live && !StopLive(stop))
                return Error::Stop;
            if (!setExposure(durationMs))
            {
                m_exposureMs = -1;
                return Error::Exposure;
            }
            m_exposureMs = durationMs;
        }
        if (!StartLive(start))
            return Error::Start;
        return Error::None;
    }
};

#endif
