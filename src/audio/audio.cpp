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

constexpr int kVoiceCount = 32;
constexpr int kSampleRate = 44100;

struct AudioEngine::Device {
    ma_device ma{};
    std::mutex mutex;

    static void DataCallback(ma_device *device, void *output, const void * /*input*/,
                             ma_uint32 frame_count) {
        auto *engine = static_cast<AudioEngine *>(device->pUserData);
        if (engine != nullptr) {
            engine->Mix(static_cast<float *>(output), static_cast<int>(frame_count));
        }
    }
};

AudioEngine::AudioEngine() {
    voices_.resize(kVoiceCount);
}

AudioEngine::~AudioEngine() {
    Shutdown();
}

bool AudioEngine::Init() {
    Shutdown();

    auto *device = new Device();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kSampleRate;
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
    if (!enabled_ || mono_pcm.empty()) {
        return false;
    }
    const std::string key(name);
    const auto existing = std::find_if(clips_.begin(), clips_.end(),
                                       [&](const Clip &c) { return c.name == key; });
    if (existing != clips_.end()) {
        return false;
    }
    clips_.push_back(Clip{key, mono_pcm});
    return true;
}

void AudioEngine::StartClip(std::string_view name, float volume, bool loop, bool music) {
    if (!enabled_ || voices_.empty()) {
        return;
    }
    const std::string key(name);
    const auto clip = std::find_if(clips_.begin(), clips_.end(),
                                   [&](const Clip &c) { return c.name == key; });
    if (clip == clips_.end() || clip->pcm.empty()) {
        return;
    }

    auto *device = static_cast<Device *>(device_);
    std::lock_guard<std::mutex> lock(device->mutex);

    // Music occupies exactly one voice; restart it in place.
    if (music) {
        for (Voice &voice : voices_) {
            if (voice.music) {
                voice.clip = &*clip;
                voice.cursor = 0.0;
                voice.step = 1.0;
                voice.volume = volume;
                voice.active = true;
                voice.loop = true;
                return;
            }
        }
    }

    for (Voice &voice : voices_) {
        if (!voice.active) {
            voice.clip = &*clip;
            voice.cursor = 0.0;
            voice.step = 1.0;
            voice.volume = volume;
            voice.active = true;
            voice.loop = loop;
            voice.music = music;
            return;
        }
    }
    // Voice pool exhausted: drop the newest request (oldest sounds finish).
}

void AudioEngine::Play(std::string_view name, float volume) {
    StartClip(name, volume, false, false);
}

void AudioEngine::PlayMusic(std::string_view name, float volume) {
    StartClip(name, volume, true, true);
}

void AudioEngine::StopMusic() {
    if (!enabled_) {
        return;
    }
    auto *device = static_cast<Device *>(device_);
    std::lock_guard<std::mutex> lock(device->mutex);
    for (Voice &voice : voices_) {
        if (voice.music) {
            voice.active = false;
        }
    }
}

void AudioEngine::SetMusicVolume(float volume) {
    if (!enabled_) {
        return;
    }
    auto *device = static_cast<Device *>(device_);
    std::lock_guard<std::mutex> lock(device->mutex);
    for (Voice &voice : voices_) {
        if (voice.music) {
            voice.volume = volume;
        }
    }
}

bool AudioEngine::AddClipWav(std::string_view name, const std::string &wav_path) {
    if (!enabled_) {
        return false;
    }
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
    if (sample_rate == static_cast<std::uint32_t>(kSampleRate)) {
        resampled = std::move(mono);
    } else {
        const double ratio = static_cast<double>(sample_rate) / kSampleRate;
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
    if (!enabled_) {
        return 0;
    }
    auto *device = static_cast<Device *>(device_);
    std::lock_guard<std::mutex> lock(device->mutex);
    std::size_t count = 0;
    for (const Voice &voice : voices_) {
        count += voice.active ? 1 : 0;
    }
    return count;
}

void AudioEngine::Mix(float *out, int frame_count) {
    if (device_ == nullptr) {
        std::fill(out, out + static_cast<std::size_t>(frame_count) * 2, 0.0f);
        return;
    }
    auto *device = static_cast<Device *>(device_);
    std::lock_guard<std::mutex> lock(device->mutex);

    for (int frame = 0; frame < frame_count; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        for (Voice &voice : voices_) {
            if (!voice.active) {
                continue;
            }
            const auto &pcm = voice.clip->pcm;
            if (pcm.empty()) {
                voice.active = false;
                continue;
            }
            auto index = static_cast<std::size_t>(voice.cursor);
            if (index >= pcm.size()) {
                if (voice.loop) {
                    voice.cursor = std::fmod(voice.cursor, static_cast<double>(pcm.size()));
                    index = static_cast<std::size_t>(voice.cursor);
                } else {
                    voice.active = false;
                    continue;
                }
            }
            const float sample = pcm[index] * voice.volume;
            left += sample;
            right += sample;
            voice.cursor += voice.step;
        }
        out[static_cast<std::size_t>(frame) * 2 + 0] = std::clamp(left, -1.0f, 1.0f);
        out[static_cast<std::size_t>(frame) * 2 + 1] = std::clamp(right, -1.0f, 1.0f);
    }
}

} // namespace fake2d
