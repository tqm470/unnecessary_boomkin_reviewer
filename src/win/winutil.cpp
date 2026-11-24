#include "winutil.h"

#include <shlobj.h>
#include <wincred.h>

#include "../../third_party/nlohmann/json.hpp"
#include "../core/spells.h"

namespace ubr {

using nlohmann::json;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring dataDir() {
    wchar_t buf[MAX_PATH] = {};
    std::wstring base;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf))) base = buf;
    else base = L".";
    std::wstring dir = base + L"\\UnnecessaryBoomkinReviewer";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring cacheDir() {
    std::wstring dir = dataDir() + L"\\cache";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

bool readFile(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > (1LL << 31)) {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    BOOL ok = out.empty() || ReadFile(h, &out[0], (DWORD)out.size(), &read, nullptr);
    CloseHandle(h);
    return ok && read == out.size();
}

bool writeFile(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != data.size()) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

AppSettings loadSettings() {
    AppSettings s;
    // Default to Portuguese when Windows is in Portuguese.
    if (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE) s.lang = Lang::PtBr;
    std::string text;
    if (!readFile(dataDir() + L"\\settings.json", text)) return s;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) return s;
    if (j.contains("lang")) s.lang = j["lang"] == "pt-BR" ? Lang::PtBr : Lang::En;
    s.fixedGcd = j.value("fixed_gcd", 0.0);
    s.combatGap = j.value("combat_gap", 10.0);
    if (s.fixedGcd < 0 || s.fixedGcd > 3) s.fixedGcd = 0;
    if (s.combatGap < 3 || s.combatGap > 120) s.combatGap = 10;
    return s;
}

void saveSettings(const AppSettings& s) {
    json j = {{"lang", s.lang == Lang::PtBr ? "pt-BR" : "en"},
              {"fixed_gcd", s.fixedGcd},
              {"combat_gap", s.combatGap}};
    writeFile(dataDir() + L"\\settings.json", j.dump(2));
}

static const wchar_t* kCredTarget = L"UnnecessaryBoomkinReviewer/WarcraftLogsApi";

bool loadCredentials(std::string& clientId, std::string& secret) {
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(kCredTarget, CRED_TYPE_GENERIC, 0, &cred)) return false;
    std::string blob((const char*)cred->CredentialBlob, cred->CredentialBlobSize);
    CredFree(cred);
    size_t nl = blob.find('\n');
    if (nl == std::string::npos) return false;
    clientId = blob.substr(0, nl);
    secret = blob.substr(nl + 1);
    SecureZeroMemory(&blob[0], blob.size());
    return !clientId.empty() && !secret.empty();
}

bool saveCredentials(const std::string& clientId, const std::string& secret) {
    if (clientId.empty() && secret.empty()) {
        CredDeleteW(kCredTarget, CRED_TYPE_GENERIC, 0);
        return true;
    }
    std::string blob = clientId + "\n" + secret;
    std::wstring user = widen(clientId);
    CREDENTIALW c = {};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = const_cast<wchar_t*>(kCredTarget);
    c.CredentialBlobSize = (DWORD)blob.size();
    c.CredentialBlob = (LPBYTE)&blob[0];
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    c.UserName = const_cast<wchar_t*>(user.c_str());
    BOOL ok = CredWriteW(&c, 0);
    SecureZeroMemory(&blob[0], blob.size());
    return ok != 0;
}

std::string loadSpellConfig(std::string& error) {
    std::wstring path = dataDir() + L"\\spells.json";
    std::string text;
    if (!readFile(path, text)) {
        writeFile(path, SpellBook::defaultJson());
        return SpellBook::defaultJson();
    }
    try {
        SpellBook::fromJson(text);
        return text;
    } catch (const std::exception& e) {
        error = e.what();
        return SpellBook::defaultJson();
    }
}

} // namespace ubr
