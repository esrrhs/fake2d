#include "fake2d/object_snapshot.h"

#include "fake2d/log.h"
#include "fake2d/save_store.h"
#include "fakelua.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace fake2d {

namespace {

using NObj = fakelua::NativeObject;

constexpr std::uint32_t kDeltaMagic = 0x4D44444Cu; // "MDDL"
constexpr std::uint32_t kDeltaFmt = 1;
constexpr std::int64_t kTempIdShift = std::int64_t(1) << 40;
constexpr unsigned kDeltaMaxPerMonth = 31;

struct VersionMark {
    std::uint64_t version = 0;
    std::uint64_t gen = 0;
    bool has_table = false;
    std::vector<NObj *> children;
};

struct ObjSnapEntry {
    std::uint64_t own_version = 0;
    std::uint64_t check_gen = 0;
    bool check_ok = false;
    std::string bytes;
    std::vector<NObj *> children;
};

struct SnapOut {
    std::string b;
    std::vector<std::vector<std::pair<std::string_view, NObj::FieldKind>>> scratch;
    std::vector<std::pair<std::string_view, NObj::FieldKind>> &scratch_at(std::size_t depth) {
        if (scratch.size() <= depth) scratch.resize(depth + 1);
        auto &v = scratch[depth];
        v.clear();
        return v;
    }
    void reserve(std::size_t n) { b.reserve(n); }
    void u8(unsigned v) { b.push_back(static_cast<char>(v)); }
    void u32(std::uint32_t v) { b.append(reinterpret_cast<const char *>(&v), 4); }
    void i64(std::int64_t v) { b.append(reinterpret_cast<const char *>(&v), 8); }
    void f64(double v) { b.append(reinterpret_cast<const char *>(&v), 8); }
    void str(std::string_view v) {
        u32(static_cast<std::uint32_t>(v.size()));
        b.append(v.data(), v.size());
    }
};

struct SnapIn {
    const std::string &b;
    std::size_t p = 0;
    bool ok = true;
    bool has(std::size_t n) const { return p + n <= b.size(); }
    unsigned u8() {
        if (!has(1)) { ok = false; return 0; }
        return static_cast<unsigned char>(b[p++]);
    }
    std::uint32_t u32() {
        if (!has(4)) { ok = false; return 0; }
        std::uint32_t v;
        std::memcpy(&v, b.data() + p, 4);
        p += 4;
        return v;
    }
    std::int64_t i64() {
        if (!has(8)) { ok = false; return 0; }
        std::int64_t v;
        std::memcpy(&v, b.data() + p, 8);
        p += 8;
        return v;
    }
    double f64() {
        if (!has(8)) { ok = false; return 0; }
        double v;
        std::memcpy(&v, b.data() + p, 8);
        p += 8;
        return v;
    }
    std::string str() {
        const std::uint32_t n = u32();
        if (!ok || !has(n)) { ok = false; return {}; }
        std::string v(b.data() + p, n);
        p += n;
        return v;
    }
};

bool is_transient_key(const ObjectSnapshotChain::Schema &schema, std::string_view type_name,
                      std::string_view key, int mode) {
    return schema.is_transient ? schema.is_transient(type_name, key, mode) : false;
}

void write_obj(NObj *o, SnapOut &out, const ObjectSnapshotChain::Schema &schema, bool full,
               std::size_t depth, std::int64_t id_shift);

bool cache_entry_valid(NObj *o, std::unordered_map<NObj *, ObjSnapEntry> &cache,
                       std::uint64_t check_gen, std::size_t depth = 0) {
    if (!o) return true;
    if (depth > 64) return false;
    auto it = cache.find(o);
    if (it == cache.end()) return false;
    ObjSnapEntry &e = it->second;
    if (e.check_gen == check_gen) return e.check_ok;
    bool ok = (o->GetVersion() == e.own_version);
    if (ok) {
        for (NObj *c : e.children) {
            if (!cache_entry_valid(c, cache, check_gen, depth + 1)) {
                ok = false;
                break;
            }
        }
    }
    e.check_gen = check_gen;
    e.check_ok = ok;
    return ok;
}

void write_obj_cached(NObj *o, SnapOut &out, const ObjectSnapshotChain::Schema &schema,
                      std::unordered_map<NObj *, ObjSnapEntry> &cache,
                      std::uint64_t check_gen, std::size_t depth);

void write_fields_impl(NObj *o, SnapOut &out, const ObjectSnapshotChain::Schema &schema, bool full,
                       std::size_t depth, std::int64_t id_shift,
                       std::unordered_map<NObj *, ObjSnapEntry> *cache, std::uint64_t check_gen,
                       std::vector<NObj *> *out_children) {
    auto &fs = out.scratch_at(depth);
    const std::string_view tname = o->GetTypeName();
    o->ForEach([&](std::string_view key, NObj::FieldKind k) {
        if (k == NObj::FieldKind::Nil || k == NObj::FieldKind::Table) return;
        if (!full && is_transient_key(schema, tname, key, 0)) return;
        fs.emplace_back(key, k);
    });
    const auto by_key = [](const auto &a, const auto &b) { return a.first < b.first; };
    if (!std::is_sorted(fs.begin(), fs.end(), by_key)) std::sort(fs.begin(), fs.end(), by_key);
    out.u32(static_cast<std::uint32_t>(fs.size()));
    for (const auto &[key, k] : fs) {
        switch (k) {
            case NObj::FieldKind::Int: out.u8(1); break;
            case NObj::FieldKind::Float: out.u8(2); break;
            case NObj::FieldKind::Bool: out.u8(3); break;
            case NObj::FieldKind::String: out.u8(4); break;
            case NObj::FieldKind::Object: out.u8(5); break;
            default: continue;
        }
        out.str(key);
        switch (k) {
            case NObj::FieldKind::Int: out.i64(o->GetInt(key)); break;
            case NObj::FieldKind::Float: out.f64(o->GetFloat(key)); break;
            case NObj::FieldKind::Bool: out.u8(o->GetBool(key) ? 1 : 0); break;
            case NObj::FieldKind::String: out.str(o->GetString(key)); break;
            case NObj::FieldKind::Object: {
                NObj *child = o->GetObject(key);
                if (out_children && child) out_children->push_back(child);
                if (cache) write_obj_cached(child, out, schema, *cache, check_gen, depth + 1);
                else write_obj(child, out, schema, full, depth + 1, id_shift);
                break;
            }
            default: break;
        }
    }
}

void write_obj(NObj *o, SnapOut &out, const ObjectSnapshotChain::Schema &schema, bool full,
               std::size_t depth, std::int64_t id_shift) {
    if (!o) {
        out.str({});
        out.i64(0);
        out.u32(0);
        return;
    }
    out.str(o->GetTypeName());
    out.i64(o->GetId() - id_shift);
    write_fields_impl(o, out, schema, full, depth, id_shift, nullptr, 0, nullptr);
}

void write_obj_cached(NObj *o, SnapOut &out, const ObjectSnapshotChain::Schema &schema,
                      std::unordered_map<NObj *, ObjSnapEntry> &cache,
                      std::uint64_t check_gen, std::size_t depth) {
    if (!o) {
        out.str({});
        out.i64(0);
        out.u32(0);
        return;
    }
    if (depth <= 64 && cache_entry_valid(o, cache, check_gen, depth)) {
        const auto &cached = cache.find(o)->second.bytes;
        out.b.append(cached.data(), cached.size());
        return;
    }
    const std::size_t start_pos = out.b.size();
    out.str(o->GetTypeName());
    out.i64(o->GetId());
    std::vector<NObj *> kids;
    write_fields_impl(o, out, schema, false, depth, 0, &cache, check_gen, &kids);
    if (depth <= 64) {
        ObjSnapEntry &e = cache[o];
        e.own_version = o->GetVersion();
        e.check_gen = check_gen;
        e.check_ok = true;
        e.children = std::move(kids);
        e.bytes.assign(out.b.data() + start_pos, out.b.size() - start_pos);
    }
}

void clear_recursive(NObj *o) {
    if (!o) return;
    std::vector<NObj *> kids;
    o->ForEach([&](std::string_view key, NObj::FieldKind k) {
        if (k == NObj::FieldKind::Object)
            if (NObj *c = o->GetObject(key)) kids.push_back(c);
    });
    o->Clear();
    for (NObj *c : kids) clear_recursive(c);
}

NObj *read_obj(SnapIn &in, fakelua::NativeObjectManager &mgr, long long group,
               std::int64_t id_shift, bool clear_first);

void read_fields(NObj *o, SnapIn &in, fakelua::NativeObjectManager &mgr, long long group,
                 std::int64_t id_shift, bool clear_first) {
    const std::uint32_t n = in.u32();
    if (!in.ok) return;
    for (std::uint32_t i = 0; i < n && in.ok; ++i) {
        const unsigned kind = in.u8();
        const std::string key = in.str();
        if (!in.ok) return;
        switch (kind) {
            case 1: o->SetInt(key, in.i64()); break;
            case 2: o->SetFloat(key, in.f64()); break;
            case 3: o->SetBool(key, in.u8() != 0); break;
            case 4: o->SetString(key, in.str()); break;
            case 5: {
                NObj *c = read_obj(in, mgr, group, id_shift, clear_first);
                if (c) o->SetObject(key, c);
                break;
            }
            default: in.ok = false; break;
        }
    }
}

NObj *read_obj(SnapIn &in, fakelua::NativeObjectManager &mgr, long long group,
               std::int64_t id_shift, bool clear_first) {
    const std::string type = in.str();
    const std::int64_t id = in.i64();
    if (!in.ok || type.empty()) { in.ok = false; return nullptr; }
    NObj *o = mgr.Create(group, type, id + id_shift);
    if (!o) { in.ok = false; return nullptr; }
    if (clear_first) clear_recursive(o);
    read_fields(o, in, mgr, group, id_shift, clear_first);
    return o;
}

void read_list(std::vector<NObj *> &v, SnapIn &in, fakelua::NativeObjectManager &mgr, long long group,
               std::int64_t id_shift, bool clear_first) {
    const std::uint32_t n = in.u32();
    if (!in.ok) return;
    v.clear();
    v.reserve(n);
    for (std::uint32_t i = 0; i < n && in.ok; ++i) v.push_back(read_obj(in, mgr, group, id_shift, clear_first));
}

struct OwnedSet {
    NObj *root = nullptr;
    std::vector<std::vector<NObj *>> lists; // matches view.lists indexing
    std::int64_t next_id = 0;
};

OwnedSet capture_set(const ObjectSnapshotChain::View &view) {
    OwnedSet s;
    s.root = view.root ? *view.root : nullptr;
    s.lists.resize(view.lists.size());
    for (std::size_t i = 0; i < view.lists.size(); ++i) {
        if (view.lists[i].objects) s.lists[i] = *view.lists[i].objects;
    }
    s.next_id = view.next_id ? *view.next_id : 0;
    return s;
}

void write_body(SnapOut &out, const ObjectSnapshotChain::Schema &schema,
                const ObjectSnapshotChain::View &view, const OwnedSet &set,
                bool full, std::int64_t id_shift,
                std::unordered_map<NObj *, ObjSnapEntry> *cache = nullptr,
                std::uint64_t check_gen = 0) {
    auto emit = [&](NObj *o) {
        if (cache && !full && id_shift == 0) write_obj_cached(o, out, schema, *cache, check_gen, 0);
        else write_obj(o, out, schema, full, 0, id_shift);
    };
    emit(set.root);
    for (std::size_t i = 0; i < view.lists.size(); ++i) {
        if (!view.lists[i].persistent) {
            out.u32(0);
            continue;
        }
        out.u32(static_cast<std::uint32_t>(set.lists[i].size()));
        for (NObj *o : set.lists[i]) emit(o);
    }
    out.i64(set.next_id);
}

bool read_body(SnapIn &in, fakelua::NativeObjectManager &mgr, long long group,
               const ObjectSnapshotChain::Schema &schema, const ObjectSnapshotChain::View &view,
               bool root_global, std::int64_t id_shift, bool clear_first, OwnedSet &set) {
    const std::string rtype = in.str();
    const std::int64_t rid = in.i64();
    if (!in.ok || rtype.empty()) return false;
    NObj *root = root_global ? mgr.GlobalCreate(schema.root_global, rtype, rid)
                             : mgr.Create(group, rtype, rid + id_shift);
    if (!root) return false;
    if (clear_first) clear_recursive(root);
    set.root = root;
    read_fields(root, in, mgr, group, id_shift, clear_first);
    set.lists.resize(view.lists.size());
    for (std::size_t i = 0; i < view.lists.size() && in.ok; ++i) {
        read_list(set.lists[i], in, mgr, group, id_shift, clear_first);
    }
    if (in.ok) set.next_id = in.i64();
    return in.ok;
}

bool walk_dirty(NObj *o, std::unordered_map<NObj *, VersionMark> &marks, std::uint64_t gen,
                bool commit, std::size_t &table_objs, std::size_t depth = 0) {
    if (!o) return false;
    if (depth > 64) return true;
    const std::uint64_t v = o->GetVersion();
    auto it = marks.find(o);
    if (it != marks.end() && it->second.version == v) {
        if (commit) it->second.gen = gen;
        bool dirty = it->second.has_table;
        if (it->second.has_table && commit) ++table_objs;
        for (NObj *c : it->second.children) {
            if (walk_dirty(c, marks, gen, commit, table_objs, depth + 1)) dirty = true;
        }
        return dirty;
    }
    bool dirty = true;
    bool has_table = false;
    std::vector<NObj *> kids;
    o->ForEach([&](std::string_view key, NObj::FieldKind k) {
        if (k == NObj::FieldKind::Table) {
            has_table = true;
        } else if (k == NObj::FieldKind::Object) {
            if (NObj *c = o->GetObject(key)) kids.push_back(c);
        }
    });
    for (NObj *c : kids) {
        walk_dirty(c, marks, gen, commit, table_objs, depth + 1);
    }
    if (has_table && commit) ++table_objs;
    if (commit) {
        marks[o] = VersionMark{v, gen, has_table, std::move(kids)};
    }
    return dirty;
}

void destroy_tree(fakelua::NativeObjectManager &mgr, NObj *o) {
    if (!o) return;
    std::vector<NObj *> all;
    std::function<void(NObj *)> collect = [&](NObj *x) {
        if (!x) return;
        all.push_back(x);
        x->ForEach([&](std::string_view key, NObj::FieldKind k) {
            if (k == NObj::FieldKind::Object) collect(x->GetObject(key));
        });
    };
    collect(o);
    for (NObj *x : all) mgr.DestroySingle(x->GetTypeName(), x->GetId());
}

struct DeltaItems {
    std::vector<std::pair<std::uint8_t, NObj *>> upserts;
    std::vector<std::pair<std::uint8_t, std::int64_t>> deletes;
    std::vector<std::pair<std::uint8_t, std::vector<std::int64_t>>> listops;
};

std::string encode_delta(const DeltaItems &items, const ObjectSnapshotChain::Schema &schema,
                         std::int64_t next_id, std::int64_t elapsed,
                         std::unordered_map<NObj *, ObjSnapEntry> &cache, std::uint64_t check_gen) {
    SnapOut out;
    out.u32(kDeltaMagic);
    out.u32(kDeltaFmt);
    out.i64(next_id);
    out.i64(elapsed);
    out.u32(static_cast<std::uint32_t>(items.upserts.size()));
    for (const auto &[list, o] : items.upserts) {
        out.u8(list);
        write_obj_cached(o, out, schema, cache, check_gen, 0);
    }
    out.u32(static_cast<std::uint32_t>(items.deletes.size()));
    for (const auto &[list, id] : items.deletes) {
        out.u8(list);
        out.i64(id);
    }
    out.u32(static_cast<std::uint32_t>(items.listops.size()));
    for (const auto &[list, ids] : items.listops) {
        out.u8(list);
        out.u32(static_cast<std::uint32_t>(ids.size()));
        for (std::int64_t id : ids) out.i64(id);
    }
    return out.b;
}

std::vector<std::size_t> persistent_indices(const ObjectSnapshotChain::View &view) {
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < view.lists.size(); ++i) {
        if (view.lists[i].persistent) idx.push_back(i);
    }
    return idx;
}

