// Tiny string table: English and Brazilian Portuguese.
#pragma once

#include <string>

namespace ubr {

enum class Lang { En, PtBr };

// Returns the UTF-8 text for `key` (the key itself when missing).
const char* tr(Lang lang, const char* key);

std::string fmtNum(double v, int decimals, Lang lang);   // 1.234,5 vs 1,234.5
std::string fmtPct(double fraction, Lang lang);          // 0.643 -> "64.3%"
std::string fmtClock(double seconds);                     // 125.4 -> "2:05"
std::string fmtSec(double seconds, Lang lang);            // "7.9s" / "7,9s"

} // namespace ubr
