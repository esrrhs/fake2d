#pragma once

#include "fake2d/input.h"
#include "fake2d/math.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fake2d {

/// Format version written by the current binary. Loaders reject any other
/// value with a clear error — never reinterpret an unknown layout.
inline constexpr std::uint32_t kInputRecordingVersion = 1;

/// Fixed-layout per-frame sample. POD so the recorder can append without
/// heap traffic once the sample vector is reserved, and so Save/Load is a
/// straight write of the byte span.
struct InputFrameSnapshot {
    static constexpr std::size_t kKeyCount = Input::kSnapshotKeyCount;
    static constexpr std::size_t kKeyBytes = (kKeyCount + 7) / 8;
    static constexpr std::size_t kButtonCount = Input::kSnapshotButtonCount;
    static constexpr int kPadCount = Input::kSnapshotPadCount;
    static constexpr int kPadButtonCount = Input::kSnapshotPadButtonCount;
    static constexpr int kPadAxisCount = Input::kSnapshotPadAxisCount;
    static constexpr int kPadButtonBytes = (kPadButtonCount + 7) / 8;
    /// Cap so a sample stays fixed-size; extras are dropped at capture time.
    static constexpr std::size_t kMaxText = 8;

    std::uint64_t step_index = 0;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    float mouse_dx = 0.0f;
    float mouse_dy = 0.0f;
    double wheel = 0.0;
    std::uint8_t keys[kKeyBytes] = {};
    std::uint8_t buttons = 0;
    std::uint8_t pad_connected[kPadCount] = {};
    std::uint8_t pad_buttons[kPadCount][kPadButtonBytes] = {};
    float pad_axes[kPadCount][kPadAxisCount] = {};
    std::uint16_t text_count = 0;
    std::uint16_t reserved = 0;
    std::uint32_t text[kMaxText] = {};

    void Clear();
    void SetKey(std::size_t code, bool down);
    [[nodiscard]] bool Key(std::size_t code) const;
    void SetButton(std::size_t button, bool down);
    [[nodiscard]] bool Button(std::size_t button) const;
    void SetPadButton(int pad, int button, bool down);
    [[nodiscard]] bool PadButton(int pad, int button) const;
};

/// Append-only capture buffer. Call `Begin` (optionally with a capacity hint)
/// before the run; `Capture` appends one POD sample with no allocation when
/// capacity remains. `Save` writes the versioned `F2IR` container.
class InputRecorder {
public:
    void Begin(std::size_t capacity_hint = 4096, float fixed_dt = 1.0f / 60.0f);
    void Clear();

    /// Snapshot `input` at `step_index` (typically `Engine::FrameIndex()`).
    void Capture(std::uint64_t step_index, const Input &input);

    /// Push a caller-built sample (self-tests / offline tools).
    void Push(const InputFrameSnapshot &sample);

    [[nodiscard]] bool Save(std::string_view path) const;
    [[nodiscard]] std::size_t Size() const { return samples_.size(); }
    [[nodiscard]] float FixedDt() const { return fixed_dt_; }
    [[nodiscard]] const std::vector<InputFrameSnapshot> &Samples() const {
        return samples_;
    }
    /// FNV-1a over the sample bytes — identity check for `--replay-test`.
    [[nodiscard]] std::uint64_t ContentHash() const;

private:
    std::vector<InputFrameSnapshot> samples_;
    float fixed_dt_ = 1.0f / 60.0f;
};

/// Loads a recording and feeds `Input` from it. Past the last sample the last
/// one is held so a short fixture can drive a longer attract loop.
class InputReplay {
public:
    void Clear();
    /// Load a `F2IR` file. Returns false on I/O error, bad magic, or an
    /// unknown format version (message printed to stderr).
    [[nodiscard]] bool Load(std::string_view path);

    /// Apply the sample for `step_index` into `input` (edge bookkeeping
    /// included). Returns false only when the recording is empty.
    [[nodiscard]] bool Apply(std::uint64_t step_index, Input &input);

    [[nodiscard]] bool Empty() const { return samples_.empty(); }
    [[nodiscard]] std::size_t Size() const { return samples_.size(); }
    [[nodiscard]] float FixedDt() const { return fixed_dt_; }
    [[nodiscard]] std::uint32_t Version() const { return version_; }
    [[nodiscard]] const std::vector<InputFrameSnapshot> &Samples() const {
        return samples_;
    }
    [[nodiscard]] std::uint64_t ContentHash() const;

private:
    std::vector<InputFrameSnapshot> samples_;
    float fixed_dt_ = 1.0f / 60.0f;
    std::uint32_t version_ = 0;
    /// Last index successfully applied; used to hold the tail sample.
    std::size_t cursor_ = 0;
};

} // namespace fake2d
