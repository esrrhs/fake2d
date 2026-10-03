# FakeLua 缺陷修复文档

| 项目 | 内容 |
|---|---|
| 组件 | FakeLua — https://github.com/esrrhs/fakelua |
| 当前版本 | 2.0.0，本机源码 `/Users/mingming/project/fakelua` @ `ef43eae` |
| 已安装 | `/usr/local/lib/libfakelua.2.0.0.dylib`（2026-10-02 20:44 构建，源码路径字符串为 `/Users/mingming/project/fakelua/src`） |
| 平台 | macOS / arm64，Apple Clang；后端：TCC JIT、GCC JIT、解释器 |
| 文档日期 | 2026-10-02 |

## 结论

历史上共记录 5 个缺陷。**缺陷 1–4 已在 `ef43eae` 修复并在本机实测通过，无需再处理**；
**缺陷 5 尚未修复，是本文档唯一的修复目标。**

| # | 缺陷 | 状态 | 修复提交 |
|---|---|---|---|
| 1 | 多返回值函数被参数类型特化后生成非法 C | 已修复 | `236a757` ＋ `ef43eae` |
| 2 | 尾参数展开把语句宏拼进表达式位置 | 已修复 | `ef43eae` |
| 3 | `FlMakeClosure` 用 `bool` 充当 `va_start` 锚点（C17 UB） | 已修复 | `236a757` |
| 4 | Lua 函数名 `main` 与宿主 C `main` 符号冲突 | 已修复 | `236a757` |
| 5 | 未声明的全局变量被误编译为对常量 `kNil` 的读写 | **待修复** | — |

---

## 待修复：缺陷 5 — 未声明变量被编译为 `kNil` 读写

### 现象

函数内引用一个既没有 `local` 声明、文件级也不存在的变量名（在标准 Lua 中这是合法的
隐式全局变量，初值为 `nil`）：

- **读取**该变量：被编译成 nil 单例符号 `kNil`，结果碰巧恒为 nil；
- **写入**该变量：被编译成 `kNil = <值>;`，而 `kNil` 是 `static const CVar`，
  GCC 后端直接编译失败，且错误信息对脚本作者完全不可定位。

### 最小复现用例

文件：`/tmp/flua_verify/bug5_implicit_global.lua`

```lua
local function main()
    local vx = -1
    if vx < 0 then flag = 1 end      -- 写入未声明的全局 flag
    if flag == 1 then flag = 0 end   -- 读取后再写
    return 0
end
```

### 实测报错（GCC 后端，2026-10-02）

```
fakelua_jit_xxxx.c: error: cannot assign to variable 'kNil' with
const-qualified type 'const CVar' (aka 'const struct CVar')
```

注意：`CompileFile` 在加载阶段就会预编译 GCC 变体，因此**即使用解释器方式调用，
脚本同样不可用**，报错同为 GCC compile failed。

### 生成的错误 C 代码（实测抓取）

```c
static const CVar kNil = (CVar){.type_ = VAR_NIL};
...
OpEq((kNil), ((CVar){.type_ = VAR_INT, .data_.i = 0}), flua_op);  /* 读 flag → kNil */
...
kNil = (CVar){.type_ = VAR_INT, .data_.i = 1};                    /* 写 flag → 改常量，编译失败 */
```

### 根因（代码位置）

文件 `/Users/mingming/project/fakelua/src/compile/c_gen.cpp`：

1. **读取侧 — `CompileVar()` 约 L2797–L2803。**
   对未在任何作用域声明的简单变量，函数直接返回字符串 `"kNil"`：

   ```cpp
   // 未声明/未定义的简单变量：Lua 语义中未定义变量求值为 nil。
   if (!var_to_def_map_.contains(v_ptr.get()) && !ir().global_const_vars.contains(name)
       && !global_const_table_vars_.contains(name)) {
       return (cur_section_ == Section::Globals) ? "(CVar){.type_ = VAR_NIL}" : "kNil";
   }
   ```

   这段逻辑只处理了「读取结果应为 nil」，没有区分变量出现的位置是读还是写。

2. **写入侧 — 简单变量赋值约 L1447。**
   赋值语句把 `CompileVar()` 的返回值直接作为左值：

   ```cpp
   Out() << GenTab() << CompileVar(v_ptr) << " = " << rhs << ";\n";
   ```

   `CompileVar()` 返回 `"kNil"` 后，生成 `kNil = rhs;`，即对 const 全局对象赋值。

### 修复方案（二选一）

**方案 A（推荐）：语义分析阶段拒绝未声明变量。**
fake2d 的工程约定是所有状态显式走文件级 `local`，不使用隐式全局。在语义分析阶段
（`src/compile/semantic_analysis.cpp` 的名字解析/`CheckUnsupportedSyntax` 路径）
对未声明的简单变量直接报错，并给出可定位的信息，例如：

```
unknown variable 'flag' at bug.lua:3:21; declare it with 'local' first
```

同时移除 `CompileVar()` 中返回 `"kNil"` 的兜底分支（保留也可作为最后防线，
但不应再出现在正常路径）。

**方案 B：支持 Lua 全局语义。**
未声明名字按 `_ENV`/`_G` 表访问处理：读取生成 `FlGetGlobal(_S, "flag")`，
赋值侧（L1447 之前）识别未声明左值并生成 `FlSetGlobal(_S, "flag", rhs)`。
需要新增对应运行时函数，改动面大于方案 A。

### 用户侧临时绕过

