#include "BetterMomentum.h"
#include "settings.h"
#include <curl/curl.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <atomic>
#include "FortGameStateAthena.h"

#ifndef CURLOPT_RESPONSE_CODE
#define CURLOPT_RESPONSE_CODE CURLINFO_RESPONSE_CODE
#endif
#include "FortGameModeAthena.h"
#include "gui.h"

std::string g_serverSecretKey;
std::string g_serverId;
std::string g_backendUrl = BACKEND_URL;
std::string g_masterAuthKey = SERVER_AUTH_KEY;
std::string g_webhookUptimeUrl = WEBHOOK_UPTIME_URL;
std::string g_publicIp = PUBLIC_IP;
bool g_configLoaded = true;
bool g_serverRegistered = false;

bool g_joinState = true;
int g_gamePort = 0;
std::string g_gamePlaylist;

std::thread g_heartbeatThread;
std::atomic<bool> g_heartbeatRunning{ false };
std::atomic<bool> g_stopHeartbeat{ false };
std::thread g_countThread;
std::atomic<bool> g_countRunning{ false };
std::atomic<bool> g_stopCount{ false };

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string BuildJsonPayload(const std::string& ip, int port, const std::string& playlist, const std::string& serverKey) {
    std::ostringstream json;
    json << "{"
        << "\"ip\":\"" << ip << "\","
        << "\"port\":" << port << ","
        << "\"playlist\":\"" << playlist << "\","
        << "\"serverKey\":\"" << serverKey << "\""
        << "}";
    return json.str();
}

std::string BuildHeartbeatPayload(const std::string& ip, int port, const std::string& playlist, const std::string& serverKey, bool joinable) {
    std::ostringstream json;
    json << "{"
        << "\"ip\":\"" << ip << "\","
        << "\"port\":" << port << ","
        << "\"playlist\":\"" << playlist << "\","
        << "\"serverKey\":\"" << serverKey << "\","
        << "\"joinable\":" << (joinable ? "true" : "false")
        << "}";
    return json.str();
}

std::string ExtractJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":\"";
    size_t startPos = json.find(searchKey);
    if (startPos == std::string::npos) return "";

    startPos += searchKey.length();
    size_t endPos = json.find("\"", startPos);
    if (endPos == std::string::npos) return "";

    return json.substr(startPos, endPos - startPos);
}

void HeartbeatWorker() {
    while (!g_stopHeartbeat.load()) {
        for (int i = 0; i < 450 && !g_stopHeartbeat.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!g_stopHeartbeat.load()) {
            SendHeartbeat();
        }
    }
    g_heartbeatRunning.store(false);
}

void CountWorker() {
    while (!g_stopCount.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (g_stopCount.load()) break;

        AFortGameStateAthena* gameState = nullptr;
        if (auto world = GetWorld()) {
            gameState = Cast<AFortGameStateAthena>(world->GetGameState());
        }

        if (!gameState) continue;

        int players = gameState->GetPlayersLeft();

        if (players >= 2) {
            bStartedBus = true;

            SetJoinState(false);
            StopHeartbeat();
            SendHeartbeat();
            StartHeartbeat();
            StopCount();

            auto GameMode = (AFortGameMode*)GetWorld()->GetGameMode();
            auto GameState = Cast<AFortGameStateAthena>(GameMode->GetGameState());

            AmountOfPlayersWhenBusStart = GameState->GetPlayersLeft();

            static auto WarmupCountdownEndTimeOffset = GameState->GetOffset("WarmupCountdownEndTime");
            float TimeSeconds = GameState->GetServerWorldTimeSeconds();
            float Duration = 10;
            float EarlyDuration = Duration;

            static auto WarmupCountdownStartTimeOffset = GameState->GetOffset("WarmupCountdownStartTime");
            static auto WarmupCountdownDurationOffset = GameMode->GetOffset("WarmupCountdownDuration");
            static auto WarmupEarlyCountdownDurationOffset = GameMode->GetOffset("WarmupEarlyCountdownDuration");

            GameState->Get<float>(WarmupCountdownEndTimeOffset) = TimeSeconds + Duration;
            GameMode->Get<float>(WarmupCountdownDurationOffset) = Duration;
            GameMode->Get<float>(WarmupEarlyCountdownDurationOffset) = EarlyDuration;
        }
    }

    g_countRunning.store(false);
}

extern "C" __declspec(dllexport) bool StartCount() {
    if (g_countRunning.load()) {
        StopCount();
    }
    g_stopCount.store(false);
    g_countRunning.store(true);

    try {
        g_countThread = std::thread(CountWorker);
        g_countThread.detach();
        return true;
    }
    catch (...) {
        g_countRunning.store(false);
        return false;
    }
}

extern "C" __declspec(dllexport) void StopCount() {
    if (g_countRunning.load()) {
        g_stopCount.store(true);
        int timeout = 10;
        while (g_countRunning.load() && timeout > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            timeout--;
        }
    }
}

