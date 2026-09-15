# gamemd.exe 类型清单（RTTI 自动提取）

由 `tools/rtti.py` 从二进制的 MSVC RTTI 中自动抽取，非人工猜测。

- 类型描述符（TypeDescriptor）：988
- CompleteObjectLocator：1214
- 可定位虚函数表：1209

| 类名 | 基类 | 虚表数 | 最大槽位数 | 虚表 VA |
|---|---|---:|---:|---|
| `_N::?$DynamicVectorClass` | `_N::?$VectorClass` | 1 | 7 | 0x7EAA7C |
| `_N::?$VectorClass` | — | 1 | 7 | 0x7EAA5C |
| `AbstractClass` | `IPersistStream` | 4 | 24 | 0x7E1F50, 0x7E1F34, 0x7E1F2C |
| `AbstractTypeClass` | `AbstractClass` | 4 | 27 | 0x7E2000, 0x7E1FE4, 0x7E1FDC |
| `AddTeamCommandClass` | `CommandClass` | 1 | 9 | 0x7EBE8C |
| `AircraftClass` | `FootClass` | 5 | 341 | 0x7E22A4, 0x7E2288, 0x7E2280 |
| `AircraftTypeClass` | `TechnoTypeClass` | 4 | 48 | 0x7E2868, 0x7E284C, 0x7E2844 |
| `AirstrikeClass` | `AbstractClass` | 4 | 24 | 0x7E29A8, 0x7E298C, 0x7E2984 |
| `AITriggerTypeClass` | `AbstractTypeClass` | 4 | 27 | 0x7E2A50, 0x7E2A34, 0x7E2A2C |
| `AllianceCommandClass` | `CommandClass` | 1 | 9 | 0x7EBB44 |
| `AllToCheerCommandClass` | `CommandClass` | 1 | 9 | 0x7EBA54 |
| `AlphaShapeClass` | `AbstractClass` | 4 | 24 | 0x7E32A4, 0x7E3288, 0x7E3280 |
| `Animate` | — | 1 | 8 | 0x7E35A8 |
| `AnimClass` | `ObjectClass` | 4 | 124 | 0x7E3354, 0x7E3338, 0x7E3330 |
| `AnimFile` | `Animate` | 1 | 8 | 0x7E3584 |
| `AnimTypeClass` | `ObjectTypeClass` | 4 | 41 | 0x7E3608, 0x7E35EC, 0x7E35E4 |
| `ApplicationClass` | `IApplication` | 1 | 11 | 0x7E36D4 |
| `ATL::VCChatEventSink::?$CComObject` | `CChatEventSink` | 1 | 48 | 0x7F76B4 |
| `ATL::VCDownloadEventSink::?$CComObject` | `CDownloadEventSink` | 1 | 8 | 0x7F78E4 |
| `ATL::VCNetUtilEventSink::?$CComObject` | `CNetUtilEventSink` | 1 | 10 | 0x7F766C |
| `bad_typeid` | `exception` | 1 | 1 | 0x7F95DC |
| `Base64Pipe` | `Pipe` | 1 | 5 | 0x7EB774 |
| `Base64Straw` | `Straw` | 1 | 3 | 0x7EB764 |
| `BaseClass` | — | 1 | 3 | 0x7E3880 |
| `BeaconPlacementCommandClass` | `CommandClass` | 1 | 9 | 0x7EBBBC |
| `BinkMovieHandle` | `MovieHandle` | 1 | 11 | 0x7EE154 |
| `BitFont` | — | 1 | 1 | 0x7E3A78 |
| `BitText` | — | 1 | 1 | 0x7E3A80 |
| `Blitter` | — | 1 | 5 | 0x7E5B88 |
| `BlowPipe` | `Pipe` | 1 | 5 | 0x7EFDC8 |
| `BlowStraw` | `Straw` | 1 | 3 | 0x7EDF40 |
| `BombClass` | `AbstractClass` | 4 | 24 | 0x7E3D10, 0x7E3CF4, 0x7E3CEC |
| `BrainClass` | — | 1 | 1 | 0x7E3E74 |
| `BSurface` | `XSurface` | 1 | 36 | 0x7E2070 |
| `BufferIOFileClass` | `RawFileClass` | 1 | 17 | 0x7E3A2C |
| `BufferPipe` | `Pipe` | 1 | 5 | 0x7E6200 |
| `BufferStraw` | `Straw` | 1 | 3 | 0x7E61E0 |
| `BuildingClass` | `TechnoClass` | 4 | 322 | 0x7E3EBC, 0x7E3EA0, 0x7E3E98 |
| `BuildingLightClass` | `ObjectClass` | 4 | 122 | 0x7E3AD0, 0x7E3AB4, 0x7E3AAC |
| `BuildingTypeClass` | `TechnoTypeClass` | 4 | 49 | 0x7E4570, 0x7E4554, 0x7E454C |
| `BulletClass` | `ObjectClass` | 4 | 125 | 0x7E46E4, 0x7E46C8, 0x7E46C0 |
| `BulletTypeClass` | `ObjectTypeClass` | 4 | 40 | 0x7E4948, 0x7E492C, 0x7E4924 |
| `CacheStraw` | `Straw` | 1 | 3 | 0x7EB754 |
| `CampaignClass` | `AbstractTypeClass` | 4 | 27 | 0x7E4A28, 0x7E4A0C, 0x7E4A04 |
| `CampaignEndScoreClass` | — | 1 | 2 | 0x7E4AB8 |
| `CampaignScoreClass` | — | 1 | 2 | 0x7E4AAC |
| `CaptureManagerClass` | `AbstractClass` | 4 | 24 | 0x7E4B40, 0x7E4B24, 0x7E4B1C |
| `CarryoverClass` | `LinkClass` | 1 | 10 | 0x7E4C04 |
| `CCFileClass` | `CDFileClass` | 1 | 17 | 0x7E16B0 |
| `CChatEventSink` | `ATL::ATL::VCComMultiThreadModel::?$CComObjectRootEx` | 1 | 48 | 0x7F77A4 |
| `CCINIClass` | `INIClass` | 1 | 1 | 0x7E1AF4 |
| `CCToolTip` | `ToolTipManager` | 1 | 6 | 0x7F74C4 |
| `CD` | `DiskSwap` | 1 | 3 | 0x7E4C30 |
| `CDFileClass` | `BufferIOFileClass` | 1 | 17 | 0x7E1668 |
| `CellClass` | `AbstractClass` | 4 | 24 | 0x7E4EEC, 0x7E4ED0, 0x7E4EC8 |
| `CenterBaseCommandClass` | `CommandClass` | 1 | 9 | 0x7EBB1C |
| `CenterREventCommandClass` | `CommandClass` | 1 | 9 | 0x7EBBE4 |
| `CenterTeamCommandClass` | `CommandClass` | 1 | 9 | 0x7EBEB4 |
| `CenterViewCommandClass` | `CommandClass` | 1 | 9 | 0x7EBAF4 |
| `CheckListClass` | `ListClass` | 1 | 51 | 0x7E4F84 |
| `CNetUtilEventSink` | `ATL::ATL::VCComMultiThreadModel::?$CComObjectRootEx` | 1 | 10 | 0x7F7778 |
| `ColorListClass` | `ListClass` | 1 | 53 | 0x7E5054 |
| `CombatantSelectCommandClass` | `CommandClass` | 1 | 9 | 0x7EB98C |
| `CommandClass` | — | 1 | 9 | 0x7EBE3C |
| `CommBufferClass` | — | 1 | 1 | 0x7E519C |
| `ConnectionClass` | — | 1 | 10 | 0x7E51B4 |
| `ConnectionPointClass` | `IConnectionPoint` | 1 | 8 | 0x7E5CE4 |
| `ConnManClass` | — | 1 | 16 | 0x7EC1D4 |
| `ControlClass` | `GadgetClass` | 1 | 34 | 0x7E528C |
| `ConvertClass` | — | 1 | 1 | 0x7E5358 |
| `CounterClass` | `H::?$VectorClass` | 1 | 7 | 0x7E5C54 |
| `CreateGameDialogControl` | `WonlineStringDialogControl` | 1 | 5 | 0x7F788C |
| `CreateTeamCommandClass` | `CommandClass` | 1 | 9 | 0x7EB84C |
| `CStreamClass` | `IStream` | 2 | 15 | 0x7E5DAC, 0x7E5D94 |
| `CursorPositionCommandClass` | `CommandClass` | 1 | 9 | 0x7EBF54 |
| `DeleteCommandClass` | `CommandClass` | 1 | 9 | 0x7EBF7C |
| `DeployCommandClass` | `CommandClass` | 1 | 9 | 0x7EBA2C |
| `Dial8Class` | `ControlClass` | 1 | 34 | 0x7E5E3C |
| `DiskLaserClass` | `AbstractClass` | 4 | 24 | 0x7E5FB8, 0x7E5F9C, 0x7E5F94 |
| `DisplayClass` | `MapClass` | 1 | 50 | 0x7E6114 |
| `DisplayClass::TacticalClass` | `GadgetClass` | 1 | 33 | 0x7E608C |
| `DriveLocomotionClass` | `LocomotionClass` | 3 | 50 | 0x7E7F7C, 0x7E7EB0, 0x7E7E8C |
| `DropListClass` | `EditClass` | 1 | 46 | 0x7E7FCC |
| `DropPodLocomotionClass` | `LocomotionClass` | 3 | 50 | 0x7E8344, 0x7E8278, 0x7E8254 |
| `DSurface` | `XSurface` | 1 | 38 | 0x7E85D4 |
| `E::?$BlitPlain` | `Blitter` | 1 | 5 | 0x7F7BDC |
| `E::?$BlitPlainXlat` | `Blitter` | 1 | 5 | 0x7E5B70 |
| `E::?$BlitTrans` | `Blitter` | 1 | 5 | 0x7F7C0C |
| `E::?$BlitTransRemapDest` | `Blitter` | 1 | 5 | 0x7E5B28 |
| `E::?$BlitTransRemapXlat` | `Blitter` | 1 | 5 | 0x7E5B10 |
| `E::?$BlitTransXlat` | `Blitter` | 1 | 5 | 0x7E5B58 |
| `E::?$BlitTransZRemapXlat` | `Blitter` | 1 | 5 | 0x7E5B40 |
| `E::?$RLEBlitTransRemapDest` | `RLEBlitter` | 1 | 3 | 0x7E5AE0 |
| `E::?$RLEBlitTransRemapDestZRead` | `RLEBlitter` | 1 | 3 | 0x7E5AA0 |
| `E::?$RLEBlitTransRemapDestZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E5A60 |
| `E::?$RLEBlitTransRemapXlat` | `RLEBlitter` | 1 | 3 | 0x7E5AD0 |
| `E::?$RLEBlitTransRemapXlatZRead` | `RLEBlitter` | 1 | 3 | 0x7E5A90 |
| `E::?$RLEBlitTransRemapXlatZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E5A50 |
| `E::?$RLEBlitTransXlat` | `RLEBlitter` | 1 | 3 | 0x7E5B00 |
| `E::?$RLEBlitTransXlatZRead` | `RLEBlitter` | 1 | 3 | 0x7E5AC0 |
| `E::?$RLEBlitTransXlatZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E5A80 |
| `E::?$RLEBlitTransZRemapXlat` | `RLEBlitter` | 1 | 3 | 0x7E5AF0 |
| `E::?$RLEBlitTransZRemapXlatZRead` | `RLEBlitter` | 1 | 3 | 0x7E5AB0 |
| `E::?$RLEBlitTransZRemapXlatZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E5A70 |
| `E::?$VectorClass` | — | 1 | 7 | 0x7F65F4 |
| `EditClass` | `ControlClass` | 1 | 39 | 0x7E81A4 |
| `EMPulseClass` | `AbstractClass` | 4 | 24 | 0x7E87A8, 0x7E878C, 0x7E8784 |
| `EnumConnectionPointsClass` | `IEnumConnectionPoints` | 1 | 7 | 0x7E5D28 |
| `EnumConnectionsClass` | `IEnumConnections` | 1 | 7 | 0x7E5CA4 |
| `FactoryClass` | `AbstractClass` | 4 | 24 | 0x7E88D0, 0x7E88B4, 0x7E88AC |
| `FileClass` | — | 1 | 17 | 0x7F08BC |
| `FilePipe` | `Pipe` | 1 | 5 | 0x7E4DA0 |
| `FileStraw` | `Straw` | 1 | 3 | 0x7E4D90 |
| `FlyLocomotionClass` | `LocomotionClass` | 2 | 50 | 0x7E8AC0, 0x7E89F4 |
| `FoggedObjectClass` | `AbstractClass` | 4 | 25 | 0x7E8B38, 0x7E8B1C, 0x7E8B14 |
| `FoggedObjectClass::UDrawRecord::?$DynamicVectorClass` | `FoggedObjectClass::UDrawRecord::?$VectorClass` | 1 | 7 | 0x7E8BA0 |
| `FoggedObjectClass::UDrawRecord::?$VectorClass` | — | 1 | 7 | 0x7E8BC0 |
| `FollowCommandClass` | `CommandClass` | 1 | 9 | 0x7EBDC4 |
| `FootClass` | `TechnoClass` | 4 | 341 | 0x7E8C94, 0x7E8C78, 0x7E8C70 |
| `FreeForAll` | `MultiplayerGameMode` | 1 | 52 | 0x7EE424 |
| `G::?$BlitPlain` | `Blitter` | 1 | 5 | 0x7F7BC4 |
| `G::?$BlitPlainXlat` | `Blitter` | 1 | 5 | 0x7E5A38 |
| `G::?$BlitPlainXlatAlpha` | `Blitter` | 1 | 5 | 0x7E57F8 |
| `G::?$BlitPlainXlatZRead` | `Blitter` | 1 | 5 | 0x7E5990 |
| `G::?$BlitPlainXlatZReadWrite` | `Blitter` | 1 | 5 | 0x7E58A0 |
| `G::?$BlitTrans` | `Blitter` | 1 | 5 | 0x7F7BF4 |
| `G::?$BlitTransDarken` | `Blitter` | 1 | 5 | 0x7E59F0 |
| `G::?$BlitTransDarkenZRead` | `Blitter` | 1 | 5 | 0x7E5948 |
| `G::?$BlitTransDarkenZReadWrite` | `Blitter` | 1 | 5 | 0x7E5858 |
| `G::?$BlitTransLucent25` | `Blitter` | 1 | 5 | 0x7E59A8 |
| `G::?$BlitTransLucent25Alpha` | `Blitter` | 1 | 5 | 0x7E5780 |
| `G::?$BlitTransLucent25AlphaZRead` | `Blitter` | 1 | 5 | 0x7E5690 |
| `G::?$BlitTransLucent25AlphaZReadWarp` | `Blitter` | 1 | 5 | 0x7E5648 |
| `G::?$BlitTransLucent25AlphaZReadWrite` | `Blitter` | 1 | 5 | 0x7E55D0 |
| `G::?$BlitTransLucent25ZRead` | `Blitter` | 1 | 5 | 0x7E5900 |
| `G::?$BlitTransLucent25ZReadWarp` | `Blitter` | 1 | 5 | 0x7E58B8 |
| `G::?$BlitTransLucent25ZReadWrite` | `Blitter` | 1 | 5 | 0x7E5810 |
| `G::?$BlitTransLucent50` | `Blitter` | 1 | 5 | 0x7E59C0 |
| `G::?$BlitTransLucent50Alpha` | `Blitter` | 1 | 5 | 0x7E5798 |
| `G::?$BlitTransLucent50AlphaZRead` | `Blitter` | 1 | 5 | 0x7E56A8 |
| `G::?$BlitTransLucent50AlphaZReadWarp` | `Blitter` | 1 | 5 | 0x7E5660 |
| `G::?$BlitTransLucent50AlphaZReadWrite` | `Blitter` | 1 | 5 | 0x7E55E8 |
| `G::?$BlitTranslucent50NonzeroAlpha` | `Blitter` | 1 | 5 | 0x7E5720 |
| `G::?$BlitTranslucent50ZeroAlpha` | `Blitter` | 1 | 5 | 0x7E5708 |
| `G::?$BlitTransLucent50ZRead` | `Blitter` | 1 | 5 | 0x7E5918 |
| `G::?$BlitTransLucent50ZReadWarp` | `Blitter` | 1 | 5 | 0x7E58D0 |
| `G::?$BlitTransLucent50ZReadWrite` | `Blitter` | 1 | 5 | 0x7E5828 |
| `G::?$BlitTransLucent75` | `Blitter` | 1 | 5 | 0x7E59D8 |
| `G::?$BlitTransLucent75Alpha` | `Blitter` | 1 | 5 | 0x7E57B0 |
| `G::?$BlitTransLucent75AlphaZRead` | `Blitter` | 1 | 5 | 0x7E56C0 |
| `G::?$BlitTransLucent75AlphaZReadWarp` | `Blitter` | 1 | 5 | 0x7E5678 |
| `G::?$BlitTransLucent75AlphaZReadWrite` | `Blitter` | 1 | 5 | 0x7E5600 |
| `G::?$BlitTransLucent75ZRead` | `Blitter` | 1 | 5 | 0x7E5930 |
| `G::?$BlitTransLucent75ZReadWarp` | `Blitter` | 1 | 5 | 0x7E58E8 |
| `G::?$BlitTransLucent75ZReadWrite` | `Blitter` | 1 | 5 | 0x7E5840 |
| `G::?$BlitTranslucentWriteAlpha` | `Blitter` | 1 | 5 | 0x7E5738 |
| `G::?$BlitTransXlat` | `Blitter` | 1 | 5 | 0x7E5A20 |
| `G::?$BlitTransXlatAlpha` | `Blitter` | 1 | 5 | 0x7E57E0 |
| `G::?$BlitTransXlatAlphaZRead` | `Blitter` | 1 | 5 | 0x7E56F0 |
| `G::?$BlitTransXlatAlphaZReadWrite` | `Blitter` | 1 | 5 | 0x7E5630 |
| `G::?$BlitTransXlatMultWriteAlpha` | `Blitter` | 1 | 5 | 0x7E5750 |
| `G::?$BlitTransXlatWriteAlpha` | `Blitter` | 1 | 5 | 0x7E5768 |
| `G::?$BlitTransXlatZRead` | `Blitter` | 1 | 5 | 0x7E5978 |
| `G::?$BlitTransXlatZReadWrite` | `Blitter` | 1 | 5 | 0x7E5888 |
| `G::?$BlitTransZRemapXlat` | `Blitter` | 1 | 5 | 0x7E5A08 |
| `G::?$BlitTransZRemapXlatAlpha` | `Blitter` | 1 | 5 | 0x7E57C8 |
| `G::?$BlitTransZRemapXlatAlphaZRead` | `Blitter` | 1 | 5 | 0x7E56D8 |
| `G::?$BlitTransZRemapXlatAlphaZReadWrite` | `Blitter` | 1 | 5 | 0x7E5618 |
| `G::?$BlitTransZRemapXlatZRead` | `Blitter` | 1 | 5 | 0x7E5960 |
| `G::?$BlitTransZRemapXlatZReadWrite` | `Blitter` | 1 | 5 | 0x7E5870 |
| `G::?$DynamicVectorClass` | `G::?$VectorClass` | 1 | 7 | 0x7E3844 |
| `G::?$RLEBlitTransDarken` | `RLEBlitter` | 1 | 3 | 0x7E55A0 |
| `G::?$RLEBlitTransDarkenZRead` | `RLEBlitter` | 1 | 3 | 0x7E5540 |
| `G::?$RLEBlitTransDarkenZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E54B0 |
| `G::?$RLEBlitTransLucent25` | `RLEBlitter` | 1 | 3 | 0x7E5570 |
| `G::?$RLEBlitTransLucent25Alpha` | `RLEBlitter` | 1 | 3 | 0x7E5430 |
| `G::?$RLEBlitTransLucent25AlphaZRead` | `RLEBlitter` | 1 | 3 | 0x7E53E0 |
| `G::?$RLEBlitTransLucent25AlphaZReadWarp` | `RLEBlitter` | 1 | 3 | 0x7E53B0 |
| `G::?$RLEBlitTransLucent25AlphaZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E5360 |
| `G::?$RLEBlitTransLucent25ZRead` | `RLEBlitter` | 1 | 3 | 0x7E5510 |
| `G::?$RLEBlitTransLucent25ZReadWarp` | `RLEBlitter` | 1 | 3 | 0x7E54E0 |
| `G::?$RLEBlitTransLucent25ZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E5480 |
| `G::?$RLEBlitTransLucent50` | `RLEBlitter` | 1 | 3 | 0x7E5580 |
| `G::?$RLEBlitTransLucent50Alpha` | `RLEBlitter` | 1 | 3 | 0x7E5440 |
| `G::?$RLEBlitTransLucent50AlphaZRead` | `RLEBlitter` | 1 | 3 | 0x7E53F0 |
| `G::?$RLEBlitTransLucent50AlphaZReadWarp` | `RLEBlitter` | 1 | 3 | 0x7E53C0 |
| `G::?$RLEBlitTransLucent50AlphaZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E5370 |
| `G::?$RLEBlitTransLucent50ZRead` | `RLEBlitter` | 1 | 3 | 0x7E5520 |
| `G::?$RLEBlitTransLucent50ZReadWarp` | `RLEBlitter` | 1 | 3 | 0x7E54F0 |
| `G::?$RLEBlitTransLucent50ZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E5490 |
| `G::?$RLEBlitTransLucent75` | `RLEBlitter` | 1 | 3 | 0x7E5590 |
| `G::?$RLEBlitTransLucent75Alpha` | `RLEBlitter` | 1 | 3 | 0x7E5450 |
| `G::?$RLEBlitTransLucent75AlphaZRead` | `RLEBlitter` | 1 | 3 | 0x7E5400 |
| `G::?$RLEBlitTransLucent75AlphaZReadWarp` | `RLEBlitter` | 1 | 3 | 0x7E53D0 |
| `G::?$RLEBlitTransLucent75AlphaZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E5380 |
| `G::?$RLEBlitTransLucent75ZRead` | `RLEBlitter` | 1 | 3 | 0x7E5530 |
| `G::?$RLEBlitTransLucent75ZReadWarp` | `RLEBlitter` | 1 | 3 | 0x7E5500 |
| `G::?$RLEBlitTransLucent75ZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E54A0 |
| `G::?$RLEBlitTransXlat` | `RLEBlitter` | 1 | 3 | 0x7E55C0 |
| `G::?$RLEBlitTransXlatAlpha` | `RLEBlitter` | 1 | 3 | 0x7E5470 |
| `G::?$RLEBlitTransXlatAlphaZRead` | `RLEBlitter` | 1 | 3 | 0x7E5420 |
| `G::?$RLEBlitTransXlatAlphaZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E53A0 |
| `G::?$RLEBlitTransXlatZRead` | `RLEBlitter` | 1 | 3 | 0x7E5560 |
| `G::?$RLEBlitTransXlatZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E54D0 |
| `G::?$RLEBlitTransZRemapXlat` | `RLEBlitter` | 1 | 3 | 0x7E55B0 |
| `G::?$RLEBlitTransZRemapXlatAlpha` | `RLEBlitter` | 1 | 3 | 0x7E5460 |
| `G::?$RLEBlitTransZRemapXlatAlphaZRead` | `RLEBlitter` | 1 | 3 | 0x7E5410 |
| `G::?$RLEBlitTransZRemapXlatAlphaZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E5390 |
| `G::?$RLEBlitTransZRemapXlatZRead` | `RLEBlitter` | 1 | 3 | 0x7E5550 |
| `G::?$RLEBlitTransZRemapXlatZReadWrite` | `RLEBlitter` | 1 | 3 | 0x7E54C0 |
| `G::?$VectorClass` | — | 1 | 7 | 0x7E3824 |
| `GadgetClass` | `LinkClass` | 1 | 33 | 0x7E92BC |
| `GaugeClass` | `ControlClass` | 1 | 42 | 0x7E9384 |
| `GenericList` | — | 1 | 1 | 0x7E1B04 |
| `GenericNode` | — | 1 | 1 | 0x7E1B0C |
| `GraphicMenu` | — | 1 | 1 | 0x7EA5FC |
| `GraphicMenuAnimItem` | `GraphicMenuItem` | 1 | 6 | 0x7EA658 |
| `GraphicMenuImageItem` | `GraphicMenuItem` | 1 | 6 | 0x7EA674 |
| `GraphicMenuItem` | — | 1 | 6 | 0x7EA690 |
| `GraphicMenuShortcutItem` | `GraphicMenuItem` | 1 | 6 | 0x7EA6AC |
| `GScreenClass` | `IGameMap` | 1 | 22 | 0x7EA6FC |
| `GuardCommandClass` | `CommandClass` | 1 | 9 | 0x7EBAA4 |
| `H::?$DynamicVectorClass` | `H::?$VectorClass` | 1 | 7 | 0x7E4E78 |
| `H::?$TypeList` | `H::?$DynamicVectorClass` | 1 | 7 | 0x7E4DD8 |
| `H::?$VectorClass` | — | 1 | 7 | 0x7E4DB8 |
| `H::V?$TPoint3D::?$VectorClass` | — | 1 | 7 | 0x7E4638 |
| `H::V?$TRect::?$DynamicVectorClass` | `H::V?$TRect::?$VectorClass` | 1 | 7 | 0x7ED99C |
| `H::V?$TRect::?$VectorClass` | — | 1 | 7 | 0x7ED970 |
| `H::V?$TRect::V?$VectorClass::H::V?$TRect::?$VectorCursor` | — | 1 | 4 | 0x7F71CC |
| `HealthNavCommandClass` | `CommandClass` | 1 | 9 | 0x7EB93C |
| `HouseClass` | `AbstractClass` | 7 | 24 | 0x7EA8A0, 0x7EA884, 0x7EA87C |
| `HouseClass::PAUBuildChoiceClass::?$DynamicVectorClass` | `HouseClass::PAUBuildChoiceClass::?$VectorClass` | 1 | 7 | 0x7EA7B4 |
| `HouseClass::PAUBuildChoiceClass::?$VectorClass` | — | 1 | 7 | 0x7EA7D4 |
| `HouseClass::PAUStartingTechnoStruct::?$DynamicVectorClass` | `HouseClass::PAUStartingTechnoStruct::?$VectorClass` | 1 | 7 | 0x7EA944 |
| `HouseClass::PAUStartingTechnoStruct::?$VectorClass` | — | 1 | 7 | 0x7EA964 |
| `HouseTypeClass` | `AbstractTypeClass` | 4 | 27 | 0x7EAB58, 0x7EAB3C, 0x7EAB34 |
| `HoverLocomotionClass` | `LocomotionClass` | 2 | 50 | 0x7EADC8, 0x7EACFC |
| `I::?$DynamicVectorClass` | `I::?$VectorClass` | 1 | 7 | 0x7E37CC |
| `I::?$VectorClass` | — | 1 | 7 | 0x7E37EC |
| `I::IV?$DynamicVectorClass::?$VectorCursor` | — | 1 | 4 | 0x7EA6C8 |
| `II::U?$HashObject::?$DynamicVectorClass` | `II::U?$HashObject::?$VectorClass` | 1 | 7 | 0x7ED540 |
| `II::U?$HashObject::?$VectorClass` | — | 1 | 7 | 0x7ED5C0 |
| `InfantryClass` | `FootClass` | 4 | 343 | 0x7EB058, 0x7EB03C, 0x7EB034 |
| `InfantryTypeClass` | `TechnoTypeClass` | 4 | 48 | 0x7EB610, 0x7EB5F4, 0x7EB5EC |
| `INIClass` | — | 1 | 1 | 0x7EA5F4 |
| `INIClass::INIEntry` | `INIClass::PAUINIEntry::?$Node` | 1 | 1 | 0x7EB734 |
| `INIClass::INISection` | `INIClass::PAUINISection::?$Node` | 1 | 1 | 0x7EB73C |
| `INIClass::PAUINIEntry::?$List` | `GenericList` | 1 | 1 | 0x7EB744 |
| `INIClass::PAUINISection::?$List` | `GenericList` | 1 | 1 | 0x7E1AFC |
| `INIClass::PAUINISection::?$Node` | `GenericNode` | 1 | 1 | 0x7EB74C |
| `INoticeSink` | — | 1 | 1 | 0x7E1FBC |
| `INoticeSource` | — | 1 | 1 | 0x7E1FB4 |
| `IPXConnClass` | `ConnectionClass` | 1 | 11 | 0x7EC0CC |
| `IPXGlobalConnClass` | `IPXConnClass` | 1 | 18 | 0x7EC10C |
| `IPXInterfaceClass` | `WinsockInterfaceClass` | 1 | 23 | 0x7F794C |
| `IPXManagerClass` | `ConnManClass` | 1 | 25 | 0x7EC16C |
| `IsometricTileClass` | `ObjectClass` | 4 | 122 | 0x7EC258, 0x7EC23C, 0x7EC234 |
| `IsometricTileTypeClass` | `ObjectTypeClass` | 4 | 40 | 0x7ECC48, 0x7ECC2C, 0x7ECC24 |
| `IsometricTileTypeClass::PAUTileInsertType::?$DynamicVectorClass` | `IsometricTileTypeClass::PAUTileInsertType::?$VectorClass` | 1 | 7 | 0x7ECBDC |
| `IsometricTileTypeClass::PAUTileInsertType::?$VectorClass` | — | 1 | 7 | 0x7ECBFC |
| `IUSubzoneConnectionStruct::U?$HashObject::?$DynamicVectorClass` | `IUSubzoneConnectionStruct::U?$HashObject::?$VectorClass` | 1 | 7 | 0x7ED520 |
| `IUSubzoneConnectionStruct::U?$HashObject::?$VectorClass` | — | 1 | 7 | 0x7ED5E0 |
| `JumpjetLocomotionClass` | `LocomotionClass` | 3 | 50 | 0x7ECE34, 0x7ECD68, 0x7ECD44 |
| `K::?$DynamicVectorClass` | `K::?$VectorClass` | 1 | 7 | 0x7F3728 |
| `K::?$VectorClass` | — | 1 | 7 | 0x7F3748 |
| `LayerClass` | `PAVObjectClass::?$DynamicVectorClass` | 1 | 10 | 0x7E6060 |
| `LCWPipe` | `Pipe` | 1 | 5 | 0x7ECF2C |
| `LCWStraw` | `Straw` | 1 | 3 | 0x7ECF44 |
| `LightConvertClass` | `ConvertClass` | 1 | 2 | 0x7ED0A4 |
| `LightSourceClass` | `AbstractClass` | 4 | 24 | 0x7ED028, 0x7ED00C, 0x7ED004 |
| `LightSourceClass::PAVPendingCellClass::?$DynamicVectorClass` | `LightSourceClass::PAVPendingCellClass::?$VectorClass` | 1 | 7 | 0x7ECFBC |
| `LightSourceClass::PAVPendingCellClass::?$VectorClass` | — | 1 | 7 | 0x7ECFDC |
| `LinkClass` | — | 1 | 10 | 0x7E9344 |
| `ListClass` | `ControlClass` | 1 | 51 | 0x7ED10C |
| `LoadOptionsClass` | — | 1 | 9 | 0x7ED2E4 |
| `LoadProgressMgr` | `INoticeSink` | 1 | 1 | 0x7ECF64 |
| `LocomotionClass` | `IPersistStream` | 2 | 50 | 0x7EAEC0, 0x7EADF4 |
| `LogicClass` | `LayerClass` | 1 | 11 | 0x7E18FC |
| `LZOPipe` | `Pipe` | 1 | 5 | 0x7ED37C |
| `LZOStraw` | `Straw` | 1 | 3 | 0x7ED394 |
| `MapClass` | `GScreenClass` | 1 | 30 | 0x7ED404 |
| `MapSeedClass` | `LoadOptionsClass` | 1 | 9 | 0x7ED8E4 |
| `MapSelect` | `MSEngine` | 1 | 3 | 0x7EDB4C |
| `MechLocomotionClass` | `LocomotionClass` | 2 | 50 | 0x7EDC38, 0x7EDB6C |
| `Megawealth` | `MultiplayerGameMode` | 1 | 52 | 0x7EE5F4 |
| `MissionClass` | `ObjectClass` | 4 | 157 | 0x7EDCC0, 0x7EDCA4, 0x7EDC9C |
| `MixFileClass` | `PAVMixFileClass::?$Node` | 1 | 1 | 0x7EDF50 |
| `Mouse` | — | 1 | 18 | 0x7F7B78 |
| `MouseClass` | `ScrollClass` | 2 | 55 | 0x7E1964, 0x7E195C |
| `MovieHandle` | — | 1 | 11 | 0x7EE124 |
| `MPCooperative` | `MultiplayerGameMode` | 1 | 52 | 0x7EE27C |
| `MSAnim` | — | 1 | 9 | 0x7EE8E8 |
| `MSBinkAnim` | `MSAnim` | 1 | 9 | 0x7EE988 |
| `MSBitPrintAnim` | `MSAnim` | 1 | 9 | 0x7EE9D8 |
| `MSEngine` | — | 1 | 3 | 0x7EEBD4 |
| `MSFadeAnim` | `MSShapeAnim` | 1 | 9 | 0x7EE938 |
| `MSFont` | — | 1 | 5 | 0x7EEC64 |
| `MSFrameAnim` | `MSAnim` | 1 | 9 | 0x7F7104 |
| `MSOverlayAnim` | `MSFadeAnim` | 1 | 9 | 0x7EE960 |
| `MSPCXAnim` | `MSAnim` | 1 | 9 | 0x7EEA2C |
| `MSPrintAnim` | `MSAnim` | 1 | 9 | 0x7EEA00 |
| `MSShapeAnim` | `MSAnim` | 1 | 9 | 0x7EE910 |
| `MSVQAnim` | `MSAnim` | 1 | 9 | 0x7EE9B0 |
| `MultiplayerBattle` | `MultiplayerGameMode` | 1 | 52 | 0x7EE184 |
| `MultiplayerBattleTeam` | `MultiplayerTeam` | 1 | 3 | 0x7EE258 |
| `MultiplayerDebugCommandClass` | `CommandClass` | 1 | 9 | 0x7EBE14 |
| `MultiplayerGameMode` | — | 1 | 52 | 0x7EED60 |
| `MultiplayerGameMode::InitializerBase` | — | 1 | 2 | 0x7EEE74 |
| `MultiplayerGameMode::VFreeForAll::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 1 | 2 | 0x7EEE8C |
| `MultiplayerGameMode::VMPCooperative::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 1 | 2 | 0x7EEE80 |
| `MultiplayerGameMode::VMultiplayerBattle::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 1 | 2 | 0x7EEEBC |
| `MultiplayerGameMode::VMultiplayerManBattle::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 1 | 2 | 0x7EEEB0 |
| `MultiplayerGameMode::VMultiplayerSiege::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 1 | 2 | 0x7EEEA4 |
| `MultiplayerGameMode::VUnholyAlliance::?$Initializer` | `MultiplayerGameMode::InitializerBase` | 1 | 2 | 0x7EEE98 |
| `MultiplayerManBattle` | `MultiplayerGameMode` | 1 | 52 | 0x7EE50C |
| `MultiplayerObserverTeam` | `MultiplayerTeam` | 1 | 3 | 0x7EE6C8 |
| `MultiplayerSiege` | `MultiplayerGameMode` | 1 | 52 | 0x7EE6FC |
| `MultiplayerSiegeAttackerTeam` | `MultiplayerTeam` | 1 | 3 | 0x7EE7F4 |
| `MultiplayerSiegeDefenderTeam` | `MultiplayerTeam` | 1 | 3 | 0x7EE7E4 |
| `MultiplayerSyncCommandClass` | `CommandClass` | 1 | 9 | 0x7EBDEC |
| `MultiplayerTeam` | — | 1 | 3 | 0x7EEEDC |
| `N::?$DynamicVectorClass` | `N::?$VectorClass` | 1 | 7 | 0x7EDA6C |
| `N::?$VectorClass` | — | 1 | 7 | 0x7EDA4C |
| `NeuronClass` | `AbstractClass` | 4 | 24 | 0x7E3DF0, 0x7E3DD4, 0x7E3DCC |
| `NextObjectCommandClass` | `CommandClass` | 1 | 9 | 0x7EB9DC |
| `NullModemClass` | `ConnManClass` | 1 | 16 | 0x7EEFDC |
| `NullModemConnClass` | `ConnectionClass` | 1 | 10 | 0x7EEF90 |
| `ObjectClass` | `AbstractClass` | 4 | 122 | 0x7EF060, 0x7EF044, 0x7EF03C |
| `ObjectTypeClass` | `AbstractTypeClass` | 4 | 40 | 0x7EF2D8, 0x7EF2BC, 0x7EF2B4 |
| `OptionsCommandClass` | `CommandClass` | 1 | 9 | 0x7EBC5C |
| `OverlayClass` | `ObjectClass` | 4 | 122 | 0x7EF3D4, 0x7EF3B0, 0x7EF3CC |
| `OverlayTypeClass` | `ObjectTypeClass` | 4 | 41 | 0x7EF600, 0x7EF5E4, 0x7EF5DC |
| `OwnerDraw::DialogControl` | — | 1 | 5 | 0x7EF720 |
| `OwnerDraw::SimpleDialogControl` | `OwnerDraw::DialogControl` | 1 | 5 | 0x7EF738 |
| `OwnerTalkClass::PAUConnectionListStruct::?$DynamicVectorClass` | `OwnerTalkClass::PAUConnectionListStruct::?$VectorClass` | 1 | 7 | 0x7F0C2C |
| `OwnerTalkClass::PAUConnectionListStruct::?$VectorClass` | — | 1 | 7 | 0x7F0C4C |
| `PAD::?$DynamicVectorClass` | `PAD::?$VectorClass` | 1 | 7 | 0x7E5BC4 |
| `PAD::?$VectorClass` | — | 1 | 7 | 0x7E5C24 |
| `PAD::PAV?$DynamicVectorClass::?$DynamicVectorClass` | `PAD::PAV?$DynamicVectorClass::?$VectorClass` | 1 | 7 | 0x7E5BE4 |
| `PAD::PAV?$DynamicVectorClass::?$VectorClass` | — | 1 | 7 | 0x7E5C04 |
| `PAE::?$DynamicVectorClass` | `PAE::?$VectorClass` | 1 | 7 | 0x7F7AEC |
| `PAE::?$VectorClass` | — | 1 | 7 | 0x7F7B0C |
| `PAG::?$DynamicVectorClass` | `PAG::?$VectorClass` | 1 | 7 | 0x7ECCEC |
| `PAG::?$VectorClass` | — | 1 | 7 | 0x7ECD0C |
| `PageUserCommandClass` | `CommandClass` | 1 | 9 | 0x7EBF2C |
| `ParasiteClass` | `AbstractClass` | 4 | 24 | 0x7EF890, 0x7EF874, 0x7EF86C |
| `ParticleClass` | `ObjectClass` | 4 | 123 | 0x7EF954, 0x7EF938, 0x7EF930 |
| `ParticleSystemClass` | `ObjectClass` | 4 | 122 | 0x7EFB9C, 0x7EFB80, 0x7EFB78 |
| `ParticleSystemTypeClass` | `ObjectTypeClass` | 4 | 40 | 0x7F00A8, 0x7F008C, 0x7F0084 |
| `ParticleTypeClass` | `ObjectTypeClass` | 4 | 40 | 0x7F0188, 0x7F016C, 0x7F0164 |
| `PAU_DDSURFACEDESC::?$DynamicVectorClass` | `PAU_DDSURFACEDESC::?$VectorClass` | 1 | 7 | 0x7E5E0C |
| `PAU_DDSURFACEDESC::?$VectorClass` | — | 1 | 7 | 0x7E5DEC |
| `PAU_WIN32_FIND_DATAA::?$DynamicVectorClass` | `PAU_WIN32_FIND_DATAA::?$VectorClass` | 1 | 7 | 0x7ED94C |
| `PAU_WIN32_FIND_DATAA::?$VectorClass` | — | 1 | 7 | 0x7ED92C |
| `PAUButtonFadeEffect::?$DynamicVectorClass` | `PAUButtonFadeEffect::?$VectorClass` | 1 | 7 | 0x7E856C |
| `PAUButtonFadeEffect::?$VectorClass` | — | 1 | 7 | 0x7E8500 |
| `PAUControlNode::?$DynamicVectorClass` | `PAUControlNode::?$VectorClass` | 1 | 7 | 0x7E4BA4 |
| `PAUControlNode::?$VectorClass` | — | 1 | 7 | 0x7E4BC4 |
| `PAUCrossDissolveEffect::?$DynamicVectorClass` | `PAUCrossDissolveEffect::?$VectorClass` | 1 | 7 | 0x7E854C |
| `PAUCrossDissolveEffect::?$VectorClass` | — | 1 | 7 | 0x7E8520 |
| `PAUDamageGroup::?$DynamicVectorClass` | `PAUDamageGroup::?$VectorClass` | 1 | 7 | 0x7E5170 |
| `PAUDamageGroup::?$VectorClass` | — | 1 | 7 | 0x7E5144 |
| `PAUGlobalPacketType::?$DynamicVectorClass` | `PAUGlobalPacketType::?$VectorClass` | 1 | 7 | 0x7F11D4 |
| `PAUGlobalPacketType::?$VectorClass` | — | 1 | 7 | 0x7F1234 |
| `PAUHWND__::?$DynamicVectorClass` | `PAUHWND__::?$VectorClass` | 1 | 7 | 0x7EEC8C |
| `PAUHWND__::?$VectorClass` | — | 1 | 7 | 0x7EECAC |
| `PAUIConnectionPoint::?$DynamicVectorClass` | `PAUIConnectionPoint::?$VectorClass` | 1 | 7 | 0x7E5D48 |
| `PAUIConnectionPoint::?$VectorClass` | — | 1 | 7 | 0x7E5D08 |
| `PAUKamikazeControl::?$DynamicVectorClass` | `PAUKamikazeControl::?$VectorClass` | 1 | 7 | 0x7ECE7C |
| `PAUKamikazeControl::?$VectorClass` | — | 1 | 7 | 0x7ECE9C |
| `PAUMPlayerScoreType::?$DynamicVectorClass` | `PAUMPlayerScoreType::?$VectorClass` | 1 | 7 | 0x7EE3F0 |
| `PAUMPlayerScoreType::?$VectorClass` | — | 1 | 7 | 0x7EE3D0 |
| `PAUNodeNameType::?$DynamicVectorClass` | `PAUNodeNameType::?$VectorClass` | 1 | 7 | 0x7EE370 |
| `PAUNodeNameType::?$VectorClass` | — | 1 | 7 | 0x7EE390 |
| `PAUtConnInfoStruct::?$DynamicVectorClass` | `PAUtConnInfoStruct::?$VectorClass` | 1 | 7 | 0x7F78C4 |
| `PAUtConnInfoStruct::?$VectorClass` | — | 1 | 7 | 0x7F78A4 |
| `PAUThemeControl::?$DynamicVectorClass` | `PAUThemeControl::?$VectorClass` | 1 | 7 | 0x7F568C |
| `PAUThemeControl::?$VectorClass` | — | 1 | 7 | 0x7EA584 |
| `PAVAbstractClass::?$DynamicVectorClass` | `PAVAbstractClass::?$VectorClass` | 1 | 7 | 0x7E91EC |
| `PAVAbstractClass::?$VectorClass` | — | 1 | 7 | 0x7E920C |
| `PAVAbstractTypeClass::?$DynamicVectorClass` | `PAVAbstractTypeClass::?$VectorClass` | 1 | 7 | 0x7EA524 |
| `PAVAbstractTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA544 |
| `PAVAircraftClass::?$DynamicVectorClass` | `PAVAircraftClass::?$VectorClass` | 1 | 7 | 0x7E9E64 |
| `PAVAircraftClass::?$VectorClass` | — | 1 | 7 | 0x7E9E84 |
| `PAVAircraftTypeClass::?$DynamicVectorClass` | `PAVAircraftTypeClass::?$VectorClass` | 1 | 7 | 0x7EA264 |
| `PAVAircraftTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA284 |
| `PAVAirstrikeClass::?$DynamicVectorClass` | `PAVAirstrikeClass::?$VectorClass` | 1 | 7 | 0x7E293C |
| `PAVAirstrikeClass::?$VectorClass` | — | 1 | 7 | 0x7E295C |
| `PAVAITriggerTypeClass::?$DynamicVectorClass` | `PAVAITriggerTypeClass::?$VectorClass` | 1 | 7 | 0x7E9B64 |
| `PAVAITriggerTypeClass::?$VectorClass` | — | 1 | 7 | 0x7E9B84 |
| `PAVAlphaLightingRemapClass::?$DynamicVectorClass` | `PAVAlphaLightingRemapClass::?$VectorClass` | 1 | 7 | 0x7E2AD0 |
| `PAVAlphaLightingRemapClass::?$VectorClass` | — | 1 | 7 | 0x7E2AF0 |
| `PAVAlphaShapeClass::?$DynamicVectorClass` | `PAVAlphaShapeClass::?$VectorClass` | 1 | 7 | 0x7E3238 |
| `PAVAlphaShapeClass::?$VectorClass` | — | 1 | 7 | 0x7E3258 |
| `PAVAnimClass::?$DynamicVectorClass` | `PAVAnimClass::?$VectorClass` | 1 | 7 | 0x7E9F24 |
| `PAVAnimClass::?$VectorClass` | — | 1 | 7 | 0x7E9F44 |
| `PAVAnimTypeClass::?$DynamicVectorClass` | `PAVAnimTypeClass::?$VectorClass` | 1 | 7 | 0x7EA2E4 |
| `PAVAnimTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA304 |
| `PAVBombClass::?$DynamicVectorClass` | `PAVBombClass::?$VectorClass` | 1 | 7 | 0x7E17CC |
| `PAVBombClass::?$VectorClass` | — | 1 | 7 | 0x7E17EC |
| `PAVBuildingClass::?$DynamicVectorClass` | `PAVBuildingClass::?$VectorClass` | 1 | 7 | 0x7E9E24 |
| `PAVBuildingClass::?$VectorClass` | — | 1 | 7 | 0x7E9E44 |
| `PAVBuildingLightClass::?$DynamicVectorClass` | `PAVBuildingLightClass::?$VectorClass` | 1 | 7 | 0x7E9C24 |
| `PAVBuildingLightClass::?$VectorClass` | — | 1 | 7 | 0x7E9C44 |
| `PAVBuildingTypeClass::?$DynamicVectorClass` | `PAVBuildingTypeClass::?$VectorClass` | 1 | 7 | 0x7EA224 |
| `PAVBuildingTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA244 |
| `PAVBulletClass::?$DynamicVectorClass` | `PAVBulletClass::?$VectorClass` | 1 | 7 | 0x7E4678 |
| `PAVBulletClass::?$VectorClass` | — | 1 | 7 | 0x7E4698 |
| `PAVBulletTypeClass::?$DynamicVectorClass` | `PAVBulletTypeClass::?$VectorClass` | 1 | 7 | 0x7EA364 |
| `PAVBulletTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA384 |
| `PAVCampaignClass::?$DynamicVectorClass` | `PAVCampaignClass::?$VectorClass` | 1 | 7 | 0x7E9FE4 |
| `PAVCampaignClass::?$VectorClass` | — | 1 | 7 | 0x7EA004 |
| `PAVCaptureManagerClass::?$DynamicVectorClass` | `PAVCaptureManagerClass::?$VectorClass` | 1 | 7 | 0x7E4AD4 |
| `PAVCaptureManagerClass::?$VectorClass` | — | 1 | 7 | 0x7E4AF4 |
| `PAVCCINIClass::?$DynamicVectorClass` | `PAVCCINIClass::?$VectorClass` | 1 | 7 | 0x7EB82C |
| `PAVCCINIClass::?$VectorClass` | — | 1 | 7 | 0x7EB80C |
| `PAVCellClass::?$DynamicVectorClass` | `PAVCellClass::?$VectorClass` | 1 | 7 | 0x7ED9BC |
| `PAVCellClass::?$VectorClass` | — | 1 | 7 | 0x7ED480 |
| `PAVColorScheme::?$DynamicVectorClass` | `PAVColorScheme::?$VectorClass` | 1 | 7 | 0x7EF790 |
| `PAVColorScheme::?$VectorClass` | — | 1 | 7 | 0x7EF7B0 |
| `PAVConvertClass::?$DynamicVectorClass` | `PAVConvertClass::?$VectorClass` | 1 | 7 | 0x7E5318 |
| `PAVConvertClass::?$VectorClass` | — | 1 | 7 | 0x7E5338 |
| `PAVCoopCampaignClass::?$DynamicVectorClass` | `PAVCoopCampaignClass::?$VectorClass` | 1 | 7 | 0x7EE350 |
| `PAVCoopCampaignClass::?$VectorClass` | — | 1 | 7 | 0x7EE3B0 |
| `PAVDiskLaserClass::?$DynamicVectorClass` | `PAVDiskLaserClass::?$VectorClass` | 1 | 7 | 0x7E5EDC |
| `PAVDiskLaserClass::?$VectorClass` | — | 1 | 7 | 0x7E5EFC |
| `PAVEBolt::?$DynamicVectorClass` | `PAVEBolt::?$VectorClass` | 1 | 7 | 0x7E868C |
| `PAVEBolt::?$VectorClass` | — | 1 | 7 | 0x7E86AC |
| `PAVEgoClass::?$DynamicVectorClass` | `PAVEgoClass::?$VectorClass` | 1 | 7 | 0x7E86DC |
| `PAVEgoClass::?$VectorClass` | — | 1 | 7 | 0x7E86FC |
| `PAVEMPulseClass::?$DynamicVectorClass` | `PAVEMPulseClass::?$VectorClass` | 1 | 7 | 0x7E873C |
| `PAVEMPulseClass::?$VectorClass` | — | 1 | 7 | 0x7E875C |
| `PAVEventClass::?$DynamicVectorClass` | `PAVEventClass::?$VectorClass` | 1 | 7 | 0x7EFE04 |
| `PAVEventClass::?$VectorClass` | — | 1 | 7 | 0x7EFE24 |
| `PAVFactoryClass::?$DynamicVectorClass` | `PAVFactoryClass::?$VectorClass` | 1 | 7 | 0x7E9FA4 |
| `PAVFactoryClass::?$VectorClass` | — | 1 | 7 | 0x7E9FC4 |
| `PAVFileEntryClass::?$DynamicVectorClass` | `PAVFileEntryClass::?$VectorClass` | 1 | 7 | 0x7ED30C |
| `PAVFileEntryClass::?$VectorClass` | — | 1 | 7 | 0x7ED32C |
| `PAVFoggedObjectClass::?$DynamicVectorClass` | `PAVFoggedObjectClass::?$VectorClass` | 1 | 7 | 0x7E44F4 |
| `PAVFoggedObjectClass::?$VectorClass` | — | 1 | 7 | 0x7E4514 |
| `PAVFootClass::?$DynamicVectorClass` | `PAVFootClass::?$VectorClass` | 1 | 7 | 0x7E8C28 |
| `PAVFootClass::?$VectorClass` | — | 1 | 7 | 0x7E8C48 |
| `PAVGraphicMenuItem::?$DynamicVectorClass` | `PAVGraphicMenuItem::?$VectorClass` | 1 | 7 | 0x7EA604 |
| `PAVGraphicMenuItem::?$VectorClass` | — | 1 | 7 | 0x7EA624 |
| `PAVGraphicMenuItem::V?$DynamicVectorClass::PAVGraphicMenuItem::?$VectorCursor` | — | 1 | 4 | 0x7EA644 |
| `PAVHouseClass::?$DynamicVectorClass` | `PAVHouseClass::?$VectorClass` | 1 | 7 | 0x7E9EE4 |
| `PAVHouseClass::?$VectorClass` | — | 1 | 7 | 0x7E9F04 |
| `PAVHouseTypeClass::?$DynamicVectorClass` | `PAVHouseTypeClass::?$VectorClass` | 1 | 7 | 0x7EA064 |
| `PAVHouseTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA084 |
| `PAVInfantryClass::?$DynamicVectorClass` | `PAVInfantryClass::?$VectorClass` | 1 | 7 | 0x7E43C8 |
| `PAVInfantryClass::?$VectorClass` | — | 1 | 7 | 0x7E43E8 |
| `PAVInfantryTypeClass::?$DynamicVectorClass` | `PAVInfantryTypeClass::?$VectorClass` | 1 | 7 | 0x7EA324 |
| `PAVInfantryTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA344 |
| `PAVIonBlastClass::?$DynamicVectorClass` | `PAVIonBlastClass::?$VectorClass` | 1 | 7 | 0x7EC05C |
| `PAVIonBlastClass::?$VectorClass` | — | 1 | 7 | 0x7EC07C |
| `PAVIsometricTileClass::?$DynamicVectorClass` | `PAVIsometricTileClass::?$VectorClass` | 1 | 7 | 0x7E18BC |
| `PAVIsometricTileClass::?$VectorClass` | — | 1 | 7 | 0x7E18DC |
| `PAVIsometricTileTypeClass::?$DynamicVectorClass` | `PAVIsometricTileTypeClass::?$VectorClass` | 1 | 7 | 0x7EA3E4 |
| `PAVIsometricTileTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA404 |
| `PAVLaserDrawClass::?$DynamicVectorClass` | `PAVLaserDrawClass::?$VectorClass` | 1 | 7 | 0x7ECEDC |
| `PAVLaserDrawClass::?$VectorClass` | — | 1 | 7 | 0x7ECEFC |
| `PAVLightConvertClass::?$DynamicVectorClass` | `PAVLightConvertClass::?$VectorClass` | 1 | 7 | 0x7E186C |
| `PAVLightConvertClass::?$VectorClass` | — | 1 | 7 | 0x7E188C |
| `PAVLightSourceClass::?$DynamicVectorClass` | `PAVLightSourceClass::?$VectorClass` | 1 | 7 | 0x7ECF7C |
| `PAVLightSourceClass::?$VectorClass` | — | 1 | 7 | 0x7ECF9C |
| `PAVLineTrail::?$DynamicVectorClass` | `PAVLineTrail::?$VectorClass` | 1 | 7 | 0x7ED0CC |
| `PAVLineTrail::?$VectorClass` | — | 1 | 7 | 0x7ED0EC |
| `PAVMapRegionClass::?$DynamicVectorClass` | `PAVMapRegionClass::?$VectorClass` | 1 | 7 | 0x7ED858 |
| `PAVMapRegionClass::?$VectorClass` | — | 1 | 7 | 0x7ED878 |
| `PAVMapSelection::?$DynamicVectorClass` | `PAVMapSelection::?$VectorClass` | 1 | 7 | 0x7EEB14 |
| `PAVMapSelection::?$VectorClass` | — | 1 | 7 | 0x7EEBB4 |
| `PAVMapStage::?$DynamicVectorClass` | `PAVMapStage::?$VectorClass` | 1 | 7 | 0x7EEA94 |
| `PAVMapStage::?$VectorClass` | — | 1 | 7 | 0x7EEAB4 |
| `PAVMixFileClass::?$DynamicVectorClass` | `PAVMixFileClass::?$VectorClass` | 1 | 7 | 0x7E1A44 |
| `PAVMixFileClass::?$List` | `GenericList` | 1 | 1 | 0x7EDF38 |
| `PAVMixFileClass::?$VectorClass` | — | 1 | 7 | 0x7E1A64 |
| `PAVMovieHandle::?$DynamicVectorClass` | `PAVMovieHandle::?$VectorClass` | 1 | 7 | 0x7F6984 |
| `PAVMovieHandle::?$VectorClass` | — | 1 | 7 | 0x7F69A4 |
| `PAVMSAnim::?$DynamicVectorClass` | `PAVMSAnim::?$VectorClass` | 1 | 7 | 0x7EEC04 |
| `PAVMSAnim::?$VectorClass` | — | 1 | 7 | 0x7EEC24 |
| `PAVMSAnim::V?$DynamicVectorClass::PAVMSAnim::?$VectorCursor` | — | 1 | 4 | 0x7F72EC |
| `PAVMSAnimEntry::?$DynamicVectorClass` | `PAVMSAnimEntry::?$VectorClass` | 1 | 7 | 0x7EEA74 |
| `PAVMSAnimEntry::?$VectorClass` | — | 1 | 7 | 0x7EEAD4 |
| `PAVMSSfx::?$DynamicVectorClass` | `PAVMSSfx::?$VectorClass` | 1 | 7 | 0x7EEBE4 |
| `PAVMSSfx::?$VectorClass` | — | 1 | 7 | 0x7EEC44 |
| `PAVMSSfxEntry::?$DynamicVectorClass` | `PAVMSSfxEntry::?$VectorClass` | 1 | 7 | 0x7EEA54 |
| `PAVMSSfxEntry::?$VectorClass` | — | 1 | 7 | 0x7EEAF4 |
| `PAVMSSfxEntry::V?$DynamicVectorClass::PAVMSSfxEntry::?$VectorCursor` | — | 1 | 4 | 0x7F72C4 |
| `PAVMSTextEntry::?$DynamicVectorClass` | `PAVMSTextEntry::?$VectorClass` | 1 | 7 | 0x7EEB34 |
| `PAVMSTextEntry::?$VectorClass` | — | 1 | 7 | 0x7EEB94 |
| `PAVMultiMission::?$DynamicVectorClass` | `PAVMultiMission::?$VectorClass` | 1 | 7 | 0x7F11F4 |
| `PAVMultiMission::?$VectorClass` | — | 1 | 7 | 0x7F1214 |
| `PAVMultiplayerGameMode::?$DynamicVectorClass` | `PAVMultiplayerGameMode::?$VectorClass` | 1 | 7 | 0x7EED20 |
| `PAVMultiplayerGameMode::?$VectorClass` | — | 1 | 7 | 0x7EED40 |
| `PAVMultiplayerTeam::?$DynamicVectorClass` | `PAVMultiplayerTeam::?$VectorClass` | 1 | 7 | 0x7EEE34 |
| `PAVMultiplayerTeam::?$VectorClass` | — | 1 | 7 | 0x7EEE54 |
| `PAVNeuronClass::?$VectorClass` | — | 1 | 7 | 0x7E3E54 |
| `PAVObjectClass::?$DynamicVectorClass` | `PAVObjectClass::?$VectorClass` | 1 | 7 | 0x7E4F64 |
| `PAVObjectClass::?$VectorClass` | — | 1 | 7 | 0x7E192C |
| `PAVObjectTypeClass::?$DynamicVectorClass` | `PAVObjectTypeClass::?$VectorClass` | 1 | 7 | 0x7EF26C |
| `PAVObjectTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EF28C |
| `PAVOverlayClass::?$DynamicVectorClass` | `PAVOverlayClass::?$VectorClass` | 1 | 7 | 0x7E9D24 |
| `PAVOverlayClass::?$VectorClass` | — | 1 | 7 | 0x7E9D44 |
| `PAVOverlayTypeClass::?$DynamicVectorClass` | `PAVOverlayTypeClass::?$VectorClass` | 1 | 7 | 0x7EA164 |
| `PAVOverlayTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA184 |
| `PAVParasiteClass::?$DynamicVectorClass` | `PAVParasiteClass::?$VectorClass` | 1 | 7 | 0x7EF824 |
| `PAVParasiteClass::?$VectorClass` | — | 1 | 7 | 0x7EF844 |
| `PAVParticleClass::?$DynamicVectorClass` | `PAVParticleClass::?$VectorClass` | 1 | 7 | 0x7E9D64 |
| `PAVParticleClass::?$VectorClass` | — | 1 | 7 | 0x7E9D84 |
| `PAVParticleSystemClass::?$DynamicVectorClass` | `PAVParticleSystemClass::?$VectorClass` | 1 | 7 | 0x7E9C64 |
| `PAVParticleSystemClass::?$VectorClass` | — | 1 | 7 | 0x7E9C84 |
| `PAVParticleSystemTypeClass::?$DynamicVectorClass` | `PAVParticleSystemTypeClass::?$VectorClass` | 1 | 7 | 0x7EA464 |
| `PAVParticleSystemTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA484 |
| `PAVParticleTypeClass::?$DynamicVectorClass` | `PAVParticleTypeClass::?$VectorClass` | 1 | 7 | 0x7EA424 |
| `PAVParticleTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA444 |
| `PAVPhoneEntryClass::?$DynamicVectorClass` | `PAVPhoneEntryClass::?$VectorClass` | 1 | 7 | 0x7F11B4 |
| `PAVPhoneEntryClass::?$VectorClass` | — | 1 | 7 | 0x7F1254 |
| `PAVPlanningBranchClass::?$DynamicVectorClass` | `PAVPlanningBranchClass::?$VectorClass` | 1 | 7 | 0x7EFEC4 |
| `PAVPlanningBranchClass::?$VectorClass` | — | 1 | 7 | 0x7EFF24 |
| `PAVPlanningMemberClass::?$DynamicVectorClass` | `PAVPlanningMemberClass::?$VectorClass` | 1 | 7 | 0x7EFEE4 |
| `PAVPlanningMemberClass::?$VectorClass` | — | 1 | 7 | 0x7EFF04 |
| `PAVPlanningNodeClass::?$DynamicVectorClass` | `PAVPlanningNodeClass::?$VectorClass` | 1 | 7 | 0x7EFE44 |
| `PAVPlanningNodeClass::?$VectorClass` | — | 1 | 7 | 0x7EFE64 |
| `PAVPlanningTokenClass::?$DynamicVectorClass` | `PAVPlanningTokenClass::?$VectorClass` | 1 | 7 | 0x7EFE84 |
| `PAVPlanningTokenClass::?$VectorClass` | — | 1 | 7 | 0x7EFEA4 |
| `PAVRadarEventClass::?$DynamicVectorClass` | `PAVRadarEventClass::?$VectorClass` | 1 | 7 | 0x7F0AAC |
| `PAVRadarEventClass::?$VectorClass` | — | 1 | 7 | 0x7F0ACC |
| `PAVRadBeam::?$DynamicVectorClass` | `PAVRadBeam::?$VectorClass` | 1 | 7 | 0x7F0484 |
| `PAVRadBeam::?$VectorClass` | — | 1 | 7 | 0x7F04A4 |
| `PAVRadSiteClass::?$DynamicVectorClass` | `PAVRadSiteClass::?$VectorClass` | 1 | 7 | 0x7F07A4 |
| `PAVRadSiteClass::?$VectorClass` | — | 1 | 7 | 0x7F07C4 |
| `PAVReestablish::?$DynamicVectorClass` | `PAVReestablish::?$VectorClass` | 1 | 7 | 0x7E4488 |
| `PAVReestablish::?$VectorClass` | — | 1 | 7 | 0x7E4468 |
| `PAVSchemeNode::VHashString::U?$HashObject::?$DynamicVectorClass` | `PAVSchemeNode::VHashString::U?$HashObject::?$VectorClass` | 1 | 7 | 0x7EF770 |
| `PAVSchemeNode::VHashString::U?$HashObject::?$VectorClass` | — | 1 | 7 | 0x7EF7D0 |
| `PAVScriptClass::?$DynamicVectorClass` | `PAVScriptClass::?$VectorClass` | 1 | 7 | 0x7E1B24 |
| `PAVScriptClass::?$VectorClass` | — | 1 | 7 | 0x7E1B44 |
| `PAVScriptTypeClass::?$DynamicVectorClass` | `PAVScriptTypeClass::?$VectorClass` | 1 | 7 | 0x7EA124 |
| `PAVScriptTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA144 |
| `PAVShadowControlClass::?$DynamicVectorClass` | `PAVShadowControlClass::?$VectorClass` | 1 | 7 | 0x7F42DC |
| `PAVShadowControlClass::?$VectorClass` | — | 1 | 7 | 0x7F42FC |
| `PAVSideClass::?$DynamicVectorClass` | `PAVSideClass::?$VectorClass` | 1 | 7 | 0x7EA024 |
| `PAVSideClass::?$VectorClass` | — | 1 | 7 | 0x7EA044 |
| `PAVSlaveManagerClass::?$DynamicVectorClass` | `PAVSlaveManagerClass::?$VectorClass` | 1 | 7 | 0x7F315C |
| `PAVSlaveManagerClass::?$VectorClass` | — | 1 | 7 | 0x7F317C |
| `PAVSmudgeClass::?$DynamicVectorClass` | `PAVSmudgeClass::?$VectorClass` | 1 | 7 | 0x7E9DA4 |
| `PAVSmudgeClass::?$VectorClass` | — | 1 | 7 | 0x7E9DC4 |
| `PAVSmudgeTypeClass::?$DynamicVectorClass` | `PAVSmudgeTypeClass::?$VectorClass` | 1 | 7 | 0x7EA1A4 |
| `PAVSmudgeTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA1C4 |
| `PAVSpawnManagerClass::?$DynamicVectorClass` | `PAVSpawnManagerClass::?$VectorClass` | 1 | 7 | 0x7F35E4 |
| `PAVSpawnManagerClass::?$VectorClass` | — | 1 | 7 | 0x7F3604 |
| `PAVSpotLightClass::?$DynamicVectorClass` | `PAVSpotLightClass::?$VectorClass` | 1 | 7 | 0x7EF6BC |
| `PAVSpotLightClass::?$VectorClass` | — | 1 | 7 | 0x7EF6DC |
| `PAVSubTitle::?$DynamicVectorClass` | `PAVSubTitle::?$VectorClass` | 1 | 7 | 0x7F3F6C |
| `PAVSubTitle::?$VectorClass` | — | 1 | 7 | 0x7F3F8C |
| `PAVSuperClass::?$DynamicVectorClass` | `PAVSuperClass::?$VectorClass` | 1 | 7 | 0x7EA4E4 |
| `PAVSuperClass::?$VectorClass` | — | 1 | 7 | 0x7EA504 |
| `PAVSuperWeaponTypeClass::?$DynamicVectorClass` | `PAVSuperWeaponTypeClass::?$VectorClass` | 1 | 7 | 0x7EA4A4 |
| `PAVSuperWeaponTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA4C4 |
| `PAVTActionClass::?$DynamicVectorClass` | `PAVTActionClass::?$VectorClass` | 1 | 7 | 0x7F43D0 |
| `PAVTActionClass::?$VectorClass` | — | 1 | 7 | 0x7F43F0 |
| `PAVTagClass::?$DynamicVectorClass` | `PAVTagClass::?$VectorClass` | 1 | 7 | 0x7EA5A4 |
| `PAVTagClass::?$VectorClass` | — | 1 | 7 | 0x7EA5C4 |
| `PAVTagTypeClass::?$DynamicVectorClass` | `PAVTagTypeClass::?$VectorClass` | 1 | 7 | 0x7F4558 |
| `PAVTagTypeClass::?$VectorClass` | — | 1 | 7 | 0x7F4578 |
| `PAVTaskForceClass::?$DynamicVectorClass` | `PAVTaskForceClass::?$VectorClass` | 1 | 7 | 0x7EA0A4 |
| `PAVTaskForceClass::?$VectorClass` | — | 1 | 7 | 0x7EA0C4 |
| `PAVTeamClass::?$DynamicVectorClass` | `PAVTeamClass::?$VectorClass` | 1 | 7 | 0x7E9F64 |
| `PAVTeamClass::?$VectorClass` | — | 1 | 7 | 0x7E9F84 |
| `PAVTeamTypeClass::?$DynamicVectorClass` | `PAVTeamTypeClass::?$VectorClass` | 1 | 7 | 0x7EA0E4 |
| `PAVTeamTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA104 |
| `PAVTechnoClass::?$DynamicVectorClass` | `PAVTechnoClass::?$VectorClass` | 1 | 7 | 0x7E17AC |
| `PAVTechnoClass::?$VectorClass` | — | 1 | 7 | 0x7E180C |
| `PAVTechnoClass::URadarTrackingStruct::U?$HashObject::?$DynamicVectorClass` | `PAVTechnoClass::URadarTrackingStruct::U?$HashObject::?$VectorClass` | 1 | 7 | 0x7F042C |
| `PAVTechnoClass::URadarTrackingStruct::U?$HashObject::?$VectorClass` | — | 1 | 7 | 0x7F044C |
| `PAVTechnoTypeClass::?$DynamicVectorClass` | `PAVTechnoTypeClass::?$VectorClass` | 1 | 7 | 0x7E858C |
| `PAVTechnoTypeClass::?$TypeList` | `PAVTechnoTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7E4E18 |
| `PAVTechnoTypeClass::?$VectorClass` | — | 1 | 7 | 0x7E4DF8 |
| `PAVTemporalClass::?$DynamicVectorClass` | `PAVTemporalClass::?$VectorClass` | 1 | 7 | 0x7F5114 |
| `PAVTemporalClass::?$VectorClass` | — | 1 | 7 | 0x7F5134 |
| `PAVTerrainClass::?$DynamicVectorClass` | `PAVTerrainClass::?$VectorClass` | 1 | 7 | 0x7E9DE4 |
| `PAVTerrainClass::?$VectorClass` | — | 1 | 7 | 0x7E9E04 |
| `PAVTerrainTypeClass::?$DynamicVectorClass` | `PAVTerrainTypeClass::?$VectorClass` | 1 | 7 | 0x7EA1E4 |
| `PAVTerrainTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA204 |
| `PAVTEventClass::?$DynamicVectorClass` | `PAVTEventClass::?$VectorClass` | 1 | 7 | 0x7F550C |
| `PAVTEventClass::?$VectorClass` | — | 1 | 7 | 0x7F552C |
| `PAVTiberiumClass::?$DynamicVectorClass` | `PAVTiberiumClass::?$VectorClass` | 1 | 7 | 0x7F56BC |
| `PAVTiberiumClass::?$VectorClass` | — | 1 | 7 | 0x7F56DC |
| `PAVTriggerClass::?$DynamicVectorClass` | `PAVTriggerClass::?$VectorClass` | 1 | 7 | 0x7E9BE4 |
| `PAVTriggerClass::?$VectorClass` | — | 1 | 7 | 0x7E9C04 |
| `PAVTriggerTypeClass::?$DynamicVectorClass` | `PAVTriggerTypeClass::?$VectorClass` | 1 | 7 | 0x7E9BA4 |
| `PAVTriggerTypeClass::?$VectorClass` | — | 1 | 7 | 0x7E9BC4 |
| `PAVTubeClass::?$DynamicVectorClass` | `PAVTubeClass::?$VectorClass` | 1 | 7 | 0x7E9CA4 |
| `PAVTubeClass::?$VectorClass` | — | 1 | 7 | 0x7E9CC4 |
| `PAVUnitClass::?$DynamicVectorClass` | `PAVUnitClass::?$VectorClass` | 1 | 7 | 0x7E9EA4 |
| `PAVUnitClass::?$VectorClass` | — | 1 | 7 | 0x7E9EC4 |
| `PAVUnitTypeClass::?$DynamicVectorClass` | `PAVUnitTypeClass::?$VectorClass` | 1 | 7 | 0x7EA2A4 |
| `PAVUnitTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA2C4 |
| `PAVVeinholeMonsterClass::?$DynamicVectorClass` | `PAVVeinholeMonsterClass::?$VectorClass` | 1 | 7 | 0x7F663C |
| `PAVVeinholeMonsterClass::?$VectorClass` | — | 1 | 7 | 0x7F665C |
| `PAVVocClass::?$DynamicVectorClass` | `PAVVocClass::?$VectorClass` | 1 | 7 | 0x7F68AC |
| `PAVVocClass::?$VectorClass` | — | 1 | 7 | 0x7F68CC |
| `PAVVoxClass::?$DynamicVectorClass` | `PAVVoxClass::?$VectorClass` | 1 | 7 | 0x7F6904 |
| `PAVVoxClass::?$VectorClass` | — | 1 | 7 | 0x7F6924 |
| `PAVVoxelAnimClass::?$DynamicVectorClass` | `PAVVoxelAnimClass::?$VectorClass` | 1 | 7 | 0x7E1E2C |
| `PAVVoxelAnimClass::?$VectorClass` | — | 1 | 7 | 0x7E1E4C |
| `PAVVoxelAnimTypeClass::?$DynamicVectorClass` | `PAVVoxelAnimTypeClass::?$VectorClass` | 1 | 7 | 0x7EA3A4 |
| `PAVVoxelAnimTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA3C4 |
| `PAVWarheadTypeClass::?$DynamicVectorClass` | `PAVWarheadTypeClass::?$VectorClass` | 1 | 7 | 0x7E1E84 |
| `PAVWarheadTypeClass::?$VectorClass` | — | 1 | 7 | 0x7E1EA4 |
| `PAVWaveClass::?$DynamicVectorClass` | `PAVWaveClass::?$VectorClass` | 1 | 7 | 0x7E9CE4 |
| `PAVWaveClass::?$VectorClass` | — | 1 | 7 | 0x7E9D04 |
| `PAVWaypointPathClass::?$DynamicVectorClass` | `PAVWaypointPathClass::?$VectorClass` | 1 | 7 | 0x7F6E04 |
| `PAVWaypointPathClass::?$VectorClass` | — | 1 | 7 | 0x7F6E24 |
| `PAVWeaponTypeClass::?$DynamicVectorClass` | `PAVWeaponTypeClass::?$VectorClass` | 1 | 7 | 0x7E1ED4 |
| `PAVWeaponTypeClass::?$VectorClass` | — | 1 | 7 | 0x7E1EF4 |
| `PBD::?$DynamicVectorClass` | `PBD::?$VectorClass` | 1 | 7 | 0x7EE0B4 |
| `PBD::?$VectorClass` | — | 1 | 7 | 0x7EE0D4 |
| `PBG::?$DynamicVectorClass` | `PBG::?$VectorClass` | 1 | 7 | 0x7ED1DC |
| `PBG::?$VectorClass` | — | 1 | 7 | 0x7ED1FC |
| `PBVAircraftTypeClass::?$DynamicVectorClass` | `PBVAircraftTypeClass::?$VectorClass` | 1 | 7 | 0x7EACC8 |
| `PBVAircraftTypeClass::?$TypeList` | `PBVAircraftTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7EABC8 |
| `PBVAircraftTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EAC68 |
| `PBVAnimClass::?$DynamicVectorClass` | `PBVAnimClass::?$VectorClass` | 1 | 7 | 0x7EBFCC |
| `PBVAnimClass::?$VectorClass` | — | 1 | 7 | 0x7EBFEC |
| `PBVAnimTypeClass::?$DynamicVectorClass` | `PBVAnimTypeClass::?$VectorClass` | 1 | 7 | 0x7EB714 |
| `PBVAnimTypeClass::?$TypeList` | `PBVAnimTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7EB6D4 |
| `PBVAnimTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EB6F4 |
| `PBVBuildingTypeClass::?$DynamicVectorClass` | `PBVBuildingTypeClass::?$VectorClass` | 1 | 7 | 0x7EAA28 |
| `PBVBuildingTypeClass::?$TypeList` | `PBVBuildingTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7ED90C |
| `PBVBuildingTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EAA08 |
| `PBVCommandClass::?$DynamicVectorClass` | `PBVCommandClass::?$VectorClass` | 1 | 7 | 0x7E182C |
| `PBVCommandClass::?$VectorClass` | — | 1 | 7 | 0x7E184C |
| `PBVInfantryTypeClass::?$DynamicVectorClass` | `PBVInfantryTypeClass::?$VectorClass` | 1 | 7 | 0x7EAC88 |
| `PBVInfantryTypeClass::?$TypeList` | `PBVInfantryTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7EAC08 |
| `PBVInfantryTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EAC28 |
| `PBVMultiMission::?$DynamicVectorClass` | `PBVMultiMission::?$VectorClass` | 1 | 7 | 0x7EEF70 |
| `PBVMultiMission::?$VectorClass` | — | 1 | 7 | 0x7EEF50 |
| `PBVParticleSystemTypeClass::?$DynamicVectorClass` | `PBVParticleSystemTypeClass::?$VectorClass` | 1 | 7 | 0x7E4444 |
| `PBVParticleSystemTypeClass::?$TypeList` | `PBVParticleSystemTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7F4F9C |
| `PBVParticleSystemTypeClass::?$VectorClass` | — | 1 | 7 | 0x7E4424 |
| `PBVSmudgeTypeClass::?$DynamicVectorClass` | `PBVSmudgeTypeClass::?$VectorClass` | 1 | 7 | 0x7F0DEC |
| `PBVSmudgeTypeClass::?$TypeList` | `PBVSmudgeTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7F0D1C |
| `PBVSmudgeTypeClass::?$VectorClass` | — | 1 | 7 | 0x7F0D7C |
| `PBVTeamTypeClass::?$DynamicVectorClass` | `PBVTeamTypeClass::?$VectorClass` | 1 | 7 | 0x7EAAE8 |
| `PBVTeamTypeClass::?$TypeList` | `PBVTeamTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7EA9C4 |
| `PBVTeamTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EA9E4 |
| `PBVTechnoTypeClass::?$DynamicVectorClass` | `PBVTechnoTypeClass::?$VectorClass` | 1 | 7 | 0x7E8934 |
| `PBVTechnoTypeClass::?$VectorClass` | — | 1 | 7 | 0x7E8954 |
| `PBVTerrainTypeClass::?$DynamicVectorClass` | `PBVTerrainTypeClass::?$VectorClass` | 1 | 7 | 0x7F0E0C |
| `PBVTerrainTypeClass::?$TypeList` | `PBVTerrainTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7F0CFC |
| `PBVTerrainTypeClass::?$VectorClass` | — | 1 | 7 | 0x7F0D9C |
| `PBVToolTip::?$DynamicVectorClass` | `PBVToolTip::?$VectorClass` | 1 | 7 | 0x7F57C8 |
| `PBVToolTip::?$VectorClass` | — | 1 | 7 | 0x7F57E8 |
| `PBVUnitTypeClass::?$DynamicVectorClass` | `PBVUnitTypeClass::?$VectorClass` | 1 | 7 | 0x7EACA8 |
| `PBVUnitTypeClass::?$TypeList` | `PBVUnitTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7EABE8 |
| `PBVUnitTypeClass::?$VectorClass` | — | 1 | 7 | 0x7EAC48 |
| `PBVVoxelAnimTypeClass::?$DynamicVectorClass` | `PBVVoxelAnimTypeClass::?$VectorClass` | 1 | 7 | 0x7F0DCC |
| `PBVVoxelAnimTypeClass::?$TypeList` | `PBVVoxelAnimTypeClass::?$DynamicVectorClass` | 1 | 7 | 0x7F0D3C |
| `PBVVoxelAnimTypeClass::?$VectorClass` | — | 1 | 7 | 0x7F0D5C |
| `Pipe` | — | 1 | 5 | 0x7E6218 |
| `PixelFXClass` | — | 1 | 1 | 0x7EFDA4 |
| `PKPipe` | `Pipe` | 1 | 6 | 0x7EFDAC |
| `PKStraw` | `Straw` | 1 | 4 | 0x7EFDE0 |
| `PlanningModeCommandClass` | `CommandClass` | 1 | 9 | 0x7EB9B4 |
| `PlayerProfile` | `ReferenceCounted` | 1 | 3 | 0x7F74F4 |
| `PowerClass` | `RadarClass` | 1 | 54 | 0x7EFF54 |
| `PrevObjectCommandClass` | `CommandClass` | 1 | 9 | 0x7EBA04 |
| `ProgressScreenClass` | `INoticeSource` | 1 | 1 | 0x7F0064 |
| `RadarClass` | `DisplayClass` | 1 | 54 | 0x7F0344 |
| `RadarClass::RTacticalClass` | `GadgetClass` | 1 | 33 | 0x7F02BC |
| `RadioClass` | `MissionClass` | 4 | 161 | 0x7F0508, 0x7F04EC, 0x7F04E4 |
| `RadSiteClass` | `AbstractClass` | 4 | 24 | 0x7F0810, 0x7F07F4, 0x7F07EC |
| `RAMFileClass` | `FileClass` | 1 | 17 | 0x7F0874 |
| `RandomStraw` | `Straw` | 1 | 3 | 0x7F0AFC |
| `RawFileClass` | `FileClass` | 1 | 17 | 0x7F0904 |
| `rc_ptr_base` | — | 1 | 1 | 0x7F094C |
| `ReferenceCounted` | — | 1 | 3 | 0x7F0954 |
| `RLEBlitter` | — | 1 | 3 | 0x7E5BA0 |
| `RocketLocomotionClass` | `LocomotionClass` | 2 | 50 | 0x7F0BE8, 0x7F0B1C |
| `ScatterCommandClass` | `CommandClass` | 1 | 9 | 0x7EBACC |
| `ScoreAnimClass` | — | 1 | 4 | 0x7F0EDC |
| `ScoreBigFontClass` | `ScoreFontClass` | 1 | 5 | 0x7F0F20 |
| `ScoreFontClass` | — | 1 | 5 | 0x7F0EF0 |
| `ScoreFullFontClass` | `ScoreFontClass` | 1 | 5 | 0x7F0F08 |
| `ScorePrintClass` | `ScoreAnimClass` | 1 | 4 | 0x7F0EB4 |
| `ScoreTimeClass` | `ScoreAnimClass` | 1 | 4 | 0x7F0EC8 |
| `ScreenCaptureCommandClass` | `CommandClass` | 1 | 9 | 0x7EBF04 |
| `ScriptClass` | `AbstractClass` | 4 | 24 | 0x7F0F78, 0x7F0F5C, 0x7F0F54 |
| `ScriptTypeClass` | `AbstractTypeClass` | 4 | 27 | 0x7F1008, 0x7F0FEC, 0x7F0FE4 |
| `ScrollClass` | `TabClass` | 2 | 55 | 0x7F1094, 0x7F108C |
| `SelectTeamCommandClass` | `CommandClass` | 1 | 9 | 0x7EBE64 |
| `SetDefenseTabCommandClass` | `CommandClass` | 1 | 9 | 0x7EB8C4 |
| `SetInfantryTabCommandClass` | `CommandClass` | 1 | 9 | 0x7EB874 |
| `SetStructureTabCommandClass` | `CommandClass` | 1 | 9 | 0x7EB8EC |
| `SetUnitTabCommandClass` | `CommandClass` | 1 | 9 | 0x7EB89C |
| `SetView1CommandClass` | `CommandClass` | 1 | 9 | 0x7EBCFC |
| `SetView2CommandClass` | `CommandClass` | 1 | 9 | 0x7EBCD4 |
| `SetView3CommandClass` | `CommandClass` | 1 | 9 | 0x7EBCAC |
| `SetView4CommandClass` | `CommandClass` | 1 | 9 | 0x7EBC84 |
| `ShapeButtonClass` | `ToggleClass` | 1 | 35 | 0x7E8088 |
| `SHAPipe` | `Pipe` | 1 | 5 | 0x7E4D78 |
| `ShipLocomotionClass` | `LocomotionClass` | 3 | 50 | 0x7F2E58, 0x7F2D8C, 0x7F2D68 |
| `SidebarClass` | `PowerClass` | 1 | 55 | 0x7F3058 |
| `SidebarClass::SBGadgetClass` | `GadgetClass` | 1 | 33 | 0x7F2F44 |
| `SidebarClass::StripClass::SelectClass` | `ControlClass` | 1 | 34 | 0x7F2FCC |
| `SidebarDownCommandClass` | `CommandClass` | 1 | 9 | 0x7EBC0C |
| `SidebarUpCommandClass` | `CommandClass` | 1 | 9 | 0x7EBC34 |
| `SideClass` | `AbstractTypeClass` | 4 | 27 | 0x7F2EC0, 0x7F2EA4, 0x7F2E9C |
| `SimpleWonlineDialogControl` | `OwnerDraw::SimpleDialogControl` | 1 | 5 | 0x7F7624 |
| `SlaveManagerClass` | `AbstractClass` | 4 | 24 | 0x7F31C8, 0x7F31AC, 0x7F31A4 |
| `SlaveManagerClass::PAUSlaveControl::?$DynamicVectorClass` | `SlaveManagerClass::PAUSlaveControl::?$VectorClass` | 1 | 7 | 0x7F322C |
| `SlaveManagerClass::PAUSlaveControl::?$VectorClass` | — | 1 | 7 | 0x7F324C |
| `SliderClass` | `GaugeClass` | 1 | 45 | 0x7ED21C |
| `SmudgeClass` | `ObjectClass` | 4 | 122 | 0x7F32FC, 0x7F32D8, 0x7F32F4 |
| `SmudgeTypeClass` | `ObjectTypeClass` | 4 | 41 | 0x7F3528, 0x7F350C, 0x7F3504 |
| `SpawnManagerClass` | `AbstractClass` | 4 | 24 | 0x7F3650, 0x7F3634, 0x7F362C |
| `SpawnManagerClass::PAUSpawnControl::?$DynamicVectorClass` | `SpawnManagerClass::PAUSpawnControl::?$VectorClass` | 1 | 7 | 0x7F36B4 |
| `SpawnManagerClass::PAUSpawnControl::?$VectorClass` | — | 1 | 7 | 0x7F36D4 |
| `StaticButtonClass` | `GadgetClass` | 1 | 36 | 0x7F3EA0 |
| `StopCommandClass` | `CommandClass` | 1 | 9 | 0x7EBA7C |
| `Straw` | — | 1 | 3 | 0x7E61F0 |
| `SuperClass` | `AbstractClass` | 4 | 24 | 0x7F3FE8, 0x7F3FCC, 0x7F3FC4 |
| `SuperWeaponTypeClass` | `AbstractTypeClass` | 4 | 28 | 0x7F4090, 0x7F4074, 0x7F406C |
| `Surface` | — | 1 | 34 | 0x7E2198 |
| `SwizzleManagerClass` | `ISwizzle` | 1 | 10 | 0x7F4108 |
| `TabClass` | `SidebarClass` | 2 | 55 | 0x7EDFB4, 0x7EDFAC |
| `Tactical` | `AbstractClass` | 4 | 25 | 0x7F4348, 0x7F432C, 0x7F4324 |
| `TActionClass` | `AbstractClass` | 4 | 24 | 0x7F443C, 0x7F4420, 0x7F4418 |
| `TagClass` | `AbstractClass` | 4 | 24 | 0x7F44E0, 0x7F44C4, 0x7F44BC |
| `TagTypeClass` | `AbstractTypeClass` | 4 | 27 | 0x7F45C4, 0x7F45A8, 0x7F45A0 |
| `TaskForceClass` | `AbstractTypeClass` | 4 | 27 | 0x7F4680, 0x7F4664, 0x7F465C |
| `TauntCommandClass` | `CommandClass` | 1 | 9 | 0x7EBEDC |
| `TeamClass` | `AbstractClass` | 4 | 24 | 0x7F4730, 0x7F4714, 0x7F470C |
| `TeamTypeClass` | `AbstractTypeClass` | 4 | 27 | 0x7F47D0, 0x7F47B4, 0x7F47AC |
| `TechnoClass` | `RadioClass` | 4 | 309 | 0x7F4960, 0x7F4944, 0x7F493C |
| `TechnoTypeClass` | `ObjectTypeClass` | 4 | 48 | 0x7F4ED8, 0x7F4EBC, 0x7F4EB4 |
| `TeleportLocomotionClass` | `LocomotionClass` | 3 | 50 | 0x7F50CC, 0x7F5000, 0x7F4FDC |
| `TemporalClass` | `AbstractClass` | 4 | 24 | 0x7F5180, 0x7F5164, 0x7F515C |
| `TerrainClass` | `ObjectClass` | 4 | 122 | 0x7F522C, 0x7F5200, 0x7F5224 |
| `TerrainTypeClass` | `ObjectTypeClass` | 4 | 40 | 0x7F5458, 0x7F543C, 0x7F5434 |
| `TEventClass` | `AbstractClass` | 4 | 24 | 0x7F5578, 0x7F555C, 0x7F5554 |
| `TextButtonClass` | `ToggleClass` | 1 | 38 | 0x7F55DC |
| `TextLabelClass` | `GadgetClass` | 1 | 34 | 0x7F5B44 |
| `TiberianSunClassFactory` | `IClassFactory` | 1 | 5 | 0x7EA564 |
| `TiberiumClass` | `AbstractTypeClass` | 4 | 27 | 0x7F5728, 0x7F570C, 0x7F5704 |
| `ToggleClass` | `ControlClass` | 1 | 34 | 0x7E8118 |
| `ToggleRepairCommandClass` | `CommandClass` | 1 | 9 | 0x7EBB6C |
| `ToggleSellCommandClass` | `CommandClass` | 1 | 9 | 0x7EBB94 |
| `ToolTipManager` | — | 1 | 6 | 0x7F57AC |
| `TriColorGaugeClass` | `GaugeClass` | 1 | 44 | 0x7E9430 |
| `TriggerClass` | `AbstractClass` | 4 | 24 | 0x7F5858, 0x7F583C, 0x7F5834 |
| `TriggerTypeClass` | `AbstractTypeClass` | 4 | 27 | 0x7F5904, 0x7F58E8, 0x7F58E0 |
| `TubeClass` | `AbstractClass` | 4 | 24 | 0x7F59B0, 0x7F5994, 0x7F598C |
| `TunnelLocomotionClass` | `LocomotionClass` | 2 | 50 | 0x7F5AF0, 0x7F5A24 |
| `TypeSelectCommandClass` | `CommandClass` | 1 | 9 | 0x7EB964 |
| `UAcceleratorTracker::?$DynamicVectorClass` | `UAcceleratorTracker::?$VectorClass` | 1 | 7 | 0x7EECCC |
| `UAcceleratorTracker::?$VectorClass` | — | 1 | 7 | 0x7EECEC |
| `UAngerStruct::?$DynamicVectorClass` | `UAngerStruct::?$VectorClass` | 1 | 7 | 0x7EA924 |
| `UAngerStruct::?$VectorClass` | — | 1 | 7 | 0x7EA984 |
| `UDirtyAreaStruct::?$DynamicVectorClass` | `UDirtyAreaStruct::?$VectorClass` | 1 | 7 | 0x7F429C |
| `UDirtyAreaStruct::?$VectorClass` | — | 1 | 7 | 0x7F42BC |
| `UDPInterfaceClass` | `WinsockInterfaceClass` | 1 | 31 | 0x7F7A6C |
| `UnholyAlliance` | `MultiplayerGameMode` | 1 | 52 | 0x7EE814 |
| `UnitClass` | `FootClass` | 4 | 344 | 0x7F5C70, 0x7F5C54, 0x7F5C4C |
| `UnitTypeClass` | `TechnoTypeClass` | 4 | 48 | 0x7F6218, 0x7F61FC, 0x7F61F4 |
| `UScoutStruct::?$DynamicVectorClass` | `UScoutStruct::?$VectorClass` | 1 | 7 | 0x7EA904 |
| `UScoutStruct::?$VectorClass` | — | 1 | 7 | 0x7EA9A4 |
| `USubzoneConnectionStruct::?$DynamicVectorClass` | `USubzoneConnectionStruct::?$VectorClass` | 1 | 7 | 0x7ED5A0 |
| `USubzoneConnectionStruct::?$VectorClass` | — | 1 | 7 | 0x7E177C |
| `USubzoneTrackingStruct::?$DynamicVectorClass` | `USubzoneTrackingStruct::?$VectorClass` | 1 | 7 | 0x7ED4A0 |
| `USubzoneTrackingStruct::?$VectorClass` | — | 1 | 7 | 0x7ED500 |
| `UtagCONNECTDATA::?$DynamicVectorClass` | `UtagCONNECTDATA::?$VectorClass` | 1 | 7 | 0x7E5CC4 |
| `UtagCONNECTDATA::?$VectorClass` | — | 1 | 7 | 0x7E5C84 |
| `UUndoInfoStruct::?$DynamicVectorClass` | `UUndoInfoStruct::?$VectorClass` | 1 | 7 | 0x7F327C |
| `UUndoInfoStruct::?$VectorClass` | — | 1 | 7 | 0x7F329C |
| `UZoneConnectionClass::?$DynamicVectorClass` | `UZoneConnectionClass::?$VectorClass` | 1 | 7 | 0x7ED4C0 |
| `UZoneConnectionClass::?$VectorClass` | — | 1 | 7 | 0x7ED4E0 |
| `VAircraftClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3BE8 |
| `VAircraftTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3B28 |
| `VAirstrikeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3900 |
| `VAITriggerTypeClass::?$DiscreteDistributionClass::PAVAITriggerTypeClass::V?$DistributionObject::?$DynamicVectorClass` | `VAITriggerTypeClass::?$DiscreteDistributionClass::PAVAITriggerTypeClass::V?$DistributionObject::?$VectorClass` | 1 | 7 | 0x7F4860 |
| `VAITriggerTypeClass::?$DiscreteDistributionClass::PAVAITriggerTypeClass::V?$DistributionObject::?$VectorClass` | — | 1 | 7 | 0x7F4840 |
| `VAITriggerTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3E40 |
| `VAlphaShapeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3E88 |
| `VAnimClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3C18 |
| `VAnimTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3C30 |
| `VBaseNodeClass::?$DynamicVectorClass` | `VBaseNodeClass::?$VectorClass` | 1 | 7 | 0x7E38B0 |
| `VBaseNodeClass::?$VectorClass` | — | 1 | 7 | 0x7E38F0 |
| `VBombClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3990 |
| `VBuildingClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3BD0 |
| `VBuildingLightClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F38B8 |
| `VBuildingTypeClass::?$DiscreteDistributionClass::PAVBuildingTypeClass::V?$DistributionObject::?$DynamicVectorClass` | `VBuildingTypeClass::?$DiscreteDistributionClass::PAVBuildingTypeClass::V?$DistributionObject::?$VectorClass` | 1 | 7 | 0x7EAAC4 |
| `VBuildingTypeClass::?$DiscreteDistributionClass::PAVBuildingTypeClass::V?$DistributionObject::?$VectorClass` | — | 1 | 7 | 0x7EAAA4 |
| `VBuildingTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3B10 |
| `VBulletClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3D80 |
| `VBulletTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3B58 |
| `VCampaignClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F38A0 |
| `VCaptureManagerClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3948 |
| `VCell::?$DynamicVectorClass` | `VCell::?$VectorClass` | 1 | 7 | 0x7E3890 |
| `VCell::?$VectorClass` | — | 1 | 7 | 0x7E38D0 |
| `VCellClass::?$DiscreteDistributionClass::PAVCellClass::V?$DistributionObject::?$DynamicVectorClass` | `VCellClass::?$DiscreteDistributionClass::PAVCellClass::V?$DistributionObject::?$VectorClass` | 1 | 7 | 0x7E928C |
| `VCellClass::?$DiscreteDistributionClass::PAVCellClass::V?$DistributionObject::?$VectorClass` | — | 1 | 7 | 0x7E9264 |
| `VCellClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3810 |
| `VCStreamClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3768 |
| `VDiskLaserClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3960 |
| `VDriveLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3C78 |
| `VDropPodLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3D08 |
| `VeinholeMonsterClass` | `ObjectClass` | 4 | 122 | 0x7F66A8, 0x7F668C, 0x7F6684 |
| `VEMPulseClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3828 |
| `VersionClass` | — | 1 | 1 | 0x7EA57C |
| `VeterancyNavCommandClass` | `CommandClass` | 1 | 9 | 0x7EB914 |
| `VFactoryClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3D98 |
| `VFlyLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3D20 |
| `VFoggedObjectClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3E70 |
| `VHouseClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3C60 |
| `VHouseTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3C48 |
| `VHoverLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3CA8 |
| `VHSVClass::?$DynamicVectorClass` | `VHSVClass::?$VectorClass` | 1 | 7 | 0x7EF750 |
| `VHSVClass::?$VectorClass` | — | 1 | 7 | 0x7EF7F0 |
| `View1CommandClass` | `CommandClass` | 1 | 9 | 0x7EBD9C |
| `View2CommandClass` | `CommandClass` | 1 | 9 | 0x7EBD74 |
| `View3CommandClass` | `CommandClass` | 1 | 9 | 0x7EBD4C |
| `View4CommandClass` | `CommandClass` | 1 | 9 | 0x7EBD24 |
| `VInfantryClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3C00 |
| `VInfantryTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3B40 |
| `VIsometricTileTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3B70 |
| `VJumpjetLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3C90 |
| `VLightSourceClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3840 |
| `VMechLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3D50 |
| `VNeuronClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3E58 |
| `VOverlayTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3B88 |
| `VoxelAnimClass` | `ObjectClass` | 4 | 122 | 0x7F6318, 0x7F62FC, 0x7F62F4 |
| `VoxelAnimTypeClass` | `ObjectTypeClass` | 4 | 40 | 0x7F6548, 0x7F652C, 0x7F6524 |
| `VParasiteClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3978 |
| `VParticleClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3DE0 |
| `VParticleSystemClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3E10 |
| `VParticleSystemTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3E28 |
| `VParticleTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3DF8 |
| `VPlayerProfile::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F3F44 |
| `VPoint2D::?$DynamicVectorClass` | `VPoint2D::?$VectorClass` | 1 | 7 | 0x7EEB54 |
| `VPoint2D::?$VectorClass` | — | 1 | 7 | 0x7EEB74 |
| `VQMovieHandle` | `MovieHandle` | 1 | 11 | 0x7EE0F4 |
| `VRadSiteClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F39A8 |
| `VRGBClass::?$DynamicVectorClass` | `VRGBClass::?$VectorClass` | 1 | 7 | 0x7F022C |
| `VRGBClass::?$TypeList` | `VRGBClass::?$DynamicVectorClass` | 1 | 7 | 0x7E4E58 |
| `VRGBClass::?$VectorClass` | — | 1 | 7 | 0x7E4E38 |
| `VRocketLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3CC0 |
| `VScriptClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3A50 |
| `VScriptTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3A68 |
| `VShipLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3D68 |
| `VSideClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3858 |
| `VSlaveManagerClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3930 |
| `VSmudgeTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3BA0 |
| `VSpawnManagerClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3918 |
| `VSuperClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F37E0 |
| `VSuperWeaponTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F37C8 |
| `VSwizzlePointerClass::?$DynamicVectorClass` | `VSwizzlePointerClass::?$VectorClass` | 1 | 7 | 0x7F4134 |
| `VSwizzlePointerClass::?$VectorClass` | — | 1 | 7 | 0x7F4154 |
| `VTactical::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F37F8 |
| `VTActionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3A08 |
| `VTagClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3A80 |
| `VTagTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3A98 |
| `VTaskForceClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3AE0 |
| `VTeamClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3AB0 |
| `VTeamTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3AC8 |
| `VTeleportLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3D38 |
| `VTemporalClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F38E8 |
| `VTerrainClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F37B0 |
| `VTerrainTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3798 |
| `VTEventClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F39C0 |
| `VTiberiumClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3870 |
| `VTriggerClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3A20 |
| `VTriggerTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3A38 |
| `VTubeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3888 |
| `VTunnelLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3CD8 |
| `VUnitClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3BB8 |
| `VUnitTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3AF8 |
| `VVoxelAnimClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F39F0 |
| `VVoxelAnimTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F39D8 |
| `VWalkLocomotionClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3CF0 |
| `VWarheadTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3DB0 |
| `VWaveClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3780 |
| `VWaypointClass::?$DynamicVectorClass` | `VWaypointClass::?$VectorClass` | 1 | 7 | 0x7F6ED4 |
| `VWaypointClass::?$VectorClass` | — | 1 | 7 | 0x7F6EF4 |
| `VWaypointPathClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F38D0 |
| `VWDTState::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F70EC |
| `VWDTTerritory::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7EBFA4 |
| `VWDTTerritory::V?$rc_ptr::?$DynamicVectorClass` | `VWDTTerritory::V?$rc_ptr::?$VectorClass` | 1 | 7 | 0x7F7260 |
| `VWDTTerritory::V?$rc_ptr::?$VectorClass` | — | 1 | 7 | 0x7F7230 |
| `VWeaponTypeClass::?$TClassFactory` | `IClassFactory` | 1 | 5 | 0x7F3DC8 |
| `VWstring::?$DynamicVectorClass` | `VWstring::?$VectorClass` | 1 | 7 | 0x7F12B4 |
| `VWstring::?$VectorClass` | — | 1 | 7 | 0x7F1294 |
| `W4DiskID::?$TypeList` | `W4DiskID::?$DynamicVectorClass` | 1 | 7 | 0x7F12D4 |
| `W4DiskID::?$VectorClass` | — | 1 | 7 | 0x7F1274 |
| `W4PassabilityType::?$DynamicVectorClass` | `W4PassabilityType::?$VectorClass` | 1 | 7 | 0x7ED580 |
| `W4PassabilityType::?$VectorClass` | — | 1 | 7 | 0x7ED560 |
| `WalkLocomotionClass` | `LocomotionClass` | 3 | 50 | 0x7F6AC4, 0x7F69F8, 0x7F69D4 |
| `WarheadTypeClass` | `AbstractTypeClass` | 4 | 27 | 0x7F6B30, 0x7F6B14, 0x7F6B0C |
| `WaveClass` | `ObjectClass` | 4 | 122 | 0x7F6BF4, 0x7F6BD8, 0x7F6BD0 |
| `WaypointPathClass` | `AbstractClass` | 4 | 24 | 0x7F6E70, 0x7F6E54, 0x7F6E4C |
| `WDTState` | `ReferenceCounted` | 1 | 3 | 0x7F7250 |
| `WDTTerritory` | `ReferenceCounted` | 1 | 3 | 0x7F7220 |
| `WeaponTypeClass` | `AbstractTypeClass` | 4 | 27 | 0x7F73B8, 0x7F739C, 0x7F7394 |
| `WebBrowser` | `IWOLBrowserEvent` | 1 | 18 | 0x7F743C |
| `WinModemClass` | — | 1 | 10 | 0x7F7488 |
| `WinsockInterfaceClass` | — | 1 | 23 | 0x7F79BC |
| `WinsockInterfaceClass::PAUWinsockBufferType::?$DynamicVectorClass` | `WinsockInterfaceClass::PAUWinsockBufferType::?$VectorClass` | 1 | 7 | 0x7F7A1C |
| `WinsockInterfaceClass::PAUWinsockBufferType::?$VectorClass` | — | 1 | 7 | 0x7F7A3C |
| `WonlineStringDialogControl` | `SimpleWonlineDialogControl` | 1 | 5 | 0x7F7874 |
| `WorldDominationTour::Campaign` | `ReferenceCounted` | 1 | 3 | 0x7F6F3C |
| `WorldDominationTour::CampaignProperties` | `ReferenceCounted` | 1 | 3 | 0x7F7294 |
| `WorldDominationTour::Conflict` | `ReferenceCounted` | 1 | 3 | 0x7F6FC4 |
| `WorldDominationTour::E::?$ValueGameOption` | `WorldDominationTour::GameOption` | 1 | 5 | 0x7F7048 |
| `WorldDominationTour::E::V?$ValueGameOption::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F701C |
| `WorldDominationTour::FactionSelectDialogControl` | `OwnerDraw::SimpleDialogControl` | 1 | 5 | 0x7F791C |
| `WorldDominationTour::FlagGameOption` | `WorldDominationTour::GameOption` | 1 | 5 | 0x7F709C |
| `WorldDominationTour::GameOption` | `ReferenceCounted` | 1 | 5 | 0x7F7060 |
| `WorldDominationTour::History` | `ReferenceCounted` | 1 | 3 | 0x7F70DC |
| `WorldDominationTour::Map` | `ReferenceCounted` | 1 | 3 | 0x7F7134 |
| `WorldDominationTour::Map::PAUAnimationPalette::?$DynamicVectorClass` | `WorldDominationTour::Map::PAUAnimationPalette::?$VectorClass` | 1 | 7 | 0x7F7144 |
| `WorldDominationTour::Map::PAUAnimationPalette::?$VectorClass` | — | 1 | 7 | 0x7F71A4 |
| `WorldDominationTour::MapSizeGameOption` | `WorldDominationTour::GameOption` | 1 | 5 | 0x7F70B4 |
| `WorldDominationTour::Selection` | `MSEngine` | 1 | 3 | 0x7F72B4 |
| `WorldDominationTour::State` | `ReferenceCounted` | 1 | 3 | 0x7F7314 |
| `WorldDominationTour::Territory` | `ReferenceCounted` | 1 | 3 | 0x7F7334 |
| `WorldDominationTour::VCampaign::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F6F24 |
| `WorldDominationTour::VCampaignProperties::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F6F6C |
| `WorldDominationTour::VCentroid::?$VectorClass` | — | 1 | 7 | 0x7F71E0 |
| `WorldDominationTour::VConflict::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F6F34 |
| `WorldDominationTour::VConflict::V?$rc_ptr::?$DynamicVectorClass` | `WorldDominationTour::VConflict::V?$rc_ptr::?$VectorClass` | 1 | 7 | 0x7F6F4C |
| `WorldDominationTour::VConflict::V?$rc_ptr::?$VectorClass` | — | 1 | 7 | 0x7F6F7C |
| `WorldDominationTour::VConflict::V?$rc_ptr::V?$DynamicVectorClass::WorldDominationTour::VConflict::V?$rc_ptr::?$VectorCursor` | — | 1 | 4 | 0x7F6F9C |
| `WorldDominationTour::VFlagGameOption::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F7014 |
| `WorldDominationTour::VGameOption::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F7024 |
| `WorldDominationTour::VGameOption::V?$rc_ptr::?$DynamicVectorClass` | `WorldDominationTour::VGameOption::V?$rc_ptr::?$VectorClass` | 1 | 7 | 0x7F6FD4 |
| `WorldDominationTour::VGameOption::V?$rc_ptr::?$VectorClass` | — | 1 | 7 | 0x7F6FF4 |
| `WorldDominationTour::VGameOption::V?$rc_ptr::V?$DynamicVectorClass::WorldDominationTour::VGameOption::V?$rc_ptr::?$VectorCursor` | — | 1 | 4 | 0x7F702C |
| `WorldDominationTour::VHistory::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F6F74 |
| `WorldDominationTour::VMap::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F712C |
| `WorldDominationTour::VMapSizeGameOption::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F7040 |
| `WorldDominationTour::Voices::Anim` | `MSAnim` | 1 | 9 | 0x7F7354 |
| `WorldDominationTour::VState::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F6F2C |
| `WorldDominationTour::VTerritory::?$rc_ptr` | `rc_ptr_base` | 1 | 1 | 0x7F71C4 |
| `WorldDominationTour::VTerritory::V?$rc_ptr::?$DynamicVectorClass` | `WorldDominationTour::VTerritory::V?$rc_ptr::?$VectorClass` | 1 | 7 | 0x7F7164 |
| `WorldDominationTour::VTerritory::V?$rc_ptr::?$VectorClass` | — | 1 | 7 | 0x7F7184 |
| `WorldDominationTour::VTerritory::V?$rc_ptr::V?$DynamicVectorClass::WorldDominationTour::VTerritory::V?$rc_ptr::?$VectorCursor` | — | 1 | 4 | 0x7F72D8 |
| `WWMouseClass` | `Mouse` | 1 | 18 | 0x7F7B2C |
| `XSurface` | `Surface` | 1 | 36 | 0x7E2104 |
