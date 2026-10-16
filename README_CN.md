# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License](https://img.shields.io/github/license/esrrhs/fake2d)](LICENSE)

**Fake2D** 是基于 [FakeLua](https://github.com/esrrhs/fakelua) 的现代化轻量 2D 游戏渲染引擎：C++ 宿主掌控窗口、GPU 资源与场景数据；FakeLua 脚本负责编排玩法与实体逻辑，并在每帧边界执行 Arena 线性重置（**零 GC 停顿**）。

> 当前状态：**1.24 — Phase 30 动画瓦片已完成** — 已包含 1.23 及之前的全部能力（剪裁矩形、九宫格、对齐换行文本、固定步长物理、相机跟随工具、碰撞过滤、瓦片地图 A\* 寻路、离屏渲染目标、全屏后处理 + 小地图烘焙、音频混音器、位置音频、ping-pong 趟、自动 uniform、UI 控件第二批、战争迷雾、bloom 级 FX 套件、UI 控件第三批、解码文件与流式音乐），并新增**动画瓦片**：解析 Tiled tileset 的 `animation` 循环，由引擎每帧推进一个统一时钟（每个已加载地图自动走表，脚本零簿记），按瓦片 *id* 动画——使用同一瓦片的所有格子同步播放，配套只读查询（`map_anim_time/_count/_cycle/_index/_gid/_reset`），且只有「画面」随钟走——`map_solid` 与整个碰撞网格始终读基础 gid。`--tile-test` 新增为第十二项引擎自测（55 条）；**`--tile-demo`** 演示池塘涟漪与火把闪烁。详见下方[实现计划](#实现计划)、[docs/PLAN.md](docs/PLAN.md)、[脚本编写指南](docs/SCRIPTING.md) 与[跨平台打包说明](docs/PACKAGING.md)。

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
├── scripts/                 # 纯 Lua 示例：game.lua（打砖块）、scene_demo.lua、map_demo.lua、platformer_demo.lua、mario.lua、path_demo.lua、rt_demo.lua、ui_demo.lua、post_demo.lua
├── examples/hello/          # 示例：打砖块、--scene-demo、--map-demo、--platformer-demo、--mario-demo、--path-demo、--rt-demo、--ui-demo、--post-demo、--bench、--phys-bench
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

# UI 控件：滑杆、复选框、文本输入框、滚动列表：
./build/bin/fake2d_hello --ui-demo

# 后处理：世界画进目标后经着色器呈现 + 烘焙小地图（SPACE/G）：
./build/bin/fake2d_hello --post-demo

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
| **20 — UI 控件 (1.14)** | UI | 滑杆（拖动或点击跳转、保持抓取偏移）、复选框（调用方持有数值）、UTF-8 文本输入（按码点增删、按字符数限长、焦点管理、`Input` 字符回调）、滚动容器（滚轮 + 可拖动滑块、调用方负责剪裁内容），新增 8 个绑定，`--ui-test`（30 条断言）、`--ui-demo` | **已完成** |
| **21 — 后处理与小地图 (1.15)** | 渲染 | `Renderer::ActiveCamera`（RT 内 tilemap 跟随目标画布相机）、`rt_camera`（把画布对准世界偏移）、`rt_present`（经着色器覆盖的全屏 blit）、`map_draw_scaled`（无视差小地图烘焙，抽出的 `DrawTileLayer` 共享 helper）、`assets/post_grade.*` 默认恒等的调色着色器、`--post-test`（44 条像素探针，经变异验证）、`--post-demo`；顺带修复像素边界扫描发现的 `--ui-demo` 页脚画出画布问题 | **已完成** |
| **22 — 音频混音器 (1.16)** | 音频 | 混音器与设备解耦（无设备也能注册 clip 并直接跑 `Mix()`）、三条总线（`voice × bus × master`，sfx/music 隔离）、每声道增益包络（`PlayMusic` 交叉淡入淡出、`StopMusic` 淡出、`FadeMusicTo` 斜坡）、冻结混音的 `SetPaused`、`Peak()` VU 数据源、公开 `Mix()` 供离线渲染，新增 10 个绑定（`audio_bus_*`、`audio_pause*`、`audio_music_*`、`audio_peak`），`--audio-test`（51 条直接驱动 `Mix()` 的无头断言）、`--audio-demo` 调音台面板 | **已完成** |
| **23 — 位置音频 (1.17)** | 音频 | `PlayAt` 把一次性音效放进世界（`atten` 独立于包络，衰减不动淡入淡出）、向 `SetListenerRange` 线性衰减到静音（默认 1000，≤ 0 拒绝）、声像半程即达全分离并采用显式文档化的线性交叉规律、`SetListener` 重新评估正在发声的 voice、`ResetVoice` 清空复用槽位、`PeakLeft`/`PeakRight` 逐声道 VU 数据源，新增 5 个绑定（`audio_play_at/listener/range/peak_l/peak_r`），`--audio-test` 扩到 74 条离线断言，`--spatial-demo`（可移动听者 + 范围环 + L/R 电平表） | **已完成** |
| **24 — 多趟后处理链 (1.18)** | 渲染 | `rt_copy(src, dst, shader)`（一趟 ping-pong；自拷贝/窗口/嵌套提前拒绝）、`rt_resize`（半分辨率 pass；先校验尺寸再 `Create`，拒绝 resize 不会销毁目标）、`assets/fx_blur.*` 可分离 9 抽头高斯（`u_dir = 0` 即恒等，`u_dim` 合成增益）、2 个新绑定、`--fx-test`（47 条像素探针：恒等 pass、单轴拖影、双趟覆盖、守卫、缩放拷贝、链上叠加 grade、窗口存活）、`--fx-demo` 程序化天际线 bloom + 分屏模式；记录"带着色器的 present 会重置混合模式"的坑 | **已完成** |
| **25 — UI 控件第二批 (1.19)** | UI | `ProgressBar`（`ui_bar`，钳制的纯绘制）、`RadioButton`（`ui_radio`，按钮式点击语义、选择权归调用方）、`ComboBegin`/`ComboItem`（`ui_combo`/`ui_combo_item`，组合式下拉，表头容忍甩出点击）、`Input::InjectFrame` + 可变 `Engine::GetInput()`（合成输入帧解锁无头点按路径断言）、4 个新绑定、`--ui-test` 扩到 52 条断言（4 次变异验证——变异暴露了轨道内空转的钳制探针）、`--ui2-demo` | **已完成** |
| **26 — 战争迷雾 (1.20)** | 渲染 | `BlendMode::Multiply`（`draw_set_blend(2)`，`DST_COLOR, ZERO`——遮罩在已绑定目标内部合成，`rt_copy` 因拒绝嵌套做不到；源 RGB 驱动结果、alpha 不参与门控）、`rt_sample(id, x, y)`（像素查询读回标量，非法读取返回 -1，故 0 恒为真黑）、1 个新绑定、`--fog-test`（39 条像素探针：恒等/擦除/逐通道/半遮罩、目标内合成 vs 嵌套拒绝、逐绘制混合状态、跨帧累积、越界守卫）、`--fog-demo` 程序化战争迷雾世界：累积式探索、烘焙小地图 × 遮罩、点击跳转、`rt_sample` 驱动的探索度读数 | **已完成** |
| **27 — 动画 pass (1.21)** | 渲染 | 渲染器自动灌入的 uniform：`u_time`（帧时钟，脚本零簿记）与 `u_resolution`（正在绘制的表面，像素参数与分辨率无关）、`assets/fx_bright.*`（bloom 阈值 + 软膝，0/0 即恒等）、`assets/fx_wave.*`（行波扭动；`u_amp` 以像素计，符号已记录——采样点右移画面左移）、`--fx-test` 扩到 80 条（恒等/硬切/软膝、±8 px 四分之一周期、128 与 64 同位移）、`--fx-demo` 链路扩为 `场景 → wave → bright → blur → 合成`，带开关与滑杆 | **已完成** |
| **28 — UI 控件第三批 (1.22)** | UI | `ui_window`：标题栏拖拽面板（返回「正在被拖拽」，位置由脚本经 `input_mouse_dx/_dy` 自持；抓取帧静默不跳变；按在面板主体上不抓取；按住不放拖出标题栏不丢），`ui_tab`：激活页由调用方自持，更亮、长高 2 px 并去掉下边缘，`ui_tree`：展开行（三个 quad 画的右/下三角），子项就是 if 块，`--ui-test` 扩到 76 条（拖拽路径、点击/取消语义、bar/body/分隔线像素探针、激活页更亮且下探 2 px），`--ui3-demo`：可拖拽 SETTINGS 窗口，三标签页 + 可折叠树 + `ui_hovered` 工具提示 | **已完成** |
| **29 — 解码文件与流式音乐 (1.23)** | 音频 | `audio_load_file`：miniaudio 解码器支持的任意文件（WAV/FLAC/MP3/OGG-Vorbis）混到单声道、重采样到混音器采样率并注册为 clip，`audio_stream`：同样的文件按 8192 帧窗口按需回填播放——三分钟曲目只占几 KB 而非几 MB——顺序回填避开会重置重采样器的可闻 seek，`audio_clip_len` / `audio_clip_cached` 报告时长与驻留帧数，`--audio-test` 扩到 106 条（流与解码逐块对比、拒绝路径、越尾循环、暂停/恢复/停止），`--audio-demo` 的 FILE PLAYBACK 面板带缓冲对比读数 | **已完成** |
| **30 — 动画瓦片 (1.24)** | 渲染 | 解析 Tiled tileset 的 `animation` 循环（坏帧丢弃整段动画而不是播错），引擎每帧推进「每瓦片 id 一个时钟」——水与火把零簿记自动播放，使用同一瓦片的格子全部同步；只有绘制画面随钟走，`map_solid` 与碰撞网格始终读基础 gid；只读查询 `map_anim_time/_count/_cycle/_index/_gid` + `map_anim_reset`（索引沿时间线走，0,1,0,1 闪烁被如实报告）；`--tile-test` 新增为第十二项引擎自测、55 条断言（5 个变异全被捕获）；`--tile-demo`：池塘涟漪、火把闪烁、由查询构建的 HUD 与 RESET CLOCK 按钮 | **已完成** |

详细任务清单见 [docs/PLAN.md](docs/PLAN.md)。

---

## 许可证

MIT — 详见 [LICENSE](LICENSE)。
