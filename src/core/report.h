// Turns an AnalysisResult into rows for the UI and a text report for the clipboard.
#pragma once

#include <string>
#include <vector>

#include "analysis.h"
#include "i18n.h"

namespace ubr {

struct Row {
    std::string label;
    std::string a; // the analysed player
    std::string b; // the comparison (empty when none)
};

std::vector<Row> summaryRows(const AnalysisResult& r, const AnalysisResult* cmp, Lang lang);
std::string formatTip(const Tip& t, Lang lang);
std::string roleLabel(SpellRole role, Lang lang);
std::string buildReportText(const AnalysisResult& r, const AnalysisResult* cmp, Lang lang);

} // namespace ubr
