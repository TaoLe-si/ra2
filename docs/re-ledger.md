# 逆向完成清单（RE-Complete Ledger）

> 纪律：每个子系统先把原版函数/数据/常量逆到可复述（本文件留证据），
> 再写实现；实现与证据对不上 = 违约。禁止任何"看着像"的编造。

| 原始文件 | 函数数 | 指令数 | 代码区间 | 实现落点 | RE 状态 | 实现状态 |
|---|---:|---:|---|---|---|---|
| `AbsType.cpp` | 2 | 128 | `0x410960` | — | 待挖 | 未开始 |
| `Beacon.CPP` | 1 | 268 | `0x430BA0` | — | 待挖 | 未开始 |
| `Connect.CPP` | 1 | 284 | `0x48C040` | — | 待挖 | 未开始 |
| `Conquer.CPP` | 2 | 285 | `0x48C8B0` | — | 待挖 | 未开始 |
| `coopcamp.cpp` | 1 | 719 | `0x49DB00` | — | 待挖 | 未开始 |
| `Credits.CPP` | 1 | 202 | `0x4A2370` | — | 待挖 | 未开始 |
| `Display.CPP` | 1 | 140 | `0x4AE4F0` | — | 待挖 | 未开始 |
| `Dropship.cpp` | 1 | 2962 | `0x4B6C30` | — | 待挖 | 未开始（空投界面） |
| `Egos.CPP` | 1 | 812 | `0x4C3E30` | — | 待挖 | 未开始 |
| `GDlgSupp.cpp` | 14 | 1179 | `0x4E38A0` | — | 待挖 | 未开始（对话框支持库 ← 主菜单/GraphicMenu 相关） |
| `House.CPP` | 7 | 2387 | `0x4F9B70` | game/World Houses | 待挖 | 部分（经济/AI/Base） |
| `Ini.CPP` | 1 | 159 | `0x529160` | — | 待挖 | 未开始 |
| `Init.CPP` | 4 | 2440 | `0x52BA60` | — | 待挖 | 未开始（全局初始化） |
| `Ion.cpp` | 4 | 537 | `0x539EB0` | — | 待挖 | 未开始 |
| `LdPrgMgr.cpp` | 1 | 1426 | `0x552D60` | — | 待挖 | 未开始 |
| `LoadDlg.CPP` | 2 | 1035 | `0x558DD0` | — | 待挖 | 未开始 |
| `MainLoop.CPP` | 1 | 986 | `0x55E420` | engine/GameLoop | 待挖 | 已还原（分阶段主循环） |
| `MapGen.cpp` | 4 | 1575 | `0x595680` | — | 待挖 | 未开始 |
| `MapSel.CPP` | 2 | 142 | `0x5ADD10` | — | 待挖 | 未开始 |
| `ModemGst.cpp` | 3 | 955 | `0x5B4EE0` | — | 待挖 | 未开始 |
| `ModemHst.cpp` | 5 | 1873 | `0x5B82F0` | — | 待挖 | 未开始 |
| `MPCoop.cpp` | 2 | 296 | `0x5C21D0` | — | 待挖 | 未开始 |
| `MPLayer.CPP` | 1 | 27 | `0x5C60D0` | — | 待挖 | 未开始 |
| `MPObserver.cpp` | 1 | 23 | `0x5C9470` | — | 待挖 | 未开始 |
| `MPSiegeTeam.cpp` | 2 | 46 | `0x5CAE10` | — | 待挖 | 未开始 |
| `MSChoice.cpp` | 1 | 580 | `0x5CF8E0` | — | 待挖 | 未开始 |
| `Multiplayer.cpp` | 1 | 472 | `0x5D7590` | — | 待挖 | 未开始 |
| `NetDlg.CPP` | 1 | 292 | `0x5DA750` | — | 待挖 | 未开始 |
| `netdlg2.cpp` | 7 | 4640 | `0x5DAFE0` | — | 待挖 | 未开始（网络对话框） |
| `netshare.cpp` | 9 | 4596 | `0x5E39C0` | — | 待挖 | 未开始（网络共享） |
| `NullDlg.CPP` | 2 | 613 | `0x5F0690` | — | 待挖 | 未开始 |
| `NullMgr.CPP` | 3 | 1303 | `0x5F1FA0` | — | 待挖 | 未开始 |
| `Options.CPP` | 1 | 110 | `0x5FC000` | — | 待挖 | 未开始 |
| `ownrdraw.cpp` | 3 | 1060 | `0x604060` | — | 待挖 | 未开始（游戏内绘制指令） |
| `PhoneEd.cpp` | 3 | 482 | `0x630EA0` | — | 待挖 | 未开始 |
| `PlanMgr.cpp` | 11 | 1384 | `0x637270` | — | 待挖 | 未开始 |
| `Power.CPP` | 1 | 22 | `0x640450` | — | 待挖 | 未开始 |
| `Queue.CPP` | 6 | 3308 | `0x6475F0` | engine/FrameQueue | 待挖 | 已还原（锁步+CRC） |
| `Radar.CPP` | 1 | 262 | `0x653FA0` | game/GameShell Draw_Radar | 待挖 | 部分（矩形链+内缩 RE，缩略图自绘） |
| `Restate.cpp` | 1 | 123 | `0x65F520` | — | 待挖 | 未开始 |
| `Scenario.CPP` | 8 | 3355 | `0x683560` | game/World + map/MapFile | 待挖 | 部分（段解析/对象投放/触发） |
| `SendFile.CPP` | 2 | 1200 | `0x6941A0` | — | 待挖 | 未开始 |
| `SerialEd.cpp` | 4 | 761 | `0x695BC0` | — | 待挖 | 未开始 |
| `Session.CPP` | 5 | 1729 | `0x6977C0` | — | 待挖 | 未开始 |
| `Sidebar.CPP` | 2 | 1171 | `0x6A9540` | game/GameShell 侧栏 | 待挖 | 部分（布局/页签/雷达，cameo 生产链） |
| `Skirmish.cpp` | 2 | 1674 | `0x6ACEE0` | — | 待挖 | 未开始（遭遇战界面） |
| `Startup.CPP` | 1 | 3740 | `0x6BB9A0` | game/GameMain | 待挖 | 部分（入口流程） |
| `Super.CPP` | 1 | 49 | `0x6CC2B0` | — | 待挖 | 未开始 |
| `Tactical.CPP` | 1 | 1004 | `0x6D3D10` | game/GameShell 战术视口 | 待挖 | 部分（相机/滚动/缩放） |
| `ToolTip.cpp` | 1 | 72 | `0x724AD0` | — | 待挖 | 未开始 |
| `UICmnds.cpp` | 6 | 1052 | `0x731AF0` | — | 待挖 | 未开始 |
| `WDTProps.cpp` | 1 | 49 | `0x76BF00` | — | 待挖 | 未开始 |
| `WDTSel.cpp` | 7 | 1384 | `0x76C290` | — | 待挖 | 未开始 |
| `WDTTerr.cpp` | 1 | 165 | `0x76F970` | — | 待挖 | 未开始 |
| `WOLPersonaInformation.cpp` | 5 | 1321 | `0x778F30` | — | 待挖 | 未开始 |
| `wonline.cpp` | 43 | 14049 | `0x77DC90` | — | 待挖 | 未开始 |
| `WorldDom.cpp` | 1 | 218 | `0x7AFB90` | — | 待挖 | 未开始 |
| `wwmous.cpp` | 6 | 488 | `0x7B8730` | — | 待挖 | 未开始 |

