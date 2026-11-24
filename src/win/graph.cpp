#include "graph.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "winutil.h"

namespace ubr {

namespace {

const wchar_t* kClass = L"UbrTimeline";

// Night sky palette.
const COLORREF kSkyTop = RGB(11, 16, 38);
const COLORREF kSkyBottom = RGB(30, 20, 64);
const COLORREF kApFill = RGB(59, 42, 110);
const COLORREF kApLine = RGB(255, 179, 71);
const COLORREF kCapLine = RGB(255, 92, 122);
const COLORREF kGlow[3] = {RGB(48, 14, 34), RGB(78, 18, 42), RGB(120, 26, 52)};
const COLORREF kGrid = RGB(52, 60, 100);
const COLORREF kText = RGB(220, 226, 255);
const COLORREF kDim = RGB(140, 150, 190);
const COLORREF kBusy = RGB(70, 192, 138);
const COLORREF kGap = RGB(28, 34, 64);
const COLORREF kPause = RGB(232, 163, 61);
const COLORREF kLongPause = RGB(224, 72, 90);
const COLORREF kOoc = RGB(44, 48, 72);
const COLORREF kMoon = RGB(223, 230, 255);
const COLORREF kStar = RGB(166, 216, 255);
const COLORREF kPull = RGB(107, 123, 181);

struct State {
    const AnalysisResult* r[2] = {nullptr, nullptr};
    Lang lang = Lang::En;
    double v0 = 0, v1 = 1;   // visible time range
    double full = 1;         // longest fight
    int mx = -1, my = -1;    // mouse
    bool dragging = false;
    int dragX = 0;
    double dragV0 = 0;
    bool tracking = false;
};

UINT dpiOf(HWND w) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = (Fn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    return fn ? fn(w) : 96;
}

State* st(HWND w) { return (State*)GetWindowLongPtrW(w, GWLP_USERDATA); }

void fillRect(HDC dc, int l, int t, int r, int b, COLORREF c) {
    if (r <= l || b <= t) return;
    RECT rc = {l, t, r, b};
    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &rc, br);
    DeleteObject(br);
}

void line(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, int width = 1, int style = PS_SOLID) {
    HPEN pen = CreatePen(style, width, c);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x0, y0, nullptr);
    LineTo(dc, x1, y1);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void text(HDC dc, int x, int y, const std::wstring& s, COLORREF c, UINT align = DT_LEFT) {
    SetTextColor(dc, c);
    RECT rc = {x, y, x, y};
    UINT fmt = DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX;
    if (align == DT_RIGHT) { rc.left = x - 2000; rc.right = x; fmt |= DT_RIGHT; }
    else if (align == DT_CENTER) { rc.left = x - 1000; rc.right = x + 1000; fmt |= DT_CENTER; }
    DrawTextW(dc, s.c_str(), -1, &rc, fmt);
}

void gradient(HDC dc, const RECT& rc, COLORREF top, COLORREF bottom) {
    // GradientFill without msimg32: a few dozen bands look identical.
    int h = rc.bottom - rc.top;
    const int bands = 48;
    for (int i = 0; i < bands; ++i) {
        double f = (double)i / (bands - 1);
        COLORREF c = RGB((int)(GetRValue(top) + f * (GetRValue(bottom) - GetRValue(top))),
                         (int)(GetGValue(top) + f * (GetGValue(bottom) - GetGValue(top))),
                         (int)(GetBValue(top) + f * (GetBValue(bottom) - GetBValue(top))));
        fillRect(dc, rc.left, rc.top + h * i / bands, rc.right, rc.top + h * (i + 1) / bands, c);
    }
}

void stars(HDC dc, const RECT& rc, double scale) {
    unsigned s = 12345;
    for (int i = 0; i < 90; ++i) {
        s = s * 1103515245u + 12345u;
        int x = rc.left + (int)((s >> 8) % (unsigned)std::max(1L, rc.right - rc.left));
        s = s * 1103515245u + 12345u;
        int y = rc.top + (int)((s >> 8) % (unsigned)std::max(1L, rc.bottom - rc.top));
        int b = 70 + (int)((s >> 4) % 90);
        int sz = (s % 11 == 0) ? (int)std::max(2.0, 2 * scale) : 1;
        fillRect(dc, x, y, x + sz, y + sz, RGB(b, b, b + 30 > 255 ? 255 : b + 30));
    }
}

void crescent(HDC dc, int cx, int cy, int r, COLORREF moon) {
    // full disc minus an offset disc, so the sky behind shows through
    int off = std::max(2, r * 2 / 3);
    HRGN a = CreateEllipticRgn(cx - r, cy - r, cx + r + 1, cy + r + 1);
    HRGN b = CreateEllipticRgn(cx - r + off, cy - r - off / 3, cx + r + off + 1, cy + r - off / 3 + 1);
    CombineRgn(a, a, b, RGN_DIFF);
    HBRUSH br = CreateSolidBrush(moon);
    FillRgn(dc, a, br);
    DeleteObject(br);
    DeleteObject(a);
    DeleteObject(b);
}

void starGlyph(HDC dc, int cx, int cy, int r, COLORREF c) {
    POINT p[10];
    for (int i = 0; i < 10; ++i) {
        double a = -3.14159265 / 2 + i * 3.14159265 / 5;
        double rr = (i % 2 == 0) ? r : r * 0.45;
        p[i].x = cx + (int)std::lround(rr * std::cos(a));
        p[i].y = cy + (int)std::lround(rr * std::sin(a));
    }
    HBRUSH br = CreateSolidBrush(c);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    HGDIOBJ ob = SelectObject(dc, br);
    Polygon(dc, p, 10);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
}

std::wstring w(const std::string& s) { return widen(s); }

double niceStep(double span, int plotW, double minPx) {
    const double steps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
    for (double s : steps)
        if (plotW * s / span >= minPx) return s;
    return 600;
}

struct Geo {
    int L, R, T, B;           // plot area
    int laneH, gap;
    double sc;
    int x(double t, const State& s) const {
        return L + (int)std::lround((t - s.v0) / (s.v1 - s.v0) * (R - L));
    }
    double t(int px, const State& s) const { return s.v0 + (double)(px - L) / (R - L) * (s.v1 - s.v0); }
};

Geo geometry(HWND wnd, const State& s) {
    RECT rc;
    GetClientRect(wnd, &rc);
    double sc = dpiOf(wnd) / 96.0;
    Geo g;
    g.sc = sc;
    g.L = (int)(56 * sc);
    g.R = rc.right - (int)(16 * sc);
    g.T = (int)(12 * sc);
    g.B = rc.bottom - (int)(28 * sc);
    int n = s.r[1] ? 2 : 1;
    g.gap = (int)(22 * sc);
    g.laneH = std::max(40, (g.B - g.T - g.gap * (n - 1)) / n);
    return g;
}

std::wstring stateAt(const AnalysisResult& r, double t, Lang lang) {
    for (const auto& b : r.busy)
        if (t >= b.start && t < b.end) return lang == Lang::PtBr ? L"castando" : L"casting";
    for (const auto& c : r.combat)
        if (t >= c.start && t < c.end) {
            double gs = c.start, ge = c.end;
            for (const auto& b : r.busy) {
                if (b.end <= t) gs = std::max(gs, b.end);
                if (b.start > t) { ge = std::min(ge, b.start); break; }
            }
            return (lang == Lang::PtBr ? L"parado " : L"idle ") + w(fmtSec(ge - gs, lang));
        }
    return lang == Lang::PtBr ? L"fora de combate" : L"out of combat";
}

void paintLane(HDC dc, const Geo& g, const State& s, const AnalysisResult& r, int top, HFONT small) {
    int apTop = top + (int)(34 * g.sc); // header row, then marker row
    int stripH = std::max((int)(12 * g.sc), g.laneH / 6);
    int apBottom = top + g.laneH - stripH - (int)(6 * g.sc);
    int stripTop = apBottom + (int)(6 * g.sc);
    int stripBottom = stripTop + stripH;
    double apMax = 100;
    for (const auto& p : r.ap) apMax = std::max(apMax, p.max);
    auto y = [&](double v) { return apBottom - (int)std::lround(v / apMax * (apBottom - apTop)); };

    HRGN clip = CreateRectRgn(g.L, top, g.R, stripBottom + 1);
    SelectClipRgn(dc, clip);

    // cap glow
    for (const auto& c : r.capWindows) {
        int x0 = g.x(c.start, s), x1 = std::max(g.x(c.start + c.duration, s), x0 + 1);
        int pad = (int)(3 * g.sc);
        fillRect(dc, x0 - pad, apTop, x1 + pad, apBottom, kGlow[0]);
        fillRect(dc, x0 - pad / 2, apTop, x1 + pad / 2, apBottom, kGlow[1]);
        fillRect(dc, x0, apTop, x1, apBottom, kGlow[2]);
    }
    // grid
    line(dc, g.L, y(apMax), g.R, y(apMax), kCapLine, 1, PS_DOT);
    line(dc, g.L, y(apMax / 2), g.R, y(apMax / 2), kGrid, 1, PS_DOT);
    line(dc, g.L, apBottom, g.R, apBottom, kGrid);

    // Astral Power: filled step polygon + line
    if (!r.ap.empty()) {
        std::vector<POINT> pts;
        pts.push_back({g.x(std::max(s.v0, 0.0), s), apBottom});
        double prev = r.ap.front().value;
        for (const auto& p : r.ap) {
            if (p.t < s.v0) { prev = p.value; continue; }
            if (p.t > s.v1) break;
            int px = g.x(p.t, s);
            if (pts.size() == 1) pts.push_back({g.x(s.v0, s), y(prev)});
            pts.push_back({px, y(prev)});
            pts.push_back({px, y(p.value)});
            prev = p.value;
        }
        if (pts.size() == 1) pts.push_back({g.x(s.v0, s), y(prev)});
        pts.push_back({g.x(std::min(s.v1, r.fightDuration), s), y(prev)});
        std::vector<POINT> poly = pts;
        poly.push_back({pts.back().x, apBottom});
        HBRUSH br = CreateSolidBrush(kApFill);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        HGDIOBJ ob = SelectObject(dc, br);
        Polygon(dc, poly.data(), (int)poly.size());
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(br);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, (int)std::lround(1.6 * g.sc)), kApLine);
        op = SelectObject(dc, pen);
        Polyline(dc, pts.data() + 1, (int)pts.size() - 1);
        SelectObject(dc, op);
        DeleteObject(pen);
    }

    // activity strip
    fillRect(dc, g.L, stripTop, g.R, stripBottom, kOoc);
    for (const auto& c : r.combat) fillRect(dc, g.x(c.start, s), stripTop, g.x(c.end, s), stripBottom, kGap);
    for (const auto& p : r.pauses)
        fillRect(dc, g.x(p.start, s), stripTop, std::max(g.x(p.start + p.duration, s), g.x(p.start, s) + 1),
                 stripBottom, p.duration >= 2.5 ? kLongPause : kPause);
    for (const auto& b : r.busy)
        fillRect(dc, g.x(b.start, s), stripTop, std::max(g.x(b.end, s), g.x(b.start, s) + 1), stripBottom, kBusy);

    // pull boundaries
    if (r.pulls.size() > 1) {
        SelectObject(dc, small);
        for (const auto& p : r.pulls) {
            int x = g.x(p.start, s);
            line(dc, x, apTop, x, stripBottom, kPull, 1, PS_DASH);
            text(dc, x + (int)(3 * g.sc), stripBottom - stripH - (int)(16 * g.sc),
                 L"P" + std::to_wstring(p.index), kPull);
        }
    }

    // markers
    int my = top + (int)(25 * g.sc);
    for (const auto& m : r.markers) {
        if (m.t < s.v0 || m.t > s.v1) continue;
        int x = g.x(m.t, s);
        int rad = (int)(6 * g.sc);
        if (m.name.find("Incarnation") != std::string::npos || m.name.find("Celestial") != std::string::npos)
            crescent(dc, x, my, rad, kMoon);
        else
            starGlyph(dc, x, my, rad, kStar);
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);

    // lane header + axis labels
    SelectObject(dc, small);
    std::wstring head = w(r.playerName) + L"   " +
                        w(tr(s.lang, "col_uptime")) + L" " + w(fmtPct(r.uptimeCombat, s.lang)) +
                        L"   cap " + w(fmtSec(r.capTime, s.lang)) +
                        L"   " + w(tr(s.lang, "col_waste")) + L" " + w(fmtNum(r.wastePct(), 1, s.lang)) + L"%";
    text(dc, g.L + (int)(20 * g.sc), top, head, kText);
    text(dc, g.L - (int)(6 * g.sc), y(apMax) - (int)(7 * g.sc), L"100", kCapLine, DT_RIGHT);
    text(dc, g.L - (int)(6 * g.sc), y(apMax / 2) - (int)(7 * g.sc), L"50", kDim, DT_RIGHT);
    text(dc, g.L - (int)(6 * g.sc), apBottom - (int)(7 * g.sc), L"AP 0", kDim, DT_RIGHT);
    text(dc, g.L - (int)(6 * g.sc), stripTop + stripH / 2 - (int)(7 * g.sc), L"GCD", kDim, DT_RIGHT);
}

