#include "formats/community.hpp"

#include "formats/io.hpp"
#include "formats/mission_ini.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <tuple>

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

bool startsWithNoCase(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && lower(s.substr(0, p.size())) == lower(p);
}

constexpr const char* kHouseWords[] = {"Atreides", "Harkonnen", "Ordos", "Sardaukar", "Emperor", "Fremen", "Smugglers",
                                       "Mercenaries"};

int houseOrder(const std::string& h) {
    for (int i = 0; i < int(std::size(kHouseWords)); ++i)
        if (lower(h) == lower(kHouseWords[i])) return i;
    return h.empty() ? -1 : 99;
}

// "(Option 2)" / "(part 2)" anywhere in `s` -> k (0 if absent).
int parenNumber(const std::string& s, const std::string& word) {
    const std::string l = lower(s), w = "(" + lower(word) + " ";
    const auto at = l.find(w);
    if (at == std::string::npos) return 0;
    return std::atoi(l.c_str() + at + w.size());
}

// Stable id from a display name: letters / digits, other runs -> '_'.
std::string makeId(const std::string& name) {
    std::string id;
    for (unsigned char c : name) {
        if (std::isalnum(c)) id += static_cast<char>(std::tolower(c));
        else if (!id.empty() && id.back() != '_') id += '_';
    }
    while (!id.empty() && id.back() == '_') id.pop_back();
    return id;
}

std::string mostCommon(const std::vector<std::string>& v) {
    std::map<std::string, int> n;  // case-insensitive count, first spelling kept
    std::map<std::string, std::string> spelling;
    for (const auto& s : v) {
        if (s.empty()) continue;
        const std::string k = lower(s);
        ++n[k];
        spelling.emplace(k, s);
    }
    std::string best;
    int bn = 0;
    for (const auto& [k, c] : n)
        if (c > bn) { bn = c; best = spelling[k]; }
    return best;
}

int mostCommonSide(const std::vector<int>& v) {
    std::map<int, int> n;
    for (int s : v)
        if (s >= 0) ++n[s];
    int best = -1, bn = 0;
    for (const auto& [s, c] : n)
        if (c > bn) { bn = c; best = s; }
    return best;
}

// Campaign names for CustomCampaignData folders with SplashArtData (the launcher's window
// titles); others use the folder name with spaces between words.
std::string folderTitle(const std::string& folder) {
    if (lower(folder) == "summerssolstice") return "Summers' Solstice";
    std::string out;
    for (std::size_t i = 0; i < folder.size(); ++i) {
        if (i > 0 && std::isupper(static_cast<unsigned char>(folder[i])) &&
            std::islower(static_cast<unsigned char>(folder[i - 1])))
            out += ' ';
        out += folder[i];
    }
    return out;
}

// Missions whose name does not follow the scheme but belong to a series (map stem -> series,
// number): the Butlerian Jihad finale is listed after Zone Zero mission 9 by the launcher
// ("Butlerian Jihad Zone Zero Z").
struct StemOverride {
    const char* stem;
    const char* series;
    int number;
};
constexpr StemOverride kStemOverrides[] = {{"butlerian jihad zone zero z", "Butlerian Jihad: Zone Zero", 10}};

}  // namespace

int CommunityCampaign::missionCount() const {
    int n = 0;
    for (const auto& s : steps) n += int(s.missions.size());
    return n;
}

MissionNameParts parseCommunityMissionName(const std::string& name) {
    MissionNameParts p;
    const auto first = name.find(" - ");
    if (first == std::string::npos) return p;
    const auto last = name.rfind(" - ");
    p.series = trim(name.substr(0, first));
    if (last > first) p.subtitle = trim(name.substr(first + 3, last - first - 3));
    std::string rest = trim(name.substr(last + 3));
    for (const char* h : kHouseWords) {
        const std::string hw = std::string(h) + " ";
        if (startsWithNoCase(rest, hw)) {
            p.house = h;
            rest = trim(rest.substr(hw.size()));
            break;
        }
    }
    if (startsWithNoCase(rest, "Mission")) {
        std::string r = trim(rest.substr(7));
        if (startsWithNoCase(r, "Bonus")) {
            p.bonus = true;
        } else {
            std::size_t i = 0;
            while (i < r.size() && std::isdigit(static_cast<unsigned char>(r[i]))) ++i;
            if (i == 0) return p;
            p.number = std::atoi(r.substr(0, i).c_str());
            if (i < r.size() && std::isalpha(static_cast<unsigned char>(r[i])) &&
                (i + 1 == r.size() || !std::isalpha(static_cast<unsigned char>(r[i + 1]))))
                p.option = std::tolower(static_cast<unsigned char>(r[i])) - 'a' + 1;
            if (const int o = parenNumber(r, "Option")) p.option = o;
            p.part = parenNumber(r, "part");
        }
    } else if (lower(rest) == "bonus") {
        p.bonus = true;
    } else {
        return p;
    }
    if (lower(p.series) == "original campaign") return p;  // sub-house versions of the original missions
    p.valid = !p.series.empty();
    return p;
}

