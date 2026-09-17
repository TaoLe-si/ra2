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
| **INI → 类型表** | ✅ | 553 个 TechnoType，rules 477 键 / art 206 键直方图清点后**全量**落表；逐键对账 0 处不一致 + 独立实现 22637 行 0 差异；见 `src/data/TypeDB.{h,cpp}` |
| **PCX 解码** | ✅ | 255 个样本全解通；ra2.mix 的 160 个逐条 RGBA 哈希与参考实现 0 差异 |
| **HVA 体素动画** | ✅ | 220 个样本全解通；ra2.mix 183 个逐条哈希与参考实现 0 差异 |
| **VXL 体素** | ✅ | 184+37 个全解通；239309 列 / 856623 体素零异常，哈希 0 差异；已接进 DX12 |
| **DX12 渲染器** | 🟡 | 设备/交换链/离屏/调色板纹理/精灵上传已通；**地形 + 体素已出画面**；批渲染未做 |
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
| **HVA 体素动画** | `src/gfx/HvaFile.cpp` | 220 个样本哈希与参考实现 0 差异 | ✅ |
| **VXL 体素** | `src/gfx/VxlFile.cpp` | 856623 个体素零异常 + 哈希 0 差异 + 出画面 | ✅ |
| 文件名反查补齐 | `db/mix-names.txt` | 命中率从 45% 提到 80%+ | 🟡 |

### P0 素材层：格式笔记（2026-09-15 全部破完）

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

**VXL 已全部破完**（2026-09-15）。`+0` 16 字节 `"Voxel Animation"`，注意这个串
**不在 exe 里**，说明游戏不比较魔数：

```
+0    16    "Voxel Animation"
+16   u32   PaletteCount（恒 1）
+20   u32   NumLimbs                     L
+24   u32   NumLimbFrames（恒 == NumLimbs）
+28   u32   BodySize
+32   u8    RemapStart（恒 16）  ← 之前被误读成 u32 "LimbSize=0x1F10"，
+33   u8    RemapEnd  （恒 31）     其实 0x10/0x1F 就是 (16,31) 两个字节
+34   768B  RGB 调色板                 → 头长 = 32 + 770*NP = 802
+802  L×28  肢体头：name[16] + limb_number(i32) + unk1(=1) + unk2(=0)
      BodySize 字节的 body
      L×92  肢体尾：
        +0  u32 span_start_ofs  } 相对 body 的偏移，指向两张 X*Y 的 u32 表
        +4  u32 span_end_ofs    }
        +8  u32 span_data_ofs   — 体素数据区起点（**不是第三张表**）
        +12 f32 det（= 1/12）   +16 f32[12] 3×4 变换（行主序）
        +64 f32[3] min_bounds   +76 f32[3] max_bounds
        +88 u8 X / +89 u8 Y / +90 u8 Z / +91 u8 NormalsType(2 或 4)
```

总长不变量（184+37 个样本 100% 成立，0 例外）：
`32 + 770*NP + 28*L + BodySize + 92*L == size`。

**只认两张表**：网上不少资料写 span_start/span_end/span_data 是三张表，实测不是。
硬证据：多数肢体上 `span_data_ofs + X*Y*4` 会直接越出 body，
而两张表的值恰好落在 `0..数据区长度-1`。空列在两表里都是 `0xFFFFFFFF`。

**列内编码**（239309 列 / 856623 体素零异常，Python 与 C++ 双实现逐字节对齐）：

```
游标 z = 0，反复读：
    delta  u8    游标前移 z += delta      ← 增量编码，不是绝对坐标
    n      u8    本游程体素数；n == 0 即终止符
    n × 2B       每个体素 (colour, normal)
    n      u8    计数再写一遍（渲染器要能反向遍历 span）
    z += n
终止符 = [delta][0][0]，delta 是最后一簇之上剩的空格数；
若最后一簇正好填满到 Z，终止符整个省略（所以长度会不整齐）。
```

举例（`0x36B0C51B`，12×11×12，Z=12）：

