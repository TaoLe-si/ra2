# gamemd.exe 二进制基线

分析对象：`D:\westwood\RA2YR\gamemd.exe`
分析工具：`tools/`（全部可重跑，见 README）

## 1. 目标确认

YR 安装目录里有两个看起来像主程序的文件：

| 文件 | 大小 | 身份 |
|---|---:|---|
| `RA2MD.EXE` | 94,208 | 启动器（launcher） |
| `YURI.EXE` | 977,633 | 启动器 |
| **`gamemd.exe`** | **4,813,072** | **真正的游戏主程序** |

因此逆向目标锁定 `gamemd.exe`。

## 2. PE 基本信息

| 项 | 值 |
|---|---|
| 机器 | x86 (0x14C)，PE32 |
| ImageBase | `0x00400000` |
| 入口点 VA | `0x007CD80F` |
| SizeOfImage | 0x793000（约 7.8 MB） |
| 链接时间戳 | `0x3BDF544E` = **2001-10-31 01:30:54 UTC** |
| Characteristics | `0x30F` —— **重定位表已剥离** |
| 子系统 | 2（Windows GUI） |
| 校验和 | `0x004A6EAB` |
| 链接器版本 | 6.00 |

> **重定位表已剥离**这一点很重要：镜像基址永远是 `0x00400000`，
> 静态分析得到的地址就是运行时地址，不需要任何基址换算。

## 3. 节区

| 名称 | RVA | 虚拟大小 | 原始大小 | 特征 |
|---|---:|---:|---:|---|
| `.text` | 0x00001000 | 0x003DF38D | 0x003E0000 | 执行/读 |
| `.rdata` | 0x003E1000 | 0x00030074 | 0x00031000 | 只读 |
| `.data` | 0x00412000 | 0x00367BE4 | 0x0006C000 | 读写 |
| `.rsrc` | 0x0077A000 | 0x00018128 | 0x00019000 | 只读 |

数据目录只有 4 项有效：Import(0x40F0E0)、Resource(0x77A000)、
Debug(0x3E1610)、IAT(0x3E1000)。**没有导出表、没有异常表、没有重定位表**。

## 4. 版本资源与构建信息

| 字段 | 值 |
|---|---|
| CompanyName | Westwood Studios |
| FileDescription | Main executable for Yuri's Revenge |
| ProductName | Command & Conquer : Yuri's Revenge |
| FileVersion / ProductVersion | **1.11** |
| InternalName | **Sun** |
| OriginalFilename | **Sun.exe** |
| LegalCopyright | Copyright © 2001 Westwood Studios |

调试目录（type=4，MISC）里还留着：

- 符号文件名：**`tsun.dbg`**
- 原始可执行名：**`SUN.exe`**

> **结论**：YR 沿用的是 Westwood 内部代号 **Sun** 的引擎 —— 与《泰伯利亚之日》
> 同一套内核。这解释了为什么代码里能同时看到 Tiberian Sun 时代的遗留类名
> （`TiberianSunClassFactory`、`TiberiumClass`、`VeinholeMonsterClass` 等）。
> 还原时可以合理参考 TS 引擎的公开资料来交叉验证，但不能照搬。

符号文件 `tsun.dbg` 随二进制一起分发是不太可能的，本目录下没有它，
因此**没有 PDB/DBG 符号可用**，全部靠静态分析。

## 5. 导入表

15 个 DLL，共约 350 个导入。与本项目强相关的几组：

**图形 / 音频**
- `DDRAW.dll` —— 仅 1 个导入（`DirectDrawCreate`），其余接口运行时动态获取
- `DSOUND.dll` —— 仅 1 个导入（`DirectSoundCreate`）
- `binkw32.dll` —— 13 个（`_BinkDoFrame@4`、`_BinkNextFrame@4` 等，过场动画）

**线程与同步**（说明原引擎**已经多线程**了）

