# 全量还原路线（渲染层改用 DX12）

> 决定：**全量还原**（不是 DLL 注入打热补丁），渲染部分**不还原 DirectDraw，改用 DirectX 12 重写**。
> 本文把这个决定拆成可验收的阶段，并写清每阶段的"怎样算做完"。

---

## 0. 为什么这么定

### 0.1 为什么全量还原

热补丁方案（往 `gamemd.exe` 里注入 DLL 改函数）看起来省力，但对目标不成立：

| | 热补丁 | 全量还原 |
|---|---|---|
| 多核改造 | 只能在原单线程骨架上打洞，锁步/全局单例难以拆 | 可以从一开始按并行友好的结构写 |
| 对象布局 | 受制于原二进制字段偏移 | 自己定义，但必须与原存档/联机协议兼容 |
| 可维护性 | 每次改动都要反推原函数副作用 | 代码即规格 |
| 起步成本 | 低 | 高（这是代价） |

目标是**多核并行优化**，不是"让游戏跑起来"。热补丁做不到前半句，所以选全量。

### 0.2 为什么渲染用 DX12 而不是还原 DirectDraw

原渲染层（`Blitter` 家族 57 个派生 + `RLEBlitter` 51 个派生）是为 1999 年的
软件光栅化 + 显存 Blt 设计的：主表面/后备缓冲、逐像素 blit、锁表面写指针。
这套东西在现代系统上：

- DirectDraw 已被弃用，全屏独占模式在 Win10/11 上行为不稳定；
- 逐像素 CPU blit 无法吃到 GPU，反而成为并行化的瓶颈；
- 还原它要还原 100+ 个 Blitter 子类，工作量大且**与多核目标背道而驰**。

所以：**素材格式照原样还原（SHP/PAL/PCX/VXL/INI），但把"像素搬到屏幕上"这一步用 DX12 重写。**
Blitter 家族整体作废，不再还原。

代价：画面细节（阴影、透明、调色板重映射的精确行为）必须自己对齐，
验收标准是"截图与原版逐像素可比"，不是"代码结构相同"。

---

## 1. 现状盘点（已完成）

| 项 | 状态 | 依据 |
|---|---|---|
| MSVC RTTI 全量提取 | ✅ | 988 TypeDescriptor / 1209 虚表 / **949 个真实类名** |
| 继承层次 + 虚表槽位 | ✅ | `src/re/ClassHierarchy.h`，12717 槽位，可运行时 vptr 反查类名 |
| 关键类 sizeof | 🟡 | 233 个类有实测值；`UnitClass=2280` `InfantryClass=1776` `AircraftClass=1752` |
| **加密 MIX 解密** | ✅ | RSA(320bit) + Blowfish + Westwood CRC，C++ 与 Python 双实现结果一致 |
| **明文 MIX** | ✅ | flags 不带 0x00020000；地形归档全是这一类，之前完全看不见 |
| 嵌套 MIX | ✅ | ra2md.mix → 6 个子 MIX → 400 个叶子条目 |
| 文件名 CRC 反查 | 🟡 | 392 个 ID 中命中 178 个（45%），源自 `gamemd.exe` 字符串 |
| SHP(TS) 头/帧表 | ✅ | 8 字节头 + 每帧 24 字节，已由真实文件验证 |
| **SHP 帧数据解码** | ✅ | flags=0x0/0x1 未压缩、0x2/0x3 RLE-Zero；6373 帧全量 100% 命中 |
| **PAL 调色板** | ✅ | 768 字节，6→8 bit 用 `(v<<2)|(v>>4)`，索引 0 透明 |
| **TMP 等距地形** | ✅ | 660 个模板 / 2626 个 cell 全量对账通过；见 commit 44ac040 |
| **INI 解析** | ✅ | 4 个真实 INI、63790 行，与参考实现逐行一致；见 `src/data/Ini.{h,cpp}` |
| **PCX 解码** | ✅ | 255 个样本全解通；ra2.mix 的 160 个逐条 RGBA 哈希与参考实现 0 差异 |
| **HVA 体素动画** | 🟡 | 格式已破（见下），未落 C++ |
| **VXL 体素** | 🟡 | 头部字段已定，body 区编码未破，需反汇编加载器 |
| **DX12 渲染器** | 🟡 | 设备/交换链/离屏/调色板纹理/精灵上传已通；**地形已出画面**；批渲染未做 |
| 游戏逻辑 | ⬜ | 未开始 |

