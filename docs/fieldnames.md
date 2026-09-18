# 字段名（从二进制自己的 `Read_INI` 里读出来的）

由 `tools/fieldname.py` 静态分析得出，未运行目标进程。全量数据见 `db/fieldnames.json`。

## 方法

`XxxTypeClass::Read_INI(CCINIClass& ini)` 的编译形态极其规整 —— 实测
`TechnoTypeClass::Read_INI@0x712170`：

```asm
mov eax, dword ptr [ebp + 0x604]   ; 缺省值：先从字段里读出来
lea ebx, [ebp + 0x24]              ; section 名（对象里的字符串）
push eax                           ; 缺省值
push 0x844520                      ; "LandTargeting"   <- 键名
push ebx                           ; section
mov ecx, esi                       ; CCINIClass*
call 0x5276D0                      ; ReadInteger
mov dword ptr [ebp + 0x604], eax   ; 结果存回**同一个**字段
```

**同一个偏移在键名两侧各出现一次**，这一条本身就是自证 —— 它不依赖任何
外部资料。编译器是从 Westwood 的写法生成的：

```cpp
Strength = ini.ReadInteger(section, "Strength", Strength);
```

### 找 `Read_INI`：槽号是发现出来的，不是规定的

对每张虚表的每个函数数『它引用了多少个**真实存在**的 INI 键名』，
超过 12 个就认作 `Read_INI`。实测收敛：

| 虚表槽号 | 命中函数数 |
|---|---:|
| #25 | 6 |

（TechnoTypeClass / BuildingTypeClass / UnitTypeClass 三张**互不相干**的表
独立收敛到同一个槽号，这本身就是槽号猜对了的佐证。）

键集来自 `tools/inikeys.py` 对真实 INI 文件的清点：rules 476 种键，art 206 种。

### CCINIClass 读函数

| 地址 | 实测 | 认定 |
|---|---|---|
| `0x005295F0` | 114 次调用，紧随 `mov byte` 105 次 | `ReadBool` |
| `0x005276D0` | 91 次调用，紧随 `mov dword` 68 次 | `ReadInteger` |
| 其它（`0x00524EC0` / `0x00528A10` / `0x007514D0` …） | 紧随没有稳定的存指令 | **不断言类型**，只记调用地址 |

类型列只有 `int` / `bool` / `?` 三种。`?` 表示读取入口不在已认出的两个里。
**不断言一个没有正面证据的类型** —— `AmbientSound` / `ImpactLandSound` 这类
「值是整数」的键也从收字符串的入口过，硬标成 `str` 就是编造。

## 交叉验证（三条独立证据）

| 证据 | 判据 | 结果 |
|---|---|---|
| 宽度与构造函数扫描一致 | 同一个偏移，`Read_INI` 这边的存取宽度必须等于`db/fields.json`（构造函数扫描，另一套无关分析）里的字段宽度 | 313 / 396 条同时被构造函数扫描独立看到，不符 **0** |
| 偏移落在 sizeof 之内 | sizeof 来自 `push N; call new`（第三条通道） | 越界 **0** |
| 手工反汇编锚点 | `ObjectTypeClass` 的 `Armor@0x9C` / `Strength@0xA0`，`TechnoTypeClass` 的 `Cost@0x610` / `TechLevel@0x634` / `Sight@0x5E8` / `Points@0x728` | 全部成立，进 `FieldNames_Check()` |

**298 / 396 条是「双向」**（缺省值与结果落在同一个偏移上）—— 最强的证据等级。

另外 83 条是 Read_INI 独有 —— 构造函数不碰这些偏移，这条通道**补上了**那个盲区。

（不列「键名必须出现在真实 INI 里」当成一条验证 —— 那是**输入端**的筛子，不是证据：键名本来就是因为命中键集才被收进来的，拿它当验证是同义反复。）

## 结果

| 类 | 命名字段数 | 双向 | 新发现（构造函数没碰） |
|---|---:|---:|---:|
| `TechnoTypeClass` | 178 | 148 | 10 |
| `BuildingTypeClass` | 170 | 121 | 72 |
| `InfantryTypeClass` | 22 | 14 | 0 |
| `ObjectTypeClass` | 15 | 9 | 0 |
| `UnitTypeClass` | 11 | 6 | 1 |

### `TechnoTypeClass`