std::vector<CommunityCampaign> buildCommunityCampaigns(const std::vector<CommunityMissionRef>& missions,
                                                       const std::string& dataDir) {
    std::vector<CommunityCampaign> out;
    std::vector<bool> used(missions.size(), false);
    std::map<std::string, int> byStem;
    for (std::size_t i = 0; i < missions.size(); ++i) byStem.emplace(lower(missions[i].stem), int(i));

    // 1. SplashArtData arcs (CustomCampaignData/<folder>/SplashArtData/config.ini + arc<N>.txt).
    std::set<std::string> folders;
    for (const auto& m : missions)
        if (!m.campaignFolder.empty()) folders.insert(m.campaignFolder);
    for (const std::string& folder : folders) {
        const std::string dir = customCampaignDir(dataDir, folder);
        const std::string splash = dir.empty() ? std::string() : findFileNoCase(dir, "SplashArtData");
        const std::string cfgPath = splash.empty() ? std::string() : findFileNoCase(splash, "config.ini");
        if (cfgPath.empty()) continue;
        const IniFile cfg = readIni(cfgPath);
        const int arcs = std::atoi(iniGet(cfg, "Misc", "Arcs").c_str());
        CommunityCampaign c;
        c.folder = folder;
        c.name = folderTitle(folder);
        c.series = c.name;
        c.id = makeId(c.name);
        c.chapters = arcs;
        std::vector<std::string> authors;
        std::vector<int> sides;
        std::map<int, int> stepOfIndex;  // config Index -> step
        for (int arc = 1; arc <= std::max(arcs, 0); ++arc) {
            const std::string arcPath = findFileNoCase(splash, "arc" + std::to_string(arc) + ".txt");
            if (arcPath.empty()) continue;
            std::ifstream in(arcPath);
            std::string line;
            while (std::getline(in, line)) {
                const std::string stem = trim(line);
                auto it = byStem.find(lower(stem));
                if (stem.empty() || it == byStem.end() || used[std::size_t(it->second)]) continue;
                const int mi = it->second;
                const std::string idx = iniGet(cfg, stem, "Index");
                const int index = idx.empty() ? 1000 + mi : std::atoi(idx.c_str());
                auto [sit, fresh] = stepOfIndex.emplace(index, int(c.steps.size()));
                if (fresh) {
                    CommunityStep st;
                    st.chapter = arc;
                    // "FEYS03B1": a bonus mission; title "SBON1: Hunted" -> label "SBON1".
                    st.bonus = stem.size() >= 2 && std::toupper(static_cast<unsigned char>(stem[stem.size() - 2])) == 'B';
                    c.steps.push_back(st);
                }
                CommunityStep& st = c.steps[std::size_t(sit->second)];
                st.missions.push_back(mi);
                st.stems.push_back(missions[std::size_t(mi)].stem);
                if (st.label.empty()) {
                    std::string t = iniGet(cfg, stem, "Title");
                    if (const auto colon = t.find(':'); colon != std::string::npos) t = t.substr(0, colon);
                    st.label = t.empty() ? stem : trim(t);
                }
                used[std::size_t(mi)] = true;
                authors.push_back(missions[std::size_t(mi)].author);
                sides.push_back(missions[std::size_t(mi)].side);
            }
        }
        if (c.steps.empty()) continue;
        c.author = mostCommon(authors);
        c.side = mostCommonSide(sides);
        out.push_back(std::move(c));
    }

    // 2. Name scheme.
    struct Entry {
        int mission;
        MissionNameParts parts;
    };
    std::map<std::string, std::vector<Entry>> groups;  // lower(series) + "|" + lower(house)
    std::map<std::string, std::pair<std::string, std::string>> groupNames;
    for (std::size_t i = 0; i < missions.size(); ++i) {
        if (used[i]) continue;
        MissionNameParts p = parseCommunityMissionName(missions[i].name);
        for (const auto& o : kStemOverrides) {
            if (lower(missions[i].stem) == o.stem) {
                p = {};
                p.valid = true;
                p.series = o.series;
                p.number = o.number;
            }
        }
        if (!p.valid) continue;
        const std::string key = lower(p.series) + "|" + lower(p.house);
        groups[key].push_back({int(i), p});
        groupNames.emplace(key, std::make_pair(p.series, p.house));
    }
    std::vector<CommunityCampaign> named;
    for (auto& [key, entries] : groups) {
        // Order: number, part, bonus last; alternatives by option.
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return std::make_tuple(a.parts.bonus, a.parts.number, a.parts.part, a.parts.option) <
                   std::make_tuple(b.parts.bonus, b.parts.number, b.parts.part, b.parts.option);
        });
        CommunityCampaign c;
        c.series = groupNames[key].first;
        c.house = groupNames[key].second;
        c.name = c.house.empty() ? c.series : c.series + " - " + c.house;
        c.id = makeId(c.name);
        std::vector<std::string> authors, folderNames;
        std::vector<int> sides;
        for (const Entry& e : entries) {
            const bool sameStep = !c.steps.empty() && !e.parts.bonus && !c.steps.back().bonus &&
                                  c.steps.back().label == "Mission " + std::to_string(e.parts.number) +
                                                              (e.parts.part ? " part " + std::to_string(e.parts.part) : "");
            if (!sameStep) {
                CommunityStep st;
                st.bonus = e.parts.bonus;
                st.label = e.parts.bonus ? "Bonus"
                                         : "Mission " + std::to_string(e.parts.number) +
                                               (e.parts.part ? " part " + std::to_string(e.parts.part) : "");
                c.steps.push_back(st);
            }
            c.steps.back().missions.push_back(e.mission);
            c.steps.back().stems.push_back(missions[std::size_t(e.mission)].stem);
            authors.push_back(missions[std::size_t(e.mission)].author);
            folderNames.push_back(missions[std::size_t(e.mission)].campaignFolder);
            sides.push_back(missions[std::size_t(e.mission)].side);
        }
        if (c.steps.size() < 2) continue;  // a single mission is not a campaign
        c.author = mostCommon(authors);
        c.folder = mostCommon(folderNames);
        c.side = mostCommonSide(sides);
        named.push_back(std::move(c));
    }
    std::stable_sort(named.begin(), named.end(), [](const CommunityCampaign& a, const CommunityCampaign& b) {
        const std::string sa = lower(a.series), sb = lower(b.series);
        if (sa != sb) return sa < sb;
        return houseOrder(a.house) < houseOrder(b.house);
    });
    for (auto& c : named) out.push_back(std::move(c));
    std::stable_sort(out.begin(), out.end(),
                     [](const CommunityCampaign& a, const CommunityCampaign& b) { return lower(a.series) < lower(b.series); });
    return out;
}

