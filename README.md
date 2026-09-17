# ra2 — 红色警戒 2：尤里的复仇 逆向还原工程

对 `D:\westwood\RA2YR\gamemd.exe` 做汇编级逆向，用 C++ 逐步还原引擎源码，
为后续的**多核优化**做准备。

> 本仓库只包含自己编写的分析工具、还原代码与逆向笔记。
> 不含任何游戏原始二进制、素材或数据文件。

---

## 现状速览

| 项 | 结果 |
|---|---|
| 目标 | `gamemd.exe`（4,813,072 字节），YR 主程序，2001-10-31 构建，内部名 `Sun.exe` |
| 反汇编 | 1,392,506 条指令 |
| 函数 | 6,740（可靠）/ 28,827（含线性兜底） |
| 类名 | **988 个真实类名**（来自 RTTI，非猜测），其中非模板类 328 个 |
| 原始源文件 | **58 个**（来自 assert 的 `__FILE__` 字符串），构建机路径 `D:\ra2mdpost\` |
| 关键子系统 | 主循环、锁步帧队列、寻路、地图、网络包泵均已定位到具体 VA |
| C++ 骨架 | 可编译、可运行，寻路并行实测 **3.5~4.0x** 且结果与串行逐位一致 |
| 已还原模块 | 对象体系、地图/格子、两级寻路、锁步帧队列、主循环、文件系统(MIX)、INI、Locomotor、战斗、阵营/经济、AI 小队与触发、界面、网络接口 |

---

## 目录结构

```
tools/           逆向分析工具（Python）
  peimage.py       PE 只读封装，RVA<->文件偏移换算
  rtti.py          RTTI 类型描述符提取 -> db/rtti.json, db/classes.md
  disasm.py        capstone 线性扫描 + 递归下降函数识别
  analyze.py       总控：字符串/函数/虚表/命名 -> db/*.json
  sourcemap.py     从 assert 路径恢复原始源码结构 -> docs/source-map.md
  query.py         检索：按字符串找函数、查调用者、列热点
  build-msvc.bat   用本机 MSVC 编译 src/

db/              分析数据库（JSON，脚本可重跑）
  rtti.json  classes.md  functions.json  strings.json
  vtables.json  names.json  sources.json  summary.md

docs/            逆向笔记
  binary-baseline.md   二进制基线与关键子系统定位
  source-map.md        原始 .cpp -> 代码地址区间对照表
  multicore-plan.md    多核改造方案

src/             C++ 还原代码
  core/     Types.h / Abstract.h        基础类型与对象体系
            Subsystems.h/.cpp           Locomotor/战斗/阵营/AI脚本/INI/界面/网络
  map/      Cell.h / Map.h/.cpp         格子与地图
  ai/       PathFinder.h/.cpp           两级寻路（含并行实现）
  engine/   FrameQueue.h/.cpp           锁步帧队列
            GameLoop.h/.cpp             主循环（分阶段）
  io/       FileSystem.h/.cpp           FileClass 家族 / MIX 打包 / Pipe-Straw
  threading/ TaskSystem.h/.cpp          线程池（新增，原引擎没有）

tests/           预留
```

---

## 快速开始

### 1. 跑分析（可选，db/ 已有结果）

依赖装在 `tools/pylibs`（已随仓库排除，需重新安装）：

```bash
python -m pip install --target tools/pylibs capstone pefile
python tools/analyze.py      # 约 17 秒，产出 db/*.json
python tools/rtti.py         # 产出 db/rtti.json, db/classes.md
python tools/sourcemap.py    # 产出 db/sources.json, docs/source-map.md
```

常用检索：

```bash
python tools/query.py --string "findpath"   # 按字符串找函数
python tools/query.py --func 0x0055E420     # 看函数详情
python tools/query.py --hot 30              # 指令数最多的 30 个函数
python tools/query.py --vtables 20          # 最大的 20 张虚表
```

### 2. 编译还原代码

```bash
python tools/build.py --run     # 一条命令编出 ra2core / ra2view / ra2game
# 或：tools\build-msvc.bat
# 或：cmake -B build -S . && cmake --build build --config Release
build\ra2core.exe
```

> `/utf-8` 已写进三个构建入口，**别删**：源码是 UTF-8 无 BOM，中文 Windows
> （ACP=936）下 `cl.exe` 会按 GBK 解码，直接编不过（C2001/C3688/C4819）。
> `build.py` 自己拼 INCLUDE/LIB（不碰注册表），适合受限环境。

预期输出：

```
OK  路径 256 条（命中 224），串行 85.23 ms，并行 22.16 ms，加速 3.85x
OK  锁步帧队列行为正确，帧 CRC = 0x15307EAB
```

第一条是**确定性回归测试**：并行结果与串行逐条比对，不一致即失败。
任何并行化改动都必须先让它通过。

### 3. 跑起来

```bash
# 素材查看器：给单位名，模型/炮塔/炮管/HVA 全自动
build\ra2view.exe --gamedir "<游戏目录>" --unit YTNK --turretyaw 40 --offscreen

# 游戏本体：进战场（--gamedir 会按游戏自己的挂载顺序挂整套素材包，
# 并自动从 Maps\ 里挑一张默认图）
build\ra2game.exe --gamedir "<游戏目录>" --offscreen --out build/game.raw
build\ra2game.exe --gamedir "<游戏目录>"          # 开窗口玩
```

`--gamedir` 走 `GameInstall::Resolve`（`src/core/GameVersion.cpp`）：
枚举 `expandmd%02d.mix`、按号升序挂载，缺哪个包会直接报出来。
`build\ra2core.exe --detect "<游戏目录>"` 可以先看一眼挂载列表。

渲染层的诊断开关（都是环境变量，默认关）：

| 变量 | 作用 |
|---|---|
| `RA2_BATCH_STATS=1` | 每帧末打一行批渲染统计（批次数 / 精灵数 / 单独 draw 次数） |
| `RA2_NO_SPRITE_BATCH=1` | 关掉批渲染，走"一精灵一纹理一 draw"的老路 |
| `RA2_BATCH_ONE=1` | 每条实例单独一批（诊断偏移类问题用） |
| `RA2_BATCH_DEBUG=1` | 打印描述符句柄与每批的首条实例数据 |
| `RA2_D3D_DEBUG=1` | 开 D3D12 调试层 |

**批渲染的正确性判定**：同一个场景跑两遍，`RA2_NO_SPRITE_BATCH=1` 的
回读图必须与默认（批渲染开着）**逐字节一致**。这是它唯一的硬证据 ——
画面"看着对"不算。

---

## 关键结论

1. **引擎代号 Sun**，与《泰伯利亚之日》同源，2001 年 MSVC 6 时代构建，
   无可用符号文件（`tsun.dbg` 缺失）。
2. **RTTI 给了 988 个真实类名**，但没有类层次数据
   （`CompleteObjectLocator` 为 0），所以虚表和类名无法直接对应。
3. **锁步模型**：15 FPS 定步长，每帧等齐所有玩家输入才推进，靠 CRC 检测失步。
   多核改造只能在一帧内部做，且不得引入顺序相关的归约。
4. **寻路是最佳突破口**：两级（分层 + 常规 A*），请求之间完全独立。
   实测并行 3.5~4.0x，但前提是**先消除每次寻路的缓冲区分配**——
   否则并行反而更慢（实测 0.89x）。
5. **MIX 归档是加密的**：实测 `ra2md.mix` / `ra2.mix` / `expandmd01.mix`
   头部标志 `0x00030000`（加密+校验和）、`MULTIMD.MIX` 为 `0x00020000`（加密）。
   在 `MULTIMD.MIX` 上枚举所有候选头部偏移都找不到自洽的 (文件数, 数据区长度)，
   说明**头部与索引整体是 Blowfish 密文**，必须先还原会话密钥才能读。
   无加密的经典 MIX（标志 0）已完整支持。

详见 `docs/binary-baseline.md` 与 `docs/multicore-plan.md`。

---

## 已知限制

- 结构体的字段偏移与 `sizeof` **尚未**从二进制确认，代码中所有未确认处
  都标了 `TODO(逆向)` 并给出验证方法，不以猜测填充。
- 虚函数表的槽位顺序未还原（原因见上面结论 2）。
- `Hierarchical_Find_Path` 目前退化为常规 A*：分层规划的区域图
  （`MapRegionClass` / `PlanningNodeClass` 等）尚未还原。
- 线性反汇编在函数边界上仍有噪声，`db/functions.json` 里 `src=gap` 的
  条目是兜底产物，统计时应排除。
