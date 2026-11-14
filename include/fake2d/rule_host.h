#pragma once

// 多单元规则宿主的编译侧服务（FakeLua）：state 创建与原生注册（幂等）、脚本分块发现、
// 按后端编译（GCC 失败自动退解释器）、编译后 lint（未注册原语点名）、入口存在性检查。
//
// 这里只管「把脚本编进来」：日志前缀由宿主传入（门禁按前缀 grep 行），规则分派与游戏
// 上下文（World 绑定、参数表、失败后的降级语义）仍归宿主游戏侧。日志经 fake2d::log，
// 与宿主 log_error 同一通道同一格式。

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fake2d {

class RuleHost {
  public:
    RuleHost();
    ~RuleHost();
    RuleHost(const RuleHost &) = delete;
    RuleHost &operator=(const RuleHost &) = delete;

    using NativeRegistrar = std::function<void(void *state)>;
    using NativeChecker = std::function<bool(const char *name)>;

    /// 创建 FakeLua state 并注册原生（reg 幂等：state 已建时跳过）。失败返回 false。
    bool Open(const NativeRegistrar &reg);
    bool Opened() const;
    /// 裸 state 指针：宿主的按名分派仍直接走 fakelua 调用。
    void *State();

    /// 目录下 `prefix*.lua` 分块发现：排序、剔除编辑器临时件（`~` 结尾/`.bak`），
    /// `first`（公共前缀文件）恒居列表头。目录不存在返回仅含 first 的列表由调用方裁决。
    static std::vector<std::string> DiscoverChunks(const std::string &dir, std::string_view prefix,
                                                   const std::string &first);

    /// lint 已注册名表（装载期点名用，可空 = 跳过 lint 的原生核对）。
    void SetNativeChecker(NativeChecker check);

    /// 编译单文件并 lint。gcc=false 走解释器。失败打
    /// "<prefix>: compile <path> failed: <what>" 并返回 false。
    bool CompileUnit(const std::string &path, bool gcc, const char *log_prefix, const char *lint_tag);

    /// 后端回落梯子：*gcc 为真时先编 GCC，失败自动置 false 退解释器重编；两边都失败
    /// 打 "<prefix>: compile <path> failed: <gcc>; interpreter: <interp>" 并返回 false。
    bool CompileUnitWithFallback(const std::string &path, bool &gcc, const char *log_prefix,
                                 const char *lint_tag);

    /// data.lua 式前导件：强制解释器编译 + 入口检查 + 调用推送规则。失败分别打
    /// "<prefix>: compile <path> failed: …" / "<prefix>: <push> missing in <path>"。
    bool CompilePrelude(const std::string &path, const char *push_rule, const char *log_prefix);

    /// 一组上下文分块的后端回落：全组先按 *gcc 逐个编译（notify 逐文件回报）；
    /// 任一失败则 on_retry()（宿主借此卷回进度）、*gcc 置 false，全组从头按解释器
    /// 重编（FakeLua 允许重定义；按名调用必须同后端）。两边都失败打
    /// "<prefix>: compile <path> failed: <gcc>; interpreter: <interp>" 并返回 false。
    /// 全组成功后按最终后端逐文件 lint。lint_tag 由调用方按路径给出。
    using ProgressFn = std::function<void(const std::string &path)>;
    using LintTagFn = std::function<std::string(const std::string &path)>;
    bool CompileSetWithFallback(const std::vector<std::string> &files, bool &gcc, const char *log_prefix,
                                const ProgressFn &notify, const std::function<void()> &on_retry,
                                const LintTagFn &lint_tag);

    bool HasEntry(const char *rule, bool gcc) const;

  private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace fake2d
