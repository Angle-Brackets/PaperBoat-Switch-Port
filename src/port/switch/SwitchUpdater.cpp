// In-game updater: checks the latest GitHub release, downloads its paperboat.nro next to the running one,
// and swaps it in on exit (romfs is read from the running file, so it can't be replaced while mounted).
// Switch tab > Updates; also checks once on launch unless turned off.

#ifdef __SWITCH__

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <libultraship/bridge/consolevariablebridge.h>

#include "port/build.h"
#include "common.h"
#include "game_modes.h"
#include "port/switch/SwitchDebug.h"
#include "port/ui/Notification.h"

#ifndef SWITCH_RELEASE_TAG
#define SWITCH_RELEASE_TAG ""
#endif

extern "C" char** __system_argv;

namespace {
constexpr const char* kLatestReleaseUrl = "https://api.github.com/repos/" SWITCH_UPDATE_REPO "/releases/latest";
constexpr const char* kAssetName = "paperboat.nro";
// Checked on launch after this long, so it doesn't compete with boot.
constexpr auto kLaunchCheckDelay = std::chrono::seconds(20);

std::atomic<int> sState { SWITCH_UPDATE_IDLE };
std::atomic<int> sProgress { 0 }; // download, percent
std::atomic<bool> sAnnounce { false };
std::mutex sMutex;
std::string sLatestTag;
std::string sDownloadUrl;
std::string sError;
bool sPendingSwap = false;

// The running NRO, as the loader passed it.
std::string NroPath() {
    return __system_argv != nullptr && __system_argv[0] != nullptr ? __system_argv[0] : "";
}

std::string NewNroPath() {
    return NroPath() + ".new";
}

// "switch-v1.0.3" / "1.0.3.1" -> {1, 0, 3, 1}
std::vector<int> VersionNumbers(const std::string& s) {
    std::vector<int> out;
    int cur = -1;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
        } else {
            if (cur >= 0) {
                out.push_back(cur);
            }
            cur = -1;
            if (c == '-' && !out.empty()) {
                break; // pre-release suffix
            }
        }
    }
    if (cur >= 0) {
        out.push_back(cur);
    }
    return out;
}

std::vector<int> OwnVersion() {
    if (SWITCH_RELEASE_TAG[0] != '\0') {
        return VersionNumbers(SWITCH_RELEASE_TAG);
    }
    return { gBuildVersionMajor, gBuildVersionMinor, gBuildVersionPatch };
}

// "switch-v1.0.3-rc1" and "-beta2" are pre-releases of 1.0.3.
bool IsPreRelease(const std::string& tag) {
    const size_t v = tag.find_first_of("0123456789");
    return v != std::string::npos && tag.find('-', v) != std::string::npos;
}

bool OwnIsPreRelease() {
    return IsPreRelease(SWITCH_RELEASE_TAG);
}

// Compares numbers first; with equal numbers a release is newer than its pre-releases, so an RC build
// is offered the final release of the same version.
bool IsNewer(const std::vector<int>& a, bool aPre, const std::vector<int>& b, bool bPre) {
    for (size_t i = 0; i < std::max(a.size(), b.size()); i++) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y) {
            return x > y;
        }
    }
    return bPre && !aPre;
}

void Fail(const std::string& what) {
    SPDLOG_WARN("[update] {}", what);
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sError = what;
    }
    sState = SWITCH_UPDATE_FAILED;
}

CURL* NewRequest(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (curl != nullptr) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "PaperBoat-Switch-Updater");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // release assets redirect to a CDN
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    }
    return curl;
}

size_t AppendToString(char* data, size_t size, size_t count, void* out) {
    static_cast<std::string*>(out)->append(data, size * count);
    return size * count;
}

size_t WriteToFile(char* data, size_t size, size_t count, void* file) {
    return fwrite(data, size, count, static_cast<FILE*>(file)) * size;
}

int OnProgress(void*, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    if (total > 0) {
        sProgress = (int) (now * 100 / total);
    }
    return 0;
}

void CheckThread(bool announce) {
    std::string body;
    CURL* curl = NewRequest(kLatestReleaseUrl);
    if (curl == nullptr) {
        return Fail("Couldn't start a request.");
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AppendToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    const CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    // releases/latest skips pre-releases, so it's 404 until a full release is published: nothing newer.
    if (res == CURLE_HTTP_RETURNED_ERROR && status == 404) {
        SPDLOG_INFO("[update] no full release published yet");
        sState = SWITCH_UPDATE_CURRENT;
        return;
    }
    if (res != CURLE_OK) {
        return Fail(std::string("Couldn't reach GitHub: ") + curl_easy_strerror(res));
    }

    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.contains("tag_name")) {
        return Fail("GitHub sent an unexpected reply.");
    }
    const std::string tag = json["tag_name"].get<std::string>();
    std::string url;
    for (const auto& asset : json.value("assets", nlohmann::json::array())) {
        if (asset.value("name", "") == kAssetName) {
            url = asset.value("browser_download_url", "");
        }
    }
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sLatestTag = tag;
        sDownloadUrl = url;
    }
    const bool newer = IsNewer(VersionNumbers(tag), IsPreRelease(tag), OwnVersion(), OwnIsPreRelease());
    SPDLOG_INFO("[update] latest release {} ({})", tag, newer ? "newer" : "not newer");
    if (newer && url.empty()) {
        return Fail("The latest release has no " + std::string(kAssetName) + ".");
    }
    sState = newer ? SWITCH_UPDATE_AVAILABLE : SWITCH_UPDATE_CURRENT;
    sAnnounce = newer && announce;
}

