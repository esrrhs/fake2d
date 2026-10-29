#include "fake2d/audio.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>

namespace fake2d {


struct AudioEngine::Device {
    ma_device ma{};

    static void DataCallback(ma_device *device, void *output, const void * /*input*/,
                             ma_uint32 frame_count) {
        auto *engine = static_cast<AudioEngine *>(device->pUserData);
        if (engine != nullptr) {
            engine->Mix(static_cast<float *>(output), static_cast<int>(frame_count));
        }
    }
};

namespace {

// The mixer owns its own lock: a host with no audio device still has clips,
// voices and fades to protect.
std::mutex &MixerMutex(void *opaque) {
    return *static_cast<std::mutex *>(opaque);
}

float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

} // namespace

AudioEngine::AudioEngine() {
    voices_.resize(kMaxVoices);
    mutex_ = new std::mutex();
}

AudioEngine::~AudioEngine() {
    Shutdown();
    // Stream decoders hold the file open; clips outlive the device, so this
    // is the destructor's job, not Shutdown()'s.
    ReleaseStreams();
    delete static_cast<std::mutex *>(mutex_);
    mutex_ = nullptr;
}

bool AudioEngine::Init() {
    Shutdown();

    auto *device = new Device();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = AudioEngine::kSampleRate;
    config.dataCallback = &Device::DataCallback;
    config.pUserData = this;

    if (ma_device_init(nullptr, &config, &device->ma) != MA_SUCCESS) {
        delete device;
        std::fprintf(stderr, "fake2d: no audio device available, sound disabled\n");
        return false;
    }
    if (ma_device_start(&device->ma) != MA_SUCCESS) {
        ma_device_uninit(&device->ma);
        delete device;
        std::fprintf(stderr, "fake2d: audio device failed to start, sound disabled\n");
        return false;
    }

    device_ = device;
    enabled_ = true;
    return true;
}

void AudioEngine::Shutdown() {
    if (device_ != nullptr) {
        auto *device = static_cast<Device *>(device_);
        ma_device_stop(&device->ma);
        ma_device_uninit(&device->ma);
        delete device;
        device_ = nullptr;
    }
    enabled_ = false;
}

bool AudioEngine::AddClip(std::string_view name, const std::vector<float> &mono_pcm) {
    // No enabled_ gate: clips are just memory, and a headless host still has
    // to be able to register and mix them.
    if (mono_pcm.empty()) {
        return false;
    }
    const std::string key(name);
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    const auto existing = std::find_if(clips_.begin(), clips_.end(),
                                       [&](const Clip &c) { return c.name == key; });
    if (existing != clips_.end()) {
        return false;
    }
    Clip clip;
    clip.name = key;
    clip.pcm = mono_pcm;
    clips_.push_back(std::move(clip));
    return true;
}

namespace {

/// The decoder config every file path shares: mono float at the mixer rate,
/// which makes miniaudio do the channel downmix and the resample itself.
ma_decoder_config DecoderConfig() {
    return ma_decoder_config_init(ma_format_f32, 1,
                                  static_cast<ma_uint32>(fake2d::AudioEngine::kSampleRate));
}

/// Read the whole file through `decoder` into `pcm`. The decoder is left for
/// the caller to uninit.
bool ReadAllPcm(ma_decoder *decoder, std::vector<float> &pcm) {
    std::vector<float> chunk(4096);
    for (;;) {
        ma_uint64 read = 0;
        if (ma_decoder_read_pcm_frames(decoder, chunk.data(),
                                       static_cast<ma_uint64>(chunk.size()),
                                       &read) != MA_SUCCESS) {
            break;
        }
        if (read == 0) {
            break;
        }
        pcm.insert(pcm.end(), chunk.begin(),
                   chunk.begin() + static_cast<std::ptrdiff_t>(read));
    }
    return !pcm.empty();
}

} // namespace

bool AudioEngine::AddClipFile(std::string_view name, const std::string &path) {
    const std::string key(name);
    {
        std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
        const auto existing = std::find_if(clips_.begin(), clips_.end(),
                                           [&](const Clip &c) { return c.name == key; });
        if (existing != clips_.end()) {
            return false;
        }
    }
    const ma_decoder_config config = DecoderConfig();
    ma_decoder decoder{};
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
        return false;
    }
    std::vector<float> pcm;
    const bool ok = ReadAllPcm(&decoder, pcm);
    ma_decoder_uninit(&decoder);
    if (!ok) {
        return false;
    }
    return AddClip(key, pcm);
}

