#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fakelua {
class NativeObject;
}

namespace fake2d {

/// Generic binary serializer & incremental anchor/delta snapshot chain for
/// FakeLua NativeObject trees (monthly full anchor + daily object-level deltas).
class ObjectSnapshotChain {
  public:
    struct ListBinding {
        const char *type_name = "";
        std::vector<fakelua::NativeObject *> *objects = nullptr;
        bool persistent = true; // false -> writes 0 items on full snapshot (transient/rebuilt)
    };

    /// Predicate returning true when `(type_name, key)` is a transient field
    /// that should be omitted from non-full snapshots (mode==0) or preserved
    /// across loads (mode==1).
    using FieldFilter = std::function<bool(std::string_view type_name, std::string_view key, int mode)>;

    struct Schema {
        char magic[4] = {'M', 'D', 'O', 'S'};
        const char *root_global = "world";
        FieldFilter is_transient;
        std::string reanchor_env;
        std::string stats_env;
    };

    struct View {
        fakelua::NativeObject **root = nullptr;
        long long *group = nullptr;
        std::int64_t *next_id = nullptr;
        std::vector<ListBinding> lists;
    };

    ObjectSnapshotChain();
    ~ObjectSnapshotChain();
    ObjectSnapshotChain(const ObjectSnapshotChain &) = delete;
    ObjectSnapshotChain &operator=(const ObjectSnapshotChain &) = delete;

    void Clear();

    [[nodiscard]] std::string SnapshotFull(void *lua_state, const Schema &schema,
                                           const View &view, bool include_transients = true) const;

    bool Reset(void *lua_state, const Schema &schema, const View &view, int y, int m, int d);
    void Rebase(const View &view, int y, int m, int d);
    bool CatchUp(void *lua_state, const Schema &schema, const View &view,
                 int y, int m, int d, std::int64_t elapsed);

    [[nodiscard]] std::string Materialize(void *lua_state, const Schema &schema,
                                          const View &view) const;
    [[nodiscard]] std::string SnapshotForSave(void *lua_state, const Schema &schema,
                                              const View &view, bool materialize_anchor,
                                              std::int64_t elapsed);
    bool Restore(void *lua_state, const Schema &schema, View &view, const std::string &buf,
                 std::string *error_out = nullptr, std::string *warning_out = nullptr);

    [[nodiscard]] std::string SaveTransients(const Schema &schema, const View &view, bool all) const;
    void LoadTransients(const Schema &schema, const View &view, const std::string &buf, bool all) const;

  private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace fake2d
