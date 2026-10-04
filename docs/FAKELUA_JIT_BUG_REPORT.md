# FakeLua 缺陷修复文档

| 项目 | 内容 |
|---|---|
| 组件 | FakeLua — https://github.com/esrrhs/fakelua |
| 当前版本 | 2.0.0，本机源码 `/Users/mingming/project/fakelua` @ `a55a4bf`（`master`，已与 `origin/master` 同步） |
| 已安装 | `/usr/local/lib/libfakelua.2.0.0.dylib`（2026-10-04 20:05 由 `a55a4bf` 构建安装） |
| 平台 | macOS / arm64，Apple Clang；后端：TCC JIT、GCC JIT、解释器 |
| 文档日期 | 2026-10-02；2026-10-04 复测 + 升级到 `a55a4bf` |

## 结论

历史上共记录 5 个缺陷。**截至 2026-10-04，5 个缺陷全部已在上游修复并在本机实测通过，
本文档转为纯回归基线，无待修复项。**

| # | 缺陷 | 状态 | 修复提交 |
|---|---|---|---|
| 1 | 多返回值函数被参数类型特化后生成非法 C | 已修复 | `236a757` ＋ `ef43eae` |
| 2 | 尾参数展开把语句宏拼进表达式位置 | 已修复 | `ef43eae` |
| 3 | `FlMakeClosure` 用 `bool` 充当 `va_start` 锚点（C17 UB） | 已修复 | `236a757` |
| 4 | Lua 函数名 `main` 与宿主 C `main` 符号冲突 | 已修复 | `236a757` |
| 5 | 未声明的全局变量被误编译为对常量 `kNil` 的读写 | **已修复（2026-10-04 复测确认）** | `b83f574`（方案 A） |

> 缺陷 5 的修复提交 `b83f574 fix: reject undeclared variables at semantic analysis (bug5) (#23)`
> 已随 2026-10-04 的 `master` 升级一并安装。下方「缺陷 5 根因 / 修复方案」小节保留为历史记录，
> **不要按其中的方案 A/B 再改一遍**。

---

## 版本沿革与升级记录

