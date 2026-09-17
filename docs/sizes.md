# gamemd.exe 类大小实测值

由 `tools/sizeofscan.py` 静态分析得出，未运行目标进程。

## 方法

1. 先定位 `operator new`：被调用前紧跟 `push <常量>` 次数最多的函数。
   实测为 `0x007C8E17`，其函数体是 `push 1; push [esp+8]; call _nh_malloc`，确认无误。
2. 找构造函数：函数开头 32 条指令内出现 `mov [<this>], <虚表 VA>`
   （this 可能在 ecx/esi/eax 等任意寄存器里），或 `mov <reg>, <虚表 VA>`。
3. 配对三种形态：
   - `push N; call new` 之后紧邻写虚表（构造函数被内联）
   - `push N; call new` 之后 16 条指令内 `call <构造函数>`
   - 工厂函数：整个函数只有一处分配、只调用一个类的构造函数
4. **用字段末端给候选做硬过滤**（见下）。

`evidence=new` 表示来自分配大小（直接证据）；`evidence=stride` 表示来自
静态数组遍历步长（间接证据，置信度低一些）。

### 为什么需要第 4 步

「最近配对」会配错：构造函数里往往还嵌着**别的**分配（子对象、临时
缓冲），那些 `push N; call new` 会被记到外层类头上。实测 `CCFileClass`
就是 —— 投票选出 36（4 票），而字段偏移扫描说这个类的构造函数在
`+0x68` 上写过 4 字节，**对象至少 108 字节**；108 也在候选里，只是票少。

判据是硬的：字段偏移是『对象内偏移』，必须落在 sizeof 之内。两条数据
来自完全不同的机制（分配常量配对 vs 指令流里 this 偏移跟踪），谁也不能
回头改谁。所以规则是：**在候选里挑一个与字段不矛盾的**（优先票数，同票
取小）；如果连一个都不剩，就不改，只记 `note` 留给人看 —— 那说明两条
数据里有一条本身错了，乱猜比不改更坏。

被这条规则改过的条目带 `calibrated` 标记，无论票数多少都进 C++ 表。

## 结果（共 233 个类）