| 偏移 | 键名 | 类型 | 宽度 | 字段表里有 | 证据 |
|---:|---|---|---:|---|---|
| 0x294 | `WALKRATE` | int | 4 | 是 | 双向 |
| 0x298 | `IDLERATE` | int | 4 | 是 | 双向 |
| 0x2C0 | `SPECIALTHREATVALUE` | ? | 4 | 是 | 单向-读 |
| 0x2F0 | `THREATAVOIDANCECOEFFICIENT` | ? | 4 | 是 | 单向-读 |
| 0x300 | `DEACCELERATIONFACTOR` | ? | 4 | 是 | 单向-读 |
| 0x308 | `ACCELERATIONFACTOR` | ? | 4 | 是 | 单向-读 |
| 0x310 | `CLOAKINGSPEED` | int | 4 | 是 | 双向 |
| 0x370 | `WEIGHT` | ? | 4 | 是 | 单向-读 |
| 0x378 | `PHYSICALSIZE` | ? | 4 | 是 | 单向-读 |
| 0x380 | `SIZE` | ? | 4 | 是 | 单向-读 |
| 0x388 | `SIZELIMIT` | ? | 4 | 是 | 单向-读 |
| 0x390 | `HOVERATTACK` | bool | 1 | 是 | 双向 |
| 0x394 | `VHPSCAN` | ? | 4 | 是 | 双向　**同一偏移读到别的键：DEBRISTYPES** |
| 0x3A8 | `PITCHSPEED` | ? | 4 | 是 | 单向-读　**同一偏移读到别的键：LOCOMOTOR** |
| 0x3B8 | `BUILDLIMIT` | int | 4 | 是 | 双向　**同一偏移读到别的键：UNDEPLOYSINTO** |
| 0x3BC | `CATEGORY` | ? | 4 | 是 | 单向-读　**同一偏移读到别的键：POWERSUNIT** |
| 0x3C8 | `DEPLOYTIME` | ? | 4 | 是 | 单向-读 |
| 0x3D0 | `FIREANGLE` | ? | 4 | 是 | 双向 |
| 0x3D4 | `PIPSCALE` | ? | 4 | 是 | 双向 |
| 0x3D8 | `PIPSDRAWFORALL` | bool | 1 | 是 | 双向 |
| 0x3E0 | `PIXELSELECTIONBRACKETDELTA` | int | 4 | 是 | 双向 |
| 0x410 | `POWEREDUNIT` | bool | 1 | 是 | 双向 |
| 0x568 | `MOVESOUND` | ? | 4 | 是 | 单向-读　**同一偏移读到别的键：DIESOUND** |
| 0x5B4 | `MOVEMENTZONE` | ? | 4 | 是 | 单向-读 |
| 0x5B8 | `GUARDRANGE` | ? | 4 | 是 | 双向 |
| 0x5BC | `MAXDEBRIS` | int | 4 | 是 | 双向　**同一偏移读到别的键：DEBRISANIMS** |
| 0x5C0 | `MINDEBRIS` | int | 4 | 是 | 双向 |
| 0x5E0 | `PASSENGERS` | ? | 4 | 是 | 双向 |
| 0x5E4 | `OPENTOPPED` | bool | 1 | 是 | 双向 |
| 0x5E8 | `SIGHT` | int | 4 | 是 | 双向 |
| 0x5EC | `RESOURCEGATHERER` | bool | 1 | 是 | 双向 |
| 0x5ED | `RESOURCEDESTINATION` | bool | 1 | 是 | 双向 |
| 0x5EE | `REVEALTOALL` | bool | 1 | 是 | 双向 |
| 0x5EF | `DRAINABLE` | bool | 1 | 是 | 双向 |
| 0x5F0 | `SENSORSSIGHT` | int | 4 | 是 | 双向 |
| 0x5F4 | `DETECTDISGUISERANGE` | int | 4 | 是 | 双向 |
| 0x5F8 | `BOMBSIGHT` | int | 4 | 是 | 双向 |
| 0x5FC | `LEADERSHIPRATING` | int | 4 | 是 | 双向 |
| 0x600 | `NAVALTARGETING` | int | 4 | 是 | 双向 |
| 0x604 | `LANDTARGETING` | int | 4 | 是 | 双向 |
| 0x60C | `BUILDTIMEMULTIPLIER` | ? | 4 | 是 | 单向-读 |
| 0x610 | `COST` | int | 4 | 是 | 双向　**同一偏移读到别的键：DEPLOYINGANIM** |
| 0x614 | `SOYLENT` | int | 4 | 是 | 双向 |
| 0x61C | `AIRSTRIKETEAM` | int | 4 | 是 | 单向-读 |
| 0x62C | `AIRSTRIKERECHARGETIME` | int | 4 | 是 | 单向-读 |
| 0x634 | `TECHLEVEL` | int | 4 | 是 | 双向　**同一偏移读到别的键：ELITEAIRSTRIKETEAMTYPE** |
| 0x670 | `THREATPOSED` | int | 4 | 是 | 双向 |
| 0x67C | `SPEEDTYPE` | ? | 4 | 是 | 双向 |
| 0x684 | `AMMO` | int | 4 | 是 | 双向 |
| 0x688 | `IFVMODE` | int | 4 | 是 | 双向 |
| 0x68C | `AIRRANGEBONUS` | ? | 4 | 是 | 双向 |
| 0x690 | `BERSERKFRIENDLY` | bool | 1 | 是 | 双向 |
| 0x693 | `NATURAL` | bool | 1 | 是 | 双向 |
| 0x694 | `UNNATURAL` | bool | 1 | 是 | 双向 |
| 0x695 | `CLOSERANGE` | bool | 1 | 是 | 双向 |
| 0x6A4 | `RADIALFIRESEGMENTS` | int | 4 | 是 | 双向 |
| 0x6AC | `DEPLOYFIRE` | bool | 1 | 是 | 双向 |
| 0x6AD | `DEPLOYTOLAND` | bool | 1 | 是 | 双向 |
| 0x6AF | `OPPORTUNITYFIRE` | bool | 1 | 是 | 双向 |
| 0x6B0 | `DISTRIBUTEDFIRE` | bool | 1 | 是 | 双向 |
| 0x6BC | `VETERANABILITIES` | ? | 4 | 是 | 单向-读　**同一偏移读到别的键：ELITEABILITIES、PRIMARYFIREFLH、ELITEPRIMARYFIREFLH** |
| 0x6C1 | `ATTACKCURSORONFRIENDLIES` | ? | 1 | 是 | 双向 |
| 0x6C4 | `UNDEPLOYDELAY` | ? | 4 | 是 | 双向 |
| 0x6C8 | `PREVENTATTACKMOVE` | bool | 1 | 是 | 双向 |
| 0x6CC | `OWNER` | ? | 4 | 是 | 双向 |
| 0x6D0 | `AIBASEPLANNINGSIDE` | int | 4 | 是 | 双向 |
| 0x6D4 | `STUPIDHUNT` | ? | 1 | 是 | 双向 |
| 0x6D5 | `ALLOWEDTOSTARTINMULTIPLAYER` | ? | 1 | 是 | 双向 |
| 0x71C | `ROT` | ? | 4 | 是 | 双向 |
| 0x720 | `TURRETOFFSET` | ? | 4 | 是 | 双向 |
| 0x724 | `CANBEHIDDEN` | bool | 1 | 是 | 双向 |
| 0x728 | `POINTS` | int | 4 | 是 | 双向 |
| 0x774 | `DAMAGESMOKEOFFSET` | ? | 4 | 是 | 单向-读　**同一偏移读到别的键：REFINERYSMOKEOFFSETONE、REFINERYSMOKEOFFSETTWO、REFINERYSMOKEOFFSETTHREE、REFINERYSMOKEOFFSETFOUR、PREREQUISITE、PREREQUISITEOVERRIDE** |
| 0x7FC | `SHADOWINDEX` | ? | 4 | 是 | 双向　**同一偏移读到别的键：CAMEO** |
| 0x800 | `STORAGE` | int | 4 | 是 | 双向　**同一偏移读到别的键：DOCK、DEPLOYSINTO** |
| 0x805 | `GUNNER` | bool | 1 | 是 | 双向 |
| 0x806 | `HASTURRETTOOLTIPS` | bool | 1 | 是 | 双向 |
| 0x808 | `TURRETCOUNT` | int | 4 | 是 | 双向 |
| 0x80C | `WEAPONCOUNT` | int | 4 | **否** | 双向　**同一偏移读到别的键：PRIMARY、ELITEPRIMARY、ELITESECONDARY、AUXSOUND2、SINKINGSOUND、SECONDARYFIREFLH** |
| 0x810 | `ISCHARGETURRET` | bool | 1 | 是 | 双向 |
| 0x8A8 | `PBARRELLENGTH` | ? | 4 | 是 | 双向 |
| 0x8AC | `PBARRELTHICKNESS` | ? | 4 | 是 | 双向 |
| 0x8C8 | `ELITESECONDARYFIREFLH` | ? | 4 | **否** | 单向-读　**同一偏移读到别的键：SECONDSPAWNOFFSET** |
| 0xA90 | `SECONDARY` | ? | 1 | 是 | 单向-读　**同一偏移读到别的键：AUXSOUND1、CREATESOUND、DAMAGESOUND、IMPACTLANDSOUND、CRASHINGSOUND、VOICECRASHING、VOICEENTER、VOICECAPTURE** |
| 0xAB0 | `VOICEMOVE` | ? | 4 | **否** | 单向-读　**同一偏移读到别的键：VOICESELECT、VOICESELECTENSLAVED、VOICESELECTDEACTIVATED、VOICEATTACK、VOICESPECIALATTACK、VOICEFEEDBACK** |
| 0xC8C | `TYPEIMMUNE` | bool | 1 | 是 | 双向 |
| 0xC8D | `MOVETOSHROUD` | bool | 1 | 是 | 双向 |
| 0xC8E | `TRAINABLE` | bool | 1 | 是 | 双向 |
| 0xC90 | `TARGETLASER` | ? | 1 | 是 | 双向 |
| 0xC91 | `IMMUNETOVEINS` | ? | 1 | 是 | 双向 |
| 0xC95 | `PITCHANGLE` | ? | 1 | 是 | 单向-读 |
| 0xC96 | `TOPROTECT` | ? | 1 | 是 | 双向 |
| 0xC99 | `DEATHWEAPON` | ? | 1 | 是 | 单向-读 |
| 0xC9B | `RADARVISIBLE` | ? | 1 | 是 | 双向 |
| 0xC9D | `SENSORS` | bool | 1 | 是 | 双向 |
| 0xC9E | `NOMINAL` | bool | 1 | 是 | 双向 |
| 0xC9F | `DONTSCORE` | bool | 1 | 是 | 双向 |
| 0xCA0 | `DAMAGESELF` | bool | 1 | 是 | 双向 |
| 0xCA1 | `TURRET` | bool | 1 | 是 | 单向-读　**同一偏移读到别的键：LEAVETRANSPORTSOUND、UNDEPLOYSOUND、VOICEHARVEST、VOICESECONDARYWEAPONATTACK、DEACTIVATESOUND、REFINERYSMOKEPARTICLESYSTEM、DAMAGEPARTICLESYSTEMS** |
| 0xCA2 | `TURRETRECOIL` | ? | 1 | 是 | 单向-读　**同一偏移读到别的键：TURRETTRAVEL、BARRELTRAVEL、BARRELCOMPRESSFRAMES、BARRELHOLDFRAMES、BARRELRECOVERFRAMES** |
| 0xCCC | `REPAIRABLE` | bool | 1 | 是 | 双向 |
| 0xCCD | `CREWED` | bool | 1 | 是 | 双向 |
| 0xCCE | `NAVAL` | bool | 1 | 是 | 双向 |
| 0xCCF | `REMAPABLE` | ? | 1 | 是 | 双向 |
| 0xCD0 | `CLOAKABLE` | bool | 1 | 是 | 双向 |
| 0xCD1 | `GAPGENERATOR` | bool | 1 | 是 | 双向 |
| 0xCD2 | `GAPRADIUSINCELLS` | int | 1 | 是 | 双向/宽度存疑 |
| 0xCD3 | `SUPERGAPRADIUSINCELLS` | int | 1 | 是 | 双向/宽度存疑 |
| 0xCD4 | `TELEPORTER` | bool | 1 | 是 | 双向 |
| 0xCD5 | `ISGATTLING` | bool | 1 | 是 | 双向 |
| 0xCD8 | `WEAPONSTAGES` | int | 4 | 是 | 双向 |
| 0xD0C | `RATEUP` | int | 4 | 是 | 双向 |
| 0xD10 | `RATEDOWN` | int | 4 | 是 | 双向 |
| 0xD14 | `SELFHEALING` | ? | 1 | 是 | 双向 |
| 0xD15 | `EXPLODES` | bool | 1 | 是 | 双向 |
| 0xD18 | `DEBRISMAXIMUMS` | ? | 4 | 是 | 单向-读 |
| 0xD21 | `TURRETSPINS` | bool | 1 | 是 | 双向　**同一偏移读到别的键：TURRETROTATESOUND、CHRONOINSOUND** |
| 0xD22 | `TILTCRASHJUMPJET` | bool | 1 | 是 | 双向　**同一偏移读到别的键：ENTERTRANSPORTSOUND、DEPLOYSOUND、CHRONOOUTSOUND、VOICEDEPLOY、ACTIVATESOUND、EXPLOSION、DESTROYANIM** |
| 0xD23 | `NORMALIZED` | ? | 1 | 是 | 双向 |
| 0xD28 | `CRUSHER` | ? | 1 | 是 | 双向 |
| 0xD29 | `OMNICRUSHER` | ? | 1 | 是 | 双向 |
| 0xD2A | `OMNICRUSHRESISTANT` | ? | 1 | 是 | 双向 |
| 0xD2D | `AUTOCRUSH` | ? | 1 | 是 | 双向 |
| 0xD2E | `BUNKERABLE` | ? | 1 | **否** | 双向 |
| 0xD2F | `CANDISGUISE` | bool | 1 | 是 | 双向 |
| 0xD30 | `PERMADISGUISE` | bool | 1 | 是 | 双向 |
| 0xD31 | `DETECTDISGUISE` | bool | 1 | 是 | 双向 |
| 0xD32 | `DISGUISEWHENSTILL` | bool | 1 | 是 | 双向 |
| 0xD33 | `CANAPPROACHTARGET` | bool | 1 | 是 | 双向 |
| 0xD35 | `IMMUNETOPSIONICS` | ? | 1 | **否** | 双向 |
| 0xD36 | `IMMUNETOPSIONICWEAPONS` | ? | 1 | **否** | 双向 |
| 0xD37 | `IMMUNETORADIATION` | ? | 1 | 是 | 双向 |
| 0xD38 | `PARASITEABLE` | ? | 1 | **否** | 双向 |
| 0xD39 | `DEFAULTTOGUARDAREA` | ? | 1 | 是 | 双向 |
| 0xD3B | `IMMUNETOPOISON` | ? | 1 | **否** | 双向 |
| 0xD3C | `RESELECTIFLIMBOED` | bool | 1 | 是 | 双向 |
| 0xD3D | `REJOINTEAMIFLIMBOED` | bool | 1 | 是 | 双向 |
| 0xD3E | `SLAVED` | ? | 1 | 是 | 双向 |
| 0xD40 | `ENSLAVES` | ? | 4 | 是 | 双向 |
| 0xD44 | `SLAVESNUMBER` | ? | 4 | 是 | 双向 |
| 0xD48 | `SLAVEREGENRATE` | ? | 4 | 是 | 双向 |
| 0xD4C | `SLAVERELOADRATE` | ? | 4 | 是 | 双向 |
| 0xD50 | `OPENTRANSPORTWEAPON` | ? | 4 | 是 | 双向 |
| 0xD54 | `SPAWNED` | ? | 1 | 是 | 双向 |
| 0xD58 | `SPAWNS` | ? | 4 | 是 | 双向 |
| 0xD5C | `SPAWNSNUMBER` | ? | 4 | 是 | 双向 |
| 0xD60 | `SPAWNREGENRATE` | ? | 4 | 是 | 双向 |
| 0xD64 | `SPAWNRELOADRATE` | ? | 4 | 是 | 双向 |
| 0xD68 | `MISSILESPAWN` | ? | 1 | 是 | 双向 |
| 0xD69 | `UNDERWATER` | ? | 1 | 是 | 双向 |
| 0xD6A | `BALLOONHOVER` | ? | 1 | 是 | 双向 |
| 0xD6C | `SUPPRESSIONTHRESHOLD` | ? | 4 | 是 | 双向 |
| 0xD70 | `JUMPJETTURNRATE` | ? | 4 | 是 | 双向　**同一偏移读到别的键：JUMPJETCLIMB、JUMPJETCRASH、JUMPJETACCEL、JUMPJETWOBBLES** |
| 0xD74 | `JUMPJETSPEED` | ? | 4 | 是 | 双向 |
| 0xD80 | `JUMPJETHEIGHT` | ? | 4 | 是 | 双向 |
| 0xD8C | `JUMPJETNOWOBBLES` | ? | 1 | 是 | 双向 |
| 0xD90 | `JUMPJETDEVIATION` | ? | 4 | 是 | 双向 |
| 0xD94 | `JUMPJET` | ? | 1 | 是 | 双向 |
| 0xD95 | `CRASHABLE` | ? | 1 | 是 | 双向 |
| 0xD96 | `CONSIDEREDAIRCRAFT` | ? | 1 | **否** | 双向 |
| 0xD97 | `ORGANIC` | ? | 1 | **否** | 双向 |
| 0xD98 | `NOSHADOW` | ? | 1 | 是 | 双向 |
| 0xD99 | `CANPASSIVEAQUIRE` | bool | 1 | 是 | 双向 |
| 0xD9A | `CANRETALIATE` | bool | 1 | 是 | 双向 |
| 0xD9B | `REQUIRESSTOLENTHIRDTECH` | bool | 1 | 是 | 双向 |
| 0xD9C | `REQUIRESSTOLENSOVIETTECH` | bool | 1 | 是 | 双向 |
| 0xD9D | `REQUIRESSTOLENALLIEDTECH` | bool | 1 | 是 | 双向 |
| 0xDA0 | `REQUIREDHOUSES` | ? | 4 | 是 | 双向 |
| 0xDA4 | `FORBIDDENHOUSES` | ? | 4 | 是 | 双向　**同一偏移读到别的键：AIRSTRIKETEAMTYPE、ELITEAIRSTRIKERECHARGETIME、SPEED、UNLOADINGCLASS** |
| 0xDA8 | `SECRETHOUSES` | ? | 4 | 是 | 双向 |
| 0xDAC | `USEBUFFER` | ? | 1 | 是 | 双向　**同一偏移读到别的键：PALETTE** |
| 0xDBC | `ISSELECTABLECOMBATANT` | ? | 1 | 是 | 双向 |
| 0xDBD | `ACCELERATES` | ? | 1 | 是 | 双向 |
| 0xDBE | `DISABLEVOXELCACHE` | ? | 1 | 是 | 双向　**同一偏移读到别的键：ALTCAMEO** |
| 0xDBF | `DISABLESHADOWCACHE` | ? | 1 | 是 | 单向-读 |
| 0xDC4 | `ZFUDGECOLUMN` | ? | 4 | 是 | 双向 |
| 0xDC8 | `ZFUDGETUNNEL` | ? | 4 | 是 | 双向 |
| 0xDCC | `ZFUDGEBRIDGE` | ? | 4 | 是 | 单向-读 |

