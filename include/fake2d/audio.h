#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fake2d {

/// Minimal one-shot clip player built on miniaudio.
///
/// Clips are in-memory mono float PCM (normalized -1..1) at the device rate.
/// Playback failure is tolerated: Init() returns false when no audio device
/// exists (headless CI) and Play() becomes a no-op.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;

    /// Open a playback device (44.1 kHz stereo f32). Safe to call on hosts
    /// without audio; returns false and disables playback instead of failing.
    bool Init();
    void Shutdown();

    /// Register a mono float clip (44.1 kHz). Returns false when audio is
    /// disabled or the clip is empty; name must be unique.
    bool AddClip(std::string_view name, const std::vector<float> &mono_pcm);

    /// Decode a PCM WAV file (8/16-bit integer, mono/stereo, any rate),
    /// downmix to mono and linearly resample to 44.1 kHz, then register it.
    bool AddClipWav(std::string_view name, const std::string &wav_path);

    /// Fire-and-forget one-shot playback (voice pool capped, drops overflow).
    void Play(std::string_view name, float volume = 1.0f);

    /// Start the single looping music track (restarts if already playing).
    void PlayMusic(std::string_view name, float volume = 0.35f);
    void StopMusic();
    void SetMusicVolume(float volume);

    [[nodiscard]] bool IsEnabled() const { return enabled_; }
    [[nodiscard]] std::size_t ClipCount() const { return clips_.size(); }
    [[nodiscard]] std::size_t PlayingCount() const;

private:
    struct Clip {
        std::string name;
        std::vector<float> pcm;
    };
    struct Voice {
        const Clip *clip = nullptr;
        double cursor = 0.0; // samples (double for fractional stepping)
        double step = 1.0;   // samples per output frame (pitch)
        float volume = 1.0f;
        bool active = false;
        bool loop = false;
        bool music = false;
    };

    void StartClip(std::string_view name, float volume, bool loop, bool music);

    struct Device;
    friend struct Device;

    void Mix(float *out, int frame_count);

    bool enabled_ = false;
    void *device_ = nullptr; // ma_device*
    std::vector<Clip> clips_;
    std::vector<Voice> voices_;
};

} // namespace fake2d