void DownloadThread() {
    std::string url;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        url = sDownloadUrl;
    }
    const std::string dest = NewNroPath();
    FILE* file = fopen(dest.c_str(), "wb");
    if (file == nullptr) {
        return Fail("Couldn't write " + dest + ".");
    }
    CURL* curl = NewRequest(url);
    if (curl == nullptr) {
        fclose(file);
        return Fail("Couldn't start a request.");
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    const bool written = fclose(file) == 0;
    if (res != CURLE_OK || !written) {
        remove(dest.c_str());
        return Fail(std::string("Download failed: ") + curl_easy_strerror(res));
    }

    // An NRO has "NRO0" at 0x10; anything else (an error page, a truncated file) is not swapped in.
    char magic[4] = {};
    file = fopen(dest.c_str(), "rb");
    const bool valid = file != nullptr && fseek(file, 0x10, SEEK_SET) == 0 && fread(magic, 1, 4, file) == 4 &&
                       memcmp(magic, "NRO0", 4) == 0;
    if (file != nullptr) {
        fclose(file);
    }
    if (!valid) {
        remove(dest.c_str());
        return Fail("The download isn't a valid NRO.");
    }
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sPendingSwap = true;
    }
    SPDLOG_INFO("[update] downloaded {}", dest);
    sState = SWITCH_UPDATE_READY;
}

void Start(int busyState, void (*body)()) {
    static std::once_flag sCurlInit;
    std::call_once(sCurlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    // libnx can't detach threads (detach() throws), so keep the worker and join it before the next one; it has
    // finished by then, since a check or download only starts when none is running. Never destroyed: a joinable
    // std::thread's destructor would abort at exit.
    static std::thread* sWorker = nullptr;
    if (sWorker != nullptr) {
        sWorker->join();
        delete sWorker;
    }
    sState = busyState;
    sWorker = new std::thread(body);
}
} // namespace

extern "C" {

int SwitchUpdater_State(void) {
    return sState;
}

int SwitchUpdater_Progress(void) {
    return sProgress;
}

const char* SwitchUpdater_OwnVersion(void) {
    static const std::string sVersion = SWITCH_RELEASE_TAG[0] != '\0' ? std::string(SWITCH_RELEASE_TAG)
                                                                        : std::string(gBuildVersion) + " (dev build)";
    return sVersion.c_str();
}

// Copies so the UI never holds a reference into strings the worker writes.
void SwitchUpdater_Info(char* latestTag, size_t tagSize, char* error, size_t errorSize) {
    std::lock_guard<std::mutex> lock(sMutex);
    snprintf(latestTag, tagSize, "%s", sLatestTag.c_str());
    snprintf(error, errorSize, "%s", sError.c_str());
}

void SwitchUpdater_Check(void) {
    const int state = sState;
    if (state == SWITCH_UPDATE_CHECKING || state == SWITCH_UPDATE_DOWNLOADING || state == SWITCH_UPDATE_READY) {
        return;
    }
    Start(SWITCH_UPDATE_CHECKING, [] { CheckThread(false); });
}

void SwitchUpdater_Download(void) {
    if (sState != SWITCH_UPDATE_AVAILABLE) {
        return;
    }
    sProgress = 0;
    Start(SWITCH_UPDATE_DOWNLOADING, DownloadThread);
}

void SwitchUpdater_Tick(void) {
    static const auto sStart = std::chrono::steady_clock::now();
    static bool sLaunchChecked = false;
    if (!sLaunchChecked && std::chrono::steady_clock::now() - sStart > kLaunchCheckDelay) {
        sLaunchChecked = true;
        if (CVarGetInteger(SWITCH_CHECK_UPDATES, 1) && sState == SWITCH_UPDATE_IDLE) {
            Start(SWITCH_UPDATE_CHECKING, [] { CheckThread(true); });
        }
    }
    // Only on the title screen or file select, never mid-game: if the check finishes during play, the
    // notification waits for the next return to the title. Emitted from the main loop, like the game's own.
    const s32 mode = get_game_mode();
    const bool atMenus = mode == GAME_MODE_TITLE_SCREEN || mode == GAME_MODE_FILE_SELECT;
    if (atMenus && sAnnounce.exchange(false)) {
        std::string tag;
        {
            std::lock_guard<std::mutex> lock(sMutex);
            tag = sLatestTag;
        }
        Notification::Emit({ .prefix = "Update available:", .message = tag, .suffix = "(Switch tab > Updates)" });
    }
}

// From userAppExit, after romfs is unmounted: the running file can be replaced now, and the loader starts
// whatever is at the path (see GameEngine::RelaunchIfRequested).
void SwitchUpdater_ApplyPending(void) {
    if (!sPendingSwap) {
        return;
    }
    const std::string nro = NroPath();
    const std::string fresh = NewNroPath();
    const std::string old = nro + ".old";
    remove(old.c_str());
    if (rename(nro.c_str(), old.c_str()) != 0) {
        return; // keep running the current version; the download is retried next time
    }
    if (rename(fresh.c_str(), nro.c_str()) != 0) {
        rename(old.c_str(), nro.c_str());
        return;
    }
    remove(old.c_str());
}
}

#endif
