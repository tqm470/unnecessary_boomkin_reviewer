// Synthetic fights shaped exactly like Warcraft Logs API responses, so the
// whole pipeline (parsing + analysis + UI) can run without network access.
#pragma once

#include <string>

namespace ubr {

struct DemoData {
    std::string reportJson;     // GraphQL response for the report query
    std::string castsJson;      // events array (Casts, includeResources)
    std::string resourcesJson;  // events array (Resources)
    int fightId = 1;
    int playerId = 0;
};

// sloppy = true: pauses on mechanics, Cat Form trips, re-dots at cap.
DemoData makeDemo(bool sloppy, unsigned seed);

} // namespace ubr