| 日期 | 提交 | 内容 | 状态 |
|---|---|---|---|
| — | `ef43eae` | 缺陷 1/2 修复基线 | 曾安装 |
| 2026-10-03 | `b83f574` (#23) | 缺陷 5 修复（方案 A：语义分析拒绝未声明变量） | 曾安装 |
| 2026-10-04 | `a55a4bf` (#24) | P1-5b Lua 5.4 标准模式匹配；P1-9 while 条件重求值 | **当前安装** |

### 升级到 `a55a4bf` 新增的两项能力

1. **P1-5b：Lua 5.4 标准模式匹配**。`src/native/string/lua_pattern.{h,cpp}` 自带字节级匹配器
   （移植自 Lua 5.4 `lstrlib.c`），**替换了原先的 `boost::regex` ECMAScript 引擎**。
   `string.find/match/gmatch/gsub` 现支持 `%` 转义、字符类（`%a %d %w %s %u %l` 等）、
   自定义集合与范围（`[%a-z]`）、量词（`* + - ?`）、捕获 `()`、反向引用 `%1-%9`、
   平衡匹配 `%b()`、前向断言 `%f[set]`，以及 gsub 替换模板（`%0-%9`、`%%`、函数/表分派）。
   ⚠️ 语义与旧 ECMAScript 引擎不同（集合/转义规则更严格），升级后如有脚本依赖旧宽松行为需复查。
2. **P1-9：while 条件重求值**。`IsPureNativeNumericExp` 阻止把非纯数值表达式
   （如 `#t` 这类需发射 `FlLenInt` 语句的运算）提升为单次求值的原生快路径 `while`；
   纯数值条件（字面量、局部变量、纯二元运算）仍走快路径。

   实测：`while #t > 0 do t[#t] = nil; n = n + 1 end` 正确迭代 3 次
   （`newfeat_while.lua`；修复前会被提升为单次求值，只跑 1 次）。

### 升级操作与验证（2026-10-04 实测）

```bash
cd /Users/mingming/project/fakelua
git checkout master && git merge --ff-only origin/master   # b83f574 → a55a4bf
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
         -DFAKELUA_BUILD_TESTS=OFF -DFAKELUA_BUILD_BENCHMARKS=OFF
make -j"$(sysctl -n hw.ncpu)" && make install
```

⚠️ 两个必须注意的点：

- **旧库先备份**：`cp /usr/local/lib/libfakelua.2.0.0.dylib /tmp/fakelua_backup/`。
  2026-10-04 备份位于 `/tmp/fakelua_backup/libfakelua.2.0.0.dylib.bak-20261004-200538`
  （旧库 sha256 `69c63cd4…`，新库 `acf90432…`）。
- **重装 dylib 后必须重新链接测试 runner**。`/tmp/flua_verify/runner_installed` 是静态链接
  旧 dylib 的产物，不重新 `clang++` 就会测到旧库而误判（2026-10-04 首次复测即踩此坑）。

本次升级后实测全绿：新特性 2 项 PASS、旧缺陷 1/2/4/5 回归 PASS、fake2d 5 脚本 × 1200 帧
5/5 `exit=0` 且 0 条 `[ERROR]`。

---

## 已修复：缺陷 5 — 未声明变量被编译为 `kNil` 读写（2026-10-04 复测确认）

> 本节为历史档案。上游选择了**方案 A**（语义分析阶段拒绝未声明变量）并已合入 `b83f574`。
> 下文的现象、根因、方案对比仅供理解，**修复工作已结束**。

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

### 验收结果（2026-10-04 实测，方案 A 全部满足）

| 场景 | 用例 | 实测结果 |
|---|---|---|
| 写未声明全局 | `bug5_implicit_global.lua` | ❌ 预期拒绝 → `undeclared variable 'flag' on the left side of assignment … at bug5_implicit_global.lua:3:20` |
| 只读未声明全局 | `b5_read_only.lua` | ❌ 预期拒绝 → `unknown variable 'readonly_global' … at b5_read_only.lua:3:26` |
| 先写后读 | `b5_read_after_write.lua` | ❌ 预期拒绝 → 指向首次赋值 `at b5_read_after_write.lua:2:5` |
| 文件级裸赋值 | `b5_filelevel.lua` | ❌ 预期拒绝 → `unsupported file-level statement Assign … at b5_filelevel.lua:1:8` |
| 已 `local` 声明后赋值 | `b5_ok_declared.lua` | ✅ PASS（GCC + 解释器） |
| fake2d 5 个脚本 × 1200 帧 | `fake2d_hello --headless` | ✅ 5/5 `exit=0`，0 条 `[ERROR]` |

GCC JIT 与解释器两条路径表现一致（预编译在加载阶段发生，故后端不影响判定）。
`scripts/*.lua` 无需再改——fake2d 早已全部显式 `local` 声明。

---

## 工作区绕过的还原（2026-10-04）

fakelua 修好之后，fake2d 里为绕开缺陷 1/2 而写的代码就可以还原了。逐项实测后**只还原了
`scripts/platformer_demo.lua` 的 3 处**，其余写法属于 FakeLua 2.0 的**现行语义**（不是 bug），
必须保留 —— 完整分类见 [SCRIPTING.md](SCRIPTING.md) 的 "Not a bug" 与 "Reverted" 小节。

### 已还原（确认不再需要）

| 位置 | 原绕过写法 | 还原为 | 依据 |
|---|---|---|---|
| `platformer_demo.lua:270` | `build_intent()` 无参 + 函数内 `local frame = time_frame()`，注释称「typed-arg 特化多返回值会 codegen 失败」 | `build_intent(frame)`，调用点 `build_intent(time_frame())` | 缺陷 1 已由 `598632f` 修（返回形态资格检查） |
| `platformer_demo.lua:355` | 提升 `shake_amt` 局部量再 `camera_shake(shake_amt)` | `camera_shake(math.min(0.5, impact / 1200.0))` | 缺陷 2 已由 `ef43eae` 修（`CompileExp` 先求值入局部再输出） |
| `platformer_demo.lua:278` | 预计算 `up_row` / `down_row` 局部量 | 直接写 `map_solid(map_id, ahead, feet_row - 1)` 等 | 同上 |

验证：`--platformer-demo` 在 frames=1 / 100 / 1200 三档下均 `exit=0`、0 条 `[ERROR]`；
其余 4 个脚本 1200 帧同样全绿。游戏逻辑等价（`frame` 仍驱动 AI 跳跃节拍 `frame % 80 == 0`）。

> 还原时的一个教训：`build_intent` 里的 `frame` **确实被使用**（函数体内
> `frame % 80 == 0` 控制 attract AI 跳跃），不能因为看着像残留就删掉。
> 中途尝试过「彻底删参数」，结果 `unknown variable 'frame'` 直接编译失败 ——
> 这正是缺陷 5 修复后的正常报错。正确做法是恢复传参。

### 必须保留（不是 bug，是现行语义）

| 写法 | 出现量 | 实测行为 |
|---|---|---|
| 可变数值用 `0 + 0` 表达式初始化 | 60 处 / 5 脚本 | `local x = 0` 后赋值 → `cannot reassign file-level constant 'x'` |
| 持久状态用扁平数值而非表 | 全面 | 空表 `{}` 后**字段**赋值 → `attempt to modify a const table` |
| 文件级 `local` 必须有初值 | — | 无初值 → `global constant must be initialized` |
| 全部变量显式 `local` | 全部脚本 | 未声明 → `unknown variable 'x'; fakelua has no implicit globals` |

⚠️ 一个文档与实现的偏差（已在 SCRIPTING.md 记录）：「每帧表是免费的」这条**只对部分形态成立**。
`local t = {}` 后做**下标写入**（`t[i] = v`）或**原生写入**（`table.insert`）是 mutable 的，
但后做**字段写入**（`t.hp = 10`）即使在 `update` 内部也会被判 const —— 这是类型推断的产物。
`scripts/game.lua:build_powers()` 用的正是 `t[i] = v` 形态，正确，不要「修正」成字段写入。

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
