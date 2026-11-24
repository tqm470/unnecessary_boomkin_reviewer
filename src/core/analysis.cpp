#include "analysis.h"

#include <algorithm>
#include <cmath>

namespace ubr {

namespace {

struct GcdCast {
    double start = 0, end = 0; // end = when the cast finished (== start for instants)
    bool instant = true;
    const SpellInfo* spell = nullptr;
    std::string name;
    int abilityId = 0;
};

struct Canceled {
    double start = 0, lost = 0;
    std::string name;
};

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

double apAt(const std::vector<ApPoint>& ap, double t) {
    // last point strictly before t (state "just before" a cast at t)
    double v = ap.empty() ? 0 : ap.front().value;
    for (const auto& p : ap) {
        if (p.t >= t) break;
        v = p.value;
    }
    return v;
}

// Value when a cast at t was pressed: everything before t, plus the cast
// event's own snapshot at t (order 0), but not the gain it produced (order 1).
static double apBeforeGain(const std::vector<ApPoint>& ap, double t) {
    double v = ap.empty() ? 0 : ap.front().value;
    for (const auto& p : ap) {
        if (p.t > t + 1e-6) break;
        if (p.t >= t - 1e-6 && p.order != 0) break;
        v = p.value;
    }
    return v;
}

static double apMaxAt(const std::vector<ApPoint>& ap, double t) {
    double m = ap.empty() ? 100 : ap.front().max;
    for (const auto& p : ap) {
        if (p.t >= t) break;
        m = p.max;
    }
    return m;
}

AnalysisResult analyze(const FightData& fd, const SpellBook& book, const AnalysisSettings& s) {
    AnalysisResult r;
    r.playerName = fd.player.name;
    r.reportCode = fd.reportCode;
    r.reportTitle = fd.reportTitle;
    r.fightName = fd.fight.name;
    r.link = fd.link;
    r.dungeon = fd.fight.isDungeon();
    r.keystoneLevel = fd.fight.keystoneLevel;
    r.kill = fd.fight.kill;
    r.fightDuration = fd.fight.durationSec();
    r.apConvention = fd.apConvention;
    const double F = r.fightDuration;

    std::map<std::string, SpellCount> counts;
    auto countFor = [&](const std::string& name, SpellRole role) -> SpellCount& {
        SpellCount& c = counts[name];
        c.name = name;
        c.role = role;
        return c;
    };

    // ---- 1. Pair begincast/cast into hardcasts, detect cancels -------------
    std::vector<GcdCast> gcd;
    std::vector<Canceled> canceled;
    struct Pending { double t; int id; double durMs; };
    std::vector<Pending> pending;

    auto nameOf = [&](int id) {
        const SpellInfo* sp = book.lookup(id, fd.abilityName(id));
        return sp ? sp->name : fd.abilityName(id);
    };
    auto cancelPending = [&](double now) {
        for (const auto& p : pending) {
            double lost = std::max(0.0, std::min(now, p.t + (p.durMs > 0 ? p.durMs / 1000 : 2.0)) - p.t);
            canceled.push_back({p.t, lost, nameOf(p.id)});
        }
        pending.clear();
    };

    for (const auto& c : fd.casts) {
        if (c.begin) {
            cancelPending(c.t); // starting a new cast abandons any previous one
            pending.push_back({c.t, c.abilityId, c.durationMs});
            continue;
        }
        const std::string abilityName = fd.abilityName(c.abilityId);
        const SpellInfo* sp = book.lookup(c.abilityId, abilityName);
        GcdCast g;
        g.spell = sp;
        g.name = sp ? sp->name : abilityName;
        g.abilityId = c.abilityId;
        g.start = g.end = c.t;

        auto it = std::find_if(pending.begin(), pending.end(),
                               [&](const Pending& p) { return p.id == c.abilityId; });
        if (it != pending.end()) {
            double maxLen = it->durMs > 0 ? it->durMs / 1000.0 + 1.0 : 6.0;
            if (c.t - it->t <= maxLen) {
                g.start = it->t;
                g.instant = false;
            }
            pending.erase(it);
        }
        if (!pending.empty() && book.onGcd(sp)) cancelPending(g.start); // pressed something else

        if (!sp) {
            r.unclassified[abilityName]++;
            continue;
        }
        countFor(sp->name, sp->role).casts++;
        if (sp->marker) r.markers.push_back({g.start, sp->name});
        if (book.onGcd(sp)) gcd.push_back(g);
        else if (sp->role == SpellRole::OffGcd) {
            // tracked for "pressed at cap" below
            if (!fd.ap.empty() && sp->marker &&
                apAt(fd.ap, g.start) >= apMaxAt(fd.ap, g.start) - 0.5)
                r.cooldownsAtCap++;
        }
    }
    cancelPending(F);
    for (const auto& c : canceled) {
        if (c.start < 0 || c.start > F) continue;
        r.canceledCasts++;
        r.canceledTime += c.lost;
        countFor(c.name, SpellRole::Unknown).canceled++;
    }
    std::sort(gcd.begin(), gcd.end(), [](const GcdCast& a, const GcdCast& b) { return a.start < b.start; });
    r.gcdActions = (int)gcd.size();

    // ---- 2. Estimate the GCD over time -------------------------------------
    // Two instants pressed back to back are at most one GCD apart, so the
    // shortest such interval near a moment is a good local GCD estimate.
    std::vector<std::pair<double, double>> instantGaps;
    for (size_t i = 0; i + 1 < gcd.size(); ++i)
        if (gcd[i].instant) {
            double d = gcd[i + 1].start - gcd[i].start;
            if (d >= 0.7 && d <= 1.6) instantGaps.push_back({gcd[i].start, d});
        }
    auto localGcd = [&](double t) {
        if (s.fixedGcd > 0) return s.fixedGcd;
        double best = 1e9;
        for (const auto& g : instantGaps)
            if (std::fabs(g.first - t) <= 30) best = std::min(best, g.second);
        if (best > 100) best = 1.1;
        return std::min(std::max(best, 0.75), 1.5);
    };

    // ---- 3. Busy blocks ------------------------------------------------------
    std::vector<Interval> raw;
    std::vector<double> gcdSamples;
    for (const auto& g : gcd) {
        double lg = localGcd(g.start);
        gcdSamples.push_back(lg);
        raw.push_back({g.start, std::max(g.end, g.start + lg)});
    }
    r.gcdEstimate = median(gcdSamples);
    for (const auto& iv : raw) {
        Interval c{std::max(0.0, iv.start), std::min(F, iv.end)};
        if (c.end <= c.start) continue;
        if (!r.busy.empty() && c.start <= r.busy.back().end + 1e-6)
            r.busy.back().end = std::max(r.busy.back().end, c.end);
        else
            r.busy.push_back(c);
    }

    // ---- 4. Combat windows / pulls ------------------------------------------
    bool split = s.segmentation == Segmentation::SplitPulls ||
                 (s.segmentation == Segmentation::Auto && r.dungeon);
    if (!r.busy.empty()) {
        if (!split) {
            r.combat.push_back({0, F});
        } else {
            Interval cur = r.busy.front();
            for (size_t i = 1; i < r.busy.size(); ++i) {
                if (r.busy[i].start - cur.end > s.combatGap) {
                    r.combat.push_back(cur);
                    cur = r.busy[i];
                } else {
                    cur.end = r.busy[i].end;
                }
            }
            r.combat.push_back(cur);
        }
    }

    auto overlap = [](const Interval& a, double s0, double e0) {
        return std::max(0.0, std::min(a.end, e0) - std::max(a.start, s0));
    };
    for (size_t i = 0; i < r.combat.size(); ++i) {
        const Interval& w = r.combat[i];
        Pull p;
        p.index = (int)i + 1;
        p.start = w.start;
        p.end = w.end;
        for (const auto& b : r.busy) p.busy += overlap(b, w.start, w.end);
        r.pulls.push_back(p);
        r.busyTime += p.busy;
        r.combatTime += w.len();
    }
    r.uptimeFight = F > 0 ? r.busyTime / F : 0;
    r.uptimeCombat = r.combatTime > 0 ? r.busyTime / r.combatTime : 0;

    // ---- 5. Pauses inside combat --------------------------------------------
    auto nextCastAt = [&](double t) -> const GcdCast* {
        for (const auto& g : gcd)
            if (g.start >= t - 1e-6) return &g;
        return nullptr;
    };
    for (const auto& w : r.combat) {
        double cursor = w.start;
        auto addGap = [&](double a, double b) {
            double d = b - a;
            if (d <= 0.02) return;
            if (d >= s.pauseMin) {
                Pause p;
                p.start = a;
                p.duration = d;
                if (const GcdCast* n = nextCastAt(b)) {
                    p.nextSpell = n->name;
                    p.nextRole = n->spell ? n->spell->role : SpellRole::Unknown;
                }
                r.pauses.push_back(p);
                r.pauseTime += d;
                if (d >= s.longPause) r.longPauses++;
            } else {
                r.smallDelays++;
                r.smallDelayTime += d;
            }
        };
        for (const auto& b : r.busy) {
            if (b.end <= w.start || b.start >= w.end) continue;
            if (b.start > cursor) addGap(cursor, b.start);
            cursor = std::max(cursor, b.end);
        }
        if (w.end > cursor) addGap(cursor, w.end);
    }
    std::sort(r.pauses.begin(), r.pauses.end(),
              [](const Pause& a, const Pause& b) { return a.duration > b.duration; });

    // ---- 6. Role counts --------------------------------------------------------
    for (const auto& g : gcd) {
        switch (g.spell->role) {
            case SpellRole::Spender: r.spenders++; break;
            case SpellRole::Filler: r.fillers++; break;
            case SpellRole::Dot: r.dots++; break;
            case SpellRole::Cooldown: r.cooldowns++; break;
            case SpellRole::Form: r.forms++; break;
            case SpellRole::Movement: r.movement++; break;
            default: break;
        }
        if (g.spell->name == "Fury of Elune") r.furyOfElune++;
    }

    // ---- 7. Astral Power ------------------------------------------------------
    r.ap = fd.ap;
    r.hasAp = !fd.ap.empty();
    if (r.hasAp) {
        // time at cap + windows
        CapWindow cur;
        bool in = false;
        for (size_t i = 0; i < r.ap.size(); ++i) {
            double t0 = std::max(0.0, r.ap[i].t);
            double t1 = std::min(F, i + 1 < r.ap.size() ? r.ap[i + 1].t : F);
            if (t1 <= t0) continue;
            bool capped = r.ap[i].value >= r.ap[i].max - 0.5;
            if (capped) {
                r.capTime += t1 - t0;
                if (in && t0 - (cur.start + cur.duration) < 0.05) {
                    cur.duration = t1 - cur.start;
                } else {
                    if (in) r.capWindows.push_back(cur);
                    cur = CapWindow{t0, t1 - t0, {}};
                    in = true;
                }
            } else if (in) {
                r.capWindows.push_back(cur);
                in = false;
            }
        }
        if (in) r.capWindows.push_back(cur);

        for (auto& w : r.capWindows) {
            for (const auto& g : gcd)
                if (g.start >= w.start - 0.05 && g.start <= w.start + w.duration + 0.05)
                    w.casts.push_back(g.name);
            for (const auto& m : r.markers)
                if (m.t >= w.start - 0.05 && m.t <= w.start + w.duration + 0.05 &&
                    std::find(w.casts.begin(), w.casts.end(), m.name) == w.casts.end())
                    w.casts.push_back(m.name + " *");
            r.longestCap = std::max(r.longestCap, w.duration);
        }
        std::sort(r.capWindows.begin(), r.capWindows.end(),
                  [](const CapWindow& a, const CapWindow& b) { return a.duration > b.duration; });

        // what was pressed while capped
        for (const auto& g : gcd) {
            double v = apBeforeGain(r.ap, g.start);
            double mx = apMaxAt(r.ap, g.start + 1e-4);
            bool capped = v >= mx - 0.5;
            if (g.spell->role == SpellRole::Filler && !g.instant) {
                if (apAt(r.ap, g.start + 1e-4) >= s.highApThreshold) r.fillersStartedHigh++;
            }
            if (!capped || g.spell->role == SpellRole::Spender) continue;
            r.castsAtCapNonSpender++;
            countFor(g.spell->name, g.spell->role).atCap++;
            if (g.spell->role == SpellRole::Dot) r.dotsAtCap++;
            if (g.spell->role == SpellRole::Form || g.spell->role == SpellRole::Movement) r.formsAtCap++;
        }

        for (const auto& gn : fd.gains) {
            r.apGained += gn.gain;
            r.apWasted += gn.waste;
            std::string nm = nameOf(gn.abilityId);
            const SpellInfo* sp = book.lookup(gn.abilityId, fd.abilityName(gn.abilityId));
            SpellCount& c = countFor(nm, sp ? sp->role : SpellRole::Unknown);
            c.gain += gn.gain;
            c.waste += gn.waste;
        }

        for (auto& p : r.pulls) {
            for (const auto& w : r.capWindows)
                p.capTime += overlap({w.start, w.start + w.duration}, p.start, p.end);
            for (const auto& gn : fd.gains)
                if (gn.t >= p.start - 0.5 && gn.t <= p.end + 0.5) p.waste += gn.waste;
        }
    }

    // fill role for spells that only appeared as canceled
    for (auto& kv : counts) {
        if (kv.second.role == SpellRole::Unknown) {
            if (const SpellInfo* sp = book.lookup(-1, kv.first)) kv.second.role = sp->role;
        }
        r.spells.push_back(kv.second);
    }
    std::sort(r.spells.begin(), r.spells.end(), [](const SpellCount& a, const SpellCount& b) {
        if (a.casts != b.casts) return a.casts > b.casts;
        return a.name < b.name;
    });
    std::sort(r.markers.begin(), r.markers.end(),
              [](const Marker& a, const Marker& b) { return a.t < b.t; });

    // ---- 8. Tips (most impactful first) ---------------------------------------
    if (r.combatTime > 0 && r.uptimeCombat < 0.80)
        r.tips.push_back({"uptime", r.uptimeCombat * 100, r.perMin(r.longPauses) * 10});
    if (r.hasAp && r.wastePct() > 5) r.tips.push_back({"waste", r.wastePct(), r.capTime});
    if (r.dotsAtCap >= 3) r.tips.push_back({"dots_at_cap", (double)r.dotsAtCap});
    if (r.fillersStartedHigh >= 3) r.tips.push_back({"filler_high", (double)r.fillersStartedHigh, s.highApThreshold});
    if (r.cooldownsAtCap >= 1) r.tips.push_back({"cd_at_cap", (double)r.cooldownsAtCap});
    if (r.combatTime > 0 && r.perMin(r.forms + r.movement) * 10 > 8)
        r.tips.push_back({"forms", (double)(r.forms + r.movement), r.perMin(r.forms + r.movement) * 10});
    int pausesEndingInDot = 0;
    for (const auto& p : r.pauses)
        if (p.duration >= s.longPause && p.nextRole == SpellRole::Dot) pausesEndingInDot++;
    if (pausesEndingInDot >= 3) r.tips.push_back({"redot_after_pause", (double)pausesEndingInDot});
    if (r.combatTime > 0 && r.canceledCasts > 0 && r.perMin(r.canceledCasts) * 10 > 8)
        r.tips.push_back({"canceled", (double)r.canceledCasts, r.canceledTime});
    if (r.combatTime > 0 && r.smallDelayTime / r.combatTime > 0.12)
        r.tips.push_back({"small_delays", r.smallDelayTime, r.smallDelayTime / std::max(1, r.smallDelays)});
    return r;
}

} // namespace ubr
