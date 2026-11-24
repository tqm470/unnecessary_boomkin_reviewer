// The timeline: a hand-painted GDI chart of Astral Power, cap windows, GCD
// activity and pauses. Wheel to zoom, drag to pan, double-click to reset.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../core/analysis.h"
#include "../core/i18n.h"

namespace ubr {

void registerTimelineClass(HINSTANCE inst);
HWND createTimeline(HWND parent, int id);
// Pointers must stay valid until the next call (or nullptr to clear).
void timelineSet(HWND wnd, const AnalysisResult* a, const AnalysisResult* b, Lang lang);

} // namespace ubr
