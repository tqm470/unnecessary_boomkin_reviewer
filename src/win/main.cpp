// Unnecessary Boomkin Reviewer - Win32 front end.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <memory>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../core/analysis.h"
#include "../core/demo.h"
#include "../core/report.h"
#include "../core/wcl.h"
#include "graph.h"
#include "http_win.h"
#include "winutil.h"

using namespace ubr;

namespace {

enum : int {
    ID_LINK = 100, ID_CMP, ID_COMBO_A, ID_COMBO_B, ID_ANALYZE, ID_DEMO, ID_COPY, ID_FOLDER,
    ID_SETTINGS, ID_TAB, ID_STATUS, ID_LV_SUMMARY, ID_TIPS, ID_TIMELINE, ID_LV_PAUSES,
    ID_LV_CAP, ID_LV_SPELLS, ID_LV_PULLS,
    // settings window
    ID_S_ID = 300, ID_S_SECRET, ID_S_LANG, ID_S_GCD, ID_S_GAP, ID_S_SAVE, ID_S_CANCEL,
};
const UINT WM_APP_STATUS = WM_APP + 1;
const UINT WM_APP_DONE = WM_APP + 2;

struct Slot {
    std::string code, fightSpec;
    std::vector<Actor> candidates;
    int chosenId = -1;
};

struct Job {
    std::string links[2];
    Slot slots[2];
    std::string clientId, secret, spellJson;
    AnalysisSettings settings;
};

struct JobResult {
    std::string error;
    std::unique_ptr<AnalysisResult> res[2];
    Slot slots[2];
    bool ambiguous = false;
};

struct App {
    HINSTANCE inst = nullptr;
    HWND wnd = nullptr;
    HWND lblLink, editLink, lblCmp, editCmp, lblPlayer, comboA, lblVs, comboB;
    HWND btnAnalyze, btnDemo, btnCopy, btnFolder, btnSettings, tab, status;
    HWND lvSummary, editTips, timeline, lvPauses, lvCap, lvSpells, lvPulls;
    HFONT font = nullptr;
    UINT dpi = 96;
    AppSettings settings;
    std::string spellJson;
    SpellBook book;
    std::unique_ptr<AnalysisResult> res[2];
    Slot slots[2];
    bool working = false;
    bool demo = false;
} g;

double S(double v) { return v * g.dpi / 96.0; }
int Si(double v) { return (int)std::lround(S(v)); }
std::wstring T(const char* key) { return widen(tr(g.settings.lang, key)); }

UINT windowDpi(HWND w) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = (Fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    return fn ? fn(w) : 96;
}

std::wstring getText(HWND w) {
    int n = GetWindowTextLengthW(w);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(w, &s[0], n + 1);
    s.resize(n);
    return s;
}

void setStatus(const std::wstring& s) { SendMessageW(g.status, SB_SETTEXTW, 0, (LPARAM)s.c_str()); }

template <class T>
void postOwned(HWND w, UINT msg, T* p) {
    if (!PostMessageW(w, msg, 0, (LPARAM)p)) delete p;
}

// ------------------------------------------------------------------ cache --
std::wstring cachePath(const std::string& key) {
    std::string safe;
    for (char c : key) safe += (isalnum((unsigned char)c) || c == '_' || c == '-') ? c : '_';
    return cacheDir() + L"\\" + widen(safe) + L".json";
}

ResponseCache diskCache() {
    return {[](const std::string& k, std::string& out) { return readFile(cachePath(k), out); },
            [](const std::string& k, const std::string& v) { writeFile(cachePath(k), v); }};
}

// ----------------------------------------------------------------- worker --
std::string actorLabel(const Actor& a) {
    std::string s = a.name;
    if (!a.spec.empty() || !a.cls.empty()) s += "  (" + a.spec + (a.spec.empty() ? "" : " ") + a.cls + ")";
    return s;
}

void runJob(HWND wnd, Job job) {
    auto* out = new JobResult();
    try {
        SpellBook book = SpellBook::fromJson(job.spellJson);
        WinHttpClient http;
        for (int i = 0; i < 2; ++i) {
            if (job.links[i].empty()) continue;
            LinkInfo li = parseLink(job.links[i]);
            if (!li.ok) throw std::runtime_error((i ? "Comparison link: " : "Link: ") + li.error);
            WclClient client(http, job.clientId, job.secret, li.siteHost);
            client.setCache(diskCache());
            client.setProgress([wnd, i](const std::string& m) {
                postOwned(wnd, WM_APP_STATUS, new std::wstring(widen((i ? "[2] " : "") + m)));
            });
            ReportMeta meta = client.loadReport(li.code);
            const Fight* fight = resolveFight(meta, li.fight);
            if (!fight) throw std::runtime_error("Fight '" + li.fight + "' was not found in report " + li.code + ".");
            client.loadSpecs(meta, fight->id);
            std::vector<Actor> cands = rankCandidates(meta);
            if (cands.empty()) throw std::runtime_error("No players found in that fight.");

            const Slot& prev = job.slots[i];
            int pick = -1;
            bool same = prev.code == li.code && prev.fightSpec == li.fight;
            if (same && prev.chosenId >= 0) pick = prev.chosenId;
            else if (li.source >= 0) pick = li.source;
            auto found = std::find_if(cands.begin(), cands.end(), [&](const Actor& a) { return a.id == pick; });
            if (found == cands.end()) {
                found = cands.begin();
                int balance = (int)std::count_if(cands.begin(), cands.end(),
                                                 [](const Actor& a) { return a.spec == "Balance"; });
                if (balance > 1 || (balance == 0 && cands.front().cls != "Druid")) out->ambiguous = true;
            }
            out->slots[i].code = li.code;
            out->slots[i].fightSpec = li.fight;
            out->slots[i].candidates = cands;
            out->slots[i].chosenId = found->id;

            FightData fd = client.loadFight(meta, *fight, *found, book.astralPowerResourceType());
            fd.link = job.links[i];
            out->res[i].reset(new AnalysisResult(analyze(fd, book, job.settings)));
        }
    } catch (const std::exception& e) {
        out->error = e.what();
    }
    postOwned(wnd, WM_APP_DONE, out);
}

// ----------------------------------------------------------------- lists --
void lvColumns(HWND lv, const std::vector<std::pair<std::wstring, int>>& cols) {
    while (ListView_DeleteColumn(lv, 0)) {}
    for (size_t i = 0; i < cols.size(); ++i) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = i == 0 ? LVCFMT_LEFT : LVCFMT_LEFT;
        c.cx = Si(cols[i].second);
        c.pszText = const_cast<wchar_t*>(cols[i].first.c_str());
        ListView_InsertColumn(lv, (int)i, &c);
    }
}

