# gamemd.exe 类层次（RTTI 自动抽取）

由 `tools/genmodel.py` 依据 `tools/rtti.py` 抽取的 MSVC RTTI 生成。
类名、基类、虚表地址、槽位数**全部来自二进制本身**，不是猜测。

- 具名类 / 模板实例总数：949

## 游戏对象模型（`AbstractClass` 子树）

- `AbstractClass` — 24 槽，虚表 `0x007E1F50`
  - `AbstractTypeClass` — 27 槽，虚表 `0x007E2000`
    - `AITriggerTypeClass` — 27 槽，虚表 `0x007E2A50`
    - `CampaignClass` — 27 槽，虚表 `0x007E4A28`
    - `HouseTypeClass` — 27 槽，虚表 `0x007EAB58`
    - `ObjectTypeClass` — 40 槽，虚表 `0x007EF2D8`
      - `AnimTypeClass` — 41 槽，虚表 `0x007E3608`
      - `BulletTypeClass` — 40 槽，虚表 `0x007E4948`
      - `IsometricTileTypeClass` — 40 槽，虚表 `0x007ECC48`
      - `OverlayTypeClass` — 41 槽，虚表 `0x007EF600`
      - `ParticleSystemTypeClass` — 40 槽，虚表 `0x007F00A8`
      - `ParticleTypeClass` — 40 槽，虚表 `0x007F0188`
      - `SmudgeTypeClass` — 41 槽，虚表 `0x007F3528`
      - `TechnoTypeClass` — 48 槽，虚表 `0x007F4ED8`
        - `AircraftTypeClass` — 48 槽，虚表 `0x007E2868`
        - `BuildingTypeClass` — 49 槽，虚表 `0x007E4570`
        - `InfantryTypeClass` — 48 槽，虚表 `0x007EB610`
        - `UnitTypeClass` — 48 槽，虚表 `0x007F6218`
      - `TerrainTypeClass` — 40 槽，虚表 `0x007F5458`
      - `VoxelAnimTypeClass` — 40 槽，虚表 `0x007F6548`
    - `ScriptTypeClass` — 27 槽，虚表 `0x007F1008`
    - `SideClass` — 27 槽，虚表 `0x007F2EC0`
    - `SuperWeaponTypeClass` — 28 槽，虚表 `0x007F4090`
    - `TagTypeClass` — 27 槽，虚表 `0x007F45C4`
    - `TaskForceClass` — 27 槽，虚表 `0x007F4680`
    - `TeamTypeClass` — 27 槽，虚表 `0x007F47D0`
    - `TiberiumClass` — 27 槽，虚表 `0x007F5728`
    - `TriggerTypeClass` — 27 槽，虚表 `0x007F5904`
    - `WarheadTypeClass` — 27 槽，虚表 `0x007F6B30`
    - `WeaponTypeClass` — 27 槽，虚表 `0x007F73B8`
  - `AirstrikeClass` — 24 槽，虚表 `0x007E29A8`
  - `AlphaShapeClass` — 24 槽，虚表 `0x007E32A4`
  - `BombClass` — 24 槽，虚表 `0x007E3D10`
  - `CaptureManagerClass` — 24 槽，虚表 `0x007E4B40`
  - `CellClass` — 24 槽，虚表 `0x007E4EEC`
  - `DiskLaserClass` — 24 槽，虚表 `0x007E5FB8`
  - `EMPulseClass` — 24 槽，虚表 `0x007E87A8`
  - `FactoryClass` — 24 槽，虚表 `0x007E88D0`
  - `FoggedObjectClass` — 25 槽，虚表 `0x007E8B38`
  - `HouseClass` — 24 槽，虚表 `0x007EA8A0`
  - `LightSourceClass` — 24 槽，虚表 `0x007ED028`
  - `NeuronClass` — 24 槽，虚表 `0x007E3DF0`
  - `ObjectClass` — 122 槽，虚表 `0x007EF060`
    - `AnimClass` — 124 槽，虚表 `0x007E3354`
    - `BuildingLightClass` — 122 槽，虚表 `0x007E3AD0`
    - `BulletClass` — 125 槽，虚表 `0x007E46E4`
    - `IsometricTileClass` — 122 槽，虚表 `0x007EC258`
    - `MissionClass` — 157 槽，虚表 `0x007EDCC0`
      - `RadioClass` — 161 槽，虚表 `0x007F0508`
        - `TechnoClass` — 309 槽，虚表 `0x007F4960`
          - `BuildingClass` — 322 槽，虚表 `0x007E3EBC`
          - `FootClass` — 341 槽，虚表 `0x007E8C94`
            - `AircraftClass` — 341 槽，虚表 `0x007E22A4`
            - `InfantryClass` — 343 槽，虚表 `0x007EB058`
            - `UnitClass` — 344 槽，虚表 `0x007F5C70`
    - `OverlayClass` — 122 槽，虚表 `0x007EF3D4`
    - `ParticleClass` — 123 槽，虚表 `0x007EF954`
    - `ParticleSystemClass` — 122 槽，虚表 `0x007EFB9C`
    - `SmudgeClass` — 122 槽，虚表 `0x007F32FC`
    - `TerrainClass` — 122 槽，虚表 `0x007F522C`
    - `VeinholeMonsterClass` — 122 槽，虚表 `0x007F66A8`
    - `VoxelAnimClass` — 122 槽，虚表 `0x007F6318`
    - `WaveClass` — 122 槽，虚表 `0x007F6BF4`
  - `ParasiteClass` — 24 槽，虚表 `0x007EF890`
  - `RadSiteClass` — 24 槽，虚表 `0x007F0810`
  - `ScriptClass` — 24 槽，虚表 `0x007F0F78`
  - `SlaveManagerClass` — 24 槽，虚表 `0x007F31C8`
  - `SpawnManagerClass` — 24 槽，虚表 `0x007F3650`
  - `SuperClass` — 24 槽，虚表 `0x007F3FE8`
  - `Tactical` — 25 槽，虚表 `0x007F4348`
  - `TActionClass` — 24 槽，虚表 `0x007F443C`
  - `TagClass` — 24 槽，虚表 `0x007F44E0`
  - `TeamClass` — 24 槽，虚表 `0x007F4730`
  - `TemporalClass` — 24 槽，虚表 `0x007F5180`
  - `TEventClass` — 24 槽，虚表 `0x007F5578`
  - `TriggerClass` — 24 槽，虚表 `0x007F5858`
  - `TubeClass` — 24 槽，虚表 `0x007F59B0`
  - `WaypointPathClass` — 24 槽，虚表 `0x007F6E70`

## 其余继承树（按根名字排序）

- `_N::?$VectorClass` — 7 槽，虚表 `0x007EAA5C`
  - `_N::?$DynamicVectorClass` — 7 槽，虚表 `0x007EAA7C`
- `Animate` — 8 槽，虚表 `0x007E35A8`
  - `AnimFile` — 8 槽，虚表 `0x007E3584`
- `BaseClass` — 3 槽，虚表 `0x007E3880`
- `BitFont` — 1 槽，虚表 `0x007E3A78`
- `BitText` — 1 槽，虚表 `0x007E3A80`
- `Blitter` — 5 槽，虚表 `0x007E5B88`
  - `E::?$BlitPlain` — 5 槽，虚表 `0x007F7BDC`
  - `E::?$BlitPlainXlat` — 5 槽，虚表 `0x007E5B70`
  - `E::?$BlitTrans` — 5 槽，虚表 `0x007F7C0C`
  - `E::?$BlitTransRemapDest` — 5 槽，虚表 `0x007E5B28`
  - `E::?$BlitTransRemapXlat` — 5 槽，虚表 `0x007E5B10`
  - `E::?$BlitTransXlat` — 5 槽，虚表 `0x007E5B58`
  - `E::?$BlitTransZRemapXlat` — 5 槽，虚表 `0x007E5B40`
  - `G::?$BlitPlain` — 5 槽，虚表 `0x007F7BC4`
  - `G::?$BlitPlainXlat` — 5 槽，虚表 `0x007E5A38`
  - `G::?$BlitPlainXlatAlpha` — 5 槽，虚表 `0x007E57F8`
  - `G::?$BlitPlainXlatZRead` — 5 槽，虚表 `0x007E5990`
  - `G::?$BlitPlainXlatZReadWrite` — 5 槽，虚表 `0x007E58A0`
  - `G::?$BlitTrans` — 5 槽，虚表 `0x007F7BF4`
  - `G::?$BlitTransDarken` — 5 槽，虚表 `0x007E59F0`
  - `G::?$BlitTransDarkenZRead` — 5 槽，虚表 `0x007E5948`
  - `G::?$BlitTransDarkenZReadWrite` — 5 槽，虚表 `0x007E5858`
  - `G::?$BlitTransLucent25` — 5 槽，虚表 `0x007E59A8`
  - `G::?$BlitTransLucent25Alpha` — 5 槽，虚表 `0x007E5780`
  - `G::?$BlitTransLucent25AlphaZRead` — 5 槽，虚表 `0x007E5690`
  - `G::?$BlitTransLucent25AlphaZReadWarp` — 5 槽，虚表 `0x007E5648`
  - `G::?$BlitTransLucent25AlphaZReadWrite` — 5 槽，虚表 `0x007E55D0`
  - `G::?$BlitTransLucent25ZRead` — 5 槽，虚表 `0x007E5900`
  - `G::?$BlitTransLucent25ZReadWarp` — 5 槽，虚表 `0x007E58B8`
  - `G::?$BlitTransLucent25ZReadWrite` — 5 槽，虚表 `0x007E5810`
  - `G::?$BlitTransLucent50` — 5 槽，虚表 `0x007E59C0`
  - `G::?$BlitTransLucent50Alpha` — 5 槽，虚表 `0x007E5798`
  - `G::?$BlitTransLucent50AlphaZRead` — 5 槽，虚表 `0x007E56A8`
  - `G::?$BlitTransLucent50AlphaZReadWarp` — 5 槽，虚表 `0x007E5660`
  - `G::?$BlitTransLucent50AlphaZReadWrite` — 5 槽，虚表 `0x007E55E8`
  - `G::?$BlitTranslucent50NonzeroAlpha` — 5 槽，虚表 `0x007E5720`
  - `G::?$BlitTranslucent50ZeroAlpha` — 5 槽，虚表 `0x007E5708`
  - `G::?$BlitTransLucent50ZRead` — 5 槽，虚表 `0x007E5918`
  - `G::?$BlitTransLucent50ZReadWarp` — 5 槽，虚表 `0x007E58D0`
  - `G::?$BlitTransLucent50ZReadWrite` — 5 槽，虚表 `0x007E5828`
  - `G::?$BlitTransLucent75` — 5 槽，虚表 `0x007E59D8`
  - `G::?$BlitTransLucent75Alpha` — 5 槽，虚表 `0x007E57B0`
  - `G::?$BlitTransLucent75AlphaZRead` — 5 槽，虚表 `0x007E56C0`
  - `G::?$BlitTransLucent75AlphaZReadWarp` — 5 槽，虚表 `0x007E5678`
  - `G::?$BlitTransLucent75AlphaZReadWrite` — 5 槽，虚表 `0x007E5600`
  - `G::?$BlitTransLucent75ZRead` — 5 槽，虚表 `0x007E5930`
  - `G::?$BlitTransLucent75ZReadWarp` — 5 槽，虚表 `0x007E58E8`
  - `G::?$BlitTransLucent75ZReadWrite` — 5 槽，虚表 `0x007E5840`
  - `G::?$BlitTranslucentWriteAlpha` — 5 槽，虚表 `0x007E5738`
  - `G::?$BlitTransXlat` — 5 槽，虚表 `0x007E5A20`
  - `G::?$BlitTransXlatAlpha` — 5 槽，虚表 `0x007E57E0`
  - `G::?$BlitTransXlatAlphaZRead` — 5 槽，虚表 `0x007E56F0`
  - `G::?$BlitTransXlatAlphaZReadWrite` — 5 槽，虚表 `0x007E5630`
  - `G::?$BlitTransXlatMultWriteAlpha` — 5 槽，虚表 `0x007E5750`
  - `G::?$BlitTransXlatWriteAlpha` — 5 槽，虚表 `0x007E5768`
  - `G::?$BlitTransXlatZRead` — 5 槽，虚表 `0x007E5978`
  - `G::?$BlitTransXlatZReadWrite` — 5 槽，虚表 `0x007E5888`
  - `G::?$BlitTransZRemapXlat` — 5 槽，虚表 `0x007E5A08`
  - `G::?$BlitTransZRemapXlatAlpha` — 5 槽，虚表 `0x007E57C8`
  - `G::?$BlitTransZRemapXlatAlphaZRead` — 5 槽，虚表 `0x007E56D8`
  - `G::?$BlitTransZRemapXlatAlphaZReadWrite` — 5 槽，虚表 `0x007E5618`
  - `G::?$BlitTransZRemapXlatZRead` — 5 槽，虚表 `0x007E5960`
  - `G::?$BlitTransZRemapXlatZReadWrite` — 5 槽，虚表 `0x007E5870`
- `BrainClass` — 1 槽，虚表 `0x007E3E74`
- `CampaignEndScoreClass` — 2 槽，虚表 `0x007E4AB8`
- `CampaignScoreClass` — 2 槽，虚表 `0x007E4AAC`
- `CommandClass` — 9 槽，虚表 `0x007EBE3C`
  - `AddTeamCommandClass` — 9 槽，虚表 `0x007EBE8C`
  - `AllianceCommandClass` — 9 槽，虚表 `0x007EBB44`
  - `AllToCheerCommandClass` — 9 槽，虚表 `0x007EBA54`
  - `BeaconPlacementCommandClass` — 9 槽，虚表 `0x007EBBBC`
  - `CenterBaseCommandClass` — 9 槽，虚表 `0x007EBB1C`
  - `CenterREventCommandClass` — 9 槽，虚表 `0x007EBBE4`
  - `CenterTeamCommandClass` — 9 槽，虚表 `0x007EBEB4`
  - `CenterViewCommandClass` — 9 槽，虚表 `0x007EBAF4`
  - `CombatantSelectCommandClass` — 9 槽，虚表 `0x007EB98C`
  - `CreateTeamCommandClass` — 9 槽，虚表 `0x007EB84C`
  - `CursorPositionCommandClass` — 9 槽，虚表 `0x007EBF54`
  - `DeleteCommandClass` — 9 槽，虚表 `0x007EBF7C`
  - `DeployCommandClass` — 9 槽，虚表 `0x007EBA2C`
  - `FollowCommandClass` — 9 槽，虚表 `0x007EBDC4`
  - `GuardCommandClass` — 9 槽，虚表 `0x007EBAA4`
  - `HealthNavCommandClass` — 9 槽，虚表 `0x007EB93C`
  - `MultiplayerDebugCommandClass` — 9 槽，虚表 `0x007EBE14`
  - `MultiplayerSyncCommandClass` — 9 槽，虚表 `0x007EBDEC`
  - `NextObjectCommandClass` — 9 槽，虚表 `0x007EB9DC`
  - `OptionsCommandClass` — 9 槽，虚表 `0x007EBC5C`
  - `PageUserCommandClass` — 9 槽，虚表 `0x007EBF2C`
  - `PlanningModeCommandClass` — 9 槽，虚表 `0x007EB9B4`
  - `PrevObjectCommandClass` — 9 槽，虚表 `0x007EBA04`
  - `ScatterCommandClass` — 9 槽，虚表 `0x007EBACC`
  - `ScreenCaptureCommandClass` — 9 槽，虚表 `0x007EBF04`
  - `SelectTeamCommandClass` — 9 槽，虚表 `0x007EBE64`
  - `SetDefenseTabCommandClass` — 9 槽，虚表 `0x007EB8C4`
  - `SetInfantryTabCommandClass` — 9 槽，虚表 `0x007EB874`
  - `SetStructureTabCommandClass` — 9 槽，虚表 `0x007EB8EC`
  - `SetUnitTabCommandClass` — 9 槽，虚表 `0x007EB89C`
  - `SetView1CommandClass` — 9 槽，虚表 `0x007EBCFC`
  - `SetView2CommandClass` — 9 槽，虚表 `0x007EBCD4`
  - `SetView3CommandClass` — 9 槽，虚表 `0x007EBCAC`
  - `SetView4CommandClass` — 9 槽，虚表 `0x007EBC84`
  - `SidebarDownCommandClass` — 9 槽，虚表 `0x007EBC0C`
  - `SidebarUpCommandClass` — 9 槽，虚表 `0x007EBC34`
  - `StopCommandClass` — 9 槽，虚表 `0x007EBA7C`
  - `TauntCommandClass` — 9 槽，虚表 `0x007EBEDC`
  - `ToggleRepairCommandClass` — 9 槽，虚表 `0x007EBB6C`
  - `ToggleSellCommandClass` — 9 槽，虚表 `0x007EBB94`
  - `TypeSelectCommandClass` — 9 槽，虚表 `0x007EB964`
  - `VeterancyNavCommandClass` — 9 槽，虚表 `0x007EB914`
  - `View1CommandClass` — 9 槽，虚表 `0x007EBD9C`
  - `View2CommandClass` — 9 槽，虚表 `0x007EBD74`
  - `View3CommandClass` — 9 槽，虚表 `0x007EBD4C`
  - `View4CommandClass` — 9 槽，虚表 `0x007EBD24`
- `CommBufferClass` — 1 槽，虚表 `0x007E519C`
- `ConnectionClass` — 10 槽，虚表 `0x007E51B4`
  - `IPXConnClass` — 11 槽，虚表 `0x007EC0CC`
    - `IPXGlobalConnClass` — 18 槽，虚表 `0x007EC10C`
  - `NullModemConnClass` — 10 槽，虚表 `0x007EEF90`
- `ConnManClass` — 16 槽，虚表 `0x007EC1D4`
  - `IPXManagerClass` — 25 槽，虚表 `0x007EC16C`
  - `NullModemClass` — 16 槽，虚表 `0x007EEFDC`
- `ConvertClass` — 1 槽，虚表 `0x007E5358`
  - `LightConvertClass` — 2 槽，虚表 `0x007ED0A4`
- `E::?$VectorClass` — 7 槽，虚表 `0x007F65F4`
- `FileClass` — 17 槽，虚表 `0x007F08BC`
  - `RAMFileClass` — 17 槽，虚表 `0x007F0874`
  - `RawFileClass` — 17 槽，虚表 `0x007F0904`
    - `BufferIOFileClass` — 17 槽，虚表 `0x007E3A2C`
      - `CDFileClass` — 17 槽，虚表 `0x007E1668`
        - `CCFileClass` — 17 槽，虚表 `0x007E16B0`
- `FoggedObjectClass::UDrawRecord::?$VectorClass` — 7 槽，虚表 `0x007E8BC0`
  - `FoggedObjectClass::UDrawRecord::?$DynamicVectorClass` — 7 槽，虚表 `0x007E8BA0`
- `G::?$VectorClass` — 7 槽，虚表 `0x007E3824`
  - `G::?$DynamicVectorClass` — 7 槽，虚表 `0x007E3844`
- `GenericList` — 1 槽，虚表 `0x007E1B04`
  - `INIClass::PAUINIEntry::?$List` — 1 槽，虚表 `0x007EB744`
  - `INIClass::PAUINISection::?$List` — 1 槽，虚表 `0x007E1AFC`
  - `PAVMixFileClass::?$List` — 1 槽，虚表 `0x007EDF38`
- `GenericNode` — 1 槽，虚表 `0x007E1B0C`
  - `INIClass::PAUINISection::?$Node` — 1 槽，虚表 `0x007EB74C`
    - `INIClass::INISection` — 1 槽，虚表 `0x007EB73C`
- `GraphicMenu` — 1 槽，虚表 `0x007EA5FC`
- `GraphicMenuItem` — 6 槽，虚表 `0x007EA690`
  - `GraphicMenuAnimItem` — 6 槽，虚表 `0x007EA658`
  - `GraphicMenuImageItem` — 6 槽，虚表 `0x007EA674`
  - `GraphicMenuShortcutItem` — 6 槽，虚表 `0x007EA6AC`
- `H::?$VectorClass` — 7 槽，虚表 `0x007E4DB8`
  - `CounterClass` — 7 槽，虚表 `0x007E5C54`
  - `H::?$DynamicVectorClass` — 7 槽，虚表 `0x007E4E78`
    - `H::?$TypeList` — 7 槽，虚表 `0x007E4DD8`
- `H::V?$TPoint3D::?$VectorClass` — 7 槽，虚表 `0x007E4638`
- `H::V?$TRect::?$VectorClass` — 7 槽，虚表 `0x007ED970`
  - `H::V?$TRect::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED99C`
- `H::V?$TRect::V?$VectorClass::H::V?$TRect::?$VectorCursor` — 4 槽，虚表 `0x007F71CC`
- `HouseClass::PAUBuildChoiceClass::?$VectorClass` — 7 槽，虚表 `0x007EA7D4`
  - `HouseClass::PAUBuildChoiceClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA7B4`
- `HouseClass::PAUStartingTechnoStruct::?$VectorClass` — 7 槽，虚表 `0x007EA964`
  - `HouseClass::PAUStartingTechnoStruct::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA944`
- `I::?$VectorClass` — 7 槽，虚表 `0x007E37EC`
  - `I::?$DynamicVectorClass` — 7 槽，虚表 `0x007E37CC`
- `I::IV?$DynamicVectorClass::?$VectorCursor` — 4 槽，虚表 `0x007EA6C8`
- `II::U?$HashObject::?$VectorClass` — 7 槽，虚表 `0x007ED5C0`
  - `II::U?$HashObject::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED540`