## 全量语料落盘（redump_all.py 生成）

| 子系统 | 函数数 | 指令数 |
|---|---:|---:|
| misc_007C0000 | 304 | 16074 |
| misc_007D0000 | 286 | 19249 |
| misc_00400000 | 213 | 8384 |
| misc_00720000 | 206 | 10442 |
| misc_00480000 | 165 | 14016 |
| misc_00470000 | 156 | 12291 |
| misc_006C0000 | 154 | 6552 |
| misc_004A0000 | 144 | 11789 |
| misc_00650000 | 144 | 12285 |
| misc_007B0000 | 144 | 12416 |
| misc_006E0000 | 141 | 11400 |
| misc_00630000 | 134 | 9618 |
| misc_00750000 | 134 | 10999 |
| misc_00430000 | 124 | 11239 |
| misc_005F0000 | 124 | 10239 |
| misc_00550000 | 120 | 11457 |
| misc_00620000 | 118 | 11485 |
| wonline | 116 | 25020 |
| misc_00680000 | 111 | 9589 |
| misc_004C0000 | 105 | 8768 |
| misc_00760000 | 105 | 8710 |
| misc_00540000 | 104 | 11936 |
| misc_00670000 | 104 | 17346 |
| misc_00700000 | 104 | 11588 |
| misc_00520000 | 99 | 9643 |
| House | 98 | 13856 |
| misc_005A0000 | 98 | 15286 |
| misc_00690000 | 96 | 7017 |
| misc_006D0000 | 94 | 11325 |
| misc_00420000 | 92 | 11716 |
| misc_00410000 | 91 | 7279 |
| misc_00580000 | 91 | 15370 |
| misc_005D0000 | 90 | 7231 |
| netshare | 90 | 11877 |
| misc_00450000 | 89 | 9892 |
| misc_00530000 | 88 | 9446 |
| misc_006B0000 | 87 | 8405 |
| misc_00710000 | 86 | 11871 |
| misc_005C0000 | 85 | 6761 |
| misc_005B0000 | 83 | 8011 |
| misc_00570000 | 80 | 17918 |
| misc_00770000 | 79 | 5499 |
| misc_004D0000 | 75 | 10999 |
| misc_00740000 | 72 | 8020 |
| misc_006F0000 | 70 | 14674 |
| ownrdraw | 68 | 14544 |
| misc_00640000 | 67 | 5936 |
| misc_00510000 | 64 | 6805 |
| misc_00560000 | 64 | 11784 |
| misc_007A0000 | 64 | 3843 |
| misc_004F0000 | 63 | 6020 |
| misc_00500000 | 63 | 3731 |
| misc_00660000 | 63 | 16806 |
| misc_00730000 | 61 | 5979 |
| misc_004E0000 | 53 | 2037 |
| misc_00460000 | 51 | 7766 |
| misc_00490000 | 51 | 4998 |
| misc_006A0000 | 51 | 9304 |
| PlanMgr | 44 | 3847 |
| misc_004B0000 | 42 | 7055 |
| GDlgSupp | 37 | 2853 |
| misc_005E0000 | 34 | 2475 |
| misc_00590000 | 32 | 10179 |
| Scenario | 30 | 6764 |
| Queue | 28 | 12725 |
| UICmnds | 27 | 2077 |
| WOLPersonaInformation | 25 | 2424 |
| WDTSel | 20 | 2923 |
| misc_00600000 | 20 | 2454 |
| netdlg2 | 20 | 6423 |
| Session | 19 | 3452 |
| Ion | 18 | 1201 |
| MapGen | 17 | 4110 |
| misc_00440000 | 16 | 4729 |
| Init | 15 | 4862 |
| ModemHst | 15 | 2559 |
| ModemGst | 11 | 1960 |
| Sidebar | 10 | 2211 |
| wwmous | 10 | 883 |
| SerialEd | 7 | 1093 |
| Skirmish | 7 | 2332 |
| PhoneEd | 6 | 759 |
| misc_007E0000 | 6 | 318 |
| AbsType | 4 | 172 |
| NullMgr | 3 | 1303 |
| Conquer | 2 | 285 |
| LoadDlg | 2 | 1035 |
| MPCoop | 2 | 296 |
| MPSiegeTeam | 2 | 46 |
| MapSel | 2 | 142 |
| NullDlg | 2 | 613 |
| SendFile | 2 | 1200 |
| Startup | 2 | 4538 |
| Beacon | 1 | 268 |
| Connect | 1 | 284 |
| Credits | 1 | 202 |
| Display | 1 | 140 |
| Dropship | 1 | 2962 |
| Egos | 1 | 812 |
| Ini | 1 | 159 |
| LdPrgMgr | 1 | 1426 |
| MPLayer | 1 | 27 |
| MPObserver | 1 | 23 |
| MSChoice | 1 | 580 |
| MainLoop | 1 | 986 |
| Multiplayer | 1 | 472 |
| NetDlg | 1 | 292 |
| Options | 1 | 110 |
| Power | 1 | 22 |
| Radar | 1 | 262 |
| Restate | 1 | 123 |
| Super | 1 | 49 |
| Tactical | 1 | 1004 |
| ToolTip | 1 | 72 |
| WDTProps | 1 | 49 |
| WDTTerr | 1 | 165 |
| WorldDom | 1 | 218 |
| coopcamp | 1 | 719 |
| **合计** | **6740** | — |

## 主菜单子系统（2026-09-17 深挖定案）

**数据源已全部定位（WDT.MIX，6.2MB，TS 加密头 0x00030000，126 条）：**

| 内容 | 证据 |
|---|---|
| 菜单布局（二进制 WDT 格式，~80 条） | 头 `{u16 0, u16 w, u16 h, u16 type}`，每分辨率一套 |
| BIK 视频（菜单动画，3 条：120K/133K/133K） | 条目头 "BIKi" |
| PCX 背景（每分辨率，~17 条，8K~780K） | 条目头 0x0A 05 01 08（ZSoft PCX） |
| 调色板（768B，6 条） | 条目 3f003f... |
| 文本定义（3 条） | `[FactionChoiceMenu640/800]`（Background/Theme/Items×Image/Origin/ActiveRect/Highlighted/HighlightSound）、`[LAYOUTS]`（战役地图屏）、`[PublicKey/PrivateKey]` |

**exe 侧对应机制（gamemd）：**
- WDT.MIX 挂载 @0x7AFC54（还有 WDTVOX.MIX、Local.MIX、'WDTTheater%02.VQA'）
- 每分辨率初始化跳转表 @0x7B0074（5 档）
- GraphicMenu 类：Background/Intro/Theme/Palette 键（ctor @0x4F1CA0/0x4F2140）
- GraphicMenuItem：Type=Image/Anim、Image（如 sdbtnanm.shp）、Origin、ActiveRect、
  Highlighted（高亮态图）、HighlightSound（如 Choice1.AUD）、Rate=5、Loop
- 主菜单 = RT_DIALOG 226（.rsrc，533×369 du，11 控件）
- 加载器链：0x7AF500（WDT 加载）、0x52FEC0、0x6241F0