实测基准（每次改动后跑，保证不退化）：

```
ra2core.exe D:\westwood\RA2YR\ra2md.mix
OK   Westwood CRC 通过 4 个实测锚点
     flags=加密(Blowfish) 带校验和 0x00030000 条目=25 数据区=204527280 起=396 索引自洽（越界 0）
OK   按名找到 LOCALMD.MIX：off=6237216 size=4819480
OK   嵌套 MIX 打开成功：条目=187 数据区=4817120 越界=0
OK   递归取出密钥文件（151 字节），开头 = [PublicKey]

ra2core.exe                                    # 不带参数 = 全量冒烟
OK   INI 自检全部通过（18 项）
OK  路径 256 条（命中 224），串行 63.18 ms，并行 8.58 ms，加速 7.36x

ra2core.exe --ini D:\westwood\RA2YR\ra2md.mix 0x8218F9F4
OK   742958 字节 -> 1477 段 / 23379 条目 / 畸形行 2
     [InfantryTypes   ]   65 项   首=E1 末=YADOG
     [VehicleTypes    ]   80 项   首=AMCV 末=CIVP
     [BuildingTypes   ]  403 项   首=GAPOWR 末=CALUNR02
     [Animations      ]  611 项   首=TWLT100 末=YAPOWR_CD

ra2view.exe D:\westwood\RA2YR\ra2.mix --tmp 0x0F5D1D99 --offscreen
OK   可用地形模板 327 个，每格 60x30；拼了 225 个地块 -> 索引图 900x570
OK   离屏渲染 1024x768 -> build/frame.raw

ra2core.exe --pcxhash D:\westwood\RA2YR\ra2.mix
# 与 tools/pcxhash.py 的输出 diff 为空
OK   160 个 PCX，解码失败 0

ra2core.exe --pcx D:\westwood\RA2YR\ra2.mix 0x9B570683
OK   0x9B570683 800x600 planes=3 bpl=800 内嵌调色板=无
     # 写出 build/pcx/0x9B570683_800x600_p3.bmp，肉眼确认是 YR 加载画面（鹰徽 + 闪电）
```

---

## 2. 分阶段路线

### P0 — 素材层：把字节变成"有语义的数据"

| 任务 | 产出 | 验收标准 | 状态 |
|---|---|---|---|
| MIX 解密（加密） | `src/io/MixCrypto.cpp` | 索引自洽、嵌套可开、密钥文件可取 | ✅ |
| MIX 解密（明文） | `src/io/FileSystem.cpp` | 13 个地形大包全部可开 | ✅ |
| **SHP 解码** | `src/gfx/ShpFile.cpp` | 像素数 = 帧宽×帧高；flags 0/1/2/3 全覆盖 | ✅ 6373 帧 100% |
| **PAL 调色板** | `src/gfx/Palette.cpp` | 768 字节 → 256 色，第 0 色透明 | ✅ |
| **TMP 地形** | `src/gfx/TmpFile.cpp` | 2323 个模板与参考实现像素一致 | ✅ |
| **INI 解析** | `src/data/Ini.cpp` | 4 个真实 INI 逐行对账一致 | ✅ |
| **PCX 解码** | `src/gfx/PcxFile.cpp` | 160 个 PCX 的 RGBA 哈希与参考实现 0 差异 | ✅ |
| **HVA 体素动画** | `src/gfx/HvaFile.cpp` | 帧×肢体矩阵与文件长度严丝合缝 | 🟡 格式已破 |
| **VXL 体素** | `src/gfx/VxlFile.cpp` | 能取出体素坐标+色号 | 🟡 头部已破，body 未破 |
| 文件名反查补齐 | `db/mix-names.txt` | 命中率从 45% 提到 80%+ | 🟡 |

### P0 剩余：VXL / HVA 的已知线索（2026-09-15）

素材全在 `ra2.mix` 的顶层子归档 **0xA8548FD9**（607 条目 / 34.8MB）里：
184 个 VXL、183 个 HVA、148 个 PCX、46 个 SHP、11 个 INI、4 个 PAL。
另外 ra2md.mix 里有 37 个 VXL。**RA2 确实用体素**：`artmd.ini` 里
`Voxel=yes` 出现 92 次（Rhino `[HTNK]`、Apocalypse `[MTNK]`、Tesla `[TTNK]`、
`[HARV]`/`[CMIN]` 采矿车……都是体素）。