```
列[17] `00 0c | 24B | 0c`                   → 0..11 全满，无终止符，共 27 字节
列[53] `00 01 | 39 02 | 01`                 → z=0
       `02 02 | 14 20 28 16 | 02`           → Δ=2 从 z=1+2=3 起，z=3/4
       `07 00 00`                           → 4+7 = 12 = Z ✓
```

两个必须记住的点：**z 是增量**（列[53] 第二段写 02 而不是 03），
**n 在数据前后各写一次**（把这一条写错，多 span 列会整片错位）。

**肢体局部原点不是索引 (0,0,0)**（2026-09-15 第二次修正，最容易踩且最晚才发现）：
`min_bounds`/`max_bounds` 就是**该肢体体素在局部坐标系下的 AABB**，而局部原点
落在 AABB 中心 —— 13 根肢体的 `(min+max)/2` 实测都 ≤1.8，BODY 是
`(0.000,-0.164,0.345)`。所以体素索引必须先平移再套变换：

```
world = R · (index + min_bounds) + T
```

把索引直接喂变换的后果（先前的错误版本）：单肢模型（184 个里的 180 个）
因为整体平移、归一化后看不出问题，多肢模型立刻散架 ——

| 模型 | `world = R·index + T`（错） | `world = R·(index+min_bounds)+T`（对） |
|---|---|---|
| JEEP 车身 | z 6.96–18.96（悬空 7 格） | z 0.09–12.30（落地） |
| JEEP 车顶机枪 GUN01 | 嵌在车身中部，看不见 | z 11.96–14.58，正压车顶 12.30 |
| 四足机甲（13 肢）脚 | z 0.83–5.9，与小腿断 3.3 格 | z 0.15–5.03，**正好踩地** |
| 四足机甲 车体中心 | (32.0, 15.5)，四足重心在 (7.4, 6.7) | (6.0, 0.5)，与髋部 ±12.4/±6.6 对齐 |
| SHAD DUMMY01 | — | z 0.11–25.61（落地） |

用 `(N-1)/2` 当枢轴只能算近似，会差 0.5~3.6 格：GUN01 会被整个埋进车身里。
用 `+min_bounds` 是唯一能**精确复现文件自带的 AABB** 的做法。

**HVA 接渲染的两条单位规则**（3 组肢体名唯一的配对 / 18 根肢体实测）：
- `3×3` 部分两边**完全相同**（最大差 0.000000），直接复用；
- 平移 `T_vxl == T_hva × det`，det 恒 `1/12`，所以 HVA 的平移要乘 det；
- **det 只乘平移**。写成 `world = det×(R·v + T)` 会把体素坐标缩 12 倍而平移不变，
  13 根肢体立刻散成天上的一堆小方块（实测踩过）。
- 配对判据：肢数相同 + 每根肢体 f0 的 `R` 与 `T·det` 都对上，要求**唯一命中**，
  不唯一就不用（宁可静态也不要配错）。多肢模型的**肢体名也完全一致**
  （WALKER 13/13、SHAD 3/3、NARTURNTA 2/2），可作额外确认。

**炮塔和炮管是独立 VXL**：`MTNK.VXL` 只有 1 根肢体 DUMMY01（整个车体），
炮塔在 `<名>TUR.VXL`、炮管在 `<名>BARL.VXL`（实测 MTNKTUR=0x17F0096E、
HTNKTUR=0x8F537E7E、GTNKTUR=0xFDC7E10F、SREFTUR=0x13C3E16F、
MTNKBARL=0xB19F831F、FVTUR=0x1529B889、RTNKTUR=0x697C7BE3、JEEPTUR=0xDA65DCB1
全在 ra2.mix 里）。ra2.mix 里只有 4 个 VXL 是多肢的（JEEP 2、SHAD 3、
NAR TURNTA 2、四足机甲 13），所以"没炮塔"≠ 解码错，是**要按数据层拼装**。