**1:1 还原所需实现清单（按依赖序）：**
1. WDT.MIX 挂载（已有 TS 加密 MIX 支持）+ WDT 二进制布局解析
2. SDBTNANM.SHP 按钮动画（Rate=5 帧/步，hover 升起动画）+ xyz.pal
3. 高亮态图切换 + HighlightSound（Choice1.AUD —— AUD 解码器已就绪）
4. BIK 视频解码（菜单背景动画）—— Bink 格式，最大单项
5. 每分辨率美术选择（0x7B0074 跳转表语义）

## WDT 二进制格式（进行中，2026-09-17 第二轮）

**条目结构（已解，条目 0 实测 4576B）：**
```
+0   10B 头：{u16 0; u16 w; u16 h; u16 type=4; u16 0}
+10  N × 24B 记录：{u16 0; u16 w; u16 h; u16 type=3; u16 0;
     u32 asset_id; u32 0; u32 data_offset; u16 0}
     —— data_offset 指向本条目内嵌的图像数据块
+106 图像数据块（4 块：offsets 104/688/1984/3280）
```
- 记录里的 asset_id（0x00E0E2F4 等）为资产引用（跨条目/跨 mix 待确认）
- 内嵌图像数据：RLE-Zero 风格但**不是整块直解**（解出 590/1280/1295/1270 像素，
  与 w×h=1638 不齐）——组块化编码，真格式在 0x624130（0x7AF500 加载链下层）
- 0x7AF540 = 分辨率选择状态机（比较的是**对话框单位**而非像素：
  0x10..0x16C / 0x82..0x12C / 0xAF..0xF5 / 0xFC..0x1D2 / 0xD4..0x1F0 五档，
  对应 0x7B0074 跳转表的 5 个分支）

## 主菜单对话框机制（2026-09-17 第三轮，已解）

**dialog 226 完整控件表（DLGTEMPLATEEX 逐字节解析）：**
```
ctl 0  id=0x03EE BUTTON 'GUI:ExitGame'         (425,330 108x23) WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON
ctl 1  id=0x0683 BUTTON 'GUI:SinglePlayer'     (425,125 108x23)
ctl 2  id=0x0684 BUTTON 'GUI:WWOnline'         (425,152 108x23)
ctl 3  id=0x055C BUTTON 'GUI:Options'          (425,233 108x23)
ctl 4  id=0x0578 BUTTON 'GUI:Network'         (425,179 108x23)
ctl 5  id=0x0694 STATIC 'GUI:MainMenu'         (425,1   108x10) 标题
ctl 6  id=0x0695 STATIC 'GUI:Blank'            (2,355  303x12) 底部状态条
ctl 7  id=0x0686 BUTTON 'GUI:MoviesAndCredits'(425,206 108x23)
ctl 8  id=0x071A STATIC 无标题                 (0,0    304x266) ← 图像控件（左上大图区）
ctl 9  id=0x071C STATIC 无标题                 (447,29 61x33)  ← 图像控件（右上小图）
ctl10  id=0x071D STATIC 'GUI:Blank'            (425,357 108x10) 底部右侧条
```
**按钮全是标准 Windows BUTTON**（无 owner-draw 标志）——由游戏的自绘对话框
框架画（非 Windows 原生按钮样式）。

**对话框切换机制（窗口过程 0x78DA70）：**
- 自定义消息 `0x4E3` = 弹栈切页（处理 @0x78DB13 → 0x7757E0：对话框栈
  @0xB72D20[0xB72F50 个] 弹到第 N 层，DestroyWindow 上层，SetWindowPos 顶层）
- `0x4E4` = 顶层重置（@0x78DAE0）
- 0x52B9B0 = ctl8(0x71A) 图像控件初始化：GetDlgItem(0x71A) → SetWindowPos
  按屏幕宽（≤800 时减半边距）居中 → SendMessage(0x71A, 0x4E3, 1) 启动
  图形；**≤0x280=640 时消息 0x4E4 带 'Ra2ts_s'，否则 'Ra2ts_l'**
  （_s=small/_l=large：按分辨率的图像变体名）
- 0x531D89 = SendMessage(0x4E3) 的另一个调用点（对话框流程切换）
- 0x46DF40 = 控件可见性/禁用批量链（0x522/0x6A3/0x6A4/0x6D1/0x5A8/0x71C/0x468
  逐个 GetDlgItem → EnableWindow/ShowWindow）

## 对话框文本/颜色系统（2026-09-17 第四轮，已解）

**文本绘制 @0x5BD3D0**（对话框框架的文字 blit）：
- 逐字符绘制：`TextOutA(hdc, x*0xABF1E0 + 10, y*(0xABF1C8+0xABF1D8) + 10, &ch)`
- 颜色选择：色索引 0 → SetTextColor(0xABF110)、1 → 反色（先 SetBkColor 再 SetTextColor 互换）
- **颜色 = GetSysColor**（@0x5BC618/0x5BC621 初始化）：
  `0xABF128 = GetSysColor(0xF = COLOR_BTNTEXT)`、`0xABF110 = GetSysColor(8 = COLOR_WINDOWTEXT)`
  —— 对话框文字是**系统色**，非硬编码
- 字体：CreateFontIndirectA（LOGFONT 从对话框模板的 font 字段），GetTextMetricsA
  填 0xABF1C8/0xABF1D8（宽高步进）→ 像素步进公式 *(w + dw) / x*dw? （0x5BD45E 的
  `imul eax, esi` + `imul ecx, ebx`）

**初始化链 @0x5BC5C0**：CreateFontIndirect → SelectObject → GetTextMetrics →
GetSysColor(15/8) → AdjustWindowRect（对话框客户区对齐）——**整个对话框文本
渲染是 GDI 真窗口系统**（CreateDialogIndirectParamA + TextOutA），不是游戏
DirectDraw 表面上的自绘！对话框（含主菜单 226）是**真 Win32 窗口**。

**架构结论**：原版主菜单 = 真 Win32 对话框（CreateDialogIndirectParamA）
叠在游戏表面上；按钮是标准 BUTTON 控件、文字走 GDI TextOut、颜色系统色。
"还原"它的 1:1 路径 = 保留 GDI 窗口方案或完全复刻其绘制参数（字体/颜色/步进）。

## 归档挂载顺序与扩展包编号（2026-09-17，已解）

**证据**：`gamemd.exe` 的 `.data` 里有一张连续的归档名表，偏移 `0x4266??` 一带：

```
ELOCAL*.MIX   ECACHE*.MIX   " LOCAL.MIX"  "LOCAL.MIX"  "LOCALMD.MIX"
"CACHE.MIX"   "CACHEMD.MIX" " CACHE.MIX"
"RA2.MIX"     "RA2MD.MIX"
" %s"         "EXPANDMD%02d.MIX"        <-- 编号循环，不是固定 01
"MIXFILES\MOVMD03.MIX" "MOVMD03.MIX" "MIXFILES\MOVMD*.MIX" ... "MOVIES*.MIX"
" THEME.MIX"  "     Initializing ThemesMD.MIX"   " MULTIMD.MIX"
```

因此挂载顺序（后挂的覆盖先挂的）：

```
LOCAL.MIX/LOCALMD.MIX -> CACHE.MIX/CACHEMD.MIX -> RA2.MIX -> RA2MD.MIX
  -> EXPANDMD%02d.MIX（i 从 01 到 99，按号升序）-> THEME.MIX/THEMEMD.MIX
  -> MULTIMD.MIX
```