bool AudioEngine::AddStream(std::string_view name, const std::string &path) {
    const std::string key(name);
    {
        std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
        const auto existing = std::find_if(clips_.begin(), clips_.end(),
                                           [&](const Clip &c) { return c.name == key; });
        if (existing != clips_.end()) {
            return false;
        }
    }
    const ma_decoder_config config = DecoderConfig();
    auto *decoder = new ma_decoder();
    if (ma_decoder_init_file(path.c_str(), &config, decoder) != MA_SUCCESS) {
        delete decoder;
        return false;
    }
    ma_uint64 length = 0;
    // Frames at the *decoder output* rate, i.e. the mixer rate, which is the
    // unit the voice cursor runs in.
    if (ma_decoder_get_length_in_pcm_frames(decoder, &length) != MA_SUCCESS || length == 0) {
        ma_decoder_uninit(decoder);
        delete decoder;
        return false;
    }
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    // Re-check: another thread could have taken the name while decoding.
    const auto dup = std::find_if(clips_.begin(), clips_.end(),
                                  [&](const Clip &c) { return c.name == key; });
    if (dup != clips_.end()) {
        ma_decoder_uninit(decoder);
        delete decoder;
        return false;
    }
    Clip clip;
    clip.name = key;
    clip.stream = true;
    clip.decoder = decoder;
    clip.length_frames = length;
    // Empty window on purpose: the first mix decodes what it needs.
    clips_.push_back(std::move(clip));
    return true;
}

bool AudioEngine::FillWindow(Clip &clip, std::uint64_t start_frame) {
    auto *decoder = static_cast<ma_decoder *>(clip.decoder);
    if (decoder == nullptr) {
        return false;
    }
    // Sequential playback asks for the frame right after the window we just
    // read, and the decoder is already sitting there — keep reading instead of
    // seeking. A seek resets the resampler, and a reset at every window
    // boundary is audible (and makes a stream differ from a straight decode).
    // A jump — loop wrap, restart, or a second voice — still seeks.
    const std::uint64_t next = clip.window_start + clip.pcm.size();
    if (start_frame != next &&
        ma_decoder_seek_to_pcm_frame(decoder, start_frame) != MA_SUCCESS) {
        return false;
    }
    clip.pcm.assign(kStreamWindowFrames, 0.0f);
    ma_uint64 read = 0;
    if (ma_decoder_read_pcm_frames(decoder, clip.pcm.data(),
                                   static_cast<ma_uint64>(kStreamWindowFrames),
                                   &read) != MA_SUCCESS) {
        return false;
    }
    clip.pcm.resize(static_cast<std::size_t>(read));
    clip.window_start = start_frame;
    return !clip.pcm.empty();
}

void AudioEngine::ReleaseStreams() {
    for (Clip &clip : clips_) {
        if (clip.decoder != nullptr) {
            auto *decoder = static_cast<ma_decoder *>(clip.decoder);
            ma_decoder_uninit(decoder);
            delete decoder;
            clip.decoder = nullptr;
        }
    }
}

float AudioEngine::ClipDuration(std::string_view name) const {
    const std::string key(name);
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    const auto it = std::find_if(clips_.begin(), clips_.end(),
                                 [&](const Clip &c) { return c.name == key; });
    if (it == clips_.end()) {
        return 0.0f;
    }
    const std::uint64_t frames = it->stream ? it->length_frames : it->pcm.size();
    return static_cast<float>(frames) / static_cast<float>(kSampleRate);
}

std::size_t AudioEngine::ResidentFrames(std::string_view name) const {
    const std::string key(name);
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    const auto it = std::find_if(clips_.begin(), clips_.end(),
                                 [&](const Clip &c) { return c.name == key; });
    if (it == clips_.end()) {
        return 0;
    }
    // A stream's pcm *is* its window, so this reads the same for both kinds —
    // which is the point: it is the memory a track actually costs right now.
    return it->pcm.size();
}

