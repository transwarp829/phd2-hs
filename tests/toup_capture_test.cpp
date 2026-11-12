#include "toup_capture_wait.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

// A deterministic camera/condition-variable adapter. Pull and callback entry
// enforce the same lock contract as the real SDK adapter.
struct FakeCamera
{
    bool locked = false;
    bool cameraError = false;
    bool interrupted = false;
    bool expired = false;
    unsigned long long generation = 0;
    int pulls = 0;
    int waits = 0;
    std::function<ToupCaptureWait::PullResult(FakeCamera&)> onPull;
    std::function<void(FakeCamera&)> onWait;

    void Lock()
    {
        require(!locked, "recursive callback lock / SDK called while locked");
        locked = true;
    }
    void Unlock()
    {
        require(locked, "unlock without lock");
        locked = false;
    }
    unsigned long long Generation() const
    {
        require(locked, "generation read without callback lock");
        return generation;
    }
    bool HasError() const
    {
        require(locked, "error read without callback lock");
        return cameraError;
    }
    bool Interrupted() const { return interrupted; }
    bool Expired() const { return expired; }
    ToupCaptureWait::PullResult Pull()
    {
        require(!locked, "SDK Pull must run without callback lock");
        require(++pulls < 20, "unexpected busy loop");
        return onPull(*this);
    }
    void Wait()
    {
        require(locked, "condition wait must start under callback lock");
        require(++waits < 20, "unexpected wait loop");
        Unlock();
        onWait(*this);
        Lock();
    }
    void Notify(bool error = false)
    {
        Lock();
        cameraError = error;
        ++generation;
        Unlock();
    }
};

static void queued_frame_does_not_need_a_new_event()
{
    FakeCamera cam;
    cam.generation = 7; // RealTime=0: existing notifications, full queue, producer stopped
    cam.onPull = [](FakeCamera&) { return ToupCaptureWait::PullResult::Ready; };
    cam.onWait = [](FakeCamera&) { throw std::runtime_error("full queue cannot produce a new event"); };
    require(ToupCaptureWait::WaitForFrame(cam) == ToupCaptureWait::Result::Ready, "queued frame not consumed");
    require(cam.pulls == 1 && cam.waits == 0 && !cam.locked, "queued frame should be returned immediately");
}

static void pending_waits_for_frame()
{
    FakeCamera cam;
    bool ready = false;
    cam.onPull = [&](FakeCamera&) { return ready ? ToupCaptureWait::PullResult::Ready : ToupCaptureWait::PullResult::Pending; };
    cam.onWait = [&](FakeCamera& c)
    {
        ready = true;
        c.Notify();
    };
    require(ToupCaptureWait::WaitForFrame(cam) == ToupCaptureWait::Result::Ready, "frame event not consumed");
    require(cam.pulls == 2 && cam.waits == 1 && !cam.locked, "pending should wait once before retrying");
}

static void notification_during_pull_is_not_lost()
{
    FakeCamera cam;
    cam.onPull = [](FakeCamera& c)
    {
        if (c.pulls == 1)
        {
            // The SDK decides E_PENDING, then delivers IMAGE before returning.
            c.Notify();
            return ToupCaptureWait::PullResult::Pending;
        }
        return ToupCaptureWait::PullResult::Ready;
    };
    cam.onWait = [](FakeCamera&) { throw std::runtime_error("notification during Pull was lost"); };
    require(ToupCaptureWait::WaitForFrame(cam) == ToupCaptureWait::Result::Ready, "race failed to retry");
    require(cam.pulls == 2 && cam.waits == 0 && !cam.locked, "changed generation must skip waiting");
}

static void spurious_wakeup_retries_without_claiming_a_frame()
{
    FakeCamera cam;
    cam.onPull = [](FakeCamera& c)
    { return c.waits == 2 ? ToupCaptureWait::PullResult::Ready : ToupCaptureWait::PullResult::Pending; };
    cam.onWait = [](FakeCamera& c)
    {
        if (c.waits == 2)
            c.Notify();
    };
    require(ToupCaptureWait::WaitForFrame(cam) == ToupCaptureWait::Result::Ready, "spurious wakeup mishandled");
    require(cam.waits == 2 && cam.pulls == 3 && !cam.locked, "spurious wakeup must recheck SDK");
}

static void failures_and_cancellation_are_preserved()
{
    using ToupCaptureWait::Result;
    FakeCamera cam;
    cam.onPull = [](FakeCamera&) { return ToupCaptureWait::PullResult::Pending; };
    cam.onWait = [](FakeCamera& c) { c.Notify(true); };
    require(ToupCaptureWait::WaitForFrame(cam) == Result::CameraError, "camera error lost while waiting");
    require(!cam.locked, "camera error left lock held");

    cam = FakeCamera();
    cam.cameraError = true;
    require(ToupCaptureWait::WaitForFrame(cam) == Result::CameraError && cam.pulls == 0,
            "pending camera error must precede Pull");

    cam = FakeCamera();
    cam.onPull = [](FakeCamera& c)
    {
        c.Notify(true);
        return ToupCaptureWait::PullResult::Pending;
    };
    require(ToupCaptureWait::WaitForFrame(cam) == Result::CameraError && cam.waits == 0,
            "camera error during Pull must not wait");

    cam = FakeCamera();
    cam.onPull = [](FakeCamera&) { return ToupCaptureWait::PullResult::Failed; };
    require(ToupCaptureWait::WaitForFrame(cam) == Result::PullError && cam.waits == 0, "SDK failure was retried");

    cam = FakeCamera();
    cam.interrupted = true;
    require(ToupCaptureWait::WaitForFrame(cam) == Result::Interrupted && cam.pulls == 0, "early interrupt ignored");

    cam = FakeCamera();
    cam.onPull = [](FakeCamera& c)
    {
        c.interrupted = true;
        return ToupCaptureWait::PullResult::Pending;
    };
    require(ToupCaptureWait::WaitForFrame(cam) == Result::Interrupted && cam.waits == 0,
            "interrupt between Pull and wait ignored");

    cam = FakeCamera();
    cam.onPull = [](FakeCamera&) { return ToupCaptureWait::PullResult::Pending; };
    cam.onWait = [](FakeCamera& c) { c.expired = true; };
    require(ToupCaptureWait::WaitForFrame(cam) == Result::Timeout && cam.waits == 1, "watchdog expiry ignored");
    require(!cam.locked, "timeout left lock held");
}

int main()
{
    try
    {
        queued_frame_does_not_need_a_new_event();
        pending_waits_for_frame();
        notification_during_pull_is_not_lost();
        spurious_wakeup_retries_without_claiming_a_frame();
        failures_and_cancellation_are_preserved();
        std::cout << "ToupTek capture wait regression tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ToupTek capture wait regression failed: " << error.what() << '\n';
        return 1;
    }
}
