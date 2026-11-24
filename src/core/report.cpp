#include "report.h"

#include <algorithm>
#include <sstream>

namespace ubr {

static std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }
    return s;
}

std::string roleLabel(SpellRole role, Lang lang) {
    return tr(lang, (std::string("role_") + roleName(role)).c_str());
}

std::string formatTip(const Tip& t, Lang lang) {
    std::string key = "tip_" + t.id;
    std::string s = tr(lang, key.c_str());
    std::string a, b;
    if (t.id == "uptime") a = fmtNum(t.a, 1, lang) + "%";
    else if (t.id == "waste") { a = fmtNum(t.a, 1, lang) + "%"; b = fmtSec(t.b, lang); }
    else if (t.id == "filler_high") { a = fmtNum(t.a, 0, lang); b = fmtNum(t.b, 0, lang); }
    else if (t.id == "forms") { a = fmtNum(t.a, 0, lang); b = fmtNum(t.b, 1, lang); }
    else if (t.id == "canceled") { a = fmtNum(t.a, 0, lang); b = fmtSec(t.b, lang); }
    else if (t.id == "small_delays") { a = fmtSec(t.a, lang); b = fmtSec(t.b, lang); }
    else a = fmtNum(t.a, 0, lang);
    s = replaceAll(s, "%1", a);
    s = replaceAll(s, "%2", b);
    return s;
}

static std::string fightLabel(const AnalysisResult& r, Lang lang) {
    std::string s = r.fightName;
    if (r.keystoneLevel > 0) s += " +" + std::to_string(r.keystoneLevel);
    else s += std::string(" (") + (r.kill ? tr(lang, "kill") : tr(lang, "wipe")) + ")";
    return s;
}

std::vector<Row> summaryRows(const AnalysisResult& r, const AnalysisResult* c, Lang L) {
    std::vector<Row> rows;
    auto add = [&](const char* key, auto fn) {
        Row row;
        row.label = tr(L, key);
        row.a = fn(r);
        if (c) row.b = fn(*c);
        rows.push_back(row);
    };
    add("m_player", [&](const AnalysisResult& x) { return x.playerName; });
    add("m_fight", [&](const AnalysisResult& x) { return fightLabel(x, L); });
    add("m_duration", [&](const AnalysisResult& x) { return fmtClock(x.fightDuration); });
    add("m_combat", [&](const AnalysisResult& x) { return fmtClock(x.combatTime); });
    add("m_uptime_combat", [&](const AnalysisResult& x) { return fmtPct(x.uptimeCombat, L); });
    add("m_uptime_fight", [&](const AnalysisResult& x) { return fmtPct(x.uptimeFight, L); });
    add("m_gcd", [&](const AnalysisResult& x) { return fmtNum(x.gcdEstimate, 2, L) + "s"; });
    add("m_gcd_actions", [&](const AnalysisResult& x) { return fmtNum(x.gcdActions, 0, L); });
    add("m_pauses", [&](const AnalysisResult& x) {
        return fmtNum((double)x.pauses.size(), 0, L) + " (" + fmtSec(x.pauseTime, L) + ")";
    });
    add("m_long_pauses", [&](const AnalysisResult& x) { return fmtNum(x.longPauses, 0, L); });
    add("m_small", [&](const AnalysisResult& x) {
        return fmtNum(x.smallDelays, 0, L) + " (" + fmtSec(x.smallDelayTime, L) + ")";
    });
    add("m_canceled", [&](const AnalysisResult& x) {
        return fmtNum(x.canceledCasts, 0, L) + " (" + fmtSec(x.canceledTime, L) + ")";
    });
    if (r.dungeon || (c && c->dungeon))
        add("m_pulls", [&](const AnalysisResult& x) { return fmtNum((double)x.pulls.size(), 0, L); });
    add("m_cap_time", [&](const AnalysisResult& x) {
        if (!x.hasAp) return std::string("-");
        double pct = x.fightDuration > 0 ? x.capTime / x.fightDuration : 0;
        return fmtSec(x.capTime, L) + " (" + fmtPct(pct, L) + ")";
    });
    add("m_longest_cap", [&](const AnalysisResult& x) {
        return x.hasAp ? fmtSec(x.longestCap, L) : std::string("-");
    });
    add("m_ap_wasted", [&](const AnalysisResult& x) {
        if (!x.hasAp) return std::string("-");
        return fmtNum(x.apWasted, 0, L) + " / " + fmtNum(x.apGained, 0, L) + " (" +
               fmtNum(x.wastePct(), 1, L) + "%)";
    });
    add("m_ap_gen_min", [&](const AnalysisResult& x) {
        return fmtNum(x.combatTime > 0 ? x.apGained / (x.combatTime / 60) : 0, 0, L);
    });
    add("m_spenders_min", [&](const AnalysisResult& x) { return fmtNum(x.perMin(x.spenders), 1, L); });
    add("m_fury_min", [&](const AnalysisResult& x) {
        return fmtNum(x.perMin(x.furyOfElune), 2, L) + " (" + std::to_string(x.furyOfElune) + ")";
    });
    add("m_dots_min", [&](const AnalysisResult& x) { return fmtNum(x.perMin(x.dots), 1, L); });
    add("m_forms", [&](const AnalysisResult& x) { return fmtNum(x.forms + x.movement, 0, L); });
    add("m_dots_cap", [&](const AnalysisResult& x) { return fmtNum(x.dotsAtCap, 0, L); });
    add("m_filler_high", [&](const AnalysisResult& x) { return fmtNum(x.fillersStartedHigh, 0, L); });
    add("m_cd_cap", [&](const AnalysisResult& x) { return fmtNum(x.cooldownsAtCap, 0, L); });
    add("m_unclassified", [&](const AnalysisResult& x) {
        int n = 0;
        for (const auto& kv : x.unclassified) n += kv.second;
        return fmtNum(n, 0, L);
    });
    add("m_ap_convention", [&](const AnalysisResult& x) { return x.apConvention; });
    return rows;
}