### `BuildingTypeClass`

| 偏移 | 键名 | 类型 | 宽度 | 字段表里有 | 证据 |
|---:|---|---|---:|---|---|
| 0xE08 | `BUILDCAT` | ? | 4 | 是 | 双向 |
| 0xE28 | `GATECLOSEDELAY` | ? | 4 | 是 | 单向-读 |
| 0xE30 | `LIGHTVISIBILITY` | int | 4 | 是 | 双向 |
| 0xE34 | `LIGHTINTENSITY` | ? | 4 | 是 | 单向-存 |
| 0xE38 | `LIGHTREDTINT` | ? | 4 | 是 | 单向-存 |
| 0xE3C | `LIGHTGREENTINT` | ? | 4 | 是 | 单向-存 |
| 0xE40 | `LIGHTBLUETINT` | ? | 4 | 是 | 单向-存 |
| 0xEB4 | `ADJACENT` | int | 4 | 是 | 双向 |
| 0xEB8 | `FACTORY` | ? | 4 | 是 | 单向-读 |
| 0xEDC | `DEPLOYFACING` | int | 4 | 是 | 双向 |
| 0xEF0 | `FOUNDATION` | ? | 4 | 是 | 单向-读 |
| 0xEF4 | `HEIGHT` | int | 4 | 是 | 单向-读 |
| 0xF1C | `ANIMACTIVE` | ? | 4 | 是 | 单向-读 |
| 0xF44 | `ACTIVEANIM` | ? | 4 | 是 | 单向-读　**同一偏移读到别的键：ACTIVEANIMDAMAGED** |
| 0xF48 | `ACTIVEANIMGARRISONED` | ? | 4 | 是 | 单向-读 |
| 0x1050 | `ACTIVEANIMZADJUST` | int | 4 | **否** | 双向 |
| 0x1054 | `ACTIVEANIMYSORT` | int | 4 | **否** | 双向 |
| 0x1058 | `ACTIVEANIMPOWERED` | bool | 1 | **否** | 双向 |
| 0x1059 | `ACTIVEANIMPOWEREDLIGHT` | bool | 1 | **否** | 双向　**同一偏移读到别的键：ACTIVEANIMTWO** |
| 0x105A | `ACTIVEANIMTWODAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x105B | `ACTIVEANIMPOWEREDSPECIAL` | bool | 1 | **否** | 双向 |
| 0x1094 | `ACTIVEANIMTWOZADJUST` | int | 4 | **否** | 双向 |
| 0x1098 | `ACTIVEANIMTWOYSORT` | int | 4 | **否** | 双向 |
| 0x109C | `ACTIVEANIMTWOPOWERED` | bool | 1 | **否** | 双向 |
| 0x109D | `ACTIVEANIMTHREE` | ? | 1 | **否** | 单向-读 |
| 0x109E | `ACTIVEANIMTHREEDAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x10D8 | `ACTIVEANIMTHREEZADJUST` | int | 4 | **否** | 双向 |
| 0x10DC | `ACTIVEANIMTHREEYSORT` | int | 4 | **否** | 双向 |
| 0x10E0 | `ACTIVEANIMTHREEPOWERED` | bool | 1 | **否** | 双向 |
| 0x10E1 | `ACTIVEANIMFOUR` | ? | 1 | **否** | 单向-读 |
| 0x10E2 | `ACTIVEANIMFOURDAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x111C | `ACTIVEANIMFOURZADJUST` | int | 4 | **否** | 双向 |
| 0x1120 | `ACTIVEANIMFOURYSORT` | int | 4 | **否** | 双向 |
| 0x1125 | `SUPERANIM` | ? | 1 | **否** | 单向-读 |
| 0x1126 | `SUPERANIMDAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x115C | `TURRETANIM` | ? | 4 | **否** | 单向-读 |
| 0x11A0 | `IDLEANIM` | ? | 4 | **否** | 单向-读 |
| 0x11A4 | `PRODUCTIONANIMZADJUST` | int | 4 | **否** | 双向 |
| 0x11A8 | `PRODUCTIONANIMYSORT` | int | 4 | **否** | 双向　**同一偏移读到别的键：IDLEANIMDAMAGED** |
| 0x11E0 | `TURRETANIMX` | int | 4 | **否** | 双向 |
| 0x11E4 | `TURRETANIMY` | int | 4 | **否** | 双向 |
| 0x11E8 | `TURRETANIMZADJUST` | int | 4 | **否** | 双向 |
| 0x122C | `SPECIALANIMZADJUST` | int | 4 | **否** | 双向 |
| 0x1230 | `SPECIALANIMYSORT` | int | 4 | **否** | 双向 |
| 0x1234 | `SPECIALANIMPOWERED` | bool | 1 | **否** | 双向 |
| 0x1235 | `SPECIALANIMPOWEREDLIGHT` | bool | 1 | **否** | 双向　**同一偏移读到别的键：SPECIALANIMTWO** |
| 0x1236 | `SPECIALANIMTWODAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x1270 | `SPECIALANIMTWOZADJUST` | int | 4 | **否** | 双向 |
| 0x1274 | `SPECIALANIMTWOYSORT` | int | 4 | **否** | 双向 |
| 0x1279 | `SPECIALANIMTHREE` | ? | 1 | **否** | 单向-读 |
| 0x127A | `SPECIALANIMTHREEDAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x12B4 | `SPECIALANIMTHREEZADJUST` | int | 4 | **否** | 双向 |
| 0x12B8 | `SPECIALANIMTHREEYSORT` | int | 4 | **否** | 双向 |
| 0x12BD | `SPECIALANIMFOUR` | ? | 1 | **否** | 单向-读 |
| 0x12BE | `SPECIALANIMFOURDAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x12F8 | `SPECIALANIMFOURZADJUST` | int | 4 | **否** | 双向 |
| 0x1301 | `LOWPOWER` | ? | 1 | **否** | 单向-读 |
| 0x1302 | `LOWPOWERDAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x133C | `SUPERANIMZADJUST` | int | 4 | **否** | 双向 |
| 0x1340 | `SUPERANIMYSORT` | int | 4 | **否** | 双向 |
| 0x1344 | `SUPERANIMPOWERED` | bool | 1 | **否** | 双向 |
| 0x1345 | `SUPERANIMTWO` | ? | 1 | **否** | 单向-读 |
| 0x1346 | `SUPERANIMTWODAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x1380 | `SUPERANIMTWOZADJUST` | int | 4 | **否** | 双向 |
| 0x1384 | `SUPERANIMTWOYSORT` | int | 4 | **否** | 双向 |
| 0x1388 | `SUPERANIMTWOPOWERED` | bool | 1 | **否** | 双向 |
| 0x1389 | `SUPERANIMTHREE` | ? | 1 | **否** | 单向-读 |
| 0x138A | `SUPERANIMTHREEDAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x13C4 | `SUPERANIMTHREEZADJUST` | int | 4 | **否** | 双向 |
| 0x13C8 | `SUPERANIMTHREEYSORT` | int | 4 | **否** | 双向 |
| 0x13CC | `SUPERANIMTHREEPOWERED` | bool | 1 | **否** | 双向 |
| 0x13CD | `SUPERANIMFOUR` | ? | 1 | **否** | 单向-读 |
| 0x13CE | `SUPERANIMTHREEPOWEREDEFFECT` | bool | 1 | **否** | 双向　**同一偏移读到别的键：SUPERANIMFOURDAMAGED** |
| 0x1408 | `SUPERANIMFOURZADJUST` | int | 4 | **否** | 双向 |
| 0x140C | `SUPERANIMFOURYSORT` | int | 4 | **否** | 双向 |
| 0x1410 | `SUPERANIMFOURPOWERED` | bool | 1 | **否** | 双向 |
| 0x1411 | `SPECIALANIM` | ? | 1 | **否** | 单向-读 |
| 0x1412 | `SPECIALANIMDAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x144C | `IDLEANIMZADJUST` | int | 4 | **否** | 双向 |
| 0x1450 | `IDLEANIMYSORT` | int | 4 | **否** | 双向 |
| 0x1454 | `IDLEANIMPOWERED` | bool | 1 | **否** | 双向 |
| 0x1498 | `LOWPOWERPOWERED` | bool | 1 | **否** | 双向 |
| 0x1499 | `SUPERLOWPOWER` | ? | 1 | **否** | 单向-读 |
| 0x149A | `SUPERLOWPOWERDAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x14DC | `SUPERLOWPOWERPOWERED` | bool | 1 | **否** | 双向 |
| 0x14DD | `PRODUCTIONANIM` | ? | 1 | **否** | 单向-读 |
| 0x14DE | `PRODUCTIONANIMDAMAGED` | ? | 1 | **否** | 单向-读 |
| 0x14E0 | `UPGRADES` | int | 4 | 是 | 双向 |
| 0x1524 | `ANTIAIRVALUE` | int | 4 | 是 | 双向 |
| 0x1528 | `ANTIARMORVALUE` | int | 4 | 是 | 双向　**同一偏移读到别的键：WATERBOUND** |
| 0x152C | `ANTIINFANTRYVALUE` | int | 4 | 是 | 双向 |
| 0x154A | `TOGGLEPOWER` | bool | 1 | 是 | 双向　**同一偏移读到别的键：CREATEUNITSOUND、WORKINGSOUND** |
| 0x154B | `HALFDAMAGESMOKELOCATION1` | ? | 1 | 是 | 单向-读 |
| 0x154C | `NOTWORKINGSOUND` | ? | 1 | 是 | 单向-读 |
| 0x154F | `BASENORMAL` | bool | 1 | 是 | 双向 |
| 0x1550 | `ELIGIBILEFORALLYBUILDING` | bool | 1 | 是 | 双向 |
| 0x1551 | `ELIGIBLEFORDELAYKILL` | bool | 1 | 是 | 双向 |
| 0x1552 | `NEEDSENGINEER` | bool | 1 | 是 | 双向 |
| 0x1554 | `CAPTUREEVAEVENT` | ? | 4 | 是 | 双向 |
| 0x1558 | `PRODUCECASHSTARTUP` | int | 4 | 是 | 双向 |
| 0x155C | `PRODUCECASHAMOUNT` | int | 4 | 是 | 双向 |
| 0x1560 | `PRODUCECASHDELAY` | int | 4 | 是 | 双向 |
| 0x1564 | `INFANTRYGAINSELFHEAL` | int | 4 | 是 | 双向 |
| 0x1568 | `UNITSGAINSELFHEAL` | int | 4 | 是 | 双向 |
| 0x156C | `REFINERYSMOKEFRAMES` | int | 4 | 是 | 双向 |
| 0x1570 | `BIB` | bool | 1 | 是 | 双向 |
| 0x1571 | `WALL` | bool | 1 | 是 | 双向 |
| 0x1572 | `CAPTURABLE` | bool | 1 | 是 | 双向 |
| 0x1573 | `POWERED` | bool | 1 | 是 | 双向 |
| 0x1574 | `POWEREDSPECIAL` | bool | 1 | 是 | 双向 |
| 0x1575 | `OVERPOWERABLE` | bool | 1 | 是 | 双向 |
| 0x1576 | `SPYABLE` | bool | 1 | 是 | 双向 |
| 0x1577 | `CANC4` | bool | 1 | 是 | 双向 |
| 0x1579 | `UNSELLABLE` | bool | 1 | 是 | 双向 |
| 0x157A | `CLICKREPAIRABLE` | bool | 1 | 是 | 双向 |
| 0x157B | `CANBEOCCUPIED` | bool | 1 | 是 | 双向 |
| 0x157C | `CANOCCUPYFIRE` | bool | 1 | 是 | 双向 |
| 0x1580 | `MAXNUMBEROCCUPANTS` | int | 4 | 是 | 双向 |
| 0x1620 | `NUMBERIMPASSABLEROWS` | int | 4 | 是 | 双向 |
| 0x16A4 | `RADAR` | bool | 1 | 是 | 双向 |
| 0x16A5 | `SPYSAT` | bool | 1 | 是 | 双向 |
| 0x16A7 | `ISANIMDELAYEDFIRE` | bool | 1 | 是 | 双向　**同一偏移读到别的键：TOOVERLAY** |
| 0x16A9 | `UNITREPAIR` | bool | 1 | 是 | 单向-读 |
| 0x16AA | `UNITRELOAD` | bool | 1 | 是 | 双向 |
| 0x16AB | `BUNKER` | bool | 1 | 是 | 双向 |
| 0x16AC | `CLONING` | bool | 1 | 是 | 单向-读 |
| 0x16AD | `GRINDING` | bool | 1 | 是 | 双向 |
| 0x16AE | `UNITABSORB` | bool | 1 | 是 | 双向 |
| 0x16AF | `INFANTRYABSORB` | bool | 1 | 是 | 单向-读 |
| 0x16B0 | `SECRETLAB` | bool | 1 | 是 | 双向 |
| 0x16B1 | `DOUBLETHICK` | bool | 1 | 是 | 双向 |
| 0x16B2 | `FLAT` | bool | 1 | 是 | 双向 |
| 0x16B3 | `DOCKUNLOAD` | bool | 1 | 是 | 双向 |
| 0x16B4 | `RECOILLESS` | bool | 1 | 是 | 双向 |
| 0x16B5 | `HASSTUPIDGUARDMODE` | bool | 1 | 是 | 双向 |
| 0x16B6 | `BRIDGEREPAIRHUT` | bool | 1 | 是 | 双向 |
| 0x16B7 | `GATE` | bool | 1 | 是 | 单向-读 |
| 0x16B9 | `CONSTRUCTIONYARD` | bool | 1 | 是 | 双向 |
| 0x16BA | `NUKESILO` | bool | 1 | 是 | 单向-读 |
| 0x16BB | `REFINERY` | bool | 1 | 是 | 双向 |
| 0x16BD | `WEAPONSFACTORY` | bool | 1 | 是 | 双向 |
| 0x16C1 | `HOSPITAL` | bool | 1 | 是 | 单向-读 |
| 0x16C4 | `CHARGEDANIMTIME` | ? | 1 | 是 | 单向-读 |
| 0x16C5 | `TURRETANIMISVOXEL` | bool | 1 | 是 | 双向 |
| 0x16C8 | `SENSORARRAY` | bool | 1 | 是 | 双向 |
| 0x16C9 | `TARGETCOORDOFFSET` | ? | 1 | 是 | 单向-读　**同一偏移读到别的键：EXITCOORD** |
| 0x16CB | `HELIPAD` | bool | 1 | 是 | 双向 |
| 0x16CC | `OREPURIFIER` | bool | 1 | 是 | 双向　**同一偏移读到别的键：FREEUNIT** |
| 0x16CD | `FACTORYPLANT` | bool | 1 | 是 | 双向 |
| 0x16E4 | `GDIBARRACKS` | bool | 1 | 是 | 双向 |
| 0x16E5 | `NODBARRACKS` | bool | 1 | 是 | 单向-读 |
| 0x16E6 | `YURIBARRACKS` | bool | 1 | 是 | 双向 |
| 0x16EC | `DELAYEDFIREDELAY` | int | 4 | 是 | 双向 |
| 0x16F0 | `SUPERWEAPON` | ? | 4 | 是 | 双向 |
| 0x16F8 | `GATESTAGES` | int | 4 | 是 | 双向 |
| 0x1701 | `INVISIBLEINGAME` | bool | 1 | 是 | 双向 |
| 0x1702 | `TERRAINPALETTE` | bool | 1 | 是 | 双向 |
| 0x1703 | `PLACEANYWHERE` | bool | 1 | 是 | 双向 |
| 0x1704 | `EXTRADAMAGESTAGE` | bool | 1 | 是 | 双向 |
| 0x1705 | `AIBUILDTHIS` | bool | 1 | 是 | 双向 |
| 0x1706 | `ISBASEDEFENSE` | bool | 1 | 是 | 双向 |
| 0x1708 | `CONCENTRICRADIALINDICATOR` | bool | 1 | 是 | 双向 |
| 0x170C | `PSYCHICDETECTIONRADIUS` | int | 4 | 是 | 双向 |
| 0x1764 | `PRIMARYFIREDUALOFFSET` | bool | 1 | 是 | 双向 |
| 0x1765 | `PROTECTWITHWALL` | bool | 1 | 是 | 双向 |
| 0x1766 | `CANHIDETHINGS` | bool | 1 | 是 | 双向 |
| 0x1767 | `CRATEBENEATH` | bool | 1 | 是 | 双向 |
| 0x1768 | `LEAVERUBBLE` | bool | 1 | 是 | 双向 |
| 0x1769 | `CRATEBENEATHISMONEY` | bool | 1 | 是 | 双向 |
| 0x1780 | `NUMBEROFDOCKS` | int | 4 | 是 | 双向 |