| 类别 | 函数 |
|---|---|
| 线程 | `CreateThread`、`SetThreadPriority`、`GetCurrentThread`、`GetCurrentThreadId` |
| 临界区 | `InitializeCriticalSection`、`EnterCriticalSection`、`LeaveCriticalSection`、`DeleteCriticalSection` |
| 互锁 | `InterlockedIncrement`、`InterlockedDecrement` |
| 互斥/事件 | `CreateMutexA`、`OpenMutexA`、`ReleaseMutex`、`CreateEventA`、`SetEvent`、`ResetEvent`、`OpenEventA` |
| 计时 | `QueryPerformanceCounter`、`QueryPerformanceFrequency`、`timeSetEvent`、`timeKillEvent`、`SetTimer`、`KillTimer` |

**网络**：`WSOCK32.dll`（19 个导入）。

> 关键判断：原引擎用线程做的是**辅助工作**（网络收包、音频、文件 IO），
> **游戏逻辑本身是单线程**的，靠锁步帧队列串行推进。这正是可以挖多核的地方。

## 6. RTTI：988 个真实类名

`tools/rtti.py` 扫出 **988 个 MSVC TypeDescriptor**：

- 所有描述符的 `pVFTable` 都等于 `0x007F9594`、`spare` 都为 0
  —— 符合 MSVC `type_info` 的标准布局，可确认是编译器生成的真实类型名；
- 其中非模板类 **328 个**，模板实例 660 个。
- 完整清单：`db/classes.md`。

但这些描述符**几乎没有被数据引用**（988 个里只有 1 个被某个 dword 指向），
`RTTICompleteObjectLocator` 数量为 **0**。

> 意味着：编译时**没有生成可用于 `dynamic_cast` 的 RTTI 类层次图**。
> 类名可以放心用作还原时的命名依据，但**不能**用来把虚表和类名对应起来。
> 虚表只能用 `tools/analyze.py` 定位（已找到 1026 张，其中 800 张被代码引用过），
> 再用 `tools/vtmap.py` 做继承推断与人工确认，详见 §11。

## 7. 静态分析结果

`tools/analyze.py` 输出（`db/summary.md`）：

| 项 | 数量 |
|---|---:|
| 反汇编指令 | 1,392,506 |
| 识别函数（entry/call/prologue 播种） | 6,740 |
| 识别函数（含线性兜底） | 28,827 |
| 调用点 | 68,244 |
| 虚函数表 | 1026（其中 800 张被代码引用） |
| 字符串 | 16,382 |
| 依据内嵌 `Class::Method` 字符串可命名的函数 | 18 |

线性扫描的噪声主要来自 .text 里的跳转表和内联数据；
`src=gap` 的函数是兜底产物，仅供参考，统计时应当排除。

## 8. 已定位的关键子系统

| 子系统 | VA | 证据 |
|---|---|---|
| 主循环 | `0x0055E420` | 引用 `D:\ra2mdpost\MainLoop.CPP`；986 指令 |
| 主循环外層 | `0x0055DEE0` | 235 指令，调用上面的函数 |
| 网络包泵 | `0x00541820` | "Multicast channel has gone bad"、"Adding DATA_ACK packet..." |
| **锁步帧核心** | `0x006475F0` | "Processing Ticks:%03d Frames:%03d"、"Wait_For_Players returned %d"、"Frame %d, my sent = %d" |
| 命令列表执行 | `0x00648710` | "Failure executing DoList"；1444 指令 |
| **寻路** | `0x0042C900` | 同时引用 "Regular findpath failure" 与 "Hierarchical findpath failure" |
| 地图初始化 | `0x005659F0` | "MapClass::Init_Clear done" |
| 雷达 | `0x00652DE0` / `0x00652E90` | "RadarClass::Init_Clear / Init_For_House" |
| CRC 校验 | 多处 | "*************** Building CRCs"、"Sides CRCs"、"Waypoint Path CRCs" |

## 9. 原始源码结构

