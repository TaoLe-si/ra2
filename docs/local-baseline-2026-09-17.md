# 本机基线复现（2026-09-17）

**目的**：在中文 Windows（ACP=936）上把工程**编译起来**，并以本机实际安装的
`D:\RA2\Reunion 2023` 作素材源，把 P0/P1/P2 各条回归**跑一遍并留数**。
文档里记录的旧基线取自 `D:\westwood\RA2YR`，两者**素材包不同**，本文把差异摊开。

---

## 1. 环境

| 项 | 值 |
|---|---|
| 系统代码页 | ACP = 936（GBK），所以 `/utf-8` 不能省 |
| 编译器 | MSVC 14.51.36231（VS 18 Insiders，`Hostx64\x64`） |
| cmake | 4.4.3 |
| Windows SDK | 10（`build.py` 自动选版本最高的） |
| Python 分析依赖 | `tools/pylibs` 已装 capstone 5.0.9 + pefile 2024.8.26 |
| 构建入口 | `python tools/build.py`（无需 vcvarsall，不碰注册表） |
| 产物 | `build/ra2core.exe`、`build/ra2view.exe`、`build/ra2game.exe` |

---

## 2. 改动清单（P0/P1/P2 那轮，8 个文件）

| 文件 | 改动 | 原因 |
|---|---|---|
| `tools/build.py` | `CFLAGS` 加 `/utf-8` | 源码 UTF-8 无 BOM；不加会被按 GBK 解码，48 个 error |
| `CMakeLists.txt` | MSVC 分支加 `/utf-8` | 同上（cmake 那条构建路径） |
| `tools/build-msvc.bat` | cl 命令行加 `/utf-8` | 同上 |
| `src/core/GameVersion.cpp` | 扩展包改为枚举 `expandmd%02d.mix`(01..99) + `thememd.mix` | 原来写死 `expandmd01.mix`，改版安装会漏挂规则 |
| `src/core/GameVersion.h` | 同步挂载顺序注释 | — |
| `src/viewer/ViewerMain.cpp` | `mix2`(单副归档) → `extra`(挂载列表)；新增 `--gamedir`；`--addmix` 可重复 | 一个 `--addmix` 挂不下真实安装的 10 个包 |
| `README.md` | 补 `/utf-8` 说明 | 防止后人删掉编译选项 |
| `docs/re-ledger.md` | 新增「归档挂载顺序」与「构建环境」两节 | 留 RE 证据链 |

### 2.1 本轮（P3 字段偏移）改动

| 文件 | 改动 | 原因 |
|---|---|---|
| `tools/peimage.py` | `DEFAULT_IMAGE` 改为 `resolve_default_image()` 自动定位 | 旧基线的 `D:\westwood\RA2YR` 在本机不存在，整条分析管线跑不起来 |
| `tools/analyze.py` | 摘要里补文件大小 + 「跨机器认入口点/时间戳，不认路径」 | 上面那条的配套说明 |
| `tools/fieldscan.py` | **新增**：从构造函数抽字段偏移 | P3 的地基 |
| `src/main.cpp` | 新增 `Layout_Check()`（进冒烟测试）与 `--layout` 命令 | 把字段偏移变成可回归的硬判据 |
| `src/re/FieldOffsets.h` | **新增**（自动生成，核心继承链 17 个类） | 同上 |
| `.gitignore` | `re/` → `/re/` | 不带前导斜杠的 `re/` 会匹配任意层级，把 `src/re/*.h` 一起挡掉 |
| `db/summary.md` | 重新生成（仅镜像路径一行变化） | 见第 3 节第 13 项 |
| `tools/sizeofscan.py` | 新增「字段末端硬过滤候选」；`virtuals.json` 缺失不再中断 | 修 `CCFileClass` 的 sizeof；`virtuals.json` 既不在仓库里也生成不出来，读到的值从头到尾没被用过（见第 3 节第 17 项） |
| `db/sizes.json` | 重新生成：新增 `fields_end` / `note` / `calibrated` 三个键 | 同上 |
| `docs/sizes.md` | 重新生成：方法补第 4 步 + 结果表加两列 | 同上 |

### 2.2 本轮（P3 字段名）改动

| 文件 | 改动 | 原因 |
|---|---|---|
| `tools/fieldname.py` | **新增**：从 `Read_INI` 的「键名两侧同偏移」抽字段名 | P3 第二步 |
| `db/fieldnames.json` | **新增**（自动生成）：字段名 + 覆盖面对账 + 争议留痕 | 同上 |
| `docs/fieldnames.md` | **新增**（自动生成）：人读版（含"够不到的三种形态"） | 同上 |
| `src/re/FieldNames.h` | **新增**（自动生成，396 条） | 同上 |
| `src/main.cpp` | 新增 `FieldNames_Check()`（进冒烟测试）与 `--fieldnames` 命令 | 把字段名变成可回归的硬判据 |
| `README.md` | 新增 §3.2「对象字段**名**」；现状速览、目录结构、已知限制同步 | 不留过期描述 |
| `docs/re-ledger.md` | 新增「对象字段名」一节（含三个真踩到的坑） | 留 RE 证据链 |
| `docs/restoration-plan.md` | P3 第二步标记完成；状态表加「对象字段名」行 | 同上 |

