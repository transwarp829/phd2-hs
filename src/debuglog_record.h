#ifndef PHD2_DEBUGLOG_RECORD_H
#define PHD2_DEBUGLOG_RECORD_H

#include <cstddef>
#include <string>

namespace debuglog
{
// PHD2 always wrote the debug log through wxFFile in text mode: the bytes were
// UTF-8 (wxConvAuto's default for wide-to-multibyte conversion) while the CRT
// turned every '\n' into CRLF. Keep that on-disk format so existing log viewers
// and older PHD2 versions see the same file; the buffered writer itself is
// binary, so the translation happens here.
inline std::string ExpandLineEndings(const char *data, size_t length)
{
    std::string out;
    out.reserve(length + length / 8);
    for (size_t i = 0; i < length; ++i)
    {
        if (data[i] == '\n')
            out += "\r\n";
        else
            out.push_back(data[i]);
    }
    return out;
}
} // namespace debuglog

#endif