### `InfantryTypeClass`

| 偏移 | 键名 | 类型 | 宽度 | 字段表里有 | 证据 |
|---:|---|---|---:|---|---|
| 0xDFC | `PIP` | ? | 4 | 是 | 双向　**同一偏移读到别的键：ELITEOCCUPYWEAPON** |
| 0xE00 | `OCCUPYPIP` | ? | 4 | 是 | 单向-读　**同一偏移读到别的键：OCCUPYWEAPON** |
| 0xE40 | `FIREUP` | int | 4 | 是 | 双向 |
| 0xE48 | `SECONDARYFIRE` | int | 4 | 是 | 单向-读 |
| 0xEAC | `ENTERWATERSOUND` | ? | 1 | 是 | 单向-读　**同一偏移读到别的键：LEAVEWATERSOUND** |
| 0xEAD | `NOTHUMAN` | bool | 1 | 是 | 双向 |
| 0xEAE | `IVAN` | bool | 1 | 是 | 双向 |
| 0xEB4 | `OCCUPIER` | bool | 1 | 是 | 双向 |
| 0xEB5 | `ASSAULTER` | bool | 1 | 是 | 单向-读 |
| 0xEB8 | `HARVESTRATE` | int | 4 | 是 | 双向 |
| 0xEBC | `FEARLESS` | bool | 1 | 是 | 双向 |
| 0xEBD | `CRAWLS` | bool | 1 | 是 | 单向-读 |
| 0xEBE | `INFILTRATE` | bool | 1 | 是 | 单向-读 |
| 0xEBF | `FRAIDYCAT` | bool | 1 | 是 | 双向 |
| 0xEC0 | `TIBERIUMPROOF` | bool | 1 | 是 | 单向-读 |
| 0xEC1 | `CIVILIAN` | bool | 1 | 是 | 双向 |
| 0xEC3 | `ENGINEER` | bool | 1 | 是 | 双向 |
| 0xEC4 | `AGENT` | bool | 1 | 是 | 双向 |
| 0xEC8 | `DEPLOYER` | bool | 1 | 是 | 双向 |
| 0xEC9 | `DEPLOYEDCRUSHABLE` | bool | 1 | 是 | 单向-读 |
| 0xECA | `USEOWNNAME` | bool | 1 | 是 | 双向 |
| 0xECB | `JUMPJETTURN` | bool | 1 | 是 | 双向 |