bool apply_delta(const std::string &body, fakelua::NativeObjectManager &mgr, long long group,
                 const ObjectSnapshotChain::Schema &schema, const ObjectSnapshotChain::View &view,
                 const std::vector<std::size_t> &pidx, bool root_global, std::int64_t id_shift,
                 OwnedSet &set) {
    SnapIn in{body, 0};
    if (in.u32() != kDeltaMagic || in.u32() != kDeltaFmt) return false;
    const std::int64_t nid = in.i64();
    in.i64(); // elapsed
    const std::uint32_t nu = in.u32();
    for (std::uint32_t i = 0; i < nu && in.ok; ++i) {
        const std::uint8_t list = static_cast<std::uint8_t>(in.u8());
        if (list > pidx.size()) return false;
        if (list == 0) {
            const std::string type = in.str();
            const std::int64_t id = in.i64();
            if (!in.ok || type.empty()) return false;
            NObj *r = root_global ? mgr.GlobalCreate(schema.root_global, type, id)
                                  : mgr.Create(group, type, id + id_shift);
            if (!r) return false;
            clear_recursive(r);
            set.root = r;
            read_fields(r, in, mgr, group, id_shift, true);
        } else {
            read_obj(in, mgr, group, id_shift, true);
        }
    }
    const std::uint32_t nd = in.u32();
    for (std::uint32_t i = 0; i < nd && in.ok; ++i) {
        const std::uint8_t list = static_cast<std::uint8_t>(in.u8());
        const std::int64_t id = in.i64();
        if (list == 0 || list > pidx.size()) return false;
        destroy_tree(mgr, mgr.Get(view.lists[pidx[list - 1]].type_name, id + id_shift));
    }
    const std::uint32_t nl = in.u32();
    for (std::uint32_t i = 0; i < nl && in.ok; ++i) {
        const std::uint8_t list = static_cast<std::uint8_t>(in.u8());
        const std::uint32_t cnt = in.u32();
        if (list == 0 || list > pidx.size()) return false;
        const std::size_t vidx = pidx[list - 1];
        std::vector<NObj *> v;
        v.reserve(cnt);
        for (std::uint32_t k = 0; k < cnt && in.ok; ++k) {
            const std::int64_t id = in.i64();
            NObj *o = mgr.Get(view.lists[vidx].type_name, id + id_shift);
            if (!o) return false;
            v.push_back(o);
        }
        set.lists[vidx] = std::move(v);
    }
    if (!in.ok || in.p != body.size()) return false;
    set.next_id = nid;
    return true;
}