**HVA 已破**（无魔数，游戏是按 `名字 + ".HVA"` 拼文件名去查的；
`gamemd.exe` 里 `0x004268E4` 处 `".HVA"` 和 `"Failed to create VoxLib!"` 相邻）：

```
+0    16  char[16]   编译期残留的源文件路径（"N:\RA2\ASSETS\C\0" 之类，已截断）
+16    4  uint32     FrameCount
+20    4  uint32     LimbCount
+24   16*LimbCount   char[16] 每根肢体名（"BODY" / "TURRET" / "DUMMY01" …）
+..   48*Frame*Limb  float[12]  每帧每肢体的 3×4 变换矩阵
文件长度 = 24 + 16*L + 48*F*L
```
实测 ra2.mix：183 个候选**全部**精确吻合该算术，0 例外。
解出的矩阵是"单位旋转 + 平移"，例如 RAD03：translate(-1.352, -3.202, 186.803)。
分布：180 个是 (1 帧, 1 肢)；另有 (17,13)、(2,3)、(1,2) 各 1 个。
坑：有 4 个 SHP 因为巧合也能解出"合法"的 F/L，必须靠 `48*F*L` 算术拦掉。

**反汇编锚点**（`tools/query.py --string` 追出来的，下一步从这里进）：

```
0x00531680  Init_VoxLib()      561B/133 指令
    push "DPOD.VXL" -> CRC名 -> operator new(0x1C=28) -> 0x00755CD0
        结果存 [0xA8ECD8] = VoxLib 指针
    push "DPOD.HVA" -> operator new(0x10=16) -> 0x005BD570
        结果存 [0xA8ECDC] = MotLib（HVA）指针
    operator new 是 0x007C8E17，释放是 0x007C8B3D
0x00755CD0  VXL 装载包装（清零 28 字节对象，转调 0x00755DB0）
0x005BD570  HVA 装载包装（清零 16 字节对象，转调 0x005BD5C0）
0x005BD5C0  HVA 真装载器：call [file+0x1c](1) 打开 ->
            `push 0x18` / `call [edx+0x24]` 即 Read(24)
            ★ 正好是 24 字节头，**独立印证了下面 HVA 的字段布局**
0x0052BA60  引用 "voxels.vpl"（VPL 体素调色板加载器）
0x0074B050  引用 "VoxelIndex"
0x005F92D0  引用 "Voxel"（art.ini 里读 Voxel=yes）
```

**VXL 头部已破**（`+0` 16 字节 `"Voxel Animation"`，注意这个串**不在 exe 里**，
说明游戏不比较魔数）：

```
+16  4  uint32  Unused（恒为 1）
+20  4  uint32  NumLimbs
+24  4  uint32  NumLimbFrames
+28  4  uint32  BodySize
+32  4  uint32  LimbSize（只有一个肢体时是常量 0x1F10=7952，>=2 时是 0xCDCDCDCD 类垃圾）
+36  .. BodySize 字节的 body 段
```

**关键不变量**（184 个 VXL 全部精确成立，0 例外）：
`size - BodySize = 802 + 120 * NumLimbs`
（1 肢 → 922，2 肢 → 1042，3 肢 → 1162，13 肢 → 2362）

但 `BodySize + LimbSize + 36 != size`，所以 `LimbSize` 不是"每肢 N 字节"的意思。
body 段（从 +36 开始）的字节是 `00 ab 00 ab 00 ab ab 00 ab 00 57 ff 57 ff ff 57 …`
这样的两字节交替模式，**不是朴素的 x/y/z/color 四元组**；
文件末尾 64 字节是浮点矩阵（可见 `00 00 80 3f` = 1.0f）。结论：body 段有编码，
**下一步必须反汇编 `gamemd.exe` 里读 `.VXL` 的加载器**，靠猜字节划不来。

**文件名反查的增益有限但有用**：VXL/HVA 无魔数，只能靠
`UnitClass"的 Image 名 + ".VXL"/".HVA"` → Westwood CRC 查表。所以接下来
应当把 `db/mix-names.txt` 里 92 个 `Voxel=yes` 的名字全部算 CRC 存成表。
| 文件名反查补齐 | `db/mix-names.txt` | 命中率从 45% 提到 80%+ | 🟡 |

