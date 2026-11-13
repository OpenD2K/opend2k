#include "platform/catalog.hpp"

#include "platform/language.hpp"

#include "formats/io.hpp"
#include "formats/language.hpp"
#include "formats/mission_ini.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>

namespace d2k::platform {

const char* const kHouseNames[3] = {"Atreides", "Harkonnen", "Ordos"};

namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}


using formats::IniFile;
using formats::iniGet;
using formats::readIni;

// Text.UIB uses '¬' (0xAC in Latin-1) as a line break; Gruntmods inline briefings use "__".
std::string cleanText(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xAC) out += '\n';
        else if (c == '_' && i + 1 < s.size() && s[i + 1] == '_') { out += "\n\n"; ++i; }
        else out += static_cast<char>(c);
    }
    while (!out.empty() && (out.front() == '\n' || out.front() == ' ')) out.erase(out.begin());
    return out;
}

// Case-insensitive compare with digit runs compared as numbers ("Mission 2" < "Mission 10").
bool naturalLess(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            std::size_t i2 = i, j2 = j;
            while (i2 < a.size() && std::isdigit(static_cast<unsigned char>(a[i2]))) ++i2;
            while (j2 < b.size() && std::isdigit(static_cast<unsigned char>(b[j2]))) ++j2;
            const long na = std::atol(a.substr(i, i2 - i).c_str()), nb = std::atol(b.substr(j, j2 - j).c_str());
            if (na != nb) return na < nb;
            i = i2;
            j = j2;
            continue;
        }
        const int la = std::tolower(ca), lb = std::tolower(cb);
        if (la != lb) return la < lb;
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

bool isCampaignStem(const std::string& stem) {
    // [AHO]<digit>V<digit>
    return stem.size() == 4 && (stem[0] == 'A' || stem[0] == 'H' || stem[0] == 'O') && std::isdigit((unsigned char)stem[1]) &&
           (stem[2] == 'V' || stem[2] == 'v') && std::isdigit((unsigned char)stem[3]);
}

}  // namespace