std::string buildReportText(const AnalysisResult& r, const AnalysisResult* c, Lang L) {
    std::ostringstream o;
    o << "# " << tr(L, "app_title") << " - " << r.playerName << " - " << fightLabel(r, L) << "\n";
    if (!r.link.empty()) o << r.link << "\n";
    o << "\n";
    for (const auto& row : summaryRows(r, c, L)) {
        o << "- " << row.label << ": " << row.a;
        if (c) o << "  |  " << row.b;
        o << "\n";
    }
    if (!r.tips.empty()) {
        o << "\n## " << tr(L, "tips") << "\n";
        int i = 1;
        for (const auto& t : r.tips) o << i++ << ". " << formatTip(t, L) << "\n";
    }
    o << "\n## " << tr(L, "tab_pauses") << "\n";
    for (size_t i = 0; i < r.pauses.size() && i < 10; ++i) {
        const auto& p = r.pauses[i];
        o << "- " << fmtClock(p.start) << "  " << fmtSec(p.duration, L) << "  -> " << p.nextSpell << "\n";
    }
    if (r.hasAp) {
        o << "\n## " << tr(L, "tab_cap") << "\n";
        for (size_t i = 0; i < r.capWindows.size() && i < 8; ++i) {
            const auto& w = r.capWindows[i];
            o << "- " << fmtClock(w.start) << "  " << fmtSec(w.duration, L) << "  ";
            for (size_t k = 0; k < w.casts.size(); ++k) o << (k ? ", " : "") << w.casts[k];
            o << "\n";
        }
    }
    if (c) {
        o << "\n(" << tr(L, "comparison") << ": " << c->playerName << " - " << fightLabel(*c, L) << ")\n";
        if (!c->link.empty()) o << c->link << "\n";
    }
    return o.str();
}

} // namespace ubr
