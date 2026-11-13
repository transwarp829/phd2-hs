#include "stats_refresh_policy.h"

#include <iostream>
#include <stdexcept>
#include <string>

static void Require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

// This backend stands in for the wxTimer, clock and grid. The production
// controller decides when to read the latest text, change the cell and paint.
struct FakeStatsWindow
{
    std::int64_t now = 0;
    bool timerRunning = false;
    int timerStarts = 0;
    int timerStops = 0;
    int timerInterval = 0;
    int textReads = 0;
    int cellWrites = 0;
    int paints = 0;
    std::string latestText;
    std::string displayedText;

    std::int64_t NowMs() const { return now; }
    void StartTimer(int interval)
    {
        Require(!timerRunning, "visibility update must not create a second timer");
        timerRunning = true;
        timerInterval = interval;
        ++timerStarts;
    }
    void StopTimer()
    {
        timerRunning = false;
        ++timerStops;
    }
    std::string FrameRateText()
    {
        ++textReads;
        return latestText;
    }
    const std::string& DisplayedText() const { return displayedText; }
    void SetText(const std::string& text)
    {
        displayedText = text;
        ++cellWrites;
    }
    void FlushPaint() { ++paints; }
};

static void visibility_owns_one_timer()
{
    StatsRefreshPolicy refresh;
    FakeStatsWindow window;
    window.latestText = "30.0 fps";
    refresh.OnTimer(window);
    Require(!window.timerRunning && window.textReads == 0, "initially hidden window must do no display work");

    refresh.SetVisible(window, true);
    Require(window.timerRunning && window.timerStarts == 1 && window.timerInterval == 250,
            "showing the pane must start one four-Hz timer");
    Require(window.textReads == 0 && window.paints == 0, "showing must not perform synchronous paint work");
    refresh.SetVisible(window, true);
    Require(window.timerStarts == 1, "repeated visible state must not restart the timer");
    window.now = 250;
    refresh.OnTimer(window);
    Require(window.paints == 1 && window.displayedText == "30.0 fps", "first timer tick must show current rate");

    refresh.SetVisible(window, false);
    Require(!window.timerRunning && window.timerStops == 1, "hiding must stop the timer");
    window.now = 500;
    window.latestText = "60.0 fps";
    refresh.OnTimer(window); // A timer event may already have been queued before hiding.
    Require(window.textReads == 1 && window.paints == 1, "queued hidden timer event must not read or paint");
    refresh.SetVisible(window, false);
    Require(window.timerStops == 1, "repeated hidden state should be a no-op");

    refresh.SetVisible(window, true);
    Require(window.timerStarts == 2 && window.paints == 1, "reopening only starts a timer");
    window.now = 749;
    refresh.OnTimer(window);
    Require(window.paints == 1, "queued old timer event cannot bypass reopen delay");
    window.now = 750;
    refresh.OnTimer(window);
    Require(window.displayedText == "60.0 fps" && window.paints == 2, "reopen must sample the latest rate");
}

static void frequent_requests_cannot_bypass_the_limit()
{
    StatsRefreshPolicy refresh;
    FakeStatsWindow window;
    refresh.SetVisible(window, true);
    // Model a busy event stream: neither repeated visibility notifications nor
    // extra timer events can turn display work into a per-frame operation.
    for (int now = 1; now <= 1000; ++now)
    {
        window.now = now;
        window.latestText = std::to_string(now);
        refresh.SetVisible(window, true);
        refresh.OnTimer(window);
    }
    Require(window.timerStarts == 1, "frequent state notifications must not restart the timer");
    Require(window.textReads == 4 && window.cellWrites == 4 && window.paints == 4,
            "one thousand requests must produce at most four updates in a second");
    Require(window.displayedText == "1000", "rate limiting must coalesce to the newest value");
}

static void unchanged_text_does_not_repaint()
{
    StatsRefreshPolicy refresh;
    FakeStatsWindow window;
    window.latestText = "29.4 fps (34 ms)";
    refresh.SetVisible(window, true);
    for (int now = 250; now <= 2000; now += 250)
    {
        window.now = now;
        refresh.OnTimer(window);
    }
    Require(window.textReads == 8, "timer must continue sampling a stable rate");
    Require(window.cellWrites == 1 && window.paints == 1, "unchanged formatted text must not dirty or repaint the cell");
}

static void stopped_capture_clears_once_on_the_next_tick()
{
    StatsRefreshPolicy refresh;
    FakeStatsWindow window;
    window.latestText = "34.5 fps (29 ms)";
    refresh.SetVisible(window, true);
    window.now = 250;
    refresh.OnTimer(window);
    // The real backend yields empty text after capture stops, while paused, or
    // before the first measured frame interval exists.
    window.latestText.clear();
    window.now = 251;
    refresh.OnTimer(window);
    Require(!window.displayedText.empty() && window.paints == 1, "stop must not bypass the paint limit");
    window.now = 500;
    refresh.OnTimer(window);
    Require(window.displayedText.empty() && window.paints == 2, "stopped capture must clear on the next tick");
    window.now = 750;
    refresh.OnTimer(window);
    Require(window.cellWrites == 2 && window.paints == 2, "idle empty text must not repaint repeatedly");
    window.latestText = "10.0 fps (100 ms)";
    window.now = 1000;
    refresh.OnTimer(window);
    Require(window.displayedText == window.latestText && window.paints == 3, "capture restart must show new data");
}

static void delayed_events_do_not_catch_up_in_a_burst()
{
    StatsRefreshPolicy refresh;
    FakeStatsWindow window;
    refresh.SetVisible(window, true);
    window.now = 10000;
    window.latestText = "newest rate";
    refresh.OnTimer(window);
    for (int i = 0; i < 100; ++i)
    {
        window.latestText = std::to_string(i);
        refresh.OnTimer(window);
    }
    Require(window.textReads == 1 && window.paints == 1, "delayed timer events must not cause catch-up paints");
    window.now = 10250;
    refresh.OnTimer(window);
    Require(window.displayedText == "99" && window.paints == 2, "the next ordinary deadline must use latest data");
}

int main()
{
    try
    {
        visibility_owns_one_timer();
        frequent_requests_cannot_bypass_the_limit();
        unchanged_text_does_not_repaint();
        stopped_capture_clears_once_on_the_next_tick();
        delayed_events_do_not_catch_up_in_a_burst();
        std::cout << "Statistics refresh regression tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