---

## 3. 验证结果（全绿）

| # | 命令 | 结果 |
|---|---|---|
| 1 | `ra2core.exe`（无参数，全量冒烟） | INI 自检 **18/18 通过**；路径 256 条（命中 224）串行 61.56ms / 并行 16.91ms = **3.64x**；锁步帧队列 **CRC = 0x15307EAB**（与 README 记录**逐位一致**）；sizeof 67 条；RTTI 949 类 / 12717 槽 |
| 2 | `ra2core.exe <ra2md.mix>` | 明文 MIX 索引自洽（越界 0）；LOCALMD.MIX 按名命中；嵌套归档 208 条目可开；密钥文件 151 字节解出 `[PublicKey]` |
| 3 | `ra2core.exe --detect <游戏目录>` | 认出 YR，**10 个素材包**按序输出 |
| 4 | `ra2core.exe --unitdb <10 个包>` | `段 rules=1482 art=1594 \| 单位 559 \| 体素 86 \| 炮塔 17 \| 炮管 7 \| 车体缺失 0` |
| 5 | `ra2core.exe --vxlhash <ra2.mix>` | 183 个 VXL，**结构失败 0** |
| 6 | `ra2core.exe --hvahash <ra2.mix>` | 183 个 HVA（含 17×13 与 2×3 多肢例） |
| 7 | `ra2core.exe --pcxhash <ra2.mix>` | 159 个 PCX，**解码失败 0** |
| 8 | `ra2core.exe --ini <expandmd01> 0x8218F9F4` | 743218 字节 → **1477 段 / 23392 条目 / 畸形行 0** |
| 9 | `ra2view.exe --gamedir <目录> --unit YTNK --turretyaw 40 --offscreen` | 10 包挂载 → INI 解析 → YTNK.VXL+YTNKTUR.VXL → HVA → DX12 离屏 1024×768 → `build/frame.raw`，出图正常 |
| 10 | `ra2core.exe --typetable <8 个包>` | `TechnoType 553`（列表 559 名字，6 个无段）；rules 477 键种 / 15315 次、art 206 键种 / 7322 次；**逐键对账 0 处不一致**；武器 189 / 弹头 116 / 抛射体 38 / 声音 1018 / 曲目 36 / ai.ini 无 |
| 11 | `python tools/techno.py <目录> --check build/type_raw.txt` | **22637 行逐行一致**（独立实现，从 MIX 字节重新读起） |
| 12 | `python tools/inikeys.py <目录>` | 键直方图落 `build/_inikeys.txt`：rules 477 种 / art 206 种 / 武器 52 / 弹头 85 / 抛射体 35 |
| 13 | `python tools/analyze.py` | 重新生成 `db/functions.json` 等；**除路径一行外与旧基线逐字节相同**（1392506 指令 / 28827 函数 / 1026 虚表 / 16382 字符串）—— 这是"两处 gamemd.exe 是同一构建"的硬证据 |
| 14 | `python tools/fieldscan.py` | 646 个类 / 6965 条字段；sizeof 越界 **4**（通过 207）；RTTI 嵌入基类位移 **387 处一条不漏**：可判定 159 处**全中**，另 228 处基类无虚表，退到同链写入印证 228 处 |
| 15 | `ra2core.exe`（无参数） | 冒烟 **22 OK**（比上一轮多 1 条：`Layout_Check()` 字段偏移硬判据）；帧 CRC 仍 `0x15307EAB` |
| 16 | `ra2core.exe --layout` / `--layout UnitClass` | 核心继承链 17 个类的字段表；`UnitClass` 30 个字段，末端 0x6E8（sizeof 2280 之内） |
| 17 | `python tools/fieldname.py` | 5 个 `Read_INI`（全部落在虚表槽 #25）；字段名 **411 条 / 6 个类**，「双向」307 条（75%）；C++ 表收 **396** 条；覆盖面 TechnoTypeClass 250/252、BuildingTypeClass 181/193、UnitTypeClass 42/44、InfantryTypeClass 25/25、ObjectTypeClass(IsometricTile) 15/19 |
| 18 | `ra2core.exe`（无参数） | 冒烟 **23 OK**（比上一轮多 1 条：`FieldNames_Check()` 字段名硬判据）；帧 CRC 仍 `0x15307EAB` |
| 19 | `ra2core.exe --fieldnames` / `--fieldnames TechnoTypeClass` | 6 个类合计 396 条；`TechnoTypeClass` 168 条（`Cost@0x610`、`TechLevel@0x634`、`Sight@0x5E8`、`Points@0x728` 等） |
| 17 | `python tools/sizeofscan.py` | 按字段末端校准 1 个类：`CCFileClass` 36 → **108**；另 4 个类所有候选都小于字段末端，只记 `note` 不改。写入 C++ 表 67 条 |

第 4 项复现了文档基线**逐项完全相同**（`1482/1594/559/86/17/7/0`）。
第 1 项的帧 CRC 也逐位一致 —— 这两条是"代码无回归"的硬证据。

