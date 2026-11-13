#ifndef STATS_REFRESH_POLICY_H_INCLUDED
#define STATS_REFRESH_POLICY_H_INCLUDED

#include <cstdint>

// Only a visible statistics pane needs a frame-rate display. Keep both the
// scheduling and the changed-text check here so timer events cannot turn into
// per-frame synchronous repaints (including queued events after hiding).
class StatsRefreshPolicy
{
    bool m_visible = false;
    std::int64_t m_nextRefreshMs = 0;

public:
    enum
    {
        IntervalMs = 250
    };

    template<class Backend>
    void SetVisible(Backend& backend, bool visible)
    {
        if (visible == m_visible)
            return;
        m_visible = visible;
        if (visible)
        {
            m_nextRefreshMs = backend.NowMs() + IntervalMs;
            backend.StartTimer(IntervalMs);
        }
        else
            backend.StopTimer();
    }

    template<class Backend>
    void OnTimer(Backend& backend)
    {
        if (!m_visible)
            return;
        const std::int64_t now = backend.NowMs();
        if (now < m_nextRefreshMs)
            return;
        // Schedule from now, not the previous deadline: delayed events must not
        // trigger a burst of catch-up paints.
        m_nextRefreshMs = now + IntervalMs;
        const auto text = backend.FrameRateText();
        if (text != backend.DisplayedText())
        {
            backend.SetText(text);
            backend.FlushPaint();
        }
    }
};

#endif
