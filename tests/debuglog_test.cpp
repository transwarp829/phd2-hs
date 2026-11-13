#include "debuglog_buffer.h"
#include "debuglog_filename.h"
#include "debuglog_record.h"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void Require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

struct MemoryFiles
{
    std::vector<std::string> parts { "" };
    std::vector<std::string> persisted { "" };
    std::vector<std::string> operations;
    bool failFlush = false;
    bool failRotate = false;
    bool shortWrite = false;
    unsigned flushes = 0;
    unsigned rotations = 0;

    size_t Write(const char *data, size_t size)
    {
        operations.push_back("write");
        const size_t written = shortWrite && size ? size - 1 : size;
        parts.back().append(data, written);
        return written;
    }
    bool Flush()
    {
        operations.push_back("flush");
        ++flushes;
        if (failFlush)
            return false;
        persisted.back() = parts.back();
        return true;
    }
    bool Rotate(std::uint64_t& size)
    {
        operations.push_back("rotate");
        ++rotations;
        if (failRotate)
            return false;
        parts.push_back("");
        persisted.push_back("");
        size = 0;
        return true;
    }
};

static debuglog::Buffer::Error Write(debuglog::Buffer& buffer, MemoryFiles& files,
                                    const std::string& text, std::uint64_t time)
{
    return buffer.Write(files, text.data(), text.size(), time);
}

static void BatchAndIdleTail()
{
    debuglog::Buffer buffer(100, 8, 1000);
    MemoryFiles files;
    buffer.Reset(0, 0);
    Require(Write(buffer, files, "abc", 100) == debuglog::Buffer::Error::None, "initial write");
    Require(files.flushes == 0 && buffer.Pending() == 3, "small writes must remain batched");
    Require(buffer.FlushIfDue(files, 999) && files.flushes == 0, "must wait for time threshold");
    Require(buffer.FlushIfDue(files, 1000), "idle-tail flush");
    Require(files.persisted.back() == "abc" && buffer.Pending() == 0, "timer must flush without another write");
    Require(buffer.FlushIfDue(files, 5000) && files.flushes == 1, "idle clean file needs no more flushes");
    Require(Write(buffer, files, "12345678", 5001) == debuglog::Buffer::Error::None, "batch threshold write");
    Require(files.flushes == 2 && files.persisted.back() == "abc12345678", "byte threshold must flush");
    Require(Write(buffer, files, "tail", 5100) == debuglog::Buffer::Error::None, "tail write");
    Require(buffer.Flush(files, 5101), "explicit lifecycle flush");
    Require(files.persisted.back() == "abc12345678tail", "explicit disable/close/switch flush must retain tail");
}

static void RotationBoundariesAndExistingFiles()
{
    debuglog::Buffer buffer(10, 100, 1000);
    MemoryFiles files;
    files.parts[0] = files.persisted[0] = "existing";
    buffer.Reset(8, 0);
    Write(buffer, files, "12", 1);
    Require(files.rotations == 0 && buffer.Size() == 10, "exact limit must fit existing file");
    files.operations.clear();
    Require(Write(buffer, files, "3", 2) == debuglog::Buffer::Error::None, "rollover write");
    Require(files.operations == std::vector<std::string>({"flush", "rotate", "write"}), "flush before opening next part");
    Require(files.persisted[0] == "existing12" && files.parts[1] == "3", "old file must remain intact");
    Require(buffer.Size() == 1 && buffer.Pending() == 1, "new part counters");

    MemoryFiles old;
    old.parts[0] = old.persisted[0] = "already oversized";
    buffer.Reset(old.parts[0].size(), 0);
    Write(buffer, old, "next", 1);
    Require(old.parts.size() == 2 && old.parts[0] == "already oversized", "oversize legacy file must roll before append");
}

static void Utf8BytesAndOversizeRecords()
{
    debuglog::Buffer buffer(10, 6, 1000);
    MemoryFiles files;
    buffer.Reset(0, 0);
    const std::string chinese = "\xe5\xaf\xbc\xe6\x98\x9f"; // two UTF-8 characters, six bytes
    Write(buffer, files, chinese, 1);
    Require(buffer.Size() == 6 && files.flushes == 1, "UTF-8 size must count bytes, not characters");
    Write(buffer, files, chinese, 2);
    Require(files.parts.size() == 2 && files.parts[0] == chinese && files.parts[1] == chinese,
            "UTF-8 records must rotate intact");

    MemoryFiles large;
    buffer.Reset(0, 0);
    const std::string record(25, 'x');
    Write(buffer, large, record, 1);
    Require(large.rotations == 0 && large.parts[0] == record, "oversize record must not create empty parts or split");
    Write(buffer, large, "y", 2);
    Require(large.parts.size() == 2 && large.parts[0] == record && large.parts[1] == "y", "next record leaves oversized part");
}

