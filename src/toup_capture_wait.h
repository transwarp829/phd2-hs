#ifndef TOUP_CAPTURE_WAIT_H_INCLUDED
#define TOUP_CAPTURE_WAIT_H_INCLUDED

namespace ToupCaptureWait
{
enum class PullResult
{
    Ready,
    Pending,
    Failed
};
enum class Result
{
    Ready,
    CameraError,
    PullError,
    Interrupted,
    Timeout
};

template<class Backend>
class LockGuard
{
    Backend& m_backend;

public:
    explicit LockGuard(Backend& backend) : m_backend(backend) { m_backend.Lock(); }
    ~LockGuard() { m_backend.Unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

// Backend::Wait must atomically release/reacquire the same lock used by the
// camera callback. Every callback increments Generation while holding that lock.
template<class Backend>
Result WaitForFrame(Backend& backend)
{
    for (;;)
    {
        unsigned long long generation;
        {
            LockGuard<Backend> lock(backend);
            if (backend.HasError())
                return Result::CameraError;
            if (backend.Interrupted())
                return Result::Interrupted;
            if (backend.Expired())
                return Result::Timeout;
            generation = backend.Generation();
        }

        // Never hold the callback lock while entering the camera SDK. In video
        // mode a full non-realtime queue stops producing events until we pull.
        switch (backend.Pull())
        {
        case PullResult::Ready:
            return Result::Ready;
        case PullResult::Failed:
            return Result::PullError;
        case PullResult::Pending:
            break;
        }

        {
            LockGuard<Backend> lock(backend);
            if (backend.HasError())
                return Result::CameraError;
            if (backend.Interrupted())
                return Result::Interrupted;
            if (backend.Expired())
                return Result::Timeout;
            // An event between the first snapshot and Pull returning E_PENDING
            // must cause another pull, not a wait for an additional event.
            if (backend.Generation() == generation)
                backend.Wait();
        }
    }
}
} // namespace ToupCaptureWait

#endif