std::size_t reanchor_limit(const ObjectSnapshotChain::Schema &schema, std::size_t anchor_bytes) {
    if (!schema.reanchor_env.empty()) {
        if (const char *s = std::getenv(schema.reanchor_env.c_str()); s && *s) {
            return static_cast<std::size_t>(std::strtoull(s, nullptr, 10));
        }
    }
    return anchor_bytes;
}

} // namespace

struct ObjectSnapshotChain::Impl {
    bool valid = false;
    int ay = 0, am = 0, ad = 0;
    std::string anchor_body;
    std::vector<std::string> deltas;
    std::size_t delta_bytes = 0;
    std::unordered_map<NObj *, VersionMark> versions;
    mutable std::unordered_map<NObj *, ObjSnapEntry> obj_cache;
    std::uint64_t gen = 0;
    mutable std::uint64_t cache_gen = 0;
    std::vector<std::vector<std::int64_t>> list_ids;
    std::int64_t next_id = 0;
    std::size_t table_objs = 0;
    mutable std::size_t reserve_bytes = 0;

    void prune_cache() const {
        for (auto it = obj_cache.begin(); it != obj_cache.end();) {
            if (!versions.count(it->first)) it = obj_cache.erase(it);
            else ++it;
        }
    }

    void refresh_baselines(const View &view, const OwnedSet &set, int y, int m, int d) {
        const auto pidx = persistent_indices(view);
        next_id = set.next_id;
        ay = y; am = m; ad = d;
        list_ids.resize(pidx.size());
        for (std::size_t k = 0; k < pidx.size(); ++k) {
            const auto &src = set.lists[pidx[k]];
            list_ids[k].clear();
            list_ids[k].reserve(src.size());
            for (NObj *o : src) list_ids[k].push_back(o ? o->GetId() : 0);
        }
        const std::uint64_t g = ++gen;
        std::size_t tables = 0;
        walk_dirty(set.root, versions, g, true, tables);
        for (std::size_t k = 0; k < pidx.size(); ++k) {
            for (NObj *o : set.lists[pidx[k]]) walk_dirty(o, versions, g, true, tables);
        }
        for (auto it = versions.begin(); it != versions.end();) {
            if (it->second.gen != g) it = versions.erase(it);
            else ++it;
        }
        prune_cache();
        table_objs = tables;
    }
};

