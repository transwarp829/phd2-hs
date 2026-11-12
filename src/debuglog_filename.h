#ifndef PHD2_DEBUGLOG_FILENAME_H
#define PHD2_DEBUGLOG_FILENAME_H

#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace debuglog
{
struct FileName
{
    std::string timestamp;
    unsigned int instance = 1;
    unsigned int part = 0;
    bool qualified = false;

    std::string SessionKey() const { return (qualified ? std::to_string(instance) + "_" : "") + timestamp; }
};

inline bool ParseNumber(const std::string& value, unsigned int& result)
{
    if (value.empty())
        return false;
    unsigned int n = 0;
    for (char c : value)
    {
        if (c < '0' || c > '9' || n > (std::numeric_limits<unsigned int>::max() - (c - '0')) / 10)
            return false;
        n = n * 10 + (c - '0');
    }
    result = n;
    return true;
}

inline bool ParseFileName(const std::string& name, FileName& result)
{
    const std::string prefix = "PHD2_DebugLog_";
    if (name.compare(0, prefix.size(), prefix) != 0 || name.size() < prefix.size() + 21 ||
        name.compare(name.size() - 4, 4, ".txt") != 0)
        return false;
    std::string body = name.substr(prefix.size(), name.size() - prefix.size() - 4);
    FileName parsed;
    const size_t suffix = body.find("_part");
    if (suffix != std::string::npos)
    {
        if (!ParseNumber(body.substr(suffix + 5), parsed.part) || parsed.part == 0)
            return false;
        body.resize(suffix);
    }
    if (body.size() > 17)
    {
        const size_t sep = body.size() - 18;
        if (body[sep] != '_' || !ParseNumber(body.substr(0, sep), parsed.instance) || parsed.instance == 0)
            return false;
        parsed.qualified = true;
        body.erase(0, sep + 1);
    }
    if (body.size() != 17)
        return false;
    for (size_t i = 0; i < body.size(); ++i)
    {
        if (i == 4 || i == 7 || i == 10)
        {
            if (body[i] != (i == 10 ? '_' : '-'))
                return false;
        }
        else if (body[i] < '0' || body[i] > '9')
            return false;
    }
    parsed.timestamp = body;
    result = parsed;
    return true;
}

inline std::string MakePartName(const std::string& basePath, unsigned int part)
{
    if (part == 0)
        return basePath;
    std::ostringstream suffix;
    suffix << "_part" << std::setfill('0') << std::setw(3) << part << ".txt";
    return basePath.substr(0, basePath.size() - 4) + suffix.str();
}

inline unsigned int LatestPart(const std::string& baseName, const std::vector<std::string>& names)
{
    FileName base;
    if (!ParseFileName(baseName, base))
        return 0;
    unsigned int latest = 0;
    for (const std::string& name : names)
    {
        FileName candidate;
        if (ParseFileName(name, candidate) && candidate.SessionKey() == base.SessionKey() && candidate.part > latest)
            latest = candidate.part;
    }
    return latest;
}
}
#endif