### `ObjectTypeClass`

| 偏移 | 键名 | 类型 | 宽度 | 字段表里有 | 证据 |
|---:|---|---|---:|---|---|
| 0x9C | `ARMOR` | ? | 4 | 是 | 双向 |
| 0xA0 | `STRENGTH` | int | 4 | 是 | 双向 |
| 0x1E8 | `NOSPAWNALT` | bool | 1 | 是 | 双向 |
| 0x211 | `ALTERNATEARCTICART` | bool | 1 | 是 | 单向-读 |
| 0x22C | `THEATER` | bool | 1 | 是 | 单向-读 |
| 0x22D | `CRUSHABLE` | bool | 1 | 是 | 单向-读 |
| 0x22E | `BOMBABLE` | bool | 1 | 是 | 双向 |
| 0x22F | `RADARINVISIBLE` | bool | 1 | 是 | 双向 |
| 0x230 | `SELECTABLE` | bool | 1 | 是 | 双向 |
| 0x231 | `LEGALTARGET` | bool | 1 | 是 | 单向-读 |
| 0x232 | `INSIGNIFICANT` | bool | 1 | 是 | 双向 |
| 0x233 | `IMMUNE` | bool | 1 | 是 | 单向-读 |
| 0x236 | `VOXEL` | bool | 1 | 是 | 双向 |
| 0x237 | `NEWTHEATER` | bool | 1 | 是 | 双向 |
| 0x238 | `HASRADIALINDICATOR` | bool | 1 | 是 | 单向-读 |