ObjectSnapshotChain::ObjectSnapshotChain() : impl_(new Impl()) {}
ObjectSnapshotChain::~ObjectSnapshotChain() { delete impl_; }

void ObjectSnapshotChain::Clear() {
    const std::size_t res = impl_->reserve_bytes;
    *impl_ = Impl{};
    impl_->reserve_bytes = res;
}

std::string ObjectSnapshotChain::SnapshotFull(void *lua_state, const Schema &schema,
                                              const View &view, bool include_transients) const {
    if (!lua_state || !view.root || !*view.root || !view.group || !*view.group) return {};
    const OwnedSet set = capture_set(view);
    SnapOut out;
    out.reserve(impl_->reserve_bytes);
    out.b.append(schema.magic, 4);
    out.u32(1);
    if (!include_transients) {
        write_body(out, schema, view, set, false, 0, &impl_->obj_cache, ++impl_->cache_gen);
    } else {
        write_body(out, schema, view, set, true, 0);
    }
    impl_->reserve_bytes = std::max(impl_->reserve_bytes, out.b.size());
    return out.b;
}

bool ObjectSnapshotChain::Reset(void *lua_state, const Schema &schema, const View &view, int y, int m, int d) {
    if (!lua_state || !view.root || !*view.root || !view.group || !*view.group) return false;
    const OwnedSet set = capture_set(view);
    SnapOut out;
    out.reserve(impl_->reserve_bytes);
    write_body(out, schema, view, set, false, 0, &impl_->obj_cache, ++impl_->cache_gen);
    impl_->reserve_bytes = std::max(impl_->reserve_bytes, out.b.size());
    impl_->anchor_body = std::move(out.b);
    impl_->deltas.clear();
    impl_->delta_bytes = 0;
    impl_->valid = true;
    impl_->refresh_baselines(view, set, y, m, d);
    return true;
}

