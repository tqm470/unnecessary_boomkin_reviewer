// Small Windows helpers: UTF-8 <-> UTF-16, data folder, files, settings, credentials.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

#include "../core/i18n.h"

namespace ubr {

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& w);

// %LOCALAPPDATA%\UnnecessaryBoomkinReviewer (created on demand)
std::wstring dataDir();
std::wstring cacheDir();

bool readFile(const std::wstring& path, std::string& out);
bool writeFile(const std::wstring& path, const std::string& data);

struct AppSettings {
    Lang lang = Lang::En;
    double fixedGcd = 0;
    double combatGap = 10;
};
AppSettings loadSettings();
void saveSettings(const AppSettings& s);

// Windows Credential Manager (generic credential, encrypted per user).
bool loadCredentials(std::string& clientId, std::string& secret);
bool saveCredentials(const std::string& clientId, const std::string& secret);

// Loads spells.json from the data folder, writing the default the first time.
// On a parse error returns the built-in default and fills `error`.
std::string loadSpellConfig(std::string& error);

} // namespace ubr