所有变量先 `local` 声明。fake2d 的 5 个脚本已全部按此修正。

### 验收标准

1. `bug5_implicit_global.lua`：
   - 方案 A：编译期报出指向 `flag` 具体行列的明确错误；
   - 方案 B：三后端（TCC / GCC / 解释器）运行通过，`flag` 写入/读取符合 Lua 全局语义。
2. 已声明变量的脚本行为不回归：fake2d 的 5 个脚本各跑 1200 帧保持零错误（命令见下）。
3. 建议新增回归用例：未声明变量的读（期望 nil）、写、先写后读三个场景。

---

## 已修复缺陷档案（供写回归测试参考）

以下问题在 `ef43eae` 已修复，**不要重复修复**；修复缺陷 5 时注意不要回退这些路径。

### 缺陷 1：多返回值 × 数学参数特化

`local function f(x) return x, x+1 end` 因 `x` 被识别为数学参数，生成 `int64_t f_0(...)`
标量返回的特化体，多返回值分支却无条件生成 `return FlMakeMulti(...)`（CVar）→ 类型冲突。
修复：发现阶段增加返回形态资格检查（`IsEligibleForMathSpec`），多返回值函数只走通用
CVar 变体。

### 缺陷 2：尾参数展开 × 慢路径算术

动态类型操作数的算术被编译为语句宏（`OpAdd(...)` → `do { ... } while (0)`），
而 `CompileCallArgs` 在「前缀先写入输出流 → `CompileExp` 中途发语句」的顺序下把语句宏
拼进了表达式位置。修复：`CompileExp` 先求值存入局部字符串再输出（`c_gen.cpp` L4500–4501；
generic for 的丢弃返回值路径 L2118–2119 一并修）。

### 缺陷 3：`va_start` 锚点为 `bool`（C17 7.16.1.4 UB）

`FlMakeClosure(..., bool is_vararg, ...)` 中 `bool` 受默认实参提升为 `int`，
以其为 `va_start` 锚点行为未定义。修复：锚点形参改为 `int is_vararg`
（`src/compile/c_runtime_header.h`）。

### 缺陷 4：用户函数 C 符号名 `main` 冲突

Lua 函数直接发射为同名 C 符号，函数名为 `main` 时与宿主入口签名冲突。修复：用户函数
C 符号统一加 `flua_fn_` 前缀。

### 回归建议

- 多返回值函数 × 多类型调用点（int / float 实参各一）；
- 尾参数展开 × 慢路径算术/比较/字符串拼接（`+ - * / // % ^ ..` × 多返回解包 /
  原生返回 / 类型退化 local 三类动态值来源）。

---

## 复现与验证方法

### 验证材料（本机）

目录 `/tmp/flua_verify/`：

| 文件 | 用途 |
|---|---|
| `bug5_implicit_global.lua` | 缺陷 5 最小复现脚本 |
| `bug1_multi_spec.lua` / `bug2_tail_arith.lua` / `bug4_main_symbol.lua` | 缺陷 1/2/4 回归用例 |
| `runner_installed.cpp` → `runner_installed` | 链接 `/usr/local` 安装版，禁用 TCC，分别以 GCC JIT / 解释器调用 `main` |
| `recorder.cpp` → `recorder` | 编译脚本并通过 `record_c_code` 保存生成的 C 代码 |

重新构建 runner：

```bash
clang++ -std=c++20 /tmp/flua_verify/runner_installed.cpp \
    -I/usr/local/include -L/usr/local/lib -lfakelua \
    -o /tmp/flua_verify/runner_installed
```

### 运行

```bash
# 缺陷 5：期望当前失败（修复后按方案 A/B 的验收标准判断）
/tmp/flua_verify/runner_installed /tmp/flua_verify/bug5_implicit_global.lua

# 缺陷 1/2/4 回归：期望全部 PASS
/tmp/flua_verify/runner_installed \
    /tmp/flua_verify/bug1_multi_spec.lua \
    /tmp/flua_verify/bug2_tail_arith.lua \
    /tmp/flua_verify/bug4_main_symbol.lua

# fake2d 5 个脚本各 1200 帧（在 /Users/mingming/project/fake2d 下执行）：
for opt in "" "--scene-demo" "--map-demo" "--platformer-demo" "--mario-demo"; do
    ./build/bin/fake2d_hello --headless --frames 1200 $opt
    echo "$opt exit=$?"
done
# 当前基线：5/5 退出码均为 0，日志 0 条 ERROR。
```

---

## 附：非缺陷的行为约定（不要当作 bug 修改）

1. **文件级数值字面量 `local` 是只读常量。**
   `local x = 32` 编译为 `static const`，再赋值报 `cannot reassign file-level constant`；
   以算术式初始化（`local x = 32 + 0`）则为普通 upvalue、可重新赋值——fake2d 脚本统一
   用此写法持有可变标量。无初值的 `local x` 报 `global constant must be initialized`。
2. **TCC 后端在部分主机找不到系统头**（如 `stdarg.h not found`），属 TCC 自身 include
   路径问题；fake2d 默认 GCC，失败回落解释器。
3. **`math.floor` / `math.ceil` 返回 float。**
4. **原生调用签名严格匹配**：int/float 不可互换，注册的形参类型必须与调用实参一致。
5. **GCC JIT 单次编译约 0.3–1 s**；报错指向临时文件 `/var/folders/.../fakelua_jit_*.c`，
   错误信息中的同名 `.gcc.log` 是最有用的诊断材料。