AudioEngine::Voice *AudioEngine::StartClip(std::string_view name, float volume, bool loop,
                                           bool music, float fade_seconds, float pitch) {
    const float rate = std::clamp(pitch > 0.0f ? pitch : 1.0f, 0.25f, 4.0f);
    if (voices_.empty()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    const std::string key(name);
    // clips_ may be reallocated by AddClip, so resolve the pointer under the
    // lock and never hold a Clip* across an unlock.
    auto clip_it = std::find_if(clips_.begin(), clips_.end(),
                                [&](const Clip &c) { return c.name == key; });
    // A stream has no PCM until its first mix, so an empty window is not the
    // same thing as an empty clip.
    if (clip_it == clips_.end() || (clip_it->pcm.empty() && !clip_it->stream)) {
        return nullptr;
    }

    // Music: hand the track over in place when it is not cross-fading. A
    // fade hands the old voice to the ramp instead and starts a new one, so
    // both play at once for `fade_seconds`.
    if (music && fade_seconds <= 0.0f) {
        for (Voice &voice : voices_) {
            if (voice.music) {
                ResetVoice(voice, &*clip_it, volume);
                voice.active = true;
                voice.loop = true;
                voice.music = true;
                voice.step = rate;
                return &voice;
            }
        }
    } else if (music) {
        for (Voice &voice : voices_) {
            if (voice.music && voice.active && !voice.fade_ends_voice) {
                RampTo(voice, 0.0f, fade_seconds, true);
            }
        }
    }

    for (Voice &voice : voices_) {
        if (!voice.active) {
            ResetVoice(voice, &*clip_it, volume);
            voice.loop = loop;
            voice.music = music;
            voice.step = rate;
            if (fade_seconds > 0.0f) {
                // Fade in from silence; a fade out of 0 would end the voice.
                voice.gain_from = 0.0f;
                voice.fade_pos = 0.0f;
                voice.fade_dur = fade_seconds;
            } else {
                voice.gain_from = volume;
            }
            return &voice;
        }
    }
    // Voice pool exhausted: drop the newest request (oldest sounds finish).
    return nullptr;
}

void AudioEngine::Play(std::string_view name, float volume) {
    StartClip(name, volume, false, false, 0.0f);
}

void AudioEngine::PlayPitched(std::string_view name, float volume, float pitch) {
    StartClip(name, volume, false, false, 0.0f, pitch);
}

void AudioEngine::PlayLoop(std::string_view name, float volume, float pitch) {
    StartClip(name, volume, true, false, 0.0f, pitch);
}

void AudioEngine::StopClip(std::string_view name) {
    const std::string key(name);
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    for (Voice &voice : voices_) {
        if (voice.active && voice.clip != nullptr && voice.clip->name == key) {
            voice.active = false;
            voice.music = false;
        }
    }
}

void AudioEngine::PlayAt(std::string_view name, float volume, float x, float y) {
    Voice *voice = StartClip(name, volume, false, false, 0.0f);
    if (voice == nullptr) {
        return;
    }
    // Placed after StartClip so the position survives whatever the shared
    // acquisition path just reset.
    voice->positional = true;
    voice->pos_x = x;
    voice->pos_y = y;
    UpdateSpatial(*voice);
}

void AudioEngine::SetListener(float x, float y) {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    listener_x_ = x;
    listener_y_ = y;
    // Voices already sounding must follow the ears, otherwise walking past an
    // emitter would leave its sound parked where it started.
    for (Voice &voice : voices_) {
        UpdateSpatial(voice);
    }
}

void AudioEngine::SetListenerRange(float max_distance) {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    if (max_distance > 0.0f) {
        listener_range_ = max_distance;
        for (Voice &voice : voices_) {
            UpdateSpatial(voice);
        }
    }
}

float AudioEngine::ListenerX() const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    return listener_x_;
}

float AudioEngine::ListenerY() const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    return listener_y_;
}

float AudioEngine::ListenerRange() const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    return listener_range_;
}

void AudioEngine::ResetVoice(Voice &voice, Clip *clip, float volume) const {
    voice.clip = clip;
    voice.cursor = 0.0;
    voice.step = 1.0;
    voice.gain_from = volume;
    voice.gain_to = volume;
    voice.fade_pos = 0.0f;
    voice.fade_dur = 0.0f;
    voice.fade_ends_voice = false;
    voice.active = true;
    voice.loop = false;
    voice.music = false;
    // A reused slot must not inherit its predecessor's placement, or a plain
    // Play() would come out panned by whatever played there before.
    voice.positional = false;
    voice.pos_x = 0.0f;
    voice.pos_y = 0.0f;
    voice.atten = 1.0f;
    voice.pan = 0.0f;
}