**为什么这条必须记住**（踩过）：

- 代码里原先把扩展包写死成 `expandmd01.mix`。**这是错的**，游戏自己按
  `EXPANDMD%02d.MIX` 循环挂载。
- 后果在改版安装上立刻可见：Reunion 2023 把 `RULESMD.INI`(0x8218F9F4) /
  `ARTMD.INI`(0x5B47D8D5) 放在 **expandmd01.mix**，把 `RULES.INI`(0xF025A96C) /
  `Art.ini`(0xF91B2C8B) 放在 **expandmd97.mix**。
  只挂 `ra2.mix + ra2md.mix + expandmd01` 时，`--unitdb` 直接报
  「这些 MIX 里一份 RULES/ART 都没有」。
- 正确的挂载列表（实测该安装共 10 个包）后，`--unitdb` 复现基线：
  `段 rules=1482 art=1594 | 单位 559 | 体素 86 | 炮塔 17 | 炮管 7 | 车体缺失 0`。

落地：`GameInstall::Resolve`（`src/core/GameVersion.cpp`）改为枚举 `expandmd%02d.mix`
（i=1..99，存在的才推入）并追加 `thememd.mix`；`ra2view.exe` 新增 `--gamedir <游戏目录>`
单点入口，`--addmix` 改为可重复（同时挂多个叠加归档）。

## 构建环境（2026-09-17）

- **源码是 UTF-8 无 BOM，必须带 `/utf-8` 编译。** `cl.exe` 默认按系统 ANSI 代码页
  读源文件；中文 Windows（ACP=936）下会把 UTF-8 字节按 GBK 解码，把引号/反斜杠
  吞进"字符"里，于是报满屏 `C2001 字符串字面量中的换行符` + `C3688 伪造文本后缀`，
  外加 32 处 `C4819`。加上 `/utf-8` 后错误与警告**全部归零**（实测：48 error -> 0）。
  已补进 `tools/build.py` 的 `CFLAGS`、`CMakeLists.txt`（MSVC 分支）、
  `tools/build-msvc.bat`。作者机器应开了 UTF-8 系统区域设置（ACP=65001），所以没暴露。


## DX12 批渲染：`StartInstanceLocation` 不可用（2026-09-17）

这是本项目**第一个"照规范写、结果不对、且不对的原因在 API 语义上"**的坑，
单独记一笔，因为它的症状极具误导性。

### 想做的事

把 N 个精灵合成一次 draw：

```cpp
cmd->DrawInstanced(6, n, 0, first);   // first = 本批在实例缓冲里的起点
```

顶点着色器里 `BatchInst it = inst[SV_InstanceID];` —— 第 4 个参数按 D3D12 的
签名就是 `StartInstanceLocation`，会把 `SV_InstanceID` 整体偏移。
这样"整帧一次 memcpy、每批只报一个起点"就够了，不必为每批单独切缓冲。

### 实测结果

**起点没有生效**：每一批都从实例 0 开始读。症状是一屏只剩一个精灵在同一个位置
被反复画，而纯色批次（界面矩形）把实例 0 的矩形用 `PSSolid` 画成一片白 ——
看起来像"贴图错了"或"图集没写上"，实际跟图集毫无关系。

定位手段（值得复用）：

1. `RA2_BATCH_DEBUG=1` 打印每批的 `kind / n / first` 与首条实例的 rect/uv/color
   → 证明 **CPU 侧数据完全正确**（`first` 依次为 0、102、104、317、410）；
2. 再打印图集/实例缓冲/调色板的描述符 GPU 句柄
   → 证明**没有别名**（base+0x40/+0x80/+0xC0/+0x100，各就各位）；
3. 加开关 `RA2_BATCH_ONE=1`（每条实例单独一批）
   → CPU 数据仍正确、画面仍然只有一个精灵，**排除"多实例偏移"这一整类猜想**；
4. 对照既有可用路径：体素烘焙用的是 `DrawInstanced(6, count, 0, 0)`
   （`StartInstanceLocation` 恒 0），它是好的 —— 差异只落在"非 0 起点"上。

### 落地

不再依赖该参数：批次起点改由根常量传给顶点着色器，自己加下标。

```hlsl
cbuffer BatchC : register(b1) { uint batchBase; };
BatchInst it = inst[batchBase + SV_InstanceID];
```

```cpp
cmd->SetGraphicsRoot32BitConstant(4, first, 0);
cmd->DrawInstanced(6, n, 0, 0);
```

改完**逐字节对拍通过**（3145728 字节 0 处不同）。

### 验收方式（这条比结论更重要）

批渲染"画面对不对"肉眼不可信 —— 第一版看着就很"像那么回事"。
唯一可信的判据是：**同一个场景，开/关批渲染各跑一遍，回读的 RGBA 必须逐字节相同**。
为此专门留了 `RA2_NO_SPRITE_BATCH=1`，它走的是**完全没被改动过**的老路径。

## 地图归档的命名（2026-09-17 补正）

`MAPS%02d.MIX` / `MAPSMD%02d.MIX`（EXE 内 `.data` 归档名表）：

```
0x41C2C4  "MAPS%02d.MIX"       0x426790  "MAPS*.MIX"
0x41C2EC  "MAPSMD%02d.MIX"     0x42679C  "MAPSMD*.MIX"
```

和 `EXPANDMD%02d.MIX` 一样是**编号循环**，外加一次通配扫描（`FindFirstFile` 那种用法）。
原先代码里写死的 `MAPSMD03.MIX` / `maps02.mix` 把"03"当成常量，
换一份带 `mapsmd01.mix` 的安装就整个找不到地图，已改成枚举 + 通配。

实测那份 Reunion 2023：`MAPS*.MIX` 一个都没有，540 张 `.map` 全在 `Maps/`
（5 个子目录）下。**注意这条只记布局、不算引擎行为**：
exe 里既没有 `*.MAP` 通配串、也没有 `Maps` 目录名串，
"引擎自己扫这几个目录"尚未证实（谁在扫留到 P3 读 `MapSelect` 反汇编再定）。
落地为 `GamePaths::map_dirs` + `GameInstall::Find_First_Map`。

## 类型表打表的实证（2026-09-17）

### 键的清点是"数据即证据"

P2 打表最容易犯的错是**按印象编键名**。所以先清点：`tools/inikeys.py`
把 rules.ini / rulesmd.ini / art.ini / artmd.ini 全扫一遍，数出
**单位段 477 种键 / art 的 `[Image]` 段 206 种键**（原始输出 `build/_inikeys.txt`）。
`src/data/TypeDB.h` 的 X-macro 里每一个键都能在那份清点里找到，
且有个自检反过来卡：**声明了却在数据里一次都不命中的键必须为 0**。

### 键的真实类型只能看数据

不看数据会犯的错，实测抓到一个：

```
Occupier=yes ; I can Occupy UC buildings       ← 是布尔，不是武器名
Assaulter=no ; I clear out UC buildings        ← 同上
OccupyWeapon=UCPara                            ← 这个才是武器名
EliteOccupyWeapon=...
```

`Occupier` 名字长得像"占领武器"，第一版就当武器字段收了。现在改成 BOOL，
并新增 `occupy_weapon`（`OccupyWeapon=`）。同类还有：

