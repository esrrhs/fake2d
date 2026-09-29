# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License](https://img.shields.io/github/license/esrrhs/fake2d)](LICENSE)

**Fake2D** 是基于 [FakeLua](https://github.com/esrrhs/fakelua) 的现代化轻量 2D 游戏渲染引擎：C++ 宿主掌控窗口、GPU 资源与场景数据；FakeLua 脚本负责编排玩法与实体逻辑，并在每帧边界执行 Arena 线性重置（**零 GC 停顿**）。

> 当前状态：**Phase 3 已完成** — 可启动宿主、OpenGL 3.3 Core 可编程着色器管线、2D 正交相机、动态流式 `SpriteBatch`、`Transform2D` 场景图（图层与 Z 排序）、TexturePacker JSON 图集解析、句柄式资源管理器、脚本热重载，以及原生脚本 API（`sprite` / `camera` / `input` / `time`）与可玩的打砖块示例。详见下方[实现计划](#实现计划)、[docs/PLAN.md](docs/PLAN.md) 与[脚本编写指南](docs/SCRIPTING.md)。

---

## 现代化设计目标

Fake2D 从设计之初即对齐现代 2D 游戏引擎工业标准（参考 MonoGame、Raylib 与 Defold 等成熟架构）：

1. **零 GC 停顿的每帧运行模型（Zero-GC Per-Frame Architecture）**：
   - 彻底解决传统脚本引擎（如 Love2D、Cocos-Lua、Unity C#）在高频逻辑下由垃圾回收器引发的卡顿（GC Spikes）；
   - 脚本每帧可自由创建临时 table、向量与计算数据，帧末通过 `fakelua::inter::Reset(state)` 瞬间回收整个线性 Arena 内存，保持丝滑的 60/120+ FPS 稳定帧率。
2. **数据驱动的现代流式合批（Data-Oriented SpriteBatching）**：
   - 动态流式写入 VBO，搭配静态共享索引缓冲区（`0-1-2-2-3-0` 规则每 Quad 6 个索引）；
   - **内置 1×1 纯白单例贴图**：纯色矩形、UI 背景卡片与贴图精灵复用完全相同的 GLSL 着色器与合批队列，彻底消除因绘制色块而打断批次的管线切换开销；
   - 引入多键合批排序（Layer → Depth/Z → Texture ID → Blend Mode），将全屏绘制调用压缩至极致（Phase 5）。
3. **彻底解耦的 2D 相机与视口系统**：
   - 独立的 `Camera2D`，支持视口缩放（Zoom）、平移、旋转及 `ScreenToWorld` / `WorldToScreen` 逆矩阵坐标反查，满足像素级 UI 与世界交互需求。
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
│   ├── camera.h             # 2D 正交相机
│   ├── engine.h             # 引擎宿主与主循环
│   ├── input.h              # 每帧键盘/鼠标输入快照
│   ├── math.h               # Vec2, Rect, Color, Mat4
│   ├── node.h               # Transform2D 与场景图节点
│   ├── renderer.h           # 渲染器门面
│   ├── resource_manager.h   # 句柄式资源池
│   ├── scene.h              # 图层/Z 序场景渲染
│   ├── shader.h             # 着色器管线与 Uniform 缓存
│   ├── sprite_batch.h       # 高性能 SpriteBatch
│   ├── texture.h            # 纹理与 1x1 白图单例
│   └── version.h            # 版本定义
├── src/
│   ├── core/                # 引擎循环、生命周期与资源池
│   ├── platform/            # GLFW 窗口与 GL 上下文管理
│   ├── render/              # OpenGL 3.3 Core 渲染管线实现
│   ├── scene/               # 节点层级与场景渲染
│   └── script/              # FakeLua 桥接与原生 API 注册
├── third_party/stb/         # stb_image.h, stb_image_write.h
├── scripts/                 # 示例脚本（game.lua = 打砖块，main.lua = 场景演示）
├── examples/hello/          # 最小可运行示例（支持 --headless、--scene-demo）
├── docs/SCRIPTING.md        # 脚本编写指南与 API 参考
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

# 运行中编辑 scripts/game.lua，保存后引擎自动重新编译（热重载）：
./build/bin/fake2d_hello --hot-reload

# 以 C++ 场景图 / 图集演示替代 Lua 小游戏：
./build/bin/fake2d_hello --scene-demo
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
| **4 — 文字与音频** | 表现力增强 | 位图 / MSDF 字体渲染、音频播放桩、合批粒子发射器 | 下一步 |
| **5 — 打磨交付** | 商业可交付质量 | 多键合批排序优化、HiDPI / Retina 视网膜缩放、全平台打包指南、DrawCall 性能基准 | 计划中 |

详细任务清单见 [docs/PLAN.md](docs/PLAN.md)。

---

## 许可证

MIT — 详见 [LICENSE](LICENSE)。
