#pragma once

// Minimal Steamworks API surface used by fake2d's Steam adapter, for
// compile-only CI checks (`-DFAKE2D_TEST_STEAM_HEADERS=ON`). It is NOT the
// Steamworks SDK and must never be shipped: the real backend is only built
// against the proprietary SDK.

#include <cstdint>

class ISteamUtils {
public:
    const char *GetSteamUILanguage() { return "english"; }
};

class ISteamUserStats {
public:
    bool SetAchievement(const char *) { return true; }
    bool StoreStats() { return true; }
    bool RequestCurrentStats() { return true; }
};

class ISteamFriends {
public:
    bool SetRichPresence(const char *, const char *) { return true; }
    void ClearRichPresence() {}
};

inline bool SteamAPI_Init() { return true; }
inline void SteamAPI_Shutdown() {}
inline void SteamAPI_RunCallbacks() {}
inline bool SteamAPI_RestartAppIfNecessary(std::uint32_t) { return false; }

inline ISteamUtils *SteamUtils() {
    static ISteamUtils utils;
    return &utils;
}

inline ISteamUserStats *SteamUserStats() {
    static ISteamUserStats stats;
    return &stats;
}

inline ISteamFriends *SteamFriends() {
    static ISteamFriends friends;
    return &friends;
}