Catalog Catalog::load(const std::string& dataDir) {
    Catalog c;
    using formats::findFileNoCase;
    // Text.UIB with the active language's values (platform/language.hpp).
    try { c.text = loadGameText(dataDir); } catch (const std::exception& e) { SDL_Log("Text.UIB: %s", e.what()); }
    const std::string missions = dataDir + "/Missions", maps = dataDir + "/maps";

    // Index the folders once (case-insensitive lookups).
    std::map<std::string, std::string> missionFiles, mapFiles;
    std::map<std::string, formats::StringTable> uibCache;  // mission TextUib tables
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(missions, ec)) missionFiles[lower(e.path().filename().string())] = e.path().filename().string();
    for (const auto& e : fs::directory_iterator(maps, ec)) mapFiles[lower(e.path().filename().string())] = e.path().filename().string();

    for (const auto& [lname, fname] : missionFiles) {
        if (lname.size() < 5 || lname.substr(lname.size() - 4) != ".ini") continue;
        const std::string stem = fname.substr(0, fname.size() - 4);
        const std::string lstem = lower(stem);
        auto mapIt = missionFiles.find(lstem + ".map");
        if (mapIt == missionFiles.end()) continue;
        const bool hasMis = missionFiles.count(lstem + ".mis") || missionFiles.count("_" + lstem + ".mis") ||
                            mapFiles.count("_" + lstem + ".mis");
        if (!hasMis) continue;
        const formats::MissionIni mi = formats::loadMissionIni(missions + "/" + fname);
        MissionInfo m;
        m.map = mapIt->second;
        m.stem = stem;
        // Catalog text is UTF-8: each string converted from its own file's code page (a
        // community .ini is Latin unless it is UTF-8 or Russian), drawn right by the Play font
        // and, converted to the active code page, by the bitmap fonts (render/text.cpp).
        const int iniCp = mi.codepage;
        const int gameCp = formats::lang::activeCodepage();  // c.text (the language's table)
        m.name = formats::lang::sourceToUtf8(mi.name, iniCp);
        if (m.name.empty()) m.name = stem;
        m.author = formats::lang::sourceToUtf8(mi.author, iniCp);
        m.side = std::max(mi.sideId, 0) & 7;
        m.number = mi.missionNumber;
        m.campaignFolder = mi.campaignFolder;
        m.modsFolder = mi.modsFolder;
        m.intelId = mi.intelId;
        // Briefing: Briefing= wins (the launcher ignores the key then), else TextUibBriefingKey
        // in the mission's TextUib (mods folder UI_DATA first) or Text.UIB.
        if (!mi.briefing.empty()) m.briefing = formats::lang::sourceToUtf8(cleanText(formats::missionBriefingText(mi.briefing)), iniCp);
        if (m.briefing.empty() && !mi.textUibBriefingKey.empty()) {
            const formats::StringTable* table = &c.text;
            int tableCp = gameCp;
            if (!mi.textUib.empty()) {
                const std::string key = lower(mi.campaignFolder + "/" + mi.modsFolder + "/" + mi.textUib);
                auto it = uibCache.find(key);
                if (it == uibCache.end()) {
                    formats::StringTable t;
                    const std::string p = formats::findOverlayFile(formats::missionModsDataDir(dataDir, mi), dataDir, "UI_DATA", mi.textUib);
                    if (!p.empty()) {
                        try { t = formats::loadUib(p); } catch (const std::exception& e) { SDL_Log("%s: %s", p.c_str(), e.what()); }
                    }
                    it = uibCache.emplace(key, std::move(t)).first;
                }
                if (!it->second.entries.empty()) {
                    table = &it->second;
                    tableCp = formats::lang::detectCodepage(*table);
                }
            }
            if (const std::string* t = table->find(mi.textUibBriefingKey)) m.briefing = formats::lang::sourceToUtf8(cleanText(*t), tableCp);
            else if (const std::string* t2 = c.text.find(mi.textUibBriefingKey))
                m.briefing = formats::lang::sourceToUtf8(cleanText(*t2), gameCp);
        }
        // Intel entries (MissionLauncher: CustomCampaignData/<folder>/Intel/Entries/<id>_<n>.txt).
        if (!mi.intelId.empty()) {
            const std::string dir = formats::customCampaignDir(dataDir, mi.campaignFolder);
            const std::string intel = dir.empty() ? std::string() : formats::findFileNoCase(dir, "Intel");
            const std::string entries = intel.empty() ? std::string() : formats::findFileNoCase(intel, "Entries");
            for (int k = 1; k <= 4 && !entries.empty(); ++k) {
                const std::string p = formats::findFileNoCase(entries, mi.intelId + "_" + std::to_string(k) + ".txt");
                if (p.empty()) continue;
                try {
                    const auto bytes = formats::readFile(p);
                    std::string t(bytes.begin(), bytes.end());
                    std::string clean;
                    for (char ch : t)
                        if (ch != '\r') clean += ch;
                    if (!clean.empty()) m.intel.push_back(formats::lang::sourceToUtf8(clean, formats::lang::detectCodepage(clean)));
                } catch (const std::exception&) {}
            }
        }
        // Extended .MIS (1.06p+ event system): more than the original 68066 bytes.
        {
            std::string misName = missionFiles.count(lstem + ".mis") ? missionFiles[lstem + ".mis"] : std::string();
            if (misName.empty() && missionFiles.count("_" + lstem + ".mis")) misName = missionFiles["_" + lstem + ".mis"];
            std::error_code fec;
            const auto size = misName.empty() ? 0 : fs::file_size(fs::path(missions) / misName, fec);
            if (!fec && size >= 68066 + 4) {
                m.extendedMis = true;
                std::ifstream in(fs::path(missions) / misName, std::ios::binary);
                in.seekg(68066);
                unsigned char b[2] = {0, 0};
                in.read(reinterpret_cast<char*>(b), 2);
                m.extendedEvents = b[0] | (b[1] << 8);
            }
        }
        const auto dash = m.name.rfind(" - ");
        m.shortName = dash == std::string::npos ? m.name : m.name.substr(dash + 3);
        std::string ustem = stem;
        for (auto& ch : ustem) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (isCampaignStem(ustem)) {
            m.house = ustem[0] == 'A' ? 0 : ustem[0] == 'H' ? 1 : 2;
            // "Atreides Mission 1a" -> "Mission 1a"
            if (const auto at = m.shortName.find("Mission"); at != std::string::npos) m.shortName = m.shortName.substr(at);
            c.campaign[m.house].push_back(std::move(m));
        } else {
            m.house = m.side <= 2 ? m.side : -1;
            c.custom.push_back(std::move(m));
        }
    }
    for (auto& list : c.campaign)
        std::sort(list.begin(), list.end(), [](const MissionInfo& a, const MissionInfo& b) { return lower(a.map) < lower(b.map); });
    std::sort(c.custom.begin(), c.custom.end(), [](const MissionInfo& a, const MissionInfo& b) { return naturalLess(a.name, b.name); });
    {
        std::vector<formats::CommunityMissionRef> refs;
        refs.reserve(c.custom.size());
        for (const auto& m : c.custom) refs.push_back({m.stem, m.name, m.author, m.side, m.campaignFolder});
        c.community = formats::buildCommunityCampaigns(refs, dataDir);
    }

    const auto mp = readIni(findFileNoCase(maps, "MultiplayerMaps.ini"));
    if (auto it = mp.find("maps"); it != mp.end()) {
        for (const auto& [stem, name] : it->second) {
            auto f = mapFiles.find(lower(stem) + ".map");
            if (f == mapFiles.end()) continue;
            SkirmishMap m;
            m.stem = stem;
            m.name = formats::lang::sourceToUtf8(name, formats::lang::detectCodepage(name));
            m.file = f->second;
            // "(MED 2-4 player)" -> 4; else the digit in "<n>PLAY" (0 = unknown -> 8).
            m.maxPlayers = std::isdigit((unsigned char)stem[0]) ? stem[0] - '0' : 0;
            if (const auto p = name.find(" player"); p != std::string::npos) {
                std::size_t q = p;
                while (q > 0 && std::isdigit((unsigned char)name[q - 1])) --q;
                if (q < p) m.maxPlayers = std::atoi(name.c_str() + q);
            }
            if (m.maxPlayers < 2) m.maxPlayers = 8;
            c.skirmish.push_back(std::move(m));
        }
    }
    SDL_Log("Catalog: %zu/%zu/%zu campaign missions, %zu custom missions, %zu community campaigns, %zu skirmish maps",
            c.campaign[0].size(), c.campaign[1].size(), c.campaign[2].size(), c.custom.size(), c.community.size(),
            c.skirmish.size());
    return c;
}

}  // namespace d2k::platform
