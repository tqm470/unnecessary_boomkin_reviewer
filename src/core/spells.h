// Spell classification for Balance Druid.
// Spells are matched by ID first, then by name (English or any alias), so a
// patch that changes an ID does not silently break the analysis.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace ubr {

enum class SpellRole {
    Unknown,    // not in the config: treated as off-GCD, listed in diagnostics
    OffGcd,     // known, does not use the GCD (Incarnation, Barkskin, potions...)
    Filler,     // Starfire, Wrath
    Dot,        // Moonfire, Sunfire, Stellar Flare
    Spender,    // Starsurge, Starfall
    Cooldown,   // Fury of Elune, moons, Force of Nature, Convoke...
    Form,       // Moonkin / Cat / Bear / Travel Form
    Movement,   // Tiger Dash, Dash, Stampeding Roar, Wild Charge
    Utility,    // dispels, CC, heals, battle rez...
};

struct SpellInfo {
    std::string name;                 // canonical English name
    std::vector<int> ids;
    std::vector<std::string> aliases; // localized names etc.
    SpellRole role = SpellRole::Unknown;
    bool marker = false;              // drawn as a tick on the timeline
};

class SpellBook {
public:
    // Parses the JSON config. Throws std::runtime_error on malformed input.
    static SpellBook fromJson(const std::string& json);
    static const char* defaultJson();

    // Resolve a cast. `name` is the ability name from the report (may be empty).
    const SpellInfo* lookup(int abilityId, const std::string& name) const;

    bool onGcd(const SpellInfo* s) const;
    int astralPowerResourceType() const { return astralPowerType_; }

    const std::vector<SpellInfo>& all() const { return spells_; }

private:
    std::vector<SpellInfo> spells_;
    std::map<int, size_t> byId_;
    std::map<std::string, size_t> byName_; // lowercase
    int astralPowerType_ = 8;
};

const char* roleName(SpellRole r);
std::string toLower(const std::string& s);

} // namespace ubr
