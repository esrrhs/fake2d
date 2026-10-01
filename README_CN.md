# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License](https://img.shields.io/github/license/esrrhs/fake2d)](LICENSE)

**Fake2D** 是基于 [FakeLua](https://github.com/esrrhs/fakelua) 的现代化轻量 2D 游戏渲染引擎：C++ 宿主掌控窗口、GPU 资源与场景数据；FakeLua 脚本负责编排玩法与实体逻辑，并在每帧边界执行 Arena 线性重置（**零 GC 停顿**）。

> 当前状态：**1.2 — Phase 7 已完成** — 在 1.1（渲染、音频、粒子、物理、动画、瓦片、UI）基础上，新增物理空间哈希宽相及基准、GL 线条/调试图元、Trauma 相机震动、WAV 解码与循环背景音乐，以及含土狼时间/跳跃缓冲/单向平台的瓦片平台跳跃角色控制器，并有完整滚动关卡 `--platformer-demo`。详见下方[实现计划](#实现计划)、[docs/PLAN.md](docs/PLAN.md)、[脚本编写指南](docs/SCRIPTING.md) 与[跨平台打包说明](docs/PACKAGING.md)。

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
├── scripts/                 # 示例脚本（game.lua = 打砖块，main.lua = 场景演示）
├── examples/hello/          # 示例：打砖块、--scene-demo、--map-demo、--platformer-demo、--bench、--phys-bench
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

# 以 C++ 场景图 / 图集演示替代 Lua 小游戏：
./build/bin/fake2d_hello --scene-demo

# Phase 6：Tiled 瓦片地图 + 内置物理 + UI（按 R 或点击 RESET 重置）：
./build/bin/fake2d_hello --map-demo

# Phase 7：滚动平台跳跃关卡（A/D 移动、空格跳跃；无头模式自动演示）：
./build/bin/fake2d_hello --platformer-demo

# 物理宽相基准（暴力 O(n²) vs 空间哈希网格）：
./build/bin/fake2d_hello --headless --phys-bench
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

详细任务清单见 [docs/PLAN.md](docs/PLAN.md)。

---

## 许可证

MIT — 详见 [LICENSE](LICENSE)。