| 键 | 真实类型 | 取值样本 |
|---|---|---|
| `ExtraDamageStage` | BOOL | `no` |
| `EntryDamageStage`（若有） | — | — |
| `AllowedToStartInMultiplayer` | BOOL | `no` |
| `LightVisibility` | INT | `5000`（lepton 距离） |
| `LightIntensity`/`LightRedTint`… | DBL | `0.2` / `0.05` |
| `SpecialThreatValue` | DBL | `1`（注释写 "between 0 and 1"） |
| `NumberImpassableRows` | INT | `3` |

**空值要分两种**：`Report=` 这种"键在、值为空"与"键不存在"是两回事
（`Atoi("")==0`，但 `strtod("")` 一个字符都没吃 → 返回缺省）。
实测原始 INI 有 214 处空值，所以这条区分不是纸面上的。

### 列表里有名字、rules 里没有段（原版数据自身的不一致）

四个类型列表共 559 个名字，其中 **6 个在全库找不到对应段**：

```
YDUM  APACHE  CASYDN01  CATIME  CALA02  CALOND02
```

`[AircraftTypes]` 里明明白白写着 `1=APACHE`，但 `[APACHE]` 段在
rules.ini / rulesmd.ini 里都不存在（RA2 时代的老机型，YR 的 rulesmd
列表没清干净）。这不是解析错 —— 打表时跳过并**如实记名**，
`--typetable` 会把它们印出来。

### `[Warheads]` 是权威登记表，武器没有对应列表

- 弹头：`[Warheads]` 编号列表 **105** 项（引擎自己的名单），
  加上武器引用到的，去重后 116 个。
- 武器：**没有** `[Weapons]` 列表。全库里"有 `Damage=` 且 `Warhead=`"的段有
  **272** 个，但游戏实际引用的（Primary/Secondary/Elite*/Weapon1..5/
  DeathWeapon/OccupyWeapon）只有 **189** 个。表收后者（P3 需要的集合），
  前者只作统计参照报出来。

### theme 的同名曲目是**真的重复**，不是合并 bug

`[Themes]` 是编号列表，合并后同时存在：

```
1=INTRO  2=SCORE  3=LOADING  4=CREDITS      ← RA2 的 theme.ini
21=BrainFreeze … 28=TranceLVania            ← RA2MD 新增
31=INTRO 32=Grinder … 47=RA2Options 48=Motorized 50=HM2 … 56=Rollout
```

于是 `INTRO / SCORE / LOADING / CREDITS` 各出现两次（编号不同）。
**引擎按编号索引，所以它看到的也是这样** —— 忠实还原，不做去重。
另有 `49=`（值为空）与 `47=RA2Options`（无同名段）两种边角，都已按原样处理。

### 探针本身的 bug：顶层 `find` 漏掉嵌套条目

`tools/inikeys.py` 第一版用 `MixFile.find()` 找 INI 名字，那是**只看顶层**的；
C++ 侧走的是 `Read_Deep`（`Build_Deep_Index` 建的全局平表，会进嵌套 MIX）。
结果：探针报 `THEME.INI 未找到`，C++ 却多读出一份 RA2 的主题表（12 条）。
**不是代码 bug，是探针覆盖不全。** 现在 `tools/techno.py` 里照抄了
`Build_Deep_Index` 的顺序（先本层全部、再按条目顺序逐个子归档，
一律先到先得），两边才真正对齐。

### `AI.INI` / `AIMD.INI`：本安装里不存在

旧基线的名字表里有这两个 CRC（`AI.INI=0x9E11E49A @ra2.mix`、
`AIMD.INI=0x116F3F76 @ra2md.mix`），但 Reunion 2023 的 8 个归档
（ra2/ra2md/expandmd01/94/95/96/97/thememd）**一个都没有**。
加载器已就绪，`--typetable` 如实打 `ai.ini 无`，不假装读过。

## 对象字段偏移：从构造函数里挖（2026-09-17，P3 的第一块地基）

RTTI 给类名、继承关系、虚表槽位，**但不给字段偏移**。没有字段偏移，
还原出的 C++ 结构体就只是空壳：读到 `[esi+0x1C]` 时不知道那是什么。
所以 P3 的第一件事是把偏移表建出来。

### 办法

构造函数是唯一会从头到尾把对象每个字段初始化一遍的地方，特征也强：
它会把虚表指针写进对象首字段。于是扫每个函数体、跟踪 this 指针
（thiscall 入口 `ecx`），把 `mov [this+off], ...` 记成字段访问。

this 的流转要认全，少一条就整段丢：

```
mov r, ecx              r 也是 this（MSVC 常先 mov esi, ecx）
lea r, [this+d]         r 是 this+d，之后 [r+k] 的偏移是 d+k
mov [ebp-d], ecx        把 this 溢出到栈槽
mov r, [ebp-d]          从栈槽取回 this —— 大对象的构造函数很常见
任何其他写寄存器的指令   取消跟踪（含 call 破坏 eax/ecx/edx）
```

### 三个真踩到的坑

**1. 归属判据必须是"有效偏移恰为 0"，不是指令里的 `disp == 0`。**
`MouseClass` 的构造函数在 `[esi+0x5518]` 处内嵌构造一个 INoticeSink
子对象，编译出来是：

```
lea edi, [esi+0x5518]
mov dword ptr [edi], 0x7e1fbc     ; ??_7INoticeSink@@6B@
```

指令里的 disp 正是 0。只看 disp，就会把 MouseClass 的构造函数整个
记到 INoticeSink 头上 —— 症状是 INoticeSink（92 字节）冒出 +0x5544。
改成"寄存器跟踪偏移 + disp"的有效偏移后，一次消掉 13 个越界。

**2. 线性兜底的"函数"与真实函数重叠，起点落在指令中间。**
`analyze.py` 除了按调用点播种，还有一道线性兜底。实测 `0x4739E7` 与
`0x4739F0` 是同一段代码，前者早 9 字节。从指令中间开始反汇编会先吐出
几条垃圾指令再重新同步，跟踪状态已经被污染。
修法：逐字节占用表判重叠，非 gap 优先、长的优先。

**3. `.gitignore` 里的 `re/` 会把 `src/re/*.h` 一起挡掉。**
不带前导斜杠的模式匹配**任意层级**的 `re` 目录。`ObjectSizes.h` /
`ClassHierarchy.h` 因为更早加入所以逃过了，新生成的 `FieldOffsets.h`
被静默忽略 —— `git add -A` 之后 `git status` 干净得看不出问题。
改成锚定的 `/re/`。

### 三条独立证据（互相对拍，不靠单点结论）

| 证据 | 来源 | 结果 |
|---|---|---|
| sizeof（`push N; call operator new` 配对） | 与字段扫描**无关**的另一套分析 | 206 个类有基准，201 个字段末端落在界内 |
| RTTI 的嵌入基类位移 `mdisp` | PE 的 RTTI 段（数据，不是指令） | 387 处里 159 处在 .text 里找到对应写虚表指令 |
| 字段末端沿继承链严格递增 | 类层次 | 0x21→0xAC→0xD4→0xEE→0x520→0x6B9→0x6E8 全递增 |

**最漂亮的一条交点**：MSVC RTTI 说 `TechnoClass` 的 `FlasherClass` 基类
子对象在偏移 240、`StageClass` 在 248。字段扫描在 `TechnoClass` 的
0xF0 与 0xF8 上各找到一次虚表写入 —— 一个来自 PE 的 RTTI 段，
一个来自 .text 的指令流，两边完全独立却对上了。

