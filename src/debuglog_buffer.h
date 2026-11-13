#ifndef PHD2_DEBUGLOG_BUFFER_H
#define PHD2_DEBUGLOG_BUFFER_H

#include <cstddef>
#include <cstdint>

namespace debuglog
{
// Synchronous stdio writes retain normal stream buffering. This policy bounds
// flush frequency and rotates between records; a single oversize record is kept
// intact. Api implements Write, Flush and Rotate, with no wxWidgets dependency.
class Buffer
{
    std::uint64_t m_size = 0;
    std::uint64_t m_pending = 0;
    std::uint64_t m_lastFlush = 0;
    std::uint64_t m_nextRotateAttempt = 0;
    const std::uint64_t m_maxSize;
    const std::uint64_t m_batchSize;
    const std::uint64_t m_intervalMs;

public:
    enum class Error
    {
        None,
        Rotate,
        Write,
        Flush
    };
    explicit Buffer(std::uint64_t maxSize = 10 * 1024 * 1024, std::uint64_t batchSize = 64 * 1024,
                    std::uint64_t intervalMs = 1000)
        : m_maxSize(maxSize), m_batchSize(batchSize), m_intervalMs(intervalMs)
    {
    }

    void Reset(std::uint64_t existingBytes, std::uint64_t now)
    {
        m_size = existingBytes;
        m_pending = 0;
        m_lastFlush = now;
        m_nextRotateAttempt = 0;
    }
    std::uint64_t Size() const { return m_size; }
    std::uint64_t Pending() const { return m_pending; }

    template<typename Api>
    bool Flush(Api& api, std::uint64_t now)
    {
        if (!api.Flush())
            return false;
        m_pending = 0;
        m_lastFlush = now;
        return true;
    }

    template<typename Api>
    bool FlushIfDue(Api& api, std::uint64_t now)
    {
        return m_pending == 0 || now - m_lastFlush < m_intervalMs || Flush(api, now);
    }

    template<typename Api>
    Error Write(Api& api, const char *data, size_t length, std::uint64_t now)
    {
        Error error = Error::None;
        if (m_size != 0 && (m_size >= m_maxSize || length > m_maxSize - m_size) && now >= m_nextRotateAttempt)
        {
            // Never switch away from a stream whose buffered tail failed to flush.
            if (!Flush(api, now))
                error = Error::Flush;
            else
            {
                std::uint64_t size = 0;
                if (api.Rotate(size))
                    Reset(size, now);
                else
                    error = Error::Rotate;
            }
            // On failure keep writing to the original file; don't lose diagnostics
            // or hammer the filesystem on every guide-loop log line.
            if (error != Error::None)
                m_nextRotateAttempt = now + m_intervalMs;
        }
        const size_t written = api.Write(data, length);
        m_size += written;
        m_pending += written;
        if (written != length)
            return Error::Write;
        if ((m_pending >= m_batchSize || now - m_lastFlush >= m_intervalMs) && !Flush(api, now))
            return Error::Flush;
        return error;
    }
};
}
#endif