void AudioEngine::PanGains(float pan, float &left, float &right) {
    // Linear crossfade rather than constant power: a centred voice must keep
    // full level on both sides (every existing mix is centred), so fading the
    // far channel out is the whole behaviour. Hard pan mutes one side.
    const float p = std::clamp(pan, -1.0f, 1.0f);
    left = 1.0f - (p > 0.0f ? p : 0.0f);
    right = 1.0f + (p < 0.0f ? p : 0.0f);
}

void AudioEngine::UpdateSpatial(Voice &voice) const {
    if (!voice.positional) {
        return;
    }
    const float dx = voice.pos_x - listener_x_;
    const float dy = voice.pos_y - listener_y_;
    if (listener_range_ <= 0.0f) {
        voice.atten = 0.0f;
        voice.pan = 0.0f;
        return;
    }
    const float dist = std::sqrt(dx * dx + dy * dy);
    voice.atten = std::clamp(1.0f - dist / listener_range_, 0.0f, 1.0f);
    // Half the rolloff range reaches full separation, so a hard-panned voice
    // still has half its level — being hard left must not mean being silent.
    voice.pan = std::clamp(dx / (listener_range_ * 0.5f), -1.0f, 1.0f);
}

void AudioEngine::PlayMusic(std::string_view name, float volume, float fade_seconds) {
    StartClip(name, volume, true, true, fade_seconds);
}

void AudioEngine::StopMusic(float fade_seconds) {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    for (Voice &voice : voices_) {
        if (voice.music && voice.active) {
            if (fade_seconds > 0.0f) {
                RampTo(voice, 0.0f, fade_seconds, true);
            } else {
                voice.active = false;
                voice.fade_dur = 0.0f;
                voice.fade_ends_voice = false;
            }
        }
    }
}

void AudioEngine::FadeMusicTo(float volume, float seconds) {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    for (Voice &voice : voices_) {
        if (voice.music && voice.active && !voice.fade_ends_voice) {
            RampTo(voice, volume, seconds, false);
        }
    }
}

void AudioEngine::SetMusicVolume(float volume) {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    for (Voice &voice : voices_) {
        if (voice.music && voice.active) {
            // Immediate: cancel any ramp and settle on the new gain.
            voice.gain_from = volume;
            voice.gain_to = volume;
            voice.fade_pos = 0.0f;
            voice.fade_dur = 0.0f;
        }
    }
}

void AudioEngine::SetBusVolume(AudioBus bus, float volume) {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    switch (bus) {
        case AudioBus::Master: bus_master_ = Clamp01(volume); break;
        case AudioBus::Sfx:    bus_sfx_ = Clamp01(volume); break;
        case AudioBus::Music:  bus_music_ = Clamp01(volume); break;
    }
}

float AudioEngine::BusVolume(AudioBus bus) const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    switch (bus) {
        case AudioBus::Master: return bus_master_;
        case AudioBus::Sfx:    return bus_sfx_;
        case AudioBus::Music:  return bus_music_;
    }
    return 0.0f;
}

void AudioEngine::SetPaused(bool paused) {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    paused_ = paused;
}

bool AudioEngine::IsMusicPlaying() const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    // A track fading out is still audible, so it still counts as playing —
    // MusicName() is the one that skips it (there the *new* track wins).
    for (const Voice &voice : voices_) {
        if (voice.music && voice.active) {
            return true;
        }
    }
    return false;
}

std::string_view AudioEngine::MusicName() const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    for (const Voice &voice : voices_) {
        if (voice.music && voice.active && !voice.fade_ends_voice &&
            voice.clip != nullptr) {
            return voice.clip->name;
        }
    }
    return {};
}

float AudioEngine::MusicVolume() const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    for (const Voice &voice : voices_) {
        if (voice.music && voice.active && !voice.fade_ends_voice) {
            return voice.gain_to;
        }
    }
    return 0.0f;
}

float AudioEngine::EnvelopeGain(const Voice &voice) {
    if (voice.fade_dur <= 0.0f) {
        return voice.gain_to; // settled
    }
    const float t = std::clamp(voice.fade_pos / voice.fade_dur, 0.0f, 1.0f);
    return voice.gain_from + (voice.gain_to - voice.gain_from) * t;
}

