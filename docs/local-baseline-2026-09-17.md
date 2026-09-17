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

## 2. 改动清单（8 个文件）

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

1. **P1 收尾**：精灵批渲染（`SpriteBatch`）—— 一个 draw call 画几千个精灵；
   以及与原版截图逐像素比对（当前体素出图已通，但明暗/阴影尚未对拍）。
2. **P2 剩余**：把 INI 全量填进 `TechnoTypeClass`（现在只到"模型组成"这一跳），
   然后 `ai.ini` / `sound(md).ini` / `theme(md).ini`。
3. `GamePaths::map_mixes` 仍指向不存在的 `MAPSMD03.MIX` —— 本机地图在
   `Maps/`（`Standard` / `Custom` / `Cooperative` / `MadHQ` / `DDLY` 五个子目录），
   应该改成扫目录。目前只影响 `--map` 的默认查找，不影响其它回归。
4. 代码尚未 commit（本次改动全在工作区）。