### `UnitTypeClass`

| 偏移 | 键名 | 类型 | 宽度 | 字段表里有 | 证据 |
|---:|---|---|---:|---|---|
| 0x67C | `SPEEDTYPE` | ? | 4 | **否** | 双向 |
| 0xDFC | `MOVEMENTRESTRICTEDTO` | ? | 4 | 是 | 双向 |
| 0xE0D | `CRATEGOODIE` | bool | 1 | 是 | 双向 |
| 0xE0E | `HARVESTER` | bool | 1 | 是 | 双向 |
| 0xE13 | `ISSIMPLEDEPLOYER` | bool | 1 | 是 | 双向 |
| 0xE14 | `ISTILTER` | bool | 1 | 是 | 单向-读 |
| 0xE16 | `TOOBIGTOFITUNDERBRIDGE` | bool | 1 | 是 | 单向-读 |
| 0xE1A | `CARRIESCRATE` | bool | 1 | 是 | 双向 |
| 0xE38 | `REPAIRTURRETINDEX` | int | 4 | 是 | 单向-读　**同一偏移读到别的键：REPAIRTURRETWEAPON、MACHINEGUNTURRETINDEX、MACHINEGUNTURRETWEAPON、FLAKTURRETINDEX、FLAKTURRETWEAPON、PISTOLTURRETINDEX、PISTOLTURRETWEAPON、SNIPERTURRETINDEX、SNIPERTURRETWEAPON、SHOCKTURRETINDEX、SHOCKTURRETWEAPON、EXPLODETURRETINDEX、EXPLODETURRETWEAPON、BRAINBLASTTURRETINDEX、BRAINBLASTTURRETWEAPON、RADCANNONTURRETINDEX、RADCANNONTURRETWEAPON、CHRONOTURRETINDEX、CHRONOTURRETWEAPON、TERRORISTEXPLODETURRETINDEX、TERRORISTEXPLODETURRETWEAPON、COWTURRETINDEX、COWTURRETWEAPON、INITIATETURRETINDEX、INITIATETURRETWEAPON、VIRUSTURRETINDEX、VIRUSTURRETWEAPON、YURIPRIMETURRETINDEX、YURIPRIMETURRETWEAPON、GUARDIANTURRETINDEX、GUARDIANTURRETWEAPON** |
| 0xE5C | `WALKFRAMES` | int | 1 | 是 | 双向/宽度存疑 |
| 0xE5D | `FIRINGFRAMES` | int | 1 | 是 | 双向/宽度存疑 |

