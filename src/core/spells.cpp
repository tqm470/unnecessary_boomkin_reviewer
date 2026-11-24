#include "spells.h"

#include <stdexcept>

#include "../../third_party/nlohmann/json.hpp"

namespace ubr {

using nlohmann::json;

// Default classification. Written to spells.json on first run so it can be
// edited without recompiling. IDs are from the Midnight-era client; names and
// aliases are the fallback when an ID changes.
static const char* kDefaultSpells = R"JSON({
  "version": 1,
  "astral_power_resource_type": 8,
  "spells": [
    {"name": "Starfire",        "ids": [194153],         "role": "filler",   "aliases": ["Fogo Estelar"]},
    {"name": "Wrath",           "ids": [190984, 5176],   "role": "filler",   "aliases": ["Ira"]},
    {"name": "Moonfire",        "ids": [8921],           "role": "dot",      "aliases": ["Fogo Lunar"]},
    {"name": "Sunfire",         "ids": [93402],          "role": "dot",      "aliases": ["Fogo Solar"]},
    {"name": "Stellar Flare",   "ids": [202347],         "role": "dot",      "aliases": ["Labareda Estelar"]},
    {"name": "Starsurge",       "ids": [78674],          "role": "spender",  "aliases": ["Surto Estelar"]},
    {"name": "Starfall",        "ids": [191034],         "role": "spender",  "aliases": ["Chuva Estelar"]},
    {"name": "Fury of Elune",   "ids": [202770],         "role": "cooldown", "aliases": ["Fúria de Eluna"], "marker": true},
    {"name": "New Moon",        "ids": [274281],         "role": "cooldown", "aliases": ["Lua Nova"]},
    {"name": "Half Moon",       "ids": [274282],         "role": "cooldown", "aliases": ["Meia-lua"]},
    {"name": "Full Moon",       "ids": [274283],         "role": "cooldown", "aliases": ["Lua Cheia"]},
    {"name": "Wild Mushroom",   "ids": [88747],          "role": "cooldown", "aliases": ["Cogumelo Selvagem"]},
    {"name": "Force of Nature", "ids": [205636],         "role": "cooldown", "aliases": ["Força da Natureza"]},
    {"name": "Convoke the Spirits", "ids": [391528, 323764], "role": "cooldown", "aliases": ["Evocar os Espíritos"], "marker": true},

    {"name": "Moonkin Form",    "ids": [24858],          "role": "form",     "aliases": ["Forma de Luniscante"]},
    {"name": "Cat Form",        "ids": [768],            "role": "form",     "aliases": ["Forma de Felino"]},
    {"name": "Bear Form",       "ids": [5487],           "role": "form",     "aliases": ["Forma de Urso"]},
    {"name": "Travel Form",     "ids": [783],            "role": "form",     "aliases": ["Forma de Viagem"]},
    {"name": "Tiger Dash",      "ids": [252216],         "role": "movement", "aliases": ["Arremetida do Tigre"]},
    {"name": "Dash",            "ids": [1850],           "role": "movement", "aliases": ["Arremetida"]},
    {"name": "Stampeding Roar", "ids": [106898, 77764, 77761], "role": "movement", "aliases": ["Estouro da Manada"]},
    {"name": "Wild Charge",     "ids": [102401, 102383], "role": "movement", "aliases": ["Investida Selvagem"]},

    {"name": "Typhoon",         "ids": [132469],         "role": "utility",  "aliases": ["Tufão"]},
    {"name": "Ursol's Vortex",  "ids": [102793],         "role": "utility",  "aliases": ["Vórtice de Ursol"]},
    {"name": "Mass Entanglement", "ids": [102359],       "role": "utility",  "aliases": ["Enredamento em Massa"]},
    {"name": "Incapacitating Roar", "ids": [99],         "role": "utility",  "aliases": ["Rugido Incapacitante"]},
    {"name": "Mighty Bash",     "ids": [5211],           "role": "utility",  "aliases": ["Pancada Poderosa"]},
    {"name": "Remove Corruption", "ids": [2782],         "role": "utility",  "aliases": ["Remover Corrupção"]},
    {"name": "Soothe",          "ids": [2908],           "role": "utility",  "aliases": ["Calmante"]},
    {"name": "Regrowth",        "ids": [8936],           "role": "utility",  "aliases": ["Recrescimento"]},
    {"name": "Rejuvenation",    "ids": [774],            "role": "utility",  "aliases": ["Rejuvenescer"]},
    {"name": "Swiftmend",       "ids": [18562],          "role": "utility",  "aliases": ["Recomposição"]},
    {"name": "Entangling Roots","ids": [339],            "role": "utility",  "aliases": ["Raízes Enredantes"]},
    {"name": "Cyclone",         "ids": [33786],          "role": "utility",  "aliases": ["Ciclone"]},
    {"name": "Hibernate",       "ids": [2637],           "role": "utility",  "aliases": ["Hibernar"]},
    {"name": "Rebirth",         "ids": [20484],          "role": "utility",  "aliases": ["Renascimento"]},
    {"name": "Mark of the Wild","ids": [1126],           "role": "utility",  "aliases": ["Marca do Indomado"]},
    {"name": "Innervate",       "ids": [29166],          "role": "utility",  "aliases": ["Estimular"]},

    {"name": "Incarnation: Chosen of Elune", "ids": [102560, 390414], "role": "offgcd", "aliases": ["Encarnação: Escolhido de Eluna"], "marker": true},
    {"name": "Celestial Alignment", "ids": [194223, 383410], "role": "offgcd", "aliases": ["Alinhamento Celestial"], "marker": true},
    {"name": "Barkskin",        "ids": [22812],          "role": "offgcd",   "aliases": ["Pele de Árvore"]},
    {"name": "Solar Beam",      "ids": [78675],          "role": "offgcd",   "aliases": ["Raio Solar"]},
    {"name": "Renewal",         "ids": [108238],         "role": "offgcd",   "aliases": ["Renovação"]},
    {"name": "Eclipse (Lunar)", "ids": [48518],          "role": "offgcd",   "aliases": ["Eclipse Lunar"]},
    {"name": "Eclipse (Solar)", "ids": [48517],          "role": "offgcd",   "aliases": ["Eclipse Solar"]}
  ]
})JSON";

