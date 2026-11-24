// Warcraft Logs API v2 (GraphQL) access. Platform-neutral: the actual HTTPS
// transport is injected through HttpClient (WinHTTP on Windows, a fake in tests).
#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "model.h"

namespace ubr {

struct LinkInfo {
    bool ok = false;
    std::string error;
    std::string siteHost;   // host used for the API (www.warcraftlogs.com, classic..., ...)
    std::string code;       // report code
    std::string fight;      // "" (not given), "last", or a number
    int source = -1;        // actor id from the link, -1 if absent
};

LinkInfo parseLink(const std::string& url);

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string error; // transport error (no status)
};

using Headers = std::vector<std::pair<std::string, std::string>>;

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse post(const std::string& url, const Headers& headers,
                              const std::string& body) = 0;
};

// Optional raw-response cache, so re-analysing a log costs no API points.
struct ResponseCache {
    std::function<bool(const std::string& key, std::string& out)> get;
    std::function<void(const std::string& key, const std::string& value)> put;
};

struct ReportMeta {
    std::string code;
    std::string title;
    std::vector<Fight> fights;
    std::vector<Actor> players;
    std::map<int, std::string> abilityNames;
};

class WclClient {
public:
    WclClient(HttpClient& http, std::string clientId, std::string clientSecret,
              std::string siteHost = "www.warcraftlogs.com");

    void setCache(ResponseCache cache) { cache_ = std::move(cache); }
    // Called with short status lines ("Downloading casts, page 2...").
    void setProgress(std::function<void(const std::string&)> cb) { progress_ = std::move(cb); }

    ReportMeta loadReport(const std::string& code);
    // Fills Actor::spec for players in the fight when the report knows it.
    void loadSpecs(ReportMeta& meta, int fightId);
    // Downloads casts + resource events and builds FightData.
    FightData loadFight(const ReportMeta& meta, const Fight& fight, const Actor& player,
                        int astralPowerType);

    // Exposed for tests.
    std::string token();

private:
    std::string graphql(const std::string& query, const std::string& variablesJson,
                        const std::string& cacheKey);
    std::string events(const std::string& code, const Fight& fight, int sourceId,
                       const std::string& dataType, bool includeResources);

    HttpClient& http_;
    std::string id_, secret_, host_;
    std::string token_;
    ResponseCache cache_;
    std::function<void(const std::string&)> progress_;
};

// Pure helpers (tested directly).
ReportMeta parseReportMeta(const std::string& code, const std::string& responseJson);
void applyPlayerDetails(ReportMeta& meta, const std::string& responseJson);
const Fight* resolveFight(const ReportMeta& meta, const std::string& fightSpec);
// Balance Druids first (by spec), then other Druids, then everyone else.
std::vector<Actor> rankCandidates(const ReportMeta& meta);
FightData buildFightData(const ReportMeta& meta, const Fight& fight, const Actor& player,
                         const std::string& castEventsJson,     // JSON array
                         const std::string& resourceEventsJson, // JSON array
                         int astralPowerType);

std::string base64(const std::string& in);

} // namespace ubr
