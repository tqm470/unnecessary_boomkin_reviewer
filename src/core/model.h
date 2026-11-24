// Plain data passed between the Warcraft Logs layer and the analysis.
// All times are in seconds, relative to the start of the fight.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace ubr {

struct Fight {
    int id = 0;
    std::string name;
    double startMs = 0, endMs = 0; // report-relative milliseconds
    bool kill = false;
    int encounterId = 0;
    int keystoneLevel = 0;
    int difficulty = 0;
    double durationSec() const { return (endMs - startMs) / 1000.0; }
    bool isDungeon() const { return keystoneLevel > 0; }
};

struct Actor {
    int id = 0;
    std::string name;
    std::string cls;     // "Druid"
    std::string server;
    std::string spec;    // "Balance" when known
};

struct RawCast {
    double t = 0;        // event time
    int abilityId = 0;
    bool begin = false;  // begincast vs cast
    double durationMs = 0; // begincast only, when the log provides it
};

struct ApPoint {
    double t = 0;
    double value = 0;    // Astral Power, 0..max (real units, not tenths)
    double max = 100;
    int order = 0;       // tie-breaker for equal timestamps
};

struct ApGain {
    double t = 0;
    int abilityId = 0;
    double gain = 0;     // raw gain
    double waste = 0;    // overcap
};

struct FightData {
    std::string reportCode;
    std::string reportTitle;
    std::string link;
    Fight fight;
    Actor player;
    std::vector<RawCast> casts;
    std::vector<ApPoint> ap;
    std::vector<ApGain> gains;
    std::map<int, std::string> abilityNames;
    std::string apConvention; // diagnostics: how resource events were read

    std::string abilityName(int id) const {
        auto it = abilityNames.find(id);
        return it == abilityNames.end() ? ("#" + std::to_string(id)) : it->second;
    }
};

} // namespace ubr