拼装规则（`GTNK` 实测，三个文件**共享同一模型空间原点**，`T` 完全相同
`(0.377,-0.052,-0.555)`）：

| 文件 | 世界 AABB | 说明 |
|---|---|---|
| `GTNK.VXL` 车体 | z 0.01–11.01 | 落地，顶面 11.01 |
| `GTNKTUR.VXL` 炮塔 | z **11.02**–17.02 | 底面**正好压**在车体顶面 |
| `GTNKBARL.VXL` 炮管 | x 7.85–32.85, z 11.18–14.18 | 从炮塔内部穿出，向前伸 |

所以数据层只要：`hull + TUR(绕 Z 转炮塔朝向) + BARL(绕 X 抬炮口)`，
再叠 art.ini 的 `TurretOffset=` 微调即可 —— 不需要额外坐标换算。

调色板坑：VXL 内嵌调色板常被说成"6 位值要 <<2"，实测**是错的** ——
184 个样本的 768 个分量**全部** ≡ 3 (mod 4)，即存的已经是 `(v6<<2)|(v6>>4)`
展开好的 8 位值。再 <<2 会把炮塔渲成一片青紫洋红（实测踩过）。
另外 0..15 号恒为品红 (255,0,255) "无此色"标记，16..31 是御主色渐变。

**文件名反查**：VXL/HVA 无魔数，只能靠 `名字 + ".VXL"/".HVA"` → Westwood CRC 查表。
`tools/vxlname.py` 用"INI 节名 + token + 后缀(TUR/BARL/WO)"的全量 CRC 扫描，
把 ra2.mix 的 184 个 VXL 命名到 **115 个**（全部同时命中 `.HVA` 的 CRC，
等于双重证据）。`tools/mixnames.py` 也补了两轮：**INI 节名也要当候选**
（`[ZEP]` 就是基洛夫，只扫 `key=value` 会整个漏掉）和**已命中名 + 后缀扩展**，
命中率 **8.7% → 26.0%**（11233 个叶子 ID），见 `db/mix-names.txt`。

仍未命名的（ra2.mix 里 69 个 VXL，含 `0xDF94FB26` 那台四足机甲）：INI 里
查不到任何对应名字，exe 字符串里也没有 —— 判为**未使用的遗留素材**。
游戏本身是按 CRC 查表的，所以不影响还原。

`ra2view.exe` 的命令行参数上限原来是 8，加上炮塔那组开关（`--turret` /
`--barrel` / `--turretyaw` / `--barrelpitch`）就溢出，超出的参数被**静默丢弃**
（表现是 `--offscreen` 消失、开了个窗口等按键）。已提到 32 并加截断告警。

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
| 精灵批渲染（一个 draw call 画几千个精灵） | `src/gfx/dx12/Dx12Renderer.{h,cpp}`（图集 + 实例缓冲） | ✅ |

**已达成**：`ra2view.exe <mix> --tmp <0x归档ID> --offscreen` 能从明文 MIX 里
取出全部 TMP 模板、按等距格点由远及近铺成一张索引图并上屏，
树冠/岩壁这类 extra 也画出来了（见 `build/terrain_random.png`）。

**批渲染（已完成）**：小图拼进 4096² 的 shelf 图集（索引色一张、真彩一张），
N 个精灵共用一个 SRV，差异只剩"矩形 + UV + 颜色"——那三样塞进实例缓冲，
同类连续区段合成一次 `DrawInstanced(6, N)`。
放不进图集的大件（地形整图 5220×2395）退回"一精灵一纹理"，
由 `Flush_Batch` 在切换时刻发出，**绘制顺序与录制顺序严格一致**。

实测（`ra2game --gamedir <安装> --offscreen`，一帧）：

```
批渲染：索引 0 批 / 真彩 3 批 / 纯色 2 批，批内精灵 417 条，单独 draw 1 次
```

也就是**一帧 417 个精灵 = 6 次 draw call**（老路径 417 次），
帧时间 3.5 ms → 2.1 ms。

