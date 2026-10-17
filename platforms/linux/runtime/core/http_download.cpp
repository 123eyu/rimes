#include "http_download.hpp"
#include "plugin_store.hpp"
#include <algorithm>
#include <chrono>
#include <curl/curl.h>
#include <memory>
#include <stdexcept>

namespace rimes::runtime {
namespace {
struct CurlClose { void operator()(CURL* curl) const { if (curl) curl_easy_cleanup(curl); } };
struct URLClose { void operator()(CURLU* url) const { if (url) curl_url_cleanup(url); } };
std::string Part(CURLU* url, CURLUPart part) {
    char* text = nullptr;
    if (curl_url_get(url, part, &text, 0) != CURLUE_OK) return {};
    std::string result(text); curl_free(text); return result;
}
size_t Write(char* data, size_t size, size_t count, void* context) {
    if (size && count > plugins::PackageStore::kPackageLimit / size) return 0;
    const auto bytes = size * count;
    auto& output = *static_cast<std::string*>(context);
    if (output.size() + bytes > plugins::PackageStore::kPackageLimit) return 0;
    output.append(data, bytes); return bytes;
}
}

bool OfficialDownloadURL(std::string_view source) {
    if (source.size() > 8192 || source.find_first_of("\r\n\0", 0, 3) != std::string_view::npos) return false;
    std::unique_ptr<CURLU, URLClose> url(curl_url());
    if (!url || curl_url_set(url.get(), CURLUPART_URL, std::string(source).c_str(), 0) != CURLUE_OK) return false;
    const auto host = Part(url.get(), CURLUPART_HOST), port = Part(url.get(), CURLUPART_PORT);
    return Part(url.get(), CURLUPART_SCHEME) == "https" && (port.empty() || port == "443") &&
        Part(url.get(), CURLUPART_USER).empty() && Part(url.get(), CURLUPART_PASSWORD).empty() &&
        Part(url.get(), CURLUPART_FRAGMENT).empty() &&
        (host == "github.com" || host == "objects.githubusercontent.com" || host == "release-assets.githubusercontent.com");
}

std::string DownloadOfficialPackage(const std::string& source) {
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) throw std::runtime_error("Network initialization failed");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    auto url = source;
    for (int redirect = 0; redirect != 4; ++redirect) {
        if (!OfficialDownloadURL(url)) throw std::runtime_error("Untrusted plugin download recipient");
        std::unique_ptr<CURL, CurlClose> request(curl_easy_init());
        if (!request) throw std::runtime_error("Plugin download unavailable");
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) throw std::runtime_error("Plugin download timed out");
        std::string bytes;
        curl_easy_setopt(request.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(request.get(), CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(request.get(), CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(request.get(), CURLOPT_CONNECTTIMEOUT_MS, std::min(10000L, static_cast<long>(remaining)));
        curl_easy_setopt(request.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(remaining));
        curl_easy_setopt(request.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(request.get(), CURLOPT_WRITEFUNCTION, Write);
        curl_easy_setopt(request.get(), CURLOPT_WRITEDATA, &bytes);
        curl_easy_setopt(request.get(), CURLOPT_USERAGENT, "XSIME-Linux-Plugins");
        if (curl_easy_perform(request.get()) != CURLE_OK) throw std::runtime_error("Plugin download failed; retry when connected");
        long status = 0;
        curl_easy_getinfo(request.get(), CURLINFO_RESPONSE_CODE, &status);
        if (status == 200) return bytes;
        if (status >= 300 && status < 400) {
            char* location = nullptr;
            curl_easy_getinfo(request.get(), CURLINFO_REDIRECT_URL, &location);
            if (!location) throw std::runtime_error("Plugin redirect has no destination");
            url = location; continue;
        }
        throw std::runtime_error("Plugin package is not available for download");
    }
    throw std::runtime_error("Too many plugin redirects");
}
} // namespace rimes::runtime
