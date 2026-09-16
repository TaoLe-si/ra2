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