正确性判定不靠肉眼：`RA2_NO_SPRITE_BATCH=1` 走完全相同的旧路径，
两条路的回读图**逐字节一致**（3145728 字节，0 处不同）；
`RA2_BATCH_ONE=1`（每条实例单独一批）也一致 —— 后者能单独卡住
"批次起点算错"这一类问题。

**踩过的坑**：`DrawInstanced` 的第 4 个参数 `StartInstanceLocation`
（本该整体偏移 `SV_InstanceID`）**实测无效** —— 每一批都从第 0 条实例开始读，
症状是整屏只剩一个精灵被反复画、纯色矩形叠成白色方块。
改成把批次起点用根常量（`b1`）喂给顶点着色器，自己加下标。详见 `docs/re-ledger.md`。

**还差**：与原版截图逐像素比对（需要一份原版运行时的参考截图）。

### P2 — 数据层：rules.ini / art.ini / ai.ini

原引擎把"什么单位有什么属性"全放在 INI 里，代码里只有类型表。
所以 P2 是**解析 INI → 填 TypeClass 表**：

- `rules.ini`（约 1.3 MB）：单位/建筑/武器的全部数值
- `art.ini`：图形绑定（SHP 文件名、帧数、朝向数）
- `ai.ini`、`sound.ini`、`theme.ini`

**验收**（2026-09-17 达成）：打表出 **553** 个 TechnoType（四个类型列表共 559 个名字，
其中 6 个在原版数据里没有对应段 —— 见下文），属性与原版一致。
对拍方式比"用 XCC Mixer 导出的数据比"更硬：**直接与游戏自己的 INI 字节比**。

#### P2 第一步（已完成）：单位名 → 体素模型组成

先做"最小可用的一跳"：**给单位名，自动推出它由哪几个 VXL/HVA 组成**。
这一跳打通，"给单位名出图"才算真的连上（之前是手写三个 0xID）。

**INI 里怎么摆（实测，`tools/iniprobe.py` 四个文件全扫）**

| 键 | 落在哪 | 说明 |
|---|---|---|
| `Voxel=yes` | `art(md).ini` 的 `[Image]` 段 | rules 里**一处都没有** |
| `Turret=yes` | `rules(md).ini` 的 `[单位]` 段 | art 里**一处都没有** |
| `Image=` | `rules(md).ini` 的 `[单位]` 段 | 缺省 = 单位名本身 |
| `PrimaryFireFLH=` | `art(md).ini` 的 `[Image]` 段 | 炮口位置 |
| `TurretOffset=` | `art(md).ini` 的 `[Image]` 段 | |

也就是说**同一个单位的信息被拆在 rules 和 art 两个文件里**，只认一个必然错：
实测 `[MTNK]` 写的是 `Image=GTNK`（灰熊坦克的美术名），不查 rules 就找不着模型。

**文件名怎么拼**（MIX 里没名字、只有 CRC，而 CRC 大小写不敏感，统一用大写）：

```
<IMAGE>.VXL      车体（必需）
<IMAGE>.HVA      车体动画
<IMAGE>TUR.VXL   炮塔   ← Turret=yes 且文件在包里时
<IMAGE>BARL.VXL  炮管   ← 文件在包里时
```

**炮塔不是一定有独立文件**：18 个 `Turret=yes` 的单位里 17 个有 `TUR.VXL`，
只有 `SCHP`（苏联攻城直升机）没有 —— 它的炮塔是车体 VXL 里的肢体
（`CYLINDER19`/`CYLINDER57`/`DUMMY01`）。所以判据是**文件在不在 MIX 里**，
不是 `Turret=yes` 没有。反过来也有一个特例：`GTGCAN` 有 `TUR.VXL`+`BARL.VXL`
却没有车体 VXL、art 里也没 `Voxel=yes` —— 判定为**废弃素材**。

**载入顺序与合并语义（踩过的坑）**