void ObjectSnapshotChain::Rebase(const View &view, int y, int m, int d) {
    if (!impl_->valid || !view.root || !*view.root || !view.group || !*view.group) return;
    const OwnedSet set = capture_set(view);
    impl_->refresh_baselines(view, set, y, m, d);
}

bool ObjectSnapshotChain::CatchUp(void *lua_state, const Schema &schema, const View &view,
                                  int y, int m, int d, std::int64_t elapsed) {
    if (!lua_state || !view.root || !*view.root || !view.group || !*view.group) return false;
    const std::size_t limit = reanchor_limit(schema, impl_->anchor_body.size());
    if (!impl_->valid || impl_->ay != y || impl_->am != m ||
        impl_->deltas.size() >= kDeltaMaxPerMonth ||
        (limit && impl_->delta_bytes >= limit)) {
        return Reset(lua_state, schema, view, y, m, d);
    }
    const OwnedSet set = capture_set(view);
    const auto pidx = persistent_indices(view);
    impl_->list_ids.resize(pidx.size());

    DeltaItems items;
    const std::uint64_t g = ++impl_->gen;
    std::size_t tables = 0;
    if (walk_dirty(set.root, impl_->versions, g, true, tables)) {
        items.upserts.emplace_back(static_cast<std::uint8_t>(0), set.root);
    }
    for (std::size_t k = 0; k < pidx.size(); ++k) {
        const auto &src = set.lists[pidx[k]];
        std::vector<std::int64_t> ids;
        ids.reserve(src.size());
        for (NObj *o : src) {
            ids.push_back(o ? o->GetId() : 0);
            if (walk_dirty(o, impl_->versions, g, true, tables)) {
                items.upserts.emplace_back(static_cast<std::uint8_t>(k + 1), o);
            }
        }
        if (ids != impl_->list_ids[k]) {
            std::unordered_set<std::int64_t> cur(ids.begin(), ids.end());
            for (std::int64_t id : impl_->list_ids[k]) {
                if (!cur.count(id)) items.deletes.emplace_back(static_cast<std::uint8_t>(k + 1), id);
            }
            items.listops.emplace_back(static_cast<std::uint8_t>(k + 1), ids);
            impl_->list_ids[k] = std::move(ids);
        }
    }
    for (auto it = impl_->versions.begin(); it != impl_->versions.end();) {
        if (it->second.gen != g) it = impl_->versions.erase(it);
        else ++it;
    }
    impl_->prune_cache();
    impl_->table_objs = tables;
    impl_->next_id = set.next_id;
    std::string body = encode_delta(items, schema, set.next_id, elapsed, impl_->obj_cache, ++impl_->cache_gen);
    impl_->delta_bytes += body.size();
    impl_->deltas.push_back(std::move(body));
    if (!schema.stats_env.empty() && std::getenv(schema.stats_env.c_str())) {
        std::fprintf(stderr, "[snap] %d-%d-%d upserts=%zu deltas=%zu bytes=%zu tables=%zu chain=%zu\n",
                     y, m, d, items.upserts.size(), impl_->deltas.size(), impl_->deltas.back().size(),
                     tables, impl_->anchor_body.size() + impl_->delta_bytes);
    }
    return true;
}