void AudioEngine::RampTo(Voice &voice, float to, float seconds, bool ends_voice) {
    const float from = EnvelopeGain(voice);
    if (seconds <= 0.0f) {
        voice.gain_from = to;
        voice.gain_to = to;
        voice.fade_dur = 0.0f;
        voice.fade_ends_voice = false;
        voice.active = voice.active && !(ends_voice && to <= 0.0f);
        return;
    }
    voice.gain_from = from;
    voice.gain_to = to;
    voice.fade_pos = 0.0f;
    voice.fade_dur = seconds;
    voice.fade_ends_voice = ends_voice;
}

bool AudioEngine::AddClipWav(std::string_view name, const std::string &wav_path) {
    std::ifstream in(wav_path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "fake2d: cannot open WAV %s\n", wav_path.c_str());
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        std::fprintf(stderr, "fake2d: not a WAV file: %s\n", wav_path.c_str());
        return false;
    }

    // Walk RIFF chunks for "fmt " and "data".
    std::uint16_t audio_format = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits = 0;
    const std::uint8_t *data = nullptr;
    std::uint32_t data_size = 0;
    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const char chunk_id[4] = {
            static_cast<char>(bytes[pos]), static_cast<char>(bytes[pos + 1]),
            static_cast<char>(bytes[pos + 2]), static_cast<char>(bytes[pos + 3])};
        std::uint32_t chunk_size = 0;
        std::memcpy(&chunk_size, &bytes[pos + 4], 4);
        const std::size_t body = pos + 8;
        if (body + chunk_size > bytes.size()) {
            break;
        }
        if (std::memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            std::memcpy(&audio_format, &bytes[body + 0], 2);
            std::memcpy(&channels, &bytes[body + 2], 2);
            std::memcpy(&sample_rate, &bytes[body + 4], 4);
            std::memcpy(&bits, &bytes[body + 14], 2);
        } else if (std::memcmp(chunk_id, "data", 4) == 0) {
            data = &bytes[body];
            data_size = chunk_size;
        }
        pos = body + chunk_size + (chunk_size & 1u); // chunks are word-aligned
    }

    if (data == nullptr || audio_format != 1 || channels == 0 ||
        (bits != 8 && bits != 16) || sample_rate == 0) {
        std::fprintf(stderr, "fake2d: unsupported WAV format in %s\n", wav_path.c_str());
        return false;
    }

    const std::size_t frame_count =
        data_size / (static_cast<std::size_t>(bits / 8) * channels);

    auto read_sample = [&](std::size_t frame, std::size_t ch) -> float {
        if (bits == 8) {
            // WAV 8-bit PCM is unsigned, centered at 128.
            return (static_cast<int>(data[frame * channels + ch]) - 128) / 128.0f;
        }
        std::int16_t s = 0;
        std::memcpy(&s, &data[(frame * channels + ch) * 2], 2);
        return static_cast<float>(s) / 32768.0f;
    };

    // Downmix to mono.
    std::vector<float> mono(frame_count);
    if (channels == 1) {
        for (std::size_t i = 0; i < frame_count; ++i) {
            mono[i] = read_sample(i, 0);
        }
    } else {
        for (std::size_t i = 0; i < frame_count; ++i) {
            float sum = 0.0f;
            for (std::uint16_t c = 0; c < channels; ++c) {
                sum += read_sample(i, c);
            }
            mono[i] = std::clamp(sum / channels, -1.0f, 1.0f);
        }
    }

    // Linear resample to the device rate.
    std::vector<float> resampled;
    if (sample_rate == static_cast<std::uint32_t>(AudioEngine::kSampleRate)) {
        resampled = std::move(mono);
    } else {
        const double ratio = static_cast<double>(sample_rate) / AudioEngine::kSampleRate;
        const auto out_count = static_cast<std::size_t>(frame_count / ratio);
        resampled.resize(out_count);
        for (std::size_t i = 0; i < out_count; ++i) {
            const double src = i * ratio;
            const auto i0 = static_cast<std::size_t>(src);
            const auto i1 = std::min(i0 + 1, frame_count - 1);
            const float frac = static_cast<float>(src - static_cast<double>(i0));
            resampled[i] = mono[i0] + (mono[i1] - mono[i0]) * frac;
        }
    }
    return AddClip(name, resampled);
}

