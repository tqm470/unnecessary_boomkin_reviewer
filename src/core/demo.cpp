#include "demo.h"

#include <algorithm>
#include <cmath>

#include "../../third_party/nlohmann/json.hpp"

namespace ubr {

using nlohmann::json;

namespace {

enum Id {
    STARFIRE = 194153, MOONFIRE = 8921, SUNFIRE = 93402, STARSURGE = 78674, STARFALL = 191034,
    FURY = 202770, SHOOTING = 202497, INCARN = 102560, CAT = 768, MOONKIN = 24858, TIGER = 252216,
};

struct Sim {
    int pid;
    double startMs = 50000;
    double ap = 30;
    json casts = json::array();
    json res = json::array();

    double ms(double t) const { return std::round(startMs + t * 1000.0); }
    json cr(double cost = 0) const {
        return json::array({{{"amount", (int)std::round(ap * 10)}, {"max", 1000}, {"type", 8},
                             {"cost", (int)std::round(cost * 10)}}});
    }
    void cast(double t, int id, double cost = 0) {
        casts.push_back({{"timestamp", ms(t)}, {"type", "cast"}, {"sourceID", pid}, {"targetID", 900},
                         {"abilityGameID", id}, {"fight", 1}, {"classResources", cr(cost)}});
        ap = std::max(0.0, ap - cost);
    }
    void begin(double t, int id, double durS) {
        casts.push_back({{"timestamp", ms(t)}, {"type", "begincast"}, {"sourceID", pid}, {"targetID", 900},
                         {"abilityGameID", id}, {"fight", 1}, {"duration", (int)std::round(durS * 1000)}});
    }
    void gain(double t, int id, double amt) {
        double add = std::min(amt, 100.0 - ap);
        double waste = amt - add;
        ap += add;
        res.push_back({{"timestamp", ms(t)}, {"type", "resourcechange"}, {"sourceID", pid},
                       {"targetID", pid}, {"abilityGameID", id}, {"fight", 1},
                       {"resourceChange", (int)amt}, {"resourceChangeType", 8},
                       {"otherResourceChange", 0}, {"maxResourceAmount", 100},
                       {"waste", (int)waste}, {"resourceActor", 1}, {"classResources", cr()}});
    }
};

} // namespace

DemoData makeDemo(bool sloppy, unsigned seed) {
    const double duration = 300;
    const double g = 0.9;        // GCD
    const double sfCast = 1.3;   // Starfire cast time
    unsigned rng = seed ? seed : 1;
    auto rnd = [&]() { rng = rng * 1103515245u + 12345u; return (rng >> 16) & 0x7fff; };

    Sim s;
    s.pid = sloppy ? 7 : 8;

    double nextShoot = 2.0;
    double furyTickT = 1e9;
    int furyTicks = 0;
    auto advance = [&](double to) {
        while (true) {
            double next = std::min(nextShoot, furyTicks > 0 ? furyTickT : 1e9);
            if (next > to) break;
            if (next == nextShoot) {
                s.gain(nextShoot, SHOOTING, 2);
                nextShoot += 3.0 + (rnd() % 100) / 100.0;
            } else {
                s.gain(furyTickT, FURY, 3);
                furyTickT += 0.5;
                --furyTicks;
            }
        }
    };

    double mfExp = 0, sfExp = 0, nextFury = 6, nextIncarn = 4;
    int spends = 0;
    double t = 0;
    auto inWindow = [](double x) {
        if (x < 20) return -1.0;
        double k = std::floor((x - 20) / 30.0);
        double ws = 20 + k * 30;
        return x < ws + 4.5 ? ws : -1.0;
    };
    auto nextWindow = [](double x) {
        double k = std::ceil((x - 20) / 30.0);
        return 20 + std::max(0.0, k) * 30;
    };

    while (t < duration - 1) {
        advance(t);
        // off-GCD cooldown
        if (t >= nextIncarn) {
            s.cast(t, INCARN);
            nextIncarn += 180;
        }
        double w = inWindow(t);
        if (w >= 0) {
            if (sloppy) {
                // stop, run as a cat, come back, re-dot first
                double catT = std::max(t, w + 3.2);
                advance(catT);
                s.cast(catT, CAT);
                if (rnd() % 2) s.cast(catT + g, TIGER);
                double back = std::max(catT + 2 * g, w + 6.0);
                advance(back);
                s.cast(back, MOONKIN);
                t = back + g;
                advance(t);
                s.cast(t, MOONFIRE);
                mfExp = t + 22;
                s.gain(t, MOONFIRE, 6);
                t += g;
                advance(t);
                s.cast(t, SUNFIRE);
                sfExp = t + 18;
                s.gain(t, SUNFIRE, 6);
                t += g;
                continue;
            }
            // tidy: instants on the move
            if (s.ap >= 36) {
                bool fall = (++spends % 4) == 0;
                s.cast(t, fall ? STARFALL : STARSURGE, fall ? 50 : 36);
            } else if (mfExp - t < 10) {
                s.cast(t, MOONFIRE); mfExp = t + 22; s.gain(t, MOONFIRE, 6);
            } else {
                s.cast(t, SUNFIRE); sfExp = t + 18; s.gain(t, SUNFIRE, 6);
            }
            t += g + 0.05 * (rnd() % 3);
            if (rnd() % 3 == 0) t += 1.2 + 0.1 * (rnd() % 6); // a short real stop
            continue;
        }

        bool mfDue = mfExp - t < (sloppy ? 9.0 : 6.6);
        bool sfDue = sfExp - t < (sloppy ? 8.0 : 5.4);
        bool spendNow = sloppy ? s.ap >= 97 : (s.ap >= 36 && s.ap + 12 > 100) || s.ap >= 75;

        if (!sloppy && spendNow) {
            bool fall = (++spends % 4) == 0;
            s.cast(t, fall ? STARFALL : STARSURGE, fall ? 50 : 36);
            t += g + 0.06 * (rnd() % 5);
            continue;
        }
        if (mfDue) {
            s.cast(t, MOONFIRE); mfExp = t + 22; s.gain(t, MOONFIRE, 6);
            t += g + (sloppy ? 0.15 * (rnd() % 4) : 0.02 * (rnd() % 3));
            continue;
        }
        if (sfDue) {
            s.cast(t, SUNFIRE); sfExp = t + 18; s.gain(t, SUNFIRE, 6);
            t += g + (sloppy ? 0.15 * (rnd() % 4) : 0.02 * (rnd() % 3));
            continue;
        }
        if (t >= nextFury) {
            s.cast(t, FURY);
            furyTicks = 8;
            furyTickT = t + 0.5;
            nextFury = t + (sloppy ? 90 : 60);
            t += g;
            continue;
        }
        if (sloppy && spendNow) {
            s.cast(t, STARSURGE, 36);
            t += g + 0.2 * (rnd() % 4);
            continue;
        }
        // Starfire
        double nw = nextWindow(t);
        if (t + sfCast > nw) {
            if (sloppy && (rnd() % 2)) {
                s.begin(t, STARFIRE, sfCast); // will be canceled by the mechanic
                t = nw;
                continue;
            }
            if (!sloppy) {
                // instant filler instead of a cast that would be cut
                if (s.ap >= 36) { s.cast(t, STARSURGE, 36); }
                else { s.cast(t, SUNFIRE); sfExp = t + 18; s.gain(t, SUNFIRE, 6); }
                t += g;
                continue;
            }
            t = nw;
            continue;
        }
        s.begin(t, STARFIRE, sfCast);
        double done = t + sfCast;
        advance(done);
        s.cast(done, STARFIRE);
        s.gain(done, STARFIRE, 10);
        t = done + (sloppy ? 0.12 * (rnd() % 6) : 0.05 * (rnd() % 5));
    }
    advance(duration);

    DemoData d;
    d.fightId = 1;
    d.playerId = s.pid;
    json abilities = json::array();
    const std::pair<int, const char*> names[] = {
        {STARFIRE, "Starfire"}, {MOONFIRE, "Moonfire"}, {SUNFIRE, "Sunfire"}, {STARSURGE, "Starsurge"},
        {STARFALL, "Starfall"}, {FURY, "Fury of Elune"}, {SHOOTING, "Shooting Stars"},
        {INCARN, "Incarnation: Chosen of Elune"}, {CAT, "Cat Form"}, {MOONKIN, "Moonkin Form"},
        {TIGER, "Tiger Dash"}};
    for (const auto& n : names) abilities.push_back({{"gameID", n.first}, {"name", n.second}});
    json report = {{"data", {{"reportData", {{"report", {
        {"title", "Demo report"},
        {"fights", json::array({{{"id", 1}, {"name", "Demo Boss"}, {"startTime", s.startMs},
                                 {"endTime", s.startMs + duration * 1000}, {"kill", true},
                                 {"encounterID", 9999}, {"keystoneLevel", nullptr}, {"difficulty", 4}}})},
        {"masterData", {
            {"actors", json::array({{{"id", s.pid}, {"name", sloppy ? "Sloppykin" : "Tidykin"},
                                     {"type", "Player"}, {"subType", "Druid"}, {"server", "Demo"}}})},
            {"abilities", abilities}}}}}}}}}};
    d.reportJson = report.dump();
    // Keep events in time order, like the API does.
    auto byTime = [](const json& a, const json& b) { return a["timestamp"] < b["timestamp"]; };
    std::vector<json> c(s.casts.begin(), s.casts.end()), r(s.res.begin(), s.res.end());
    std::stable_sort(c.begin(), c.end(), byTime);
    std::stable_sort(r.begin(), r.end(), byTime);
    d.castsJson = json(c).dump();
    d.resourcesJson = json(r).dump();
    return d;
}

} // namespace ubr