## 覆盖面（有多少键没配上）

**没配上的键名不进常量表** —— 这条通道只声称它真看到的东西。
下面把差额数出来，是为了不让『覆盖率』看起来像『全量』。

| Read_INI | 归属类（提供实现的那个） | 出现的键 | 配上偏移 | 没配上 | 同一实现的其它类 |
|---|---|---:|---:|---:|---|
| `0x00712170` | `TechnoTypeClass` | 252 | 250 | 2 | — |
| `0x0045FE50` | `BuildingTypeClass` | 193 | 181 | 12 | — |
| `0x00747620` | `UnitTypeClass` | 44 | 42 | 2 | — |
| `0x005240A0` | `InfantryTypeClass` | 25 | 25 | 0 | — |
| `0x005F92D0` | `ObjectTypeClass` | 19 | 15 | 4 | `IsometricTileTypeClass` |

**「同一实现的其它类」这一列**：这些类的虚表槽位上放着**同一个**函数指针，
也就是它们没有覆盖 `Read_INI`，用的是祖先的实现。字段归实现者，
不给它们各复制一份记录 —— 否则「某类有 N 个命名字段」会把继承来的
当成自己的。这一列留着是为了回答『谁在读这些键』。

**`TechnoTypeClass` 没配上的 2 个键**：

> `DEATHWEAPONDAMAGEMODIFIER`、`ELITEAIRSTRIKETEAM`

**`BuildingTypeClass` 没配上的 12 个键**：

> `AIRCRAFTCOSTBONUS`、`BUILDINGSCOSTBONUS`、`BUILDUP`、`DEFENSESCOSTBONUS`、`EXTRAPOWER`、`INFANTRYCOSTBONUS`、`OCCUPYHEIGHT`、`POWER`、`PRIMARYFIREPIXELOFFSET`、`QUEUEINGCELL`、`UNITSCOSTBONUS`、`ZSHAPEPOINTMOVE`

**`UnitTypeClass` 没配上的 2 个键**：

> `NORMALTURRETINDEX`、`NORMALTURRETWEAPON`

**`ObjectTypeClass` 没配上的 4 个键**：

> `ALPHAIMAGE`、`AMBIENTSOUND`、`CRUSHSOUND`、`IMAGE`

## 同一个偏移被两个键名主张 —— 按证据强度取舍

取舍规则不是『有没有人争』，而是『争的人证据够不够硬』：

| 记录本身 | 有人来争同一偏移 | 处理 |
|---|---|---|
| 双向（缺省值与结果同偏移） | 是 | **保留**。它自洽，且手工反汇编核对过 |
| 单向 | 是 | 弃用。本来就弱，再来一个人争就没理由留下 |
| 单向 | 否 | 保留，标 `单向-*` |

这条规则是被 `ra2core` 的锚点断言逼出来的：最初一刀切『有争议就丢』，把手工核对过的 `Cost@0x610` / `TechLevel@0x634` 一起丢掉了，冒烟测试立刻红。

