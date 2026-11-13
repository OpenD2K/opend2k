#include "platform/community_menu.hpp"

#include "formats/language.hpp"
#include "platform/settings.hpp"
#include "render/ui_text.hpp"

#include <cctype>
#include <cstdio>
#include <filesystem>

namespace d2k::platform {

namespace {

// Our English text, or (Enhanced pages) its translation.
const char* T(bool translated, const char* english) { return translated ? render::tr(english) : english; }

// printf with the English or the translated pattern.
std::string F(bool translated, const char* english, int a, int b = 0) {
    if (translated) return render::trf(english, a, b);
    char buf[512];
    std::snprintf(buf, sizeof buf, english, a, b);
    return buf;
}

}  // namespace

std::string uiUtf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = n > 0 && i + n <= s.size();
        for (int k = 1; k < n && ok; ++k) ok = (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
        if (!ok) return formats::lang::toUtf8(s, formats::lang::activeCodepage());
        i += std::size_t(n);
    }
    return s;
}

std::vector<CommunityRow> communityRows(const Catalog& cat, const formats::CommunityCampaign& c,
                                        const formats::CommunityProgress& p, bool translated) {
    std::vector<CommunityRow> rows;
    for (int s = 0; s < int(c.steps.size()); ++s) {
        const formats::CommunityStep& st = c.steps[std::size_t(s)];
        const bool open = formats::communityStepUnlocked(c, s, p);
        for (std::size_t k = 0; k < st.missions.size(); ++k) {
            const int mi = st.missions[k];
            if (mi < 0 || mi >= int(cat.custom.size())) continue;
            const MissionInfo& m = cat.custom[std::size_t(mi)];
            CommunityRow r;
            r.step = s;
            r.mission = mi;
            // Arc campaigns have titled missions ("S03V1: Double Bind"); the others "<House>
            // Mission 3 (Option 1)" after the series name.
            r.text = c.chapters > 0 ? m.name : m.shortName;
            if (c.chapters > 0 && st.chapter > 0) r.text = F(translated, "Arc %d", st.chapter) + "  " + (translated ? uiUtf8(r.text) : r.text);
            r.bonus = st.bonus;
            r.won = p.isWon(c.id, m.stem);
            r.locked = !open;
            if (r.won) r.status = T(translated, r.bonus ? "Bonus - won" : "Won");
            else if (r.locked) r.status = T(translated, "Locked");
            else if (r.bonus) r.status = T(translated, "Bonus");
            // Steps with several versions: pick one ("Choice"); once one is won the others stay
            // playable ("Alternative").
            if (st.missions.size() > 1 && !r.won && !r.locked && r.status.empty())
                r.status = T(translated, formats::communityStepWon(c, s, p) ? "Alternative" : "Choice");
            rows.push_back(std::move(r));
        }
    }
    return rows;
}

int communitySuggestedRow(const std::vector<CommunityRow>& rows, const formats::CommunityCampaign& c,
                          const formats::CommunityProgress& p) {
    const int step = formats::communityNextStep(c, p);
    for (int i = 0; i < int(rows.size()); ++i)
        if (rows[std::size_t(i)].step == step) return i;
    return rows.empty() ? -1 : 0;
}

std::string communityProgressText(const formats::CommunityCampaign& c, const formats::CommunityProgress& p,
                                  bool translated) {
    return F(translated, "%d / %d missions won", formats::communityWonSteps(c, p), formats::communityRequiredSteps(c));
}

std::vector<std::string> communityCampaignInfo(const Catalog& cat, const formats::CommunityCampaign& c,
                                               bool translated) {
    std::vector<std::string> out;
    if (!c.author.empty()) {
        // "by %s" (the author's name may be longer than a printf buffer would like: no trf here)
        const std::string pat = T(translated, "by %s");
        const auto at = pat.find("%s");
        const std::string a = translated ? uiUtf8(c.author) : c.author;
        out.push_back(at == std::string::npos ? pat + " " + a : pat.substr(0, at) + a + pat.substr(at + 2));
    }
    int bonus = 0, choices = 0, mods = 0, extended = 0, missions = 0;
    for (const auto& st : c.steps) {
        bonus += st.bonus ? 1 : 0;
        choices += st.missions.size() > 1 ? 1 : 0;
        for (int mi : st.missions) {
            if (mi < 0 || mi >= int(cat.custom.size())) continue;
            const MissionInfo& m = cat.custom[std::size_t(mi)];
            ++missions;
            mods += m.modsFolder.empty() ? 0 : 1;
            extended += m.extendedMis ? 1 : 0;
        }
    }
    std::string size = F(translated, "%d missions", formats::communityRequiredSteps(c));
    if (missions > formats::communityRequiredSteps(c)) size += " " + F(translated, "(%d maps)", missions);
    if (c.chapters > 0) size += " " + F(translated, "in %d arcs", c.chapters);
    out.push_back(size);
    if (choices) out.push_back(F(translated, "%d missions offer a choice of versions", choices));
    if (bonus) out.push_back(F(translated, "%d bonus missions", bonus));
    if (mods) out.push_back(F(translated, "Custom game data (units, tilesets, colours) for %d missions", mods));
    if (extended)
        out.push_back(F(translated,
                        "%d missions use the extended event system of the 1.06p+ patch (supported; a few rarely "
                        "used event types are not - docs/extended-events.md).",
                        extended));
    return out;
}

std::string communityBriefingText(const MissionInfo& m, bool translated) {
    auto g = [&](const std::string& s) { return translated ? uiUtf8(s) : s; };  // game text next to ours
    std::string t = m.briefing.empty() ? std::string(T(translated, "(No briefing text.)")) : g(m.briefing);
    for (std::size_t i = 0; i < m.intel.size(); ++i) t += "\n\n" + F(translated, "Intel %d:", int(i) + 1) + "\n" + g(m.intel[i]);
    if (m.extendedMis)
        t += "\n\n" + F(translated, "(This mission uses the extended event system of the 1.06p+ patch (%d events).)",
                        m.extendedEvents);
    return t;
}

std::string communityProgressPath() {
    const std::string settings = Settings::path();
    if (settings.empty()) return {};
    return (std::filesystem::path(settings).parent_path() / "community_progress.ini").string();
}

void recordCommunityWin(const Catalog& cat, formats::CommunityProgress& p, const std::string& stem) {
    if (stem.empty()) return;
    auto lower = [](std::string v) {
        for (auto& ch : v) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return v;
    };
    const std::string want = lower(stem);
    for (const auto& c : cat.community)
        for (const auto& st : c.steps)
            for (const auto& s : st.stems)
                if (lower(s) == want) p.markWon(c.id, s);
}

}  // namespace d2k::platform
