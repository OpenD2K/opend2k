#pragma once
// A UTF-8 byte-order mark (EF BB BF) at the start of a text file. Editors on Windows (Notepad's "UTF-8 with
// BOM", PowerShell 5's Set-Content -Encoding UTF8) write one; without skipping it the first line's key reads
// as "\xEF\xBB\xBFkey" and is silently ignored (settings.ini lost its first setting that way).

#include <istream>
#include <string>
#include <string_view>

namespace d2k::formats {

inline constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

// Consumes a BOM at the stream's current position (call right after opening); anything else stays unread.
inline void skipUtf8Bom(std::istream& in) {
    if (!in) return;
    char b[3] = {};
    const std::streampos start = in.tellg();
    in.read(b, 3);
    if (in.gcount() == 3 && std::string_view(b, 3) == kUtf8Bom) return;
    in.clear();
    if (start != std::streampos(-1)) {
        in.seekg(start);
    } else {  // not seekable: put back what was read
        for (std::streamsize i = in.gcount(); i-- > 0;) in.putback(b[i]);
    }
}

// The text without a leading BOM.
inline std::string_view stripUtf8Bom(std::string_view s) {
    return s.substr(0, kUtf8Bom.size()) == kUtf8Bom ? s.substr(kUtf8Bom.size()) : s;
}

}  // namespace d2k::formats
