/*
 *  debuglog.cpp
 *  PHD Guiding
 *
 *  Created by Bret McKee
 *  Copyright (c) 2012 Bret McKee
 *  All rights reserved.
 *
 *  This source code is distributed under the following "BSD" license
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *    Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *    Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *    Neither the name of Bret McKee, Dad Dog Development,
 *     Craig Stark, Stark Labs nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "phd.h"

#include <wx/dir.h>
#include "debuglog_filename.h"
#include <chrono>
#include <cstdio>
#include <limits>
#ifdef __WINDOWS__
# include <io.h>
#endif

const int RetentionPeriod = 30;
static const size_t DebugBufferSize = 64 * 1024;

static std::uint64_t LogClockMillis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct DebugLogFiles
{
    DebugLog& log;
    size_t Write(const char *data, size_t size) { return log.wxFFile::Write(data, size); }
    bool Flush() { return log.wxFFile::Flush(); }
    bool Rotate(std::uint64_t& size)
    {
        if (log.m_part == std::numeric_limits<unsigned int>::max())
            return false;
        const unsigned int part = log.m_part + 1;
        const wxString path = wxString::FromUTF8(debuglog::MakePartName(log.m_path.ToUTF8().data(), part).c_str());
        wxFFile next;
        if (!next.Open(path, "ab"))
            return false;
        // Configure stdio before its first operation; otherwise its small default
        // buffer can still turn a batch of records into many filesystem writes.
        if (std::setvbuf(next.fp(), nullptr, _IOFBF, DebugBufferSize) != 0)
            log.ReportIOError("configure file buffer");
        const wxFileOffset length = next.Length();
        if (length < 0)
            return false;

        // The old tail is already flushed. Open the replacement before detaching
        // the old handle so an open failure leaves logging fully operational.
        FILE *old = log.Detach();
        log.Attach(next.Detach());
        if (old && std::fclose(old) != 0)
            log.ReportIOError("close rotated file");
        log.m_part = part;
        size = static_cast<std::uint64_t>(length);
        return true;
    }
};

DebugLog::DebugLog() : m_enabled(false), m_lastWriteTime(wxDateTime::UNow()), m_part(0), m_ioErrorReported(false) { }

DebugLog::~DebugLog()
{
    Close();
}

static bool ParseLogTimestamp(wxDateTime *p, const wxString& s)
{
    wxDateTime dt;
    wxString::const_iterator iter;
    if (dt.ParseFormat(s, "%Y-%m-%d_%H%M%S", wxDateTime(), &iter) && iter == s.end())
    {
        *p = dt;
        return true;
    }
    return false;
}

wxDateTime DebugLog::GetLogFileTime()
{
    // find the latest log file time
    wxDir dir(Debug.GetLogDir());
    wxString filename;
    // PHD2_DebugLog_YYYY-mm-dd_HHMMSS.txt
    bool cont = dir.GetFirst(&filename, "PHD2_DebugLog_*.txt", wxDIR_FILES);
    wxDateTime latest;
    while (cont)
    {
        wxDateTime dt;
        debuglog::FileName parsed;
        if (debuglog::ParseFileName(filename.ToUTF8().data(), parsed) &&
            ParseLogTimestamp(&dt, wxString::FromUTF8(parsed.timestamp.c_str())) &&
            (!latest.IsValid() || dt.IsLaterThan(latest)))
        {
            latest = dt;
        }
        cont = dir.GetNext(&filename);
    }

    wxDateTime res = wxDateTime::Now();
    if (latest.IsValid() && PhdApp::IsSameImagingDay(latest, res))
        res = latest;

    return res;
}

bool DebugLog::Enable(bool enable)
{
    wxCriticalSectionLocker lock(m_criticalSection);
    const bool prevState = m_enabled.load();
    if (!enable)
        FlushLocked();
    m_enabled.store(enable && IsOpened());
    return prevState;
}

void DebugLog::InitDebugLog(bool enable, bool forceOpen)
{
    wxCriticalSectionLocker lock(m_criticalSection);
    if (!enable)
    {
        if (forceOpen)
        {
            CloseLocked();
            m_path.clear(); // next enable must use the newly selected directory
        }
        else
            FlushLocked();
        m_enabled.store(false);
        return;
    }
    if (IsOpened() && !forceOpen)
    {
        m_enabled.store(true);
        return;
    }
    if (!CloseLocked())
        ReportIOError("close before reopening");
    if (m_path.IsEmpty() || forceOpen)
    {
        const wxDateTime& logFileTime = wxGetApp().GetLogFileTime();
        // Keep log files separated for multiple PHD2 instances
        wxString qualifier = wxGetApp().GetInstanceNumber() > 1 ? std::to_string(wxGetApp().GetInstanceNumber()) + "_" : "";
        m_path = GetLogDir() + PATHSEPSTR + "PHD2_DebugLog_" + qualifier + logFileTime.Format("%Y-%m-%d_%H%M%S.txt");
    }
    std::vector<std::string> names;
    wxDir dir(wxFileName(m_path).GetPath());
    wxString name;
    if (dir.IsOpened())
        for (bool more = dir.GetFirst(&name, "PHD2_DebugLog_*.txt", wxDIR_FILES); more; more = dir.GetNext(&name))
            names.push_back(name.ToUTF8().data());
    m_part = debuglog::LatestPart(wxFileName(m_path).GetFullName().ToUTF8().data(), names);
    const wxString path = wxString::FromUTF8(debuglog::MakePartName(m_path.ToUTF8().data(), m_part).c_str());
    if (!wxFFile::Open(path, "ab"))
    {
        m_enabled.store(false);
        ReportIOError("open");
        wxMessageBox(wxString::Format(_("unable to open file %s"), path));
        return;
    }
    if (std::setvbuf(fp(), nullptr, _IOFBF, DebugBufferSize) != 0)
        ReportIOError("configure file buffer");
    const wxFileOffset size = Length();
    if (size < 0)
    {
        ReportIOError("read file length");
        wxFFile::Close();
        m_enabled.store(false);
        return;
    }
    m_buffer.Reset(static_cast<std::uint64_t>(size), LogClockMillis());
    m_ioErrorReported = false;
    m_enabled.store(true);
}

bool DebugLog::ChangeDirLog(const wxString& newdir)
{
    bool enabled = IsEnabled();
    bool ok = true;

    if (!SetLogDir(newdir))
    {
        Debug.Write(wxString::Format("Error: unable to set new log dir %s\n", newdir));
        wxMessageBox(wxString::Format("invalid folder name %s, debug log folder unchanged", newdir));
        return false;
    }

    Debug.Write(wxString::Format("Changed log dir to %s\n", newdir));
    InitDebugLog(enabled, true);

    return ok;
}

void DebugLog::RemoveOldFiles()
{
    Logger::RemoveMatchingFiles("PHD2_DebugLog*.txt", RetentionPeriod);
}

wxString DebugLog::AddLine(const wxString& str)
{
    return Write(str + "\n");
}

wxString DebugLog::AddBytes(const wxString& str, const unsigned char *pBytes, unsigned int count)
{
    wxString Line = str + " - ";

    for (unsigned int i = 0; i < count; i++)
    {
        unsigned char ch = pBytes[i];
        Line += wxString::Format("%2.2X (%c) ", ch, isprint(ch) ? ch : '?');
    }

    return Write(Line + "\n");
}

void DebugLog::ReportIOError(const char *operation)
{
    if (m_ioErrorReported)
        return;
    m_ioErrorReported = true;
    // Do not call wxLog here: an active wxLog target may feed back into Debug.
    const wxString message = wxString::Format("PHD2 debug log: %s failed (%s, part %u)\n", operation, m_path, m_part);
    const wxScopedCharBuffer bytes = message.ToUTF8();
    std::fwrite(bytes.data(), 1, bytes.length(), stderr);
    std::fflush(stderr);
#ifdef __WINDOWS__
    OutputDebugString(message.c_str());
#endif
}

bool DebugLog::FlushLocked()
{
    if (!IsOpened())
        return true;
    DebugLogFiles files { *this };
    const bool ok = m_buffer.Flush(files, LogClockMillis());
    if (!ok)
        ReportIOError("flush");
    return ok;
}

bool DebugLog::CloseLocked()
{
    m_enabled.store(false);
    if (!IsOpened())
        return true;
    const bool flushed = FlushLocked();
    const bool closed = wxFFile::Close();
    if (!closed)
        ReportIOError("close");
    m_buffer.Reset(0, LogClockMillis());
    return flushed && closed;
}

bool DebugLog::Close()
{
    wxCriticalSectionLocker lock(m_criticalSection);
    return CloseLocked();
}

bool DebugLog::Flush()
{
    wxCriticalSectionLocker lock(m_criticalSection);
    return FlushLocked();
}

bool DebugLog::FlushIfDue()
{
    wxCriticalSectionLocker lock(m_criticalSection);
    if (!IsOpened())
        return true;
    DebugLogFiles files { *this };
    const bool ok = m_buffer.FlushIfDue(files, LogClockMillis());
    if (!ok)
        ReportIOError("timed flush");
    return ok;
}

bool DebugLog::FlushToDisk()
{
    wxCriticalSectionLocker lock(m_criticalSection);
    if (!FlushLocked())
        return false;
#ifdef __WINDOWS__
    if (IsOpened() && !FlushFileBuffers((HANDLE) _get_osfhandle(_fileno(fp()))))
    {
        ReportIOError("flush to disk");
        return false;
    }
#endif
    return true;
}

wxString DebugLog::Write(const wxString& str)
{
    if (m_enabled.load())
    {
        wxCriticalSectionLocker lock(m_criticalSection);
        if (!m_enabled.load() || !IsOpened())
            return str;

        wxDateTime now = wxDateTime::UNow();
        wxTimeSpan deltaTime = now - m_lastWriteTime;
        m_lastWriteTime = now;
        wxString outputLine = wxString::Format("%s %s %lu %s", now.Format("%H:%M:%S.%l"), deltaTime.Format("%S.%l"),
                                               (unsigned long) wxThread::GetCurrentId(), str);

        const wxScopedCharBuffer bytes = outputLine.ToUTF8();
        DebugLogFiles files { *this };
        const debuglog::Buffer::Error error = m_buffer.Write(files, bytes.data(), bytes.length(), LogClockMillis());
        if (error != debuglog::Buffer::Error::None)
        {
            const char *operation = error == debuglog::Buffer::Error::Rotate ? "rotate (continuing in current file)"
                : error == debuglog::Buffer::Error::Write                    ? "write"
                                                                             : "flush";
            ReportIOError(operation);
            if (error == debuglog::Buffer::Error::Write)
            {
                // Preserve a diagnostic copy when the log file cannot accept it.
                std::fwrite(bytes.data(), 1, bytes.length(), stderr);
                std::fflush(stderr);
            }
        }
        else
            m_ioErrorReported = false;
#if defined(__WINDOWS__) && defined(_DEBUG)
        OutputDebugString(outputLine.c_str());
#endif
    }

    return str;
}

DebugLog& operator<<(DebugLog& out, const wxString& str)
{
    out.Write(str);
    return out;
}

DebugLog& operator<<(DebugLog& out, const char *str)
{
    out.Write(str);
    return out;
}

DebugLog& operator<<(DebugLog& out, const int i)
{
    out.Write(wxString::Format(_T("%d"), i));
    return out;
}

DebugLog& operator<<(DebugLog& out, const double d)
{
    out.Write(wxString::Format(_T("%f"), d));
    return out;
}