- rules 在 `ra2.mix`（`RULES.INI`）、rulesmd 在 `ra2md.mix`（`RULESMD.INI`），
  **一人一份**，只挂一个包就只有一半：单挂 ra2.mix 是 1191 段 / 59 个体素单位，
  两个一起才是 1482 段 / 86 个体素单位。
- md 覆盖 base，而且是**覆盖在位**（Westwood `INIClass::Load` 的语义），
  不是"追加后取第一个"。反例很实在：`[BuildingTypes]` 是编号列表，
  rules.ini 有 301 项、rulesmd.ini 有 403 项；用"追加 + 取第一个"的写法，
  两份的 1..301 会**并存**，单位总数变成 565 —— 比真实的 559 多出 6 个
  （`CAARMR`/`NAHPAD`/`NAWAST`/`GARADR`/`CAEURO01`/`CAIRSFGL`，只在老列表里）。
- 落地：`IniFile::Merge(data, size, overwrite)`（`src/data/Ini.h`），
  `Load` 保持原语义不动（`--inidump` 的逐行回归依赖它）。

**落地与验收**

- `src/data/UnitModel.{h,cpp}`：`UnitModelDB::Load(悬挂的 MIX 数组)` +
  `Resolve(单位名)`。为了不逐个 `Read_Deep` 解 Blowfish，
  先用 `MixFileClass::Collect_Leaf_IDs()` 把全部条目 ID 摊平成一张集合。
- `ra2core.exe --unitdb <mix...> [--dump out.txt]`：全量解析 + 自检
  （**车体缺失必须是 0**，缺一个就说明 Image= / CRC 大小写 / 段合并有一处写错）。
- `ra2view.exe <mix> --addmix <mix2> --unit MTNK ...`：单位模式，
  车体/炮塔/炮管/HVA 全自动，其余开关（`--turretyaw` 等）照旧可用。
- `tools/unitvxl.py`（Python 参考实现）+ `tools/unitvxl_check.py`（逐行对账）。
- **实测结果**：段 rules=1482 / art=1594（与 Python 完全一致）；单位 559；
  体素单位 **86**，车体命中 **86/86**，炮塔 17，炮管 7，车体缺失 **0**；
  C++ vs Python **559 个单位 × 9 个字段全部一致**。
- 端到端（`tools/unitrender.py`）：
  `--unit MTNK` → GTNK.VXL + GTNKTUR + GTNKBARL 三件套自动拼出灰熊坦克；
  `--unit YTNK --yaw 40` → 加特林坦克，双管炮塔转 40°；
  `--unit ZEP/APOC/LTNK/DISK/HARV/SREF/AMCV/V3` 全部出图。

#### P2 第二步（已完成）：INI 全量进类型表

`src/data/TypeDB.{h,cpp}` + `ra2core --typetable <mix...>`。

**先清点，再定结构**。`tools/inikeys.py` 把四个真实 INI 扫了一遍（输出
`build/_inikeys.txt`），先弄清楚**原版到底写了哪些键**：

| 来源 | 键种数 | 出现次数 |
|---|---|---|
| rules(md) 单位段（559 个名字） | **477** | 15315 |
| art(md) 的 `[Image]` 段 | **206** | 7322 |
| 武器段（117 个被引用的） | 52 | — |
| 弹头段 | 85 | — |
| 抛射体段 | 35 | — |

然后才是 **"类型化字段 + 原始键值表"双轨**：

- `TechnoType.rules` / `.art` 两个 `ValueMap` 存**全量**键值 —— 一个键都不丢；
- 结构体字段是类型化**视图**，**声明与填充用同一份 X-macro 清单**
  （`RA2_TECHNO_FIELDS`），两边不可能分叉。

**验收（两条独立证据，缺一不可）**

1. `ra2core --typetable` 自检 —— 对 553 个 TechnoType 的 rules 段与 art 段
   逐键对账：**段的去重键数 == 表的大小**、**每个键的值逐字节相等**。
   这一条证明"表里存的东西 == 它自己读的 INI"。
