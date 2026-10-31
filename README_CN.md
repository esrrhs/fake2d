# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-brightgreen.svg)](https://en.cppreference.com/w/cpp/23)
[![OpenGL](https://img.shields.io/badge/OpenGL-3.3%20Core%20%7C%202.1-orange.svg)](https://www.opengl.org/)
[![FakeLua](https://img.shields.io/badge/Script-FakeLua-red.svg)](https://github.com/esrrhs/fakelua)
[![Version](https://img.shields.io/badge/version-1.36.0-informational.svg)](include/fake2d/version.h)

**Fake2D** 是基于 [FakeLua](https://github.com/esrrhs/fakelua) 驱动的现代化、轻量级、高性能 2D 游戏引擎：
- **C++23 宿主引擎**：掌管窗口管理、GPU 渲染管线、空间物理、音频混音与底层资源生命周期；
- **FakeLua 脚本层**：高效编排游戏玩法、UI 与实体交互，并在每帧边界执行 Arena 线性重置，实现**彻底的零垃圾回收停顿（Zero-GC Pauses）**。

Fake2D 采用现代工业级 2D 引擎架构（对齐 Raylib、MonoGame、Defold 设计哲学），拥有完备的**渲染批处理、Tiled 瓦片地图、空间物理与碰撞、多总线/空间音频、后处理特效链、即时模式 UI、UTF-8/CJK 文本与本地化，以及跨平台自动化测试系统**，现已具备成熟的商业级 2D 游戏开发与交付能力。

---

## 🌟 核心设计理念

### 1. 彻底消除 GC 停顿的每帧运行模型（Zero-GC Per-Frame Architecture）
- **无卡顿稳定高帧率**：传统脚本引擎（如 Love2D、Cocos-Lua、Unity C#）在高频运行实体分配与临时计算时，极易因垃圾回收器回收引发卡顿掉帧（GC Spikes）；
- **线性 Arena 帧回收**：Fake2D 脚本在每帧逻辑中可自由创建临时 table、向量与闭包运算数据。帧末引擎统一执行 `fakelua::inter::Reset(state)`，数微秒内瞬时复位整个内存区域，保证丝滑稳定的 60 / 120+ FPS 运行体验；
- **跨帧持久存储体系**：提供 C++ 接管的 `EntityStore`（256 个持久原生实体槽位）与 `SaveStore`（JSON 键值存档 + 带 CRC 校验与元数据头的二进制 v2 存档容器），持久状态与帧内瞬态数据边界清晰。

### 2. 数据驱动的动态流式合批（Data-Oriented SpriteBatching）
- **动态 VBO 流式写入**：搭配静态预分配共享索引缓冲区（`0-1-2-2-3-0` 规则每 Quad 6 个索引）；
- **1×1 纯白单例贴图**：纯色色块、进度条、UI 面板背景与纹理精灵共享完全相同的 GLSL 着色器管线，彻底消除因绘制纯色图元打断批次的开销；
- **多键智能排序合批**：支持延迟指令缓冲与多键稳定排序（Layer → Depth/Z → Blend Mode → Texture ID）。同纹理与同状态四边形自动合并绘制；支持 Alpha 覆盖、Additive 加法与 Multiply 正片叠底混合；
- **超高渲染吞吐**：内置 `--bench` 实测，在 768 精灵 / 8 纹理场景下，DrawCall 从 674 次大幅降低至 **8 次**。

### 3. 解耦的 2D 摄像机与视口系统（Camera2D）
- 独立摄像机变换矩阵（正交 VP 矩阵），支持平滑平移、视口缩放（Zoom）、旋转及 `ScreenToWorld` / `WorldToScreen` 逆矩阵精准坐标反查；
- **原生 HiDPI / Retina 适配**：底层投影自适应物理帧缓冲分辨率，脚本游戏坐标统一使用逻辑点，窗口尺寸与系统 DPI 变动实时同步跟踪；
- **完备的相机工具箱**：世界边界自动钳制（`SetBounds`）、视口居中、平滑阻尼跟随（Smoothing）、跟随时死区（Deadzone）、经典卷轴单向锁（Forward-only Scroll Lock）与 Trauma 创伤衰减屏幕震动（`camera_shake`）。

### 4. 完备的现代化 2D 子系统生态
Fake2D 不仅是渲染器，更是一套自洽完备的 2D 游戏开发平台：
- **Tiled 瓦片系统**：支持正交 JSON 地图、视差图层（Parallax）、图片层、对象层（`objectgroup` 刷怪点/触发区）、斜坡地表物理、以及**动态瓦片循环动画（Animated Tiles）**；
- **内置物理与导航**：零外部依赖 2D 刚体引擎（圆 / 朝向盒 / 凸多边形），按密度求质量与转动惯量、顺序冲量 +  Coulomb 摩擦与 warm starting、运动学刚体、阻尼、自动休眠、距离/绳索/铰链（马达与限位）/焊接关节、子步防穿透；空间哈希宽相（约为暴力检测 5 倍）、Fixed Timestep 与姿态渲染插值、16 位碰撞过滤（Category / Mask）、射线检测与形状探测、A\* 寻路（防切角与视线拉直）；
- **专业级音频系统**：设备解耦的纯软件混音核心、三总线路由（Master / SFX / Music）、平滑交叉淡入淡出（Cross-fade Envelopes）、3D 空间位置音频（距离线性衰减与立体声声相 Pan）、大曲目 8192 帧按需流式回填（支持 WAV/MP3/OGG/FLAC）；
- **多趟后处理与 FBO**：离屏渲染目标（RenderTarget）、Ping-Pong 缓冲交换、Bloom 辉光、可分离 9 抽头高斯模糊、波浪扭曲特效、战争迷雾（Fog of War）以及全景小地图烘焙；
- **实时 2D 光照与投影阴影**：点光源带径向柔和衰减，每个遮挡物向外投影硬阴影，光源移动时阴影随之扫过场景。整个通道只用一张生成的衰减贴图与一次 Multiply 混合，并按混合状态合批——64 个运动光照打满 256 个遮挡物槽位仍是 4 个 Draw Call、0.7 ms。遮挡物支持矩形、多边形与实时物理刚体，每道阴影都被约束在自己的光照圆内，每帧阴影四边形预算兜住最坏情况；
- **2D 骨骼动画**：骨骼层级由关键帧剪辑驱动，一套走路循环是一串角度而不是两百张图。骨架从 JSON 载入（bones / slots / attachments / skins / clips 的 Spine 风格结构），逐通道线性或阶梯插值，交叉淡入淡出时旧剪辑仍在底层继续播放，时间轴事件跨过循环接缝也能命中当帧，并支持双臂两骨 IK 与指向 IK。代价是量出来的：一张图集上的 12 个部件只需 1 个 Draw Call，12 个士兵加起来仍是 1 个；
- **即时模式 UI（Immediate-Mode UI）**：开箱即用的 UI 控件库（Panel、Button、Label、Slider、Checkbox、TextInput、ScrollArea、ProgressBar、RadioButton、Combo、可拖拽 Window、Tab 标签页、Tree 折叠树、Tooltip）。
- **文本与平台能力层**：UTF-8 按需字形图集（原生支持 CJK/日文/韩文，`font_load` 可载入任意 TTF/TTC/OTF，缺字自动回退字体）、中文逐字换行与英文按词换行（含禁则与 kerning）、旋转/阴影/描边/自动缩字、运行时切换的 TSV 语言包（BOM 兼容、缺 key 报告）、跨平台资源/用户目录、带 CRC 与 `.bak` 回滚的 v2 存档容器、实体快照存读、文件日志与崩溃报告、离线成就镜像、`mod.json` 模组管理器，以及可选 Steamworks 适配层（成就 / Rich Presence / 语言 / 云存档开关，未链接 SDK 时自动降级为零依赖 stub）。
- **玩法基础工具**：确定性 splitmix64 随机数、命名缓动曲线与帧率无关的 `Approach`、可重绑的输入动作映射、窗口尺寸/resizable/最小尺寸/光标控制、音频 pitch/循环/停止，以及运行时 FPS / 帧耗时 / draw call 统计。
- **2.5D 清版动作套件**：全局游戏时间缩放 + 命中停顿 `time_set_scale`/`time_hitstop`（脚本、物理、动画、粒子一起定格）；每个实体 16 个数值 + 4 个字符串槽、最多 512 个实体，足以承载完整角色阵容；动画剪辑池扩至 128；配合 `draw_set_z` + `batch_set_sorted` 实现深度正确的横版 y 排序。

### 5. 现代化工程化与极佳开发者体验
- **JIT 脚本热重载**：支持 GCC JIT 与解释器双模式，在运行中修改 Lua 脚本保存即刻生效，无需重启进程；
- **无头测试与 CI/CD**：内置原生 `--headless`、`--frames N` 与 `--screenshot` 参数，可在无物理显示器的虚拟机、Docker 容器中运行自动化回归验证与帧缓冲像素级比对；
- **29 项内置自测试套件**：1393 断言，涵盖刚体动力学、关节、休眠、防穿透、物理查询、碰撞过滤、时间步长、地图对象、A\* 寻路、离屏 FBO、即时 UI、后处理链、战争迷雾、2D 光照与阴影包含性、骨骼层级/剪辑播放/交叉淡入/IK/时间轴事件、音频混音、动画瓦片、UTF-8/CJK 文本与字体回退、平台能力层、确定性随机、缓动、实体快照、输入动作、窗口控制、游戏时间缩放/命中停顿、16 槽实体角色、128 剪辑动画池、飞机大战素材管线、城市建造地形/建筑图集/基础地图、RTS 地形/单位/建筑/地图管线与视觉小说背景/立绘素材等，所有测试用例均经过变异测试（Mutation-tested）验证；
- **开箱即用独立打包**：提供自动化打包脚本，一键生成包含依赖库的独立二进制压缩包，分发给玩家免安装环境。

---

## 🏛 引擎架构拓扑

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            FakeLua 业务脚本层                               │
│              update(dt) · 实体管理 · 状态机 · 关卡逻辑 · UI 编排            │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ Native 绑定 (draw_*, phys_*, audio_*, map_*, ...)
┌──────────────────────────────────────▼──────────────────────────────────────┐
│                                Fake2D Engine                                │
│    ScriptHost (JIT/热重载) · 资源管理器 · 实体槽位 · 存档 (SaveStore v2)   │
├───────────────────────┬──────────────────────────────┬──────────────────────┤
│      渲染管线         │          物理与世界          │       音频引擎       │
│  · SpriteBatch (合批) │  · PhysicsWorld (AABB/圆/宽相)│  · AudioEngine (混音) │
│  · Camera2D (视口变换)│  · 空间哈希网格 (SpatialHash)│  · 3 总线 (Master/SFX)│
│  · RenderTarget (FBO) │  · 固定步长累加器 (Fixed-dt) │  · 3D 空间位置声像   │
│  · PostFX (多趟后处理)│  · 碰撞过滤 (Category/Mask)  │  · 8K 帧流式播放     │
│  · UTF-8/CJK 字体图集 │  · Tiled 地图 (解析/渲染/对象)│  · VU 峰值电平监控   │
│  · CPU 粒子池 (8192)  │  · A* 寻路网格 (防切角/拉直) │                      │
├───────────────────────┴──────────────────────────────┴──────────────────────┤
│                           即时模式 UI 控件库 (UISystem)                      │
│     窗口 · 面板 · 按钮 · 滑条 · 输入框 · 复选框 · 单选 · 下拉 · 滚动 · 标签 · 树     │
│     自动缩字 (LabelFit) · 悬浮提示队列 (Tooltip)                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                     平台抽象 · 本地化 (I18n) · 用户目录 · Steam             │
│         OpenGL 3.3 Core (向下兼容 GL 2.1) · GLFW 3.4 (窗口/手柄/输入快照)   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 🎮 精彩内置 Demo 矩阵

Fake2D 仓库自带 29 个可直接运行的高质量 Demo，并提供一个引擎内启动器供你自由挑选，涵盖平台跳跃、经典街机、清版动作、纵版弹幕射击、斯诺克台球物理、魂斗罗式横版射击、城市模拟经营、红警式即时战略、心跳回忆式视觉小说、迷宫寻路、即时 UI、战争迷雾、带投影阴影的实时 2D 光照、关键帧驱动的 2D 骨骼动画、多趟后处理与空间音频等各个维度：

| Demo 启动参数 | 演示主题 | 核心特性亮点 |
|---|---|---|
| `--launcher` | **Demo 启动器（默认入口）** | 29 个 Demo 的封面图选择界面：分类过滤、鼠标与键盘操作、并显示每项对应的命令行；选中后以子进程启动该 Demo，关掉游戏就回到这里继续挑 |
| `--breakout-demo` | **经典打砖块 (Breakout)** | 物理反弹、砖块碰撞消除、粒子碎屑飞溅、HUD 文本与音效合成 |
| `--mario-demo` | **超级马里奥兄弟 1-1** | 完整复刻 SMB 1-1；大/小双形态、板栗仔巡逻与踩扁、顶砖块碎裂粒子、问号箱、旗杆结算流程、Attract 自动巡航 |
| `--angry-birds-demo` | **愤怒的小鸟沙盒** | 弹弓冲量发射、休眠塔堆、接触事件爆猪与计分、相机跟随与合成音效 |
| `--limbo-demo` | **LIMBO 多关节沙盒** | 6 段铰链布娃娃、9 节绳索与铰链吊桥、浮力水体、死亡断肢后仍互相牵引 |
| `--platformer-demo` | **平台跳跃与手柄控制器** | 瓦片跳跃控制：土狼时间（Coyote Time）、跳跃预输入缓冲、单向板平台穿越、金币收集、平滑跟随相机 |
| `--beat-em-up-demo` | **2.5D 清版动作（Dino Brawl）** | CC0 像素角色（Chasersgaming 的 Renegade/Soldier）与 CC0 夜景城市视差背景（FisherG），最近邻采样与调色板换色；地面平面 x/纵深/高度移动与 y 排序角色；三段连击、踢击、旋转、命中停顿、无敌帧、击倒/起身、锁镜头刷怪波次、敌人 AI 与 Boss，无头模式自带吸引式 AI |
| `--shmup-demo` | **纵版射击（雷电风格）** | ansimuz CC0 像素素材：纵版滚动沙漠+云层、八方向移动与自动射击、三种可升级武器（Vulcan / Laser / Missiles）、清屏炸弹、敌机波次（瞄准弹/散射弹）、带血条的多阶段 Boss、命数/分数/关卡循环，无头模式自带吸引式 AI |
| `--snooker-demo` | **斯诺克台球（完整物理）** | Lua 自研台球求解器：固定子步 + 接触时刻回退修正、等质量弹性碰撞与重叠分离、库边法向反弹与侧旋变线、六个袋口、台呢滚动摩擦、跟进/回旋。球杆瞄准线、幽灵球与旋转垫；完整规则（红球 → 指定彩球 → 黄到黑）、犯规罚分、彩球复位、双人计分，以及自动找袋的幽灵球 AI |
| `--contra-demo` | **魂斗罗式横版射击** | 多层棕榈/山体视差丛林关；跑动、可变高度跳跃、蹲伏与八方向射击；武器胶囊（步枪/机枪/散弹/激光）、推进士兵、壁挂炮塔、飞行补给舱、深坑、可摧毁炮塔与反应堆核心的堡垒 Boss；命数、重生、关卡循环、合成进行曲主题，以及无头模式跑通关卡的吸引式 AI |
| `--city-demo` | **城市模拟经营（模拟城市风格）** | 生成的 56x42 地块：自己修路、划区（住宅/商业/工业）、建公园与发电厂。月度模拟驱动道路连通性、电力覆盖、RCI 需求、建筑三级成长、废弃重建、污染与税收/维护预算。像素级准确的屏幕空间 HUD、可点击小地图、拖拽铺设、地块信息浮窗、合成城市主题，以及无头模式自动建城的“市长”AI |
| `--rts-demo` | **即时战略（红警风格）** | 64x48 战场与镜像双基地：矿车自动采矿并往返精炼厂，建造电厂/兵营/战车工厂/精炼厂/炮塔，训练步兵、火箭兵、坦克与矿车；框选单位、右键移动/攻击/采矿。制导步枪/火箭/炮弹、按阵营着色的生成素材、电力限制生产、小地图、任务 HUD、敌方指挥官多波进攻，以及无头模式自动打完一局的吸引式指挥官 |
| `--vn-demo` | **视觉小说 / 恋爱模拟（心跳回忆风格）** | 四位女主与 AI 绘制的表情立绘、八张绘制背景与中文分支剧情。14 天日程包含学习/锻炼/打扮/打工/休息、体力与金钱；每位女孩三种话题（好感 20 解锁心事）、礼物、好感 30 解锁约会、夏日祭典与告白结局。打字机对话、点击选项、好感浮字、快速存读档，以及无头模式自动读完整段剧情的吸引式玩家 |
| `--slope-demo` | **斜坡地形控制器** | Tiled 斜坡解析（`map_slope_dir`/`map_ground_y`），角色沿倾斜地面平滑贴合走动与实时地表高度计算 |
| `--path-demo` | **迷宫追逐与 A\* 寻路** | 迷宫地图，错峰重寻路的追踪 AI，玩家自动航路演示模式，实时 A\* 路径折线显示（防切角与视线拉直） |
| `--tile-demo` | **Tiled 动画瓦片** | 引擎统一时钟推进 tileset 动画；池塘水面波光涟漪与跳动火把循环播放，碰撞网格与显示画面解耦 |
| `--fog-demo` | **战争迷雾与小地图** | 过程化大世界；Multiply 混合模式遮罩、探索区域跨帧累积擦除、无视差小地图联动与 `rt_sample` 探索度采样 |
| `--light-demo` | **2D 光照与投影阴影** | 会移动的光源与随之扫动的投影阴影：手提灯、闪烁火把、环绕光球、无阴影补光、滑动的木箱与一个由物理模拟驱动的木箱都拖着自己的阴影；一键点亮 24 个额外光源，整个光照通道仅 4 个 Draw Call |
| `--skeleton-demo` | **2D 骨骼动画** | 一个挂在 15 根骨骼上的武装士兵：以关键帧角度写出的走路循环、会呼吸的待机、手臂两骨 IK 让枪口始终指向鼠标、枪口火光与后坐、从当前姿态交叉淡入的受击与死亡一击、在撰写帧精确触发的时间轴事件（尘土、火光、弹壳）、皮肤切换与骨骼叠加视图，身后还有 6 个共用同一套骨架的士兵一起行军 |
| `--fx-demo` | **多趟后处理与 Bloom 辉光** | 程序化赛博天际线；FBO 降采样、双趟可分离高斯模糊、Bloom 阈值提取、波浪动态扭曲与分屏（Split-screen）对比 |
| `--post-demo` | **全屏调色与烘焙小地图** | 调色着色器（灰度、对比度拉伸、暗角 Vignette、扫描线 Scanlines）与即时渲染视口烘焙小地图 |
| `--rt-demo` | **离屏渲染与运动拖尾** | RenderTarget 离屏烘焙、残影拖尾累积反馈（仅淡出不清屏），对比即时渲染模式 |
| `--spatial-demo` | **3D 空间位置音频** | 4 个世界音源循环发声，WASD 移动侦听者；动态距离线性衰减、立体声声相 Pan、衰减范围圈与 L/R 双通道 VU 电平表 |
| `--audio-demo` | **专业音频调音台** | 三总线增益滑块、交叉淡入淡出（Cross-fade）、混音冻结暂停、24 段实时 VU 电平表、解码与 8K 帧流式播放内存对比 |
| `--ui-demo` / `--ui2-demo` / `--ui3-demo` | **即时模式 UI 全家桶** | 滑块、复选框、UTF-8 文本输入、滚动列表、进度条、单选组、下拉菜单、拖拽窗口（带 mouse delta 拖拽）、标签页与折叠树 |
| `--text-demo` | **文本与平台能力层** | UTF-8/CJK 渲染与运行时语言切换、中文逐字换行、旋转文字与自动缩字、用户存档/配置路径、带 CRC 的 v2 存档容器与 Steam 适配状态 |

---

### 随包美术资源（Shipped Art）

所有游戏类 Demo 的场景表现都来自随仓库提交的图片资源，而不再是首次运行时现画的图元：CC0 像素包（Chasersgaming、ansimuz、FisherG）、`assets/` 下的 AI 插画关键图，以及用 AI 材质重打光的 tile sheet。`tools/art_key.py` 负责从原始生成图烘焙出这些提交的 PNG；`--art-test` 会遍历整棵美术目录并逐张加载，因此某个文件夹一旦从 `.gitignore` 里漏掉就会直接让构建失败，而不是悄悄退回图元绘制。

### Demo 启动器

不带任何 Demo 参数直接运行，就会进入引擎内的选择界面：每个 Demo 一张卡片，带随包封面图、一句话简介、操作说明以及它对应的完整命令行。

```bash
./build/bin/fake2d_hello              # 窗口模式、无参数 -> 启动器
./build/bin/fake2d_hello --launcher   # 显式进入启动器
```

UP/DOWN 或鼠标选择，ENTER 或点击启动，TAB 切换分类过滤（全部 / 游戏 / 引擎特性），滚轮与 PAGEUP/PAGEDOWN 滚动，ESC 退出。可选清单来自 `include/fake2d/demos.h`，也就是命令行解析所用的同一张表，所以菜单只会提供宿主确实能启动的 Demo，往表里加一行就会自动出现在界面上；`--launcher-test` 负责守住这张表（flag 唯一、分类合法、脚本文件真实存在）。

选中某个 Demo 后，宿主以 `--<flag>-demo` **拉起子进程**并等它结束：每个 Demo 都跑在干净的会话里、各自完成自己的资源与音频初始化，而关掉游戏后启动器还在原地，可以直接挑下一个。窗口模式下的 Demo 与启动器会跳过 GCC JIT、直接用解释器跑脚本，这一来一回因此只有一秒左右而不是五秒；`--jit` 强制原生编译，`--interp` 强制走 VM，无头模式（CI）仍按原样使用 JIT。

## 🚀 快速上手

### 环境依赖

- **CMake**：≥ 3.16
- **C++ 编译器**：支持 **C++23** 的现代化编译器（GCC 13+、Clang 16+、Apple Clang 15+、MSVC 2022）
- **图形与窗口**：OpenGL 3.3+ Core Profile（支持运行时后备至 GL 2.1）；GLFW 3.4 与 nlohmann_json 会由 CPM 自动拉取或使用本地依赖
- **脚本引擎**：[FakeLua](https://github.com/esrrhs/fakelua)（可通过系统的 `/usr/local` 自动由 CMake `find_package(fakelua REQUIRED)` 发现）

### 编译构建

```bash
# 1. 克隆代码库
git clone https://github.com/esrrhs/fake2d.git
cd fake2d

# 2. 生成构建工程（Release 优化模式）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 3. 编译构建
cmake --build build --parallel

# 4. 运行：打开 Demo 选择器（加 --<name>-demo 可直接进入某个 Demo）
./build/bin/fake2d_hello
```

> **要在仓库根目录下运行。** `scripts/` 与 `assets/` 按当前工作目录解析，从别处启动只会得到一个空窗口。macOS 上直接双击 `run.command` 即可：它会 `cd` 进仓库、首次运行自动编译，然后打开选择器。

> **纯 C++ 骨架模式**：如果本地暂未安装 FakeLua，可传入 `-DFAKE2D_WITH_FAKELUA=OFF` 编译纯 C++ 骨架工程用于底层图形与硬件调试。

---

## 💻 命令行运行手册

### 体验精选 Demos

```bash
# 超级马里奥兄弟 1-1（A/D 移动、Shift 奔跑加速、空格跳跃，也支持手柄）
./build/bin/fake2d_hello --mario-demo

# 愤怒的小鸟沙盒（拖拽发射，相机跟随 + 音效，R 重开）
./build/bin/fake2d_hello --angry-birds-demo

# LIMBO 多关节沙盒（A/D 移动、SPACE 跳、E 抓绳、K 断肢、R 重置）
./build/bin/fake2d_hello --limbo-demo

# 滚动平台跳跃演示（平滑相机、金币、单向平台）
./build/bin/fake2d_hello --platformer-demo

# 迷宫追逐与 A* 路径规划（按 P 切换路径线绘制）
./build/bin/fake2d_hello --path-demo

# Tiled 动态瓦片动画（水波泛光与火把闪烁）
./build/bin/fake2d_hello --tile-demo

# 战争迷雾系统（F 切换全景迷雾，R 重置探索）
./build/bin/fake2d_hello --fog-demo

# 2D 光照与投影阴影（左右方向键提灯行走，SPACE 光照爆发，L 切换光照）
./build/bin/fake2d_hello --light-demo

# 2D 骨骼动画（左右方向键行走，SPACE 射击，H 受击，K 死亡，R 复活，B 骨骼叠加，N 换皮肤）
./build/bin/fake2d_hello --skeleton-demo

# 多趟后处理 Bloom 辉光与分屏模式（按 S 切换分屏）
./build/bin/fake2d_hello --fx-demo

# 3D 空间位置音频（WASD 移动玩家听者，鼠标滚轮互动）
./build/bin/fake2d_hello --spatial-demo

# 调音台界面（流式 vs 预解码播放、三总线控制）
./build/bin/fake2d_hello --audio-demo

# 即时模式 UI 展示（窗口拖拽、选项卡、下拉菜单）
./build/bin/fake2d_hello --ui3-demo

# UTF-8/CJK 文本与平台能力层（L 切换语言，SAVE 写入 v2 存档）
./build/bin/fake2d_hello --text-demo

# 2.5D 清版动作（WASD 移动，SPACE/J 出拳，K 踢击，L 旋转）
./build/bin/fake2d_hello --beat-em-up-demo

# 纵版射击（WASD 飞行，SPACE/J 射击，K 炸弹）
./build/bin/fake2d_hello --shmup-demo

# 斯诺克台球（鼠标瞄准，按住左键蓄力，方向键/旋转垫加塞，R 重开一局）
./build/bin/fake2d_hello --snooker-demo

# 魂斗罗式横版射击（A/D 移动，W 抬枪，S 蹲伏，Space 跳跃，J/X 射击）
./build/bin/fake2d_hello --contra-demo

# 城市模拟经营（1-7 选择工具，拖拽修路/划区，WASD 平移，滚轮缩放，空格暂停）
./build/bin/fake2d_hello --city-demo

# 即时战略（左键点选/框选，右键移动/攻击，1-5 建造，6-9 训练，WASD 平移）
./build/bin/fake2d_hello --rts-demo

# 视觉小说 / 恋爱模拟（点击或空格推进，1-4 选择选项，F5/F9 存读档）
./build/bin/fake2d_hello --vn-demo
```

### 生产力与调试选项

```bash
# 开启脚本热重载（运行期间修改 scripts/game.lua 保存即刻在游戏内生效）
./build/bin/fake2d_hello --hot-reload

# 选择脚本后端：窗口模式的 Demo 与启动器默认走解释器，一秒进入而非五秒；
# --jit 强制原生编译，无头模式无论如何都用 JIT
./build/bin/fake2d_hello --jit --mario-demo
./build/bin/fake2d_hello --interp --mario-demo

# 运行指定的 Lua 脚本
./build/bin/fake2d_hello --entry scripts/mario.lua

# 调整物理固定更新频率（默认 60 Hz，可调整为 120 Hz 或回退至变步长）
./build/bin/fake2d_hello --fixed-hz 120 --mario-demo
./build/bin/fake2d_hello --variable-step --mario-demo

# 运行图形合批性能基准测试（立即模式 vs 排序多键合批）
./build/bin/fake2d_hello --headless --bench

# 运行物理碰撞宽相基准测试（暴力扫描 vs 空间哈希网格）
./build/bin/fake2d_hello --headless --phys-bench
```

### 无头模式与自动化 CI 验证（Headless & Testing）

Fake2D 原生支持无头运行模式，可在无 X11/Wayland 物理显示器的 Linux CI 机器（配合 xvfb）上完整运行全部图形与逻辑：

```bash
# 运行 60 帧无头测试并平稳退出
xvfb-run ./build/bin/fake2d_hello --headless --frames 60

# 无头截取指定帧缓冲导出为 PNG（用于视觉回归像素比对）
xvfb-run ./build/bin/fake2d_hello --headless --mario-demo --shot-frame 60 --screenshot mario_snap.png

# 拒绝“保存成功但画面空白”的截图（退出码 2）：颜色数量下限探测
xvfb-run ./build/bin/fake2d_hello --headless --text-demo --screenshot text.png --shot-min-colors 12

# 运行 29 组内置无头自测试套件（通过退出码断言执行结果）
xvfb-run ./build/bin/fake2d_hello --phys-query-test    # 射线检测、形状重叠、接触生命周期
xvfb-run ./build/bin/fake2d_hello --phys-filter-test   # 16位 Category/Mask 碰撞过滤
xvfb-run ./build/bin/fake2d_hello --phys-rigid-test    # 转动惯量、摩擦与堆叠稳定性
xvfb-run ./build/bin/fake2d_hello --phys-extended-test # 关节、休眠、运动学与防穿透
xvfb-run ./build/bin/fake2d_hello --timestep-test      # 固定步长物理累加器与相机平滑
xvfb-run ./build/bin/fake2d_hello --map-object-test    # Tiled 对象层与自定义属性
xvfb-run ./build/bin/fake2d_hello --path-test          # 瓦片地图 A* 寻路（防切角与拉直）
xvfb-run ./build/bin/fake2d_hello --tile-test          # Tiled 动画瓦片时钟推进
xvfb-run ./build/bin/fake2d_hello --rt-test            # 离屏 FBO 渲染目标生命周期
xvfb-run ./build/bin/fake2d_hello --ui-test            # 即时模式 UI 与输入注入测试
xvfb-run ./build/bin/fake2d_hello --post-test          # 全屏后处理与小地图烘焙
xvfb-run ./build/bin/fake2d_hello --fx-test            # 多趟 Ping-Pong 后处理链与高斯模糊
xvfb-run ./build/bin/fake2d_hello --fog-test           # 战争迷雾 Multiply 混合与像素采样
xvfb-run ./build/bin/fake2d_hello --light-test         # 光照池、阴影包含性与四边形预算
xvfb-run ./build/bin/fake2d_hello --light-bench        # 光照合批：Draw Call 数不随光源增长
xvfb-run ./build/bin/fake2d_hello --skeleton-test      # 骨骼层级、剪辑、交叉淡入、IK 与时间轴事件
xvfb-run ./build/bin/fake2d_hello --audio-test         # 纯软件混音核心、包络、流式播放
xvfb-run ./build/bin/fake2d_hello --font-test          # UTF-8/CJK 字形图集、换行、旋转与自动缩字
xvfb-run ./build/bin/fake2d_hello --platform-test      # 语言包、用户目录、存档 v2、Steam stub、UI 文本
xvfb-run ./build/bin/fake2d_hello --gameplay-test      # 随机数、缓动、实体快照、输入动作、窗口
xvfb-run ./build/bin/fake2d_hello --combat-test        # 时间缩放、命中停顿、16 槽角色、动画池
xvfb-run ./build/bin/fake2d_hello --shmup-test         # 射击素材图集、最近邻采样、缺失文件
xvfb-run ./build/bin/fake2d_hello --snooker-test       # 球体阴影贴图、透明度/光照采样、加载器拒绝
xvfb-run ./build/bin/fake2d_hello --contra-test         # 士兵图集、爆炸序列、月亮/辉光采样、加载器拒绝
xvfb-run ./build/bin/fake2d_hello --city-test           # 城市地形/建筑图集、基础地图道路与湖泊、加载器拒绝
xvfb-run ./build/bin/fake2d_hello --rts-test            # RTS 地形/单位/建筑素材、战场混凝土/矿脉/水体
xvfb-run ./build/bin/fake2d_hello --vn-test             # VN 背景/立绘、透明度采样、加载器拒绝
```

---

## 📝 极简脚本范例

Fake2D 的 Lua 脚本设计极度简洁且符合现代 2D 逻辑直觉。下方以一个具有**精灵绘制、输入控制、物理与声音**的极简脚本为例：

```lua
-- scripts/my_game.lua
local W, H = 960, 540
local player_x = W * 0.5
local player_y = H * 0.8
local speed = 350.0 + 0.0     -- 数值初始化为浮点表达式

function update(dt)
    -- 1. 获取每帧键盘/手柄输入
    if input_key_down("left") or input_pad_down(0, "dpad_left") then
        player_x = player_x - speed * dt
    end
    if input_key_down("right") or input_pad_down(0, "dpad_right") then
        player_x = player_x + speed * dt
    end
    if input_key_pressed("space") or input_pad_pressed(0, "a") then
        audio_play("jump", 0.8)
    end

    -- 2. 摄像机平滑跟随玩家
    camera_follow(player_x, player_y, 8.0, dt)

    -- 3. 绘制游戏内容（SpriteBatch 自动合批）
    draw_clear(24, 28, 36, 255)
    
    -- 绘制瓦片地图层
    map_draw(1)
    
    -- 绘制玩家色块矩形或精灵
    draw_quad(player_x - 16, player_y - 16, 32, 32, 60, 180, 255, 255)
    
    -- 绘制即时模式 UI
    ui_panel("hud", "top_left", 10, 10, 160, 40)
    ui_label("score", "top_left", 20, 20, "SCORE: 1200", 1.0)
    if ui_button("btn_pause", "top_right", -110, 10, 100, 32, "PAUSE") then
        -- 点击按钮逻辑
    end

    return 0
end
```

> 📖 **进阶开发文档**：
> - 想要深入了解 FakeLua 语法规范、状态存储规则以及完整 API 清单，请参阅 [**脚本编写指南（docs/SCRIPTING.md）**](docs/SCRIPTING.md)。
> - 想要了解如何跨平台将游戏打包含依赖打包分发，请参阅 [**跨平台打包说明（docs/PACKAGING.md）**](docs/PACKAGING.md)。

---

## 🗂 目录结构全览

```
fake2d/
├── CMakeLists.txt           # Modern CMake 工程主配置
├── run.command              # macOS 双击即编译并打开 Demo 选择器
├── include/fake2d/          # C++ 对外公共接口与模块头文件
│   ├── animation.h          # 关键帧动画系统（AnimationSystem）
│   ├── atlas.h              # TexturePacker JSON 图集解析
│   ├── audio.h              # 音频引擎、混音器、空间音频与流式播放
│   ├── camera.h             # 2D 正交相机、视口反查、平滑跟随与震屏
│   ├── character.h          # 平台跳跃角色控制器（土狼时间/跳跃缓冲）
│   ├── engine.h             # 引擎宿主生命周期与主循环
│   ├── entity_store.h       # C++ 实体槽位管理（穿越 Arena 帧重置）
│   ├── font.h               # TTF / 8x8 位图字体图集与排版
│   ├── input.h              # 键盘、鼠标、增量拖拽与手柄输入快照
│   ├── lighting.h           # 点光源径向衰减与投影阴影体积
│   ├── math.h               # Vec2, Rect, Color, Mat4 数学库
│   ├── node.h               # Transform2D 空间层级节点树
│   ├── particle.h           # 零分配 8192 粒子池与发射器
│   ├── physics.h            # 内置 2D 物理世界、空间哈希宽相与碰撞过滤
│   ├── renderer.h           # 渲染器门面（OpenGL 上下文封装）
│   ├── render_target.h      # 离屏渲染目标（FBO）与多趟后处理
│   ├── resource_manager.h   # 句柄式纹理/着色器资源管理器
│   ├── save_store.h         # JSON 键值持久化存档
│   ├── scene.h              # 场景图渲染门面
│   ├── script_host.h        # FakeLua 脚本宿主桥接
│   ├── shader.h             # 着色器管线与自动 Uniform 注入
│   ├── skeleton.h           # 骨骼层级、关键帧剪辑、IK 与皮肤
│   ├── sprite_batch.h       # 高性能多键排序流式 SpriteBatch
│   ├── texture.h            # Texture2D 与 1x1 白图单例
│   ├── tilemap.h            # Tiled 地图、对象层、斜坡、动画瓦片与 A* 寻路
│   ├── ui.h                 # 即时模式 UI 控件系统（UISystem）
│   └── version.h            # 语义化版本定义
├── src/                     # C++ 实现源码
│   ├── audio/               # miniaudio 设备驱动、混音算法与格式解码
│   ├── core/                # 引擎循环、时间步长、实体与存档
│   ├── gameplay/            # 玩法控制器与控制器逻辑
│   ├── physics/             # 刚体模拟、空间网格、碰撞检测与查询
│   ├── platform/            # GLFW 窗口生命周期、输入轮询与事件
│   ├── render/              # OpenGL 渲染、合批、FBO、着色器、字体、粒子
│   ├── scene/               # 变换缓存与节点树
│   ├── script/              # FakeLua 原生 API 绑定与交互
│   └── ui/                  # 即时模式 UI 几何计算与状态机
├── scripts/                 # 丰富纯 Lua 游戏与系统 Demo 脚本集
│   ├── game.lua             # 经典打砖块游戏
│   ├── mario.lua            # 超级马里奥兄弟 1-1 关卡
│   ├── angry_birds_demo.lua # 弹弓刚体沙盒
│   ├── limbo_demo.lua       # 布娃娃 / 绳索 / 吊桥 / 浮力沙盒
│   ├── platformer_demo.lua  # 平台跳跃关卡
│   ├── slope_demo.lua       # 斜坡地面测试
│   ├── path_demo.lua        # 迷宫追逐与 A* 寻路
│   ├── tile_demo.lua        # 动态瓦片动画
│   ├── fog_demo.lua         # 战争迷雾与动态遮罩
│   ├── light_demo.lua       # 实时 2D 光照与投影阴影
│   ├── skeleton_demo.lua    # 2D 骨骼动画与骨架姿态
│   ├── fx_demo.lua          # Bloom 后处理与分屏渲染
│   ├── post_demo.lua        # 全屏调色与小地图
│   ├── rt_demo.lua          # 离屏渲染与运动拖尾
│   ├── audio_demo.lua       # 混音器调音台
│   ├── spatial_demo.lua     # 3D 空间音频
│   ├── text_demo.lua        # UTF-8/CJK、本地化、路径、存档 v2 与 Steam
│   ├── snooker_demo.lua     # 子步台球物理与完整斯诺克规则
│   ├── contra_demo.lua      # 魂斗罗式横版射击与丛林关卡
│   ├── city_demo.lua        # 模拟城市式划区、经济与成长模拟
│   ├── rts_demo.lua         # 红警式基地、矿车、单位与战斗
│   ├── vn_demo.lua          # 心跳回忆式视觉小说与恋爱模拟
│   └── ui_demo*.lua         # 即时模式 UI 集合
├── tools/                   # Linux / macOS / Windows 自包含打包脚本
├── docs/                    # 详尽文档手册
│   ├── SCRIPTING.md         # 脚本编写指南与完整 Lua API 手册
│   ├── PACKAGING.md         # 跨平台自包含打包发布说明
│   └── PLAN.md              # 架构规范与历史里程碑记录
└── tests/                   # 自动化集成测试与测试夹具
```

---

## 📦 跨平台打包与发布（Distribution）

Fake2D 提供了针对 Linux、macOS 与 Windows 的一键独立可再发行打包工具（位于 `tools/` 目录），会自动解析依赖动态库并打包着色器、脚本和资源，产出可直接分发的免安装压缩包：

```bash
# macOS 打包（生成 .tar.gz，内含已签名 dylibs 与 @executable_path 链接）：
bash tools/package-macos.sh

# Linux 打包（基于 patchelf 设置 $ORIGIN rpath，打包全部依赖 so）：
bash tools/package-linux.sh

# Windows 打包（MSYS2 / MinGW 收集运行时 DLL 与可执行程序）：
bash tools/package-windows.sh
```

详细打包参数与依赖分析见 [**跨平台打包说明（docs/PACKAGING.md）**](docs/PACKAGING.md)。

---

## 📄 开源许可证

Fake2D 基于 **[MIT License](LICENSE)** 协议开源。无论是个人爱好、学术研究还是商业游戏项目，均可自由免费使用。