static void FailuresKeepDiagnosticsAndRetry()
{
    debuglog::Buffer buffer(10, 100, 1000);
    MemoryFiles files;
    buffer.Reset(0, 0);
    Write(buffer, files, "1234567890", 1);
    files.failRotate = true;
    Require(Write(buffer, files, "failure", 2) == debuglog::Buffer::Error::Rotate, "rotation failure must be reported");
    Require(files.parts.size() == 1 && files.parts[0] == "1234567890failure", "failed create must append original file");
    Write(buffer, files, "tail", 3);
    Require(files.rotations == 1, "failed rotation must not be retried for every log line");
    files.failRotate = false;
    Write(buffer, files, "recovered", 1002);
    Require(files.parts.size() == 2 && files.persisted[0] == "1234567890failuretail", "retry must retain original tail");

    MemoryFiles flushFailure;
    buffer.Reset(0, 0);
    Write(buffer, flushFailure, "1234567890", 1);
    flushFailure.failFlush = true;
    Require(Write(buffer, flushFailure, "a", 2) == debuglog::Buffer::Error::Flush, "flush failure must be reported");
    Require(flushFailure.rotations == 0 && buffer.Pending() == 11, "failed flush must preserve pending tail and original handle");
    Require(buffer.FlushIfDue(flushFailure, 1000), "timer must wait out the flush backoff");
    Require(flushFailure.flushes == 1, "no flush attempt inside the backoff window");
    Require(!buffer.FlushIfDue(flushFailure, 1003), "timer must surface failed flush after the backoff");
    flushFailure.failFlush = false;
    Require(buffer.Flush(flushFailure, 1004) && buffer.Pending() == 0, "later explicit flush can recover");
    Require(flushFailure.persisted[0] == "1234567890a", "failed flush must not lose records");

    MemoryFiles shortFile;
    buffer.Reset(0, 0);
    shortFile.shortWrite = true;
    Require(Write(buffer, shortFile, "four", 1) == debuglog::Buffer::Error::Write, "partial write must report failure");
    Require(buffer.Size() == 3 && buffer.Pending() == 3, "partial write counts must reflect actual bytes");
}

static void LineEndingsMatchUpstreamTextMode()
{
    const std::string lf = "one\ntwo\n";
    Require(debuglog::ExpandLineEndings(lf.data(), lf.size()) == "one\r\ntwo\r\n", "LF must become CRLF");
    const std::string mixed = "a\r\nb\n";
    Require(debuglog::ExpandLineEndings(mixed.data(), mixed.size()) == "a\r\r\nb\r\n",
            "every LF is translated exactly like the old text-mode stream");
    Require(debuglog::ExpandLineEndings("", 0).empty(), "empty record");
}

static void FlushFailureBacksOff()
{
    debuglog::Buffer buffer(1000, 8, 1000);
    MemoryFiles files;
    buffer.Reset(0, 0);
    Require(Write(buffer, files, "abc", 1) == debuglog::Buffer::Error::None, "write");
    files.failFlush = true;
    Require(!buffer.FlushIfDue(files, 1000), "first due flush fails");
    const unsigned failed = files.flushes;
    Require(Write(buffer, files, "123456789", 1100) == debuglog::Buffer::Error::None, "batched write while backing off");
    Require(files.flushes == failed, "write path must not retry inside the backoff window");
    Require(!buffer.FlushIfDue(files, 2100), "second attempt after the first backoff fails");
    const unsigned failed2 = files.flushes;
    Require(files.flushes == failed + 1, "backoff allowed exactly one retry");
    Require(buffer.FlushIfDue(files, 2500) && files.flushes == failed2, "doubled backoff defers the next attempt");
    files.failFlush = false;
    Require(buffer.Flush(files, 10000), "explicit flush retries immediately");
}

