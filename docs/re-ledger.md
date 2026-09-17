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
