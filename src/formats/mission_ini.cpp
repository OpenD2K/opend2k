#include "formats/mission_ini.hpp"

#include "formats/io.hpp"
#include "formats/language.hpp"

#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace d2k::formats {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// A sub folder / file of `dir` by name, ignoring case; "" if missing.
std::string child(const std::string& dir, const std::string& name) {
    if (dir.empty() || name.empty()) return {};
    return findFileNoCase(dir, name);
}

}  // namespace

IniFile parseIni(const std::string& text) {
    IniFile out;
    std::string cur;
    std::size_t i = 0;
    while (i <= text.size()) {
        std::size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string l = trim(text.substr(i, e - i));
        i = e + 1;
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l.front() == '[' && l.back() == ']') {
            cur = lower(trim(l.substr(1, l.size() - 2)));
            out[cur];
            continue;
        }
        const auto eq = l.find('=');
        if (eq == std::string::npos) continue;
        out[cur].push_back({trim(l.substr(0, eq)), trim(l.substr(eq + 1))});
    }
    return out;
}

IniFile readIni(const std::string& path) {
    if (path.empty()) return {};
    try {
        const auto bytes = readFile(path);
        return parseIni(std::string(bytes.begin(), bytes.end()));
    } catch (const std::exception&) {
        return {};
    }
}

const IniSection* iniSection(const IniFile& ini, const std::string& section) {
    auto it = ini.find(lower(section));
    return it == ini.end() ? nullptr : &it->second;
}

std::string iniGet(const IniFile& ini, const std::string& section, const std::string& key, const std::string& def) {
    if (const IniSection* s = iniSection(ini, section)) {
        const std::string k = lower(key);
        for (const auto& [kk, v] : *s)
            if (lower(kk) == k) return v;  // first one wins (GetPrivateProfileString)
    }
    return def;
}

MissionIni parseMissionIni(const IniFile& ini) {
    MissionIni m;
    m.present = true;
    m.name = iniGet(ini, "Basic", "Name");
    m.author = iniGet(ini, "Basic", "Author");
    m.music = iniGet(ini, "Basic", "Music");
    if (const std::string s = iniGet(ini, "Basic", "SideId"); !s.empty()) m.sideId = std::atoi(s.c_str());
    m.missionNumber = std::atoi(iniGet(ini, "Basic", "MissionNumber").c_str());
    m.textUib = iniGet(ini, "Basic", "TextUib");
    m.textUibBriefingKey = iniGet(ini, "Basic", "TextUibBriefingKey");
    m.briefing = iniGet(ini, "Basic", "Briefing");
    m.campaignFolder = iniGet(ini, "Data", "CampaignFolder");
    m.modsFolder = iniGet(ini, "Data", "ModsFolder");
    m.coloursFile = iniGet(ini, "Data", "ColoursFile");
    m.playersFile = iniGet(ini, "Data", "PlayersFile");
    m.intelId = iniGet(ini, "Data", "IntelId");
    if (const IniSection* s = iniSection(ini, "Vars")) m.vars = *s;
    if (const IniSection* s = iniSection(ini, "Text")) {
        for (const auto& [k, v] : *s) {
            if (k.empty() || !std::isdigit(static_cast<unsigned char>(k[0]))) continue;
            m.text.emplace(std::atoi(k.c_str()), v);  // first one wins
        }
    }
    if (const IniSection* s = iniSection(ini, "Sides")) {
        for (const auto& [k, v] : *s) {
            const int i = k.size() == 1 && std::isdigit(static_cast<unsigned char>(k[0])) ? k[0] - '0' : -1;
            if (i >= 0 && i < 8 && m.sides[std::size_t(i)].empty()) m.sides[std::size_t(i)] = v;
        }
    }
    // The code page of the text values (keys and numbers are ASCII and would outweigh them).
    std::string all = m.name + ' ' + m.author + ' ' + m.briefing;
    for (const auto& [k, v] : m.text) all += ' ' + v;
    for (const auto& v : m.sides) all += ' ' + v;
    m.codepage = lang::detectCodepage(all);
    return m;
}

MissionIni loadMissionIni(const std::string& path) {
    if (path.empty()) return {};
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return {};
    return parseMissionIni(readIni(path));
}

std::string missionBriefingText(const std::string& raw) {
    std::string out = raw;
    for (auto& c : out)
        if (c == '_') c = '\n';
    while (!out.empty() && (out.front() == '\n' || out.front() == ' ')) out.erase(out.begin());
    return out;
}

std::string customCampaignDir(const std::string& dataDir, const std::string& campaignFolder) {
    if (dataDir.empty() || campaignFolder.empty()) return {};
    std::filesystem::path d(dataDir);
    if (!d.has_filename()) d = d.parent_path();  // "…/data/"
    const std::string root = d.parent_path().string();
    return child(child(root, "CustomCampaignData"), campaignFolder);
}

std::string missionModsDataDir(const std::string& dataDir, const MissionIni& ini) {
    if (ini.modsFolder.empty()) return {};
    return child(child(customCampaignDir(dataDir, ini.campaignFolder), ini.modsFolder), "data");
}

std::string findOverlayFile(const std::string& modsData, const std::string& dataDir, const std::string& sub,
                            const std::string& name) {
    auto look = [&](const std::string& root) -> std::string {
        if (root.empty()) return {};
        const std::string dir = sub.empty() ? root : child(root, sub);
        return dir.empty() ? std::string() : findFileNoCase(dir, name);
    };
    if (std::string p = look(modsData); !p.empty()) return p;
    return look(dataDir);
}

std::array<std::string, 8> loadPlayersFile(const std::string& path) {
    std::array<std::string, 8> names{};
    const IniFile ini = readIni(path);
    std::string all;
    for (int i = 0; i < 8; ++i) {
        names[std::size_t(i)] = iniGet(ini, "Player" + std::to_string(i), "name");
        all += names[std::size_t(i)] + ' ';
    }
    const int cp = lang::detectCodepage(all);
    for (auto& n : names) n = lang::sourceToUtf8(n, cp);
    return names;
}

}  // namespace d2k::formats