static void NamesAndRestartSelection()
{
    debuglog::FileName parsed;
    const std::string base = "PHD2_DebugLog_2026-11-13_015959.txt";
    Require(debuglog::ParseFileName(base, parsed), "legacy filename must parse");
    Require(parsed.timestamp == "2026-11-13_015959" && parsed.instance == 1 && !parsed.qualified && parsed.part == 0,
            "legacy timestamp and instance must remain compatible");
    Require(debuglog::ParseFileName("PHD2_DebugLog_2_2026-11-13_015959_part003.txt", parsed), "instance part filename");
    Require(parsed.instance == 2 && parsed.part == 3 && parsed.SessionKey() == "2_2026-11-13_015959", "pairing key keeps instance");
    Require(debuglog::MakePartName(base, 0) == base, "base part keeps original name");
    Require(debuglog::MakePartName(base, 1) == "PHD2_DebugLog_2026-11-13_015959_part001.txt", "first part name");
    Require(debuglog::MakePartName(base, 1000) == "PHD2_DebugLog_2026-11-13_015959_part1000.txt", "large part index");
    const std::vector<std::string> names = { base, debuglog::MakePartName(base, 2), debuglog::MakePartName(base, 10),
        "PHD2_DebugLog_2_2026-11-13_015959_part999.txt", "PHD2_DebugLog_2026-11-12_015959_part100.txt" };
    Require(debuglog::LatestPart(base, names) == 10, "restart must choose highest matching part despite gaps");
    Require(debuglog::LatestPart(base, {}) == 0, "new or empty session starts with base file");
    for (const std::string& invalid : { "PHD2_DebugLog_2026-11-13_015959_part0.txt",
            "PHD2_DebugLog_2026-11-13_015959_part-1.txt", "PHD2_DebugLog_2026-11-13_015959_part9999999999999.txt",
            "PHD2_DebugLog_0_2026-11-13_015959.txt", "PHD2_DebugLog_2026-11-13_015959_backup.txt",
            "PHD2_GuideLog_2026-11-13_015959.txt" })
        Require(!debuglog::ParseFileName(invalid, parsed), "invalid debug filename must be ignored");
}

struct StdioFiles
{
    std::vector<FILE *> parts;
    StdioFiles() { parts.push_back(std::tmpfile()); Require(parts.back() != nullptr, "create temporary log"); }
    ~StdioFiles() { for (FILE *part : parts) std::fclose(part); }
    size_t Write(const char *data, size_t size) { return std::fwrite(data, 1, size, parts.back()); }
    bool Flush() { return std::fflush(parts.back()) == 0; }
    bool Rotate(std::uint64_t& size)
    {
        FILE *next = std::tmpfile();
        if (!next) return false;
        parts.push_back(next);
        size = 0;
        return true;
    }
    std::string Read(size_t index)
    {
        std::rewind(parts[index]);
        char bytes[128];
        const size_t count = std::fread(bytes, 1, sizeof bytes, parts[index]);
        return std::string(bytes, count);
    }
};

static void RealFileWrites()
{
    StdioFiles files;
    debuglog::Buffer buffer(10, 8, 1000);
    buffer.Reset(0, 0);
    const std::string legacy = "old\r\n";
    Require(buffer.Write(files, legacy.data(), legacy.size(), 1) == debuglog::Buffer::Error::None, "real old log write");
    Require(buffer.Flush(files, 2), "real flush before reopening existing state");
    buffer.Reset(legacy.size(), 3); // reopen an existing nonempty log
    const std::string next = "next\n";
    buffer.Write(files, next.data(), next.size(), 4);
    buffer.Write(files, "tail\n", 5, 5);
    Require(files.parts.size() == 2, "actual files must rotate");
    Require(buffer.FlushIfDue(files, 1005), "real idle tail flush");
    Require(files.Read(0) == "old\r\nnext\n" && files.Read(1) == "tail\n", "actual file contents must preserve mixed old/new lines");
}

int main()
{
    try
    {
        BatchAndIdleTail();
        RotationBoundariesAndExistingFiles();
        Utf8BytesAndOversizeRecords();
        FailuresKeepDiagnosticsAndRetry();
        LineEndingsMatchUpstreamTextMode();
        FlushFailureBacksOff();
        NamesAndRestartSelection();
        RealFileWrites();
        std::cout << "Debug log regression tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
