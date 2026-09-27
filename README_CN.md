# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License](https://img.shields.io/github/license/esrrhs/fake2d)](LICENSE)

**Fake2D** 是基于 [FakeLua](https://github.com/esrrhs/fakelua) 的 2D 游戏渲染引擎：C++ 负责窗口、GPU 资源与场景数据；FakeLua 脚本编排玩法逻辑，并在每帧结束做 Arena `Reset()`，无 GC 停顿。

> 当前进度：**Phase 0 骨架** — 工程结构、主循环、OpenGL 清屏、FakeLua `update(dt)` 钩子。详见下方[实现计划](#实现计划)。

## 设计原则

与 FakeLua 宿主模型对齐：

| 层级 | 职责 |
|------|------|
| **C++ 宿主** | 窗口、GL、渲染器、贴图/网格、场景图、输入、资源 |
| **FakeLua** | 玩法粘合：生成实体、驱动动画、UI 流程、关卡脚本 |
| **每帧约定** | `update(dt)` → 提交绘制 → `fakelua::Reset(state)` |

脚本保持浅状态；精灵、图集、物理体等重对象驻留 C++，通过窄原生 API 暴露。

## 目标架构

```
┌─────────────────────────────────────────────┐
│              FakeLua 脚本层                 │
│         update / 场景 / UI 逻辑             │
└─────────────────────┬───────────────────────┘
                      │ Call / Native 绑定
┌─────────────────────▼───────────────────────┐
│                   Engine                    │
│  ScriptHost · Scene · Input · Resources     │
├─────────────────────┬───────────────────────┤
│   SpriteBatch       │  Camera / Transform   │
│   Texture / Atlas   │  Font（后续）         │
├─────────────────────▼───────────────────────┤
│        Renderer（OpenGL 3.3 core）          │
│        Window（GLFW）                       │
└─────────────────────────────────────────────┘
```

## 目录结构

```
include/fake2d/     公共头文件
src/core/           引擎主循环
src/render/         GPU 渲染
src/script/         FakeLua 桥接
src/platform/       GLFW 窗口
src/scene/          场景图（Phase 2+）
scripts/            示例脚本
examples/hello/     最小可运行窗口
docs/PLAN.md        详细路线图
```

## 依赖

- CMake ≥ 3.16，C++23
- OpenGL 3.3+ / GLFW 3.4（CPM 拉取）
- FakeLua 查找顺序：`../fakelua` → `-DFAKELUA_SOURCE_DIR=` → CPM 拉取

## 编译

```bash
# 推荐：将 FakeLua 与本仓库并列克隆
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/fake2d_hello
```

仅窗口骨架（不链接 FakeLua，便于 CI / 先跑通）：

```bash
cmake -S . -B build -DFAKE2D_WITH_FAKELUA=OFF
cmake --build build --parallel
```

## 实现计划

| 阶段 | 目标 | 交付物 |
|------|------|--------|
| **0 — 骨架** *（当前）* | 可启动宿主 | GLFW 窗口、GL 清屏、FakeLua 编译/`update`/`Reset`、hello 示例、双语文档 |
| **1 — 绘制图元** | 第一批像素 | 正交相机、色块四边形、精灵批处理、PNG 贴图、UV 矩形 |
| **2 — 场景与资源** | 结构化 | 节点/变换层级、图层、图集、资源缓存、TCC 热更 |
| **3 — 脚本 API** | 用 Lua 做游戏 | `sprite`/`camera`/`input`/`time` 绑定；小样例游戏 |
| **4 — 文字与音频** | 表现力 | 位图/SDF 字体、音频播放桩、简单粒子 |
| **5 — 打磨** | 可交付质量 | 批排序、vsync/dpi、打包说明、DrawCall 基准 |

明细清单见 [docs/PLAN.md](docs/PLAN.md)。

## 版本

[`include/fake2d/version.h`](include/fake2d/version.h) 中 `FAKE2D_VERSION_STRING`，当前 **0.1.0**。

## 许可证

MIT — 见 [LICENSE](LICENSE)。
