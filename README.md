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
| 反汇编 | 1,388,640 条指令（锚点重同步 + **跳表本体作数据区**后；不带这两样是 1,392,506 条，差额就是从跳表字节里"解"出来的垃圾指令） |
| 函数 | **20,138 个**（分档播种 + 两遍锚点重扫 + 跳表遮罩，每个入口都带 `tier`）；见 §3.3 |
| 类名 | **988 个真实类名**（来自 RTTI，非猜测），其中非模板类 328 个 |
| 原始源文件 | **58 个**（来自 assert 的 `__FILE__` 字符串），构建机路径 `D:\ra2mdpost\` |
| 关键子系统 | 主循环、锁步帧队列、寻路、地图、网络包泵均已定位到具体 VA |
| C++ 骨架 | 可编译、可运行，寻路并行实测 **3.5~4.0x** 且结果与串行逐位一致 |
| 对象字段偏移 | **646 个类 / 6965 条**，从构造函数里挖出来的；四条独立证据交叉验证，387 处 RTTI 位移一条不漏 |
| 对象字段**名** | **5 个类 / 381 条**，从二进制自己的 `Read_INI` 里读出来的 —— 键名两侧同偏移，自证；300 条另被构造函数扫描独立看到且宽度一致 |
| 对象模型 | **8 个类 / 1161 条成员**铺成能编译的结构体（`src/re/ObjectModel.h`）；每条字段一条 `static_assert`，`pack(1)` + 显式填充，`offsetof` 与实测偏移逐条相等；末端是 `sizeof` 的下界 |
| 函数边界 | **20,138 个函数**（`src/re/FuncTable.h`）；`0x401000–0x7C0000` 段入口 **100%** 落在 16 字节边界（19,274/19,274，两条独立通道各算一遍）；区间两两不重叠；旧清单 `call` 档 6,635 条一条不少；`.text` 字节账 代码 95.6% / 填充 3.8% / 数据 0.4% / **未知 0.17%**（不猜不填，逐段列出） |
| 基本块 / CFG | **203,071 个基本块 / 287,359 条边**（`db/blocks.json` + `src/re/BlockTable.h`）；多块函数 11,925、单块 8,213；跳表/表外 `tail` 边 2,706；重走失败 0；独立对账器 `tools/cfgcheck.py` **40/40**（不复用扫描器代码，从磁盘重读 JSON 与头文件各算一遍再互钉） |
| 已还原模块 | 对象体系、地图/格子、两级寻路、锁步帧队列、主循环、文件系统(MIX)、INI、Locomotor、战斗、阵营/经济、AI 小队与触发、界面、网络接口 |

---

## 目录结构

```
tools/           逆向分析工具（Python）
  peimage.py       PE 只读封装，RVA<->文件偏移换算
  rtti.py          RTTI 类型描述符提取 -> db/rtti.json, db/classes.md
  disasm.py        capstone 线性扫描 + 锚点重同步 + **数据区遮罩**（跳表本体不当指令）
  funcscan.py      S1 函数边界：分档播种 + 过程内递归下降 + 空隙通道
                   -> db/funcs.json + src/re/FuncTable.h + docs/functions.md
  cfgscan.py       S2 基本块与 CFG（leader 切块 + 四类边 + 跳表展开）
                   -> db/blocks.json + src/re/BlockTable.h + docs/cfg.md
  cfgcheck.py      S2 **独立对账器**：不复用 cfgscan 的代码路径，从磁盘重读
                   db/blocks.json 与 BlockTable.h 各算一遍再互钉（40 项）
  analyze.py       总控：字符串/函数/虚表/命名 -> db/*.json
  sourcemap.py     从 assert 路径恢复原始源码结构 -> docs/source-map.md
  query.py         检索：按字符串找函数、查调用者、列热点
  mixdump.py       MIX 解密（加密/明文/嵌套）+ 深度索引（C++ 侧的对账基准）
  inidump.py       INI 参考解析 + 逐行对账
  inikeys.py       清点 INI 里**真实出现**的键（P2 打表的证据来源）
  techno.py        类型表打表的独立实现（与 C++ 逐行 diff）
  fieldscan.py     从构造函数里抽对象字段偏移 -> db/fields.json + src/re/FieldOffsets.h
  fieldname.py     从 Read_INI 的「键名两侧同偏移」抽字段**名**
                   -> db/fieldnames.json + src/re/FieldNames.h
  sizeofscan.py    从 `push N; call new` 配对抽类 sizeof -> db/sizes.json
                   （用 fieldscan 的字段末端硬过滤候选，两条工具互为输入）
  layout.py        把偏移表 + 名字表 + RTTI 继承铺成能编译的结构体
                   -> src/re/ObjectModel.h + db/layout.json
  build-msvc.bat   用本机 MSVC 编译 src/

db/              分析数据库（JSON，脚本可重跑）
  rtti.json  classes.md  functions.json  strings.json
  vtables.json  names.json  sources.json  summary.md
  sizes.json（类 sizeof 实测）  fields.json（类字段偏移）
  fieldnames.json（字段名 + 覆盖面对账 + 争议留痕）
  layout.json（对象模型：每个类的成员序列、来源、填充）
  funcs.json（S1：20,138 个函数，tier / exits / calls / tails / ret_bytes / frame）
  blocks.json（S2：基本块与 CFG，多块函数在 `funcs`、单块在 `single`/`single_edges`）

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

**P2 数据层（INI → 类型表）**：

```bash
# 把 rules / art / sound / theme 全量装进类型表，并逐键对账
# （--gamedir 只有 ra2game/ra2view 认；ra2core 这里是显式列包）
G="<游戏目录>"
build\ra2core.exe --typetable "$G/ra2.mix" "$G/ra2md.mix" "$G/expandmd01.mix" \
    "$G/expandmd94.mix" "$G/expandmd95.mix" "$G/expandmd96.mix" \
    "$G/expandmd97.mix" "$G/thememd.mix" \
    --raw build/type_raw.txt --summary build/type_summary.txt --top 40

# 独立实现对账（Python 从 MIX 字节重新读起，与上面那份逐行 diff）
python tools\techno.py "$G" --check build/type_raw.txt

# 键的清点（数据即证据：原版到底写了哪些键）
python tools\inikeys.py "$G"
```

`--typetable` 的通过判据是**两行**：`逐键对账：不一致 0 处`，以及
`X-macro 声明 N 个键，其中 0 个在数据里一次都没出现`
（后者是防"键名打错"的闸 —— 打错的键永远不会命中，也不会报任何错）。
`--top N` 表尾会印"还没类型化的键 Top N"，补字段照着加就行。

### 3.1 对象字段偏移（P3 的地基）

```bash
# 从 gamemd.exe 的构造函数里抽字段偏移，写 db/fields.json 等三个产物
python tools\fieldscan.py

# 只看一个类，或列核心继承链
build\ra2core.exe --layout UnitClass
build\ra2core.exe --layout
```

产出：`db/fields.json`（646 个类 / 6965 条）、`docs/fields.md`（人读版）、
`src/re/FieldOffsets.h`（核心继承链，自动生成，`ra2core` 冒烟测试里做硬判据）。

**验收判据是四条独立证据**，不是"看着合理"：

| 证据 | 判据 | 实测 |
|---|---|---|
| sizeof（`db/sizes.json`，另一套分析） | 每个类的字段末端必须落在它自己的 sizeof 之内 | 207 通过 / 435 无基准 / **4 越界**，每条带判定写进 `docs/fields.md` |
| RTTI 嵌入基类位移 `mdisp`（来自 PE 的 RTTI 段） | 该在 .text 里找得到 `mov [对象+mdisp], <基类主虚表>`，或 `lea ecx,[对象+mdisp]; call <基类构造函数>` | 可判定的 **159 / 159 = 100%** |
| 同上，当基类**没有虚表**时 | 退一步：该继承链上某个类的构造函数在**有效偏移 mdisp** 上写过东西 | 其余 **228 / 228** —— 387 处一条不漏 |
| 继承关系 | 沿继承链字段末端必须严格递增 | 固化进冒烟测试，见 `Layout_Check()` |

**为什么"可判定子集"要单独算**：387 处 `mdisp` 里只有 159 处的基类拥有自己的
RTTI（COL）。其余 228 处的基类是两类**二进制里没有虚表**的东西 —— 纯抽象接口
（`IUnknown` / `IRTTITypeInfo` / `ILocomotion` / `IPiggyback` / …，MSVC 对
"没有非内联虚函数、又从不被完整构造"的类既不生成虚表也不生成 COL），以及
根本没有虚函数的普通子对象（`FlasherClass` / `StageClass`）。实测
`TechnoClass::ctor@0x6F2B40` 在 240 上写的是 `mov dword ptr [esi+0xf0], ebx`
（`ebx` 就是 0），根本不是虚表；`AnimClass::ctor@0x421EA0` 在 172 上同理。
把它们算进分母，会把"可判定的全中"稀释成"387 处只中 41%" —— 那是自欺。

sizeof 与继承链两条已固化进 `ra2core` 冒烟测试（`Layout_Check()`），不成立即
返回非 0。

**发现的问题不静默吞掉**：越界的类会带上判定写进 `docs/fields.md`，不悄悄放过。
实测就是这样发现 `db/sizes.json` 里 `CCFileClass` 的 sizeof 取错了 —— 投票选出
36（4 票），而字段末端是 108；108 本来就在它自己的候选列表里，只是票少。
`tools/sizeofscan.py` 现在用字段末端**硬过滤候选**（字段偏移是对象内偏移，
必须落在 sizeof 之内），改过的条目打 `calibrated` 标记并**无条件**进 C++ 表
（证据比投票硬）；反过来，如果一个合法候选都不剩，就**不改**、只记 `note`
留给人看 —— 那说明两条数据里有一条本身错了，乱猜比不改更坏。

镜像路径由 `tools/peimage.py` 自动定位（环境变量 `RA2_GAMEMD` 优先）。
**跨机器复现认入口点与链接时间戳，不认路径。**

### 3.2 对象字段**名**（P3 第二步）

有了偏移只算"这个位置有个 4 字节的字段"，还是不知道它叫什么。名字从二进制
**自己的** `Read_INI` 里读：

```bash
python tools\fieldname.py

build\ra2core.exe --fieldnames
build\ra2core.exe --fieldnames TechnoTypeClass
```

产出：`db/fieldnames.json`、`docs/fieldnames.md`、`src/re/FieldNames.h`。

**为什么这条路是通的** —— `XxxTypeClass::Read_INI(CCINIClass&)` 的编译形态极规整，
实测 `TechnoTypeClass::Read_INI@0x712170`：

```asm
mov eax, dword ptr [ebp + 0x610]   ; 缺省值：先从字段里读出来
push eax
push 0x825470                      ; "Cost"            <- 键名
push ebx                           ; section 名
mov ecx, esi                       ; CCINIClass*
call 0x5276D0                      ; ReadInteger
mov dword ptr [ebp + 0x610], eax   ; 结果存回**同一个**字段
```

**同一个偏移在键名两侧各出现一次**，这一条本身就是自证 —— 编译器是从
`Cost = ini.ReadInteger(section, "Cost", Cost);` 生成的。`ra2core` 的
`FieldNames_Check()` 把它做成了硬判据，锚点包括 `ObjectTypeClass` 的
`Armor@0x9C` / `Strength@0xA0`、`TechnoTypeClass` 的
`Cost@0x610` / `TechLevel@0x634` / `Sight@0x5E8` / `Points@0x728`。

| 环节 | 做法 | 实测 |
|---|---|---|
| 找 `Read_INI` | 数每个函数引用了多少个**真实存在**的 INI 键名，超阈值即认定。**槽号是发现出来的，不是规定的** | 5 个函数，全部落在虚表**槽 #25**；TechnoTypeClass / BuildingTypeClass / UnitTypeClass 三张互不相干的表独立收敛到同一槽号 |
| 键集从哪来 | `tools/inikeys.py` 从真实 INI 文件清点，不做语义解释 | rules 476 种 / art 206 种 |
| 类型怎么定 | 只有 `ReadBool`(0x5295F0) 与 `ReadInteger`(0x5276D0) 两个入口被实测区分；其余一律 `?`，不留猜测 | `AmbientSound` 这类"值是整数"的键也从收字符串的入口过，硬标 `str` 就是编造 |
| 交叉验证 | 名字侧宽度必须与构造函数扫描的字段宽度一致；偏移必须落在 sizeof 之内 | **300 条**同时被构造函数扫描独立看到且宽度一致；宽度不符 **0**；sizeof 越界 **0** |

覆盖面对账（`db/fieldnames.json` 的 `coverage` 段）：TechnoTypeClass 252 个键配上
250、BuildingTypeClass 193→181、UnitTypeClass 44→42、InfantryTypeClass 25→25、
ObjectTypeClass 19→15。**没配上的键名不进常量表**，但会在 `docs/fieldnames.md`
里逐个列出来 —— 不让"覆盖率"看起来像"全量"。

**归属用"提供实现的类"，不是"虚表里挂着这个函数指针的类"。** 后者会把继承来的
`Read_INI` 记成派生类自己的：`0x5F92D0` 同时挂在 `ObjectTypeClass` 和
`IsometricTileTypeClass` 的虚表槽 #25 上，只有一个实现 —— RTTI 说后者继承前者，
所以实现者是**最浅**的那一个。按"最深"排（初版就是这么排的）会得出
"IsometricTileTypeClass 有 15 个命名字段"，而这 15 个字段的偏移全在
`ObjectTypeClass` 的 0x9C~0x238 里。改对之后它自有命名字段 = 0，
这一点随后被对象模型的交叉核对独立撞上（见 §3.3）。没覆盖 `Read_INI` 的类
进 `read_by` 字段留痕，回答"谁在读这些键"用。

够不到的三种形态写进了文档，不靠调宽规则硬凑数字：

1. **结果经过变换再存**。`Speed` 读出后先钳 100、再 ×256/100、再钳 255，最后存进
   `[ebp+0x678]` —— 离键名很远。按"键名旁边那条存"会错认成 `0x630`，而 `0x630`
   其实属于**上一条**键（它的结果存被调度器插到了这里）。宁可**不命名**。
2. **数组字段**。32 个 `XxxTurretIndex` / `XxxTurretWeapon` 都往 `TurretType[i]`
   里写，索引在寄存器里，于是 32 条键名争一个基偏移。
3. **目的地不是本对象的字段**。字符串键先读进栈上临时缓冲再 `strcpy`，中间隔了
   `strlen` / `rep movs`。

同一个偏移被两个键名主张时，取舍按**证据强度**分档，不按"有没有人争"一刀切：
「双向」记录（缺省值与结果同偏移）保留，「单向」记录若被争则弃用。这条规则是被
冒烟测试的锚点断言逼出来的 —— 最初一刀切"有争议就丢"，把手工核对过的
`Cost@0x610` / `TechLevel@0x634` 一起丢了，测试立刻红。

### 3.3 对象模型（P3 第三步：从查询表到能编译的结构体）

前三步都是**查询表**：按 (类, 偏移) 查宽度、查名字、按类查 `sizeof`。
查询表回答不了 `obj.COST = 1000`，也说不出 `sizeof(TechnoTypeClass)`。

```bash
python tools\layout.py

build\ra2core.exe --model
build\ra2core.exe --model TechnoTypeClass
```

产出：`src/re/ObjectModel.h`、`db/layout.json`、`docs/object-model.md`。
把 (偏移, 宽度, 名字) 按 RTTI 的继承链铺成**连续内存布局**：

```cpp
#pragma pack(push, 1)                 // 二进制里的布局是事实，不让编译器改写它
struct ObjectTypeClass : public AbstractTypeClass {
    u8  _pad_0065[51];               // +0x65  没有证据
    ...
    u32 ARMOR;                       // +0x9C  INI: ARMOR
    u32 STRENGTH;                    // +0xA0  INI: STRENGTH
    ...
};
struct TechnoTypeClass : public ObjectTypeClass {
    ...
    u32 COST;                        // +0x610 INI: COST
    ...
};
#pragma pack(pop)
RA2_OFF(TechnoTypeClass, COST, 0x610);   // static_assert(offsetof(...) == 0x610)
```

| 环节 | 做法 | 实测 |
|---|---|---|
| 字段集合 | 构造函数写过 ∪ Read_INI 读写过 | 两条通道宽度不一致 **0** 处 |
| 铺法 | 字段之间插 `_pad_XXXX[n]`，使每个已知字段恰好落在它的偏移上 | 8 个类 / **1161** 条成员，严丝合缝铺满，无洞无重叠 |
| 对齐 | `#pragma pack(push,1)`；MSVC 下 `offsetof`/`sizeof` 精确等于铺出来的偏移 | `tools/_probe_offsetof.cpp` 造 4 层继承链实测过，不是推测 |
| 钉死 | 每条字段一条 `static_assert(offsetof(...) == 偏移)` | 改坏布局**编译不过**，不会悄悄漂移 |
| 交叉核对 | `Model_Check()` 拿 `FieldNames.h` 的每条命名字段回来在模型里找同名成员 | 381 条全对齐（其中 1 条落在继承来的字段上） |

算出来的数（**末端是 `sizeof` 的下界，不是 `sizeof`**）：

| 类 | 父类 | 起点 | 末端 | 已知字段 | 有名字 | 填充 |
|---|---|---|---|---|---|---|
| `ObjectTypeClass` | `AbstractTypeClass` | 0x65 | 0x294 | 65 | 15 | 392 |
| `TechnoTypeClass` | `ObjectTypeClass` | 0x294 | 0xDF4 | 460 | 178 | 1490 |
| `BuildingTypeClass` | `TechnoTypeClass` | 0xDF4 | 0x1792 | 311 | 170 | 1640 |
| `UnitTypeClass` | `TechnoTypeClass` | 0xDF4 | 0xE5F | 40 | 10 | 4 |
| `InfantryTypeClass` | `TechnoTypeClass` | 0xDF4 | 0xECC | 65 | 22 | 34 |

`_pad_XXXX` **不代表**那些字节是空的，只代表"构造函数和 `Read_INI` 都没写到那里"。
`UnitTypeClass` 覆盖率低是这个意思，不是"它只有 130 字节有内容"。

**独立对账**：`ObjectTypeClass` 的末端（扫构造函数得到 0x294）与 `TechnoTypeClass`
自有命名字段的最小偏移（扫 `Read_INI` 得到 0x294）**正好吻合** —— RTTI 只说
"继承"，不给边界在哪，边界是两条独立通道各自算出来又对上的。

**这一步还逼出了一个上一轮的归属错误。** `Model_Check()` 第一次跑就红：
`IsometricTileTypeClass` 的 `ARMOR@0x9C` 在模型里找不到成员。查下去是
`fieldname.py` 把函数归属按"深→浅"排了 —— 而 `0x5F92D0` 同时挂在
`ObjectTypeClass` 和 `IsometricTileTypeClass` 的虚表槽 #25 上，同一个函数体不可能
被两个类各自实现，实现者是**最浅**的那个。修完 `FieldNames.h` 从 6 类 396 条变成
**5 类 381 条**。细节见 `docs/re-ledger.md`。

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

- 结构体的字段偏移与 `sizeof` 已从二进制确认（见 §3.1）；字段**名**只覆盖
  各 TypeClass 的 `Read_INI`（5 个类 / 381 条，见 §3.2），其余类的字段仍是
  无名偏移 —— 代码中所有未确认处都标了 `TODO(逆向)` 并给出验证方法，
  不以猜测填充。
- 对象模型（§3.3）里 `_pad_XXXX[n]` 的填充**只代表"构造函数和 `Read_INI`
  都没写到那里"**，不代表那些字节是空的；类末端是 `sizeof` 的**下界**，
  不是 `sizeof`。覆盖率低不等于字段少。
- 对象模型只覆盖 4 层继承链上的 8 个 `*TypeClass`。`BuildingClass` /
  `CellClass` / `HouseClass` 这些静态对象池里的类 `sizeof` 拿不到
  （不是 `operator new` 出来的），字段证据也还没挖。
- `offsetof` 用在非 standard-layout 类型上是**条件支持**的（模型里每一层基类都
  有数据成员）。MSVC 给出正确结果且被 900+ 条 `static_assert` 钉过 —— 是
  "在目标编译器上已验证"，不是"标准保证"；换编译器要重跑这一层。
- 虚函数表的槽位顺序未还原（原因见上面结论 2）。
- `Hierarchical_Find_Path` 目前退化为常规 A*：分层规划的区域图
  （`MapRegionClass` / `PlanningNodeClass` 等）尚未还原。
- 线性反汇编在函数边界上仍有噪声，`db/functions.json` 里 `src=gap` 的
  条目是兜底产物，统计时应排除。