// ---- progress --------------------------------------------------------------------------------

CommunityProgress CommunityProgress::load(const std::string& path) {
    CommunityProgress p;
    for (const auto& [section, kv] : readIni(path)) {
        if (section.empty()) continue;
        for (const auto& [k, v] : kv) {
            if (lower(k) != "won") continue;
            std::stringstream ss(v);
            std::string stem;
            while (std::getline(ss, stem, ','))
                if (!trim(stem).empty()) p.won[section].insert(lower(trim(stem)));
        }
    }
    return p;
}

bool CommunityProgress::save(const std::string& path) const {
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "; d2k-mac: community campaign progress (won missions per campaign)\n";
    for (const auto& [id, stems] : won) {
        if (stems.empty()) continue;
        out << "\n[" << id << "]\nwon=";
        bool first = true;
        for (const auto& s : stems) {
            out << (first ? "" : ",") << s;
            first = false;
        }
        out << "\n";
    }
    return bool(out);
}

bool CommunityProgress::isWon(const std::string& campaign, const std::string& stem) const {
    auto it = won.find(lower(campaign));
    return it != won.end() && it->second.count(lower(stem)) != 0;
}

void CommunityProgress::markWon(const std::string& campaign, const std::string& stem) {
    won[lower(campaign)].insert(lower(stem));
}

bool communityStepWon(const CommunityCampaign& c, int step, const CommunityProgress& p) {
    if (step < 0 || step >= int(c.steps.size())) return false;
    for (const auto& s : c.steps[std::size_t(step)].stems)
        if (p.isWon(c.id, s)) return true;
    return false;
}

bool communityStepUnlocked(const CommunityCampaign& c, int step, const CommunityProgress& p) {
    if (step < 0 || step >= int(c.steps.size())) return false;
    int prev = step - 1;
    while (prev >= 0 && c.steps[std::size_t(prev)].bonus) --prev;
    return prev < 0 || communityStepWon(c, prev, p);
}

int communityWonSteps(const CommunityCampaign& c, const CommunityProgress& p) {
    int n = 0;
    for (int i = 0; i < int(c.steps.size()); ++i)
        if (!c.steps[std::size_t(i)].bonus && communityStepWon(c, i, p)) ++n;
    return n;
}

int communityRequiredSteps(const CommunityCampaign& c) {
    int n = 0;
    for (const auto& s : c.steps) n += s.bonus ? 0 : 1;
    return n;
}

int communityNextStep(const CommunityCampaign& c, const CommunityProgress& p) {
    for (int i = 0; i < int(c.steps.size()); ++i)
        if (!c.steps[std::size_t(i)].bonus && communityStepUnlocked(c, i, p) && !communityStepWon(c, i, p)) return i;
    return c.steps.empty() ? -1 : int(c.steps.size()) - 1;
}

}  // namespace d2k::formats