**顺带纠正了 sizeof 表的一条**：`CCFileClass` 的字段末端是 0x6C = 108，
而 `db/sizes.json` 给的是 36。它的候选列表里本来就有 `108: 2` 票，
只是当初选了 36 —— 字段扫描独立地证明了 36 是错的。这类越界现在
会自动判定并写进 `docs/fields.md`，不静默吞掉。

### 明确没做到的

- 5 个类越界（`CounterClass`、`CCINIClass`、`PAVReestablish::?$VectorClass`
  判定为"疑似扫描错"，仍在册；`CCFileClass` / `BufferIOFileClass`
  判定为 sizeof 基准错）。
- **字段名未知**。这里只给"这个偏移上确实有这么一个宽度的字段"，
  语义要等把访问该偏移的代码读通（例如 `TechnoTypeClass::Read_INI`
  的读取顺序，可以和第 P2 轮的 INI 键表对照）才能填。
- 只扫了**写虚表的函数**。只被游戏逻辑赋值、构造函数不碰的字段收不到；
  下一步是靠"读某偏移的代码 + 该偏移在别的类里的语义"补齐。

落地：`tools/fieldscan.py` + `db/fields.json`（646 个类 / 6965 条）
+ `docs/fields.md` + `src/re/FieldOffsets.h`（核心继承链 17 个类，
在 `ra2core` 冒烟测试里做硬判据回归）。

---

## 【更正】对象字段偏移的印证口径

上一节有两处说过头了，这里逐条更正 —— 账要留痕，不悄悄改。

### 1. 「387 处里 159 处印证」——分母不对

159 不是"只印证了 41%"，而是**可判定子集的全部**。387 处 `mdisp` 按基类
能不能对名字，只有一个分法：

- **159 处**的基类拥有自己的 COL（`??_7X@@6B@` 存在），可以拿虚表 VA 去对
  「`mov [对象+mdisp], <那张虚表>`」。**这 159 处全部命中，100%。**
- **228 处**的基类在二进制里**根本没有虚表**，分两类：
  1. **纯抽象接口** —— `IUnknown`（101 处）、`IRTTITypeInfo`（78）、
     `ILocomotion`（12）、`IPiggyback`（6）、`ATL::CComObjectRootBase`（5）、
     `IFlyControl`、`ILinkStream`、`IHouse`、`IPublicHouse`、
     `IConnectionPointContainer`。MSVC 对"没有任何非内联虚函数、又从不被
     完整构造"的类**既不生成虚表也不生成 COL**。
  2. **没有虚函数的普通子对象** —— `StageClass`（9）、`FlasherClass`（6）、
     `BounceClass`（1）。

把 228 算进分母，就把"可判定的全中"稀释成了"387 处只中 41%"。

### 2. 「0xF0 与 0xF8 上各找到一次虚表写入」—— 0xF0 上没有

这条是上一节最漂亮的一句，可惜**是伪印证**。实测反汇编：

```
0x006F2B52  mov dword ptr [esi + 0xf0], ebx   ; TechnoClass::ctor@0x6F2B40
0x006F2B58  mov byte  ptr [esi + 0xf4], bl
0x006F2B5E  mov dword ptr [esi + 0xf8], ebx
0x006F2B64  mov byte  ptr [esi + 0xfc], bl
0x00421EB8  mov dword ptr [esi + 0xac], ebx   ; AnimClass::ctor@0x421EA0
```

写进去的是 `ebx`（0），**不是虚表**。`FlasherClass` / `StageClass` 没有虚函数，
它们所在的那几字节里根本不存在虚表指针。上一节之所以"看到"0xF8 命中，是因为
那条通道统计的是「**全库**有没有人在有效偏移 0xF8 上写过虚表」—— 别的类写过，
跟这一行毫无关系。**这是伪印证，比不印证更坏**：它会让一个坏掉的通道看起来
在工作。

现在的第三条通道把范围收到**该继承链**上。判据必须沿 RTTI 的 `bases` 做传递
闭包：`UnitClass` 在 240 上的子对象是 `TechnoClass` 的构造函数写的，
`UnitClass` 自己的构造函数根本不碰那一段 —— 只看本类会整段漏掉。

### 3. 更正后的账（387 处一条不漏）

| 印证方式 | 处数 | 判据 |
|---|---:|---|
| 虚表直写 | 159 | `mov [对象+N], <该基类主虚表>` 在 .text 里找到 |
| 调用点 | 0 | `lea ecx,[对象+N]; call <该基类构造函数>` |
| 同链写入 | 228 | 该继承链上某个类的构造函数在有效偏移 N 上写过东西 |
| **未印证** | **0** | — |

「调用点」这条通道本身是好的（全库另有 75 个类在它上面有命中：
`H::?$VectorClass`、`PBVBuildingTypeClass::?$VectorClass`、`rc_ptr_base`、
`CounterClass`、`ShapeButtonClass` …），只是这 387 处里没有一例走这条形态 ——
基类构造函数全被内联了。

### 4. 顺手修掉 sizeof 取错的候选

`CCFileClass`：投票 36（4 票）vs 字段末端 108 —— `36 < 108`，物理上不可能；
108 本来就在候选里（2 票），只是当初票少。`tools/sizeofscan.py` 现在用字段末端
**硬过滤候选**，改过的条目打 `calibrated` 标记并**无条件**进
`src/re/ObjectSizes.h`（字段末端是硬下界，比投票硬）。

剩下 4 个类（`CounterClass`、`CCINIClass`、`BufferIOFileClass`、
`PAVReestablish::?$VectorClass`）**所有**候选都小于字段末端 —— 那说明两条数据里
有一条本身错了，**不改**，只记 `note` 留给人看。乱猜比不改更坏。

sizeof 越界因此 5 → 4，通过数 206 → 207。

### 5. 工具链上的一个坑：读了用不到的文件

`tools/sizeofscan.py` 里有一行 `json.load(open(a.virtuals))` 读
`db/virtuals.json` —— 这个文件**既不在仓库里、也生成不出来**，而读到的值从头到尾
**没被用过**（真正的「虚表 VA → 类名」映射是从 `db/rtti.json` 现算的）。
它让整条流程在别的机器上直接抛 `FileNotFoundError`，而报错位置离真因很远。
已改成：缺文件只提示一句，不中断。

**教训**：一个没人用的输入，会让工具在一台能跑的机器上看着正常，换台机器就死。
留着这种「幽灵输入」，等于给自己埋一个只在别人机器上触发的地雷。

---

## 对象字段**名**：从二进制自己的 `Read_INI` 里读（2026-09-18，P3 第二步）

偏移知道了只算"这个位置有个 4 字节的东西"。名字得另找一条路。

### 办法

`XxxTypeClass::Read_INI(CCINIClass&)` 的编译形态极规整。实测
`TechnoTypeClass::Read_INI@0x712170`：

```asm
mov eax, dword ptr [ebp + 0x610]   ; 缺省值：先从字段里读出来
push eax
push 0x825470                      ; "Cost"            <- 键名
push ebx                           ; section 名
mov ecx, esi                       ; CCINIClass*
call 0x5276D0                      ; ReadInteger
mov dword ptr [ebp + 0x610], eax   ; 结果存回**同一个**字段
```

**同一个偏移在键名两侧各出现一次**。编译器是从

