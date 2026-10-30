#include "fake2d/steam.h"

#if defined(FAKE2D_WITH_STEAM_SDK)
#include <steam/steam_api.h>
#endif

#include <string>

namespace fake2d::steam {

namespace {
bool g_available = false;
bool g_cloud = false;
std::string g_language = "english";
} // namespace

bool Init(std::uint32_t app_id) {
    (void)app_id;
#if defined(FAKE2D_WITH_STEAM_SDK)
    if (g_available) {
        return true;
    }
    if (!SteamAPI_Init()) {
        return false;
    }
    g_available = true;
    if (ISteamUtils *utils = SteamUtils()) {
        const char *language = utils->GetSteamUILanguage();
        if (language != nullptr && language[0] != '\0') {
            g_language = language;
        }
    }
    return true;
#else
    g_available = false;
    return false;
#endif
}

bool Available() {
    return g_available;
}

std::string_view BackendName() {
#if defined(FAKE2D_WITH_STEAM_SDK)
    return "steamworks";
#else
    return "stub";
#endif
}

bool RestartAppIfNecessary(std::uint32_t app_id) {
#if defined(FAKE2D_WITH_STEAM_SDK)
    // true = Steam relaunched us; the caller must exit.
    return SteamAPI_RestartAppIfNecessary(app_id);
#else
    (void)app_id;
    return false;
#endif
}

void Shutdown() {
#if defined(FAKE2D_WITH_STEAM_SDK)
    if (g_available) {
        SteamAPI_Shutdown();
    }
#endif
    g_available = false;
}

void Tick() {
#if defined(FAKE2D_WITH_STEAM_SDK)
    if (g_available) {
        SteamAPI_RunCallbacks();
    }
#endif
}

void UnlockAchievement(std::string_view id) {
#if defined(FAKE2D_WITH_STEAM_SDK)
    if (g_available && !id.empty()) {
        if (ISteamUserStats *stats = SteamUserStats()) {
            stats->SetAchievement(std::string(id).c_str());
            stats->StoreStats();
        }
    }
#else
    (void)id;
#endif
}

void SetRichPresence(std::string_view key, std::string_view value) {
#if defined(FAKE2D_WITH_STEAM_SDK)
    if (g_available && !key.empty()) {
        if (ISteamFriends *friends = SteamFriends()) {
            friends->SetRichPresence(std::string(key).c_str(),
                                     std::string(value).c_str());
        }
    }
#else
    (void)key;
    (void)value;
#endif
}

void ClearRichPresence() {
#if defined(FAKE2D_WITH_STEAM_SDK)
    if (g_available) {
        if (ISteamFriends *friends = SteamFriends()) {
            friends->ClearRichPresence();
        }
    }
#endif
}

std::string Language() {
    return g_language;
}

void SetCloudEnabled(bool enabled) {
    g_cloud = enabled;
}

bool CloudEnabled() {
    return g_cloud;
}

} // namespace fake2d::steam