std::string ObjectSnapshotChain::Materialize(void *lua_state, const Schema &schema, const View &view) const {
    if (!lua_state || !impl_->valid || !view.group || !*view.group) return {};
    auto *s = static_cast<fakelua::State *>(lua_state);
    auto &mgr = fakelua::GetNativeObjectManager(s);
    const long long gid = mgr.CreateGroup();
    OwnedSet set;
    const auto pidx = persistent_indices(view);
    SnapIn in{impl_->anchor_body, 0};
    bool ok = read_body(in, mgr, gid, schema, view, false, kTempIdShift, false, set) &&
              in.p == impl_->anchor_body.size();
    for (std::size_t i = 0; ok && i < impl_->deltas.size(); ++i) {
        ok = apply_delta(impl_->deltas[i], mgr, gid, schema, view, pidx, false, kTempIdShift, set);
    }
    std::string body;
    if (ok) {
        SnapOut out;
        write_body(out, schema, view, set, false, kTempIdShift);
        body = std::move(out.b);
    }
    mgr.DestroyGroup(gid);
    return body;
}

std::string ObjectSnapshotChain::SnapshotForSave(void *lua_state, const Schema &schema, const View &view,
                                                 bool materialize_anchor, std::int64_t elapsed) {
    if (!lua_state || !view.root || !*view.root || !view.group || !*view.group) return {};
    if (!impl_->valid) {
        const int y = static_cast<int>((*view.root)->GetInt("date_y", 0));
        const int m = static_cast<int>((*view.root)->GetInt("date_m", 0));
        const int d = static_cast<int>((*view.root)->GetInt("date_d", 0));
        if (!Reset(lua_state, schema, view, y, m, d)) return {};
    }
    if (materialize_anchor) {
        const std::string body = Materialize(lua_state, schema, view);
        if (body.empty()) return {};
        SnapOut out;
        out.b.append(schema.magic, 4);
        out.u32(1);
        out.b += body;
        return out.b;
    }
    const OwnedSet set = capture_set(view);
    const auto pidx = persistent_indices(view);
    impl_->list_ids.resize(pidx.size());
    DeltaItems items;
    std::size_t tables = 0;
    if (walk_dirty(set.root, impl_->versions, impl_->gen, false, tables)) {
        items.upserts.emplace_back(static_cast<std::uint8_t>(0), set.root);
    }
    for (std::size_t k = 0; k < pidx.size(); ++k) {
        const auto &src = set.lists[pidx[k]];
        std::vector<std::int64_t> ids;
        ids.reserve(src.size());
        for (NObj *o : src) {
            ids.push_back(o ? o->GetId() : 0);
            if (walk_dirty(o, impl_->versions, impl_->gen, false, tables)) {
                items.upserts.emplace_back(static_cast<std::uint8_t>(k + 1), o);
            }
        }
        if (ids != impl_->list_ids[k]) {
            std::unordered_set<std::int64_t> cur(ids.begin(), ids.end());
            for (std::int64_t id : impl_->list_ids[k]) {
                if (!cur.count(id)) items.deletes.emplace_back(static_cast<std::uint8_t>(k + 1), id);
            }
            items.listops.emplace_back(static_cast<std::uint8_t>(k + 1), ids);
        }
    }
    const std::string now = encode_delta(items, schema, set.next_id, elapsed, impl_->obj_cache, ++impl_->cache_gen);
    SnapOut out;
    auto put_block = [&](const std::string &blk) {
        out.u32(static_cast<std::uint32_t>(blk.size()));
        out.u32(SaveStore::Crc32(blk));
        out.b += blk;
    };
    out.b.append(schema.magic, 4);
    out.u32(2);
    put_block(impl_->anchor_body);
    out.u32(static_cast<std::uint32_t>(impl_->deltas.size() + 1));
    for (const std::string &d : impl_->deltas) put_block(d);
    put_block(now);
    return out.b;
}