void lvRow(HWND lv, const std::vector<std::wstring>& cells) {
    LVITEMW it = {};
    it.mask = LVIF_TEXT;
    it.iItem = ListView_GetItemCount(lv);
    it.pszText = const_cast<wchar_t*>(cells[0].c_str());
    int row = ListView_InsertItem(lv, &it);
    for (size_t i = 1; i < cells.size(); ++i)
        ListView_SetItemText(lv, row, (int)i, const_cast<wchar_t*>(cells[i].c_str()));
}

void render() {
    Lang L = g.settings.lang;
    const AnalysisResult* a = g.res[0].get();
    const AnalysisResult* b = g.res[1].get();
    for (HWND lv : {g.lvSummary, g.lvPauses, g.lvCap, g.lvSpells, g.lvPulls}) {
        SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(lv);
    }
    std::wstring you = a ? widen(a->playerName) : T("you");
    std::wstring other = b ? widen(b->playerName) : T("other");

    // summary
    std::vector<std::pair<std::wstring, int>> cols = {{T("col_metric"), 300}, {you, 220}};
    if (b) cols.push_back({other, 220});
    lvColumns(g.lvSummary, cols);
    if (a)
        for (const auto& r : summaryRows(*a, b, L)) {
            std::vector<std::wstring> cells = {widen(r.label), widen(r.a)};
            if (b) cells.push_back(widen(r.b));
            lvRow(g.lvSummary, cells);
        }

    std::wstring tips;
    if (a) {
        tips = T("tips") + L"\r\n";
        if (a->tips.empty()) tips += L"  -\r\n";
        int i = 1;
        for (const auto& t : a->tips) tips += std::to_wstring(i++) + L". " + widen(formatTip(t, L)) + L"\r\n";
        if (!a->unclassified.empty()) {
            tips += L"\r\n" + T("m_unclassified") + L": ";
            bool first = true;
            for (const auto& kv : a->unclassified) {
                tips += (first ? L"" : L", ") + widen(kv.first) + L" x" + std::to_wstring(kv.second);
                first = false;
            }
            tips += L"\r\n";
        }
        if (g.demo) tips += L"\r\n" + T("demo_note") + L"\r\n";
    }
    SetWindowTextW(g.editTips, tips.c_str());

    auto who = [&](int i) { return i == 0 ? you : other; };
    const AnalysisResult* rs[2] = {a, b};
    bool two = b != nullptr;
    auto withWho = [&](std::vector<std::pair<std::wstring, int>> c) {
        if (two) c.insert(c.begin(), {L"", 110});
        return c;
    };

    lvColumns(g.lvPauses, withWho({{T("col_time"), 90}, {T("col_duration"), 90}, {T("col_next"), 260}}));
    lvColumns(g.lvCap, withWho({{T("col_time"), 90}, {T("col_duration"), 90}, {T("col_casts_capped"), 700}}));
    lvColumns(g.lvSpells, withWho({{T("col_spell"), 220}, {T("col_role"), 110}, {T("col_casts"), 70},
                                   {T("col_canceled"), 80}, {T("col_at_cap"), 70}, {T("col_gain"), 90},
                                   {T("col_waste"), 110}}));
    lvColumns(g.lvPulls, withWho({{T("col_pull"), 60}, {T("col_start"), 80}, {T("col_duration"), 80},
                                  {T("col_uptime"), 80}, {T("col_cap"), 100}, {T("col_waste"), 110}}));

    for (int i = 0; i < 2; ++i) {
        const AnalysisResult* r = rs[i];
        if (!r) continue;
        auto row = [&](std::vector<std::wstring> cells) {
            if (two) cells.insert(cells.begin(), who(i));
            return cells;
        };
        for (const auto& p : r->pauses)
            lvRow(g.lvPauses, row({widen(fmtClock(p.start)), widen(fmtSec(p.duration, L)), widen(p.nextSpell)}));
        for (const auto& c : r->capWindows) {
            std::string casts;
            for (size_t k = 0; k < c.casts.size(); ++k) casts += (k ? ", " : "") + c.casts[k];
            lvRow(g.lvCap, row({widen(fmtClock(c.start)), widen(fmtSec(c.duration, L)), widen(casts)}));
        }
        for (const auto& s : r->spells) {
            if (s.casts == 0 && s.canceled == 0 && s.gain == 0) continue;
            lvRow(g.lvSpells, row({widen(s.name), widen(roleLabel(s.role, L)), std::to_wstring(s.casts),
                                   std::to_wstring(s.canceled), std::to_wstring(s.atCap),
                                   widen(fmtNum(s.gain, 0, L)), widen(fmtNum(s.waste, 0, L))}));
        }
        for (const auto& kv : r->unclassified)
            lvRow(g.lvSpells, row({widen(kv.first), widen(roleLabel(SpellRole::Unknown, L)),
                                   std::to_wstring(kv.second), L"0", L"0", L"0", L"0"}));
        for (const auto& p : r->pulls)
            lvRow(g.lvPulls, row({std::to_wstring(p.index), widen(fmtClock(p.start)),
                                  widen(fmtSec(p.end - p.start, L)), widen(fmtPct(p.uptime(), L)),
                                  widen(fmtSec(p.capTime, L)), widen(fmtNum(p.waste, 0, L))}));
    }
    for (HWND lv : {g.lvSummary, g.lvPauses, g.lvCap, g.lvSpells, g.lvPulls}) {
        SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(lv, nullptr, TRUE);
    }
    timelineSet(g.timeline, a, b, L);
    EnableWindow(g.btnCopy, a != nullptr);
}