2. `tools/techno.py`（**独立实现**，从 MIX 字节重新读起、自己实现合并语义）
   产出同一份 dump，逐行 diff —— **22637 行 0 差异**。
   这一条才真正证明 **INI 解析/合并本身**没错（自检做不到这点：
   如果合并语义写反了，自检照样全绿）。

**实测（Reunion 2023，553 个 TechnoType）**

```
类型表：TechnoType 553（四个类型列表共 559 个名字，其中 6 个在原版数据里没有对应段）
  rules 单位段：键种 477  出现 15315 次  已类型化 313 种（65.6%） / 按出现次数 95.8%
  art  Image 段：键种 206  出现 7322 次  已类型化 111 种（53.9%） / 按出现次数 97.3%
  X-macro 声明 424 个键，其中 0 个在数据里一次都没出现
武器 189（全库有 Damage+Warhead 的段共 272）
弹头 116（[Warheads] 列表 105）  抛射体 38
sound.ini 有（1018 个编号）  theme.ini 有（36 首）  ai.ini 无
逐键对账：不一致 0 处
```

覆盖率按**键种**看只有 46%/54%，按**出现次数**看是 96%/97% —— 没类型化的
那 164 种 rules 键绝大多数只出现在 1~3 个单位上。剩下没类型化的高频键
已按频次排好印在 `--top` 的表尾（"还没类型化的键 Top N"），
要补字段照着加就行，不用猜。

**踩过的坑**

- **`Occupier=` / `Assaulter=` 是布尔，不是武器名**。第一版按名字猜成
  "武器字段"，实测值是 `yes`/`no`（`Occupier=yes ; I can Occupy UC buildings`）。
  这种"名字像就当地址用"的错最容易溜过去 —— 现在 X-macro 里声明了却
  在数据里一次都不命中的键会被逐个列出来，等于给手误上了闸。
- 键名打错**不会报任何错**，只会永远取缺省值。所以加了
  "声明的键 vs 实际命中的键"这条对账，当前 424/424 全命中。
- `techno.py` 第一版把 units 行全局排序，导致同一单位里 `art` 排在
  `rules` 前面（C++ 是"先扫完 rules 再扫 art"），**每一行都错位** ——
  跨实现对账必须先把"排序口径"写清楚，否则红的是对账本身而不是数据。

#### P2 还差的（下一轮）

- `ai.ini` / `aimd.ini`：**本安装的 8 个归档里都没有**（旧基线的名字表里
  有这两个 CRC：`AI.INI=0x9E11E49A @ra2.mix`、`AIMD.INI=0x116F3F76 @ra2md.mix`，
  但 Reunion 2023 重打包时没带上）。加载器已就绪，喂进去就能用；
  `--typetable` 会如实打 `ai.ini 无`，不假装读过。
- `sound(md).ini` / `theme(md).ini` 已进 `SoundDB` / `ThemeDB`
  （1018 个声音编号、36 首曲目），但还没接给 AudioDevice / 战场音乐。
- 剩下 164 种 rules 键 + 95 种 art 键的类型化（按 `--top` 表尾的顺序补）。
- 属性对拍（与 XCC 导出的原版数据比）—— 现在这条其实**更强**：
  不与第三方导出比，直接与游戏自己的 INI 字节比。

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

1. ~~SHP flags=0x3 解码未定~~ —— **已解**：它和 0x02 共用同一套 RLE，6373 帧全过。
2. **字段偏移基本未知** —— RTTI 只给类名和槽位。P3 的主要工作量在这。
3. **静态对象池类的 sizeof** —— 不走 `operator new`，现有扫描法拿不到，需要换思路。
4. **文件名反查 26.0%**（11233 个叶子 ID / 2926 个命中，原 8.7%）—— 不阻塞
   （游戏按 CRC 查）。补齐的关键是"INI 节名也算候选名 + 后缀扩展"两轮，
   见 `db/mix-names.txt`。