```cpp
Cost = ini.ReadInteger(section, "Cost", Cost);
```

生成的，不是我们推出来的。所以这条证据是**自证**的：只用二进制自己的字节。

### 「哪一槽是 `Read_INI`」是发现出来的，不是规定的

对每张虚表的**每个**函数数"它引用了多少个**真实存在**的 INI 键名"，超过阈值
就认定。结果：5 个函数全部落在**槽 #25**，而且 `TechnoTypeClass` /
`BuildingTypeClass` / `UnitTypeClass` 三张互不相干的表独立收敛到同一个槽号。
——如果一开始就写死 `#25`，这个交叉验证就做不出来了。

### 三个真踩到的坑

**一、字符串要按**大小写不敏感**去对，但要拿真实键集去对。**

第一版要求字面量是**全大写**（`^[A-Z][A-Z0-9_]+$`），因为 INI 文件里键都是大写的。
方向就错了：文件里全大写只是文件风格，二进制里的字面量是 `"Cost"` / `"Armor"` /
`"Strength"` 这种首字母大写形式，`CCINIClass` 比较时不分大小写。第一版扫出来
的全是 `TXT_*` 这种 CSF 标签，一个 INI 键都没有。

同时，键集必须用 `tools/inikeys.py` **从真实 INI 文件清点出来的**那一份，
不能用 `src/data/TypeDB.h` 里那份 —— 后者是我们在 P2 里挑出来类型化的**子集**，
拿它当全集会把"还没类型化的键"判成非法。

**二、结果存回要**以 `call` 为锚**，不能用"键名之后第一条存"。**

这是本轮最大的一个错误。`ObjectTypeClass::Read_INI@0x5F94B3`：

```asm
mov edx, dword ptr [ebx + 0x9C]    ; Armor 的缺省值
push edx
push 0x81D9D4                      ; "Armor"     <- 键名
push ebp
mov byte ptr [ebx + 0x231], al     ; **上一条键(LegalTarget)的结果**被插在这里
call 0x4753F0
mov dword ptr [ebx + 0x9C], eax    ; Armor 的结果
```

编译器把上一条键的结果存**调度**到了这一条键的键名和调用之间。按"键后第一条存"
就会拿到 `0x231`，与缺省值侧读到的 `0x9C` 打架，于是 Armor 直接消失。
改成"`call` 之后的 4 条内找存"就稳了：`0x9C` 立刻对上。

**这条错误的形状值得记住**：它不是"少找到几条"，而是"以很自然的方式找到**错的**
那条"。第一版里 `Speed` 被记成 `0x630`，看着毫无破绽。

**三、同一个函数体被多个类共享时，归属要按继承链最长的那个。**

`ObjectTypeClass::Read_INI` 同时挂在 `ObjectTypeClass` 与 `IsometricTileTypeClass`
的槽 #25 上（后者不覆盖）。数据要记在两个类上，但**类名**用最长链的那个。

### 一个"宁可不给名字"的例子：`Speed`

`Speed` 在 `TechnoTypeClass::Read_INI@0x71464C` 被读出来之后：

```asm
push -1                            ; 缺省值是立即数 -1，不是从字段读的
push 0x81D9CC                      ; "Speed"
call 0x5276D0                      ; ReadInteger
cmp eax, -1 / je …                 ; 钳到 100
shl ecx, 8 / imul 0x51EB851F       ; ×256
sar edx, 5                         ; ÷100
mov dword ptr [ebp + 0x678], edx   ; 结果落在 0x678
```

值先被钳位、再做定点换算（`×256/100`，即存成 1/256 单位），最后才存进 `0x678` ——
离键名十几条指令，中间还夹着分支。相邻存规则够不到。

**够不到就不给名字。** 第一版在这里给了 `0x630`，而 `0x630` 其实属于**上一条**
键（它的结果存被调度插进来了）——也就是说，**放宽规则只会让错误更自信**。

### 三条独立证据

| 证据 | 判据 | 实测 |
|---|---|---|
| 名字侧宽度 vs 构造函数扫描的字段宽度 | 同一个偏移上两条通道必须给出一致的宽度 | 396 条里 **300 条**被构造函数扫描独立看到，宽度不符 **0** |
| 偏移必须落在 sizeof 之内 | sizeof 来自 `push N; call new`（第三条通道） | 越界 **0** |
| 手工反汇编锚点 | `ObjectTypeClass` 的 `Armor@0x9C`/`Strength@0xA0`，`TechnoTypeClass` 的 `Cost@0x610`/`TechLevel@0x634`/`Sight@0x5E8`/`Points@0x728` | 全部成立，写进 `FieldNames_Check()` |

### 覆盖面对账（不让"覆盖率"看起来像"全量"）

| Read_INI | 出现的键 | 配上偏移 | 没配上 |
|---|---:|---:|---:|
| `TechnoTypeClass` | 252 | 250 | 2 |
| `BuildingTypeClass` | 193 | 181 | 12 |
| `UnitTypeClass` | 44 | 42 | 2 |
| `InfantryTypeClass` | 25 | 25 | 0 |
| `ObjectTypeClass` / `IsometricTileTypeClass` | 19 | 15 | 4 |

没配上的键名**不进常量表**，但会在 `docs/fieldnames.md` 里逐个列出来。

### 同一偏移被两个键名主张：按证据强度分档

取舍规则不是"有没有人争"，是"争的人证据够不够硬"：

- **「双向」**（缺省值与结果同偏移）→ 保留。它自洽，且手工核对过。
- **「单向」**（只有一边）且被人争 → 弃用。本来就弱，再来一个人争就没理由留。

**这条规则是被冒烟测试逼出来的。** 最初一刀切"有争议就丢"，`ra2core` 立刻红：

```
FAIL: TechnoTypeClass 的 0x610 应该是 COST，查到的却是 (没有)
```

`Cost@0x610` 是手工反汇编核对过的（上面那段就是），却被"有争议"连着丢了。
**锚点断言的价值就在这里**：它不是"再跑一遍生成器"，而是拿一条**手工确认过的
事实**去卡自动流程。

### 明确没做到的

- 只覆盖 `Read_INI`。构造函数里赋常量、或游戏逻辑里才算出来的字段，仍然没名字。
- 数组字段（`TurretType[i]`，32 个键名争一个基偏移）、经变换再存的字段
  （`Speed`）、目的地不在对象上的字符串字段，这三类**够不到**，不做外推。
- 类型只区分 `ReadBool` / `ReadInteger` 两个入口，其余一律 `?`。
  `AmbientSound` 这类"值是整数"的键也从收字符串的入口过 —— 硬标 `str` 就是编造。

---

## 对象模型：三张表铺成能编译的结构体（2026-09-18，P3 第三步）

工具 `tools/layout.py`，产物 `src/re/ObjectModel.h`、`db/layout.json`、
`docs/object-model.md`。

### 为什么非做不可

前三轮的产物都是**查询表**：按 (类, 偏移) 查宽度、按 (类, 偏移) 查名字、按类查
`sizeof`。查询表回答不了两个最基本的问题：

- 代码里写不出 `obj.COST = 1000`（没有 `COST` 这个成员）；
- 说不出 `sizeof(TechnoTypeClass)`（它不在 `operator new` 的 `push` 里）。

把 (偏移, 宽度, 名字) 按 RTTI 的继承链铺成**连续内存布局**，这两件事同时解决。
三步：

