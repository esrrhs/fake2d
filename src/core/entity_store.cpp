#include <cstdint>
#include "fake2d/entity_store.h"

#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <vector>

namespace fake2d {

namespace {
constexpr std::uint64_t kSlotMask = 0xFFFFFFFFu;
constexpr std::string_view kSnapshotHeader = "F2ES1";

std::uint64_t Encode(std::uint32_t generation, std::uint32_t slot) {
    return (static_cast<std::uint64_t>(generation) << 32) | slot;
}

std::string Escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += ch; break;
        }
    }
    return out;
}

std::string Unescape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out += text[i];
            continue;
        }
        switch (text[++i]) {
            case 't': out += '\t'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case '\\': out += '\\'; break;
            default:
                out += '\\';
                out += text[i];
                break;
        }
    }
    return out;
}

std::vector<std::string_view> SplitTabs(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

} // namespace

EntityStore::Entity *EntityStore::Resolve(Id id) {
    if (id == kInvalid) {
        return nullptr;
    }
    const auto slot = static_cast<std::size_t>(id & kSlotMask);
    const auto generation = static_cast<std::uint32_t>(id >> 32);
    if (slot >= kMaxEntities) {
        return nullptr;
    }
    Entity &e = entities_[slot];
    return (e.active && e.generation == generation) ? &e : nullptr;
}

const EntityStore::Entity *EntityStore::Resolve(Id id) const {
    if (id == kInvalid) {
        return nullptr;
    }
    const auto slot = static_cast<std::size_t>(id & kSlotMask);
    const auto generation = static_cast<std::uint32_t>(id >> 32);
    if (slot >= kMaxEntities) {
        return nullptr;
    }
    const Entity &e = entities_[slot];
    return (e.active && e.generation == generation) ? &e : nullptr;
}

EntityStore::Id EntityStore::Create(std::int64_t tag) {
    for (std::size_t i = 0; i < kMaxEntities; ++i) {
        Entity &e = entities_[i];
        if (e.active) {
            continue;
        }
        e.active = true;
        e.tag = tag;
        e.numbers.fill(0.0);
        for (auto &s : e.strings) {
            s.clear();
        }
        ++live_;
        return Encode(e.generation, static_cast<std::uint32_t>(i));
    }
    return kInvalid;
}

void EntityStore::Destroy(Id id) {
    Entity *e = Resolve(id);
    if (e == nullptr) {
        return;
    }
    e->active = false;
    ++e->generation;
    for (auto &s : e->strings) {
        s.clear();
    }
    --live_;
}

void EntityStore::Clear() {
    for (Entity &e : entities_) {
        if (!e.active) {
            continue;
        }
        e.active = false;
        ++e.generation;
        for (auto &s : e.strings) {
            s.clear();
        }
    }
    live_ = 0;
}

bool EntityStore::Active(Id id) const { return Resolve(id) != nullptr; }

std::int64_t EntityStore::Tag(Id id) const {
    const Entity *e = Resolve(id);
    return e ? e->tag : 0;
}

void EntityStore::SetTag(Id id, std::int64_t tag) {
    Entity *e = Resolve(id);
    if (e != nullptr) {
        e->tag = tag;
    }
}

double EntityStore::GetNumber(Id id, std::size_t slot) const {
    const Entity *e = Resolve(id);
    return (e != nullptr && slot < kNumSlots) ? e->numbers[slot] : 0.0;
}

void EntityStore::SetNumber(Id id, std::size_t slot, double value) {
    Entity *e = Resolve(id);
    if (e != nullptr && slot < kNumSlots) {
        e->numbers[slot] = value;
    }
}

std::string EntityStore::GetString(Id id, std::size_t slot) const {
    const Entity *e = Resolve(id);
    return (e != nullptr && slot < kNumStrings) ? e->strings[slot] : std::string();
}

void EntityStore::SetString(Id id, std::size_t slot, std::string_view value) {
    Entity *e = Resolve(id);
    if (e != nullptr && slot < kNumStrings) {
        e->strings[slot] = std::string(value);
    }
}

std::size_t EntityStore::CountWithTag(std::int64_t tag) const {
    std::size_t n = 0;
    for (const Entity &e : entities_) {
        if (e.active && e.tag == tag) {
            ++n;
        }
    }
    return n;
}

EntityStore::Id EntityStore::At(std::size_t index) const {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < kMaxEntities; ++i) {
        if (!entities_[i].active) {
            continue;
        }
        if (seen == index) {
            return Encode(entities_[i].generation, static_cast<std::uint32_t>(i));
        }
        ++seen;
    }
    return kInvalid;
}

EntityStore::Id EntityStore::AtWithTag(std::size_t index, std::int64_t tag) const {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < kMaxEntities; ++i) {
        if (!entities_[i].active || entities_[i].tag != tag) {
            continue;
        }
        if (seen == index) {
            return Encode(entities_[i].generation, static_cast<std::uint32_t>(i));
        }
        ++seen;
    }
    return kInvalid;
}

std::string EntityStore::Serialize() const {
    std::ostringstream out;
    out << kSnapshotHeader << '\n';
    for (std::size_t i = 0; i < kMaxEntities; ++i) {
        const Entity &e = entities_[i];
        if (!e.active) {
            continue;
        }
        out << i << '\t' << e.tag;
        for (const double value : e.numbers) {
            out << '\t' << std::setprecision(17) << value;
        }
        for (const std::string &text : e.strings) {
            out << '\t' << Escape(text);
        }
        out << '\n';
    }
    return out.str();
}

bool EntityStore::Deserialize(std::string_view data) {
    Clear();
    // Reject up front so a malformed blob does not leave a half-restored store.
    if (data.substr(0, data.find('\n')) != kSnapshotHeader) {
        return false;
    }
    const std::size_t header_end = data.find('\n');
    if (header_end == std::string_view::npos) {
        return false;
    }

    std::array<Entity, kMaxEntities> restored{};
    std::size_t live = 0;
    std::size_t line_start = header_end + 1;
    while (line_start < data.size()) {
        const std::size_t line_end = data.find('\n', line_start);
        const std::string_view line =
            data.substr(line_start, line_end == std::string_view::npos
                                        ? std::string_view::npos
                                        : line_end - line_start);
        line_start = line_end == std::string_view::npos ? data.size() : line_end + 1;
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string_view> fields = SplitTabs(line);
        if (fields.size() != 2 + kNumSlots + kNumStrings) {
            Clear();
            return false;
        }
        const long slot = std::strtol(std::string(fields[0]).c_str(), nullptr, 10);
        if (slot < 0 || static_cast<std::size_t>(slot) >= kMaxEntities) {
            Clear();
            return false;
        }
        Entity &e = restored[static_cast<std::size_t>(slot)];
        if (e.active) {
            Clear();
            return false;
        }
        e.active = true;
        e.tag = std::strtoll(std::string(fields[1]).c_str(), nullptr, 10);
        for (std::size_t n = 0; n < kNumSlots; ++n) {
            e.numbers[n] = std::strtod(std::string(fields[2 + n]).c_str(), nullptr);
        }
        for (std::size_t s = 0; s < kNumStrings; ++s) {
            e.strings[s] = Unescape(fields[2 + kNumSlots + s]);
        }
        ++live;
    }

    // Keep each slot's generation from the live store so stale handles from
    // this session still fail to resolve.
    for (std::size_t i = 0; i < kMaxEntities; ++i) {
        if (restored[i].active) {
            restored[i].generation = entities_[i].generation;
        }
    }
    entities_ = std::move(restored);
    live_ = live;
    return true;
}

} // namespace fake2d
