#include "fake2d/entity_store.h"

namespace fake2d {

namespace {
constexpr std::uint64_t kSlotMask = 0xFFFFFFFFu;

std::uint64_t Encode(std::uint32_t generation, std::uint32_t slot) {
    return (static_cast<std::uint64_t>(generation) << 32) | slot;
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

} // namespace fake2d
