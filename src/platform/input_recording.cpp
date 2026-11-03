#include "fake2d/input_recording.h"

#include <cstdio>
#include <type_traits>
#include <cstring>
#include <fstream>
#include <string>

namespace fake2d {
namespace {

constexpr char kMagic[4] = {'F', '2', 'I', 'R'};

std::uint64_t HashBytes(const void *data, std::size_t size, std::uint64_t seed = 14695981039346656037ull) {
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    std::uint64_t hash = seed;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t HashSamples(const std::vector<InputFrameSnapshot> &samples, float fixed_dt) {
    std::uint64_t hash = HashBytes(&fixed_dt, sizeof(fixed_dt));
    if (!samples.empty()) {
        hash = HashBytes(samples.data(), samples.size() * sizeof(InputFrameSnapshot), hash);
    }
    return hash;
}

#pragma pack(push, 1)
struct FileHeaderV1 {
    char magic[4];
    std::uint32_t version;
    std::uint32_t flags;
    float fixed_dt;
    std::uint32_t reserved;
    std::uint64_t sample_count;
};
#pragma pack(pop)

static_assert(sizeof(FileHeaderV1) == 4 + 4 + 4 + 4 + 4 + 8, "F2IR header layout");
static_assert(std::is_trivially_copyable_v<InputFrameSnapshot>,
              "InputFrameSnapshot must stay POD for mmap-style I/O");

} // namespace

void InputFrameSnapshot::Clear() {
    *this = InputFrameSnapshot{};
}

void InputFrameSnapshot::SetKey(std::size_t code, bool down) {
    if (code >= kKeyCount) {
        return;
    }
    const std::size_t byte = code / 8;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << (code % 8));
    if (down) {
        keys[byte] = static_cast<std::uint8_t>(keys[byte] | bit);
    } else {
        keys[byte] = static_cast<std::uint8_t>(keys[byte] & static_cast<std::uint8_t>(~bit));
    }
}

bool InputFrameSnapshot::Key(std::size_t code) const {
    if (code >= kKeyCount) {
        return false;
    }
    return (keys[code / 8] & static_cast<std::uint8_t>(1u << (code % 8))) != 0;
}

void InputFrameSnapshot::SetButton(std::size_t button, bool down) {
    if (button >= kButtonCount) {
        return;
    }
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << button);
    if (down) {
        buttons = static_cast<std::uint8_t>(buttons | bit);
    } else {
        buttons = static_cast<std::uint8_t>(buttons & static_cast<std::uint8_t>(~bit));
    }
}

bool InputFrameSnapshot::Button(std::size_t button) const {
    if (button >= kButtonCount) {
        return false;
    }
    return (buttons & static_cast<std::uint8_t>(1u << button)) != 0;
}

void InputFrameSnapshot::SetPadButton(int pad, int button, bool down) {
    if (pad < 0 || pad >= kPadCount || button < 0 || button >= kPadButtonCount) {
        return;
    }
    const int byte = button / 8;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << (button % 8));
    if (down) {
        pad_buttons[pad][byte] =
            static_cast<std::uint8_t>(pad_buttons[pad][byte] | bit);
    } else {
        pad_buttons[pad][byte] = static_cast<std::uint8_t>(
            pad_buttons[pad][byte] & static_cast<std::uint8_t>(~bit));
    }
}

bool InputFrameSnapshot::PadButton(int pad, int button) const {
    if (pad < 0 || pad >= kPadCount || button < 0 || button >= kPadButtonCount) {
        return false;
    }
    return (pad_buttons[pad][button / 8] &
            static_cast<std::uint8_t>(1u << (button % 8))) != 0;
}

void InputRecorder::Begin(std::size_t capacity_hint, float fixed_dt) {
    samples_.clear();
    samples_.reserve(capacity_hint);
    fixed_dt_ = fixed_dt;
}

void InputRecorder::Clear() {
    samples_.clear();
    fixed_dt_ = 1.0f / 60.0f;
}