**P0 曾经的关键卡点：SHP flags=0x3。** 已解决 —— 结论是它和 0x02 共用同一套
RLE-Zero，真正的坑是"行尾那个游程计数常常比实际多 1"，必须逐行裁剪。
详细证据见 commit `6e1e0bc` 与 `tools/shpcrack.py`。

**本轮（P0 收尾）踩过的坑，值得单独记：**
- 明文 MIX 之前完全没实现，导致 ra2.mix 顶层 21 个条目里 13 个 10~35MB 的
  地形包被当成 SHP —— 地形素材整个不可见。
- TMP 多 cell 模板不是矩形堆叠，而是按 TileX/TileY 等倾铺排；
  按 `(bx*cw, by*ch)` 堆叠会错 60% 的像素。
- TMP 的 extra 数据故意画到格子外（126/135 个 1x1 模板的 ExtraY 是负数），
  所以画布不能把 extra 算进包围盒。
- INI 的编号列表**不从 0 开始**（[InfantryTypes] 是 1..65，[Animations] 还跳号），
  按"0,1,2 一直到缺号"读会一个都读不到。
- 原始 INI 里有 6 行少写了等号（`842-GAWETH_ED` 之类），解析器必须跳过而不是报错。
- **PCX 的内嵌调色板是标准 8 位**（实测 136 个样本最大分量 = 255），
  不是 `.PAL` 那种 0..63 的 6 位值。套 `Palette::Load` 的 `(v<<2)|(v>>4)` 展开
  会让整幅图暗一截、并把 255 压成 252 —— 这正是 `.PAL` 那条规则**不能无脑复用**的例子。
- **24 位 PCX 是"按行交织"存 R/G/B 三条带**，不是"整幅 R 段 + 整幅 G 段"。
  写错的话颜色会整体偏，但尺寸、行数全对，靠结构检查抓不出来 ——
  得真把图渲染出来看一眼才对得上。
- **MIX 里有个 101MB 的假条目**（id=0x08050506，在 ra2.mix 深度 2）：
  前 4 字节碰巧解出 `flags=0x0605050A / count=600 / data_size=1`，
  光看头部完全像明文 MIX。C++ 侧 `Open_Nested` 里有 `Validate_Index` 拦住了，
  Python 侧原先没有，于是它被当成归档打开、600 个垃圾条目全在"数据区"外，
  再读数据读到的其实是文件里毫不相干的字节 —— 两边遍历出的叶子集合对不上。
  已在 `tools/mixdump.py` 补 `index_fits()`（索引必须落在数据区内）+
  `iter_leaves()` 跳过 `off+size > data_size` 的条目。

### P1 — 渲染层：让第一个 RA2 精灵出现在屏幕上

| 任务 | 产出 | 状态 |
|---|---|---|
| DX12 设备 + 交换链 + 命令队列 | `src/gfx/dx12/Dx12Renderer.cpp` | ✅ |
| 调色板纹理（256×1 RGBA）+ 索引纹理上传 | `src/gfx/dx12/` | ✅ |
| 等距地形渲染（60×30 菱形格） | `ViewerMain.cpp --tmp` | ✅ 已出画面 |
| 精灵批渲染（一个 draw call 画几千个精灵） | `src/gfx/dx12/SpriteBatch.cpp` | ⬜ |

**已达成**：`ra2view.exe <mix> --tmp <0x归档ID> --offscreen` 能从明文 MIX 里
取出全部 TMP 模板、按等距格点由远及近铺成一张索引图并上屏，
树冠/岩壁这类 extra 也画出来了（见 `build/terrain_random.png`）。

**还差**：批渲染 + 与原版截图逐像素比对。

### P2 — 数据层：rules.ini / art.ini / ai.ini

原引擎把"什么单位有什么属性"全放在 INI 里，代码里只有类型表。
所以 P2 是**解析 INI → 填 TypeClass 表**：

- `rules.ini`（约 1.3 MB）：单位/建筑/武器的全部数值
- `art.ini`：图形绑定（SHP 文件名、帧数、朝向数）
- `ai.ini`、`sound.ini`、`theme.ini`