| 类 | sizeof | 证据 | 票数 | 字段末端 | 备注 |
|---|---:|---|---:|---:|---|
| `WinModemClass` | 8328 | new | 4 | 108 |  |
| `AircraftTypeClass` | 3600 | new | 6 | 3599 |  |
| `UnitClass` | 2280 | new | 23 | 1768 |  |
| `PBVBuildingTypeClass::?$VectorClass` | 2220 | stride | 2 | 14 |  |
| `PBVParticleSystemTypeClass::?$VectorClass` | 1940 | stride | 1 | 14 |  |
| `PAD::?$VectorClass` | 1824 | new | 2 | 14 |  |
| `InfantryClass` | 1776 | new | 21 | 1772 |  |
| `AircraftClass` | 1752 | new | 4 | 1750 |  |
| `WebBrowser` | 1044 | new | 1 | 1044 |  |
| `CampaignClass` | 928 | new | 4 | 672 |  |
| `PAUNodeNameType::?$DynamicVectorClass` | 836 | new | 1 |  |  |
| `VRGBClass::?$TypeList` | 792 | new | 11 |  |  |
| `TerrainTypeClass` | 700 | new | 6 | 700 |  |
| `SmudgeTypeClass` | 676 | new | 6 | 674 |  |
| `CCToolTip` | 616 | new | 3 | 4 |  |
| `PBVToolTip::?$DynamicVectorClass` | 616 | new | 3 |  |  |
| `PAVCellClass::?$VectorClass` | 576 | new | 2 | 14 |  |
| `ScriptTypeClass` | 564 | new | 12 | 164 |  |
| `AlphaShapeClass` | 528 | new | 2 | 61 |  |
| `PBVAnimTypeClass::?$TypeList` | 464 | new | 8 | 24 |  |
| `TubeClass` | 452 | new | 8 | 452 |  |
| `VWstring::?$DynamicVectorClass` | 444 | new | 6 |  |  |
| `W4DiskID::?$TypeList` | 444 | new | 7 |  |  |
| `MapSeedClass` | 376 | new | 4 | 122 |  |
| `VeinholeMonsterClass` | 264 | new | 2 | 264 |  |
| `E::?$BlitPlainXlat` | 256 | new | 2 |  |  |
| `PAVParticleClass::?$DynamicVectorClass` | 256 | new | 19 |  |  |
| `TerrainClass` | 224 | new | 14 | 224 |  |
| `TaskForceClass` | 212 | new | 10 | 172 |  |
| `IPXConnClass` | 184 | new | 4 | 104 |  |
| `PAVPlanningMemberClass::?$VectorClass` | 184 | new | 2 | 180 |  |
| `H::?$TypeList` | 180 | new | 8 | 24 |  |
| `TriggerTypeClass` | 180 | new | 11 | 180 |  |
| `IsometricTileClass` | 176 | new | 4 | 176 |  |
| `OverlayClass` | 176 | new | 8 | 176 |  |
| `SmudgeClass` | 176 | new | 5 | 176 |  |
| `ObjectClass` | 172 | stride | 3 | 172 |  |
| `AbstractTypeClass` | 160 | stride | 1 | 101 |  |
| `PAVPlanningNodeClass::?$DynamicVectorClass` | 156 | new | 9 |  |  |
| `PAVPlanningNodeClass::?$VectorClass` | 156 | new | 1 | 14 |  |
| `Mouse` | 152 | new | 6 | 4 |  |
| `TActionClass` | 148 | new | 3 | 148 |  |
| `PAVMultiplayerGameMode::?$DynamicVectorClass` | 133 | new | 2 | 24 |  |
| `SuperClass` | 128 | new | 2 | 128 |  |
| `FoggedObjectClass::UDrawRecord::?$DynamicVectorClass` | 120 | new | 2 |  |  |
| `PBVTechnoTypeClass::?$DynamicVectorClass` | 116 | new | 26 |  |  |
| `RadSiteClass` | 116 | new | 6 | 116 |  |
| `SpawnManagerClass::PAUSpawnControl::?$DynamicVectorClass` | 116 | new | 2 | 24 |  |
| `H::V?$TRect::?$VectorClass` | 112 | new | 2 | 14 |  |
| `CCFileClass` | 108 | new | 2 | 108 | 原选 36 与字段末端 108 矛盾（36 < 108），改成候选里合法且票数最高的 108 |
| `LoadProgressMgr` | 100 | new | 2 | 100 |  |
| `SlaveManagerClass::PAUSlaveControl::?$DynamicVectorClass` | 100 | new | 2 |  |  |
| `AirstrikeClass` | 96 | new | 4 | 96 |  |
| `FlyLocomotionClass` | 96 | new | 2 | 93 |  |
| `RocketLocomotionClass` | 96 | new | 2 | 92 |  |
| `ShapeButtonClass` | 96 | stride | 2 | 93 |  |
| `BombClass` | 92 | new | 4 | 89 |  |
| `INoticeSink` | 92 | new | 2 |  |  |
| `CCINIClass` | 88 | new | 6 | 97 | 字段末端 97 超过所有候选 [88] —— 两者必有一错，留给人看 |
| `GenericNode` | 88 | new | 2 | 12 |  |
| `INIClass::PAUINISection::?$List` | 88 | new | 4 | 28 |  |
| `ParasiteClass` | 88 | new | 4 | 85 |  |
| `TEventClass` | 88 | new | 3 | 88 |  |
| `NullModemConnClass` | 84 | new | 2 | 84 |  |
| `PAUControlNode::?$DynamicVectorClass` | 80 | new | 2 |  |  |
| `TemporalClass` | 80 | new | 4 | 80 |  |
| `LightSourceClass` | 76 | new | 4 | 73 |  |
| `PAD::PAV?$DynamicVectorClass::?$VectorClass` | 76 | stride | 2 | 14 |  |
| `TeleportLocomotionClass` | 76 | new | 2 | 76 |  |
| `TextLabelClass` | 76 | new | 4 | 76 |  |
| `VWDTState::?$rc_ptr` | 76 | new | 2 | 4 |  |
| `WorldDominationTour::VCampaign::?$rc_ptr` | 76 | new | 2 | 4 |  |
| `BufferIOFileClass` | 72 | stride | 2 | 84 | 字段末端 84 超过所有候选 [72] —— 两者必有一错，留给人看 |
| `TriggerClass` | 72 | new | 6 | 69 |  |
| `BitFont` | 68 | new | 2 | 66 |  |
| `INIClass::PAUINISection::?$Node` | 68 | new | 3 |  |  |
| `DiskLaserClass` | 64 | new | 4 | 64 |  |
| `FreeForAll` | 64 | new | 1 | 4 |  |
| `MSShapeAnim` | 64 | new | 3 | 61 |  |
| `MultiplayerBattle` | 64 | new | 1 | 4 |  |
| `MultiplayerManBattle` | 64 | new | 1 | 4 |  |
| `MultiplayerSiege` | 64 | new | 1 | 4 |  |
| `UnholyAlliance` | 64 | new | 1 | 4 |  |
| `VWDTTerritory::?$rc_ptr` | 64 | new | 1 | 4 |  |
| `VWaypointClass::?$DynamicVectorClass` | 64 | new | 24 |  |  |
| `WDTTerritory` | 64 | new | 2 | 64 |  |
| `PixelFXClass` | 60 | new | 4 | 4 |  |
| `TunnelLocomotionClass` | 60 | new | 2 | 57 |  |
| `WalkLocomotionClass` | 60 | new | 2 | 60 |  |
| `ControlClass` | 56 | stride | 1 | 44 |  |
| `GraphicMenuImageItem` | 56 | new | 2 | 52 |  |
| `NeuronClass` | 56 | new | 3 | 52 |  |
| `TagClass` | 56 | new | 8 | 54 |  |
| `EMPulseClass` | 52 | new | 2 | 52 |  |
| `MSAnim` | 52 | new | 12 | 4 |  |
| `MechLocomotionClass` | 52 | new | 2 | 49 |  |
| `UtagCONNECTDATA::?$DynamicVectorClass` | 52 | new | 2 |  |  |
| `DropPodLocomotionClass` | 48 | new | 2 | 48 |  |
| `MSVQAnim` | 48 | new | 3 | 46 |  |
| `ScriptClass` | 48 | new | 9 | 48 |  |
| `CStreamClass` | 44 | new | 2 | 40 |  |
| `PAVMSAnim::?$VectorClass` | 44 | stride | 2 | 14 |  |
| `WorldDominationTour::VTerritory::?$rc_ptr` | 44 | stride | 1 | 4 |  |
| `GraphicMenuAnimItem` | 40 | new | 2 | 40 |  |
| `I::?$DynamicVectorClass` | 40 | new | 4 | 24 |  |
| `MixFileClass` | 40 | new | 106 | 40 |  |
| `DSurface` | 36 | new | 41 | 36 |  |
| `PAUIConnectionPoint::?$VectorClass` | 36 | new | 2 | 14 |  |
| `RawFileClass` | 36 | new | 4 | 33 |  |
| `USubzoneConnectionStruct::?$DynamicVectorClass` | 36 | stride | 2 | 36 |  |
| `UtagCONNECTDATA::?$VectorClass` | 36 | new | 2 | 14 |  |
| `ATL::VCChatEventSink::?$CComObject` | 32 | new | 2 |  |  |
| `ATL::VCDownloadEventSink::?$CComObject` | 32 | new | 2 | 8 |  |
| `ATL::VCNetUtilEventSink::?$CComObject` | 32 | new | 2 |  |  |
| `MSFont` | 32 | new | 4 | 29 |  |
| `Surface` | 32 | new | 4 | 12 |  |
| `WorldDominationTour::Voices::Anim` | 32 | new | 2 | 32 |  |
| `XSurface` | 32 | new | 27 | 16 |  |
| `H::?$DynamicVectorClass` | 24 | new | 2 | 24 |  |
| `H::?$VectorClass` | 24 | stride | 4 | 14 |  |
| `II::U?$HashObject::?$VectorClass` | 24 | stride | 3 | 14 |  |
| `IUSubzoneConnectionStruct::U?$HashObject::?$VectorClass` | 24 | stride | 3 | 14 |  |
| `PAVFoggedObjectClass::?$DynamicVectorClass` | 24 | new | 4 |  |  |
| `PAVSubTitle::?$DynamicVectorClass` | 24 | new | 2 |  |  |
| `PAVTechnoClass::?$DynamicVectorClass` | 24 | new | 10 | 24 |  |
| `PAVTechnoClass::?$VectorClass` | 24 | new | 9 | 14 |  |
| `PBD::?$DynamicVectorClass` | 24 | new | 2 | 24 |  |
| `PBD::?$VectorClass` | 24 | new | 2 | 14 |  |
| `ScoreFontClass` | 24 | new | 1 | 24 |  |
| `USubzoneConnectionStruct::?$VectorClass` | 24 | stride | 4 | 14 |  |
| `VCell::?$DynamicVectorClass` | 24 | new | 14 | 24 |  |
| `VCell::?$VectorClass` | 24 | new | 6 | 14 |  |
| `VPoint2D::?$VectorClass` | 24 | stride | 6 | 14 |  |
| `VQMovieHandle` | 24 | new | 2 | 21 |  |
| `WorldDominationTour::GameOption` | 24 | new | 2 | 4 |  |
| `WorldDominationTour::VCampaignProperties::?$rc_ptr` | 24 | new | 2 | 4 |  |
| `BinkMovieHandle` | 20 | new | 2 | 20 |  |
| `I::?$VectorClass` | 20 | new | 2 | 14 |  |
| `PAUtConnInfoStruct::?$VectorClass` | 20 | new | 2 | 14 |  |
| `PAD::?$DynamicVectorClass` | 16 | new | 11 |  |  |
| `PAVSchemeNode::VHashString::U?$HashObject::?$VectorClass` | 16 | new | 1 | 14 |  |
| `PAVTechnoClass::URadarTrackingStruct::U?$HashObject::?$VectorClass` | 16 | new | 2 | 14 |  |
| `ReferenceCounted` | 16 | new | 4 | 4 |  |
| `rc_ptr_base` | 16 | new | 4 | 8 |  |
| `MultiplayerObserverTeam` | 12 | new | 2 | 4 |  |
| `MultiplayerSiegeAttackerTeam` | 12 | new | 1 | 4 |  |
| `MultiplayerSiegeDefenderTeam` | 12 | new | 1 | 4 |  |
| `WorldDominationTour::MapSizeGameOption` | 12 | new | 2 |  |  |
| `AddTeamCommandClass` | 8 | new | 20 | 8 |  |
| `CampaignScoreClass` | 8 | new | 2 | 8 |  |
| `CenterTeamCommandClass` | 8 | new | 20 | 8 |  |
| `CounterClass` | 8 | new | 1 | 20 | 字段末端 20 超过所有候选 [8] —— 两者必有一错，留给人看 |
| `CreateTeamCommandClass` | 8 | new | 20 | 8 |  |
| `PAVReestablish::?$VectorClass` | 8 | new | 1 | 14 | 字段末端 14 超过所有候选 [8] —— 两者必有一错，留给人看 |
| `SelectTeamCommandClass` | 8 | new | 20 | 8 |  |
| `TauntCommandClass` | 8 | new | 16 | 8 |  |
| `VAITriggerTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VAircraftClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VAircraftTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VAirstrikeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VAlphaShapeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VAnimClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VAnimTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VBombClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VBuildingClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VBuildingLightClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VBuildingTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VBulletClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VBulletTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VCStreamClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VCampaignClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VCaptureManagerClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VCellClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VDiskLaserClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VDriveLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VDropPodLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VEMPulseClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VFactoryClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VFlyLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VFoggedObjectClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VHouseClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VHouseTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VHoverLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VInfantryClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VInfantryTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VIsometricTileTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VJumpjetLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VLightSourceClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VMechLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VNeuronClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VOverlayTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VParasiteClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VParticleClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VParticleSystemClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VParticleSystemTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VParticleTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VRadSiteClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VRocketLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VScriptClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VScriptTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VShipLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VSideClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VSlaveManagerClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VSmudgeTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VSpawnManagerClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VSuperClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VSuperWeaponTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTActionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTEventClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTactical::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTagClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTagTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTaskForceClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTeamClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTeamTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTeleportLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTemporalClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTerrainClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTerrainTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTiberiumClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTriggerClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTriggerTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTubeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VTunnelLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VUnitClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VUnitTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VVoxelAnimClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VVoxelAnimTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VWalkLocomotionClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VWarheadTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VWaveClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VWaypointPathClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
| `VWeaponTypeClass::?$TClassFactory` | 8 | new | 2 | 8 |  |
