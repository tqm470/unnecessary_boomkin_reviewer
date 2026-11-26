#include "wcl.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

#include "../../third_party/nlohmann/json.hpp"

namespace ubr {

using nlohmann::json;

// ---------------------------------------------------------------- links ----

static std::string getParam(const std::string& qs, const std::string& key) {
    size_t pos = 0;
    while (pos < qs.size()) {
        size_t amp = qs.find_first_of("&#", pos);
        std::string part = qs.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == key) return part.substr(eq + 1);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
}

LinkInfo parseLink(const std::string& raw) {
    LinkInfo li;
    std::string url = raw;
    // trim whitespace and surrounding <> (Discord-style)
    while (!url.empty() && (std::isspace((unsigned char)url.back()) || url.back() == '>')) url.pop_back();
    size_t s = 0;
    while (s < url.size() && (std::isspace((unsigned char)url[s]) || url[s] == '<')) ++s;
    url = url.substr(s);

    std::string rest = url;
    size_t scheme = rest.find("://");
    if (scheme != std::string::npos) rest = rest.substr(scheme + 3);
    size_t slash = rest.find('/');
    std::string host = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path = slash == std::string::npos ? "" : rest.substr(slash);
    std::string hostLower;
    for (char c : host) hostLower += (char)std::tolower((unsigned char)c);

    if (hostLower.find("warcraftlogs.com") == std::string::npos) {
        li.error = "Not a Warcraft Logs link.";
        return li;
    }
    // Regional/language subdomains (br., de., ...) all share the retail API.
    // Classic-family sites have their own.
    li.siteHost = "www.warcraftlogs.com";
    for (const char* sub : {"classic.", "fresh.", "sod.", "vanilla."})
        if (hostLower.rfind(sub, 0) == 0) li.siteHost = hostLower;

    size_t rep = path.find("/reports/");
    if (rep == std::string::npos) {
        li.error = "The link has no /reports/<code> part.";
        return li;
    }
    std::string after = path.substr(rep + 9);
    size_t end = after.find_first_of("/?#");
    li.code = after.substr(0, end);
    if (li.code.empty() || !std::all_of(li.code.begin(), li.code.end(),
                                        [](char c) { return std::isalnum((unsigned char)c); })) {
        li.error = "Could not read the report code from the link.";
        return li;
    }
    std::string qs;
    size_t q = path.find_first_of("?#");
    if (q != std::string::npos) qs = path.substr(q + 1);
    li.fight = getParam(qs, "fight");
    std::string src = getParam(qs, "source");
    if (!src.empty() && std::all_of(src.begin(), src.end(), ::isdigit)) li.source = std::stoi(src);
    li.ok = true;
    return li;
}

// --------------------------------------------------------------- base64 ----

std::string base64(const std::string& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(tbl[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(tbl[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

// --------------------------------------------------------------- client ----

WclClient::WclClient(HttpClient& http, std::string clientId, std::string clientSecret,
                     std::string siteHost)
    : http_(http), id_(std::move(clientId)), secret_(std::move(clientSecret)),
      host_(std::move(siteHost)) {}

static std::string describeHttpError(const HttpResponse& r, const std::string& what) {
    if (!r.error.empty()) return what + ": " + r.error;
    std::string msg = what + " failed (HTTP " + std::to_string(r.status) + ")";
    if (r.status == 401) msg += ". Check the API client ID and secret in Settings.";
    if (r.status == 429) msg += ". Warcraft Logs rate limit reached; try again later.";
    std::string snippet = r.body.substr(0, 200);
    if (!snippet.empty()) msg += "\n" + snippet;
    return msg;
}

std::string WclClient::token() {
    if (!token_.empty()) return token_;
    if (id_.empty() || secret_.empty())
        throw std::runtime_error("No Warcraft Logs API credentials. Add them in Settings.");
    if (progress_) progress_("Signing in to the Warcraft Logs API...");
    HttpResponse r = http_.post("https://" + host_ + "/oauth/token",
                                {{"Authorization", "Basic " + base64(id_ + ":" + secret_)},
                                 {"Content-Type", "application/x-www-form-urlencoded"}},
                                "grant_type=client_credentials");
    if (r.status != 200) throw std::runtime_error(describeHttpError(r, "Sign-in"));
    json j = json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.contains("access_token"))
        throw std::runtime_error("Sign-in returned an unexpected response.");
    token_ = j["access_token"].get<std::string>();
    return token_;
}

std::string WclClient::graphql(const std::string& query, const std::string& variablesJson,
                               const std::string& cacheKey) {
    std::string cached;
    if (!cacheKey.empty() && cache_.get && cache_.get(cacheKey, cached)) return cached;

    json body;
    body["query"] = query;
    body["variables"] = json::parse(variablesJson);
    HttpResponse r = http_.post("https://" + host_ + "/api/v2/client",
                                {{"Authorization", "Bearer " + token()},
                                 {"Content-Type", "application/json"},
                                 {"Accept", "application/json"}},
                                body.dump());
    if (r.status != 200) throw std::runtime_error(describeHttpError(r, "API request"));
    json j = json::parse(r.body, nullptr, false);
    if (j.is_discarded()) throw std::runtime_error("The API returned something that is not JSON.");
    if (j.contains("errors") && j["errors"].is_array() && !j["errors"].empty()) {
        std::string msg = j["errors"][0].value("message", "unknown error");
        if (msg.find("permission") != std::string::npos || msg.find("private") != std::string::npos)
            msg += " (private reports are not supported yet)";
        throw std::runtime_error("Warcraft Logs: " + msg);
    }
    if (!cacheKey.empty() && cache_.put) cache_.put(cacheKey, r.body);
    return r.body;
}

static const char* kReportQuery = R"GQL(
query($code: String!) {
  reportData {
    report(code: $code) {
      title
      fights { id name startTime endTime kill encounterID keystoneLevel difficulty }
      masterData {
        actors(type: "Player") { id name type subType server }
        abilities { gameID name }
      }
    }
  }
})GQL";

static const char* kDetailsQuery = R"GQL(
query($code: String!, $fight: Int!) {
  reportData { report(code: $code) { playerDetails(fightIDs: [$fight]) } }
})GQL";

ReportMeta WclClient::loadReport(const std::string& code) {
    if (progress_) progress_("Loading report " + code + "...");
    json vars = {{"code", code}};
    // Meta is not cached: a live log keeps adding fights.
    std::string body = graphql(kReportQuery, vars.dump(), "");
    return parseReportMeta(code, body);
}

void WclClient::loadSpecs(ReportMeta& meta, int fightId) {
    json vars = {{"code", meta.code}, {"fight", fightId}};
    try {
        std::string body = graphql(kDetailsQuery, vars.dump(),
                                   "details_" + meta.code + "_" + std::to_string(fightId));
        applyPlayerDetails(meta, body);
    } catch (const std::exception&) {
        // Specs are a convenience for picking the player; never fatal.
    }
}

std::string WclClient::events(const std::string& code, const Fight& fight, int sourceId,
                              const std::string& dataType, bool includeResources) {
    std::string key = "events2_" + code + "_" + std::to_string(fight.id) + "_" +
                      std::to_string(sourceId) + "_" + dataType;
    std::string cached;
    if (cache_.get && cache_.get(key, cached)) return cached;

    const std::string query = std::string(R"GQL(
query($code: String!, $fight: Int!, $source: Int!, $start: Float!, $end: Float!) {
  reportData { report(code: $code) {
    events(fightIDs: [$fight], sourceID: $source, startTime: $start, endTime: $end,
           dataType: )GQL") + dataType + (includeResources ? ", includeResources: true" : "") +
                              R"GQL(, limit: 10000) { data nextPageTimestamp }
  } }
})GQL";

    json all = json::array();
    double start = fight.startMs;
    for (int page = 1; page <= 200; ++page) {
        if (progress_)
            progress_("Downloading " + dataType + " events, page " + std::to_string(page) + "...");
        json vars = {{"code", code}, {"fight", fight.id}, {"source", sourceId},
                     {"start", start}, {"end", fight.endMs}};
        json j = json::parse(graphql(query, vars.dump(), ""));
        const json& ev = j["data"]["reportData"]["report"]["events"];
        if (ev.contains("data") && ev["data"].is_array())
            for (const auto& e : ev["data"]) all.push_back(e);
        if (!ev.contains("nextPageTimestamp") || ev["nextPageTimestamp"].is_null()) break;
        double next = ev["nextPageTimestamp"].get<double>();
        if (next <= start) break;
        start = next;
    }
    std::string out = all.dump();
    if (cache_.put) cache_.put(key, out);
    return out;
}

FightData WclClient::loadFight(const ReportMeta& meta, const Fight& fight, const Actor& player,
                               int astralPowerType) {
    std::string casts = events(meta.code, fight, player.id, "Casts", true);
    // Without includeResources, resource events carry no Astral Power snapshot.
    std::string res = events(meta.code, fight, player.id, "Resources", true);
    return buildFightData(meta, fight, player, casts, res, astralPowerType);
}

// --------------------------------------------------------------- parsing ----

static double num(const json& j, const char* key, double def = 0) {
    if (!j.contains(key) || j[key].is_null()) return def;
    if (j[key].is_number()) return j[key].get<double>();
    return def;
}
static std::string str(const json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_string()) return {};
    return j[key].get<std::string>();
}

ReportMeta parseReportMeta(const std::string& code, const std::string& responseJson) {
    json j = json::parse(responseJson);
    const json& rep = j["data"]["reportData"]["report"];
    if (rep.is_null()) throw std::runtime_error("Report " + code + " was not found (or is private).");
    ReportMeta m;
    m.code = code;
    m.title = str(rep, "title");
    for (const auto& f : rep["fights"]) {
        Fight fi;
        fi.id = (int)num(f, "id");
        fi.name = str(f, "name");
        fi.startMs = num(f, "startTime");
        fi.endMs = num(f, "endTime");
        fi.kill = f.contains("kill") && f["kill"].is_boolean() && f["kill"].get<bool>();
        fi.encounterId = (int)num(f, "encounterID");
        fi.keystoneLevel = (int)num(f, "keystoneLevel");
        fi.difficulty = (int)num(f, "difficulty");
        m.fights.push_back(fi);
    }
    const json& md = rep["masterData"];
    if (md.contains("actors"))
        for (const auto& a : md["actors"]) {
            Actor ac;
            ac.id = (int)num(a, "id");
            ac.name = str(a, "name");
            ac.cls = str(a, "subType");
            ac.server = str(a, "server");
            m.players.push_back(ac);
        }
    if (md.contains("abilities"))
        for (const auto& a : md["abilities"]) m.abilityNames[(int)num(a, "gameID")] = str(a, "name");
    return m;
}

// playerDetails is an opaque JSON blob; walk it and pick up anything that
// looks like {id, specs:[{spec}]} or {id, icon:"Druid-Balance"}.
static void walkDetails(const json& j, std::map<int, std::string>& specs, std::map<int, bool>& seen) {
    if (j.is_object()) {
        if (j.contains("id") && j["id"].is_number_integer() && j.contains("name") &&
            j.contains("type") && j["type"].is_string()) {
            int id = j["id"].get<int>();
            std::string spec;
            if (j.contains("specs") && j["specs"].is_array() && !j["specs"].empty()) {
                const auto& s0 = j["specs"][0];
                if (s0.is_object()) spec = str(s0, "spec");
                else if (s0.is_string()) spec = s0.get<std::string>();
            }
            if (spec.empty()) {
                std::string icon = str(j, "icon");
                size_t dash = icon.find('-');
                if (dash != std::string::npos) spec = icon.substr(dash + 1);
            }
            if (!spec.empty()) specs[id] = spec;
            seen[id] = true;
        }
        for (auto it = j.begin(); it != j.end(); ++it) walkDetails(it.value(), specs, seen);
    } else if (j.is_array()) {
        for (const auto& e : j) walkDetails(e, specs, seen);
    }
}

void applyPlayerDetails(ReportMeta& meta, const std::string& responseJson) {
    json j = json::parse(responseJson, nullptr, false);
    if (j.is_discarded()) return;
    json pd = j["data"]["reportData"]["report"]["playerDetails"];
    if (pd.is_string()) pd = json::parse(pd.get<std::string>(), nullptr, false);
    std::map<int, std::string> specs;
    std::map<int, bool> seen;
    walkDetails(pd, specs, seen);
    for (auto& p : meta.players) {
        auto it = specs.find(p.id);
        if (it != specs.end()) p.spec = it->second;
    }
    // Keep only players who were in the fight, when we learned that.
    if (!seen.empty()) {
        std::vector<Actor> kept;
        for (auto& p : meta.players)
            if (seen.count(p.id)) kept.push_back(p);
        if (!kept.empty()) meta.players = kept;
    }
}

const Fight* resolveFight(const ReportMeta& meta, const std::string& spec) {
    if (meta.fights.empty()) return nullptr;
    if (spec.empty() || spec == "last") {
        const Fight* best = &meta.fights.front();
        for (const auto& f : meta.fights)
            if (f.endMs >= best->endMs) best = &f;
        return best;
    }
    try {
        int id = std::stoi(spec);
        for (const auto& f : meta.fights)
            if (f.id == id) return &f;
    } catch (...) {
    }
    return nullptr;
}

std::vector<Actor> rankCandidates(const ReportMeta& meta) {
    std::vector<Actor> out = meta.players;
    auto rank = [](const Actor& a) {
        if (a.cls == "Druid" && a.spec == "Balance") return 0;
        if (a.cls == "Druid" && a.spec.empty()) return 1;
        if (a.cls == "Druid") return 2;
        return 3;
    };
    std::stable_sort(out.begin(), out.end(), [&](const Actor& a, const Actor& b) {
        int ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        return a.name < b.name;
    });
    return out;
}

// Reads Astral Power from an event's classResources (real units).
static bool readAp(const json& e, int apType, double& amount, double& max, double& cost) {
    if (!e.contains("classResources") || !e["classResources"].is_array()) return false;
    for (const auto& r : e["classResources"]) {
        if ((int)num(r, "type", -1) != apType) continue;
        double mx = num(r, "max", 100);
        // WCL stores Astral Power in tenths (max 1000). Normalise to real units.
        double scale = mx >= 200 ? 10.0 : 1.0;
        amount = num(r, "amount") / scale;
        max = mx / scale;
        cost = num(r, "cost") / scale;
        return true;
    }
    return false;
}

FightData buildFightData(const ReportMeta& meta, const Fight& fight, const Actor& player,
                         const std::string& castEventsJson, const std::string& resourceEventsJson,
                         int apType) {
    FightData fd;
    fd.reportCode = meta.code;
    fd.reportTitle = meta.title;
    fd.fight = fight;
    fd.player = player;
    fd.abilityNames = meta.abilityNames;
    auto rel = [&](double ms) { return (ms - fight.startMs) / 1000.0; };

    json casts = json::parse(castEventsJson);
    for (const auto& e : casts) {
        if ((int)num(e, "sourceID", -1) != player.id) continue;
        std::string type = str(e, "type");
        if (type != "cast" && type != "begincast") continue;
        RawCast c;
        c.t = rel(num(e, "timestamp"));
        c.abilityId = (int)num(e, "abilityGameID");
        c.begin = type == "begincast";
        c.durationMs = num(e, "duration");
        fd.casts.push_back(c);

        double amount, max, cost;
        if (!c.begin && readAp(e, apType, amount, max, cost)) {
            ApPoint p;
            p.t = c.t;
            p.max = max;
            // On casts, the amount is the state before the cost is paid.
            p.value = cost > 0 && amount - cost >= 0 ? amount - cost : amount;
            p.order = 0;
            fd.ap.push_back(p);
        }
    }

    json res = json::parse(resourceEventsJson);
    struct RC { double t; double amount, max; bool hasAmount; double change, waste; int ability; };
    std::vector<RC> rcs;
    for (const auto& e : res) {
        if (str(e, "type") != "resourcechange") continue;
        if ((int)num(e, "resourceChangeType", -1) != apType) continue;
        int target = (int)num(e, "targetID", player.id);
        if (target != player.id) continue;
        RC r{};
        r.t = rel(num(e, "timestamp"));
        double cost;
        r.hasAmount = readAp(e, apType, r.amount, r.max, cost);
        r.change = num(e, "resourceChange");
        r.waste = num(e, "waste");
        r.ability = (int)num(e, "abilityGameID");
        rcs.push_back(r);
        fd.gains.push_back({r.t, r.ability, r.change, r.waste});
    }

    // Is classResources on resourcechange the value before or after the gain?
    // Any event that wasted power must end at max, so test both readings on those.
    int afterVotes = 0, beforeVotes = 0;
    for (const auto& r : rcs) {
        // Fully wasted gains read "max" either way, so they cannot vote.
        if (!r.hasAmount || r.waste <= 0 || r.change <= r.waste) continue;
        if (r.amount >= r.max - 0.5) ++afterVotes;
        else ++beforeVotes;
    }
    bool after = afterVotes >= beforeVotes;
    fd.apConvention = after ? "after" : "before";
    if (afterVotes + beforeVotes == 0) fd.apConvention = "after (assumed)";

    for (const auto& r : rcs) {
        if (!r.hasAmount) continue;
        ApPoint p;
        p.t = r.t;
        p.max = r.max;
        p.value = after ? r.amount : std::min(r.max, r.amount + r.change - r.waste);
        p.order = 1;
        fd.ap.push_back(p);
    }
    std::stable_sort(fd.ap.begin(), fd.ap.end(), [](const ApPoint& a, const ApPoint& b) {
        if (a.t != b.t) return a.t < b.t;
        return a.order < b.order;
    });
    std::stable_sort(fd.casts.begin(), fd.casts.end(),
                     [](const RawCast& a, const RawCast& b) { return a.t < b.t; });
    return fd;
}

} // namespace ubr