const char* SpellBook::defaultJson() { return kDefaultSpells; }

std::string toLower(const std::string& s) {
    std::string out = s;
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    // trim
    size_t a = out.find_first_not_of(" \t");
    size_t b = out.find_last_not_of(" \t");
    if (a == std::string::npos) return {};
    return out.substr(a, b - a + 1);
}

static SpellRole parseRole(const std::string& r) {
    if (r == "filler") return SpellRole::Filler;
    if (r == "dot") return SpellRole::Dot;
    if (r == "spender") return SpellRole::Spender;
    if (r == "cooldown") return SpellRole::Cooldown;
    if (r == "form") return SpellRole::Form;
    if (r == "movement") return SpellRole::Movement;
    if (r == "utility") return SpellRole::Utility;
    if (r == "offgcd") return SpellRole::OffGcd;
    throw std::runtime_error("spells.json: unknown role '" + r + "'");
}

const char* roleName(SpellRole r) {
    switch (r) {
        case SpellRole::Filler: return "filler";
        case SpellRole::Dot: return "dot";
        case SpellRole::Spender: return "spender";
        case SpellRole::Cooldown: return "cooldown";
        case SpellRole::Form: return "form";
        case SpellRole::Movement: return "movement";
        case SpellRole::Utility: return "utility";
        case SpellRole::OffGcd: return "offgcd";
        default: return "unknown";
    }
}

SpellBook SpellBook::fromJson(const std::string& text) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("spells.json is not valid JSON: ") + e.what());
    }
    SpellBook book;
    book.astralPowerType_ = j.value("astral_power_resource_type", 8);
    if (!j.contains("spells") || !j["spells"].is_array())
        throw std::runtime_error("spells.json: missing \"spells\" array");
    for (const auto& s : j["spells"]) {
        SpellInfo info;
        info.name = s.value("name", "");
        if (info.name.empty()) throw std::runtime_error("spells.json: spell without a name");
        if (s.contains("ids"))
            for (const auto& id : s["ids"]) info.ids.push_back(id.get<int>());
        if (s.contains("aliases"))
            for (const auto& a : s["aliases"]) info.aliases.push_back(a.get<std::string>());
        info.role = parseRole(s.value("role", "utility"));
        info.marker = s.value("marker", false);
        size_t idx = book.spells_.size();
        book.spells_.push_back(info);
        for (int id : info.ids) book.byId_[id] = idx;
        book.byName_[toLower(info.name)] = idx;
        for (const auto& a : info.aliases) book.byName_[toLower(a)] = idx;
    }
    return book;
}

const SpellInfo* SpellBook::lookup(int abilityId, const std::string& name) const {
    auto it = byId_.find(abilityId);
    if (it != byId_.end()) return &spells_[it->second];
    if (!name.empty()) {
        auto jt = byName_.find(toLower(name));
        if (jt != byName_.end()) return &spells_[jt->second];
    }
    return nullptr;
}

bool SpellBook::onGcd(const SpellInfo* s) const {
    if (!s) return false;
    return s->role != SpellRole::OffGcd && s->role != SpellRole::Unknown;
}

} // namespace ubr
