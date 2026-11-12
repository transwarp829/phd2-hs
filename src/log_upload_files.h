#ifndef LOG_UPLOAD_FILES_H_INCLUDED
#define LOG_UPLOAD_FILES_H_INCLUDED

#include "debuglog_filename.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace log_upload
{
struct SessionFiles
{
    debuglog::FileName session;
    std::string guide;
    std::vector<std::string> debug;
};

inline bool ParseGuideFileName(const std::string& name, debuglog::FileName& result)
{
    const std::string prefix = "PHD2_GuideLog_";
    return name.compare(0, prefix.size(), prefix) == 0 &&
        debuglog::ParseFileName("PHD2_DebugLog_" + name.substr(prefix.size()), result) && result.part == 0;
}

// Names are basenames, never filesystem paths. Keep the actual names so upload
// ZIP entries retain instance qualifiers and every available debug-log volume.
inline std::map<std::string, SessionFiles> GroupFiles(const std::vector<std::string>& names)
{
    std::map<std::string, SessionFiles> sessions;
    for (const auto& name : names)
    {
        debuglog::FileName parsed;
        const bool isDebug = debuglog::ParseFileName(name, parsed);
        if (!isDebug && !ParseGuideFileName(name, parsed))
            continue;
        SessionFiles& files = sessions[parsed.SessionKey()];
        files.session = parsed;
        if (isDebug)
            files.debug.push_back(name);
        else
            files.guide = name;
    }
    for (auto& entry : sessions)
    {
        auto& debug = entry.second.debug;
        std::sort(debug.begin(), debug.end(),
                  [](const std::string& a, const std::string& b)
                  {
                      debuglog::FileName first, second;
                      debuglog::ParseFileName(a, first);
                      debuglog::ParseFileName(b, second);
                      return first.part != second.part ? first.part < second.part : a < b;
                  });
        debug.erase(std::unique(debug.begin(), debug.end()), debug.end());
    }
    return sessions;
}

// Re-scan at upload time: the active log can rotate while the dialog is open.
// Retain previously selected names too, so a file removed since selection is
// reported as an upload error instead of silently uploading an incomplete set.
inline std::vector<std::string> DebugFilesForUpload(const std::string& sessionKey, const std::vector<std::string>& selected,
                                                    const std::vector<std::string>& current)
{
    std::vector<std::string> names(selected);
    names.insert(names.end(), current.begin(), current.end());
    auto sessions = GroupFiles(names);
    auto found = sessions.find(sessionKey);
    return found == sessions.end() ? std::vector<std::string>() : found->second.debug;
}
} // namespace log_upload

#endif
