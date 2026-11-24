// HttpClient on top of WinHTTP. Uses the system proxy settings; TLS comes from Windows.
#pragma once

#include "../core/wcl.h"

namespace ubr {

class WinHttpClient : public HttpClient {
public:
    WinHttpClient();
    ~WinHttpClient() override;
    HttpResponse post(const std::string& url, const Headers& headers, const std::string& body) override;

private:
    void* session_ = nullptr;
};

} // namespace ubr
