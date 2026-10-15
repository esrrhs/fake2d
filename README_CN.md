# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License](https://img.shields.io/github/license/esrrhs/fake2d)](LICENSE)

**Fake2D** 是基于 [FakeLua](https://github.com/esrrhs/fakelua) 的现代化轻量 2D 游戏渲染引擎：C++ 宿主掌控窗口、GPU 资源与场景数据；FakeLua 脚本负责编排玩法与实体逻辑，并在每帧边界执行 Arena 线性重置（**零 GC 停顿**）。

> 当前状态：**1.13 — Phase 19 离屏渲染目标（FBO）已完成** — 已包含 1.12 及之前的全部能力（剪裁矩形、九宫格、对齐换行文本、固定步长物理、相机跟随工具、碰撞过滤、瓦片地图 A\* 寻路），并新增**离屏渲染目标**：用 `rt_begin`/`rt_end` 把绘制重定向到 FBO，再用 `rt_draw` 当作精灵贴回，支撑「一次性烘焙」（程序化背景、预渲染文字，任意尺寸复用）与「累积反馈」（运动残影、战争迷雾）两类典型玩法。配套 `--rt-test`（38 条断言）与新的 **`--rt-demo`**：左侧烘焙画布、中间逐帧淡出的拖尾层，右侧放一个即时模式对照区。详见下方[实现计划](#实现计划)、[docs/PLAN.md](docs/PLAN.md)、[脚本编写指南](docs/SCRIPTING.md) 与[跨平台打包说明](docs/PACKAGING.md)。

---

## 现代化设计目标

Fake2D 从设计之初即对齐现代 2D 游戏引擎工业标准（参考 MonoGame、Raylib 与 Defold 等成熟架构）：

1. **零 GC 停顿的每帧运行模型（Zero-GC Per-Frame Architecture）**：
   - 彻底解决传统脚本引擎（如 Love2D、Cocos-Lua、Unity C#）在高频逻辑下由垃圾回收器引发的卡顿（GC Spikes）；
   - 脚本每帧可自由创建临时 table、向量与计算数据，帧末通过 `fakelua::inter::Reset(state)` 瞬间回收整个线性 Arena 内存，保持丝滑的 60/120+ FPS 稳定帧率。
2. **数据驱动的现代流式合批（Data-Oriented SpriteBatching）**：
   - 动态流式写入 VBO，搭配静态共享索引缓冲区（`0-1-2-2-3-0` 规则每 Quad 6 个索引）；
   - **内置 1×1 纯白单例贴图**：纯色矩形、UI 背景卡片与贴图精灵复用完全相同的 GLSL 着色器与合批队列，彻底消除因绘制色块而打断批次的管线切换开销；
   - 多键合批排序（Layer → Depth/Z → 混合模式 → Texture ID），同纹理四边形自动合并为一次绘制；支持 Alpha/加法两种混合模式；内置 `--bench` 基准实测 768 精灵 / 8 纹理下约 674 → 8 次 DrawCall。
3. **彻底解耦的 2D 相机与视口系统**：
   - 独立的 `Camera2D`，支持视口缩放（Zoom）、平移、旋转及 `ScreenToWorld` / `WorldToScreen` 逆矩阵坐标反查，满足像素级 UI 与世界交互需求；
   - 原生 HiDPI / Retina：投影工作在物理帧缓冲像素，游戏坐标保持逻辑点；窗口缩放与 DPI 变化实时跟踪。
4. **数据驱动的场景与资源体系**：
   - 空间节点树层级（`Transform2D`）与局部/全局 Dirty 变换缓存（Phase 2）；
   - 支持图集（Texture Atlas / SpriteSheet），将整套关卡资源合并为单张大图合批绘制；
   - 结合 FakeLua JIT 后端支持脚本热重载，无需重启进程即可即时迭代玩法。
5. **现代 CI 与无头自动化测试（Headless Ready）**：
   - 原生支持 `--headless` 与 `--frames N` 参数，使引擎能够在无物理显示设备的虚拟机、Docker 容器或 GitHub Actions CI 中自动化完成冒烟测试与回归验证；
   - 遵循 Modern CMake 规范构建，采用标准包导出（`find_package(fakelua REQUIRED)`）。

---

## 架构示意

```
┌─────────────────────────────────────────────────────────┐
│                     FakeLua 脚本层                      │
│             update(dt) · 实体管理 · UI 逻辑              │
└────────────────────────────┬────────────────────────────┘
                             │ Native 原生绑定 (draw_quad, camera_*, ...)
┌────────────────────────────▼────────────────────────────┐
│                      Fake2D Engine                      │
│       ScriptHost · 场景图 · 输入快照 · 资源管理器       │
├────────────────────────────┬────────────────────────────┤
│        SpriteBatch         │         Camera2D           │
│   (动态 VBO / Quad 流)     │ (正交 VP 矩阵 / 坐标反查变换)
├────────────────────────────┴────────────────────────────┤
│         Texture2D          │          Shader            │
│    (stb_image / 1x1 白图)  │     (GLSL 330 Core)        │
├────────────────────────────┴────────────────────────────┤
│              Renderer（OpenGL 3.3 Core）                │
│              Platform Window（GLFW 3.4）                │
└─────────────────────────────────────────────────────────┘
```

---

## 目录结构

```
fake2d/
├── CMakeLists.txt           # Modern CMake 工程配置
├── include/fake2d/          # 对外公共头文件
│   ├── atlas.h              # TexturePacker JSON 图集
│   ├── audio.h              # miniaudio 一次性音效播放器
│   ├── camera.h             # 2D 正交相机
│   ├── engine.h             # 引擎宿主与主循环
│   ├── font.h               # TTF / 8x8 位图字形图集
│   ├── input.h              # 每帧键盘/鼠标输入快照
│   ├── math.h               # Vec2, Rect, Color, Mat4
│   ├── node.h               # Transform2D 与场景图节点
│   ├── particle.h           # CPU 粒子池与发射器
│   ├── renderer.h           # 渲染器门面
│   ├── resource_manager.h   # 句柄式资源池
│   ├── scene.h              # 图层/Z 序场景渲染
│   ├── shader.h             # 着色器管线与 Uniform 缓存
│   ├── sprite_batch.h       # 高性能 SpriteBatch
│   ├── texture.h            # 纹理与 1x1 白图单例
│   └── version.h            # 版本定义
├── src/
│   ├── audio/               # miniaudio 设备与语音混音
│   ├── core/                # 引擎循环、生命周期与资源池
│   ├── platform/            # GLFW 窗口与 GL 上下文管理
│   ├── render/              # OpenGL 3.3 Core 渲染、字体、粒子
│   ├── scene/               # 节点层级与场景渲染
│   └── script/              # FakeLua 桥接与原生 API 注册
├── third_party/             # stb_image、stb_truetype、stb_rect_pack、miniaudio、font8x8
├── scripts/                 # 纯 Lua 示例：game.lua（打砖块）、scene_demo.lua、map_demo.lua、platformer_demo.lua、mario.lua、path_demo.lua
├── examples/hello/          # 示例：打砖块、--scene-demo、--map-demo、--platformer-demo、--mario-demo、--path-demo、--rt-demo、--bench、--phys-bench
├── docs/SCRIPTING.md        # 脚本编写指南与 API 参考
├── docs/PACKAGING.md        # Linux / Windows / macOS 跨平台打包说明
└── docs/PLAN.md             # 详细路线图与任务清单
```

---

## 环境要求

- CMake ≥ 3.16，C++23 编译器（GCC 13+, Clang 16+, Apple Clang 15+）
- OpenGL 3.3+ Core Profile / GLFW 3.4（由 CPM 自动拉取）
- [FakeLua](https://github.com/esrrhs/fakelua)（已安装至标准路径 `/usr/local` 或由 CMake `find_package(fakelua)` 发现）

---

## 编译运行

```bash
# 标准编译（链接 FakeLua 脚本引擎）：
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/fake2d_hello

# 运行无头模式冒烟测试（适用于 CI 或无显示设备环境）：
./build/bin/fake2d_hello --headless --frames 60

# 无头模式导出帧缓冲 PNG（视觉回归测试）：
./build/bin/fake2d_hello --headless --frames 460 --screenshot game.png
./build/bin/fake2d_hello --headless --frames 40 --scene-demo --screenshot scene.png

# DrawCall / 合批刷新耗时基准（立即模式 vs 排序合批）：
./build/bin/fake2d_hello --headless --bench

# 运行中编辑 scripts/game.lua，保存后引擎自动重新编译（热重载）：
./build/bin/fake2d_hello --hot-reload

# 所有 demo 均为纯 Lua 脚本（二进制只负责生成 PNG/JSON/WAV 素材），
# 每个参数对应一个入口脚本：
./build/bin/fake2d_hello --scene-demo      # 精灵、旋转、粒子、文字

# Tiled 瓦片地图 + 内置物理 + UI（按 R 或点击 RESET 重置）：
./build/bin/fake2d_hello --map-demo

# 滚动平台跳跃关卡（A/D 移动、空格跳跃；无头模式自动演示）：
./build/bin/fake2d_hello --platformer-demo

# 经典超级马里奥兄弟 1-1 关卡（A/D 移动、Shift 加速、空格跳跃；无头模式自动演示）：
./build/bin/fake2d_hello --mario-demo

# 迷宫追逐：A* 追踪 AI + 玩家自动寻路演示（P 切换路径线）：
./build/bin/fake2d_hello --path-demo

# 离屏渲染目标：一次性烘焙画布 + 逐帧淡出的拖尾层（SPACE/C/R）：
./build/bin/fake2d_hello --rt-demo

# 物理宽相基准（暴力 O(n²) vs 空间哈希网格）：
./build/bin/fake2d_hello --headless --phys-bench

# 物理自测（纯 CPU，退出码 + 断言汇总）：
./build/bin/fake2d_hello --phys-query-test    # 射线 / 重叠 / 接触生命周期
./build/bin/fake2d_hello --phys-filter-test   # category 与 mask 碰撞过滤
./build/bin/fake2d_hello --timestep-test      # 固定步长累加器 + 相机工具

# 物理默认固定 60 Hz 步进；可调整频率或回退可变步长：
./build/bin/fake2d_hello --fixed-hz 120 --mario-demo
./build/bin/fake2d_hello --variable-step --mario-demo
```

仅窗口骨架模式（不链接 FakeLua，用于纯 C++ 基础测试）：

```bash
cmake -S . -B build -DFAKE2D_WITH_FAKELUA=OFF
cmake --build build --parallel
```

---

## 快速脚本

`scripts/game.lua`（默认入口）是一个完全由 Lua 驱动的可玩打砖块小游戏——
精灵、输入、物理，约 200 行：

```lua
local W = 960
local paddle_x = W * 0.5          -- 持久状态保存在文件级 local 中
local score = 0 + 0               -- 可变数值必须用表达式初始化
                                  -- （详见 docs/SCRIPTING.md 的状态规则）

function update(dt)
    -- 挡板跟随鼠标 / 方向键
    if input_key_down("left") then paddle_x = paddle_x - 620 * dt end

    -- 推进球、碰撞砖块、绘制所有元素
    step_ball(dt)
    draw()
    return 0
end
```

完整的 API 参考、持久状态规则（FakeLua Arena 与 JIT codegen 约束）以及性能
最佳实践见[脚本编写指南](docs/SCRIPTING.md)。

---

## 实现计划

| 阶段 | 目标 | 交付物 | 状态 |
|------|------|--------|:----:|
| **0 — 骨架** | 可启动宿主 | GLFW 窗口、GL 清屏、FakeLua 桥接、hello 示例、双语文档、CLI 无头测试参数 | **已完成** |
| **1 — 绘制图元** | 第一批像素 | 正交相机、色块四边形、`SpriteBatch` 动态合批、1x1 白图优化、stb_image 贴图加载 | **已完成** |
| **2 — 场景与资源** | 结构化支撑 | `Transform2D` 节点层级、图层与 Z 序、图集（SpriteSheet）、资源缓存、GCC JIT 脚本热更 | **已完成** |
| **3 — 脚本 API** | 脚本制作游戏 | 封装 `sprite`、`camera`、`input`、`time` 等原生模块；每帧输入快照；可玩打砖块示例；[脚本编写指南](docs/SCRIPTING.md) | **已完成** |
| **4 — 文字与音频** | 表现力增强 | TTF 位图字体（8x8 兜底）合批渲染、miniaudio 一次性音效、零分配 CPU 粒子系统、帧缓冲截图 | **已完成** |
| **5 — 打磨交付 (1.0)** | 商业可交付质量 | 多键排序合批 + 加法混合、HiDPI/Retina、`--bench` 基准、[跨平台打包说明](docs/PACKAGING.md)、标签触发的发布自动化 | **已完成** |
| **6 — 玩法基础 (1.1)** | 游戏系统 | 内置 AABB/圆形物理与传感器接触事件、精灵帧动画、Tiled JSON 瓦片地图（含固体碰撞体导出）、锚点 UI 面板/标签/按钮；`--map-demo` | **已完成** |
| **7 — 平台跳跃与规模 (1.2)** | 控制器与性能 | 空间哈希宽相（`--phys-bench` 约 6 倍）、线条/调试图元、Trauma 相机震动、WAV 与循环音乐、瓦片平台跳跃控制器（土狼/缓冲/单向平台）+ `--platformer-demo` | **已完成** |
| **8 — 引擎短板与马里奥 (1.3)** | 完整度 | 瓦片斜坡（`map_ground_y`）、Tiled 视差与图片层、`SaveStore` 存档（`storage_*`）、`EntityStore` 原生实体槽位（`ent_*`）、自定义 Shader 槽位（`shader_*`）、SMB 1-1 `--mario-demo` | **已完成** |
| **9 — 手柄 / 镜像 / 窗口 / 角色动画 (1.4)** | 可玩性 | 精灵 UV 镜像（`sprite_draw_flip`）、GLFW 标准映射手柄（`input_pad_*`，死区/扳机重映射/热插拔）、`window_quit` / 标题 / 全屏；程序化派生的待机/迈腿/跳跃精灵表与栗宝宝摇摆帧，三个玩法 Demo 朝向修正、走路动画与手柄支持 | **已完成** |
| **10 — 角色动画纳入引擎动画系统 (1.5)** | 引擎复用 | 脚本控帧 `anim_pause` / `anim_resume` / `anim_set_frame` / `anim_frame_index`、翻转绘制 `anim_draw_flip`；马里奥（大小形态 + 每只栗宝宝一个摇摆 clip）与平台/斜坡角色从手算帧矩形迁移到引擎持有的动画 clip | **已完成** |
| **11 — 自带依赖的发布流水线 (1.5)** | 分发 | `tools/package-{macos,linux,windows}.sh` 产出免编译压缩包（内置 libfakelua / OpenSSL、`scripts/`、着色器与示例地图）；Linux / Windows 打包脚本待首个 tag 端到端验证 | **已完成** |
| **12 — GL2 兼容渲染路径 (1.6)** | 覆盖面 | 运行时 GL 上下文阶梯（core 3.3 → legacy 2.1 → macOS 无头 CGL + 离屏 FBO）、GLSL 120 着色器变体、`GL_LUMINANCE` 上传、NPOT clamp；macOS GPU 冒烟跑在 Apple 软件渲染器上 | **已完成** |
| **13 — 物理查询能力 (1.7)** | 玩法查询 | `RayCast`（圆 + AABB、法线、起点在内部）、`OverlapCircle` / `OverlapBox`、完整 began/stayed/ended 接触生命周期、17 个 Lua 绑定、`--phys-query-test` | **已完成** |
| **14 — Tiled 对象层 (1.8)** | 数据化关卡 | `objectgroup` 解析（矩形/点/椭圆/折线/多边形 + 自定义属性）、`map_find_layer` / `map_layer_type` / `map_obj_*` 查询；马里奥刷怪从标记瓦片迁移到对象层 | **已完成** |
| **15 — 碰撞过滤 (1.9)** | 碰撞控制 | 每个刚体的 16 位 `category` / `mask`，宽相阶段即过滤；带掩码的射线与重叠查询；`phys_create_*_cat` / `phys_set_category` / `phys_set_mask` / `phys_*_cat` 绑定；`--phys-filter-test` | **已完成** |
| **16 — 固定步长与相机工具 (1.10)** | 确定性与相机 | 固定步长物理累加器（与帧率无关、零步长接触刷新、`phys_smooth_*` 渲染插值、`time_sim_dt/steps/alpha`），Camera2D 跟随工具（边界钳制 / 死区 / 平滑 / 只进不退滚动锁 / snap）共 8 个 Lua 绑定；马里奥、斜坡与平台 Demo 全部迁移其上；`--timestep-test` | **已完成** |
| **17 — 裁剪、九宫格与文本排版 (1.11)** | 表现力 | `draw_set_clip` 剪裁矩形（精确 flush 边界语义）、`draw_nine_slice` 拉伸面板、`draw_text_ex` 对齐换行文本与 `end_y` 段落堆叠；FBO 渲染目标与更多 UI 控件顺延 | **已完成** |
| **18 — 瓦片地图 A* 与迷宫追逐 (1.12)** | 导航 | 基于 solidity 网格的 A*（防切角、路径拉直、`map_find_path` + 路径点读取），`--path-test`（25 条断言），`--path-demo` 迷宫追逐：错峰重寻路的追踪 AI、玩家自动寻路演示模式、实时路径线 | **已完成** |
| **19 — 离屏渲染目标 (1.13)** | 渲染 | `RenderTarget` FBO 资源池 + `rt_begin`/`rt_end`/`rt_clear`/`rt_draw` 绑定（一次性烘焙、累积反馈特效），画布坐标系与 V 方向校正、无头 framebuffer 正确恢复，`--rt-test`（38 条断言）、`--rt-demo`；顺带修复每帧 35 次 `GLFW_INVALID_VALUE` 按键轮询报错 | **已完成** |

详细任务清单见 [docs/PLAN.md](docs/PLAN.md)。

---

## 许可证

MIT — 详见 [LICENSE](LICENSE)。