| 类 | 偏移 | 留下的键名 | 档位 | 来争的键名 |
|---|---:|---|---|---|
| `BuildingTypeClass` | 0xF44 | `ACTIVEANIM` | 单向-读 | `ACTIVEANIMDAMAGED` |
| `BuildingTypeClass` | 0x1059 | `ACTIVEANIMPOWEREDLIGHT` | 双向 | `ACTIVEANIMTWO` |
| `BuildingTypeClass` | 0x11A8 | `PRODUCTIONANIMYSORT` | 双向 | `IDLEANIMDAMAGED` |
| `BuildingTypeClass` | 0x1235 | `SPECIALANIMPOWEREDLIGHT` | 双向 | `SPECIALANIMTWO` |
| `BuildingTypeClass` | 0x13CE | `SUPERANIMTHREEPOWEREDEFFECT` | 双向 | `SUPERANIMFOURDAMAGED` |
| `BuildingTypeClass` | 0x1528 | `ANTIARMORVALUE` | 双向 | `WATERBOUND` |
| `BuildingTypeClass` | 0x154A | `TOGGLEPOWER` | 双向 | `CREATEUNITSOUND`、`WORKINGSOUND` |
| `BuildingTypeClass` | 0x16A7 | `ISANIMDELAYEDFIRE` | 双向 | `TOOVERLAY` |
| `BuildingTypeClass` | 0x16C9 | `TARGETCOORDOFFSET` | 单向-读 | `EXITCOORD` |
| `BuildingTypeClass` | 0x16CC | `OREPURIFIER` | 双向 | `FREEUNIT` |
| `InfantryTypeClass` | 0xDFC | `PIP` | 双向 | `ELITEOCCUPYWEAPON` |
| `InfantryTypeClass` | 0xE00 | `OCCUPYPIP` | 单向-读 | `OCCUPYWEAPON` |
| `InfantryTypeClass` | 0xEAC | `ENTERWATERSOUND` | 单向-读 | `LEAVEWATERSOUND` |
| `TechnoTypeClass` | 0x394 | `VHPSCAN` | 双向 | `DEBRISTYPES` |
| `TechnoTypeClass` | 0x3A8 | `PITCHSPEED` | 单向-读 | `LOCOMOTOR` |
| `TechnoTypeClass` | 0x3B8 | `BUILDLIMIT` | 双向 | `UNDEPLOYSINTO` |
| `TechnoTypeClass` | 0x3BC | `CATEGORY` | 单向-读 | `POWERSUNIT` |
| `TechnoTypeClass` | 0x568 | `MOVESOUND` | 单向-读 | `DIESOUND` |
| `TechnoTypeClass` | 0x5BC | `MAXDEBRIS` | 双向 | `DEBRISANIMS` |
| `TechnoTypeClass` | 0x610 | `COST` | 双向 | `DEPLOYINGANIM` |
| `TechnoTypeClass` | 0x634 | `TECHLEVEL` | 双向 | `ELITEAIRSTRIKETEAMTYPE` |
| `TechnoTypeClass` | 0x6BC | `VETERANABILITIES` | 单向-读 | `ELITEABILITIES`、`PRIMARYFIREFLH`、`ELITEPRIMARYFIREFLH` |
| `TechnoTypeClass` | 0x774 | `DAMAGESMOKEOFFSET` | 单向-读 | `REFINERYSMOKEOFFSETONE`、`REFINERYSMOKEOFFSETTWO`、`REFINERYSMOKEOFFSETTHREE`、`REFINERYSMOKEOFFSETFOUR`、`PREREQUISITE`、`PREREQUISITEOVERRIDE` |
| `TechnoTypeClass` | 0x7FC | `SHADOWINDEX` | 双向 | `CAMEO` |
| `TechnoTypeClass` | 0x800 | `STORAGE` | 双向 | `DOCK`、`DEPLOYSINTO` |
| `TechnoTypeClass` | 0x80C | `WEAPONCOUNT` | 双向 | `PRIMARY`、`ELITEPRIMARY`、`ELITESECONDARY`、`AUXSOUND2`、`SINKINGSOUND`、`SECONDARYFIREFLH` |
| `TechnoTypeClass` | 0x8C8 | `ELITESECONDARYFIREFLH` | 单向-读 | `SECONDSPAWNOFFSET` |
| `TechnoTypeClass` | 0xA90 | `SECONDARY` | 单向-读 | `AUXSOUND1`、`CREATESOUND`、`DAMAGESOUND`、`IMPACTLANDSOUND`、`CRASHINGSOUND`、`VOICECRASHING`、`VOICEENTER`、`VOICECAPTURE` |
| `TechnoTypeClass` | 0xAB0 | `VOICEMOVE` | 单向-读 | `VOICESELECT`、`VOICESELECTENSLAVED`、`VOICESELECTDEACTIVATED`、`VOICEATTACK`、`VOICESPECIALATTACK`、`VOICEFEEDBACK` |
| `TechnoTypeClass` | 0xCA1 | `TURRET` | 单向-读 | `LEAVETRANSPORTSOUND`、`UNDEPLOYSOUND`、`VOICEHARVEST`、`VOICESECONDARYWEAPONATTACK`、`DEACTIVATESOUND`、`REFINERYSMOKEPARTICLESYSTEM`、`DAMAGEPARTICLESYSTEMS` |
| `TechnoTypeClass` | 0xCA2 | `TURRETRECOIL` | 单向-读 | `TURRETTRAVEL`、`BARRELTRAVEL`、`BARRELCOMPRESSFRAMES`、`BARRELHOLDFRAMES`、`BARRELRECOVERFRAMES` |
| `TechnoTypeClass` | 0xD21 | `TURRETSPINS` | 双向 | `TURRETROTATESOUND`、`CHRONOINSOUND` |
| `TechnoTypeClass` | 0xD22 | `TILTCRASHJUMPJET` | 双向 | `ENTERTRANSPORTSOUND`、`DEPLOYSOUND`、`CHRONOOUTSOUND`、`VOICEDEPLOY`、`ACTIVATESOUND`、`EXPLOSION`、`DESTROYANIM` |
| `TechnoTypeClass` | 0xD70 | `JUMPJETTURNRATE` | 双向 | `JUMPJETCLIMB`、`JUMPJETCRASH`、`JUMPJETACCEL`、`JUMPJETWOBBLES` |
| `TechnoTypeClass` | 0xDA4 | `FORBIDDENHOUSES` | 双向 | `AIRSTRIKETEAMTYPE`、`ELITEAIRSTRIKERECHARGETIME`、`SPEED`、`UNLOADINGCLASS` |
| `TechnoTypeClass` | 0xDAC | `USEBUFFER` | 双向 | `PALETTE` |
| `TechnoTypeClass` | 0xDBE | `DISABLEVOXELCACHE` | 双向 | `ALTCAMEO` |
| `UnitTypeClass` | 0xE38 | `REPAIRTURRETINDEX` | 单向-读 | `REPAIRTURRETWEAPON`、`MACHINEGUNTURRETINDEX`、`MACHINEGUNTURRETWEAPON`、`FLAKTURRETINDEX`、`FLAKTURRETWEAPON`、`PISTOLTURRETINDEX`、`PISTOLTURRETWEAPON`、`SNIPERTURRETINDEX`…（共 31 个） |

## 这条通道够不到的三种形态

覆盖率不是 100%，差额有明确原因。写在这里是为了让『没配上』看起来
像『漏了』时能一眼找到解释 —— 而不是靠调宽规则硬凑数字。

**一、结果经过变换再存。** 最典型的是 `Speed`。它在
`TechnoTypeClass::Read_INI@0x71464C` 被读出来之后，先钳到 100、再乘 256/100、
再钳到 255，最后才存进字段：

```asm
push -1                            ; 缺省值是立即数 -1，不是从字段读的
push 0x81D9CC                      ; "Speed"
call 0x5276D0                      ; ReadInteger
cmp eax, -1 / je …                 ; 下面是钳位与 ×256/100 的定点换算
…
mov dword ptr [ebp + 0x678], edx   ; 结果落在 0x678，**不在**键名旁边
```
按『键名旁边的那条存指令』会错认成 `0x630` —— 而 `0x630` 其实属于
**上一条**键（它的结果存被调度器插到了这里）。所以本工具宁可**不命名**。

**二、数组字段。** `TurretType[i]` 这类：32 个 `XxxTurretIndex` / `XxxTurretWeapon`
都往同一段内存里写，索引在寄存器里。相邻存规则会把它们全指到基偏移上，
于是 32 条键名争一个偏移。这些进 `conflict`，**不写进 C++ 常量表**。

**三、目的地不是本对象的字段。** 字符串键常常先读进栈上的临时缓冲，
再 `strcpy` 进对象；中间隔了 `strlen` / `rep movs`。同样够不到。