`assert` 宏留下的 `__FILE__` 字符串暴露了构建机路径 `D:\ra2mdpost\`。
`tools/sourcemap.py` 恢复出 **58 个原始源文件**、锚定 **213 个函数**。
完整对照表：`docs/source-map.md`。

其中与本项目最相关的：

| 原始文件 | 代码区间 | 说明 |
|---|---|---|
| `MainLoop.CPP` | `0x55E420`-`0x55F1D3` | 主循环 |
| `Queue.CPP` | `0x6475F0`-`0x652394` | 锁步帧队列 |
| `House.CPP` | `0x4F9B70`-`0x50AF07` | 阵营 |
| `Scenario.CPP` | `0x683560`-`0x68966F` | 关卡 |
| `Tactical.CPP` | `0x6D3D10`-`0x6D4B4B` | 战术视图 |
| `PlanMgr.cpp` | `0x637270`-`0x63B044` | 分层寻路的规划管理器 |

## 10. 结论：锁步模型

综合 `Queue.CPP` 的日志字符串与网络导入，可以确定：

> RA2/YR 使用**确定性锁步（deterministic lockstep）**：
> 逻辑固定 15 FPS 推进，每帧必须收齐所有玩家输入才前进，
> 各机器同步的是**输入**而不是**状态**，用 CRC 比对检测失步。

这条结论直接决定了多核改造的边界，详见 `docs/multicore-plan.md`。

---

## 11. 虚函数表、类名与继承层次（RTTI 实证）

> **2026-09-15 更正**：本节早期版本称"本二进制没有类层次数据、`CompleteObjectLocator` 为 0"，
> 那是 `tools/rtti.py` 的两个 bug 造成的假象，结论已被推翻。现在类名与继承关系
> **直接来自二进制的 MSVC RTTI**，不是推断。

### 11.1 两个 bug：为什么一开始以为 RTTI 不可用

1. `TypeDescriptor` 的键存错。MSVC 的布局是
   `struct TypeDescriptor { const void* pVFTable; void* spare; char name[]; }`，
   名字在 **+8** 处。原代码把 `".?AVFoo@@"` 字符串的 RVA 当作描述符 RVA 存，
   而 `RTTICompleteObjectLocator.pTypeDescriptor` 指向的是描述符起始 —— 差 8 字节，
   988 个描述符一个都匹配不上，于是 `coll = 0`。
2. `parse_coll()` 把 `pTypeDescriptor` / `pClassDescriptor` 当 RVA 用，
   但这两个字段存的是 **VA**，没减 image_base。

修掉这两处后：`types=988, coll=1214, vtables=1209, classes=949`。

### 11.2 虚表定界：必须用 COL 反查，不能靠启发式

`tools/analyze.py` 的启发式（"值落在 .text 就延续"+128 槽上限）给出 1026 张虚表，
但**定界是错的**。对照 RTTI：`0x007EC258` 启发式判为 109 槽，实际是
`IsometricTileClass` 的 **122** 槽 —— 被截短了。

现在的做法（`tools/rtti.py`）：先用 COL 的引用点确定每张虚表的**确切起点**
（COL 位于 `vtable[-1]`），再向后延伸到"下一个已知虚表起点"或"值不在 .text"。

### 11.3 游戏对象模型（实证）

```
AbstractClass        24 槽  0x007E1F50   （多重继承 IPersistStream）
├─ ObjectClass      122 槽  0x007EF060
│  ├─ MissionClass  157 槽  0x007EDCC0
│  │  └─ RadioClass 161 槽  0x007F0508
│  │     └─ TechnoClass 309 槽 0x007F4960
│  │        ├─ FootClass     341 槽 0x007E8C94
│  │        │  ├─ UnitClass     344 槽 0x007F5C70
│  │        │  ├─ InfantryClass 343 槽 0x007EB058
│  │        │  └─ AircraftClass 341 槽 0x007E22A4
│  │        └─ BuildingClass    322 槽 0x007E3EBC
│  ├─ AnimClass 124 / BulletClass 125 / ParticleClass 123
│  └─ TerrainClass, OverlayClass, SmudgeClass, IsometricTileClass,
│     VoxelAnimClass, WaveClass, ParticleSystemClass,
│     BuildingLightClass, VeinholeMonsterClass        （各 122）
├─ AbstractTypeClass  27 槽  0x007E2000
│  └─ ObjectTypeClass 40 槽  0x007EF2D8
│     └─ TechnoTypeClass 48 槽 → {Aircraft,Building,Infantry,Unit}TypeClass
└─ HouseClass, CellClass, FactoryClass, TeamClass, TriggerClass, TagClass ... （24）
```

这张图和社区对 TS/RA2 引擎的描述完全吻合，反过来也互相印证了 RTTI 解析是对的。

产物：
- `src/re/ClassHierarchy.h` —— 自动生成，949 个类的 `ClassInfo` 表 +
  12717 个槽位，带 `ClassIndex` / `FindClassByVTable` / `VirtualEntry` / `IsDerivedFrom`。
  有了它，还原出的代码可以在**运行时用 vptr 反查类名**。
- `docs/class-hierarchy.md` —— 同一份数据的人类可读版。
- `db/virtuals.json` —— 每个类的槽位 → 函数入口。

### 11.4 3 槽虚表 = COM 接口

槽位分布里 3 槽有 126 张、4 槽 37 张。3 槽正好是 `IUnknown`
（QueryInterface / AddRef / Release），RTTI 里也确实有一大堆 `I*` 接口：
`IUnknown`、`IClassFactory`、`IPersist`、`IStream`、`ILocomotion`、
`IGameMap`、`IHouse`、`IRTTITypeInfo` 等。这些不是游戏对象类。

## 12. 类大小（sizeof）实测

`tools/sizeofscan.py`，数据 `db/sizes.json`，文档 `docs/sizes.md`，
代码产物 `src/re/ObjectSizes.h`。

### 12.1 方法

1. **定位 `operator new`**：它不在导入表里（CRT 静态链接）。用"被调用前紧跟
   `push <常量>` 次数最多的函数"来识别，实测命中 `0x007C8E17`；
   反汇编确认其函数体是 `push 1; push [esp+8]; call _nh_malloc`，无误。
2. **找构造函数**：函数开头 32 条指令内出现 `mov [<this>], <虚表 VA>`。
   注意 this 不一定还在 `ecx` —— MSVC 常先 `mov esi, ecx`（`AircraftClass`
   的构造函数就用 `esi` 写虚表），只认 ecx 会漏掉一批类。
3. **配对**三种形态：
   - `push N; call new` 之后紧邻写虚表（构造函数被内联）
   - `push N; call new` 之后 16 条指令内 `call <构造函数>`
   - 工厂函数：整个函数只有一处分配、只调用一个类的构造函数
     （`0x006Cxxxx` 那批 `new XTypeClass(ini)` 就是这种）

结果：**233 个类**拿到 sizeof，其中票数 ≥4 的 67 条写进了 `ObjectSizes.h`。

### 12.2 关键值

| 类 | sizeof | 票数 |
|---|---:|---:|
| `UnitClass` | 2280 | 23 |
| `InfantryClass` | 1776 | 21 |
| `AircraftClass` | 1752 | 4 |
| `AircraftTypeClass` | 3600 | 6 |
| `ObjectClass` | 172 | 3 |
| `TerrainClass` | 224 | 14 |
| `OverlayClass` / `SmudgeClass` / `IsometricTileClass` | 176 | 8 / 5 / 4 |

`AircraftClass` 的构造函数最后一个字段写在 `[esi+0x6D4]`（1 字节），
`0x6D5 + 1 = 0x6D8 = 1752` 正好等于 `new` 的大小 —— 自洽。

### 12.3 已知未解决

`BuildingClass`、`TechnoClass`、`FootClass`、`MissionClass`、`RadioClass`
这类**中间层/静态数组**的类拿不到：`BuildingClass` 的两个构造函数调用点上
都没有 `new`，说明它来自静态对象池（`.data` 的 BSS 部分有 3.5MB 未初始化数据）。
`CellClass`、`HouseClass`、`TeamClass`、`AnimClass`、`BulletClass` 同理。

试过用"引用 BSS 地址的函数里的 `add reg, <常量>`"反推数组步长，
噪声太大（同一个函数里几十个常量），**这条路没走通**，暂时搁置。