void InputRecorder::Capture(std::uint64_t step_index, const Input &input) {
    InputFrameSnapshot sample;
    input.CaptureSnapshot(sample, step_index);
    samples_.push_back(sample);
}

void InputRecorder::Push(const InputFrameSnapshot &sample) {
    samples_.push_back(sample);
}

bool InputRecorder::Save(std::string_view path) const {
    const std::string path_str(path);
    std::ofstream out(path_str, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "fake2d: input record: cannot write '%s'\n", path_str.c_str());
        return false;
    }
    FileHeaderV1 header{};
    std::memcpy(header.magic, kMagic, 4);
    header.version = kInputRecordingVersion;
    header.flags = 0;
    header.fixed_dt = fixed_dt_;
    header.reserved = 0;
    header.sample_count = static_cast<std::uint64_t>(samples_.size());
    out.write(reinterpret_cast<const char *>(&header), sizeof(header));
    if (!samples_.empty()) {
        out.write(reinterpret_cast<const char *>(samples_.data()),
                  static_cast<std::streamsize>(samples_.size() * sizeof(InputFrameSnapshot)));
    }
    if (!out) {
        std::fprintf(stderr, "fake2d: input record: write failed for '%s'\n", path_str.c_str());
        return false;
    }
    return true;
}

std::uint64_t InputRecorder::ContentHash() const {
    return HashSamples(samples_, fixed_dt_);
}

void InputReplay::Clear() {
    samples_.clear();
    fixed_dt_ = 1.0f / 60.0f;
    version_ = 0;
    cursor_ = 0;
}

bool InputReplay::Load(std::string_view path) {
    Clear();
    const std::string path_str(path);
    std::ifstream in(path_str, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "fake2d: input replay: cannot open '%s'\n", path_str.c_str());
        return false;
    }
    FileHeaderV1 header{};
    in.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!in || in.gcount() != static_cast<std::streamsize>(sizeof(header))) {
        std::fprintf(stderr, "fake2d: input replay: truncated header in '%s'\n", path_str.c_str());
        return false;
    }
    if (std::memcmp(header.magic, kMagic, 4) != 0) {
        std::fprintf(stderr, "fake2d: input replay: bad magic in '%s' (expected F2IR)\n",
                     path_str.c_str());
        return false;
    }
    if (header.version != kInputRecordingVersion) {
        std::fprintf(stderr,
                     "fake2d: input replay: unsupported format version %u in '%s' "
                     "(this build understands %u)\n",
                     header.version, path_str.c_str(), kInputRecordingVersion);
        return false;
    }
    version_ = header.version;
    fixed_dt_ = header.fixed_dt;
    samples_.resize(static_cast<std::size_t>(header.sample_count));
    if (header.sample_count > 0) {
        in.read(reinterpret_cast<char *>(samples_.data()),
                static_cast<std::streamsize>(header.sample_count * sizeof(InputFrameSnapshot)));
        if (!in) {
            std::fprintf(stderr, "fake2d: input replay: truncated samples in '%s'\n",
                         path_str.c_str());
            Clear();
            return false;
        }
    }
    cursor_ = 0;
    return true;
}

bool InputReplay::Apply(std::uint64_t step_index, Input &input) {
    if (samples_.empty()) {
        return false;
    }
    // Prefer an exact step match; otherwise the highest sample with
    // step_index <= requested; if the recording starts later, use the first.
    std::size_t index = 0;
    if (step_index >= samples_.back().step_index) {
        index = samples_.size() - 1;
    } else {
        while (index + 1 < samples_.size() &&
               samples_[index + 1].step_index <= step_index) {
            ++index;
        }
        if (samples_[index].step_index > step_index) {
            index = 0;
        }
    }
    cursor_ = index;
    input.ApplySnapshot(samples_[index]);
    return true;
}

std::uint64_t InputReplay::ContentHash() const {
    return HashSamples(samples_, fixed_dt_);
}

} // namespace fake2d
