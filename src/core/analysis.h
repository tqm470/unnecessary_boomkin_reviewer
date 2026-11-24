// The actual review: real GCD uptime, pauses, pulls and Astral Power capping.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "model.h"
#include "spells.h"

namespace ubr {

enum class Segmentation { Auto, WholeFight, SplitPulls };

struct AnalysisSettings {
    double combatGap = 10.0;      // a gap longer than this between casts = between pulls
    double pauseMin = 1.5;        // gaps at least this long count as "pauses"
    double longPause = 2.5;       // highlighted pauses
    double fixedGcd = 0.0;        // 0 = estimate from the log
    double highApThreshold = 90;  // filler started at or above this AP = overcap risk
    Segmentation segmentation = Segmentation::Auto;
};

struct Interval {
    double start = 0, end = 0;
    double len() const { return end - start; }
};

struct Pause {
    double start = 0, duration = 0;
    std::string nextSpell;
    SpellRole nextRole = SpellRole::Unknown;
};

struct CapWindow {
    double start = 0, duration = 0;
    std::vector<std::string> casts; // what was pressed while capped
};

struct Pull {
    int index = 0;
    double start = 0, end = 0;
    double busy = 0;
    double capTime = 0;
    double waste = 0;
    double uptime() const { return end > start ? busy / (end - start) : 0; }
};

struct Marker {
    double t = 0;
    std::string name;
};

// A suggestion. `id` selects the sentence (see report.cpp); a/b are its numbers.
struct Tip {
    std::string id;
    double a = 0, b = 0;
};

struct SpellCount {
    std::string name;
    SpellRole role = SpellRole::Unknown;
    int casts = 0;
    int canceled = 0;
    int atCap = 0;       // cast while Astral Power was full
    double gain = 0, waste = 0;
};

struct AnalysisResult {
    // identity
    std::string playerName, reportCode, reportTitle, fightName, link;
    bool dungeon = false;
    int keystoneLevel = 0;
    bool kill = false;
    double fightDuration = 0;

    // GCD / uptime
    double gcdEstimate = 0;        // median local GCD
    std::vector<Interval> busy;    // merged busy blocks
    std::vector<Interval> combat;  // combat windows (one per pull)
    std::vector<Pull> pulls;
    double busyTime = 0;           // inside combat windows
    double combatTime = 0;
    double uptimeFight = 0;        // busy / fight duration
    double uptimeCombat = 0;       // busy / combat time
    std::vector<Pause> pauses;     // pauseMin .. combatGap, inside combat
    double pauseTime = 0;
    int longPauses = 0;
    double smallDelayTime = 0;
    int smallDelays = 0;
    int gcdActions = 0;
    int canceledCasts = 0;
    double canceledTime = 0;

    // per role counts
    int spenders = 0, fillers = 0, dots = 0, cooldowns = 0, forms = 0, movement = 0;
    int furyOfElune = 0;
    double perMin(int n) const { return combatTime > 0 ? n / (combatTime / 60.0) : 0; }

    // Astral Power
    bool hasAp = false;
    std::string apConvention;
    std::vector<ApPoint> ap;
    double capTime = 0;
    std::vector<CapWindow> capWindows; // sorted longest first
    double longestCap = 0;
    double apGained = 0, apWasted = 0;
    double wastePct() const { return apGained > 0 ? 100.0 * apWasted / apGained : 0; }
    int castsAtCapNonSpender = 0;
    int dotsAtCap = 0;
    int formsAtCap = 0;
    int cooldownsAtCap = 0; // Incarnation/CA pressed while capped
    int fillersStartedHigh = 0;

    std::vector<SpellCount> spells;  // sorted by casts
    std::vector<Marker> markers;
    std::map<std::string, int> unclassified; // casts not in spells.json

    std::vector<Tip> tips; // ordered by importance
};

AnalysisResult analyze(const FightData& fd, const SpellBook& book, const AnalysisSettings& s);

// Value of the Astral Power step function just before time t.
double apAt(const std::vector<ApPoint>& ap, double t);

} // namespace ubr
