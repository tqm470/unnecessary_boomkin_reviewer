// Core tests. Build natively (Linux/macOS/Windows): see CMakeLists.txt or build.sh.
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>

#include "../src/core/analysis.h"
#include "../src/core/demo.h"
#include "../src/core/report.h"
#include "../src/core/wcl.h"
#include "../third_party/nlohmann/json.hpp"

using namespace ubr;
using nlohmann::json;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (cond) ++g_pass;                                                         \
        else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)
#define NEAR(a, b, eps)                                                             \
    do {                                                                            \
        double _a = (a), _b = (b);                                                  \
        if (std::fabs(_a - _b) <= (eps)) ++g_pass;                                  \
        else { ++g_fail; std::printf("FAIL %s:%d  %s = %.4f, expected %.4f\n",      \
                                     __FILE__, __LINE__, #a, _a, _b); }             \
    } while (0)

// ---------------------------------------------------------------------------
// Hand-made raid fixture with answers worked out on paper.
//
//  0.0 Moonfire            busy [0,1]
//  1.0 Sunfire             busy [1,2]
//  2.0-3.5 Starfire        busy [2,3.5]        (+10 AP, 2 wasted -> cap at 3.5)
//  3.6 Starsurge           busy [3.6,4.6]      (-36 AP)
//  4.6-8.0 pause 3.4s, ends with Moonfire
//  8.0 Moonfire            busy [8,9]
//  9.0 begin Starfire, canceled by Cat Form at 9.6 (0.6s lost)
//  9.6 Cat Form            busy [9.6,10.6]
// 11.0 Starfall            busy [11,12]
// 12.0 Incarnation (off GCD)
// 12.0-13.5 Starfire       busy [12,13.5]
// 13.5-20 pause 6.5s
// busy = 3.5 + 1 + 1 + 1 + 2.5 = 9.0 of 20s -> 45%
// ---------------------------------------------------------------------------
struct Fx {
    json casts = json::array(), res = json::array();
    double base = 1000; // fight start (ms)
    int pid = 5;
    json cr(double ap, double cost = 0) {
        return json::array({{{"amount", ap * 10}, {"max", 1000}, {"type", 8}, {"cost", cost * 10}}});
    }
    void cast(double t, int id, double ap, double cost = 0) {
        casts.push_back({{"timestamp", base + t * 1000}, {"type", "cast"}, {"sourceID", pid},
                         {"abilityGameID", id}, {"classResources", cr(ap, cost)}});
    }
    void begin(double t, int id, double dur) {
        casts.push_back({{"timestamp", base + t * 1000}, {"type", "begincast"}, {"sourceID", pid},
                         {"abilityGameID", id}, {"duration", dur * 1000}});
    }
    void rc(double t, int id, double change, double waste, double after) {
        res.push_back({{"timestamp", base + t * 1000}, {"type", "resourcechange"}, {"sourceID", pid},
                       {"targetID", pid}, {"abilityGameID", id}, {"resourceChange", change},
                       {"resourceChangeType", 8}, {"waste", waste}, {"classResources", cr(after)}});
    }
};

static ReportMeta metaFor(int pid, double start, double end, int keystone) {
    ReportMeta m;
    m.code = "TEST";
    m.title = "Test";
    Fight f;
    f.id = 1;
    f.name = keystone ? "Dungeon" : "Boss";
    f.startMs = start;
    f.endMs = end;
    f.keystoneLevel = keystone;
    f.kill = true;
    m.fights.push_back(f);
    Actor a;
    a.id = pid;
    a.name = "Mekyla";
    a.cls = "Druid";
    a.spec = "Balance";
    m.players.push_back(a);
    // names deliberately in Portuguese for some: name fallback must not matter, IDs match
    m.abilityNames = {{8921, "Fogo Lunar"}, {93402, "Fogo Solar"}, {194153, "Fogo Estelar"},
                      {78674, "Surto Estelar"}, {191034, "Chuva Estelar"}, {768, "Forma de Felino"},
                      {102560, "Encarnação: Escolhido de Eluna"}, {424242, "Some Trinket"}};
    return m;
}

static void testRaidFixture() {
    Fx fx;
    fx.cast(0.0, 8921, 80);   fx.rc(0.0, 8921, 6, 0, 86);
    fx.cast(1.0, 93402, 86);  fx.rc(1.0, 93402, 6, 0, 92);
    fx.begin(2.0, 194153, 1.5);
    fx.cast(3.5, 194153, 92); fx.rc(3.5, 194153, 10, 2, 100);
    fx.cast(3.6, 78674, 100, 36);
    fx.cast(8.0, 8921, 64);   fx.rc(8.0, 8921, 6, 0, 70);
    fx.begin(9.0, 194153, 1.5);
    fx.cast(9.6, 768, 70);
    fx.cast(11.0, 191034, 70, 50);
    fx.cast(12.0, 102560, 20);
    fx.cast(12.0, 424242, 20); // unknown trinket, off GCD
    fx.begin(12.0, 194153, 1.5);
    fx.cast(13.5, 194153, 20); fx.rc(13.5, 194153, 10, 0, 30);

    ReportMeta meta = metaFor(5, 1000, 21000, 0);
    SpellBook book = SpellBook::fromJson(SpellBook::defaultJson());
    FightData fd = buildFightData(meta, meta.fights[0], meta.players[0], fx.casts.dump(),
                                  fx.res.dump(), book.astralPowerResourceType());
    CHECK(fd.apConvention == "after");
    AnalysisSettings st;
    AnalysisResult r = analyze(fd, book, st);

    NEAR(r.fightDuration, 20.0, 1e-9);
    NEAR(r.gcdEstimate, 1.0, 1e-6);
    NEAR(r.busyTime, 9.0, 1e-6);
    NEAR(r.uptimeCombat, 0.45, 1e-6);
    NEAR(r.uptimeFight, 0.45, 1e-6);
    CHECK(r.pulls.size() == 1);
    CHECK(r.pauses.size() == 2);
    NEAR(r.pauseTime, 9.9, 1e-6);
    CHECK(r.longPauses == 2);
    if (r.pauses.size() == 2) {
        NEAR(r.pauses[0].duration, 6.5, 1e-6); // sorted longest first
        NEAR(r.pauses[1].duration, 3.4, 1e-6);
        CHECK(r.pauses[1].nextSpell == "Moonfire");
        CHECK(r.pauses[1].nextRole == SpellRole::Dot);
    }
    CHECK(r.smallDelays == 3);
    NEAR(r.smallDelayTime, 1.1, 1e-6);
    CHECK(r.canceledCasts == 1);
    NEAR(r.canceledTime, 0.6, 1e-6);
    CHECK(r.gcdActions == 8);
    CHECK(r.spenders == 2);
    CHECK(r.dots == 3);
    CHECK(r.forms == 1);
    CHECK(r.unclassified.count("Some Trinket") == 1);
    CHECK(r.markers.size() == 1);

    // Astral Power
    CHECK(r.hasAp);
    NEAR(r.capTime, 0.1, 1e-6);
    CHECK(r.capWindows.size() == 1);
    NEAR(r.longestCap, 0.1, 1e-6);
    NEAR(r.apGained, 38, 1e-9);
    NEAR(r.apWasted, 2, 1e-9);
    NEAR(apAt(r.ap, 3.55), 100, 1e-9);
    NEAR(apAt(r.ap, 3.65), 64, 1e-9);
    NEAR(apAt(r.ap, 11.5), 20, 1e-9);
    CHECK(r.castsAtCapNonSpender == 0);
    CHECK(r.cooldownsAtCap == 0);
    if (!r.capWindows.empty()) {
        bool sawSurge = false;
        for (auto& c : r.capWindows[0].casts) sawSurge |= c == "Starsurge";
        CHECK(sawSurge);
    }
    // Report builds in both languages without blowing up.
    CHECK(!buildReportText(r, nullptr, Lang::En).empty());
    CHECK(buildReportText(r, &r, Lang::PtBr).find("Uptime real") != std::string::npos);
}

// "before" convention: classResources on resourcechange show the value prior to the gain.
static void testBeforeConvention() {
    Fx fx;
    fx.cast(0.0, 8921, 90);   fx.rc(0.0, 8921, 6, 0, 90);   // before = 90 -> after 96
    fx.cast(1.0, 93402, 96);  fx.rc(1.0, 93402, 6, 2, 96);  // before = 96 -> after 100, waste 2
    fx.cast(2.0, 8921, 100);  fx.rc(2.0, 8921, 6, 6, 100);  // dot at cap
    fx.cast(3.0, 78674, 100, 36);
    ReportMeta meta = metaFor(5, 1000, 6000, 0);
    SpellBook book = SpellBook::fromJson(SpellBook::defaultJson());
    FightData fd = buildFightData(meta, meta.fights[0], meta.players[0], fx.casts.dump(),
                                  fx.res.dump(), 8);
    CHECK(fd.apConvention == "before");
    AnalysisResult r = analyze(fd, book, AnalysisSettings{});
    NEAR(apAt(r.ap, 0.5), 96, 1e-9);
    NEAR(apAt(r.ap, 1.5), 100, 1e-9);
    NEAR(r.capTime, 2.0, 1e-6); // 1.0 -> 3.0
    CHECK(r.dotsAtCap == 1);    // the Moonfire at 2.0
}

// Dungeon: pulls split on long gaps, the gap between pulls is not a "pause".
static void testDungeonSplit() {
    Fx fx;
    for (double t : {0.0, 1.0, 2.0}) fx.cast(t, 8921, 50);
    for (double t : {30.0, 31.0}) fx.cast(t, 93402, 50);
    ReportMeta meta = metaFor(5, 1000, 1000 + 40000, 10);
    SpellBook book = SpellBook::fromJson(SpellBook::defaultJson());
    FightData fd = buildFightData(meta, meta.fights[0], meta.players[0], fx.casts.dump(), "[]", 8);
    AnalysisResult r = analyze(fd, book, AnalysisSettings{});
    CHECK(r.dungeon);
    CHECK(r.pulls.size() == 2);
    NEAR(r.combatTime, 5.0, 1e-6);
    NEAR(r.uptimeCombat, 1.0, 1e-6);
    CHECK(r.pauses.empty());
    NEAR(r.apGained, 0, 1e-9);
    AnalysisSettings whole;
    whole.segmentation = Segmentation::WholeFight;
    AnalysisResult w = analyze(fd, book, whole);
    CHECK(w.pulls.size() == 1);
    NEAR(w.combatTime, 40.0, 1e-6);
}

static void testLinks() {
    LinkInfo a = parseLink("https://br.warcraftlogs.com/reports/ChkxDYKML43QcWB7?fight=1&type=damage-done");
    CHECK(a.ok && a.code == "ChkxDYKML43QcWB7" && a.fight == "1" && a.source == -1);
    CHECK(a.siteHost == "www.warcraftlogs.com");
    LinkInfo b = parseLink("  <https://www.warcraftlogs.com/reports/WhwmptnLzYg4NKTP?fight=19&type=casts&source=182>  ");
    CHECK(b.ok && b.fight == "19" && b.source == 182);
    LinkInfo c = parseLink("https://www.warcraftlogs.com/reports/XMWLH16rDGBCPx3v#fight=last&type=damage-done");
    CHECK(c.ok && c.fight == "last");
    LinkInfo d = parseLink("https://discord.com/channels/@me/1/2");
    CHECK(!d.ok);
    LinkInfo e = parseLink("https://classic.warcraftlogs.com/reports/abc123");
    CHECK(e.ok && e.siteHost == "classic.warcraftlogs.com" && e.fight.empty());
    LinkInfo f = parseLink("https://www.warcraftlogs.com/reports/");
    CHECK(!f.ok);
    CHECK(base64("id:secret") == "aWQ6c2VjcmV0");
}

static void testSpellBook() {
    SpellBook book = SpellBook::fromJson(SpellBook::defaultJson());
    // ID that changed in a patch: name fallback, English or alias
    const SpellInfo* s = book.lookup(999999, "Starfire");
    CHECK(s && s->role == SpellRole::Filler);
    s = book.lookup(999999, "Chuva Estelar");
    CHECK(s && s->name == "Starfall");
    CHECK(book.lookup(999999, "Nope") == nullptr);
    CHECK(book.onGcd(book.lookup(768, "")));
    CHECK(!book.onGcd(book.lookup(102560, "")));
    bool threw = false;
    try { SpellBook::fromJson("{\"spells\":[{\"name\":\"X\",\"role\":\"weird\"}]}"); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

// Full client pipeline against a fake HTTP server, including pagination.
struct FakeHttp : HttpClient {
    std::deque<HttpResponse> replies;
    std::vector<std::string> bodies, urls;
    HttpResponse post(const std::string& url, const Headers&, const std::string& body) override {
        urls.push_back(url);
        bodies.push_back(body);
        if (replies.empty()) return {500, "no more replies", ""};
        HttpResponse r = replies.front();
        replies.pop_front();
        return r;
    }
};

static std::string wrapEvents(const json& data, json next) {
    return json{{"data", {{"reportData", {{"report", {{"events", {{"data", data}, {"nextPageTimestamp", next}}}}}}}}}}.dump();
}

static void testClientPipeline() {
    DemoData demo = makeDemo(true, 3);
    json casts = json::parse(demo.castsJson);
    size_t half = casts.size() / 2;
    json p1(casts.begin(), casts.begin() + half), p2(casts.begin() + half, casts.end());
    double nextTs = p2.front()["timestamp"].get<double>();

    FakeHttp http;
    http.replies.push_back({200, R"({"access_token":"tok","expires_in":3600})", ""});
    http.replies.push_back({200, demo.reportJson, ""});
    http.replies.push_back({200, R"({"data":{"reportData":{"report":{"playerDetails":{"data":{"playerDetails":{"dps":[{"name":"Sloppykin","id":7,"type":"Druid","specs":[{"spec":"Balance"}]}]}}}}}}})", ""});
    http.replies.push_back({200, wrapEvents(p1, nextTs), ""});
    http.replies.push_back({200, wrapEvents(p2, nullptr), ""});
    http.replies.push_back({200, wrapEvents(json::parse(demo.resourcesJson), nullptr), ""});

    std::map<std::string, std::string> store;
    WclClient c(http, "id", "secret");
    c.setCache({[&](const std::string& k, std::string& out) {
                    auto it = store.find(k);
                    if (it == store.end()) return false;
                    out = it->second;
                    return true;
                },
                [&](const std::string& k, const std::string& v) { store[k] = v; }});
    ReportMeta meta = c.loadReport("DEMO");
    c.loadSpecs(meta, 1);
    CHECK(meta.players.size() == 1 && meta.players[0].spec == "Balance");
    CHECK(rankCandidates(meta).front().name == "Sloppykin");
    const Fight* f = resolveFight(meta, "last");
    CHECK(f && f->id == 1);
    FightData fd = c.loadFight(meta, *f, meta.players[0], 8);
    CHECK(fd.casts.size() == casts.size());
    CHECK(http.urls.size() == 6);
    CHECK(http.urls[0] == "https://www.warcraftlogs.com/oauth/token");
    CHECK(http.bodies[4].find(std::to_string((long long)nextTs)) != std::string::npos);
    // Resource events only carry Astral Power snapshots when asked for them.
    CHECK(http.bodies[5].find("Resources") != std::string::npos);
    CHECK(http.bodies[5].find("includeResources: true") != std::string::npos);

    // Second load: served entirely from cache, no HTTP.
    FightData again = c.loadFight(meta, *f, meta.players[0], 8);
    CHECK(http.urls.size() == 6);
    CHECK(again.casts.size() == fd.casts.size());

    // Errors are readable
    FakeHttp bad;
    bad.replies.push_back({401, "{\"error\":\"invalid_client\"}", ""});
    WclClient c2(bad, "x", "y");
    bool msgOk = false;
    try { c2.loadReport("X"); } catch (const std::exception& e) {
        msgOk = std::string(e.what()).find("client ID") != std::string::npos;
    }
    CHECK(msgOk);
}

static void testDemoShape() {
    SpellBook book = SpellBook::fromJson(SpellBook::defaultJson());
    for (bool sloppy : {true, false}) {
        DemoData d = makeDemo(sloppy, 42);
        ReportMeta meta = parseReportMeta("DEMO", d.reportJson);
        FightData fd = buildFightData(meta, meta.fights[0], meta.players[0], d.castsJson,
                                      d.resourcesJson, 8);
        AnalysisResult r = analyze(fd, book, AnalysisSettings{});
        std::printf("demo %-6s uptime %.1f%%  cap %.1fs  waste %.1f%%  pauses %zu  dotsAtCap %d  forms %d  canceled %d\n",
                    sloppy ? "sloppy" : "tidy", r.uptimeCombat * 100, r.capTime, r.wastePct(),
                    r.pauses.size(), r.dotsAtCap, r.forms + r.movement, r.canceledCasts);
        CHECK(r.uptimeCombat > 0.3 && r.uptimeCombat <= 1.0);
        CHECK(fd.apConvention.rfind("after", 0) == 0);
        if (sloppy) {
            CHECK(r.wastePct() > 5);
            CHECK(r.longPauses >= 5);
        } else {
            CHECK(r.uptimeCombat > 0.82);
            CHECK(r.wastePct() < 5);
        }
    }
}

int main() {
    testLinks();
    testSpellBook();
    testRaidFixture();
    testBeforeConvention();
    testDungeonSplit();
    testClientPipeline();
    testDemoShape();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