- `INIClass` — 1 槽，虚表 `0x007EA5F4`
  - `CCINIClass` — 1 槽，虚表 `0x007E1AF4`
- `INoticeSink` — 1 槽，虚表 `0x007E1FBC`
  - `LoadProgressMgr` — 1 槽，虚表 `0x007ECF64`
- `INoticeSource` — 1 槽，虚表 `0x007E1FB4`
  - `ProgressScreenClass` — 1 槽，虚表 `0x007F0064`
- `IsometricTileTypeClass::PAUTileInsertType::?$VectorClass` — 7 槽，虚表 `0x007ECBFC`
  - `IsometricTileTypeClass::PAUTileInsertType::?$DynamicVectorClass` — 7 槽，虚表 `0x007ECBDC`
- `IUSubzoneConnectionStruct::U?$HashObject::?$VectorClass` — 7 槽，虚表 `0x007ED5E0`
  - `IUSubzoneConnectionStruct::U?$HashObject::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED520`
- `K::?$VectorClass` — 7 槽，虚表 `0x007F3748`
  - `K::?$DynamicVectorClass` — 7 槽，虚表 `0x007F3728`
- `LightSourceClass::PAVPendingCellClass::?$VectorClass` — 7 槽，虚表 `0x007ECFDC`
  - `LightSourceClass::PAVPendingCellClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007ECFBC`
- `LinkClass` — 10 槽，虚表 `0x007E9344`
  - `CarryoverClass` — 10 槽，虚表 `0x007E4C04`
  - `GadgetClass` — 33 槽，虚表 `0x007E92BC`
    - `ControlClass` — 34 槽，虚表 `0x007E528C`
      - `Dial8Class` — 34 槽，虚表 `0x007E5E3C`
      - `EditClass` — 39 槽，虚表 `0x007E81A4`
        - `DropListClass` — 46 槽，虚表 `0x007E7FCC`
      - `GaugeClass` — 42 槽，虚表 `0x007E9384`
        - `SliderClass` — 45 槽，虚表 `0x007ED21C`
        - `TriColorGaugeClass` — 44 槽，虚表 `0x007E9430`
      - `ListClass` — 51 槽，虚表 `0x007ED10C`
        - `CheckListClass` — 51 槽，虚表 `0x007E4F84`
        - `ColorListClass` — 53 槽，虚表 `0x007E5054`
      - `SidebarClass::StripClass::SelectClass` — 34 槽，虚表 `0x007F2FCC`
      - `ToggleClass` — 34 槽，虚表 `0x007E8118`
        - `ShapeButtonClass` — 35 槽，虚表 `0x007E8088`
        - `TextButtonClass` — 38 槽，虚表 `0x007F55DC`
    - `DisplayClass::TacticalClass` — 33 槽，虚表 `0x007E608C`
    - `RadarClass::RTacticalClass` — 33 槽，虚表 `0x007F02BC`
    - `SidebarClass::SBGadgetClass` — 33 槽，虚表 `0x007F2F44`
    - `StaticButtonClass` — 36 槽，虚表 `0x007F3EA0`
    - `TextLabelClass` — 34 槽，虚表 `0x007F5B44`
- `LoadOptionsClass` — 9 槽，虚表 `0x007ED2E4`
  - `MapSeedClass` — 9 槽，虚表 `0x007ED8E4`
- `Mouse` — 18 槽，虚表 `0x007F7B78`
  - `WWMouseClass` — 18 槽，虚表 `0x007F7B2C`
- `MovieHandle` — 11 槽，虚表 `0x007EE124`
  - `BinkMovieHandle` — 11 槽，虚表 `0x007EE154`
  - `VQMovieHandle` — 11 槽，虚表 `0x007EE0F4`
- `MSAnim` — 9 槽，虚表 `0x007EE8E8`
  - `MSBinkAnim` — 9 槽，虚表 `0x007EE988`
  - `MSBitPrintAnim` — 9 槽，虚表 `0x007EE9D8`
  - `MSFrameAnim` — 9 槽，虚表 `0x007F7104`
  - `MSPCXAnim` — 9 槽，虚表 `0x007EEA2C`
  - `MSPrintAnim` — 9 槽，虚表 `0x007EEA00`
  - `MSShapeAnim` — 9 槽，虚表 `0x007EE910`
    - `MSFadeAnim` — 9 槽，虚表 `0x007EE938`
      - `MSOverlayAnim` — 9 槽，虚表 `0x007EE960`
  - `MSVQAnim` — 9 槽，虚表 `0x007EE9B0`
  - `WorldDominationTour::Voices::Anim` — 9 槽，虚表 `0x007F7354`
- `MSEngine` — 3 槽，虚表 `0x007EEBD4`
  - `MapSelect` — 3 槽，虚表 `0x007EDB4C`
  - `WorldDominationTour::Selection` — 3 槽，虚表 `0x007F72B4`
- `MSFont` — 5 槽，虚表 `0x007EEC64`
- `MultiplayerGameMode` — 52 槽，虚表 `0x007EED60`
  - `FreeForAll` — 52 槽，虚表 `0x007EE424`
  - `Megawealth` — 52 槽，虚表 `0x007EE5F4`
  - `MPCooperative` — 52 槽，虚表 `0x007EE27C`
  - `MultiplayerBattle` — 52 槽，虚表 `0x007EE184`
  - `MultiplayerManBattle` — 52 槽，虚表 `0x007EE50C`
  - `MultiplayerSiege` — 52 槽，虚表 `0x007EE6FC`
  - `UnholyAlliance` — 52 槽，虚表 `0x007EE814`
- `MultiplayerGameMode::InitializerBase` — 2 槽，虚表 `0x007EEE74`
  - `MultiplayerGameMode::VFreeForAll::?$Initializer` — 2 槽，虚表 `0x007EEE8C`
  - `MultiplayerGameMode::VMPCooperative::?$Initializer` — 2 槽，虚表 `0x007EEE80`
  - `MultiplayerGameMode::VMultiplayerBattle::?$Initializer` — 2 槽，虚表 `0x007EEEBC`
  - `MultiplayerGameMode::VMultiplayerManBattle::?$Initializer` — 2 槽，虚表 `0x007EEEB0`
  - `MultiplayerGameMode::VMultiplayerSiege::?$Initializer` — 2 槽，虚表 `0x007EEEA4`
  - `MultiplayerGameMode::VUnholyAlliance::?$Initializer` — 2 槽，虚表 `0x007EEE98`
- `MultiplayerTeam` — 3 槽，虚表 `0x007EEEDC`
  - `MultiplayerBattleTeam` — 3 槽，虚表 `0x007EE258`
  - `MultiplayerObserverTeam` — 3 槽，虚表 `0x007EE6C8`
  - `MultiplayerSiegeAttackerTeam` — 3 槽，虚表 `0x007EE7F4`
  - `MultiplayerSiegeDefenderTeam` — 3 槽，虚表 `0x007EE7E4`
- `N::?$VectorClass` — 7 槽，虚表 `0x007EDA4C`
  - `N::?$DynamicVectorClass` — 7 槽，虚表 `0x007EDA6C`
- `OwnerDraw::DialogControl` — 5 槽，虚表 `0x007EF720`
  - `OwnerDraw::SimpleDialogControl` — 5 槽，虚表 `0x007EF738`
    - `SimpleWonlineDialogControl` — 5 槽，虚表 `0x007F7624`
      - `WonlineStringDialogControl` — 5 槽，虚表 `0x007F7874`
        - `CreateGameDialogControl` — 5 槽，虚表 `0x007F788C`
    - `WorldDominationTour::FactionSelectDialogControl` — 5 槽，虚表 `0x007F791C`
- `OwnerTalkClass::PAUConnectionListStruct::?$VectorClass` — 7 槽，虚表 `0x007F0C4C`
  - `OwnerTalkClass::PAUConnectionListStruct::?$DynamicVectorClass` — 7 槽，虚表 `0x007F0C2C`
- `PAD::?$VectorClass` — 7 槽，虚表 `0x007E5C24`
  - `PAD::?$DynamicVectorClass` — 7 槽，虚表 `0x007E5BC4`
- `PAD::PAV?$DynamicVectorClass::?$VectorClass` — 7 槽，虚表 `0x007E5C04`
  - `PAD::PAV?$DynamicVectorClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E5BE4`
- `PAE::?$VectorClass` — 7 槽，虚表 `0x007F7B0C`
  - `PAE::?$DynamicVectorClass` — 7 槽，虚表 `0x007F7AEC`
- `PAG::?$VectorClass` — 7 槽，虚表 `0x007ECD0C`
  - `PAG::?$DynamicVectorClass` — 7 槽，虚表 `0x007ECCEC`
- `PAU_DDSURFACEDESC::?$VectorClass` — 7 槽，虚表 `0x007E5DEC`
  - `PAU_DDSURFACEDESC::?$DynamicVectorClass` — 7 槽，虚表 `0x007E5E0C`
- `PAU_WIN32_FIND_DATAA::?$VectorClass` — 7 槽，虚表 `0x007ED92C`
  - `PAU_WIN32_FIND_DATAA::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED94C`
- `PAUButtonFadeEffect::?$VectorClass` — 7 槽，虚表 `0x007E8500`
  - `PAUButtonFadeEffect::?$DynamicVectorClass` — 7 槽，虚表 `0x007E856C`
- `PAUControlNode::?$VectorClass` — 7 槽，虚表 `0x007E4BC4`
  - `PAUControlNode::?$DynamicVectorClass` — 7 槽，虚表 `0x007E4BA4`
- `PAUCrossDissolveEffect::?$VectorClass` — 7 槽，虚表 `0x007E8520`
  - `PAUCrossDissolveEffect::?$DynamicVectorClass` — 7 槽，虚表 `0x007E854C`
- `PAUDamageGroup::?$VectorClass` — 7 槽，虚表 `0x007E5144`
  - `PAUDamageGroup::?$DynamicVectorClass` — 7 槽，虚表 `0x007E5170`
- `PAUGlobalPacketType::?$VectorClass` — 7 槽，虚表 `0x007F1234`
  - `PAUGlobalPacketType::?$DynamicVectorClass` — 7 槽，虚表 `0x007F11D4`
- `PAUHWND__::?$VectorClass` — 7 槽，虚表 `0x007EECAC`
  - `PAUHWND__::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEC8C`
- `PAUIConnectionPoint::?$VectorClass` — 7 槽，虚表 `0x007E5D08`
  - `PAUIConnectionPoint::?$DynamicVectorClass` — 7 槽，虚表 `0x007E5D48`
- `PAUKamikazeControl::?$VectorClass` — 7 槽，虚表 `0x007ECE9C`
  - `PAUKamikazeControl::?$DynamicVectorClass` — 7 槽，虚表 `0x007ECE7C`
- `PAUMPlayerScoreType::?$VectorClass` — 7 槽，虚表 `0x007EE3D0`
  - `PAUMPlayerScoreType::?$DynamicVectorClass` — 7 槽，虚表 `0x007EE3F0`
- `PAUNodeNameType::?$VectorClass` — 7 槽，虚表 `0x007EE390`
  - `PAUNodeNameType::?$DynamicVectorClass` — 7 槽，虚表 `0x007EE370`
- `PAUtConnInfoStruct::?$VectorClass` — 7 槽，虚表 `0x007F78A4`
  - `PAUtConnInfoStruct::?$DynamicVectorClass` — 7 槽，虚表 `0x007F78C4`
- `PAUThemeControl::?$VectorClass` — 7 槽，虚表 `0x007EA584`
  - `PAUThemeControl::?$DynamicVectorClass` — 7 槽，虚表 `0x007F568C`
- `PAVAbstractClass::?$VectorClass` — 7 槽，虚表 `0x007E920C`
  - `PAVAbstractClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E91EC`
- `PAVAbstractTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA544`
  - `PAVAbstractTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA524`
- `PAVAircraftClass::?$VectorClass` — 7 槽，虚表 `0x007E9E84`
  - `PAVAircraftClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9E64`
- `PAVAircraftTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA284`
  - `PAVAircraftTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA264`
- `PAVAirstrikeClass::?$VectorClass` — 7 槽，虚表 `0x007E295C`
  - `PAVAirstrikeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E293C`
- `PAVAITriggerTypeClass::?$VectorClass` — 7 槽，虚表 `0x007E9B84`
  - `PAVAITriggerTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9B64`
- `PAVAlphaLightingRemapClass::?$VectorClass` — 7 槽，虚表 `0x007E2AF0`
  - `PAVAlphaLightingRemapClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E2AD0`
- `PAVAlphaShapeClass::?$VectorClass` — 7 槽，虚表 `0x007E3258`
  - `PAVAlphaShapeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E3238`
- `PAVAnimClass::?$VectorClass` — 7 槽，虚表 `0x007E9F44`
  - `PAVAnimClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9F24`
- `PAVAnimTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA304`
  - `PAVAnimTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA2E4`
- `PAVBombClass::?$VectorClass` — 7 槽，虚表 `0x007E17EC`
  - `PAVBombClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E17CC`
- `PAVBuildingClass::?$VectorClass` — 7 槽，虚表 `0x007E9E44`
  - `PAVBuildingClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9E24`
- `PAVBuildingLightClass::?$VectorClass` — 7 槽，虚表 `0x007E9C44`
  - `PAVBuildingLightClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9C24`
- `PAVBuildingTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA244`
  - `PAVBuildingTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA224`
- `PAVBulletClass::?$VectorClass` — 7 槽，虚表 `0x007E4698`
  - `PAVBulletClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E4678`
- `PAVBulletTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA384`
  - `PAVBulletTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA364`
- `PAVCampaignClass::?$VectorClass` — 7 槽，虚表 `0x007EA004`
  - `PAVCampaignClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9FE4`
- `PAVCaptureManagerClass::?$VectorClass` — 7 槽，虚表 `0x007E4AF4`
  - `PAVCaptureManagerClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E4AD4`
- `PAVCCINIClass::?$VectorClass` — 7 槽，虚表 `0x007EB80C`
  - `PAVCCINIClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EB82C`
- `PAVCellClass::?$VectorClass` — 7 槽，虚表 `0x007ED480`
  - `PAVCellClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED9BC`
- `PAVColorScheme::?$VectorClass` — 7 槽，虚表 `0x007EF7B0`
  - `PAVColorScheme::?$DynamicVectorClass` — 7 槽，虚表 `0x007EF790`
- `PAVConvertClass::?$VectorClass` — 7 槽，虚表 `0x007E5338`
  - `PAVConvertClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E5318`
- `PAVCoopCampaignClass::?$VectorClass` — 7 槽，虚表 `0x007EE3B0`
  - `PAVCoopCampaignClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EE350`
- `PAVDiskLaserClass::?$VectorClass` — 7 槽，虚表 `0x007E5EFC`
  - `PAVDiskLaserClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E5EDC`
- `PAVEBolt::?$VectorClass` — 7 槽，虚表 `0x007E86AC`
  - `PAVEBolt::?$DynamicVectorClass` — 7 槽，虚表 `0x007E868C`
- `PAVEgoClass::?$VectorClass` — 7 槽，虚表 `0x007E86FC`
  - `PAVEgoClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E86DC`
- `PAVEMPulseClass::?$VectorClass` — 7 槽，虚表 `0x007E875C`
  - `PAVEMPulseClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E873C`
- `PAVEventClass::?$VectorClass` — 7 槽，虚表 `0x007EFE24`
  - `PAVEventClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EFE04`
- `PAVFactoryClass::?$VectorClass` — 7 槽，虚表 `0x007E9FC4`
  - `PAVFactoryClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9FA4`
- `PAVFileEntryClass::?$VectorClass` — 7 槽，虚表 `0x007ED32C`
  - `PAVFileEntryClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED30C`
- `PAVFoggedObjectClass::?$VectorClass` — 7 槽，虚表 `0x007E4514`
  - `PAVFoggedObjectClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E44F4`
- `PAVFootClass::?$VectorClass` — 7 槽，虚表 `0x007E8C48`
  - `PAVFootClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E8C28`
- `PAVGraphicMenuItem::?$VectorClass` — 7 槽，虚表 `0x007EA624`
  - `PAVGraphicMenuItem::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA604`
- `PAVGraphicMenuItem::V?$DynamicVectorClass::PAVGraphicMenuItem::?$VectorCursor` — 4 槽，虚表 `0x007EA644`
- `PAVHouseClass::?$VectorClass` — 7 槽，虚表 `0x007E9F04`
  - `PAVHouseClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9EE4`
- `PAVHouseTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA084`
  - `PAVHouseTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA064`
- `PAVInfantryClass::?$VectorClass` — 7 槽，虚表 `0x007E43E8`
  - `PAVInfantryClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E43C8`
- `PAVInfantryTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA344`
  - `PAVInfantryTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA324`
- `PAVIonBlastClass::?$VectorClass` — 7 槽，虚表 `0x007EC07C`
  - `PAVIonBlastClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EC05C`
- `PAVIsometricTileClass::?$VectorClass` — 7 槽，虚表 `0x007E18DC`
  - `PAVIsometricTileClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E18BC`
- `PAVIsometricTileTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA404`
  - `PAVIsometricTileTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA3E4`
- `PAVLaserDrawClass::?$VectorClass` — 7 槽，虚表 `0x007ECEFC`
  - `PAVLaserDrawClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007ECEDC`
- `PAVLightConvertClass::?$VectorClass` — 7 槽，虚表 `0x007E188C`
  - `PAVLightConvertClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E186C`
- `PAVLightSourceClass::?$VectorClass` — 7 槽，虚表 `0x007ECF9C`
  - `PAVLightSourceClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007ECF7C`
- `PAVLineTrail::?$VectorClass` — 7 槽，虚表 `0x007ED0EC`
  - `PAVLineTrail::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED0CC`
- `PAVMapRegionClass::?$VectorClass` — 7 槽，虚表 `0x007ED878`
  - `PAVMapRegionClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED858`
- `PAVMapSelection::?$VectorClass` — 7 槽，虚表 `0x007EEBB4`
  - `PAVMapSelection::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEB14`
- `PAVMapStage::?$VectorClass` — 7 槽，虚表 `0x007EEAB4`
  - `PAVMapStage::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEA94`
- `PAVMixFileClass::?$VectorClass` — 7 槽，虚表 `0x007E1A64`
  - `PAVMixFileClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E1A44`
- `PAVMovieHandle::?$VectorClass` — 7 槽，虚表 `0x007F69A4`
  - `PAVMovieHandle::?$DynamicVectorClass` — 7 槽，虚表 `0x007F6984`
- `PAVMSAnim::?$VectorClass` — 7 槽，虚表 `0x007EEC24`
  - `PAVMSAnim::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEC04`
- `PAVMSAnim::V?$DynamicVectorClass::PAVMSAnim::?$VectorCursor` — 4 槽，虚表 `0x007F72EC`
- `PAVMSAnimEntry::?$VectorClass` — 7 槽，虚表 `0x007EEAD4`
  - `PAVMSAnimEntry::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEA74`
- `PAVMSSfx::?$VectorClass` — 7 槽，虚表 `0x007EEC44`
  - `PAVMSSfx::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEBE4`
- `PAVMSSfxEntry::?$VectorClass` — 7 槽，虚表 `0x007EEAF4`
  - `PAVMSSfxEntry::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEA54`
- `PAVMSSfxEntry::V?$DynamicVectorClass::PAVMSSfxEntry::?$VectorCursor` — 4 槽，虚表 `0x007F72C4`
- `PAVMSTextEntry::?$VectorClass` — 7 槽，虚表 `0x007EEB94`
  - `PAVMSTextEntry::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEB34`
- `PAVMultiMission::?$VectorClass` — 7 槽，虚表 `0x007F1214`
  - `PAVMultiMission::?$DynamicVectorClass` — 7 槽，虚表 `0x007F11F4`
- `PAVMultiplayerGameMode::?$VectorClass` — 7 槽，虚表 `0x007EED40`
  - `PAVMultiplayerGameMode::?$DynamicVectorClass` — 7 槽，虚表 `0x007EED20`
- `PAVMultiplayerTeam::?$VectorClass` — 7 槽，虚表 `0x007EEE54`
  - `PAVMultiplayerTeam::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEE34`
- `PAVNeuronClass::?$VectorClass` — 7 槽，虚表 `0x007E3E54`
- `PAVObjectClass::?$VectorClass` — 7 槽，虚表 `0x007E192C`
  - `PAVObjectClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E4F64`
    - `LayerClass` — 10 槽，虚表 `0x007E6060`
      - `LogicClass` — 11 槽，虚表 `0x007E18FC`
- `PAVObjectTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EF28C`
  - `PAVObjectTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EF26C`
- `PAVOverlayClass::?$VectorClass` — 7 槽，虚表 `0x007E9D44`
  - `PAVOverlayClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9D24`
- `PAVOverlayTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA184`
  - `PAVOverlayTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA164`
- `PAVParasiteClass::?$VectorClass` — 7 槽，虚表 `0x007EF844`
  - `PAVParasiteClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EF824`
- `PAVParticleClass::?$VectorClass` — 7 槽，虚表 `0x007E9D84`
  - `PAVParticleClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9D64`
- `PAVParticleSystemClass::?$VectorClass` — 7 槽，虚表 `0x007E9C84`
  - `PAVParticleSystemClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9C64`
- `PAVParticleSystemTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA484`
  - `PAVParticleSystemTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA464`
- `PAVParticleTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA444`
  - `PAVParticleTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA424`
