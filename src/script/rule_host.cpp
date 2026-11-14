#include "fake2d/rule_host.h"

#include "fakelua.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

#include "fake2d/log.h"

namespace fake2d {

namespace {

// 与宿主 script_log 同一纪律：日志自带回显与换行，此处只剥尾部换行防双行。
void host_log(std::string message) {
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();
    log::Error(message);
}

} // namespace

struct RuleHost::Impl {
    fakelua::State *state = nullptr;
    NativeChecker check;
};

RuleHost::RuleHost() : impl_(new Impl()) {}
RuleHost::~RuleHost() { delete impl_; }

bool RuleHost::Opened() const { return impl_->state != nullptr; }

void *RuleHost::State() { return impl_->state; }

void RuleHost::SetNativeChecker(NativeChecker check) { impl_->check = std::move(check); }

bool RuleHost::Open(const NativeRegistrar &reg) {
    if (impl_->state) return true;
    fakelua::StateConfig cfg;
    cfg.log_level = fakelua::LogLevel::Off;   // fakelua 自身日志静默
    impl_->state = fakelua::FakeluaNewState(cfg);
    if (!impl_->state) return false;
    if (reg) reg(impl_->state);
    return true;
}

std::vector<std::string> RuleHost::DiscoverChunks(const std::string &dir, std::string_view prefix,
                                                  const std::string &first) {
    std::vector<std::string> chunks{first};
    std::error_code ec;
    for (const auto &de : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        const std::string fn = de.path().filename().string();
        if (fn.size() <= prefix.size() + 4 || fn.compare(0, prefix.size(), prefix) != 0) continue;
        if (fn.compare(fn.size() - 4, 4, ".lua") != 0) continue;
        if (fn.find('~') != std::string::npos || fn.find(".bak") != std::string::npos) continue;
        chunks.push_back(de.path().string());
    }
    std::sort(chunks.begin() + 1, chunks.end());
    return chunks;
}

// 装载期把脚本里按名调用的原语逐个核对：FakeLua 对未注册名不报错（按名调用回 0/nil），
// 拼错一个名字就是「判定悄悄走了另一支」——位对拍与像素门禁都看不出来。干净时一行不打。
static void LintUnit(fakelua::State *state, const std::string &path, const char *lint_tag,
                     const RuleHost::NativeChecker &check) {
    std::ifstream in(path);
    if (!in) return;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::set<std::string> names;
    for (std::size_t i = 0; i + 3 < text.size(); ++i) {
        if (text[i] != 'm' && text[i] != 'u') continue;
        if (i && (isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '_')) continue;
        std::size_t j = i + 1;
        if (text[i] == 'm') {
            if (text.compare(i, 3, "md_") != 0) continue;
            j = i + 3;
        } else {
            if (text.compare(i, 2, "u_") != 0) continue;
            j = i + 2;
        }
        while (j < text.size() && (isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) ++j;
        // 只要「后面紧跟 ( 」的调用点，且名字长于裸前缀
        std::size_t k = j;
        while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) ++k;
        if (k >= text.size() || text[k] != '(') continue;
        if (j - i > 3) names.insert(text.substr(i, j - i));
    }
    for (const auto &name : names) {
        // 入口按任一后端存在即算已定义（前导件固定解释器，其它分块可能 GCC）
        if ((!check || !check(name.c_str())) && !fakelua::HasEntryFunction(state, fakelua::JIT_INTERP, name) &&
            !fakelua::HasEntryFunction(state, fakelua::JIT_GCC, name))
            host_log(std::string("script lint: ") + lint_tag + " calls unregistered '" + name + "'");
    }
}

bool RuleHost::CompileUnit(const std::string &path, bool gcc, const char *log_prefix, const char *lint_tag) {
    fakelua::CompileConfig cc;
    cc.debug_mode = false;
    cc.disable_jit[fakelua::JIT_TCC] = true;
    if (!gcc) cc.disable_jit[fakelua::JIT_GCC] = true;
    try {
        fakelua::CompileFile(impl_->state, path, cc);
    } catch (const std::exception &e) {
        host_log(std::string(log_prefix) + ": compile " + path + " failed: " + e.what());
        return false;
    }
    LintUnit(impl_->state, path, lint_tag, impl_->check);
    return true;
}

bool RuleHost::CompileUnitWithFallback(const std::string &path, bool &gcc, const char *log_prefix,
                                       const char *lint_tag) {
    std::string first_err;
    {
        fakelua::CompileConfig cc;
        cc.debug_mode = false;
        cc.disable_jit[fakelua::JIT_TCC] = true;
        if (!gcc) cc.disable_jit[fakelua::JIT_GCC] = true;
        try {
            fakelua::CompileFile(impl_->state, path, cc);
        } catch (const std::exception &e) {
            first_err = e.what();
        }
    }
    if (first_err.empty()) {
        LintUnit(impl_->state, path, lint_tag, impl_->check);
        return true;
    }
    gcc = false;   // 后端回落：GCC 编不过整层退解释器，上下文脚本必须同一后端
    fakelua::CompileConfig cc;
    cc.debug_mode = false;
    cc.disable_jit[fakelua::JIT_TCC] = true;
    cc.disable_jit[fakelua::JIT_GCC] = true;
    try {
        fakelua::CompileFile(impl_->state, path, cc);
    } catch (const std::exception &e) {
        host_log(std::string(log_prefix) + ": compile " + path + " failed: " + first_err +
                 "; interpreter: " + e.what());
        return false;
    }
    LintUnit(impl_->state, path, lint_tag, impl_->check);
    return true;
}

bool RuleHost::CompilePrelude(const std::string &path, const char *push_rule, const char *log_prefix) {
    fakelua::CompileConfig cc;
    cc.debug_mode = false;
    cc.disable_jit[fakelua::JIT_TCC] = true;
    cc.disable_jit[fakelua::JIT_GCC] = true;   // 前导件固定解释器：一次性上行推送，不值得付编译价
    try {
        fakelua::CompileFile(impl_->state, path, cc);
    } catch (const std::exception &e) {
        host_log(std::string(log_prefix) + ": compile " + path + " failed: " + e.what());
        return false;
    }
    LintUnit(impl_->state, path, std::filesystem::path(path).filename().string().c_str(), impl_->check);
    if (!fakelua::HasEntryFunction(impl_->state, fakelua::JIT_INTERP, push_rule)) {
        host_log(std::string(log_prefix) + ": " + push_rule + " missing in " + path);
        return false;
    }
    try {
        double ret = 0;
        fakelua::Call(impl_->state, fakelua::JIT_INTERP, push_rule, ret);
    } catch (const std::exception &e) {
        host_log(std::string(log_prefix) + ": " + push_rule + " failed: " + e.what());
        return false;
    }
    return true;
}

bool RuleHost::CompileSetWithFallback(const std::vector<std::string> &files, bool &gcc,
                                      const char *log_prefix, const ProgressFn &notify,
                                      const std::function<void()> &on_retry, const LintTagFn &lint_tag) {
    std::string failed_path, failed_err;
    auto try_pass = [&](bool use_gcc) {
        fakelua::CompileConfig cc;
        cc.debug_mode = false;
        cc.disable_jit[fakelua::JIT_TCC] = true;
        if (!use_gcc) cc.disable_jit[fakelua::JIT_GCC] = true;
        for (const auto &p : files) {
            if (notify) notify(p);
            try {
                fakelua::CompileFile(impl_->state, p, cc);
            } catch (const std::exception &e) {
                failed_path = p;
                failed_err = e.what();
                return false;
            }
        }
        return true;
    };
    if (try_pass(gcc)) {
        for (const auto &p : files) LintUnit(impl_->state, p, lint_tag(p).c_str(), impl_->check);
        return true;
    }
    const std::string gcc_err = failed_err, gcc_path = failed_path;
    if (on_retry) on_retry();
    gcc = false;
    if (try_pass(false)) {
        for (const auto &p : files) LintUnit(impl_->state, p, lint_tag(p).c_str(), impl_->check);
        return true;
    }
    host_log(std::string(log_prefix) + ": compile " + gcc_path + " failed: " + gcc_err +
             "; interpreter: " + failed_err);
    return false;
}

bool RuleHost::HasEntry(const char *rule, bool gcc) const {
    return fakelua::HasEntryFunction(impl_->state, gcc ? fakelua::JIT_GCC : fakelua::JIT_INTERP, rule) ||
           fakelua::HasEntryFunction(impl_->state, fakelua::JIT_INTERP, rule);
}

} // namespace fake2d