5. **工作量** —— 949 个类、12717 个虚表槽位、4 MB `.text`。
   目前手写代码约 3000 行（不含自动生成的 3794 行）。这是以季度计的工程，
   建议严格按 P0→P5 顺序推进，每阶段都要有能跑的验收。

---

## 5. 当前进度

- **P0 已完成 9/9**：MIX（加密 + 明文）✅、嵌套 ✅、CRC ✅、文件名反查 🟡、
  SHP ✅、PAL ✅、TMP ✅、INI ✅、PCX ✅、**HVA ✅、VXL ✅**。
  素材层到此**没有未解格式**。
- **P1 已完成 4/4**：DX12 设备/交换链/离屏/调色板纹理/地形渲染 ✅、
  **体素渲染 ✅**（软件等距光栅化 → R8 索引纹理 → 查表着色）、
  **精灵批渲染 ✅**（图集 + 实例缓冲：一帧 417 精灵 = 6 次 draw call，
  与关掉批渲染的旧路径**逐字节一致**）。
- 端到端链路（五档，全绿）：
  1. `ra2view.exe ra2.mix --vxl 0x8C848DEE --offscreen` → 基洛夫（ZEP），静态姿态正确；
  2. `ra2view.exe ra2.mix --vxl 0x891E5F6E` → JEEP，**带车顶机枪、轮子落地**；
  3. `ra2view.exe ra2.mix --vxl 0xDF94FB26 --hvaframe0/8` → 13 肢四足机甲，
     **四脚踩地、车体骑在腿上，f0/f8 腿姿不同**（HVA 动画真的在驱动）；
  4. `--vxl 0xAE458B95 --turret 0xFDC7E10F --barrel 0x5BA86B7E` → GTNK 坦克，
     **圆炮塔压在车体顶、炮管从炮塔穿出**；
  5. 同一条命令加 `--turretyaw 40 --barrelpitch 25` → **炮塔转、炮口抬**，
     炮管绕自身耳轴俯仰（枢轴取炮管 AABB 尾端中点）。
  外加回归：`--vxlhash` / `--hvahash` / `--tmphash` / `--initest` / 路径并行 全过，
  184+37 个 VXL 结构失败 0，239309 列 / 856623 体素异常 0，
  C++ vs Python VXL 哈希 184/184、37/37 差异 0。
- **P2 数据层已完成两步**：
  1. 模型组成（`Image=` / `Voxel=yes` / `Turret=yes` / `PrimaryFireFLH=`）——
     `--unit <单位名>` 自动推出 `<名>.VXL + <名>TUR.VXL + <名>BARL.VXL`（+ 同名 HVA）；
  2. **全量类型表**（`src/data/TypeDB.*`）—— 553 个 TechnoType 的 rules/art
     **全部 477 + 206 种键一个不丢**，值逐字节与原 INI 一致（C++ 自检 +
     Python 独立实现 22637 行 0 差异）；另有武器 189 / 弹头 116 / 抛射体 38 /
     声音 1018 / 曲目 36。
  端到端链路扩展到**六档**：
  6. `ra2view.exe ra2.mix --addmix ra2md.mix --unit YTNK --turretyaw 40 --offscreen`
     → INI 解析 → 名字拼装 → 三段 CRC → MIX 递归取件 → VXL 解码 → HVA 姿态
     → 等距光栅化（含附加层）→ R8 索引纹理 → 调色板查表 → 回读 PNG，
     画出加特林坦克的双管炮塔转 40°。
  自检：86 个体素单位车体命中 **86/86**，车体缺失 **0**；
  C++ vs Python **559 单位 × 9 字段零差异**。
- 再往后：P2 收尾（ai.ini 缺素材、剩余键类型化、声音/曲目接进游戏）、
  P3 逻辑层、P4 锁步、P5 多核并行。
- **代码尚未提交**：本轮的 `UnitModel.*`、`Ini::Merge`、`Collect_Leaf_IDs`、
  `ViewerMain --unit/--addmix`、新脚本都在工作区里没 commit（git push 也没通）。