- `PAVPhoneEntryClass::?$VectorClass` — 7 槽，虚表 `0x007F1254`
  - `PAVPhoneEntryClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F11B4`
- `PAVPlanningBranchClass::?$VectorClass` — 7 槽，虚表 `0x007EFF24`
  - `PAVPlanningBranchClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EFEC4`
- `PAVPlanningMemberClass::?$VectorClass` — 7 槽，虚表 `0x007EFF04`
  - `PAVPlanningMemberClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EFEE4`
- `PAVPlanningNodeClass::?$VectorClass` — 7 槽，虚表 `0x007EFE64`
  - `PAVPlanningNodeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EFE44`
- `PAVPlanningTokenClass::?$VectorClass` — 7 槽，虚表 `0x007EFEA4`
  - `PAVPlanningTokenClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EFE84`
- `PAVRadarEventClass::?$VectorClass` — 7 槽，虚表 `0x007F0ACC`
  - `PAVRadarEventClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F0AAC`
- `PAVRadBeam::?$VectorClass` — 7 槽，虚表 `0x007F04A4`
  - `PAVRadBeam::?$DynamicVectorClass` — 7 槽，虚表 `0x007F0484`
- `PAVRadSiteClass::?$VectorClass` — 7 槽，虚表 `0x007F07C4`
  - `PAVRadSiteClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F07A4`
- `PAVReestablish::?$VectorClass` — 7 槽，虚表 `0x007E4468`
  - `PAVReestablish::?$DynamicVectorClass` — 7 槽，虚表 `0x007E4488`
- `PAVSchemeNode::VHashString::U?$HashObject::?$VectorClass` — 7 槽，虚表 `0x007EF7D0`
  - `PAVSchemeNode::VHashString::U?$HashObject::?$DynamicVectorClass` — 7 槽，虚表 `0x007EF770`
- `PAVScriptClass::?$VectorClass` — 7 槽，虚表 `0x007E1B44`
  - `PAVScriptClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E1B24`
- `PAVScriptTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA144`
  - `PAVScriptTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA124`
- `PAVShadowControlClass::?$VectorClass` — 7 槽，虚表 `0x007F42FC`
  - `PAVShadowControlClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F42DC`
- `PAVSideClass::?$VectorClass` — 7 槽，虚表 `0x007EA044`
  - `PAVSideClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA024`
- `PAVSlaveManagerClass::?$VectorClass` — 7 槽，虚表 `0x007F317C`
  - `PAVSlaveManagerClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F315C`
- `PAVSmudgeClass::?$VectorClass` — 7 槽，虚表 `0x007E9DC4`
  - `PAVSmudgeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9DA4`
- `PAVSmudgeTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA1C4`
  - `PAVSmudgeTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA1A4`
- `PAVSpawnManagerClass::?$VectorClass` — 7 槽，虚表 `0x007F3604`
  - `PAVSpawnManagerClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F35E4`
- `PAVSpotLightClass::?$VectorClass` — 7 槽，虚表 `0x007EF6DC`
  - `PAVSpotLightClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EF6BC`
- `PAVSubTitle::?$VectorClass` — 7 槽，虚表 `0x007F3F8C`
  - `PAVSubTitle::?$DynamicVectorClass` — 7 槽，虚表 `0x007F3F6C`
- `PAVSuperClass::?$VectorClass` — 7 槽，虚表 `0x007EA504`
  - `PAVSuperClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA4E4`
- `PAVSuperWeaponTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA4C4`
  - `PAVSuperWeaponTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA4A4`
- `PAVTActionClass::?$VectorClass` — 7 槽，虚表 `0x007F43F0`
  - `PAVTActionClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F43D0`
- `PAVTagClass::?$VectorClass` — 7 槽，虚表 `0x007EA5C4`
  - `PAVTagClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA5A4`
- `PAVTagTypeClass::?$VectorClass` — 7 槽，虚表 `0x007F4578`
  - `PAVTagTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F4558`
- `PAVTaskForceClass::?$VectorClass` — 7 槽，虚表 `0x007EA0C4`
  - `PAVTaskForceClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA0A4`
- `PAVTeamClass::?$VectorClass` — 7 槽，虚表 `0x007E9F84`
  - `PAVTeamClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9F64`
- `PAVTeamTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA104`
  - `PAVTeamTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA0E4`
- `PAVTechnoClass::?$VectorClass` — 7 槽，虚表 `0x007E180C`
  - `PAVTechnoClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E17AC`
- `PAVTechnoClass::URadarTrackingStruct::U?$HashObject::?$VectorClass` — 7 槽，虚表 `0x007F044C`
  - `PAVTechnoClass::URadarTrackingStruct::U?$HashObject::?$DynamicVectorClass` — 7 槽，虚表 `0x007F042C`
- `PAVTechnoTypeClass::?$VectorClass` — 7 槽，虚表 `0x007E4DF8`
  - `PAVTechnoTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E858C`
    - `PAVTechnoTypeClass::?$TypeList` — 7 槽，虚表 `0x007E4E18`
- `PAVTemporalClass::?$VectorClass` — 7 槽，虚表 `0x007F5134`
  - `PAVTemporalClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F5114`
- `PAVTerrainClass::?$VectorClass` — 7 槽，虚表 `0x007E9E04`
  - `PAVTerrainClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9DE4`
- `PAVTerrainTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA204`
  - `PAVTerrainTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA1E4`
- `PAVTEventClass::?$VectorClass` — 7 槽，虚表 `0x007F552C`
  - `PAVTEventClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F550C`
- `PAVTiberiumClass::?$VectorClass` — 7 槽，虚表 `0x007F56DC`
  - `PAVTiberiumClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F56BC`
- `PAVTriggerClass::?$VectorClass` — 7 槽，虚表 `0x007E9C04`
  - `PAVTriggerClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9BE4`
- `PAVTriggerTypeClass::?$VectorClass` — 7 槽，虚表 `0x007E9BC4`
  - `PAVTriggerTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9BA4`
- `PAVTubeClass::?$VectorClass` — 7 槽，虚表 `0x007E9CC4`
  - `PAVTubeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9CA4`
- `PAVUnitClass::?$VectorClass` — 7 槽，虚表 `0x007E9EC4`
  - `PAVUnitClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9EA4`
- `PAVUnitTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA2C4`
  - `PAVUnitTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA2A4`
- `PAVVeinholeMonsterClass::?$VectorClass` — 7 槽，虚表 `0x007F665C`
  - `PAVVeinholeMonsterClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F663C`
- `PAVVocClass::?$VectorClass` — 7 槽，虚表 `0x007F68CC`
  - `PAVVocClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F68AC`
- `PAVVoxClass::?$VectorClass` — 7 槽，虚表 `0x007F6924`
  - `PAVVoxClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F6904`
- `PAVVoxelAnimClass::?$VectorClass` — 7 槽，虚表 `0x007E1E4C`
  - `PAVVoxelAnimClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E1E2C`
- `PAVVoxelAnimTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA3C4`
  - `PAVVoxelAnimTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA3A4`
- `PAVWarheadTypeClass::?$VectorClass` — 7 槽，虚表 `0x007E1EA4`
  - `PAVWarheadTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E1E84`
- `PAVWaveClass::?$VectorClass` — 7 槽，虚表 `0x007E9D04`
  - `PAVWaveClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E9CE4`
- `PAVWaypointPathClass::?$VectorClass` — 7 槽，虚表 `0x007F6E24`
  - `PAVWaypointPathClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F6E04`
- `PAVWeaponTypeClass::?$VectorClass` — 7 槽，虚表 `0x007E1EF4`
  - `PAVWeaponTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E1ED4`
- `PBD::?$VectorClass` — 7 槽，虚表 `0x007EE0D4`
  - `PBD::?$DynamicVectorClass` — 7 槽，虚表 `0x007EE0B4`
- `PBG::?$VectorClass` — 7 槽，虚表 `0x007ED1FC`
  - `PBG::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED1DC`
- `PBVAircraftTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EAC68`
  - `PBVAircraftTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EACC8`
    - `PBVAircraftTypeClass::?$TypeList` — 7 槽，虚表 `0x007EABC8`
- `PBVAnimClass::?$VectorClass` — 7 槽，虚表 `0x007EBFEC`
  - `PBVAnimClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EBFCC`
- `PBVAnimTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EB6F4`
  - `PBVAnimTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EB714`
    - `PBVAnimTypeClass::?$TypeList` — 7 槽，虚表 `0x007EB6D4`
- `PBVBuildingTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EAA08`
  - `PBVBuildingTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EAA28`
    - `PBVBuildingTypeClass::?$TypeList` — 7 槽，虚表 `0x007ED90C`
- `PBVCommandClass::?$VectorClass` — 7 槽，虚表 `0x007E184C`
  - `PBVCommandClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E182C`
- `PBVInfantryTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EAC28`
  - `PBVInfantryTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EAC88`
    - `PBVInfantryTypeClass::?$TypeList` — 7 槽，虚表 `0x007EAC08`
- `PBVMultiMission::?$VectorClass` — 7 槽，虚表 `0x007EEF50`
  - `PBVMultiMission::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEF70`
- `PBVParticleSystemTypeClass::?$VectorClass` — 7 槽，虚表 `0x007E4424`
  - `PBVParticleSystemTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E4444`
    - `PBVParticleSystemTypeClass::?$TypeList` — 7 槽，虚表 `0x007F4F9C`
- `PBVSmudgeTypeClass::?$VectorClass` — 7 槽，虚表 `0x007F0D7C`
  - `PBVSmudgeTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F0DEC`
    - `PBVSmudgeTypeClass::?$TypeList` — 7 槽，虚表 `0x007F0D1C`
- `PBVTeamTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EA9E4`
  - `PBVTeamTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EAAE8`
    - `PBVTeamTypeClass::?$TypeList` — 7 槽，虚表 `0x007EA9C4`
- `PBVTechnoTypeClass::?$VectorClass` — 7 槽，虚表 `0x007E8954`
  - `PBVTechnoTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E8934`
- `PBVTerrainTypeClass::?$VectorClass` — 7 槽，虚表 `0x007F0D9C`
  - `PBVTerrainTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F0E0C`
    - `PBVTerrainTypeClass::?$TypeList` — 7 槽，虚表 `0x007F0CFC`
- `PBVToolTip::?$VectorClass` — 7 槽，虚表 `0x007F57E8`
  - `PBVToolTip::?$DynamicVectorClass` — 7 槽，虚表 `0x007F57C8`
- `PBVUnitTypeClass::?$VectorClass` — 7 槽，虚表 `0x007EAC48`
  - `PBVUnitTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EACA8`
    - `PBVUnitTypeClass::?$TypeList` — 7 槽，虚表 `0x007EABE8`
- `PBVVoxelAnimTypeClass::?$VectorClass` — 7 槽，虚表 `0x007F0D5C`
  - `PBVVoxelAnimTypeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F0DCC`
    - `PBVVoxelAnimTypeClass::?$TypeList` — 7 槽，虚表 `0x007F0D3C`
- `Pipe` — 5 槽，虚表 `0x007E6218`
  - `Base64Pipe` — 5 槽，虚表 `0x007EB774`
  - `BlowPipe` — 5 槽，虚表 `0x007EFDC8`
  - `BufferPipe` — 5 槽，虚表 `0x007E6200`
  - `FilePipe` — 5 槽，虚表 `0x007E4DA0`
  - `LCWPipe` — 5 槽，虚表 `0x007ECF2C`
  - `LZOPipe` — 5 槽，虚表 `0x007ED37C`
  - `PKPipe` — 6 槽，虚表 `0x007EFDAC`
  - `SHAPipe` — 5 槽，虚表 `0x007E4D78`
- `PixelFXClass` — 1 槽，虚表 `0x007EFDA4`
- `rc_ptr_base` — 1 槽，虚表 `0x007F094C`
  - `VPlayerProfile::?$rc_ptr` — 1 槽，虚表 `0x007F3F44`
  - `VWDTState::?$rc_ptr` — 1 槽，虚表 `0x007F70EC`
  - `VWDTTerritory::?$rc_ptr` — 1 槽，虚表 `0x007EBFA4`
  - `WorldDominationTour::E::V?$ValueGameOption::?$rc_ptr` — 1 槽，虚表 `0x007F701C`
  - `WorldDominationTour::VCampaign::?$rc_ptr` — 1 槽，虚表 `0x007F6F24`
  - `WorldDominationTour::VCampaignProperties::?$rc_ptr` — 1 槽，虚表 `0x007F6F6C`
  - `WorldDominationTour::VConflict::?$rc_ptr` — 1 槽，虚表 `0x007F6F34`
  - `WorldDominationTour::VFlagGameOption::?$rc_ptr` — 1 槽，虚表 `0x007F7014`
  - `WorldDominationTour::VGameOption::?$rc_ptr` — 1 槽，虚表 `0x007F7024`
  - `WorldDominationTour::VHistory::?$rc_ptr` — 1 槽，虚表 `0x007F6F74`
  - `WorldDominationTour::VMap::?$rc_ptr` — 1 槽，虚表 `0x007F712C`
  - `WorldDominationTour::VMapSizeGameOption::?$rc_ptr` — 1 槽，虚表 `0x007F7040`
  - `WorldDominationTour::VState::?$rc_ptr` — 1 槽，虚表 `0x007F6F2C`
  - `WorldDominationTour::VTerritory::?$rc_ptr` — 1 槽，虚表 `0x007F71C4`
- `ReferenceCounted` — 3 槽，虚表 `0x007F0954`
  - `PlayerProfile` — 3 槽，虚表 `0x007F74F4`
  - `WDTState` — 3 槽，虚表 `0x007F7250`
  - `WDTTerritory` — 3 槽，虚表 `0x007F7220`
  - `WorldDominationTour::Campaign` — 3 槽，虚表 `0x007F6F3C`
  - `WorldDominationTour::CampaignProperties` — 3 槽，虚表 `0x007F7294`
  - `WorldDominationTour::Conflict` — 3 槽，虚表 `0x007F6FC4`
  - `WorldDominationTour::GameOption` — 5 槽，虚表 `0x007F7060`
    - `WorldDominationTour::E::?$ValueGameOption` — 5 槽，虚表 `0x007F7048`
    - `WorldDominationTour::FlagGameOption` — 5 槽，虚表 `0x007F709C`
    - `WorldDominationTour::MapSizeGameOption` — 5 槽，虚表 `0x007F70B4`
  - `WorldDominationTour::History` — 3 槽，虚表 `0x007F70DC`
  - `WorldDominationTour::Map` — 3 槽，虚表 `0x007F7134`
  - `WorldDominationTour::State` — 3 槽，虚表 `0x007F7314`
  - `WorldDominationTour::Territory` — 3 槽，虚表 `0x007F7334`
- `RLEBlitter` — 3 槽，虚表 `0x007E5BA0`
  - `E::?$RLEBlitTransRemapDest` — 3 槽，虚表 `0x007E5AE0`
  - `E::?$RLEBlitTransRemapDestZRead` — 3 槽，虚表 `0x007E5AA0`
  - `E::?$RLEBlitTransRemapDestZReadWrite` — 3 槽，虚表 `0x007E5A60`
  - `E::?$RLEBlitTransRemapXlat` — 3 槽，虚表 `0x007E5AD0`
  - `E::?$RLEBlitTransRemapXlatZRead` — 3 槽，虚表 `0x007E5A90`
  - `E::?$RLEBlitTransRemapXlatZReadWrite` — 3 槽，虚表 `0x007E5A50`
  - `E::?$RLEBlitTransXlat` — 3 槽，虚表 `0x007E5B00`
  - `E::?$RLEBlitTransXlatZRead` — 3 槽，虚表 `0x007E5AC0`
  - `E::?$RLEBlitTransXlatZReadWrite` — 3 槽，虚表 `0x007E5A80`
  - `E::?$RLEBlitTransZRemapXlat` — 3 槽，虚表 `0x007E5AF0`
  - `E::?$RLEBlitTransZRemapXlatZRead` — 3 槽，虚表 `0x007E5AB0`
  - `E::?$RLEBlitTransZRemapXlatZReadWrite` — 3 槽，虚表 `0x007E5A70`
  - `G::?$RLEBlitTransDarken` — 3 槽，虚表 `0x007E55A0`
  - `G::?$RLEBlitTransDarkenZRead` — 3 槽，虚表 `0x007E5540`
  - `G::?$RLEBlitTransDarkenZReadWrite` — 3 槽，虚表 `0x007E54B0`
  - `G::?$RLEBlitTransLucent25` — 3 槽，虚表 `0x007E5570`
  - `G::?$RLEBlitTransLucent25Alpha` — 3 槽，虚表 `0x007E5430`
  - `G::?$RLEBlitTransLucent25AlphaZRead` — 3 槽，虚表 `0x007E53E0`
  - `G::?$RLEBlitTransLucent25AlphaZReadWarp` — 3 槽，虚表 `0x007E53B0`
  - `G::?$RLEBlitTransLucent25AlphaZReadWrite` — 3 槽，虚表 `0x007E5360`
  - `G::?$RLEBlitTransLucent25ZRead` — 3 槽，虚表 `0x007E5510`
  - `G::?$RLEBlitTransLucent25ZReadWarp` — 3 槽，虚表 `0x007E54E0`
  - `G::?$RLEBlitTransLucent25ZReadWrite` — 3 槽，虚表 `0x007E5480`
  - `G::?$RLEBlitTransLucent50` — 3 槽，虚表 `0x007E5580`
  - `G::?$RLEBlitTransLucent50Alpha` — 3 槽，虚表 `0x007E5440`
  - `G::?$RLEBlitTransLucent50AlphaZRead` — 3 槽，虚表 `0x007E53F0`
  - `G::?$RLEBlitTransLucent50AlphaZReadWarp` — 3 槽，虚表 `0x007E53C0`
  - `G::?$RLEBlitTransLucent50AlphaZReadWrite` — 3 槽，虚表 `0x007E5370`
  - `G::?$RLEBlitTransLucent50ZRead` — 3 槽，虚表 `0x007E5520`
  - `G::?$RLEBlitTransLucent50ZReadWarp` — 3 槽，虚表 `0x007E54F0`
  - `G::?$RLEBlitTransLucent50ZReadWrite` — 3 槽，虚表 `0x007E5490`
  - `G::?$RLEBlitTransLucent75` — 3 槽，虚表 `0x007E5590`
  - `G::?$RLEBlitTransLucent75Alpha` — 3 槽，虚表 `0x007E5450`
  - `G::?$RLEBlitTransLucent75AlphaZRead` — 3 槽，虚表 `0x007E5400`
  - `G::?$RLEBlitTransLucent75AlphaZReadWarp` — 3 槽，虚表 `0x007E53D0`
  - `G::?$RLEBlitTransLucent75AlphaZReadWrite` — 3 槽，虚表 `0x007E5380`
  - `G::?$RLEBlitTransLucent75ZRead` — 3 槽，虚表 `0x007E5530`
  - `G::?$RLEBlitTransLucent75ZReadWarp` — 3 槽，虚表 `0x007E5500`
  - `G::?$RLEBlitTransLucent75ZReadWrite` — 3 槽，虚表 `0x007E54A0`
  - `G::?$RLEBlitTransXlat` — 3 槽，虚表 `0x007E55C0`
  - `G::?$RLEBlitTransXlatAlpha` — 3 槽，虚表 `0x007E5470`
  - `G::?$RLEBlitTransXlatAlphaZRead` — 3 槽，虚表 `0x007E5420`
  - `G::?$RLEBlitTransXlatAlphaZReadWrite` — 3 槽，虚表 `0x007E53A0`
  - `G::?$RLEBlitTransXlatZRead` — 3 槽，虚表 `0x007E5560`
  - `G::?$RLEBlitTransXlatZReadWrite` — 3 槽，虚表 `0x007E54D0`
  - `G::?$RLEBlitTransZRemapXlat` — 3 槽，虚表 `0x007E55B0`
  - `G::?$RLEBlitTransZRemapXlatAlpha` — 3 槽，虚表 `0x007E5460`
  - `G::?$RLEBlitTransZRemapXlatAlphaZRead` — 3 槽，虚表 `0x007E5410`
  - `G::?$RLEBlitTransZRemapXlatAlphaZReadWrite` — 3 槽，虚表 `0x007E5390`
  - `G::?$RLEBlitTransZRemapXlatZRead` — 3 槽，虚表 `0x007E5550`
  - `G::?$RLEBlitTransZRemapXlatZReadWrite` — 3 槽，虚表 `0x007E54C0`
- `ScoreAnimClass` — 4 槽，虚表 `0x007F0EDC`
  - `ScorePrintClass` — 4 槽，虚表 `0x007F0EB4`
  - `ScoreTimeClass` — 4 槽，虚表 `0x007F0EC8`
- `ScoreFontClass` — 5 槽，虚表 `0x007F0EF0`
  - `ScoreBigFontClass` — 5 槽，虚表 `0x007F0F20`
  - `ScoreFullFontClass` — 5 槽，虚表 `0x007F0F08`
- `SlaveManagerClass::PAUSlaveControl::?$VectorClass` — 7 槽，虚表 `0x007F324C`
  - `SlaveManagerClass::PAUSlaveControl::?$DynamicVectorClass` — 7 槽，虚表 `0x007F322C`
- `SpawnManagerClass::PAUSpawnControl::?$VectorClass` — 7 槽，虚表 `0x007F36D4`
  - `SpawnManagerClass::PAUSpawnControl::?$DynamicVectorClass` — 7 槽，虚表 `0x007F36B4`