---

## 4. 与文档旧基线的差异（全部来自素材包，不是代码回归）

本机是 **Reunion 2023 整合版**（Ares + Phobos + CNCNet + 中文化），
`ra2.mix` / `ra2md.mix` 被**重打包成明文 MIX**（`flags=0x00000000`），
文档里的旧基线是 **TS 加密**（`flags=0x00030000`）。

| 项 | 文档旧基线（`D:\westwood\RA2YR`） | 本机（`D:\RA2\Reunion 2023`） |
|---|---|---|
| `ra2md.mix` | 加密，条目 25，数据区 204527280，起 396 | 明文，**条目 26**，数据区 221983395，起 322 |
| `ra2md.mix` 内 LOCALMD.MIX | off=6237216 size=4819480 | off=11212 size=6219416 |
| LOCALMD.MIX 嵌套条目 | 187 | **208** |
| `ra2md.mix` 内 0xBA5EA181 | size=6226784 | size=**6373418**（改版改过） |
| VXL 数（ra2.mix） | 184 | 183 |
| PCX 数（ra2.mix） | 160 | 159 |
| `RULESMD.INI` | 在 `ra2md.mix`，742958 字节 | 在 **`expandmd01.mix`**，743218 字节 |
| `ARTMD.INI` | 在 `ra2md.mix`，336535 字节 | 在 **`expandmd01.mix`**，336469 字节 |
| `RULES.INI` | 在 `ra2.mix`，541915 字节 | 在 **`expandmd97.mix`**，541915 字节（**大小相同**） |
| `Art.ini` | 在 `ra2.mix`，245825 字节 | 在 **`expandmd97.mix`**，245825 字节（**大小相同**） |

**结论**：原版 RA2 的 `RULES.INI` / `Art.ini` 字节数与基线一致 → 底料没动；
YR 侧 `RULESMD.INI` / `ARTMD.INI` 被改版覆盖（+260 / −66 字节），
但**编号列表结构没变**，所以单位表统计值完全一致。

**给后人提个醒**：`db/mix-names.txt`、`db/ra2md-tree.txt` 里存的路径和 ID
都是 `D:/westwood/RA2YR` 那一版的。拿改版安装对账时，**同名条目的 ID 不变
（CRC 是名字算的），但所在归档和大小可能变** —— 别把"找不到"当成代码坏了。

---

## 5. 复现命令（本机可直接跑）

```bash
python tools/build.py                 # 编三个目标
build/ra2core.exe                     # 全量冒烟（无参数）
build/ra2core.exe --detect "D:/RA2/Reunion 2023"
build/ra2view.exe --gamedir "D:/RA2/Reunion 2023" --unit YTNK --turretyaw 40 --offscreen
python tools/raw2png.py build/frame.raw build/ytnk_40.png
```

`--gamedir` 是这次新增的：不用再手写一长串 `--addmix`。

---

## 6. 后续待办

1. **P1 收尾**：与原版截图逐像素比对（当前体素出图已通，但明暗/阴影尚未对拍，
   需要一份原版运行时的参考截图）。
2. **P2 收尾**：`ai.ini` / `aimd.ini` **本安装没有这两个文件**
   （旧基线里有：`AI.INI=0x9E11E49A @ra2.mix`、`AIMD.INI=0x116F3F76 @ra2md.mix`），
   加载器已就绪、缺素材；剩余 164 种 rules 键 / 95 种 art 键的类型化
   （顺序见 `--typetable --top N` 表尾）；声音/曲目接进 AudioDevice。
3. **P3 已完成两步**：
   - 第一步：对象字段偏移表（`tools/fieldscan.py` + `db/fields.json` +
     `src/re/FieldOffsets.h`），646 个类 / 6965 条。四条独立证据：
     sizeof 越界 4（207 通过）、RTTI 位移 387 处**一条不漏**（159 处可判定全中
     + 228 处同链写入）、继承链末端严格递增。
   - 第二步：**对象字段名表**（`tools/fieldname.py` + `db/fieldnames.json` +
     `src/re/FieldNames.h`），6 个类 / 396 条，来自二进制自己的 `Read_INI`。
     396 条里 300 条被构造函数扫描独立看到且宽度一致，sizeof 越界 0，
     6 条手工反汇编锚点进冒烟测试。
   **下一步**：其余类的字段名（构造函数赋常量、或逻辑里才算出来的字段，
   不能再靠"读 INI"这一条通道）；P3 剩余还有静态对象池类的 sizeof。
4. P4 锁步、P5 多核并行。
5. ~~`db/sizes.json` 里 `CCFileClass` 的 sizeof 取错（36，应为 108）~~
   —— **本轮已修**：`tools/sizeofscan.py` 用字段末端硬过滤候选，
   `calibrated` 条目无条件进 `src/re/ObjectSizes.h`。剩 4 个类
   （`CounterClass`、`CCINIClass`、`BufferIOFileClass`、
   `PAVReestablish::?$VectorClass`）所有候选都小于字段末端，两者必有一错，
   **不改**，只记 `note` 留给人看。