1. **字段集合** = 构造函数写过（`fields.json`）∪ Read_INI 读写过（`fieldnames.json`）。
   同一偏移两条通道的宽度必须一致，不一致直接终止（本轮 0 处）。
2. **继承链**取自 RTTI 的 `bases[0]`，共 4 层 8 个类。
   子类只声明"偏移 ≥ 父类末端"的部分。
3. **不满就填**：字段之间插 `u8 _pad_XXXX[n]`，使每个已知字段恰好落在它的偏移上。

### 本轮算出来的数

| 类 | 父类 | 本体起点 | 末端 | 已知字段 | 有名字 | 填充字节 |
|---|---|---|---|---|---|---|
| `AbstractClass` | （根） | 0x0 | 0x21 | 9 | 0 | 3 |
| `AbstractTypeClass` | `AbstractClass` | 0x21 | 0x65 | 4 | 0 | 61 |
| `ObjectTypeClass` | `AbstractTypeClass` | 0x65 | 0x294 | 65 | 15 | 392 |
| `IsometricTileTypeClass` | `ObjectTypeClass` | 0x294 | 0x30C | 33 | 0 | 18 |
| `TechnoTypeClass` | `ObjectTypeClass` | 0x294 | 0xDF4 | 460 | 178 | 1490 |
| `BuildingTypeClass` | `TechnoTypeClass` | 0xDF4 | 0x1792 | 311 | 170 | 1640 |
| `InfantryTypeClass` | `TechnoTypeClass` | 0xDF4 | 0xECC | 65 | 22 | 34 |
| `UnitTypeClass` | `TechnoTypeClass` | 0xDF4 | 0xE5F | 40 | 10 | 4 |
| **合计** | | | | **987** | **395** | **3642** |

**末端是 `sizeof` 的下界，不是 `sizeof`**。能说出口的是
`sizeof(TechnoTypeClass) >= 0xDF4`。

### `#pragma pack(push,1)` 是故意的

二进制里的布局是事实，不能让它被编译器的对齐规则改写。pack(1) 下 MSVC 的
`offsetof` / `sizeof` 精确等于铺出来的偏移。这一点**实测过**，不是推测：
`tools/_probe_offsetof.cpp` 造了一条 4 层继承 + 混合宽度 + pack(1) 的链，
`sizeof`（0x21/0x65/0x294/0xDF4）与跨层 `offsetof` 全部对上。

代价：`offsetof` 用在非 standard-layout 类型上是**条件支持**的（每一层基类都有
数据成员，整个链不是 standard-layout）。MSVC 对单继承非虚基类给出正确结果，
并且这里的结果被 900 多条 `static_assert` 逐条钉过 —— 是"在目标编译器上已验证"，
不是"标准保证"。换编译器要重跑这一层。

### 独立对账

**（一）继承边界精确吻合 —— 两套独立证据撞在一起。**
`ObjectTypeClass` 的末端（它自己构造函数写过的最远字段）= **0x294**；
`TechnoTypeClass` 的**自有**命名字段里最小的偏移 = **0x294**。
前者来自扫构造函数，后者来自扫 `Read_INI`。RTTI 只说"继承"，**不给**边界在哪。

**（二）子类在祖先区留下的痕迹，祖先必须已经认识。** 子类构造函数内联父类初始化、
子类 `Read_INI` 也会写继承来的字段，这些偏移必须能在祖先链的字段表里找到且名字一致。
本轮无异常。顺带量出一条有信息量的注记：

> `BuildingTypeClass` / `UnitTypeClass` / `InfantryTypeClass` 的构造函数都写了祖先区的
> `0xD2E`、`0xD35`、`0xD36`、`0xD38`、`0xD3B`、`0xD96`、`0xD97` —— 而
> `TechnoTypeClass` 自己的构造函数**没写过**这些字节，名字是它的 `Read_INI` 通道给的。

**（三）类内不得有重叠字段。** 重叠说明至少有一条写记录被归错了。本轮 0 处。

**（四）编译期 + 运行期两道闸门。** 头文件里每条字段一条
`static_assert(offsetof(...) == 偏移)`；`Model_Check()` 再在运行期把
`FieldNames.h` 的每条命名字段拿回来在模型里找同名成员（偏移、宽度、键名三者都要对）。

### 这一步把一个上一轮的归属错误逼了出来

`Model_Check()` 第一次跑就红：

```
FAIL: IsometricTileTypeClass 的命名字段 0x9C（ARMOR）在模型里找不到成员
```

顺着查下去，是 `tools/fieldname.py` 的**函数归属**排反了：它按
`-depth`（深→浅）排序，取 `attach[0]` 当归属类。而 `0x5F92D0` 同时挂在
`ObjectTypeClass` 和 `IsometricTileTypeClass` 的虚表槽 #25 上 ——
**同一个函数体不可能被两个类各自实现**，RTTI 说后者继承前者，
所以实现者是**最浅**的那一个（祖先的槽位上放着别的函数）。

排反的后果不是小错：`IsometricTileTypeClass` 凭空多出 15 个"自己的"字段，
而这 15 个偏移（0x9C/0xA0/0x1E8/0x211/0x22C~0x238）**全部落在
`ObjectTypeClass` 的本体里**。

修法两处，缺一不可：

1. 排序改 `depth.get(c, 0)` 升序 → `attach[0]` 是提供实现的类；
   没覆盖 `Read_INI` 的派生类进 `read_by` 字段留痕，回答"谁在读这些键"。
2. 记录**不再给每个 attach 的类各复制一份** —— 字段归实现者，
   否则"某类有 N 个命名字段"会把继承来的算成自己的。

修完 `IsometricTileTypeClass` 自有命名字段 = 0，与对象模型的结论一致。
`FieldNames.h` 从 6 类 396 条变成 **5 类 381 条**（那 15 条换了个类名）。

**这件事的意义在于**：`Model_Check()` 不是"再跑一遍生成器"。它拿模型去要
`FieldNames.h` 的账，两条**独立生成**的表对不上就报错 —— 而它真的抓到了一个
人工审阅很难发现的归属错误。窄口径的交叉核对比宽口径的自证有用得多。

顺带也确认了一个**不是**错误的相似现象：`UnitTypeClass::Read_INI` 读的
`SPEEDTYPE` 落在 `0x67C`，那是 `TechnoTypeClass` 的字段。派生类的 `Read_INI`
去写继承来的字段是正常的，所以模型在查名字时沿父类链往上找，
并把"落在继承来的字段上"的条数单独数出来（本轮 381 条里有 **1** 条）。

### 明确没做到的

- 只覆盖 4 层继承链上的 8 个 `*TypeClass`。`BuildingClass` / `CellClass` /
  `HouseClass` 这些静态对象池里的类 `sizeof` 拿不到、字段证据也没挖，不进模型。
- **不表示未知**。`_pad_XXXX` 只代表"构造函数和 `Read_INI` 都没写到那里"，
  不代表那些字节是空的。`UnitTypeClass` 覆盖率 3.5% 是这个意思，
  不是"它只有 130 字节有内容"。
- **没有语义、没有虚函数**。成员名就是 INI 键名原样（有据可查），
  没换成编出来的 CamelCase 内部名字；虚表信息在 `ClassHierarchy.h` / `docs/vtables.md`。
- `static_assert` 守的是**回归**，不是取证。偏移本身来自反汇编。