- `Straw` — 3 槽，虚表 `0x007E61F0`
  - `Base64Straw` — 3 槽，虚表 `0x007EB764`
  - `BlowStraw` — 3 槽，虚表 `0x007EDF40`
  - `BufferStraw` — 3 槽，虚表 `0x007E61E0`
  - `CacheStraw` — 3 槽，虚表 `0x007EB754`
  - `FileStraw` — 3 槽，虚表 `0x007E4D90`
  - `LCWStraw` — 3 槽，虚表 `0x007ECF44`
  - `LZOStraw` — 3 槽，虚表 `0x007ED394`
  - `PKStraw` — 4 槽，虚表 `0x007EFDE0`
  - `RandomStraw` — 3 槽，虚表 `0x007F0AFC`
- `Surface` — 34 槽，虚表 `0x007E2198`
  - `XSurface` — 36 槽，虚表 `0x007E2104`
    - `BSurface` — 36 槽，虚表 `0x007E2070`
    - `DSurface` — 38 槽，虚表 `0x007E85D4`
- `ToolTipManager` — 6 槽，虚表 `0x007F57AC`
  - `CCToolTip` — 6 槽，虚表 `0x007F74C4`
- `UAcceleratorTracker::?$VectorClass` — 7 槽，虚表 `0x007EECEC`
  - `UAcceleratorTracker::?$DynamicVectorClass` — 7 槽，虚表 `0x007EECCC`
- `UAngerStruct::?$VectorClass` — 7 槽，虚表 `0x007EA984`
  - `UAngerStruct::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA924`
- `UDirtyAreaStruct::?$VectorClass` — 7 槽，虚表 `0x007F42BC`
  - `UDirtyAreaStruct::?$DynamicVectorClass` — 7 槽，虚表 `0x007F429C`
- `UScoutStruct::?$VectorClass` — 7 槽，虚表 `0x007EA9A4`
  - `UScoutStruct::?$DynamicVectorClass` — 7 槽，虚表 `0x007EA904`
- `USubzoneConnectionStruct::?$VectorClass` — 7 槽，虚表 `0x007E177C`
  - `USubzoneConnectionStruct::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED5A0`
- `USubzoneTrackingStruct::?$VectorClass` — 7 槽，虚表 `0x007ED500`
  - `USubzoneTrackingStruct::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED4A0`
- `UtagCONNECTDATA::?$VectorClass` — 7 槽，虚表 `0x007E5C84`
  - `UtagCONNECTDATA::?$DynamicVectorClass` — 7 槽，虚表 `0x007E5CC4`
- `UUndoInfoStruct::?$VectorClass` — 7 槽，虚表 `0x007F329C`
  - `UUndoInfoStruct::?$DynamicVectorClass` — 7 槽，虚表 `0x007F327C`
- `UZoneConnectionClass::?$VectorClass` — 7 槽，虚表 `0x007ED4E0`
  - `UZoneConnectionClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED4C0`
- `VAITriggerTypeClass::?$DiscreteDistributionClass::PAVAITriggerTypeClass::V?$DistributionObject::?$VectorClass` — 7 槽，虚表 `0x007F4840`
  - `VAITriggerTypeClass::?$DiscreteDistributionClass::PAVAITriggerTypeClass::V?$DistributionObject::?$DynamicVectorClass` — 7 槽，虚表 `0x007F4860`
- `VBaseNodeClass::?$VectorClass` — 7 槽，虚表 `0x007E38F0`
  - `VBaseNodeClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007E38B0`
- `VBuildingTypeClass::?$DiscreteDistributionClass::PAVBuildingTypeClass::V?$DistributionObject::?$VectorClass` — 7 槽，虚表 `0x007EAAA4`
  - `VBuildingTypeClass::?$DiscreteDistributionClass::PAVBuildingTypeClass::V?$DistributionObject::?$DynamicVectorClass` — 7 槽，虚表 `0x007EAAC4`
- `VCell::?$VectorClass` — 7 槽，虚表 `0x007E38D0`
  - `VCell::?$DynamicVectorClass` — 7 槽，虚表 `0x007E3890`
- `VCellClass::?$DiscreteDistributionClass::PAVCellClass::V?$DistributionObject::?$VectorClass` — 7 槽，虚表 `0x007E9264`
  - `VCellClass::?$DiscreteDistributionClass::PAVCellClass::V?$DistributionObject::?$DynamicVectorClass` — 7 槽，虚表 `0x007E928C`
- `VersionClass` — 1 槽，虚表 `0x007EA57C`
- `VHSVClass::?$VectorClass` — 7 槽，虚表 `0x007EF7F0`
  - `VHSVClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007EF750`
- `VPoint2D::?$VectorClass` — 7 槽，虚表 `0x007EEB74`
  - `VPoint2D::?$DynamicVectorClass` — 7 槽，虚表 `0x007EEB54`
- `VRGBClass::?$VectorClass` — 7 槽，虚表 `0x007E4E38`
  - `VRGBClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F022C`
    - `VRGBClass::?$TypeList` — 7 槽，虚表 `0x007E4E58`
- `VSwizzlePointerClass::?$VectorClass` — 7 槽，虚表 `0x007F4154`
  - `VSwizzlePointerClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F4134`
- `VWaypointClass::?$VectorClass` — 7 槽，虚表 `0x007F6EF4`
  - `VWaypointClass::?$DynamicVectorClass` — 7 槽，虚表 `0x007F6ED4`
- `VWDTTerritory::V?$rc_ptr::?$VectorClass` — 7 槽，虚表 `0x007F7230`
  - `VWDTTerritory::V?$rc_ptr::?$DynamicVectorClass` — 7 槽，虚表 `0x007F7260`
- `VWstring::?$VectorClass` — 7 槽，虚表 `0x007F1294`
  - `VWstring::?$DynamicVectorClass` — 7 槽，虚表 `0x007F12B4`
- `W4DiskID::?$VectorClass` — 7 槽，虚表 `0x007F1274`
- `W4PassabilityType::?$VectorClass` — 7 槽，虚表 `0x007ED560`
  - `W4PassabilityType::?$DynamicVectorClass` — 7 槽，虚表 `0x007ED580`
- `WinModemClass` — 10 槽，虚表 `0x007F7488`
- `WinsockInterfaceClass` — 23 槽，虚表 `0x007F79BC`
  - `IPXInterfaceClass` — 23 槽，虚表 `0x007F794C`
  - `UDPInterfaceClass` — 31 槽，虚表 `0x007F7A6C`
- `WinsockInterfaceClass::PAUWinsockBufferType::?$VectorClass` — 7 槽，虚表 `0x007F7A3C`
  - `WinsockInterfaceClass::PAUWinsockBufferType::?$DynamicVectorClass` — 7 槽，虚表 `0x007F7A1C`
- `WorldDominationTour::Map::PAUAnimationPalette::?$VectorClass` — 7 槽，虚表 `0x007F71A4`
  - `WorldDominationTour::Map::PAUAnimationPalette::?$DynamicVectorClass` — 7 槽，虚表 `0x007F7144`
- `WorldDominationTour::VCentroid::?$VectorClass` — 7 槽，虚表 `0x007F71E0`
- `WorldDominationTour::VConflict::V?$rc_ptr::?$VectorClass` — 7 槽，虚表 `0x007F6F7C`
  - `WorldDominationTour::VConflict::V?$rc_ptr::?$DynamicVectorClass` — 7 槽，虚表 `0x007F6F4C`
- `WorldDominationTour::VConflict::V?$rc_ptr::V?$DynamicVectorClass::WorldDominationTour::VConflict::V?$rc_ptr::?$VectorCursor` — 4 槽，虚表 `0x007F6F9C`
- `WorldDominationTour::VGameOption::V?$rc_ptr::?$VectorClass` — 7 槽，虚表 `0x007F6FF4`
  - `WorldDominationTour::VGameOption::V?$rc_ptr::?$DynamicVectorClass` — 7 槽，虚表 `0x007F6FD4`
- `WorldDominationTour::VGameOption::V?$rc_ptr::V?$DynamicVectorClass::WorldDominationTour::VGameOption::V?$rc_ptr::?$VectorCursor` — 4 槽，虚表 `0x007F702C`
- `WorldDominationTour::VTerritory::V?$rc_ptr::?$VectorClass` — 7 槽，虚表 `0x007F7184`
  - `WorldDominationTour::VTerritory::V?$rc_ptr::?$DynamicVectorClass` — 7 槽，虚表 `0x007F7164`
- `WorldDominationTour::VTerritory::V?$rc_ptr::V?$DynamicVectorClass::WorldDominationTour::VTerritory::V?$rc_ptr::?$VectorCursor` — 4 槽，虚表 `0x007F72D8`

## 全部类清单