void fillCombo(HWND combo, const Slot& s) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i = 0; i < s.candidates.size(); ++i) {
        std::wstring label = widen(actorLabel(s.candidates[i]));
        int idx = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        SendMessageW(combo, CB_SETITEMDATA, idx, s.candidates[i].id);
        if (s.candidates[i].id == s.chosenId) sel = idx;
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

// ---------------------------------------------------------------- layout --
void showPage() {
    int page = TabCtrl_GetCurSel(g.tab);
    HWND pages[] = {g.lvSummary, g.timeline, g.lvPauses, g.lvCap, g.lvSpells, g.lvPulls};
    for (int i = 0; i < 6; ++i) ShowWindow(pages[i], i == page ? SW_SHOW : SW_HIDE);
    ShowWindow(g.editTips, page == 0 ? SW_SHOW : SW_HIDE);
}

void layout() {
    RECT rc;
    GetClientRect(g.wnd, &rc);
    SendMessageW(g.status, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(g.status, &sr);
    int statusH = sr.bottom - sr.top;
    int pad = Si(10), rowH = Si(26), lblW = Si(190), gapY = Si(6);
    int W = rc.right;
    int y = pad;
    MoveWindow(g.lblLink, pad, y + Si(4), lblW, rowH, TRUE);
    MoveWindow(g.editLink, pad + lblW, y, W - 2 * pad - lblW, rowH, TRUE);
    y += rowH + gapY;
    MoveWindow(g.lblCmp, pad, y + Si(4), lblW, rowH, TRUE);
    MoveWindow(g.editCmp, pad + lblW, y, W - 2 * pad - lblW, rowH, TRUE);
    y += rowH + gapY;

    int x = pad;
    MoveWindow(g.lblPlayer, x, y + Si(4), Si(60), rowH, TRUE);
    x += Si(60);
    MoveWindow(g.comboA, x, y, Si(240), Si(300), TRUE);
    x += Si(246);
    MoveWindow(g.lblVs, x, y + Si(4), Si(24), rowH, TRUE);
    x += Si(26);
    MoveWindow(g.comboB, x, y, Si(240), Si(300), TRUE);

    int bx = W - pad;
    auto btn = [&](HWND b, int w) {
        bx -= Si(w);
        MoveWindow(b, bx, y - Si(1), Si(w), rowH + Si(2), TRUE);
        bx -= Si(6);
    };
    btn(g.btnSettings, 110);
    btn(g.btnFolder, 150);
    btn(g.btnCopy, 130);
    btn(g.btnDemo, 70);
    btn(g.btnAnalyze, 110);
    y += rowH + Si(10);

    MoveWindow(g.tab, pad, y, W - 2 * pad, rc.bottom - statusH - pad / 2 - y, TRUE);
    RECT tr;
    GetWindowRect(g.tab, &tr);
    MapWindowPoints(nullptr, g.wnd, (POINT*)&tr, 2);
    TabCtrl_AdjustRect(g.tab, FALSE, &tr);
    int pw = tr.right - tr.left, ph = tr.bottom - tr.top;
    for (HWND p : {g.timeline, g.lvPauses, g.lvCap, g.lvSpells, g.lvPulls})
        MoveWindow(p, tr.left, tr.top, pw, ph, TRUE);
    int sumH = ph * 6 / 10;
    MoveWindow(g.lvSummary, tr.left, tr.top, pw, sumH, TRUE);
    MoveWindow(g.editTips, tr.left, tr.top + sumH + Si(6), pw, ph - sumH - Si(6), TRUE);
}

void applyFont() {
    if (g.font) DeleteObject(g.font);
    g.font = CreateFontW(-Si(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                         L"Segoe UI");
    EnumChildWindows(g.wnd, [](HWND c, LPARAM f) -> BOOL {
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return TRUE;
    }, (LPARAM)g.font);
}

void applyTexts() {
    SetWindowTextW(g.wnd, T("app_title").c_str());
    SetWindowTextW(g.lblLink, T("link_label").c_str());
    SetWindowTextW(g.lblCmp, T("compare_label").c_str());
    SetWindowTextW(g.lblPlayer, T("player_label").c_str());
    SetWindowTextW(g.lblVs, L"vs");
    SetWindowTextW(g.btnAnalyze, T("analyze").c_str());
    SetWindowTextW(g.btnDemo, T("demo").c_str());
    SetWindowTextW(g.btnCopy, T("copy_report").c_str());
    SetWindowTextW(g.btnFolder, T("open_config").c_str());
    SetWindowTextW(g.btnSettings, T("settings").c_str());
    const char* tabs[] = {"tab_summary", "tab_graph", "tab_pauses", "tab_cap", "tab_spells", "tab_pulls"};
    for (int i = 0; i < 6; ++i) {
        std::wstring s = T(tabs[i]);
        TCITEMW it = {};
        it.mask = TCIF_TEXT;
        it.pszText = &s[0];
        if (TabCtrl_GetItemCount(g.tab) > i) TabCtrl_SetItem(g.tab, i, &it);
        else TabCtrl_InsertItem(g.tab, i, &it);
    }
}

// --------------------------------------------------------------- actions --
void startAnalyze() {
    if (g.working) return;
    Job job;
    job.links[0] = narrow(getText(g.editLink));
    job.links[1] = narrow(getText(g.editCmp));
    auto trim = [](std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        s = a == std::string::npos ? "" : s.substr(a, b - a + 1);
    };
    trim(job.links[0]);
    trim(job.links[1]);
    if (job.links[0].empty()) {
        setStatus(T("ready"));
        SetFocus(g.editLink);
        return;
    }
    if (!loadCredentials(job.clientId, job.secret)) {
        setStatus(T("no_creds"));
        MessageBoxW(g.wnd, T("no_creds").c_str(), T("app_title").c_str(), MB_ICONINFORMATION);
        SendMessageW(g.wnd, WM_COMMAND, ID_SETTINGS, 0);
        return;
    }
    // remember the player picked in each combo
    HWND combos[2] = {g.comboA, g.comboB};
    for (int i = 0; i < 2; ++i) {
        job.slots[i] = g.slots[i];
        int sel = (int)SendMessageW(combos[i], CB_GETCURSEL, 0, 0);
        if (sel >= 0) job.slots[i].chosenId = (int)SendMessageW(combos[i], CB_GETITEMDATA, sel, 0);
    }
    job.spellJson = g.spellJson;
    job.settings.fixedGcd = g.settings.fixedGcd;
    job.settings.combatGap = g.settings.combatGap;
    g.working = true;
    EnableWindow(g.btnAnalyze, FALSE);
    EnableWindow(g.btnDemo, FALSE);
    setStatus(T("working"));
    SetCursor(LoadCursor(nullptr, IDC_APPSTARTING));
    std::thread(runJob, g.wnd, std::move(job)).detach();
}

void runDemo() {
    g.demo = true;
    AnalysisSettings st;
    st.fixedGcd = g.settings.fixedGcd;
    st.combatGap = g.settings.combatGap;
    for (int i = 0; i < 2; ++i) {
        DemoData d = makeDemo(i == 0, 42 + i);
        ReportMeta meta = parseReportMeta("DEMO", d.reportJson);
        FightData fd = buildFightData(meta, meta.fights[0], meta.players[0], d.castsJson, d.resourcesJson,
                                      g.book.astralPowerResourceType());
        g.res[i].reset(new AnalysisResult(analyze(fd, g.book, st)));
        g.slots[i] = Slot{};
        g.slots[i].candidates = meta.players;
        g.slots[i].chosenId = meta.players[0].id;
    }
    fillCombo(g.comboA, g.slots[0]);
    fillCombo(g.comboB, g.slots[1]);
    render();
    setStatus(T("demo_note"));
}

void copyReport() {
    if (!g.res[0]) return;
    std::wstring text = widen(buildReportText(*g.res[0], g.res[1].get(), g.settings.lang));
    for (size_t p = 0; (p = text.find(L'\n', p)) != std::wstring::npos; p += 2) text.insert(p, 1, L'\r');
    if (!OpenClipboard(g.wnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        memcpy(GlobalLock(h), text.c_str(), bytes);
        GlobalUnlock(h);
        if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
    }
    CloseClipboard();
    setStatus(T("copied"));
}

// -------------------------------------------------------------- settings --
struct SettingsWnd {
    HWND wnd, id, secret, lang, gcd, gap;
    bool done = false;
} gs;

double parseNum(const std::wstring& s, double def) {
    std::string n = narrow(s);
    std::replace(n.begin(), n.end(), ',', '.');
    try {
        return n.empty() ? def : std::stod(n);
    } catch (...) {
        return def;
    }
}

LRESULT CALLBACK settingsProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == ID_S_SAVE || LOWORD(wp) == IDOK) {
                std::string id = narrow(getText(gs.id)), secret = narrow(getText(gs.secret));
                saveCredentials(id, secret);
                Lang old = g.settings.lang;
                g.settings.lang = SendMessageW(gs.lang, CB_GETCURSEL, 0, 0) == 1 ? Lang::PtBr : Lang::En;
                g.settings.fixedGcd = std::min(3.0, std::max(0.0, parseNum(getText(gs.gcd), 0)));
                g.settings.combatGap = std::min(120.0, std::max(3.0, parseNum(getText(gs.gap), 10)));
                saveSettings(g.settings);
                if (old != g.settings.lang) {
                    applyTexts();
                    layout();
                }
                render();
                DestroyWindow(w);
                return 0;
            }
            if (LOWORD(wp) == ID_S_CANCEL || LOWORD(wp) == IDCANCEL) {
                DestroyWindow(w);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(w);
            return 0;
        case WM_DESTROY:
            gs.done = true;
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

void openSettings() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
        wc.lpfnWndProc = settingsProc;
        wc.hInstance = g.inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"UbrSettings";
        wc.hIcon = LoadIconW(g.inst, MAKEINTRESOURCEW(1));
        RegisterClassExW(&wc);
        registered = true;
    }
    int W = Si(560), H = Si(330);
    RECT pr;
    GetWindowRect(g.wnd, &pr);
    gs = SettingsWnd{};
    gs.wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"UbrSettings", T("settings").c_str(),
                             WS_POPUP | WS_CAPTION | WS_SYSMENU, pr.left + (pr.right - pr.left - W) / 2,
                             pr.top + Si(80), W, H, g.wnd, nullptr, g.inst, nullptr);
    auto mk = [&](const wchar_t* cls, const std::wstring& text, DWORD style, int x, int y, int w, int h, int id) {
        HWND c = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, text.c_str(),
                                 WS_CHILD | WS_VISIBLE | style, Si(x), Si(y), Si(w), Si(h), gs.wnd,
                                 (HMENU)(INT_PTR)id, g.inst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)g.font, TRUE);
        return c;
    };
    std::string id, secret;
    loadCredentials(id, secret);
    int y = 16;
    mk(L"STATIC", T("client_id"), 0, 16, y + 3, 230, 22, -1);
    gs.id = mk(L"EDIT", widen(id), WS_TABSTOP | ES_AUTOHSCROLL, 250, y, 280, 24, ID_S_ID);
    y += 34;
    mk(L"STATIC", T("client_secret"), 0, 16, y + 3, 230, 22, -1);
    gs.secret = mk(L"EDIT", widen(secret), WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 250, y, 280, 24, ID_S_SECRET);
    y += 34;
    mk(L"STATIC", T("creds_hint"), 0, 16, y, 514, 40, -1);
    y += 48;
    mk(L"STATIC", T("language"), 0, 16, y + 3, 230, 22, -1);
    gs.lang = mk(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 250, y, 280, 200, ID_S_LANG);
    SendMessageW(gs.lang, CB_ADDSTRING, 0, (LPARAM)L"English");
    SendMessageW(gs.lang, CB_ADDSTRING, 0, (LPARAM)L"Português (Brasil)");
    SendMessageW(gs.lang, CB_SETCURSEL, g.settings.lang == Lang::PtBr ? 1 : 0, 0);
    y += 34;
    mk(L"STATIC", T("fixed_gcd"), 0, 16, y + 3, 230, 22, -1);
    gs.gcd = mk(L"EDIT", widen(fmtNum(g.settings.fixedGcd, 2, Lang::En)), WS_TABSTOP, 250, y, 80, 24, ID_S_GCD);
    y += 34;
    mk(L"STATIC", T("combat_gap"), 0, 16, y + 3, 230, 22, -1);
    gs.gap = mk(L"EDIT", widen(fmtNum(g.settings.combatGap, 1, Lang::En)), WS_TABSTOP, 250, y, 80, 24, ID_S_GAP);
    y += 40;
    mk(L"BUTTON", T("save"), WS_TABSTOP | BS_DEFPUSHBUTTON, 330, y, 96, 28, ID_S_SAVE);
    mk(L"BUTTON", T("cancel"), WS_TABSTOP, 434, y, 96, 28, ID_S_CANCEL);
    SecureZeroMemory(&secret[0], secret.size());

    ShowWindow(gs.wnd, SW_SHOW);
    SetFocus(id.empty() ? gs.id : gs.secret);
    EnableWindow(g.wnd, FALSE);
    MSG m;
    while (!gs.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(gs.wnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(g.wnd, TRUE);
    SetForegroundWindow(g.wnd);
}

// ------------------------------------------------------------ main window --
HWND child(const wchar_t* cls, DWORD style, int id, DWORD ex = 0) {
    return CreateWindowExW(ex, cls, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g.wnd,
                           (HMENU)(INT_PTR)id, g.inst, nullptr);
}

HWND listView(int id) {
    HWND lv = child(WC_LISTVIEWW, WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, id, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    return lv;
}

void createControls() {
    g.lblLink = child(L"STATIC", 0, -1);
    g.editLink = child(L"EDIT", WS_TABSTOP | ES_AUTOHSCROLL, ID_LINK, WS_EX_CLIENTEDGE);
    g.lblCmp = child(L"STATIC", 0, -1);
    g.editCmp = child(L"EDIT", WS_TABSTOP | ES_AUTOHSCROLL, ID_CMP, WS_EX_CLIENTEDGE);
    g.lblPlayer = child(L"STATIC", 0, -1);
    g.comboA = child(L"COMBOBOX", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_COMBO_A);
    g.lblVs = child(L"STATIC", SS_CENTER, -1);
    g.comboB = child(L"COMBOBOX", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_COMBO_B);
    g.btnAnalyze = child(L"BUTTON", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_ANALYZE);
    g.btnDemo = child(L"BUTTON", WS_TABSTOP, ID_DEMO);
    g.btnCopy = child(L"BUTTON", WS_TABSTOP, ID_COPY);
    g.btnFolder = child(L"BUTTON", WS_TABSTOP, ID_FOLDER);
    g.btnSettings = child(L"BUTTON", WS_TABSTOP, ID_SETTINGS);
    g.tab = child(WC_TABCONTROLW, WS_TABSTOP | WS_CLIPSIBLINGS, ID_TAB);
    g.lvSummary = listView(ID_LV_SUMMARY);
    g.editTips = child(L"EDIT", ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, ID_TIPS, WS_EX_CLIENTEDGE);
    g.timeline = createTimeline(g.wnd, ID_TIMELINE);
    g.lvPauses = listView(ID_LV_PAUSES);
    g.lvCap = listView(ID_LV_CAP);
    g.lvSpells = listView(ID_LV_SPELLS);
    g.lvPulls = listView(ID_LV_PULLS);
    g.status = child(STATUSCLASSNAMEW, SBARS_SIZEGRIP, ID_STATUS);
    // pages sit above the tab control
    for (HWND p : {g.lvSummary, g.editTips, g.timeline, g.lvPauses, g.lvCap, g.lvSpells, g.lvPulls})
        SetWindowPos(p, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SendMessageW(g.editLink, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"https://www.warcraftlogs.com/reports/...?fight=...");
    SendMessageW(g.editCmp, EM_SETCUEBANNER, TRUE, (LPARAM)L"https://www.warcraftlogs.com/reports/...");
    EnableWindow(g.btnCopy, FALSE);
}

LRESULT CALLBACK mainProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.wnd = w;
            g.dpi = windowDpi(w);
            createControls();
            applyTexts();
            applyFont();
            TabCtrl_SetCurSel(g.tab, 0);
            showPage();
            render();
            setStatus(T("ready"));
            return 0;
        }
        case WM_SIZE:
            layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize = {Si(980), Si(560)};
            return 0;
        }
        case WM_DPICHANGED: {
            g.dpi = HIWORD(wp);
            RECT* r = (RECT*)lp;
            applyFont();
            SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            render();
            return 0;
        }
        case WM_NOTIFY: {
            auto* nm = (NMHDR*)lp;
            if (nm->hwndFrom == g.tab && nm->code == TCN_SELCHANGE) showPage();
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                case ID_ANALYZE: startAnalyze(); return 0;
                case ID_DEMO: runDemo(); return 0;
                case ID_COPY: copyReport(); return 0;
                case ID_SETTINGS: openSettings(); return 0;
                case ID_FOLDER:
                    ShellExecuteW(w, L"open", dataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                case ID_LINK:
                case ID_CMP:
                    if (HIWORD(wp) == EN_CHANGE) g.demo = false;
                    return 0;
            }
            break;
        case WM_APP_STATUS: {
            std::unique_ptr<std::wstring> s((std::wstring*)lp);
            setStatus(*s);
            return 0;
        }
        case WM_APP_DONE: {
            std::unique_ptr<JobResult> r((JobResult*)lp);
            g.working = false;
            EnableWindow(g.btnAnalyze, TRUE);
            EnableWindow(g.btnDemo, TRUE);
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            if (!r->error.empty()) {
                setStatus(widen(r->error.substr(0, r->error.find('\n'))));
                MessageBoxW(w, widen(r->error).c_str(), T("app_title").c_str(), MB_ICONWARNING);
                return 0;
            }
            g.demo = false;
            for (int i = 0; i < 2; ++i) {
                g.res[i] = std::move(r->res[i]);
                g.slots[i] = r->slots[i];
            }
            fillCombo(g.comboA, g.slots[0]);
            fillCombo(g.comboB, g.slots[1]);
            render();
            setStatus(r->ambiguous ? T("pick_player") : T("done"));
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            if ((HWND)lp == g.editTips) {
                SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g.inst = inst;
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES |
                                                ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    g.settings = loadSettings();
    std::string err;
    g.spellJson = loadSpellConfig(err);
    g.book = SpellBook::fromJson(g.spellJson);
    if (!err.empty())
        MessageBoxW(nullptr, (L"spells.json: " + widen(err) + L"\n\nUsing the built-in defaults.").c_str(),
                    L"Unnecessary Boomkin Reviewer", MB_ICONWARNING);

    registerTimelineClass(inst);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = mainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"UbrMain";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    RegisterClassExW(&wc);

    HDC screen = GetDC(nullptr);
    double sc = GetDeviceCaps(screen, LOGPIXELSX) / 96.0;
    ReleaseDC(nullptr, screen);
    HWND w = CreateWindowExW(0, L"UbrMain", L"Unnecessary Boomkin Reviewer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, (int)(1180 * sc), (int)(760 * sc), nullptr, nullptr,
                             inst, nullptr);
    ShowWindow(w, show);
    UpdateWindow(w);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(w, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