std::size_t AudioEngine::PlayingCount() const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    std::size_t count = 0;
    for (const Voice &voice : voices_) {
        count += voice.active ? 1 : 0;
    }
    return count;
}

float AudioEngine::Peak() const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    return peak_;
}

float AudioEngine::PeakLeft() const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    return peak_left_;
}

float AudioEngine::PeakRight() const {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    return peak_right_;
}

void AudioEngine::Mix(float *out, int frame_count) {
    std::lock_guard<std::mutex> lock(MixerMutex(mutex_));
    if (out == nullptr || frame_count <= 0) {
        return;
    }
    if (paused_) {
        // Frozen: silence out, and nothing advances, so resuming continues
        // from exactly this point.
        std::fill(out, out + static_cast<std::size_t>(frame_count) * 2, 0.0f);
        peak_ = 0.0f;
        peak_left_ = 0.0f;
        peak_right_ = 0.0f;
        return;
    }

    const float dt = 1.0f / static_cast<float>(AudioEngine::kSampleRate);
    float peak = 0.0f;
    float peak_left = 0.0f;
    float peak_right = 0.0f;

    for (int frame = 0; frame < frame_count; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        for (Voice &voice : voices_) {
            if (!voice.active) {
                continue;
            }
            Clip &clip = *voice.clip;
            const bool streaming = clip.stream;
            // A stream's length is the file's, not the window's: the cursor
            // runs over the whole track and the window follows it.
            const std::uint64_t length =
                streaming ? clip.length_frames
                          : static_cast<std::uint64_t>(clip.pcm.size());
            if (length == 0) {
                voice.active = false;
                continue;
            }
            auto index = static_cast<std::uint64_t>(voice.cursor);
            if (index >= length) {
                if (voice.loop) {
                    voice.cursor = std::fmod(voice.cursor, static_cast<double>(length));
                    index = static_cast<std::uint64_t>(voice.cursor);
                } else {
                    voice.active = false;
                    continue;
                }
            }
            float dry = 0.0f;
            if (streaming) {
                // Only refill when the cursor leaves the window: a normal mix
                // block advances a few hundred frames inside one window.
                if (index < clip.window_start ||
                    index >= clip.window_start + clip.pcm.size()) {
                    if (!FillWindow(clip, index)) {
                        voice.active = false;
                        continue;
                    }
                    index = std::min(index, clip.window_start + clip.pcm.size() - 1);
                }
                dry = clip.pcm[static_cast<std::size_t>(index - clip.window_start)];
            } else {
                dry = clip.pcm[static_cast<std::size_t>(index)];
            }
            // Envelope -> bus -> master. A music voice that has finished its
            // fade-out leaves the mix here rather than looping forever.
            const float envelope = EnvelopeGain(voice);
            const float bus = voice.music ? bus_music_ : bus_sfx_;
            const float gain = envelope * voice.atten * bus * bus_master_;
            float pan_l = 1.0f;
            float pan_r = 1.0f;
            PanGains(voice.pan, pan_l, pan_r);
            const float sample = dry * gain;
            left += sample * pan_l;
            right += sample * pan_r;
            voice.cursor += voice.step;
            if (voice.fade_dur > 0.0f) {
                voice.fade_pos += dt;
                if (voice.fade_pos >= voice.fade_dur) {
                    voice.fade_pos = voice.fade_dur;
                    if (voice.fade_ends_voice) {
                        voice.active = false;
                        voice.fade_dur = 0.0f;
                    } else {
                        // Settle on the target so the ramp stops applying.
                        voice.gain_from = voice.gain_to;
                        voice.fade_dur = 0.0f;
                    }
                }
            }
        }
        const float l = std::clamp(left, -1.0f, 1.0f);
        const float r = std::clamp(right, -1.0f, 1.0f);
        out[static_cast<std::size_t>(frame) * 2 + 0] = l;
        out[static_cast<std::size_t>(frame) * 2 + 1] = r;
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        peak_left = std::max(peak_left, std::fabs(l));
        peak_right = std::max(peak_right, std::fabs(r));
    }
    peak_ = peak;
    peak_left_ = peak_left;
    peak_right_ = peak_right;
}

} // namespace fake2d