bool ObjectSnapshotChain::Restore(void *lua_state, const Schema &schema, View &view,
                                  const std::string &buf, std::string *error_out,
                                  std::string *warning_out) {
    if (!lua_state || !view.root || !view.group || !view.next_id) return false;
    if (buf.size() < 8 || std::memcmp(buf.data(), schema.magic, 4) != 0) return false;
    auto *s = static_cast<fakelua::State *>(lua_state);
    auto &mgr = fakelua::GetNativeObjectManager(s);

    auto install = [&](const std::string &body) -> bool {
        impl_->versions.clear();
        impl_->obj_cache.clear();
        if (*view.group) mgr.DestroyGroup(*view.group);
        if (mgr.GlobalGet(schema.root_global)) mgr.GlobalDestroy(schema.root_global);
        *view.root = nullptr;
        for (auto &lb : view.lists) if (lb.objects) lb.objects->clear();
        *view.next_id = 1;
        *view.group = mgr.CreateGroup();
        SnapIn in{body, 0};
        OwnedSet set;
        if (!read_body(in, mgr, *view.group, schema, view, true, 0, false, set)) return false;
        if (in.p != body.size()) return false;
        *view.root = set.root;
        for (std::size_t i = 0; i < view.lists.size(); ++i) {
            if (view.lists[i].objects) *view.lists[i].objects = std::move(set.lists[i]);
        }
        *view.next_id = set.next_id;
        return true;
    };

    auto adopt = [&](const std::string &anchor, std::vector<std::string> deltas) {
        impl_->anchor_body = anchor;
        impl_->deltas = std::move(deltas);
        impl_->delta_bytes = 0;
        for (const std::string &d : impl_->deltas) impl_->delta_bytes += d.size();
        impl_->valid = true;
        NObj *R = *view.root;
        const int y = R ? static_cast<int>(R->GetInt("date_y", 0)) : 0;
        const int m = R ? static_cast<int>(R->GetInt("date_m", 0)) : 0;
        const int d = R ? static_cast<int>(R->GetInt("date_d", 0)) : 0;
        const OwnedSet set = capture_set(view);
        impl_->refresh_baselines(view, set, y, m, d);
    };

    const std::uint32_t fmt = SnapIn{buf, 4}.u32();
    if (fmt == 1) {
        const std::string body = buf.substr(8);
        if (!install(body)) return false;
        adopt(body, {});
        return true;
    }
    if (fmt != 2) return false;

    SnapIn hdr{buf, 4};
    hdr.u32();
    const std::uint32_t alen = hdr.u32();
    const std::uint32_t acrc = hdr.u32();
    if (!hdr.ok || alen > buf.size() - hdr.p) return false;
    const std::string anchor = buf.substr(hdr.p, alen);
    hdr.p += alen;
    if (SaveStore::Crc32(anchor) != acrc) {
        if (error_out) *error_out = "anchor checksum mismatch";
        return false;
    }
    if (!install(anchor)) return false;
    const std::uint32_t nd = hdr.u32();
    if (!hdr.ok) {
        adopt(anchor, {});
        if (warning_out) {
            *warning_out = "save chain truncated after the monthly anchor; rolled back to the anchor";
            log::Info("[load] " + *warning_out);
        }
        return true;
    }

    OwnedSet set = capture_set(view);
    const auto pidx = persistent_indices(view);
    std::vector<std::string> applied;
    bool bad = false;
    std::size_t bad_at = 0;
    for (std::uint32_t i = 0; i < nd; ++i) {
        const std::uint32_t len = hdr.u32();
        const std::uint32_t crc = hdr.u32();
        if (!hdr.ok || len > buf.size() - hdr.p) { bad = true; bad_at = i + 1; break; }
        std::string body = buf.substr(hdr.p, len);
        hdr.p += len;
        if (bad) continue;
        if (SaveStore::Crc32(body) != crc) { bad = true; bad_at = i + 1; continue; }
        if (!apply_delta(body, mgr, *view.group, schema, view, pidx, true, 0, set)) {
            bad = true;
            bad_at = i + 1;
            continue;
        }
        applied.push_back(std::move(body));
    }

    if (!bad) {
        for (std::size_t i = 0; i < view.lists.size(); ++i) {
            if (view.lists[i].objects) *view.lists[i].objects = std::move(set.lists[i]);
        }
        *view.next_id = set.next_id;
        adopt(anchor, std::move(applied));
        return true;
    }

    if (!install(anchor)) return false;
    adopt(anchor, {});
    char msg[256];
    std::snprintf(msg, sizeof msg,
                  "save chain corrupted at delta %zu/%u; rolled back to monthly anchor %d-%d-%d (%u days lost)",
                  bad_at, nd, impl_->ay, impl_->am, impl_->ad,
                  nd - static_cast<unsigned>(applied.size()));
    if (warning_out) {
        *warning_out = msg;
        log::Info(std::string("[load] ") + msg);
    }
    return true;
}