extern "C" __declspec(dllexport) bool RegisterServer() {
    if (!g_configLoaded) return false;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string readBuffer;
    std::string jsonString = BuildJsonPayload(g_publicIp, g_gamePort, g_gamePlaylist, g_masterAuthKey);
    std::string url = g_backendUrl + "/bettermomentum/addserver";

    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);

    CURLcode res = curl_easy_perform(curl);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return false;

    if (response_code == 201) {
        g_serverRegistered = true;
        g_serverSecretKey = ExtractJsonValue(readBuffer, "serverSecretKey");
        g_serverId = ExtractJsonValue(readBuffer, "serverId");

        return !g_serverSecretKey.empty() && !g_serverId.empty();
    }

    return false;
}

extern "C" __declspec(dllexport) bool RemoveServer() {
    if (g_serverSecretKey.empty() || !g_configLoaded) return false;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string readBuffer;
    std::string jsonString = BuildJsonPayload(g_publicIp, g_gamePort, g_gamePlaylist, g_serverSecretKey);
    std::string url = g_backendUrl + "/bettermomentum/removeserver";

    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);

    CURLcode res = curl_easy_perform(curl);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return false;

    if (response_code == 200) {
        g_serverSecretKey.clear();
        g_serverId.clear();
        g_serverRegistered = false;
        return true;
    }

    return false;
}

extern "C" __declspec(dllexport) bool SendHeartbeat() {
    if (!g_serverRegistered || g_serverSecretKey.empty() || !g_configLoaded) return false;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string readBuffer;
    std::string jsonString = BuildHeartbeatPayload(g_publicIp, g_gamePort, g_gamePlaylist, g_serverSecretKey, g_joinState);
    std::string url = g_backendUrl + "/bettermomentum/heartbeat";

    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);

    CURLcode res = curl_easy_perform(curl);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK && response_code == 200;
}

extern "C" __declspec(dllexport) bool StartHeartbeat() {
    if (g_heartbeatRunning.load()) {
        StopHeartbeat();
    }

    if (!g_serverRegistered || g_serverSecretKey.empty() || !g_configLoaded) {
        return false;
    }

    g_stopHeartbeat.store(false);
    g_heartbeatRunning.store(true);

    try {
        g_heartbeatThread = std::thread(HeartbeatWorker);
        g_heartbeatThread.detach();
        return true;
    }
    catch (...) {
        g_heartbeatRunning.store(false);
        return false;
    }
}

extern "C" __declspec(dllexport) void StopHeartbeat() {
    if (g_heartbeatRunning.load()) {
        g_stopHeartbeat.store(true);

        int timeout = 10;
        while (g_heartbeatRunning.load() && timeout > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            timeout--;
        }
    }
}

extern "C" __declspec(dllexport) bool IsHeartbeatRunning() {
    return g_heartbeatRunning.load();
}

extern "C" __declspec(dllexport) const char* GetServerSecretKey() { return g_serverSecretKey.empty() ? nullptr : g_serverSecretKey.c_str(); }
extern "C" __declspec(dllexport) const char* GetServerId() { return g_serverId.empty() ? nullptr : g_serverId.c_str(); }
extern "C" __declspec(dllexport) bool LoadBetterMomentum() { return g_configLoaded; }
extern "C" __declspec(dllexport) const char* GetBackendUrl() { return g_backendUrl.c_str(); }
extern "C" __declspec(dllexport) const char* GetMasterAuthKey() { return g_masterAuthKey.c_str(); }
extern "C" __declspec(dllexport) const char* GetWebhookUptimeUrl() { return g_webhookUptimeUrl.c_str(); }
extern "C" __declspec(dllexport) const char* GetPublicIp() { return g_publicIp.c_str(); }
extern "C" __declspec(dllexport) bool IsConfigLoaded() { return g_configLoaded; }

extern "C" __declspec(dllexport) void SetGamePort(int port) { g_gamePort = port; }
extern "C" __declspec(dllexport) int GetGamePort() { return g_gamePort; }

extern "C" __declspec(dllexport) void SetGamePlaylist(const char* playlist) {
    if (!playlist) return;
    std::string pl = playlist;
    size_t lastSlash = pl.find_last_of('/');
    if (lastSlash != std::string::npos) pl = pl.substr(lastSlash + 1);
    size_t firstDot = pl.find_first_of('.');
    if (firstDot != std::string::npos) pl = pl.substr(0, firstDot);
    std::transform(pl.begin(), pl.end(), pl.begin(), ::tolower);
    g_gamePlaylist = pl;
}

extern "C" __declspec(dllexport) const char* GetGamePlaylist() { return g_gamePlaylist.empty() ? nullptr : g_gamePlaylist.c_str(); }
extern "C" __declspec(dllexport) void SetJoinState(bool state) { g_joinState = state; }