| 类 | 基类 | 槽位数 | 虚表 VA |
|---|---|---:|---|
| `_N::?$DynamicVectorClass` | `_N::?$VectorClass` | 7 | `0x007EAA7C` |
| `_N::?$VectorClass` | — | 7 | `0x007EAA5C` |
| `AbstractClass` | `IPersistStream` | 24 | `0x007E1F50` |
| `AbstractTypeClass` | `AbstractClass` | 27 | `0x007E2000` |
| `AddTeamCommandClass` | `CommandClass` | 9 | `0x007EBE8C` |
| `AircraftClass` | `FootClass` | 341 | `0x007E22A4` |
| `AircraftTypeClass` | `TechnoTypeClass` | 48 | `0x007E2868` |
| `AirstrikeClass` | `AbstractClass` | 24 | `0x007E29A8` |
| `AITriggerTypeClass` | `AbstractTypeClass` | 27 | `0x007E2A50` |
| `AllianceCommandClass` | `CommandClass` | 9 | `0x007EBB44` |
| `AllToCheerCommandClass` | `CommandClass` | 9 | `0x007EBA54` |
| `AlphaShapeClass` | `AbstractClass` | 24 | `0x007E32A4` |
| `Animate` | — | 8 | `0x007E35A8` |
| `AnimClass` | `ObjectClass` | 124 | `0x007E3354` |
| `AnimFile` | `Animate` | 8 | `0x007E3584` |
| `AnimTypeClass` | `ObjectTypeClass` | 41 | `0x007E3608` |
| `ApplicationClass` | `IApplication` | 11 | `0x007E36D4` |
| `ATL::VCChatEventSink::?$CComObject` | `CChatEventSink` | 48 | `0x007F76B4` |
| `ATL::VCDownloadEventSink::?$CComObject` | `CDownloadEventSink` | 8 | `0x007F78E4` |
| `ATL::VCNetUtilEventSink::?$CComObject` | `CNetUtilEventSink` | 10 | `0x007F766C` |
| `bad_typeid` | `exception` | 1 | `0x007F95DC` |
| `Base64Pipe` | `Pipe` | 5 | `0x007EB774` |
| `Base64Straw` | `Straw` | 3 | `0x007EB764` |
| `BaseClass` | — | 3 | `0x007E3880` |
| `BeaconPlacementCommandClass` | `CommandClass` | 9 | `0x007EBBBC` |
| `BinkMovieHandle` | `MovieHandle` | 11 | `0x007EE154` |
| `BitFont` | — | 1 | `0x007E3A78` |
| `BitText` | — | 1 | `0x007E3A80` |
| `Blitter` | — | 5 | `0x007E5B88` |
| `BlowPipe` | `Pipe` | 5 | `0x007EFDC8` |
| `BlowStraw` | `Straw` | 3 | `0x007EDF40` |
| `BombClass` | `AbstractClass` | 24 | `0x007E3D10` |
| `BrainClass` | — | 1 | `0x007E3E74` |
| `BSurface` | `XSurface` | 36 | `0x007E2070` |
| `BufferIOFileClass` | `RawFileClass` | 17 | `0x007E3A2C` |
| `BufferPipe` | `Pipe` | 5 | `0x007E6200` |
| `BufferStraw` | `Straw` | 3 | `0x007E61E0` |
| `BuildingClass` | `TechnoClass` | 322 | `0x007E3EBC` |
| `BuildingLightClass` | `ObjectClass` | 122 | `0x007E3AD0` |
| `BuildingTypeClass` | `TechnoTypeClass` | 49 | `0x007E4570` |
| `BulletClass` | `ObjectClass` | 125 | `0x007E46E4` |
| `BulletTypeClass` | `ObjectTypeClass` | 40 | `0x007E4948` |
| `CacheStraw` | `Straw` | 3 | `0x007EB754` |
| `CampaignClass` | `AbstractTypeClass` | 27 | `0x007E4A28` |
| `CampaignEndScoreClass` | — | 2 | `0x007E4AB8` |
| `CampaignScoreClass` | — | 2 | `0x007E4AAC` |
| `CaptureManagerClass` | `AbstractClass` | 24 | `0x007E4B40` |
| `CarryoverClass` | `LinkClass` | 10 | `0x007E4C04` |
| `CCFileClass` | `CDFileClass` | 17 | `0x007E16B0` |
| `CChatEventSink` | `ATL::ATL::VCComMultiThreadModel::?$CComObjectRootEx` | 48 | `0x007F77A4` |
| `CCINIClass` | `INIClass` | 1 | `0x007E1AF4` |
| `CCToolTip` | `ToolTipManager` | 6 | `0x007F74C4` |
| `CD` | `DiskSwap` | 3 | `0x007E4C30` |
| `CDFileClass` | `BufferIOFileClass` | 17 | `0x007E1668` |
| `CellClass` | `AbstractClass` | 24 | `0x007E4EEC` |
| `CenterBaseCommandClass` | `CommandClass` | 9 | `0x007EBB1C` |
| `CenterREventCommandClass` | `CommandClass` | 9 | `0x007EBBE4` |
| `CenterTeamCommandClass` | `CommandClass` | 9 | `0x007EBEB4` |
| `CenterViewCommandClass` | `CommandClass` | 9 | `0x007EBAF4` |
| `CheckListClass` | `ListClass` | 51 | `0x007E4F84` |
| `CNetUtilEventSink` | `ATL::ATL::VCComMultiThreadModel::?$CComObjectRootEx` | 10 | `0x007F7778` |
| `ColorListClass` | `ListClass` | 53 | `0x007E5054` |
| `CombatantSelectCommandClass` | `CommandClass` | 9 | `0x007EB98C` |
| `CommandClass` | — | 9 | `0x007EBE3C` |
| `CommBufferClass` | — | 1 | `0x007E519C` |
| `ConnectionClass` | — | 10 | `0x007E51B4` |
| `ConnectionPointClass` | `IConnectionPoint` | 8 | `0x007E5CE4` |
| `ConnManClass` | — | 16 | `0x007EC1D4` |
| `ControlClass` | `GadgetClass` | 34 | `0x007E528C` |
| `ConvertClass` | — | 1 | `0x007E5358` |
| `CounterClass` | `H::?$VectorClass` | 7 | `0x007E5C54` |
| `CreateGameDialogControl` | `WonlineStringDialogControl` | 5 | `0x007F788C` |
| `CreateTeamCommandClass` | `CommandClass` | 9 | `0x007EB84C` |
| `CStreamClass` | `IStream` | 15 | `0x007E5DAC` |
| `CursorPositionCommandClass` | `CommandClass` | 9 | `0x007EBF54` |
| `DeleteCommandClass` | `CommandClass` | 9 | `0x007EBF7C` |
| `DeployCommandClass` | `CommandClass` | 9 | `0x007EBA2C` |
| `Dial8Class` | `ControlClass` | 34 | `0x007E5E3C` |
| `DiskLaserClass` | `AbstractClass` | 24 | `0x007E5FB8` |
| `DisplayClass` | `MapClass` | 50 | `0x007E6114` |
| `DisplayClass::TacticalClass` | `GadgetClass` | 33 | `0x007E608C` |
| `DriveLocomotionClass` | `LocomotionClass` | 10 | `0x007E7F7C` |
| `DropListClass` | `EditClass` | 46 | `0x007E7FCC` |
| `DropPodLocomotionClass` | `LocomotionClass` | 10 | `0x007E8344` |
| `DSurface` | `XSurface` | 38 | `0x007E85D4` |
| `E::?$BlitPlain` | `Blitter` | 5 | `0x007F7BDC` |
| `E::?$BlitPlainXlat` | `Blitter` | 5 | `0x007E5B70` |
| `E::?$BlitTrans` | `Blitter` | 5 | `0x007F7C0C` |
| `E::?$BlitTransRemapDest` | `Blitter` | 5 | `0x007E5B28` |
| `E::?$BlitTransRemapXlat` | `Blitter` | 5 | `0x007E5B10` |
| `E::?$BlitTransXlat` | `Blitter` | 5 | `0x007E5B58` |
| `E::?$BlitTransZRemapXlat` | `Blitter` | 5 | `0x007E5B40` |
| `E::?$RLEBlitTransRemapDest` | `RLEBlitter` | 3 | `0x007E5AE0` |
| `E::?$RLEBlitTransRemapDestZRead` | `RLEBlitter` | 3 | `0x007E5AA0` |
| `E::?$RLEBlitTransRemapDestZReadWrite` | `RLEBlitter` | 3 | `0x007E5A60` |
| `E::?$RLEBlitTransRemapXlat` | `RLEBlitter` | 3 | `0x007E5AD0` |
| `E::?$RLEBlitTransRemapXlatZRead` | `RLEBlitter` | 3 | `0x007E5A90` |
| `E::?$RLEBlitTransRemapXlatZReadWrite` | `RLEBlitter` | 3 | `0x007E5A50` |
| `E::?$RLEBlitTransXlat` | `RLEBlitter` | 3 | `0x007E5B00` |
| `E::?$RLEBlitTransXlatZRead` | `RLEBlitter` | 3 | `0x007E5AC0` |
| `E::?$RLEBlitTransXlatZReadWrite` | `RLEBlitter` | 3 | `0x007E5A80` |
| `E::?$RLEBlitTransZRemapXlat` | `RLEBlitter` | 3 | `0x007E5AF0` |
| `E::?$RLEBlitTransZRemapXlatZRead` | `RLEBlitter` | 3 | `0x007E5AB0` |
| `E::?$RLEBlitTransZRemapXlatZReadWrite` | `RLEBlitter` | 3 | `0x007E5A70` |
| `E::?$VectorClass` | — | 7 | `0x007F65F4` |
| `EditClass` | `ControlClass` | 39 | `0x007E81A4` |
| `EMPulseClass` | `AbstractClass` | 24 | `0x007E87A8` |
| `EnumConnectionPointsClass` | `IEnumConnectionPoints` | 7 | `0x007E5D28` |
| `EnumConnectionsClass` | `IEnumConnections` | 7 | `0x007E5CA4` |
| `FactoryClass` | `AbstractClass` | 24 | `0x007E88D0` |
| `FileClass` | — | 17 | `0x007F08BC` |
| `FilePipe` | `Pipe` | 5 | `0x007E4DA0` |
| `FileStraw` | `Straw` | 3 | `0x007E4D90` |
| `FlyLocomotionClass` | `LocomotionClass` | 10 | `0x007E8AC0` |
| `FoggedObjectClass` | `AbstractClass` | 25 | `0x007E8B38` |
| `FoggedObjectClass::UDrawRecord::?$DynamicVectorClass` | `FoggedObjectClass::UDrawRecord::?$VectorClass` | 7 | `0x007E8BA0` |
| `FoggedObjectClass::UDrawRecord::?$VectorClass` | — | 7 | `0x007E8BC0` |
| `FollowCommandClass` | `CommandClass` | 9 | `0x007EBDC4` |
| `FootClass` | `TechnoClass` | 341 | `0x007E8C94` |
| `FreeForAll` | `MultiplayerGameMode` | 52 | `0x007EE424` |
| `G::?$BlitPlain` | `Blitter` | 5 | `0x007F7BC4` |
| `G::?$BlitPlainXlat` | `Blitter` | 5 | `0x007E5A38` |
| `G::?$BlitPlainXlatAlpha` | `Blitter` | 5 | `0x007E57F8` |
| `G::?$BlitPlainXlatZRead` | `Blitter` | 5 | `0x007E5990` |
| `G::?$BlitPlainXlatZReadWrite` | `Blitter` | 5 | `0x007E58A0` |
| `G::?$BlitTrans` | `Blitter` | 5 | `0x007F7BF4` |
| `G::?$BlitTransDarken` | `Blitter` | 5 | `0x007E59F0` |
| `G::?$BlitTransDarkenZRead` | `Blitter` | 5 | `0x007E5948` |
| `G::?$BlitTransDarkenZReadWrite` | `Blitter` | 5 | `0x007E5858` |
| `G::?$BlitTransLucent25` | `Blitter` | 5 | `0x007E59A8` |
| `G::?$BlitTransLucent25Alpha` | `Blitter` | 5 | `0x007E5780` |
| `G::?$BlitTransLucent25AlphaZRead` | `Blitter` | 5 | `0x007E5690` |
| `G::?$BlitTransLucent25AlphaZReadWarp` | `Blitter` | 5 | `0x007E5648` |
| `G::?$BlitTransLucent25AlphaZReadWrite` | `Blitter` | 5 | `0x007E55D0` |
| `G::?$BlitTransLucent25ZRead` | `Blitter` | 5 | `0x007E5900` |
| `G::?$BlitTransLucent25ZReadWarp` | `Blitter` | 5 | `0x007E58B8` |
| `G::?$BlitTransLucent25ZReadWrite` | `Blitter` | 5 | `0x007E5810` |
| `G::?$BlitTransLucent50` | `Blitter` | 5 | `0x007E59C0` |
| `G::?$BlitTransLucent50Alpha` | `Blitter` | 5 | `0x007E5798` |
| `G::?$BlitTransLucent50AlphaZRead` | `Blitter` | 5 | `0x007E56A8` |
| `G::?$BlitTransLucent50AlphaZReadWarp` | `Blitter` | 5 | `0x007E5660` |
| `G::?$BlitTransLucent50AlphaZReadWrite` | `Blitter` | 5 | `0x007E55E8` |
| `G::?$BlitTranslucent50NonzeroAlpha` | `Blitter` | 5 | `0x007E5720` |
| `G::?$BlitTranslucent50ZeroAlpha` | `Blitter` | 5 | `0x007E5708` |
| `G::?$BlitTransLucent50ZRead` | `Blitter` | 5 | `0x007E5918` |
| `G::?$BlitTransLucent50ZReadWarp` | `Blitter` | 5 | `0x007E58D0` |
| `G::?$BlitTransLucent50ZReadWrite` | `Blitter` | 5 | `0x007E5828` |
| `G::?$BlitTransLucent75` | `Blitter` | 5 | `0x007E59D8` |
| `G::?$BlitTransLucent75Alpha` | `Blitter` | 5 | `0x007E57B0` |
| `G::?$BlitTransLucent75AlphaZRead` | `Blitter` | 5 | `0x007E56C0` |
| `G::?$BlitTransLucent75AlphaZReadWarp` | `Blitter` | 5 | `0x007E5678` |
| `G::?$BlitTransLucent75AlphaZReadWrite` | `Blitter` | 5 | `0x007E5600` |
| `G::?$BlitTransLucent75ZRead` | `Blitter` | 5 | `0x007E5930` |
| `G::?$BlitTransLucent75ZReadWarp` | `Blitter` | 5 | `0x007E58E8` |
| `G::?$BlitTransLucent75ZReadWrite` | `Blitter` | 5 | `0x007E5840` |
| `G::?$BlitTranslucentWriteAlpha` | `Blitter` | 5 | `0x007E5738` |
| `G::?$BlitTransXlat` | `Blitter` | 5 | `0x007E5A20` |
| `G::?$BlitTransXlatAlpha` | `Blitter` | 5 | `0x007E57E0` |
| `G::?$BlitTransXlatAlphaZRead` | `Blitter` | 5 | `0x007E56F0` |
| `G::?$BlitTransXlatAlphaZReadWrite` | `Blitter` | 5 | `0x007E5630` |
| `G::?$BlitTransXlatMultWriteAlpha` | `Blitter` | 5 | `0x007E5750` |
| `G::?$BlitTransXlatWriteAlpha` | `Blitter` | 5 | `0x007E5768` |
| `G::?$BlitTransXlatZRead` | `Blitter` | 5 | `0x007E5978` |
| `G::?$BlitTransXlatZReadWrite` | `Blitter` | 5 | `0x007E5888` |
| `G::?$BlitTransZRemapXlat` | `Blitter` | 5 | `0x007E5A08` |
| `G::?$BlitTransZRemapXlatAlpha` | `Blitter` | 5 | `0x007E57C8` |
| `G::?$BlitTransZRemapXlatAlphaZRead` | `Blitter` | 5 | `0x007E56D8` |
| `G::?$BlitTransZRemapXlatAlphaZReadWrite` | `Blitter` | 5 | `0x007E5618` |
| `G::?$BlitTransZRemapXlatZRead` | `Blitter` | 5 | `0x007E5960` |
| `G::?$BlitTransZRemapXlatZReadWrite` | `Blitter` | 5 | `0x007E5870` |
| `G::?$DynamicVectorClass` | `G::?$VectorClass` | 7 | `0x007E3844` |
| `G::?$RLEBlitTransDarken` | `RLEBlitter` | 3 | `0x007E55A0` |
| `G::?$RLEBlitTransDarkenZRead` | `RLEBlitter` | 3 | `0x007E5540` |
| `G::?$RLEBlitTransDarkenZReadWrite` | `RLEBlitter` | 3 | `0x007E54B0` |
| `G::?$RLEBlitTransLucent25` | `RLEBlitter` | 3 | `0x007E5570` |
| `G::?$RLEBlitTransLucent25Alpha` | `RLEBlitter` | 3 | `0x007E5430` |
| `G::?$RLEBlitTransLucent25AlphaZRead` | `RLEBlitter` | 3 | `0x007E53E0` |
| `G::?$RLEBlitTransLucent25AlphaZReadWarp` | `RLEBlitter` | 3 | `0x007E53B0` |
| `G::?$RLEBlitTransLucent25AlphaZReadWrite` | `RLEBlitter` | 3 | `0x007E5360` |
| `G::?$RLEBlitTransLucent25ZRead` | `RLEBlitter` | 3 | `0x007E5510` |
| `G::?$RLEBlitTransLucent25ZReadWarp` | `RLEBlitter` | 3 | `0x007E54E0` |
| `G::?$RLEBlitTransLucent25ZReadWrite` | `RLEBlitter` | 3 | `0x007E5480` |
| `G::?$RLEBlitTransLucent50` | `RLEBlitter` | 3 | `0x007E5580` |
| `G::?$RLEBlitTransLucent50Alpha` | `RLEBlitter` | 3 | `0x007E5440` |
| `G::?$RLEBlitTransLucent50AlphaZRead` | `RLEBlitter` | 3 | `0x007E53F0` |
| `G::?$RLEBlitTransLucent50AlphaZReadWarp` | `RLEBlitter` | 3 | `0x007E53C0` |
| `G::?$RLEBlitTransLucent50AlphaZReadWrite` | `RLEBlitter` | 3 | `0x007E5370` |
| `G::?$RLEBlitTransLucent50ZRead` | `RLEBlitter` | 3 | `0x007E5520` |
| `G::?$RLEBlitTransLucent50ZReadWarp` | `RLEBlitter` | 3 | `0x007E54F0` |
| `G::?$RLEBlitTransLucent50ZReadWrite` | `RLEBlitter` | 3 | `0x007E5490` |
| `G::?$RLEBlitTransLucent75` | `RLEBlitter` | 3 | `0x007E5590` |
| `G::?$RLEBlitTransLucent75Alpha` | `RLEBlitter` | 3 | `0x007E5450` |
| `G::?$RLEBlitTransLucent75AlphaZRead` | `RLEBlitter` | 3 | `0x007E5400` |
| `G::?$RLEBlitTransLucent75AlphaZReadWarp` | `RLEBlitter` | 3 | `0x007E53D0` |
| `G::?$RLEBlitTransLucent75AlphaZReadWrite` | `RLEBlitter` | 3 | `0x007E5380` |
| `G::?$RLEBlitTransLucent75ZRead` | `RLEBlitter` | 3 | `0x007E5530` |
| `G::?$RLEBlitTransLucent75ZReadWarp` | `RLEBlitter` | 3 | `0x007E5500` |
| `G::?$RLEBlitTransLucent75ZReadWrite` | `RLEBlitter` | 3 | `0x007E54A0` |
| `G::?$RLEBlitTransXlat` | `RLEBlitter` | 3 | `0x007E55C0` |
| `G::?$RLEBlitTransXlatAlpha` | `RLEBlitter` | 3 | `0x007E5470` |
| `G::?$RLEBlitTransXlatAlphaZRead` | `RLEBlitter` | 3 | `0x007E5420` |
| `G::?$RLEBlitTransXlatAlphaZReadWrite` | `RLEBlitter` | 3 | `0x007E53A0` |
| `G::?$RLEBlitTransXlatZRead` | `RLEBlitter` | 3 | `0x007E5560` |
| `G::?$RLEBlitTransXlatZReadWrite` | `RLEBlitter` | 3 | `0x007E54D0` |
| `G::?$RLEBlitTransZRemapXlat` | `RLEBlitter` | 3 | `0x007E55B0` |
| `G::?$RLEBlitTransZRemapXlatAlpha` | `RLEBlitter` | 3 | `0x007E5460` |
| `G::?$RLEBlitTransZRemapXlatAlphaZRead` | `RLEBlitter` | 3 | `0x007E5410` |
| `G::?$RLEBlitTransZRemapXlatAlphaZReadWrite` | `RLEBlitter` | 3 | `0x007E5390` |
| `G::?$RLEBlitTransZRemapXlatZRead` | `RLEBlitter` | 3 | `0x007E5550` |
| `G::?$RLEBlitTransZRemapXlatZReadWrite` | `RLEBlitter` | 3 | `0x007E54C0` |
| `G::?$VectorClass` | — | 7 | `0x007E3824` |
| `GadgetClass` | `LinkClass` | 33 | `0x007E92BC` |
| `GaugeClass` | `ControlClass` | 42 | `0x007E9384` |
| `GenericList` | — | 1 | `0x007E1B04` |
| `GenericNode` | — | 1 | `0x007E1B0C` |
| `GraphicMenu` | — | 1 | `0x007EA5FC` |
| `GraphicMenuAnimItem` | `GraphicMenuItem` | 6 | `0x007EA658` |
| `GraphicMenuImageItem` | `GraphicMenuItem` | 6 | `0x007EA674` |
| `GraphicMenuItem` | — | 6 | `0x007EA690` |
| `GraphicMenuShortcutItem` | `GraphicMenuItem` | 6 | `0x007EA6AC` |
| `GScreenClass` | `IGameMap` | 22 | `0x007EA6FC` |
| `GuardCommandClass` | `CommandClass` | 9 | `0x007EBAA4` |
| `H::?$DynamicVectorClass` | `H::?$VectorClass` | 7 | `0x007E4E78` |
| `H::?$TypeList` | `H::?$DynamicVectorClass` | 7 | `0x007E4DD8` |
| `H::?$VectorClass` | — | 7 | `0x007E4DB8` |
| `H::V?$TPoint3D::?$VectorClass` | — | 7 | `0x007E4638` |
| `H::V?$TRect::?$DynamicVectorClass` | `H::V?$TRect::?$VectorClass` | 7 | `0x007ED99C` |
| `H::V?$TRect::?$VectorClass` | — | 7 | `0x007ED970` |
| `H::V?$TRect::V?$VectorClass::H::V?$TRect::?$VectorCursor` | — | 4 | `0x007F71CC` |
| `HealthNavCommandClass` | `CommandClass` | 9 | `0x007EB93C` |
| `HouseClass` | `AbstractClass` | 24 | `0x007EA8A0` |
| `HouseClass::PAUBuildChoiceClass::?$DynamicVectorClass` | `HouseClass::PAUBuildChoiceClass::?$VectorClass` | 7 | `0x007EA7B4` |
| `HouseClass::PAUBuildChoiceClass::?$VectorClass` | — | 7 | `0x007EA7D4` |
| `HouseClass::PAUStartingTechnoStruct::?$DynamicVectorClass` | `HouseClass::PAUStartingTechnoStruct::?$VectorClass` | 7 | `0x007EA944` |
| `HouseClass::PAUStartingTechnoStruct::?$VectorClass` | — | 7 | `0x007EA964` |
| `HouseTypeClass` | `AbstractTypeClass` | 27 | `0x007EAB58` |
| `HoverLocomotionClass` | `LocomotionClass` | 10 | `0x007EADC8` |
| `I::?$DynamicVectorClass` | `I::?$VectorClass` | 7 | `0x007E37CC` |
| `I::?$VectorClass` | — | 7 | `0x007E37EC` |
| `I::IV?$DynamicVectorClass::?$VectorCursor` | — | 4 | `0x007EA6C8` |
| `II::U?$HashObject::?$DynamicVectorClass` | `II::U?$HashObject::?$VectorClass` | 7 | `0x007ED540` |
| `II::U?$HashObject::?$VectorClass` | — | 7 | `0x007ED5C0` |
| `InfantryClass` | `FootClass` | 343 | `0x007EB058` |
| `InfantryTypeClass` | `TechnoTypeClass` | 48 | `0x007EB610` |
| `INIClass` | — | 1 | `0x007EA5F4` |
| `INIClass::INIEntry` | `INIClass::PAUINIEntry::?$Node` | 1 | `0x007EB734` |
| `INIClass::INISection` | `INIClass::PAUINISection::?$Node` | 1 | `0x007EB73C` |
| `INIClass::PAUINIEntry::?$List` | `GenericList` | 1 | `0x007EB744` |
| `INIClass::PAUINISection::?$List` | `GenericList` | 1 | `0x007E1AFC` |
| `INIClass::PAUINISection::?$Node` | `GenericNode` | 1 | `0x007EB74C` |
| `INoticeSink` | — | 1 | `0x007E1FBC` |
| `INoticeSource` | — | 1 | `0x007E1FB4` |
| `IPXConnClass` | `ConnectionClass` | 11 | `0x007EC0CC` |
| `IPXGlobalConnClass` | `IPXConnClass` | 18 | `0x007EC10C` |
| `IPXInterfaceClass` | `WinsockInterfaceClass` | 23 | `0x007F794C` |
| `IPXManagerClass` | `ConnManClass` | 25 | `0x007EC16C` |
| `IsometricTileClass` | `ObjectClass` | 122 | `0x007EC258` |
| `IsometricTileTypeClass` | `ObjectTypeClass` | 40 | `0x007ECC48` |
| `IsometricTileTypeClass::PAUTileInsertType::?$DynamicVectorClass` | `IsometricTileTypeClass::PAUTileInsertType::?$VectorClass` | 7 | `0x007ECBDC` |
| `IsometricTileTypeClass::PAUTileInsertType::?$VectorClass` | — | 7 | `0x007ECBFC` |
| `IUSubzoneConnectionStruct::U?$HashObject::?$DynamicVectorClass` | `IUSubzoneConnectionStruct::U?$HashObject::?$VectorClass` | 7 | `0x007ED520` |
| `IUSubzoneConnectionStruct::U?$HashObject::?$VectorClass` | — | 7 | `0x007ED5E0` |
| `JumpjetLocomotionClass` | `LocomotionClass` | 10 | `0x007ECE34` |
| `K::?$DynamicVectorClass` | `K::?$VectorClass` | 7 | `0x007F3728` |
| `K::?$VectorClass` | — | 7 | `0x007F3748` |
| `LayerClass` | `PAVObjectClass::?$DynamicVectorClass` | 10 | `0x007E6060` |
| `LCWPipe` | `Pipe` | 5 | `0x007ECF2C` |
| `LCWStraw` | `Straw` | 3 | `0x007ECF44` |
| `LightConvertClass` | `ConvertClass` | 2 | `0x007ED0A4` |
| `LightSourceClass` | `AbstractClass` | 24 | `0x007ED028` |
| `LightSourceClass::PAVPendingCellClass::?$DynamicVectorClass` | `LightSourceClass::PAVPendingCellClass::?$VectorClass` | 7 | `0x007ECFBC` |
| `LightSourceClass::PAVPendingCellClass::?$VectorClass` | — | 7 | `0x007ECFDC` |
| `LinkClass` | — | 10 | `0x007E9344` |
| `ListClass` | `ControlClass` | 51 | `0x007ED10C` |
| `LoadOptionsClass` | — | 9 | `0x007ED2E4` |
| `LoadProgressMgr` | `INoticeSink` | 1 | `0x007ECF64` |
| `LocomotionClass` | `IPersistStream` | 10 | `0x007EAEC0` |
| `LogicClass` | `LayerClass` | 11 | `0x007E18FC` |
| `LZOPipe` | `Pipe` | 5 | `0x007ED37C` |
| `LZOStraw` | `Straw` | 3 | `0x007ED394` |
| `MapClass` | `GScreenClass` | 30 | `0x007ED404` |
| `MapSeedClass` | `LoadOptionsClass` | 9 | `0x007ED8E4` |
| `MapSelect` | `MSEngine` | 3 | `0x007EDB4C` |
| `MechLocomotionClass` | `LocomotionClass` | 10 | `0x007EDC38` |
| `Megawealth` | `MultiplayerGameMode` | 52 | `0x007EE5F4` |
| `MissionClass` | `ObjectClass` | 157 | `0x007EDCC0` |
| `MixFileClass` | `PAVMixFileClass::?$Node` | 1 | `0x007EDF50` |
| `Mouse` | — | 18 | `0x007F7B78` |
| `MouseClass` | `ScrollClass` | 55 | `0x007E1964` |
| `MovieHandle` | — | 11 | `0x007EE124` |
| `MPCooperative` | `MultiplayerGameMode` | 52 | `0x007EE27C` |
| `MSAnim` | — | 9 | `0x007EE8E8` |
| `MSBinkAnim` | `MSAnim` | 9 | `0x007EE988` |
| `MSBitPrintAnim` | `MSAnim` | 9 | `0x007EE9D8` |
| `MSEngine` | — | 3 | `0x007EEBD4` |
| `MSFadeAnim` | `MSShapeAnim` | 9 | `0x007EE938` |
| `MSFont` | — | 5 | `0x007EEC64` |
| `MSFrameAnim` | `MSAnim` | 9 | `0x007F7104` |
| `MSOverlayAnim` | `MSFadeAnim` | 9 | `0x007EE960` |
| `MSPCXAnim` | `MSAnim` | 9 | `0x007EEA2C` |
| `MSPrintAnim` | `MSAnim` | 9 | `0x007EEA00` |
| `MSShapeAnim` | `MSAnim` | 9 | `0x007EE910` |
| `MSVQAnim` | `MSAnim` | 9 | `0x007EE9B0` |
| `MultiplayerBattle` | `MultiplayerGameMode` | 52 | `0x007EE184` |
| `MultiplayerBattleTeam` | `MultiplayerTeam` | 3 | `0x007EE258` |
| `MultiplayerDebugCommandClass` | `CommandClass` | 9 | `0x007EBE14` |
| `MultiplayerGameMode` | — | 52 | `0x007EED60` |
| `MultiplayerGameMode::InitializerBase` | — | 2 | `0x007EEE74` |
| `MultiplayerGameMode::VFreeForAll::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 2 | `0x007EEE8C` |
| `MultiplayerGameMode::VMPCooperative::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 2 | `0x007EEE80` |
| `MultiplayerGameMode::VMultiplayerBattle::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 2 | `0x007EEEBC` |
| `MultiplayerGameMode::VMultiplayerManBattle::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 2 | `0x007EEEB0` |
| `MultiplayerGameMode::VMultiplayerSiege::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 2 | `0x007EEEA4` |
| `MultiplayerGameMode::VUnholyAlliance::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 2 | `0x007EEE98` |
| `MultiplayerManBattle` | `MultiplayerGameMode` | 52 | `0x007EE50C` |
| `MultiplayerObserverTeam` | `MultiplayerTeam` | 3 | `0x007EE6C8` |
| `MultiplayerSiege` | `MultiplayerGameMode` | 52 | `0x007EE6FC` |
| `MultiplayerSiegeAttackerTeam` | `MultiplayerTeam` | 3 | `0x007EE7F4` |
| `MultiplayerSiegeDefenderTeam` | `MultiplayerTeam` | 3 | `0x007EE7E4` |
| `MultiplayerSyncCommandClass` | `CommandClass` | 9 | `0x007EBDEC` |
| `MultiplayerTeam` | — | 3 | `0x007EEEDC` |
| `N::?$DynamicVectorClass` | `N::?$VectorClass` | 7 | `0x007EDA6C` |
| `N::?$VectorClass` | — | 7 | `0x007EDA4C` |
| `NeuronClass` | `AbstractClass` | 24 | `0x007E3DF0` |
| `NextObjectCommandClass` | `CommandClass` | 9 | `0x007EB9DC` |
| `NullModemClass` | `ConnManClass` | 16 | `0x007EEFDC` |
| `NullModemConnClass` | `ConnectionClass` | 10 | `0x007EEF90` |
| `ObjectClass` | `AbstractClass` | 122 | `0x007EF060` |
| `ObjectTypeClass` | `AbstractTypeClass` | 40 | `0x007EF2D8` |
| `OptionsCommandClass` | `CommandClass` | 9 | `0x007EBC5C` |
| `OverlayClass` | `ObjectClass` | 122 | `0x007EF3D4` |
| `OverlayTypeClass` | `ObjectTypeClass` | 41 | `0x007EF600` |
| `OwnerDraw::DialogControl` | — | 5 | `0x007EF720` |
| `OwnerDraw::SimpleDialogControl` | `OwnerDraw::DialogControl` | 5 | `0x007EF738` |
| `OwnerTalkClass::PAUConnectionListStruct::?$DynamicVectorClass` | `OwnerTalkClass::PAUConnectionListStruct::?$VectorClass` | 7 | `0x007F0C2C` |
| `OwnerTalkClass::PAUConnectionListStruct::?$VectorClass` | — | 7 | `0x007F0C4C` |
| `PAD::?$DynamicVectorClass` | `PAD::?$VectorClass` | 7 | `0x007E5BC4` |
| `PAD::?$VectorClass` | — | 7 | `0x007E5C24` |
| `PAD::PAV?$DynamicVectorClass::?$DynamicVectorClass` | `PAD::PAV?$DynamicVectorClass::?$VectorClass` | 7 | `0x007E5BE4` |
| `PAD::PAV?$DynamicVectorClass::?$VectorClass` | — | 7 | `0x007E5C04` |
| `PAE::?$DynamicVectorClass` | `PAE::?$VectorClass` | 7 | `0x007F7AEC` |
| `PAE::?$VectorClass` | — | 7 | `0x007F7B0C` |
| `PAG::?$DynamicVectorClass` | `PAG::?$VectorClass` | 7 | `0x007ECCEC` |
| `PAG::?$VectorClass` | — | 7 | `0x007ECD0C` |
| `PageUserCommandClass` | `CommandClass` | 9 | `0x007EBF2C` |
| `ParasiteClass` | `AbstractClass` | 24 | `0x007EF890` |
| `ParticleClass` | `ObjectClass` | 123 | `0x007EF954` |
| `ParticleSystemClass` | `ObjectClass` | 122 | `0x007EFB9C` |
| `ParticleSystemTypeClass` | `ObjectTypeClass` | 40 | `0x007F00A8` |
| `ParticleTypeClass` | `ObjectTypeClass` | 40 | `0x007F0188` |
| `PAU_DDSURFACEDESC::?$DynamicVectorClass` | `PAU_DDSURFACEDESC::?$VectorClass` | 7 | `0x007E5E0C` |
| `PAU_DDSURFACEDESC::?$VectorClass` | — | 7 | `0x007E5DEC` |
| `PAU_WIN32_FIND_DATAA::?$DynamicVectorClass` | `PAU_WIN32_FIND_DATAA::?$VectorClass` | 7 | `0x007ED94C` |
| `PAU_WIN32_FIND_DATAA::?$VectorClass` | — | 7 | `0x007ED92C` |
| `PAUButtonFadeEffect::?$DynamicVectorClass` | `PAUButtonFadeEffect::?$VectorClass` | 7 | `0x007E856C` |
| `PAUButtonFadeEffect::?$VectorClass` | — | 7 | `0x007E8500` |
| `PAUControlNode::?$DynamicVectorClass` | `PAUControlNode::?$VectorClass` | 7 | `0x007E4BA4` |
| `PAUControlNode::?$VectorClass` | — | 7 | `0x007E4BC4` |
| `PAUCrossDissolveEffect::?$DynamicVectorClass` | `PAUCrossDissolveEffect::?$VectorClass` | 7 | `0x007E854C` |
| `PAUCrossDissolveEffect::?$VectorClass` | — | 7 | `0x007E8520` |
| `PAUDamageGroup::?$DynamicVectorClass` | `PAUDamageGroup::?$VectorClass` | 7 | `0x007E5170` |
| `PAUDamageGroup::?$VectorClass` | — | 7 | `0x007E5144` |
| `PAUGlobalPacketType::?$DynamicVectorClass` | `PAUGlobalPacketType::?$VectorClass` | 7 | `0x007F11D4` |
| `PAUGlobalPacketType::?$VectorClass` | — | 7 | `0x007F1234` |
| `PAUHWND__::?$DynamicVectorClass` | `PAUHWND__::?$VectorClass` | 7 | `0x007EEC8C` |
| `PAUHWND__::?$VectorClass` | — | 7 | `0x007EECAC` |
| `PAUIConnectionPoint::?$DynamicVectorClass` | `PAUIConnectionPoint::?$VectorClass` | 7 | `0x007E5D48` |
| `PAUIConnectionPoint::?$VectorClass` | — | 7 | `0x007E5D08` |
| `PAUKamikazeControl::?$DynamicVectorClass` | `PAUKamikazeControl::?$VectorClass` | 7 | `0x007ECE7C` |
| `PAUKamikazeControl::?$VectorClass` | — | 7 | `0x007ECE9C` |
| `PAUMPlayerScoreType::?$DynamicVectorClass` | `PAUMPlayerScoreType::?$VectorClass` | 7 | `0x007EE3F0` |
| `PAUMPlayerScoreType::?$VectorClass` | — | 7 | `0x007EE3D0` |
| `PAUNodeNameType::?$DynamicVectorClass` | `PAUNodeNameType::?$VectorClass` | 7 | `0x007EE370` |
| `PAUNodeNameType::?$VectorClass` | — | 7 | `0x007EE390` |
| `PAUtConnInfoStruct::?$DynamicVectorClass` | `PAUtConnInfoStruct::?$VectorClass` | 7 | `0x007F78C4` |
| `PAUtConnInfoStruct::?$VectorClass` | — | 7 | `0x007F78A4` |
| `PAUThemeControl::?$DynamicVectorClass` | `PAUThemeControl::?$VectorClass` | 7 | `0x007F568C` |
| `PAUThemeControl::?$VectorClass` | — | 7 | `0x007EA584` |
| `PAVAbstractClass::?$DynamicVectorClass` | `PAVAbstractClass::?$VectorClass` | 7 | `0x007E91EC` |
| `PAVAbstractClass::?$VectorClass` | — | 7 | `0x007E920C` |
| `PAVAbstractTypeClass::?$DynamicVectorClass` | `PAVAbstractTypeClass::?$VectorClass` | 7 | `0x007EA524` |
| `PAVAbstractTypeClass::?$VectorClass` | — | 7 | `0x007EA544` |
| `PAVAircraftClass::?$DynamicVectorClass` | `PAVAircraftClass::?$VectorClass` | 7 | `0x007E9E64` |
| `PAVAircraftClass::?$VectorClass` | — | 7 | `0x007E9E84` |
| `PAVAircraftTypeClass::?$DynamicVectorClass` | `PAVAircraftTypeClass::?$VectorClass` | 7 | `0x007EA264` |
| `PAVAircraftTypeClass::?$VectorClass` | — | 7 | `0x007EA284` |
| `PAVAirstrikeClass::?$DynamicVectorClass` | `PAVAirstrikeClass::?$VectorClass` | 7 | `0x007E293C` |
| `PAVAirstrikeClass::?$VectorClass` | — | 7 | `0x007E295C` |
| `PAVAITriggerTypeClass::?$DynamicVectorClass` | `PAVAITriggerTypeClass::?$VectorClass` | 7 | `0x007E9B64` |
| `PAVAITriggerTypeClass::?$VectorClass` | — | 7 | `0x007E9B84` |
| `PAVAlphaLightingRemapClass::?$DynamicVectorClass` | `PAVAlphaLightingRemapClass::?$VectorClass` | 7 | `0x007E2AD0` |
| `PAVAlphaLightingRemapClass::?$VectorClass` | — | 7 | `0x007E2AF0` |
| `PAVAlphaShapeClass::?$DynamicVectorClass` | `PAVAlphaShapeClass::?$VectorClass` | 7 | `0x007E3238` |
| `PAVAlphaShapeClass::?$VectorClass` | — | 7 | `0x007E3258` |
| `PAVAnimClass::?$DynamicVectorClass` | `PAVAnimClass::?$VectorClass` | 7 | `0x007E9F24` |
| `PAVAnimClass::?$VectorClass` | — | 7 | `0x007E9F44` |
| `PAVAnimTypeClass::?$DynamicVectorClass` | `PAVAnimTypeClass::?$VectorClass` | 7 | `0x007EA2E4` |
| `PAVAnimTypeClass::?$VectorClass` | — | 7 | `0x007EA304` |
| `PAVBombClass::?$DynamicVectorClass` | `PAVBombClass::?$VectorClass` | 7 | `0x007E17CC` |
| `PAVBombClass::?$VectorClass` | — | 7 | `0x007E17EC` |
| `PAVBuildingClass::?$DynamicVectorClass` | `PAVBuildingClass::?$VectorClass` | 7 | `0x007E9E24` |
| `PAVBuildingClass::?$VectorClass` | — | 7 | `0x007E9E44` |
| `PAVBuildingLightClass::?$DynamicVectorClass` | `PAVBuildingLightClass::?$VectorClass` | 7 | `0x007E9C24` |
| `PAVBuildingLightClass::?$VectorClass` | — | 7 | `0x007E9C44` |
| `PAVBuildingTypeClass::?$DynamicVectorClass` | `PAVBuildingTypeClass::?$VectorClass` | 7 | `0x007EA224` |
| `PAVBuildingTypeClass::?$VectorClass` | — | 7 | `0x007EA244` |
| `PAVBulletClass::?$DynamicVectorClass` | `PAVBulletClass::?$VectorClass` | 7 | `0x007E4678` |
| `PAVBulletClass::?$VectorClass` | — | 7 | `0x007E4698` |
| `PAVBulletTypeClass::?$DynamicVectorClass` | `PAVBulletTypeClass::?$VectorClass` | 7 | `0x007EA364` |
| `PAVBulletTypeClass::?$VectorClass` | — | 7 | `0x007EA384` |
| `PAVCampaignClass::?$DynamicVectorClass` | `PAVCampaignClass::?$VectorClass` | 7 | `0x007E9FE4` |
| `PAVCampaignClass::?$VectorClass` | — | 7 | `0x007EA004` |
| `PAVCaptureManagerClass::?$DynamicVectorClass` | `PAVCaptureManagerClass::?$VectorClass` | 7 | `0x007E4AD4` |
| `PAVCaptureManagerClass::?$VectorClass` | — | 7 | `0x007E4AF4` |
| `PAVCCINIClass::?$DynamicVectorClass` | `PAVCCINIClass::?$VectorClass` | 7 | `0x007EB82C` |
| `PAVCCINIClass::?$VectorClass` | — | 7 | `0x007EB80C` |
| `PAVCellClass::?$DynamicVectorClass` | `PAVCellClass::?$VectorClass` | 7 | `0x007ED9BC` |
| `PAVCellClass::?$VectorClass` | — | 7 | `0x007ED480` |
| `PAVColorScheme::?$DynamicVectorClass` | `PAVColorScheme::?$VectorClass` | 7 | `0x007EF790` |
| `PAVColorScheme::?$VectorClass` | — | 7 | `0x007EF7B0` |
| `PAVConvertClass::?$DynamicVectorClass` | `PAVConvertClass::?$VectorClass` | 7 | `0x007E5318` |
| `PAVConvertClass::?$VectorClass` | — | 7 | `0x007E5338` |
| `PAVCoopCampaignClass::?$DynamicVectorClass` | `PAVCoopCampaignClass::?$VectorClass` | 7 | `0x007EE350` |
| `PAVCoopCampaignClass::?$VectorClass` | — | 7 | `0x007EE3B0` |
| `PAVDiskLaserClass::?$DynamicVectorClass` | `PAVDiskLaserClass::?$VectorClass` | 7 | `0x007E5EDC` |
| `PAVDiskLaserClass::?$VectorClass` | — | 7 | `0x007E5EFC` |
| `PAVEBolt::?$DynamicVectorClass` | `PAVEBolt::?$VectorClass` | 7 | `0x007E868C` |
| `PAVEBolt::?$VectorClass` | — | 7 | `0x007E86AC` |
| `PAVEgoClass::?$DynamicVectorClass` | `PAVEgoClass::?$VectorClass` | 7 | `0x007E86DC` |
| `PAVEgoClass::?$VectorClass` | — | 7 | `0x007E86FC` |
| `PAVEMPulseClass::?$DynamicVectorClass` | `PAVEMPulseClass::?$VectorClass` | 7 | `0x007E873C` |
| `PAVEMPulseClass::?$VectorClass` | — | 7 | `0x007E875C` |
| `PAVEventClass::?$DynamicVectorClass` | `PAVEventClass::?$VectorClass` | 7 | `0x007EFE04` |
| `PAVEventClass::?$VectorClass` | — | 7 | `0x007EFE24` |
| `PAVFactoryClass::?$DynamicVectorClass` | `PAVFactoryClass::?$VectorClass` | 7 | `0x007E9FA4` |
| `PAVFactoryClass::?$VectorClass` | — | 7 | `0x007E9FC4` |
| `PAVFileEntryClass::?$DynamicVectorClass` | `PAVFileEntryClass::?$VectorClass` | 7 | `0x007ED30C` |
| `PAVFileEntryClass::?$VectorClass` | — | 7 | `0x007ED32C` |
| `PAVFoggedObjectClass::?$DynamicVectorClass` | `PAVFoggedObjectClass::?$VectorClass` | 7 | `0x007E44F4` |
| `PAVFoggedObjectClass::?$VectorClass` | — | 7 | `0x007E4514` |
| `PAVFootClass::?$DynamicVectorClass` | `PAVFootClass::?$VectorClass` | 7 | `0x007E8C28` |
| `PAVFootClass::?$VectorClass` | — | 7 | `0x007E8C48` |
| `PAVGraphicMenuItem::?$DynamicVectorClass` | `PAVGraphicMenuItem::?$VectorClass` | 7 | `0x007EA604` |
| `PAVGraphicMenuItem::?$VectorClass` | — | 7 | `0x007EA624` |
| `PAVGraphicMenuItem::V?$DynamicVectorClass::PAVGraphicMenuItem::?$VectorCursor` | — | 4 | `0x007EA644` |
| `PAVHouseClass::?$DynamicVectorClass` | `PAVHouseClass::?$VectorClass` | 7 | `0x007E9EE4` |
| `PAVHouseClass::?$VectorClass` | — | 7 | `0x007E9F04` |
| `PAVHouseTypeClass::?$DynamicVectorClass` | `PAVHouseTypeClass::?$VectorClass` | 7 | `0x007EA064` |
| `PAVHouseTypeClass::?$VectorClass` | — | 7 | `0x007EA084` |
| `PAVInfantryClass::?$DynamicVectorClass` | `PAVInfantryClass::?$VectorClass` | 7 | `0x007E43C8` |
| `PAVInfantryClass::?$VectorClass` | — | 7 | `0x007E43E8` |
| `PAVInfantryTypeClass::?$DynamicVectorClass` | `PAVInfantryTypeClass::?$VectorClass` | 7 | `0x007EA324` |
| `PAVInfantryTypeClass::?$VectorClass` | — | 7 | `0x007EA344` |
| `PAVIonBlastClass::?$DynamicVectorClass` | `PAVIonBlastClass::?$VectorClass` | 7 | `0x007EC05C` |
| `PAVIonBlastClass::?$VectorClass` | — | 7 | `0x007EC07C` |
| `PAVIsometricTileClass::?$DynamicVectorClass` | `PAVIsometricTileClass::?$VectorClass` | 7 | `0x007E18BC` |
| `PAVIsometricTileClass::?$VectorClass` | — | 7 | `0x007E18DC` |
| `PAVIsometricTileTypeClass::?$DynamicVectorClass` | `PAVIsometricTileTypeClass::?$VectorClass` | 7 | `0x007EA3E4` |
| `PAVIsometricTileTypeClass::?$VectorClass` | — | 7 | `0x007EA404` |
| `PAVLaserDrawClass::?$DynamicVectorClass` | `PAVLaserDrawClass::?$VectorClass` | 7 | `0x007ECEDC` |
| `PAVLaserDrawClass::?$VectorClass` | — | 7 | `0x007ECEFC` |
| `PAVLightConvertClass::?$DynamicVectorClass` | `PAVLightConvertClass::?$VectorClass` | 7 | `0x007E186C` |
| `PAVLightConvertClass::?$VectorClass` | — | 7 | `0x007E188C` |
| `PAVLightSourceClass::?$DynamicVectorClass` | `PAVLightSourceClass::?$VectorClass` | 7 | `0x007ECF7C` |
| `PAVLightSourceClass::?$VectorClass` | — | 7 | `0x007ECF9C` |
| `PAVLineTrail::?$DynamicVectorClass` | `PAVLineTrail::?$VectorClass` | 7 | `0x007ED0CC` |
| `PAVLineTrail::?$VectorClass` | — | 7 | `0x007ED0EC` |
| `PAVMapRegionClass::?$DynamicVectorClass` | `PAVMapRegionClass::?$VectorClass` | 7 | `0x007ED858` |
| `PAVMapRegionClass::?$VectorClass` | — | 7 | `0x007ED878` |
| `PAVMapSelection::?$DynamicVectorClass` | `PAVMapSelection::?$VectorClass` | 7 | `0x007EEB14` |
| `PAVMapSelection::?$VectorClass` | — | 7 | `0x007EEBB4` |
| `PAVMapStage::?$DynamicVectorClass` | `PAVMapStage::?$VectorClass` | 7 | `0x007EEA94` |
| `PAVMapStage::?$VectorClass` | — | 7 | `0x007EEAB4` |
| `PAVMixFileClass::?$DynamicVectorClass` | `PAVMixFileClass::?$VectorClass` | 7 | `0x007E1A44` |
| `PAVMixFileClass::?$List` | `GenericList` | 1 | `0x007EDF38` |
| `PAVMixFileClass::?$VectorClass` | — | 7 | `0x007E1A64` |
| `PAVMovieHandle::?$DynamicVectorClass` | `PAVMovieHandle::?$VectorClass` | 7 | `0x007F6984` |
| `PAVMovieHandle::?$VectorClass` | — | 7 | `0x007F69A4` |
| `PAVMSAnim::?$DynamicVectorClass` | `PAVMSAnim::?$VectorClass` | 7 | `0x007EEC04` |
| `PAVMSAnim::?$VectorClass` | — | 7 | `0x007EEC24` |
| `PAVMSAnim::V?$DynamicVectorClass::PAVMSAnim::?$VectorCursor` | — | 4 | `0x007F72EC` |
| `PAVMSAnimEntry::?$DynamicVectorClass` | `PAVMSAnimEntry::?$VectorClass` | 7 | `0x007EEA74` |
| `PAVMSAnimEntry::?$VectorClass` | — | 7 | `0x007EEAD4` |
| `PAVMSSfx::?$DynamicVectorClass` | `PAVMSSfx::?$VectorClass` | 7 | `0x007EEBE4` |
| `PAVMSSfx::?$VectorClass` | — | 7 | `0x007EEC44` |
| `PAVMSSfxEntry::?$DynamicVectorClass` | `PAVMSSfxEntry::?$VectorClass` | 7 | `0x007EEA54` |
| `PAVMSSfxEntry::?$VectorClass` | — | 7 | `0x007EEAF4` |
| `PAVMSSfxEntry::V?$DynamicVectorClass::PAVMSSfxEntry::?$VectorCursor` | — | 4 | `0x007F72C4` |
| `PAVMSTextEntry::?$DynamicVectorClass` | `PAVMSTextEntry::?$VectorClass` | 7 | `0x007EEB34` |
| `PAVMSTextEntry::?$VectorClass` | — | 7 | `0x007EEB94` |
| `PAVMultiMission::?$DynamicVectorClass` | `PAVMultiMission::?$VectorClass` | 7 | `0x007F11F4` |
| `PAVMultiMission::?$VectorClass` | — | 7 | `0x007F1214` |
| `PAVMultiplayerGameMode::?$DynamicVectorClass` | `PAVMultiplayerGameMode::?$VectorClass` | 7 | `0x007EED20` |
| `PAVMultiplayerGameMode::?$VectorClass` | — | 7 | `0x007EED40` |
| `PAVMultiplayerTeam::?$DynamicVectorClass` | `PAVMultiplayerTeam::?$VectorClass` | 7 | `0x007EEE34` |
| `PAVMultiplayerTeam::?$VectorClass` | — | 7 | `0x007EEE54` |
| `PAVNeuronClass::?$VectorClass` | — | 7 | `0x007E3E54` |
| `PAVObjectClass::?$DynamicVectorClass` | `PAVObjectClass::?$VectorClass` | 7 | `0x007E4F64` |
| `PAVObjectClass::?$VectorClass` | — | 7 | `0x007E192C` |
| `PAVObjectTypeClass::?$DynamicVectorClass` | `PAVObjectTypeClass::?$VectorClass` | 7 | `0x007EF26C` |
| `PAVObjectTypeClass::?$VectorClass` | — | 7 | `0x007EF28C` |
| `PAVOverlayClass::?$DynamicVectorClass` | `PAVOverlayClass::?$VectorClass` | 7 | `0x007E9D24` |
| `PAVOverlayClass::?$VectorClass` | — | 7 | `0x007E9D44` |
| `PAVOverlayTypeClass::?$DynamicVectorClass` | `PAVOverlayTypeClass::?$VectorClass` | 7 | `0x007EA164` |
| `PAVOverlayTypeClass::?$VectorClass` | — | 7 | `0x007EA184` |
| `PAVParasiteClass::?$DynamicVectorClass` | `PAVParasiteClass::?$VectorClass` | 7 | `0x007EF824` |
| `PAVParasiteClass::?$VectorClass` | — | 7 | `0x007EF844` |
| `PAVParticleClass::?$DynamicVectorClass` | `PAVParticleClass::?$VectorClass` | 7 | `0x007E9D64` |
| `PAVParticleClass::?$VectorClass` | — | 7 | `0x007E9D84` |
| `PAVParticleSystemClass::?$DynamicVectorClass` | `PAVParticleSystemClass::?$VectorClass` | 7 | `0x007E9C64` |
| `PAVParticleSystemClass::?$VectorClass` | — | 7 | `0x007E9C84` |
| `PAVParticleSystemTypeClass::?$DynamicVectorClass` | `PAVParticleSystemTypeClass::?$VectorClass` | 7 | `0x007EA464` |
| `PAVParticleSystemTypeClass::?$VectorClass` | — | 7 | `0x007EA484` |
| `PAVParticleTypeClass::?$DynamicVectorClass` | `PAVParticleTypeClass::?$VectorClass` | 7 | `0x007EA424` |
| `PAVParticleTypeClass::?$VectorClass` | — | 7 | `0x007EA444` |
| `PAVPhoneEntryClass::?$DynamicVectorClass` | `PAVPhoneEntryClass::?$VectorClass` | 7 | `0x007F11B4` |
| `PAVPhoneEntryClass::?$VectorClass` | — | 7 | `0x007F1254` |
| `PAVPlanningBranchClass::?$DynamicVectorClass` | `PAVPlanningBranchClass::?$VectorClass` | 7 | `0x007EFEC4` |
| `PAVPlanningBranchClass::?$VectorClass` | — | 7 | `0x007EFF24` |
| `PAVPlanningMemberClass::?$DynamicVectorClass` | `PAVPlanningMemberClass::?$VectorClass` | 7 | `0x007EFEE4` |
| `PAVPlanningMemberClass::?$VectorClass` | — | 7 | `0x007EFF04` |
| `PAVPlanningNodeClass::?$DynamicVectorClass` | `PAVPlanningNodeClass::?$VectorClass` | 7 | `0x007EFE44` |
| `PAVPlanningNodeClass::?$VectorClass` | — | 7 | `0x007EFE64` |
| `PAVPlanningTokenClass::?$DynamicVectorClass` | `PAVPlanningTokenClass::?$VectorClass` | 7 | `0x007EFE84` |
| `PAVPlanningTokenClass::?$VectorClass` | — | 7 | `0x007EFEA4` |
| `PAVRadarEventClass::?$DynamicVectorClass` | `PAVRadarEventClass::?$VectorClass` | 7 | `0x007F0AAC` |
| `PAVRadarEventClass::?$VectorClass` | — | 7 | `0x007F0ACC` |
| `PAVRadBeam::?$DynamicVectorClass` | `PAVRadBeam::?$VectorClass` | 7 | `0x007F0484` |
| `PAVRadBeam::?$VectorClass` | — | 7 | `0x007F04A4` |
| `PAVRadSiteClass::?$DynamicVectorClass` | `PAVRadSiteClass::?$VectorClass` | 7 | `0x007F07A4` |
| `PAVRadSiteClass::?$VectorClass` | — | 7 | `0x007F07C4` |
| `PAVReestablish::?$DynamicVectorClass` | `PAVReestablish::?$VectorClass` | 7 | `0x007E4488` |
| `PAVReestablish::?$VectorClass` | — | 7 | `0x007E4468` |
| `PAVSchemeNode::VHashString::U?$HashObject::?$DynamicVectorClass` | `PAVSchemeNode::VHashString::U?$HashObject::?$VectorClass` | 7 | `0x007EF770` |
| `PAVSchemeNode::VHashString::U?$HashObject::?$VectorClass` | — | 7 | `0x007EF7D0` |
| `PAVScriptClass::?$DynamicVectorClass` | `PAVScriptClass::?$VectorClass` | 7 | `0x007E1B24` |
| `PAVScriptClass::?$VectorClass` | — | 7 | `0x007E1B44` |
| `PAVScriptTypeClass::?$DynamicVectorClass` | `PAVScriptTypeClass::?$VectorClass` | 7 | `0x007EA124` |
| `PAVScriptTypeClass::?$VectorClass` | — | 7 | `0x007EA144` |
| `PAVShadowControlClass::?$DynamicVectorClass` | `PAVShadowControlClass::?$VectorClass` | 7 | `0x007F42DC` |
| `PAVShadowControlClass::?$VectorClass` | — | 7 | `0x007F42FC` |
| `PAVSideClass::?$DynamicVectorClass` | `PAVSideClass::?$VectorClass` | 7 | `0x007EA024` |
| `PAVSideClass::?$VectorClass` | — | 7 | `0x007EA044` |
| `PAVSlaveManagerClass::?$DynamicVectorClass` | `PAVSlaveManagerClass::?$VectorClass` | 7 | `0x007F315C` |
| `PAVSlaveManagerClass::?$VectorClass` | — | 7 | `0x007F317C` |
| `PAVSmudgeClass::?$DynamicVectorClass` | `PAVSmudgeClass::?$VectorClass` | 7 | `0x007E9DA4` |
| `PAVSmudgeClass::?$VectorClass` | — | 7 | `0x007E9DC4` |
| `PAVSmudgeTypeClass::?$DynamicVectorClass` | `PAVSmudgeTypeClass::?$VectorClass` | 7 | `0x007EA1A4` |
| `PAVSmudgeTypeClass::?$VectorClass` | — | 7 | `0x007EA1C4` |
| `PAVSpawnManagerClass::?$DynamicVectorClass` | `PAVSpawnManagerClass::?$VectorClass` | 7 | `0x007F35E4` |
| `PAVSpawnManagerClass::?$VectorClass` | — | 7 | `0x007F3604` |
| `PAVSpotLightClass::?$DynamicVectorClass` | `PAVSpotLightClass::?$VectorClass` | 7 | `0x007EF6BC` |
| `PAVSpotLightClass::?$VectorClass` | — | 7 | `0x007EF6DC` |
| `PAVSubTitle::?$DynamicVectorClass` | `PAVSubTitle::?$VectorClass` | 7 | `0x007F3F6C` |
| `PAVSubTitle::?$VectorClass` | — | 7 | `0x007F3F8C` |
| `PAVSuperClass::?$DynamicVectorClass` | `PAVSuperClass::?$VectorClass` | 7 | `0x007EA4E4` |
| `PAVSuperClass::?$VectorClass` | — | 7 | `0x007EA504` |
| `PAVSuperWeaponTypeClass::?$DynamicVectorClass` | `PAVSuperWeaponTypeClass::?$VectorClass` | 7 | `0x007EA4A4` |
| `PAVSuperWeaponTypeClass::?$VectorClass` | — | 7 | `0x007EA4C4` |
| `PAVTActionClass::?$DynamicVectorClass` | `PAVTActionClass::?$VectorClass` | 7 | `0x007F43D0` |
| `PAVTActionClass::?$VectorClass` | — | 7 | `0x007F43F0` |
| `PAVTagClass::?$DynamicVectorClass` | `PAVTagClass::?$VectorClass` | 7 | `0x007EA5A4` |
| `PAVTagClass::?$VectorClass` | — | 7 | `0x007EA5C4` |
| `PAVTagTypeClass::?$DynamicVectorClass` | `PAVTagTypeClass::?$VectorClass` | 7 | `0x007F4558` |
| `PAVTagTypeClass::?$VectorClass` | — | 7 | `0x007F4578` |
| `PAVTaskForceClass::?$DynamicVectorClass` | `PAVTaskForceClass::?$VectorClass` | 7 | `0x007EA0A4` |
| `PAVTaskForceClass::?$VectorClass` | — | 7 | `0x007EA0C4` |
| `PAVTeamClass::?$DynamicVectorClass` | `PAVTeamClass::?$VectorClass` | 7 | `0x007E9F64` |
| `PAVTeamClass::?$VectorClass` | — | 7 | `0x007E9F84` |
| `PAVTeamTypeClass::?$DynamicVectorClass` | `PAVTeamTypeClass::?$VectorClass` | 7 | `0x007EA0E4` |
| `PAVTeamTypeClass::?$VectorClass` | — | 7 | `0x007EA104` |
| `PAVTechnoClass::?$DynamicVectorClass` | `PAVTechnoClass::?$VectorClass` | 7 | `0x007E17AC` |
| `PAVTechnoClass::?$VectorClass` | — | 7 | `0x007E180C` |
| `PAVTechnoClass::URadarTrackingStruct::U?$HashObject::?$DynamicVectorClass` | `PAVTechnoClass::URadarTrackingStruct::U?$HashObject::?$VectorClass` | 7 | `0x007F042C` |
| `PAVTechnoClass::URadarTrackingStruct::U?$HashObject::?$VectorClass` | — | 7 | `0x007F044C` |
| `PAVTechnoTypeClass::?$DynamicVectorClass` | `PAVTechnoTypeClass::?$VectorClass` | 7 | `0x007E858C` |
| `PAVTechnoTypeClass::?$TypeList` | `PAVTechnoTypeClass::?$DynamicVectorClass` | 7 | `0x007E4E18` |
| `PAVTechnoTypeClass::?$VectorClass` | — | 7 | `0x007E4DF8` |
| `PAVTemporalClass::?$DynamicVectorClass` | `PAVTemporalClass::?$VectorClass` | 7 | `0x007F5114` |
| `PAVTemporalClass::?$VectorClass` | — | 7 | `0x007F5134` |
| `PAVTerrainClass::?$DynamicVectorClass` | `PAVTerrainClass::?$VectorClass` | 7 | `0x007E9DE4` |
| `PAVTerrainClass::?$VectorClass` | — | 7 | `0x007E9E04` |
| `PAVTerrainTypeClass::?$DynamicVectorClass` | `PAVTerrainTypeClass::?$VectorClass` | 7 | `0x007EA1E4` |
| `PAVTerrainTypeClass::?$VectorClass` | — | 7 | `0x007EA204` |
| `PAVTEventClass::?$DynamicVectorClass` | `PAVTEventClass::?$VectorClass` | 7 | `0x007F550C` |
| `PAVTEventClass::?$VectorClass` | — | 7 | `0x007F552C` |
| `PAVTiberiumClass::?$DynamicVectorClass` | `PAVTiberiumClass::?$VectorClass` | 7 | `0x007F56BC` |
| `PAVTiberiumClass::?$VectorClass` | — | 7 | `0x007F56DC` |
| `PAVTriggerClass::?$DynamicVectorClass` | `PAVTriggerClass::?$VectorClass` | 7 | `0x007E9BE4` |
| `PAVTriggerClass::?$VectorClass` | — | 7 | `0x007E9C04` |
| `PAVTriggerTypeClass::?$DynamicVectorClass` | `PAVTriggerTypeClass::?$VectorClass` | 7 | `0x007E9BA4` |
| `PAVTriggerTypeClass::?$VectorClass` | — | 7 | `0x007E9BC4` |
| `PAVTubeClass::?$DynamicVectorClass` | `PAVTubeClass::?$VectorClass` | 7 | `0x007E9CA4` |
| `PAVTubeClass::?$VectorClass` | — | 7 | `0x007E9CC4` |
| `PAVUnitClass::?$DynamicVectorClass` | `PAVUnitClass::?$VectorClass` | 7 | `0x007E9EA4` |
| `PAVUnitClass::?$VectorClass` | — | 7 | `0x007E9EC4` |
| `PAVUnitTypeClass::?$DynamicVectorClass` | `PAVUnitTypeClass::?$VectorClass` | 7 | `0x007EA2A4` |
| `PAVUnitTypeClass::?$VectorClass` | — | 7 | `0x007EA2C4` |
| `PAVVeinholeMonsterClass::?$DynamicVectorClass` | `PAVVeinholeMonsterClass::?$VectorClass` | 7 | `0x007F663C` |
| `PAVVeinholeMonsterClass::?$VectorClass` | — | 7 | `0x007F665C` |
| `PAVVocClass::?$DynamicVectorClass` | `PAVVocClass::?$VectorClass` | 7 | `0x007F68AC` |
| `PAVVocClass::?$VectorClass` | — | 7 | `0x007F68CC` |
| `PAVVoxClass::?$DynamicVectorClass` | `PAVVoxClass::?$VectorClass` | 7 | `0x007F6904` |
| `PAVVoxClass::?$VectorClass` | — | 7 | `0x007F6924` |
| `PAVVoxelAnimClass::?$DynamicVectorClass` | `PAVVoxelAnimClass::?$VectorClass` | 7 | `0x007E1E2C` |
| `PAVVoxelAnimClass::?$VectorClass` | — | 7 | `0x007E1E4C` |
| `PAVVoxelAnimTypeClass::?$DynamicVectorClass` | `PAVVoxelAnimTypeClass::?$VectorClass` | 7 | `0x007EA3A4` |
| `PAVVoxelAnimTypeClass::?$VectorClass` | — | 7 | `0x007EA3C4` |
| `PAVWarheadTypeClass::?$DynamicVectorClass` | `PAVWarheadTypeClass::?$VectorClass` | 7 | `0x007E1E84` |
| `PAVWarheadTypeClass::?$VectorClass` | — | 7 | `0x007E1EA4` |
| `PAVWaveClass::?$DynamicVectorClass` | `PAVWaveClass::?$VectorClass` | 7 | `0x007E9CE4` |
| `PAVWaveClass::?$VectorClass` | — | 7 | `0x007E9D04` |
| `PAVWaypointPathClass::?$DynamicVectorClass` | `PAVWaypointPathClass::?$VectorClass` | 7 | `0x007F6E04` |
| `PAVWaypointPathClass::?$VectorClass` | — | 7 | `0x007F6E24` |
| `PAVWeaponTypeClass::?$DynamicVectorClass` | `PAVWeaponTypeClass::?$VectorClass` | 7 | `0x007E1ED4` |
| `PAVWeaponTypeClass::?$VectorClass` | — | 7 | `0x007E1EF4` |
| `PBD::?$DynamicVectorClass` | `PBD::?$VectorClass` | 7 | `0x007EE0B4` |
| `PBD::?$VectorClass` | — | 7 | `0x007EE0D4` |
| `PBG::?$DynamicVectorClass` | `PBG::?$VectorClass` | 7 | `0x007ED1DC` |
| `PBG::?$VectorClass` | — | 7 | `0x007ED1FC` |
| `PBVAircraftTypeClass::?$DynamicVectorClass` | `PBVAircraftTypeClass::?$VectorClass` | 7 | `0x007EACC8` |
| `PBVAircraftTypeClass::?$TypeList` | `PBVAircraftTypeClass::?$DynamicVectorClass` | 7 | `0x007EABC8` |
| `PBVAircraftTypeClass::?$VectorClass` | — | 7 | `0x007EAC68` |
| `PBVAnimClass::?$DynamicVectorClass` | `PBVAnimClass::?$VectorClass` | 7 | `0x007EBFCC` |
| `PBVAnimClass::?$VectorClass` | — | 7 | `0x007EBFEC` |
| `PBVAnimTypeClass::?$DynamicVectorClass` | `PBVAnimTypeClass::?$VectorClass` | 7 | `0x007EB714` |
| `PBVAnimTypeClass::?$TypeList` | `PBVAnimTypeClass::?$DynamicVectorClass` | 7 | `0x007EB6D4` |
| `PBVAnimTypeClass::?$VectorClass` | — | 7 | `0x007EB6F4` |
| `PBVBuildingTypeClass::?$DynamicVectorClass` | `PBVBuildingTypeClass::?$VectorClass` | 7 | `0x007EAA28` |
| `PBVBuildingTypeClass::?$TypeList` | `PBVBuildingTypeClass::?$DynamicVectorClass` | 7 | `0x007ED90C` |
| `PBVBuildingTypeClass::?$VectorClass` | — | 7 | `0x007EAA08` |
| `PBVCommandClass::?$DynamicVectorClass` | `PBVCommandClass::?$VectorClass` | 7 | `0x007E182C` |
| `PBVCommandClass::?$VectorClass` | — | 7 | `0x007E184C` |
| `PBVInfantryTypeClass::?$DynamicVectorClass` | `PBVInfantryTypeClass::?$VectorClass` | 7 | `0x007EAC88` |
| `PBVInfantryTypeClass::?$TypeList` | `PBVInfantryTypeClass::?$DynamicVectorClass` | 7 | `0x007EAC08` |
| `PBVInfantryTypeClass::?$VectorClass` | — | 7 | `0x007EAC28` |
| `PBVMultiMission::?$DynamicVectorClass` | `PBVMultiMission::?$VectorClass` | 7 | `0x007EEF70` |
| `PBVMultiMission::?$VectorClass` | — | 7 | `0x007EEF50` |
| `PBVParticleSystemTypeClass::?$DynamicVectorClass` | `PBVParticleSystemTypeClass::?$VectorClass` | 7 | `0x007E4444` |
| `PBVParticleSystemTypeClass::?$TypeList` | `PBVParticleSystemTypeClass::?$DynamicVectorClass` | 7 | `0x007F4F9C` |
| `PBVParticleSystemTypeClass::?$VectorClass` | — | 7 | `0x007E4424` |
| `PBVSmudgeTypeClass::?$DynamicVectorClass` | `PBVSmudgeTypeClass::?$VectorClass` | 7 | `0x007F0DEC` |
| `PBVSmudgeTypeClass::?$TypeList` | `PBVSmudgeTypeClass::?$DynamicVectorClass` | 7 | `0x007F0D1C` |
| `PBVSmudgeTypeClass::?$VectorClass` | — | 7 | `0x007F0D7C` |
| `PBVTeamTypeClass::?$DynamicVectorClass` | `PBVTeamTypeClass::?$VectorClass` | 7 | `0x007EAAE8` |
| `PBVTeamTypeClass::?$TypeList` | `PBVTeamTypeClass::?$DynamicVectorClass` | 7 | `0x007EA9C4` |
| `PBVTeamTypeClass::?$VectorClass` | — | 7 | `0x007EA9E4` |
| `PBVTechnoTypeClass::?$DynamicVectorClass` | `PBVTechnoTypeClass::?$VectorClass` | 7 | `0x007E8934` |
| `PBVTechnoTypeClass::?$VectorClass` | — | 7 | `0x007E8954` |
| `PBVTerrainTypeClass::?$DynamicVectorClass` | `PBVTerrainTypeClass::?$VectorClass` | 7 | `0x007F0E0C` |
| `PBVTerrainTypeClass::?$TypeList` | `PBVTerrainTypeClass::?$DynamicVectorClass` | 7 | `0x007F0CFC` |
| `PBVTerrainTypeClass::?$VectorClass` | — | 7 | `0x007F0D9C` |
| `PBVToolTip::?$DynamicVectorClass` | `PBVToolTip::?$VectorClass` | 7 | `0x007F57C8` |
| `PBVToolTip::?$VectorClass` | — | 7 | `0x007F57E8` |
| `PBVUnitTypeClass::?$DynamicVectorClass` | `PBVUnitTypeClass::?$VectorClass` | 7 | `0x007EACA8` |
| `PBVUnitTypeClass::?$TypeList` | `PBVUnitTypeClass::?$DynamicVectorClass` | 7 | `0x007EABE8` |
| `PBVUnitTypeClass::?$VectorClass` | — | 7 | `0x007EAC48` |
| `PBVVoxelAnimTypeClass::?$DynamicVectorClass` | `PBVVoxelAnimTypeClass::?$VectorClass` | 7 | `0x007F0DCC` |
| `PBVVoxelAnimTypeClass::?$TypeList` | `PBVVoxelAnimTypeClass::?$DynamicVectorClass` | 7 | `0x007F0D3C` |
| `PBVVoxelAnimTypeClass::?$VectorClass` | — | 7 | `0x007F0D5C` |
| `Pipe` | — | 5 | `0x007E6218` |
| `PixelFXClass` | — | 1 | `0x007EFDA4` |
| `PKPipe` | `Pipe` | 6 | `0x007EFDAC` |
| `PKStraw` | `Straw` | 4 | `0x007EFDE0` |
| `PlanningModeCommandClass` | `CommandClass` | 9 | `0x007EB9B4` |
| `PlayerProfile` | `ReferenceCounted` | 3 | `0x007F74F4` |
| `PowerClass` | `RadarClass` | 54 | `0x007EFF54` |
| `PrevObjectCommandClass` | `CommandClass` | 9 | `0x007EBA04` |
| `ProgressScreenClass` | `INoticeSource` | 1 | `0x007F0064` |
| `RadarClass` | `DisplayClass` | 54 | `0x007F0344` |
| `RadarClass::RTacticalClass` | `GadgetClass` | 33 | `0x007F02BC` |
| `RadioClass` | `MissionClass` | 161 | `0x007F0508` |
| `RadSiteClass` | `AbstractClass` | 24 | `0x007F0810` |
| `RAMFileClass` | `FileClass` | 17 | `0x007F0874` |
| `RandomStraw` | `Straw` | 3 | `0x007F0AFC` |
| `RawFileClass` | `FileClass` | 17 | `0x007F0904` |
| `rc_ptr_base` | — | 1 | `0x007F094C` |
| `ReferenceCounted` | — | 3 | `0x007F0954` |
| `RLEBlitter` | — | 3 | `0x007E5BA0` |
| `RocketLocomotionClass` | `LocomotionClass` | 10 | `0x007F0BE8` |
| `ScatterCommandClass` | `CommandClass` | 9 | `0x007EBACC` |
| `ScoreAnimClass` | — | 4 | `0x007F0EDC` |
| `ScoreBigFontClass` | `ScoreFontClass` | 5 | `0x007F0F20` |
| `ScoreFontClass` | — | 5 | `0x007F0EF0` |
| `ScoreFullFontClass` | `ScoreFontClass` | 5 | `0x007F0F08` |
| `ScorePrintClass` | `ScoreAnimClass` | 4 | `0x007F0EB4` |
| `ScoreTimeClass` | `ScoreAnimClass` | 4 | `0x007F0EC8` |
| `ScreenCaptureCommandClass` | `CommandClass` | 9 | `0x007EBF04` |
| `ScriptClass` | `AbstractClass` | 24 | `0x007F0F78` |
| `ScriptTypeClass` | `AbstractTypeClass` | 27 | `0x007F1008` |
| `ScrollClass` | `TabClass` | 55 | `0x007F1094` |
| `SelectTeamCommandClass` | `CommandClass` | 9 | `0x007EBE64` |
| `SetDefenseTabCommandClass` | `CommandClass` | 9 | `0x007EB8C4` |
| `SetInfantryTabCommandClass` | `CommandClass` | 9 | `0x007EB874` |
| `SetStructureTabCommandClass` | `CommandClass` | 9 | `0x007EB8EC` |
| `SetUnitTabCommandClass` | `CommandClass` | 9 | `0x007EB89C` |
| `SetView1CommandClass` | `CommandClass` | 9 | `0x007EBCFC` |
| `SetView2CommandClass` | `CommandClass` | 9 | `0x007EBCD4` |
| `SetView3CommandClass` | `CommandClass` | 9 | `0x007EBCAC` |
| `SetView4CommandClass` | `CommandClass` | 9 | `0x007EBC84` |
| `ShapeButtonClass` | `ToggleClass` | 35 | `0x007E8088` |
| `SHAPipe` | `Pipe` | 5 | `0x007E4D78` |
| `ShipLocomotionClass` | `LocomotionClass` | 10 | `0x007F2E58` |
| `SidebarClass` | `PowerClass` | 55 | `0x007F3058` |
| `SidebarClass::SBGadgetClass` | `GadgetClass` | 33 | `0x007F2F44` |
| `SidebarClass::StripClass::SelectClass` | `ControlClass` | 34 | `0x007F2FCC` |
| `SidebarDownCommandClass` | `CommandClass` | 9 | `0x007EBC0C` |
| `SidebarUpCommandClass` | `CommandClass` | 9 | `0x007EBC34` |
| `SideClass` | `AbstractTypeClass` | 27 | `0x007F2EC0` |
| `SimpleWonlineDialogControl` | `OwnerDraw::SimpleDialogControl` | 5 | `0x007F7624` |
| `SlaveManagerClass` | `AbstractClass` | 24 | `0x007F31C8` |
| `SlaveManagerClass::PAUSlaveControl::?$DynamicVectorClass` | `SlaveManagerClass::PAUSlaveControl::?$VectorClass` | 7 | `0x007F322C` |
| `SlaveManagerClass::PAUSlaveControl::?$VectorClass` | — | 7 | `0x007F324C` |
| `SliderClass` | `GaugeClass` | 45 | `0x007ED21C` |
| `SmudgeClass` | `ObjectClass` | 122 | `0x007F32FC` |
| `SmudgeTypeClass` | `ObjectTypeClass` | 41 | `0x007F3528` |
| `SpawnManagerClass` | `AbstractClass` | 24 | `0x007F3650` |
| `SpawnManagerClass::PAUSpawnControl::?$DynamicVectorClass` | `SpawnManagerClass::PAUSpawnControl::?$VectorClass` | 7 | `0x007F36B4` |
| `SpawnManagerClass::PAUSpawnControl::?$VectorClass` | — | 7 | `0x007F36D4` |
| `StaticButtonClass` | `GadgetClass` | 36 | `0x007F3EA0` |
| `StopCommandClass` | `CommandClass` | 9 | `0x007EBA7C` |
| `Straw` | — | 3 | `0x007E61F0` |
| `SuperClass` | `AbstractClass` | 24 | `0x007F3FE8` |
| `SuperWeaponTypeClass` | `AbstractTypeClass` | 28 | `0x007F4090` |
| `Surface` | — | 34 | `0x007E2198` |
| `SwizzleManagerClass` | `ISwizzle` | 10 | `0x007F4108` |
| `TabClass` | `SidebarClass` | 55 | `0x007EDFB4` |
| `Tactical` | `AbstractClass` | 25 | `0x007F4348` |
| `TActionClass` | `AbstractClass` | 24 | `0x007F443C` |
| `TagClass` | `AbstractClass` | 24 | `0x007F44E0` |
| `TagTypeClass` | `AbstractTypeClass` | 27 | `0x007F45C4` |
| `TaskForceClass` | `AbstractTypeClass` | 27 | `0x007F4680` |
| `TauntCommandClass` | `CommandClass` | 9 | `0x007EBEDC` |
| `TeamClass` | `AbstractClass` | 24 | `0x007F4730` |
| `TeamTypeClass` | `AbstractTypeClass` | 27 | `0x007F47D0` |
| `TechnoClass` | `RadioClass` | 309 | `0x007F4960` |
| `TechnoTypeClass` | `ObjectTypeClass` | 48 | `0x007F4ED8` |
| `TeleportLocomotionClass` | `LocomotionClass` | 12 | `0x007F50CC` |
| `TemporalClass` | `AbstractClass` | 24 | `0x007F5180` |
| `TerrainClass` | `ObjectClass` | 122 | `0x007F522C` |
| `TerrainTypeClass` | `ObjectTypeClass` | 40 | `0x007F5458` |
| `TEventClass` | `AbstractClass` | 24 | `0x007F5578` |
| `TextButtonClass` | `ToggleClass` | 38 | `0x007F55DC` |
| `TextLabelClass` | `GadgetClass` | 34 | `0x007F5B44` |
| `TiberianSunClassFactory` | `IClassFactory` | 5 | `0x007EA564` |
| `TiberiumClass` | `AbstractTypeClass` | 27 | `0x007F5728` |
| `ToggleClass` | `ControlClass` | 34 | `0x007E8118` |
| `ToggleRepairCommandClass` | `CommandClass` | 9 | `0x007EBB6C` |
| `ToggleSellCommandClass` | `CommandClass` | 9 | `0x007EBB94` |
| `ToolTipManager` | — | 6 | `0x007F57AC` |
| `TriColorGaugeClass` | `GaugeClass` | 44 | `0x007E9430` |
| `TriggerClass` | `AbstractClass` | 24 | `0x007F5858` |
| `TriggerTypeClass` | `AbstractTypeClass` | 27 | `0x007F5904` |
| `TubeClass` | `AbstractClass` | 24 | `0x007F59B0` |
| `TunnelLocomotionClass` | `LocomotionClass` | 10 | `0x007F5AF0` |
| `TypeSelectCommandClass` | `CommandClass` | 9 | `0x007EB964` |
| `UAcceleratorTracker::?$DynamicVectorClass` | `UAcceleratorTracker::?$VectorClass` | 7 | `0x007EECCC` |
| `UAcceleratorTracker::?$VectorClass` | — | 7 | `0x007EECEC` |
| `UAngerStruct::?$DynamicVectorClass` | `UAngerStruct::?$VectorClass` | 7 | `0x007EA924` |
| `UAngerStruct::?$VectorClass` | — | 7 | `0x007EA984` |
| `UDirtyAreaStruct::?$DynamicVectorClass` | `UDirtyAreaStruct::?$VectorClass` | 7 | `0x007F429C` |
| `UDirtyAreaStruct::?$VectorClass` | — | 7 | `0x007F42BC` |
| `UDPInterfaceClass` | `WinsockInterfaceClass` | 31 | `0x007F7A6C` |
| `UnholyAlliance` | `MultiplayerGameMode` | 52 | `0x007EE814` |
| `UnitClass` | `FootClass` | 344 | `0x007F5C70` |
| `UnitTypeClass` | `TechnoTypeClass` | 48 | `0x007F6218` |
| `UScoutStruct::?$DynamicVectorClass` | `UScoutStruct::?$VectorClass` | 7 | `0x007EA904` |
| `UScoutStruct::?$VectorClass` | — | 7 | `0x007EA9A4` |
| `USubzoneConnectionStruct::?$DynamicVectorClass` | `USubzoneConnectionStruct::?$VectorClass` | 7 | `0x007ED5A0` |
| `USubzoneConnectionStruct::?$VectorClass` | — | 7 | `0x007E177C` |
| `USubzoneTrackingStruct::?$DynamicVectorClass` | `USubzoneTrackingStruct::?$VectorClass` | 7 | `0x007ED4A0` |
| `USubzoneTrackingStruct::?$VectorClass` | — | 7 | `0x007ED500` |
| `UtagCONNECTDATA::?$DynamicVectorClass` | `UtagCONNECTDATA::?$VectorClass` | 7 | `0x007E5CC4` |
| `UtagCONNECTDATA::?$VectorClass` | — | 7 | `0x007E5C84` |
| `UUndoInfoStruct::?$DynamicVectorClass` | `UUndoInfoStruct::?$VectorClass` | 7 | `0x007F327C` |
| `UUndoInfoStruct::?$VectorClass` | — | 7 | `0x007F329C` |
| `UZoneConnectionClass::?$DynamicVectorClass` | `UZoneConnectionClass::?$VectorClass` | 7 | `0x007ED4C0` |
| `UZoneConnectionClass::?$VectorClass` | — | 7 | `0x007ED4E0` |
| `VAircraftClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3BE8` |
| `VAircraftTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3B28` |
| `VAirstrikeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3900` |
| `VAITriggerTypeClass::?$DiscreteDistributionClass::PAVAITriggerTypeClass::V?$DistributionObject::?$DynamicVectorClass` | `VAITriggerTypeClass::?$DiscreteDistributionClass::PAVAITriggerTypeClass::V?$DistributionObject::?$VectorClass` | 7 | `0x007F4860` |
| `VAITriggerTypeClass::?$DiscreteDistributionClass::PAVAITriggerTypeClass::V?$DistributionObject::?$VectorClass` | — | 7 | `0x007F4840` |
| `VAITriggerTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3E40` |
| `VAlphaShapeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3E88` |
| `VAnimClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3C18` |
| `VAnimTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3C30` |
| `VBaseNodeClass::?$DynamicVectorClass` | `VBaseNodeClass::?$VectorClass` | 7 | `0x007E38B0` |
| `VBaseNodeClass::?$VectorClass` | — | 7 | `0x007E38F0` |
| `VBombClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3990` |
| `VBuildingClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3BD0` |
| `VBuildingLightClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F38B8` |
| `VBuildingTypeClass::?$DiscreteDistributionClass::PAVBuildingTypeClass::V?$DistributionObject::?$DynamicVectorClass` | `VBuildingTypeClass::?$DiscreteDistributionClass::PAVBuildingTypeClass::V?$DistributionObject::?$VectorClass` | 7 | `0x007EAAC4` |
| `VBuildingTypeClass::?$DiscreteDistributionClass::PAVBuildingTypeClass::V?$DistributionObject::?$VectorClass` | — | 7 | `0x007EAAA4` |
| `VBuildingTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3B10` |
| `VBulletClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3D80` |
| `VBulletTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3B58` |
| `VCampaignClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F38A0` |
| `VCaptureManagerClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3948` |
| `VCell::?$DynamicVectorClass` | `VCell::?$VectorClass` | 7 | `0x007E3890` |
| `VCell::?$VectorClass` | — | 7 | `0x007E38D0` |
| `VCellClass::?$DiscreteDistributionClass::PAVCellClass::V?$DistributionObject::?$DynamicVectorClass` | `VCellClass::?$DiscreteDistributionClass::PAVCellClass::V?$DistributionObject::?$VectorClass` | 7 | `0x007E928C` |
| `VCellClass::?$DiscreteDistributionClass::PAVCellClass::V?$DistributionObject::?$VectorClass` | — | 7 | `0x007E9264` |
| `VCellClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3810` |
| `VCStreamClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3768` |
| `VDiskLaserClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3960` |
| `VDriveLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3C78` |
| `VDropPodLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3D08` |
| `VeinholeMonsterClass` | `ObjectClass` | 122 | `0x007F66A8` |
| `VEMPulseClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3828` |
| `VersionClass` | — | 1 | `0x007EA57C` |
| `VeterancyNavCommandClass` | `CommandClass` | 9 | `0x007EB914` |
| `VFactoryClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3D98` |
| `VFlyLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3D20` |
| `VFoggedObjectClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3E70` |
| `VHouseClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3C60` |
| `VHouseTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3C48` |
| `VHoverLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3CA8` |
| `VHSVClass::?$DynamicVectorClass` | `VHSVClass::?$VectorClass` | 7 | `0x007EF750` |
| `VHSVClass::?$VectorClass` | — | 7 | `0x007EF7F0` |
| `View1CommandClass` | `CommandClass` | 9 | `0x007EBD9C` |
| `View2CommandClass` | `CommandClass` | 9 | `0x007EBD74` |
| `View3CommandClass` | `CommandClass` | 9 | `0x007EBD4C` |
| `View4CommandClass` | `CommandClass` | 9 | `0x007EBD24` |
| `VInfantryClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3C00` |
| `VInfantryTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3B40` |
| `VIsometricTileTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3B70` |
| `VJumpjetLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3C90` |
| `VLightSourceClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3840` |
| `VMechLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3D50` |
| `VNeuronClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3E58` |
| `VOverlayTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3B88` |
| `VoxelAnimClass` | `ObjectClass` | 122 | `0x007F6318` |
| `VoxelAnimTypeClass` | `ObjectTypeClass` | 40 | `0x007F6548` |
| `VParasiteClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3978` |
| `VParticleClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3DE0` |
| `VParticleSystemClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3E10` |
| `VParticleSystemTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3E28` |
| `VParticleTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3DF8` |
| `VPlayerProfile::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F3F44` |
| `VPoint2D::?$DynamicVectorClass` | `VPoint2D::?$VectorClass` | 7 | `0x007EEB54` |
| `VPoint2D::?$VectorClass` | — | 7 | `0x007EEB74` |
| `VQMovieHandle` | `MovieHandle` | 11 | `0x007EE0F4` |
| `VRadSiteClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F39A8` |
| `VRGBClass::?$DynamicVectorClass` | `VRGBClass::?$VectorClass` | 7 | `0x007F022C` |
| `VRGBClass::?$TypeList` | `VRGBClass::?$DynamicVectorClass` | 7 | `0x007E4E58` |
| `VRGBClass::?$VectorClass` | — | 7 | `0x007E4E38` |
| `VRocketLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3CC0` |
| `VScriptClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3A50` |
| `VScriptTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3A68` |
| `VShipLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3D68` |
| `VSideClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3858` |
| `VSlaveManagerClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3930` |
| `VSmudgeTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3BA0` |
| `VSpawnManagerClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3918` |
| `VSuperClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F37E0` |
| `VSuperWeaponTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F37C8` |
| `VSwizzlePointerClass::?$DynamicVectorClass` | `VSwizzlePointerClass::?$VectorClass` | 7 | `0x007F4134` |
| `VSwizzlePointerClass::?$VectorClass` | — | 7 | `0x007F4154` |
| `VTactical::?$TClassFactory` | `IClassFactory` | 5 | `0x007F37F8` |
| `VTActionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3A08` |
| `VTagClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3A80` |
| `VTagTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3A98` |
| `VTaskForceClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3AE0` |
| `VTeamClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3AB0` |
| `VTeamTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3AC8` |
| `VTeleportLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3D38` |
| `VTemporalClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F38E8` |
| `VTerrainClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F37B0` |
| `VTerrainTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3798` |
| `VTEventClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F39C0` |
| `VTiberiumClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3870` |
| `VTriggerClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3A20` |
| `VTriggerTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3A38` |
| `VTubeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3888` |
| `VTunnelLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3CD8` |
| `VUnitClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3BB8` |
| `VUnitTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3AF8` |
| `VVoxelAnimClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F39F0` |
| `VVoxelAnimTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F39D8` |
| `VWalkLocomotionClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3CF0` |
| `VWarheadTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3DB0` |
| `VWaveClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3780` |
| `VWaypointClass::?$DynamicVectorClass` | `VWaypointClass::?$VectorClass` | 7 | `0x007F6ED4` |
| `VWaypointClass::?$VectorClass` | — | 7 | `0x007F6EF4` |
| `VWaypointPathClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F38D0` |
| `VWDTState::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F70EC` |
| `VWDTTerritory::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007EBFA4` |
| `VWDTTerritory::V?$rc_ptr::?$DynamicVectorClass` | `VWDTTerritory::V?$rc_ptr::?$VectorClass` | 7 | `0x007F7260` |
| `VWDTTerritory::V?$rc_ptr::?$VectorClass` | — | 7 | `0x007F7230` |
| `VWeaponTypeClass::?$TClassFactory` | `IClassFactory` | 5 | `0x007F3DC8` |
| `VWstring::?$DynamicVectorClass` | `VWstring::?$VectorClass` | 7 | `0x007F12B4` |
| `VWstring::?$VectorClass` | — | 7 | `0x007F1294` |
| `W4DiskID::?$TypeList` | `W4DiskID::?$DynamicVectorClass` | 7 | `0x007F12D4` |
| `W4DiskID::?$VectorClass` | — | 7 | `0x007F1274` |
| `W4PassabilityType::?$DynamicVectorClass` | `W4PassabilityType::?$VectorClass` | 7 | `0x007ED580` |
| `W4PassabilityType::?$VectorClass` | — | 7 | `0x007ED560` |
| `WalkLocomotionClass` | `LocomotionClass` | 10 | `0x007F6AC4` |
| `WarheadTypeClass` | `AbstractTypeClass` | 27 | `0x007F6B30` |
| `WaveClass` | `ObjectClass` | 122 | `0x007F6BF4` |
| `WaypointPathClass` | `AbstractClass` | 24 | `0x007F6E70` |
| `WDTState` | `ReferenceCounted` | 3 | `0x007F7250` |
| `WDTTerritory` | `ReferenceCounted` | 3 | `0x007F7220` |
| `WeaponTypeClass` | `AbstractTypeClass` | 27 | `0x007F73B8` |
| `WebBrowser` | `IWOLBrowserEvent` | 18 | `0x007F743C` |
| `WinModemClass` | — | 10 | `0x007F7488` |
| `WinsockInterfaceClass` | — | 23 | `0x007F79BC` |
| `WinsockInterfaceClass::PAUWinsockBufferType::?$DynamicVectorClass` | `WinsockInterfaceClass::PAUWinsockBufferType::?$VectorClass` | 7 | `0x007F7A1C` |
| `WinsockInterfaceClass::PAUWinsockBufferType::?$VectorClass` | — | 7 | `0x007F7A3C` |
| `WonlineStringDialogControl` | `SimpleWonlineDialogControl` | 5 | `0x007F7874` |
| `WorldDominationTour::Campaign` | `ReferenceCounted` | 3 | `0x007F6F3C` |
| `WorldDominationTour::CampaignProperties` | `ReferenceCounted` | 3 | `0x007F7294` |
| `WorldDominationTour::Conflict` | `ReferenceCounted` | 3 | `0x007F6FC4` |
| `WorldDominationTour::E::?$ValueGameOption` | `WorldDominationTour::GameOption` | 5 | `0x007F7048` |
| `WorldDominationTour::E::V?$ValueGameOption::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F701C` |
| `WorldDominationTour::FactionSelectDialogControl` | `OwnerDraw::SimpleDialogControl` | 5 | `0x007F791C` |
| `WorldDominationTour::FlagGameOption` | `WorldDominationTour::GameOption` | 5 | `0x007F709C` |
| `WorldDominationTour::GameOption` | `ReferenceCounted` | 5 | `0x007F7060` |
| `WorldDominationTour::History` | `ReferenceCounted` | 3 | `0x007F70DC` |
| `WorldDominationTour::Map` | `ReferenceCounted` | 3 | `0x007F7134` |
| `WorldDominationTour::Map::PAUAnimationPalette::?$DynamicVectorClass` | `WorldDominationTour::Map::PAUAnimationPalette::?$VectorClass` | 7 | `0x007F7144` |
| `WorldDominationTour::Map::PAUAnimationPalette::?$VectorClass` | — | 7 | `0x007F71A4` |
| `WorldDominationTour::MapSizeGameOption` | `WorldDominationTour::GameOption` | 5 | `0x007F70B4` |
| `WorldDominationTour::Selection` | `MSEngine` | 3 | `0x007F72B4` |
| `WorldDominationTour::State` | `ReferenceCounted` | 3 | `0x007F7314` |
| `WorldDominationTour::Territory` | `ReferenceCounted` | 3 | `0x007F7334` |
| `WorldDominationTour::VCampaign::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F6F24` |
| `WorldDominationTour::VCampaignProperties::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F6F6C` |
| `WorldDominationTour::VCentroid::?$VectorClass` | — | 7 | `0x007F71E0` |
| `WorldDominationTour::VConflict::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F6F34` |
| `WorldDominationTour::VConflict::V?$rc_ptr::?$DynamicVectorClass` | `WorldDominationTour::VConflict::V?$rc_ptr::?$VectorClass` | 7 | `0x007F6F4C` |
| `WorldDominationTour::VConflict::V?$rc_ptr::?$VectorClass` | — | 7 | `0x007F6F7C` |
| `WorldDominationTour::VConflict::V?$rc_ptr::V?$DynamicVectorClass::WorldDominationTour::VConflict::V?$rc_ptr::?$VectorCursor` | — | 4 | `0x007F6F9C` |
| `WorldDominationTour::VFlagGameOption::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F7014` |
| `WorldDominationTour::VGameOption::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F7024` |
| `WorldDominationTour::VGameOption::V?$rc_ptr::?$DynamicVectorClass` | `WorldDominationTour::VGameOption::V?$rc_ptr::?$VectorClass` | 7 | `0x007F6FD4` |
| `WorldDominationTour::VGameOption::V?$rc_ptr::?$VectorClass` | — | 7 | `0x007F6FF4` |
| `WorldDominationTour::VGameOption::V?$rc_ptr::V?$DynamicVectorClass::WorldDominationTour::VGameOption::V?$rc_ptr::?$VectorCursor` | — | 4 | `0x007F702C` |
| `WorldDominationTour::VHistory::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F6F74` |
| `WorldDominationTour::VMap::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F712C` |
| `WorldDominationTour::VMapSizeGameOption::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F7040` |
| `WorldDominationTour::Voices::Anim` | `MSAnim` | 9 | `0x007F7354` |
| `WorldDominationTour::VState::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F6F2C` |
| `WorldDominationTour::VTerritory::?$rc_ptr` | `rc_ptr_base` | 1 | `0x007F71C4` |
| `WorldDominationTour::VTerritory::V?$rc_ptr::?$DynamicVectorClass` | `WorldDominationTour::VTerritory::V?$rc_ptr::?$VectorClass` | 7 | `0x007F7164` |
| `WorldDominationTour::VTerritory::V?$rc_ptr::?$VectorClass` | — | 7 | `0x007F7184` |
| `WorldDominationTour::VTerritory::V?$rc_ptr::V?$DynamicVectorClass::WorldDominationTour::VTerritory::V?$rc_ptr::?$VectorCursor` | — | 4 | `0x007F72D8` |
| `WWMouseClass` | `Mouse` | 18 | `0x007F7B2C` |
| `XSurface` | `Surface` | 36 | `0x007E2104` |