std::string ObjectSnapshotChain::SaveTransients(const Schema &schema, const View &view, bool all) const {
    if (!view.root || !view.group || !*view.group) return {};
    SnapOut out;
    std::vector<std::pair<std::string_view, NObj::FieldKind>> fs;
    const int mode = all ? 0 : 1;
    auto dump_one = [&](NObj *o) {
        fs.clear();
        if (o) {
            const std::string_view tname = o->GetTypeName();
            o->ForEach([&](std::string_view key, NObj::FieldKind k) {
                if (k == NObj::FieldKind::Nil || k == NObj::FieldKind::Table || k == NObj::FieldKind::Object) return;
                if (!is_transient_key(schema, tname, key, mode)) return;
                fs.emplace_back(key, k);
            });
            std::sort(fs.begin(), fs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        }
        out.u32(static_cast<std::uint32_t>(fs.size()));
        for (const auto &[key, k] : fs) {
            out.str(key);
            switch (k) {
                case NObj::FieldKind::Int: out.u8(1); out.i64(o->GetInt(key)); break;
                case NObj::FieldKind::Float: out.u8(2); out.f64(o->GetFloat(key)); break;
                case NObj::FieldKind::Bool: out.u8(3); out.u8(o->GetBool(key) ? 1 : 0); break;
                default: out.u8(4); out.str(o->GetString(key)); break;
            }
        }
    };
    dump_one(*view.root);
    for (const auto &lb : view.lists) {
        const auto *v = lb.objects;
        const std::uint32_t n = v ? static_cast<std::uint32_t>(v->size()) : 0u;
        out.u32(n);
        if (v) for (NObj *o : *v) dump_one(o);
    }
    return out.b;
}

void ObjectSnapshotChain::LoadTransients(const Schema &schema, const View &view,
                                         const std::string &buf, bool all) const {
    (void)schema;
    (void)all;
    if (buf.empty() || !view.root || !view.group || !*view.group) return;
    SnapIn in{buf, 0};
    auto load_one = [&](NObj *o) {
        const std::uint32_t n = in.u32();
        if (!in.ok) return;
        for (std::uint32_t i = 0; i < n && in.ok; ++i) {
            const std::string key = in.str();
            const unsigned kind = in.u8();
            if (!in.ok) return;
            if (!o) {
                switch (kind) {
                    case 1: in.i64(); break;
                    case 2: in.f64(); break;
                    case 3: in.u8(); break;
                    default: in.str(); break;
                }
                continue;
            }
            switch (kind) {
                case 1: o->SetInt(key, in.i64()); break;
                case 2: o->SetFloat(key, in.f64()); break;
                case 3: o->SetBool(key, in.u8() != 0); break;
                default: o->SetString(key, in.str()); break;
            }
        }
    };
    load_one(*view.root);
    for (const auto &lb : view.lists) {
        const std::uint32_t n = in.u32();
        if (!in.ok) return;
        const auto *v = lb.objects;
        for (std::uint32_t i = 0; i < n && in.ok; ++i) {
            load_one(v && i < v->size() ? (*v)[i] : nullptr);
        }
    }
}

} // namespace fake2d