**验收**：打表出 500+ 个 TechnoType，属性与原版一致（可用 XCC Mixer 导出的数据对拍）。

### P3 — 逻辑层：还原对象模型与游戏循环

按 RTTI 实证的层次自底向上写：

```
AbstractClass(24) → ObjectClass(122) → MissionClass(157) → RadioClass(161)
  → TechnoClass(309) → {FootClass(341) → Unit/Infantry/Aircraft,
                        BuildingClass(322)}
AbstractTypeClass(27) → ObjectTypeClass(40) → TechnoTypeClass(48) → 各 TypeClass
```

先做到：**一个单位能从 A 走到 B**（寻路已有 `src/ai/PathFinder.cpp`），
再做到：能选中、能攻击、能建造。

**字段偏移问题**：RTTI 给了类名和槽位，但没给字段偏移。
- 已证：`AircraftClass` 最后一字段在 `[esi+0x6D4]`；
- 静态对象池类（`BuildingClass`/`CellClass`/`HouseClass`）的 sizeof 拿不到（不走 `operator new`）；
- 办法：从访问字段的指令里统计偏移分布 + 用已知的相邻对象大小反推。

### P4 — 网络与锁步

已有 `src/engine/FrameQueue.cpp`（锁步语义 + 帧 CRC）。继续做：
确定性定点数（替换浮点）、事件序列化、重放校验。

**验收**：同一份输入跑两遍，帧 CRC 序列完全一致；录制回放可复现。

### P5 — 多核并行（真正的目标）

前四阶段做完后才谈得上这一步。已识别的机会：

| 子系统 | 并行方式 |
|---|---|
| 寻路 | 已实现 `Solve_Parallel`，256 请求批处理 |
| 素材解包 | MIX 条目间零依赖，天然并行 |
| 精灵光栅化/上传 | DX12 多命令列表 + 拷贝队列 |
| AI 决策 | 按阵营分组，组内串行 |
| 物理/碰撞 | 空间划分后按区块并行 |

**硬约束**：`UnitClass` 2KB 级别，一次遍历的访存量远大于计算量 ——
并行必须按缓存行切分（SoA 布局），不能按对象切分。这是 P3 阶段就要埋好的结构。

---

## 3. 明确作废的部分

以下内容**不再还原**，因为渲染改用 DX12：

- `Blitter` 及其 57 个派生类
- `RLEBlitter` 及其 51 个派生类
- 主表面/后备缓冲管理、DirectDraw 表面锁、显存 Blt 路径
- 各种 Blitter 的 CPU 特化版本（MMX 等）

保留还原的：素材**格式**（SHP/PAL/PCX/VXL/INI）、调色板语义（重映射区间、
阴影索引 1、透明索引 0）、等距投影的几何约定。

---

## 4. 已知待办与风险

1. **SHP flags=0x3 解码未定** —— 走到 P0 一半时必解，用反汇编 blitter 解决。
2. **字段偏移基本未知** —— RTTI 只给类名和槽位。P3 的主要工作量在这。
3. **静态对象池类的 sizeof** —— 不走 `operator new`，现有扫描法拿不到，需要换思路。
4. **文件名反查只有 45%** —— 不阻塞（游戏按 CRC 查），但影响可调试性。
5. **工作量** —— 949 个类、12717 个虚表槽位、4 MB `.text`。
   目前手写代码约 3000 行（不含自动生成的 3794 行）。这是以季度计的工程，
   建议严格按 P0→P5 顺序推进，每阶段都要有能跑的验收。

---

## 5. 当前进度

- **P0 已完成 7/9**：MIX（加密 + 明文）✅、嵌套 ✅、CRC ✅、文件名反查 🟡、
  SHP ✅、PAL ✅、TMP ✅、INI ✅、PCX ✅。
- **P0 剩余**：VXL / HVA（HVA 格式已破、VXL 头部已破，body 编码要反汇编）。
- **P1 部分完成**：DX12 设备/交换链/离屏/调色板纹理/地形渲染 ✅，精灵批渲染 ⬜。
- 下一个里程碑：**VXL 出体素** —— `ra2core --vxl <mix> <0xID>` 打印体素包围盒与体素数，
  且 184 个 VXL 全量自洽；再接上 HVA 变换在 DX12 里画出第一个 Rhino。