void paint(HWND wnd, HDC dc) {
    State& s = *st(wnd);
    RECT rc;
    GetClientRect(wnd, &rc);
    double sc = dpiOf(wnd) / 96.0;
    gradient(dc, rc, kSkyTop, kSkyBottom);
    stars(dc, rc, sc);
    SetBkMode(dc, TRANSPARENT);

    HFONT small = CreateFontW(-(int)std::lround(11 * sc), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT big = CreateFontW(-(int)std::lround(16 * sc), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, small);

    if (!s.r[0]) {
        int cx = rc.right / 2, cy = rc.bottom / 2;
        crescent(dc, cx, cy - (int)(40 * sc), (int)(34 * sc), kMoon);
        SelectObject(dc, big);
        text(dc, cx, cy + (int)(10 * sc), w(tr(s.lang, "ready")), kText, DT_CENTER);
    } else {
        Geo g = geometry(wnd, s);
        for (int i = 0; i < 2; ++i)
            if (s.r[i]) paintLane(dc, g, s, *s.r[i], g.T + i * (g.laneH + g.gap), small);

        // time axis
        SelectObject(dc, small);
        double step = niceStep(s.v1 - s.v0, g.R - g.L, 70 * sc);
        for (double t = std::ceil(s.v0 / step) * step; t <= s.v1; t += step) {
            int x = g.x(t, s);
            line(dc, x, g.B, x, g.B + (int)(4 * sc), kDim);
            text(dc, x, g.B + (int)(6 * sc), w(fmtClock(t)), kDim, DT_CENTER);
        }
        // legend
        int lx = g.R, ly = g.B + (int)(6 * sc);
        auto legend = [&](COLORREF c, const wchar_t* label) {
            SIZE sz;
            GetTextExtentPoint32W(dc, label, (int)wcslen(label), &sz);
            lx -= sz.cx;
            text(dc, lx, ly, label, kDim);
            lx -= (int)(14 * sc);
            fillRect(dc, lx, ly + (int)(3 * sc), lx + (int)(10 * sc), ly + (int)(13 * sc), c);
            lx -= (int)(10 * sc);
        };
        bool pt = s.lang == Lang::PtBr;
        legend(kLongPause, pt ? L"pausa 2,5s+" : L"pause 2.5s+");
        legend(kPause, pt ? L"pausa 1,5s+" : L"pause 1.5s+");
        legend(kBusy, pt ? L"castando" : L"casting");
        legend(kGlow[2], pt ? L"no cap" : L"capped");

        // hover crosshair + tooltip
        if (s.mx >= g.L && s.mx <= g.R && s.my >= g.T && s.my <= g.B) {
            line(dc, s.mx, g.T, s.mx, g.B, RGB(200, 205, 240), 1, PS_DOT);
            double t = g.t(s.mx, s);
            std::vector<std::wstring> rows;
            rows.push_back(w(fmtClock(t)) + L"." + std::to_wstring((int)(std::fmod(std::max(0.0, t), 1.0) * 10)));
            for (int i = 0; i < 2; ++i) {
                if (!s.r[i] || t < 0 || t > s.r[i]->fightDuration) continue;
                const AnalysisResult& r = *s.r[i];
                std::wstring row = w(r.playerName) + L": ";
                if (r.hasAp) row += L"AP " + w(fmtNum(apAt(r.ap, t + 1e-6), 0, s.lang)) + L", ";
                row += stateAt(r, t, s.lang);
                rows.push_back(row);
                for (const auto& m : r.markers)
                    if (std::fabs(m.t - t) < (s.v1 - s.v0) / 150) rows.push_back(L"  " + w(m.name));
            }
            int tw = 0, th = 0;
            for (const auto& row : rows) {
                SIZE sz;
                GetTextExtentPoint32W(dc, row.c_str(), (int)row.size(), &sz);
                tw = std::max<int>(tw, sz.cx);
                th += sz.cy;
            }
            int pad = (int)(6 * sc);
            int bx = s.mx + (int)(12 * sc), by = s.my + (int)(12 * sc);
            if (bx + tw + 2 * pad > rc.right) bx = s.mx - tw - 2 * pad - (int)(12 * sc);
            if (by + th + 2 * pad > rc.bottom) by = s.my - th - 2 * pad;
            fillRect(dc, bx, by, bx + tw + 2 * pad, by + th + 2 * pad, RGB(20, 24, 48));
            HBRUSH frame = CreateSolidBrush(kPull);
            RECT fr = {bx, by, bx + tw + 2 * pad, by + th + 2 * pad};
            FrameRect(dc, &fr, frame);
            DeleteObject(frame);
            int yy = by + pad;
            for (size_t i = 0; i < rows.size(); ++i) {
                text(dc, bx + pad, yy, rows[i], i == 0 ? kApLine : kText);
                SIZE sz;
                GetTextExtentPoint32W(dc, rows[i].c_str(), (int)rows[i].size(), &sz);
                yy += sz.cy;
            }
        }
    }
    SelectObject(dc, oldFont);
    DeleteObject(small);
    DeleteObject(big);
}

void clampView(State& s) {
    double span = std::max(10.0, std::min(s.v1 - s.v0, s.full));
    if (s.v0 < 0) s.v0 = 0;
    s.v1 = s.v0 + span;
    if (s.v1 > s.full) {
        s.v1 = s.full;
        s.v0 = std::max(0.0, s.full - span);
    }
}

LRESULT CALLBACK proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* s = st(wnd);
    switch (msg) {
        case WM_CREATE:
            SetWindowLongPtrW(wnd, GWLP_USERDATA, (LONG_PTR) new State());
            return 0;
        case WM_DESTROY:
            delete s;
            SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(wnd, &ps);
            RECT rc;
            GetClientRect(wnd, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            paint(wnd, mem);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(wnd, &ps);
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(wnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE: {
            if (!s) break;
            s->mx = GET_X_LPARAM(lp);
            s->my = GET_Y_LPARAM(lp);
            if (!s->tracking) {
                TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, wnd, 0};
                TrackMouseEvent(&tme);
                s->tracking = true;
            }
            if (s->dragging && s->r[0]) {
                Geo g = geometry(wnd, *s);
                double span = s->v1 - s->v0;
                s->v0 = s->dragV0 - (double)(s->mx - s->dragX) / std::max(1, g.R - g.L) * span;
                s->v1 = s->v0 + span;
                clampView(*s);
            }
            InvalidateRect(wnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (s) { s->mx = s->my = -1; s->tracking = false; }
            InvalidateRect(wnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            if (!s) break;
            SetFocus(wnd);
            s->dragging = true;
            s->dragX = GET_X_LPARAM(lp);
            s->dragV0 = s->v0;
            SetCapture(wnd);
            return 0;
        case WM_LBUTTONUP:
            if (s) s->dragging = false;
            ReleaseCapture();
            return 0;
        case WM_LBUTTONDBLCLK:
            if (!s) break;
            s->v0 = 0;
            s->v1 = s->full;
            InvalidateRect(wnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL: {
            if (!s || !s->r[0]) break;
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(wnd, &p);
            Geo g = geometry(wnd, *s);
            double t = g.t(std::min(std::max(p.x, (LONG)g.L), (LONG)g.R), *s);
            double f = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 0.8 : 1.25;
            s->v0 = t - (t - s->v0) * f;
            s->v1 = t + (s->v1 - t) * f;
            clampView(*s);
            InvalidateRect(wnd, nullptr, FALSE);
            return 0;
        }
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

} // namespace

void registerTimelineClass(HINSTANCE inst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
}

HWND createTimeline(HWND parent, int id) {
    return CreateWindowExW(0, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, parent,
                           (HMENU)(INT_PTR)id, (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
}

void timelineSet(HWND wnd, const AnalysisResult* a, const AnalysisResult* b, Lang lang) {
    State* s = st(wnd);
    if (!s) return;
    s->r[0] = a;
    s->r[1] = b;
    s->lang = lang;
    s->full = std::max(a ? a->fightDuration : 1.0, b ? b->fightDuration : 1.0);
    s->full = std::max(s->full, 10.0);
    s->v0 = 0;
    s->v1 = s->full;
    InvalidateRect(wnd, nullptr, FALSE);
}

} // namespace ubr
