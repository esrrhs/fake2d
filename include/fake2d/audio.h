#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fake2d {

/// Mixer bus. Every voice is multiplied by its own bus gain and by the
/// master gain, so `effective = voice_gain * bus * master`.
enum class AudioBus {
    Master = 0, ///< multiplies everything
    Sfx = 1,    ///< one-shot clips (Play)
    Music = 2,  ///< the looping track (PlayMusic)
};

/// Mono float PCM clip player + mixer built on miniaudio.
///
/// Clips are in-memory mono float PCM (normalized -1..1) at the device rate.
/// The mixer is deliberately independent of the output device: `Init()`
/// returns false when no audio device exists (headless CI), but clips still
/// register, voices still run, and `Mix()` still renders — only the automatic
/// playback is missing. That is what makes the mixer testable without sound
/// hardware, and it costs nothing on hosts that do have one.
class AudioEngine {
public:
    /// Mixer sample rate: every clip is resampled to this.
    static constexpr int kSampleRate = 44100;
    /// Size of the voice pool (one-shot overflow is dropped).
    static constexpr std::size_t kMaxVoices = 32;

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;

    /// Open a playback device (44.1 kHz stereo f32). Safe to call on hosts
    /// without audio: returns false and leaves the mixer usable but silent.
    bool Init();
    void Shutdown();

    /// Register a mono float clip (44.1 kHz). Returns false when the clip is
    /// empty or the name is taken. Works with or without a device.
    bool AddClip(std::string_view name, const std::vector<float> &mono_pcm);

    /// Decode a PCM WAV file (8/16-bit integer, mono/stereo, any rate),
    /// downmix to mono and linearly resample to 44.1 kHz, then register it.
    bool AddClipWav(std::string_view name, const std::string &wav_path);

    /// Fire-and-forget one-shot playback (voice pool capped, drops overflow).
    void Play(std::string_view name, float volume = 1.0f);

    /// Start the looping music track. When `fade_seconds > 0` the new track
    /// fades in while any track already playing fades out (cross-fade), so
    /// the switch never clicks; with 0 it cuts over like before.
    void PlayMusic(std::string_view name, float volume = 0.35f,
                   float fade_seconds = 0.0f);
    /// Fade the music out over `fade_seconds` (0 = stop immediately).
    void StopMusic(float fade_seconds = 0.0f);
    /// Ramp the music's gain to `volume` over `seconds`.
    void FadeMusicTo(float volume, float seconds);
    /// Set the music gain immediately (cancels any running ramp).
    void SetMusicVolume(float volume);

    /// Bus gains, clamped to [0, 1]. The defaults are all 1, so existing
    /// scripts keep their current levels until they touch a bus.
    void SetBusVolume(AudioBus bus, float volume);
    [[nodiscard]] float BusVolume(AudioBus bus) const;

    /// Freeze the mix: `Mix` emits silence and neither cursors nor fades
    /// advance, so resuming continues exactly where it stopped.
    void SetPaused(bool paused);
    [[nodiscard]] bool IsPaused() const { return paused_; }

    [[nodiscard]] bool IsMusicPlaying() const;
    /// Name of the playing (or fading-in) music track, "" when none.
    [[nodiscard]] std::string_view MusicName() const;
    /// The music voice's target gain (not multiplied by any bus).
    [[nodiscard]] float MusicVolume() const;

    /// Whether a playback device was opened. False in headless CI, where the
    /// mixer still runs but nobody hears it.
    [[nodiscard]] bool IsEnabled() const { return enabled_; }
    [[nodiscard]] std::size_t ClipCount() const { return clips_.size(); }
    [[nodiscard]] std::size_t PlayingCount() const;
    /// Loudest absolute sample of the most recent mixed block (VU meters).
    [[nodiscard]] float Peak() const;

    /// Render `frame_count` stereo frames (interleaved L/R) — the same code
    /// the device callback drives, exposed so tests can mix offline.
    void Mix(float *out, int frame_count);

private:
    struct Clip {
        std::string name;
        std::vector<float> pcm;
    };
    struct Voice {
        const Clip *clip = nullptr;
        double cursor = 0.0; // samples (double for fractional stepping)
        double step = 1.0;   // samples per output frame (pitch)
        /// Gain envelope: `gain_to` is the settled gain; while `fade_dur > 0`
        /// the gain ramps from `gain_from` to `gain_to` over that many
        /// seconds. A ramp that ends at 0 and has `fade_ends_voice` frees
        /// the voice.
        float gain_from = 1.0f;
        float gain_to = 1.0f;
        float fade_pos = 0.0f;
        float fade_dur = 0.0f;
        bool fade_ends_voice = false;
        bool active = false;
        bool loop = false;
        bool music = false;
    };

    void StartClip(std::string_view name, float volume, bool loop, bool music,
                   float fade_seconds);
    /// Gain of the envelope right now (before any bus is applied).
    static float EnvelopeGain(const Voice &voice);
    /// Start a ramp from the voice's current gain to `to`.
    static void RampTo(Voice &voice, float to, float seconds, bool ends_voice);

    struct Device;
    friend struct Device;

    bool enabled_ = false;
    void *device_ = nullptr; // ma_device*
    std::vector<Clip> clips_;
    std::vector<Voice> voices_;
    float bus_master_ = 1.0f;
    float bus_sfx_ = 1.0f;
    float bus_music_ = 1.0f;
    bool paused_ = false;
    float peak_ = 0.0f;
    /// Guards every clip/voice field against the device thread. Lives on the
    /// engine rather than in Device so the mixer works with no device at all.
    mutable void *mutex_ = nullptr; // std::mutex*
};

} // namespace fake2d
