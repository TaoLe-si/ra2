# gamemd.exe 对象字段偏移

由 `tools/fieldscan.py` 静态分析得出，未运行目标进程。
全量数据见 `db/fields.json`。

## 方法

构造函数会把虚表指针写进对象首字段，这是它的强特征。扫每个函数体、
跟踪 this 指针（thiscall 入口 `ecx`，经 `mov r,ecx` / `lea r,[this+d]` /
`mov [ebp-d],ecx` 三种流转）把 `mov [this+off],` 与 `mov r,[this+off]`
记成字段访问。

**归属判据是『有效偏移恰为 0 的虚表写入』**，不是指令里的 `disp == 0`。
这条区别不是纸面上的：实测 `MouseClass` 的构造函数在 `[esi+0x5518]`
内嵌构造了一个 INoticeSink 子对象，写成 `lea edi,[esi+0x5518]` +
`mov [edi], ??_7INoticeSink@@6B@` —— 指令里的 disp 正是 0。
不做偏移换算，就会把 MouseClass 的构造函数整个记到 INoticeSink 头上。

## 交叉验证（三条独立证据）

| 证据 | 判据 | 结果 |
|---|---|---|
| sizeof（`push N; call new` 配对，来自 `db/sizes.json`） | 字段最大末端必须 <= sizeof | 207 个通过 / 435 个无基准 / **4 个越界** |
| RTTI 的嵌入基类位移 `mdisp`（来自 PE 的 RTTI 段） | 应在 .text 里找到 `mov [对象+mdisp], <该基类主虚表>`，或在 `lea ecx,[对象+mdisp]` 后调用该基类构造函数 | 可判定的 159 处**全部印证**（另有 228 处基类是无虚表的抽象接口，见下节） |
| 继承关系 | 沿继承链字段末端必须**严格递增**（派生类只会字段更多） | 固化进 `ra2core` 冒烟测试，见 `Layout_Check()` |

### sizeof 越界（每条都带判定）

越界只有两种可能：字段偏移算错了，或者那条 sizeof 本身取错了。
判据是 `db/sizes.json` 里该类的候选值 —— 如果候选里存在
**≥ 字段末端**的值，说明 sizeof 扫描当初选错了候选。

| 类 | sizeof | 最大末端 | 越界偏移 | 判定 |
|---|---:|---:|---|---|
| `CounterClass` | 8 | 20 | 0x8, 0xC, 0xD, 0x10 | 疑似扫描错，需人工看 |
| `CCINIClass` | 88 | 97 | 0x60 | 疑似扫描错，需人工看 |
| `BufferIOFileClass` | 72 | 84 | 0x48, 0x4C, 0x50 | sizeof 是数组步长证据（弱），很可能不是真 sizeof |
| `PAVReestablish::?$VectorClass` | 8 | 14 | 0x8, 0xC, 0xD | 疑似扫描错，需人工看 |

### 被指令流印证的 RTTI 嵌入基类位移

RTTI 说『某类的某基类子对象嵌在偏移 N』，就去 .text 里找
`mov [对象+N], 那个基类的主虚表`；找不到再退一步，看有没有
`lea ecx,[对象+N]; call 那个基类的构造函数`（基类构造函数没内联时，
偏移藏在 `ecx` 里，指令的 disp 是 0）。两边来源完全不同：一边是 PE 的
RTTI 段，一边是 .text 的指令流。

| 印证方式 | 处数 | 判据 |
|---|---:|---|
| 虚表直写 | 159 | `mov [对象+N], <该基类主虚表>` 在 .text 里找到 |
| 调用点 | 0 | `lea ecx,[对象+N]` 后调用该基类构造函数 |
| 同链写入 | 228 | 该继承链上某个类的构造函数在有效偏移 N 上写过东西 |
| 未印证 | 0 | 三条都没有 |

**可判定子集 159 处，命中 159 处 = 100%**。『可判定』= 那个基类有自己的 COL，因而存在一张可以对名字的虚表。

剩下 228 处涉及 14 个基类名（`ATL::ATL::VCComMultiThreadModel::?$CComObjectRootEx`、`ATL::CComObjectRootBase`、`BounceClass`、`FlasherClass`、`IConnectionPointContainer`、`IFlyControl`、`IHouse`、`ILinkStream`、`ILocomotion`、`IPiggyback`、`IPublicHouse`、`IRTTITypeInfo`、`IUnknown`、`StageClass`），全是**没有虚表**的基类，分两类：

1. **纯抽象接口**（`IUnknown`、`IRTTITypeInfo`、`ILocomotion`、`IPiggyback`、`IFlyControl`、`ILinkStream`、`IHouse`、`IConnectionPointContainer`）—— MSVC 对『没有非内联虚函数、又从不被完整构造』的类既不生成虚表也不生成 COL；
2. **没有虚函数的普通子对象**（`FlasherClass`、`StageClass`、`BounceClass`）—— 二进制里就是没有虚表。实测 `TechnoClass::ctor@0x6F2B40` 在 240 上写的是 `mov dword ptr [esi+0xf0], ebx`（`ebx` 是 0），`AnimClass::ctor@0x421EA0` 在 172 上写的是 `mov dword ptr [esi+0xac], ebx` —— 都不是虚表。

这两类在『虚表直写 / 调用点』两条通道上**结构上无法判定**：二进制里
根本不存在一个能拿来对名字的对象。所以它们退到『同链写入』—— 只声称
『这个位移上确实有构造函数写过东西』，**名称仍然只有 RTTI 一个来源**。
把它们算进分母，会把『可判定的 159 处全中』稀释成『387 处只中 41%』，那是自欺。

| 派生类 | 位移 | 基类 | 虚表直写 | 调用点 | 同链写入 | 判定 |
|---|---:|---|---:|---:|---:|---|
| `MouseClass` | 0x5518 | `INoticeSink` | 2 | 0 | 3 | 虚表直写 |
| `AbstractClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 1 | 同链写入 |
| `AbstractClass` | 0x4 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `AbstractClass` | 0x8 | `INoticeSink` | 3 | 0 | 1 | 虚表直写 |
| `AbstractClass` | 0xC | `INoticeSource` | 3 | 0 | 1 | 虚表直写 |
| `AbstractTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `AbstractTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `AbstractTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `AbstractTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `AircraftClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 7 | 同链写入 |
| `AircraftClass` | 0x4 | `IUnknown` | 0 | 0 | 7 | 同链写入 |
| `AircraftClass` | 0x8 | `INoticeSink` | 3 | 0 | 7 | 虚表直写 |
| `AircraftClass` | 0xC | `INoticeSource` | 3 | 0 | 7 | 虚表直写 |
| `AircraftClass` | 0xF0 | `FlasherClass` | 0 | 0 | 1 | 同链写入 |
| `AircraftClass` | 0xF8 | `StageClass` | 0 | 0 | 1 | 同链写入 |
| `AircraftClass` | 0x6C0 | `IFlyControl` | 0 | 0 | 1 | 同链写入 |
| `AircraftClass` | 0x6C0 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `AircraftTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 5 | 同链写入 |
| `AircraftTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 5 | 同链写入 |
| `AircraftTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 5 | 虚表直写 |
| `AircraftTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 5 | 虚表直写 |
| `AirstrikeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `AirstrikeClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `AirstrikeClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `AirstrikeClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `AITriggerTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `AITriggerTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `AITriggerTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `AITriggerTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `AlphaShapeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `AlphaShapeClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `AlphaShapeClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `AlphaShapeClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `AnimClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `AnimClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `AnimClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `AnimClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `AnimClass` | 0xAC | `StageClass` | 0 | 0 | 1 | 同链写入 |
| `AnimTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `AnimTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `AnimTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `AnimTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `BuildingLightClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `BuildingLightClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `BuildingLightClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `BuildingLightClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `BombClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `BombClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `BombClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `BombClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `NeuronClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `NeuronClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `NeuronClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `NeuronClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `BuildingClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 6 | 同链写入 |
| `BuildingClass` | 0x4 | `IUnknown` | 0 | 0 | 6 | 同链写入 |
| `BuildingClass` | 0x8 | `INoticeSink` | 3 | 0 | 6 | 虚表直写 |
| `BuildingClass` | 0xC | `INoticeSource` | 3 | 0 | 6 | 虚表直写 |
| `BuildingClass` | 0xF0 | `FlasherClass` | 0 | 0 | 1 | 同链写入 |
| `BuildingClass` | 0xF8 | `StageClass` | 0 | 0 | 1 | 同链写入 |
| `BuildingTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 5 | 同链写入 |
| `BuildingTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 5 | 同链写入 |
| `BuildingTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 5 | 虚表直写 |
| `BuildingTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 5 | 虚表直写 |
| `BulletClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `BulletClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `BulletClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `BulletClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `BulletTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `BulletTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `BulletTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `BulletTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `CampaignClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `CampaignClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `CampaignClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `CampaignClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `CaptureManagerClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `CaptureManagerClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `CaptureManagerClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `CaptureManagerClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `CellClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `CellClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `CellClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `CellClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `CStreamClass` | 0x4 | `ILinkStream` | 0 | 0 | 1 | 同链写入 |
| `CStreamClass` | 0x4 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `DiskLaserClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `DiskLaserClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `DiskLaserClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `DiskLaserClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `DriveLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `DriveLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `DriveLocomotionClass` | 0x18 | `IPiggyback` | 0 | 0 | 1 | 同链写入 |
| `DriveLocomotionClass` | 0x18 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `DropPodLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `DropPodLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `DropPodLocomotionClass` | 0x18 | `IPiggyback` | 0 | 0 | 1 | 同链写入 |
| `DropPodLocomotionClass` | 0x18 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `EMPulseClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `EMPulseClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `EMPulseClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `EMPulseClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `FactoryClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `FactoryClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `FactoryClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `FactoryClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `FactoryClass` | 0x24 | `StageClass` | 0 | 0 | 1 | 同链写入 |
| `FlyLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `FlyLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `FoggedObjectClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `FoggedObjectClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `FoggedObjectClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `FoggedObjectClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `FootClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 6 | 同链写入 |
| `FootClass` | 0x4 | `IUnknown` | 0 | 0 | 6 | 同链写入 |
| `FootClass` | 0x8 | `INoticeSink` | 3 | 0 | 6 | 虚表直写 |
| `FootClass` | 0xC | `INoticeSource` | 3 | 0 | 6 | 虚表直写 |
| `FootClass` | 0xF0 | `FlasherClass` | 0 | 0 | 1 | 同链写入 |
| `FootClass` | 0xF8 | `StageClass` | 0 | 0 | 1 | 同链写入 |
| `HouseClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `HouseClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `HouseClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `HouseClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `HouseClass` | 0x24 | `IHouse` | 0 | 0 | 1 | 同链写入 |
| `HouseClass` | 0x24 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `HouseClass` | 0x28 | `IPublicHouse` | 0 | 0 | 1 | 同链写入 |
| `HouseClass` | 0x28 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `HouseClass` | 0x2C | `IConnectionPointContainer` | 0 | 0 | 1 | 同链写入 |
| `HouseClass` | 0x2C | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `HouseTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `HouseTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `HouseTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `HouseTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `HoverLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `HoverLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `LocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 1 | 同链写入 |
| `LocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `InfantryClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 7 | 同链写入 |
| `InfantryClass` | 0x4 | `IUnknown` | 0 | 0 | 7 | 同链写入 |
| `InfantryClass` | 0x8 | `INoticeSink` | 3 | 0 | 7 | 虚表直写 |
| `InfantryClass` | 0xC | `INoticeSource` | 3 | 0 | 7 | 虚表直写 |
| `InfantryClass` | 0xF0 | `FlasherClass` | 0 | 0 | 1 | 同链写入 |
| `InfantryClass` | 0xF8 | `StageClass` | 0 | 0 | 1 | 同链写入 |
| `InfantryTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 5 | 同链写入 |
| `InfantryTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 5 | 同链写入 |
| `InfantryTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 5 | 虚表直写 |
| `InfantryTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 5 | 虚表直写 |
| `IsometricTileClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `IsometricTileClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `IsometricTileClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `IsometricTileClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `IsometricTileTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `IsometricTileTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `IsometricTileTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `IsometricTileTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `JumpjetLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `JumpjetLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `JumpjetLocomotionClass` | 0x18 | `IPiggyback` | 0 | 0 | 1 | 同链写入 |
| `JumpjetLocomotionClass` | 0x18 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `LightSourceClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `LightSourceClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `LightSourceClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `LightSourceClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `MechLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `MechLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `MissionClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `MissionClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `MissionClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `MissionClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `TabClass` | 0x5518 | `INoticeSink` | 2 | 0 | 1 | 虚表直写 |
| `ObjectClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `ObjectClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `ObjectClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `ObjectClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `ObjectTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `ObjectTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `ObjectTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `ObjectTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `OverlayClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `OverlayClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `OverlayClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `OverlayClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `OverlayTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `OverlayTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `OverlayTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `OverlayTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `ParasiteClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `ParasiteClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `ParasiteClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `ParasiteClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `ParticleClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `ParticleClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `ParticleClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `ParticleClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `ParticleSystemClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `ParticleSystemClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `ParticleSystemClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `ParticleSystemClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `ParticleSystemTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `ParticleSystemTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `ParticleSystemTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `ParticleSystemTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `ParticleTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `ParticleTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `ParticleTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `ParticleTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `RadioClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `RadioClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `RadioClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `RadioClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `RadSiteClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `RadSiteClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `RadSiteClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `RadSiteClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `RocketLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `RocketLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `ScriptClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `ScriptClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `ScriptClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `ScriptClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `ScriptTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `ScriptTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `ScriptTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `ScriptTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `ScrollClass` | 0x5518 | `INoticeSink` | 2 | 0 | 2 | 虚表直写 |
| `ShipLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `ShipLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `ShipLocomotionClass` | 0x18 | `IPiggyback` | 0 | 0 | 1 | 同链写入 |
| `ShipLocomotionClass` | 0x18 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `SideClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `SideClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `SideClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `SideClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `SlaveManagerClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `SlaveManagerClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `SlaveManagerClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `SlaveManagerClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `SmudgeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `SmudgeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `SmudgeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `SmudgeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `SmudgeTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `SmudgeTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `SmudgeTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `SmudgeTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `SpawnManagerClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `SpawnManagerClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `SpawnManagerClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `SpawnManagerClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `SuperClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `SuperClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `SuperClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `SuperClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `SuperWeaponTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `SuperWeaponTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `SuperWeaponTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `SuperWeaponTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `Tactical` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `Tactical` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `Tactical` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `Tactical` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `TActionClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `TActionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `TActionClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `TActionClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `TagClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `TagClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `TagClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `TagClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `TagTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `TagTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `TagTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `TagTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `TaskForceClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `TaskForceClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `TaskForceClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `TaskForceClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `TeamClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `TeamClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `TeamClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `TeamClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `TeamTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `TeamTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `TeamTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `TeamTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `TechnoClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 5 | 同链写入 |
| `TechnoClass` | 0x4 | `IUnknown` | 0 | 0 | 5 | 同链写入 |
| `TechnoClass` | 0x8 | `INoticeSink` | 3 | 0 | 5 | 虚表直写 |
| `TechnoClass` | 0xC | `INoticeSource` | 3 | 0 | 5 | 虚表直写 |
| `TechnoClass` | 0xF0 | `FlasherClass` | 0 | 0 | 1 | 同链写入 |
| `TechnoClass` | 0xF8 | `StageClass` | 0 | 0 | 1 | 同链写入 |
| `TechnoTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `TechnoTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `TechnoTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `TechnoTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `TeleportLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `TeleportLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `TeleportLocomotionClass` | 0x18 | `IPiggyback` | 0 | 0 | 1 | 同链写入 |
| `TeleportLocomotionClass` | 0x18 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `TemporalClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `TemporalClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `TemporalClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `TemporalClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `TerrainClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `TerrainClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `TerrainClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `TerrainClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `TerrainClass` | 0xAC | `StageClass` | 0 | 0 | 1 | 同链写入 |
| `TerrainTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `TerrainTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `TerrainTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `TerrainTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `TEventClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `TEventClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `TEventClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `TEventClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `TiberiumClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `TiberiumClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `TiberiumClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `TiberiumClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `TriggerClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `TriggerClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `TriggerClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `TriggerClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `TriggerTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `TriggerTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `TriggerTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `TriggerTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `TubeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `TubeClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `TubeClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `TubeClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `TunnelLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `TunnelLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `UnitClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 7 | 同链写入 |
| `UnitClass` | 0x4 | `IUnknown` | 0 | 0 | 7 | 同链写入 |
| `UnitClass` | 0x8 | `INoticeSink` | 3 | 0 | 7 | 虚表直写 |
| `UnitClass` | 0xC | `INoticeSource` | 3 | 0 | 7 | 虚表直写 |
| `UnitClass` | 0xF0 | `FlasherClass` | 0 | 0 | 1 | 同链写入 |
| `UnitClass` | 0xF8 | `StageClass` | 0 | 0 | 1 | 同链写入 |
| `UnitTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 5 | 同链写入 |
| `UnitTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 5 | 同链写入 |
| `UnitTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 5 | 虚表直写 |
| `UnitTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 5 | 虚表直写 |
| `VoxelAnimClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `VoxelAnimClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `VoxelAnimClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `VoxelAnimClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `VoxelAnimClass` | 0xB0 | `BounceClass` | 0 | 0 | 1 | 同链写入 |
| `VoxelAnimTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 4 | 同链写入 |
| `VoxelAnimTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 4 | 同链写入 |
| `VoxelAnimTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 4 | 虚表直写 |
| `VoxelAnimTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 4 | 虚表直写 |
| `VeinholeMonsterClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `VeinholeMonsterClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `VeinholeMonsterClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `VeinholeMonsterClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `WalkLocomotionClass` | 0x4 | `ILocomotion` | 0 | 0 | 2 | 同链写入 |
| `WalkLocomotionClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `WalkLocomotionClass` | 0x18 | `IPiggyback` | 0 | 0 | 1 | 同链写入 |
| `WalkLocomotionClass` | 0x18 | `IUnknown` | 0 | 0 | 1 | 同链写入 |
| `WarheadTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `WarheadTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `WarheadTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `WarheadTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `WaveClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `WaveClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `WaveClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `WaveClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `WaypointPathClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 2 | 同链写入 |
| `WaypointPathClass` | 0x4 | `IUnknown` | 0 | 0 | 2 | 同链写入 |
| `WaypointPathClass` | 0x8 | `INoticeSink` | 3 | 0 | 2 | 虚表直写 |
| `WaypointPathClass` | 0xC | `INoticeSource` | 3 | 0 | 2 | 虚表直写 |
| `WeaponTypeClass` | 0x4 | `IRTTITypeInfo` | 0 | 0 | 3 | 同链写入 |
| `WeaponTypeClass` | 0x4 | `IUnknown` | 0 | 0 | 3 | 同链写入 |
| `WeaponTypeClass` | 0x8 | `INoticeSink` | 3 | 0 | 3 | 虚表直写 |
| `WeaponTypeClass` | 0xC | `INoticeSource` | 3 | 0 | 3 | 虚表直写 |
| `ATL::VCNetUtilEventSink::?$CComObject` | 0x4 | `ATL::ATL::VCComMultiThreadModel::?$CComObjectRootEx` | 0 | 0 | 1 | 同链写入 |
| `ATL::VCNetUtilEventSink::?$CComObject` | 0x4 | `ATL::CComObjectRootBase` | 0 | 0 | 1 | 同链写入 |
| `ATL::VCChatEventSink::?$CComObject` | 0x4 | `ATL::ATL::VCComMultiThreadModel::?$CComObjectRootEx` | 0 | 0 | 1 | 同链写入 |
| `ATL::VCChatEventSink::?$CComObject` | 0x4 | `ATL::CComObjectRootBase` | 0 | 0 | 1 | 同链写入 |
| `CNetUtilEventSink` | 0x4 | `ATL::ATL::VCComMultiThreadModel::?$CComObjectRootEx` | 0 | 0 | 1 | 同链写入 |
| `CNetUtilEventSink` | 0x4 | `ATL::CComObjectRootBase` | 0 | 0 | 1 | 同链写入 |
| `CChatEventSink` | 0x4 | `ATL::ATL::VCComMultiThreadModel::?$CComObjectRootEx` | 0 | 0 | 1 | 同链写入 |
| `CChatEventSink` | 0x4 | `ATL::CComObjectRootBase` | 0 | 0 | 1 | 同链写入 |
| `ATL::VCDownloadEventSink::?$CComObject` | 0x4 | `ATL::ATL::VCComMultiThreadModel::?$CComObjectRootEx` | 0 | 0 | 1 | 同链写入 |
| `ATL::VCDownloadEventSink::?$CComObject` | 0x4 | `ATL::CComObjectRootBase` | 0 | 0 | 1 | 同链写入 |

## 字段最多的 30 个类

| 类 | 字段数 | 最大末端 | sizeof | 计数函数 |
|---|---:|---:|---:|---:|
| `TechnoTypeClass` | 455 | 0xDF4 | — | 3 |
| `HouseClass` | 344 | 0x160B8 | — | 3 |
| `BuildingTypeClass` | 251 | 0x1792 | — | 3 |
| `TechnoClass` | 224 | 0x520 | — | 3 |
| `BuildingClass` | 117 | 0x71C | — | 3 |
| `ConvertClass` | 98 | 0x188 | — | 3 |
| `FootClass` | 98 | 0x6B9 | — | 3 |
| `AnimTypeClass` | 88 | 0x375 | — | 4 |
| `WeaponTypeClass` | 88 | 0x15D | — | 4 |
| `WarheadTypeClass` | 87 | 0x1CC | — | 4 |
| `InfantryTypeClass` | 81 | 0xECC | — | 4 |
| `ObjectTypeClass` | 69 | 0x294 | — | 3 |
| `ListClass` | 67 | 0x174 | — | 4 |
| `HouseTypeClass` | 66 | 0x1A9 | — | 4 |
| `RadarClass` | 65 | 0x150C | — | 3 |
| `CellClass` | 65 | 0x144 | — | 3 |
| `AnimClass` | 59 | 0x344 | — | 3 |
| `Tactical` | 55 | 0xE14 | — | 4 |
| `BulletTypeClass` | 54 | 0x2F8 | — | 4 |
| `UnitTypeClass` | 52 | 0xE5F | — | 4 |
| `ParticleTypeClass` | 48 | 0x318 | — | 4 |
| `TeamTypeClass` | 46 | 0xF8 | — | 3 |
| `ParticleClass` | 45 | 0x132 | — | 4 |
| `IsometricTileTypeClass` | 43 | 0x30C | — | 3 |
| `VoxelAnimTypeClass` | 43 | 0x301 | — | 3 |
| `Dial8Class` | 42 | 0xC0 | — | 1 |
| `TeamClass` | 39 | 0x85 | — | 2 |
| `MapClass` | 39 | 0x1174 | — | 3 |
| `ParticleSystemTypeClass` | 39 | 0x30D | — | 4 |
| `DisplayClass` | 39 | 0x11E4 | — | 2 |

## 核心继承链的字段表

字段名是**未知的** —— 这里只给『这个偏移上确实有这么一个宽度的
字段』。语义要等把访问该偏移的代码读通（例如 INI 读取路径）才能填。

### `AbstractClass`

sizeof = 未知，写虚表的函数 4 个，字段 9 个，最大末端 0x21。

内嵌子对象：0x8 `INoticeSink`，0xC `INoticeSource`。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 4 | 0 | 0x7E1F50 |
| 0x4 | 4 | 4 | 0 | 0x7E1F34 |
| 0x8 | 4 | 6 | 0 | 0x7E1FBC, 0x7E1F2C |
| 0xC | 4 | 6 | 0 | 0x7E1FB4, 0x7E1F24 |
| 0x10 | 4 | 1 | 0 | 0xFFFFFFFF |
| 0x14 | 1 | 1 | 1 |  |
| 0x18 | 4 | 1 | 0 |  |
| 0x1C | 4 | 1 | 0 |  |
| 0x20 | 1 | 1 | 0 |  |

### `ObjectClass`

sizeof = 172，写虚表的函数 3 个，字段 36 个，最大末端 0xAC。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 3 | 0 | 0x7EF060 |
| 0x4 | 4 | 3 | 0 | 0x7EF044 |
| 0x8 | 4 | 3 | 0 | 0x7EF03C |
| 0xC | 4 | 3 | 0 | 0x7EF034 |
| 0x14 | 1 | 1 | 1 |  |
| 0x24 | 4 | 1 | 0 |  |
| 0x28 | 4 | 1 | 0 |  |
| 0x2C | 4 | 1 | 0 |  |
| 0x30 | 4 | 2 | 0 | 0x0 |
| 0x34 | 4 | 1 | 0 |  |
| 0x38 | 4 | 1 | 1 |  |
| 0x64 | 4 | 1 | 0 |  |
| 0x68 | 1 | 1 | 0 |  |
| 0x6C | 4 | 1 | 0 |  |
| 0x70 | 4 | 1 | 0 |  |
| 0x74 | 1 | 1 | 0 |  |
| 0x78 | 4 | 1 | 0 |  |
| 0x7C | 4 | 1 | 0 |  |
| 0x80 | 1 | 1 | 0 |  |
| 0x81 | 1 | 1 | 0 |  |
| 0x82 | 1 | 1 | 0 |  |
| 0x83 | 1 | 1 | 0 |  |
| 0x84 | 1 | 1 | 0 |  |
| 0x88 | 4 | 1 | 0 |  |
| 0x8C | 1 | 1 | 0 |  |
| 0x8D | 1 | 1 | 0 |  |
| 0x8E | 1 | 1 | 0 |  |
| 0x8F | 1 | 1 | 0 |  |
| 0x90 | 1 | 2 | 0 | 0x0 |
| 0x94 | 4 | 1 | 0 |  |
| 0x98 | 1 | 1 | 1 |  |
| 0x99 | 1 | 1 | 0 |  |
| 0x9C | 4 | 1 | 0 |  |
| 0xA0 | 4 | 1 | 0 |  |
| 0xA4 | 4 | 1 | 0 |  |
| 0xA8 | 4 | 2 | 1 | 0x0 |

### `MissionClass`

sizeof = 未知，写虚表的函数 2 个，字段 13 个，最大末端 0xD4。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 2 | 0 | 0x7EDCC0 |
| 0x4 | 4 | 2 | 0 | 0x7EDCA4 |
| 0x8 | 4 | 2 | 0 | 0x7EDC9C |
| 0xC | 4 | 2 | 0 | 0x7EDC94 |
| 0xAC | 4 | 1 | 0 |  |
| 0xB0 | 4 | 1 | 0 |  |
| 0xB4 | 4 | 1 | 0 |  |
| 0xB8 | 1 | 1 | 0 |  |
| 0xBC | 4 | 1 | 0 |  |
| 0xC0 | 4 | 1 | 0 |  |
| 0xC4 | 4 | 1 | 0 |  |
| 0xC8 | 4 | 1 | 0 |  |
| 0xD0 | 4 | 1 | 0 |  |

### `RadioClass`

sizeof = 未知，写虚表的函数 3 个，字段 12 个，最大末端 0xEE。

内嵌子对象：0xE0 `PAVTechnoClass::?$VectorClass`。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 4 | 0 | 0x7F0508, 0x7EDCC0 |
| 0x4 | 4 | 4 | 0 | 0x7F04EC, 0x7EDCA4 |
| 0x8 | 4 | 4 | 0 | 0x7F04E4, 0x7EDC9C |
| 0xC | 4 | 4 | 0 | 0x7F04DC, 0x7EDC94 |
| 0xD4 | 4 | 1 | 0 |  |
| 0xD8 | 4 | 1 | 0 |  |
| 0xDC | 4 | 1 | 0 |  |
| 0xE0 | 4 | 3 | 0 | 0x7E180C |
| 0xE4 | 4 | 3 | 2 | 0x0 |
| 0xE8 | 4 | 2 | 0 | 0x1, 0x0 |
| 0xEC | 1 | 1 | 0 | 0x1 |
| 0xED | 1 | 3 | 1 | 0x0, 0x1 |

### `TechnoClass`

sizeof = 未知，写虚表的函数 3 个，字段 224 个，最大末端 0x520。

内嵌子对象：0xE0 `PAVTechnoClass::?$VectorClass`，0x440 `H::?$DynamicVectorClass`，0x440 `H::?$VectorClass`，0x458 `PAVAbstractClass::?$DynamicVectorClass`，0x458 `PAVAbstractClass::?$VectorClass`，0x470 `PAVAbstractClass::?$DynamicVectorClass`，0x470 `PAVAbstractClass::?$VectorClass`。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 5 | 0 | 0x7F4960, 0x7F0508, 0x7EDCC0 |
| 0x4 | 4 | 5 | 0 | 0x7F4944, 0x7F04EC, 0x7EDCA4 |
| 0x8 | 4 | 5 | 0 | 0x7F493C, 0x7F04E4, 0x7EDC9C |
| 0xC | 4 | 5 | 0 | 0x7F4934, 0x7F04DC, 0x7EDC94 |
| 0x14 | 1 | 1 | 1 |  |
| 0xE0 | 4 | 1 | 0 | 0x7E180C |
| 0xE4 | 4 | 1 | 1 |  |
| 0xE8 | 4 | 1 | 0 |  |
| 0xED | 1 | 1 | 0 |  |
| 0xF0 | 4 | 1 | 0 |  |
| 0xF4 | 1 | 1 | 0 |  |
| 0xF8 | 4 | 1 | 0 |  |
| 0xFC | 1 | 1 | 0 |  |
| 0x100 | 4 | 1 | 0 |  |
| 0x108 | 4 | 1 | 0 |  |
| 0x10C | 4 | 1 | 0 |  |
| 0x110 | 4 | 1 | 0 |  |
| 0x114 | 4 | 1 | 0 |  |
| 0x118 | 4 | 1 | 0 |  |
| 0x11C | 4 | 1 | 0 |  |
| 0x120 | 4 | 1 | 0 | 0xFFFFFF9C |
| 0x124 | 4 | 1 | 0 |  |
| 0x128 | 4 | 1 | 0 |  |
| 0x12C | 4 | 2 | 1 |  |
| 0x130 | 4 | 2 | 1 |  |
| 0x134 | 1 | 1 | 0 |  |
| 0x138 | 4 | 1 | 0 |  |
| 0x13C | 4 | 1 | 0 |  |
| 0x140 | 4 | 1 | 0 |  |
| 0x144 | 4 | 1 | 0 |  |
| 0x148 | 4 | 1 | 0 |  |
| 0x14C | 4 | 1 | 0 |  |
| 0x158 | 4 | 1 | 0 |  |
| 0x15C | 4 | 1 | 0 |  |
| 0x160 | 4 | 1 | 0 |  |
| 0x164 | 4 | 1 | 0 |  |
| 0x168 | 4 | 1 | 0 |  |
| 0x170 | 4 | 1 | 0 |  |
| 0x174 | 4 | 1 | 0 |  |
| 0x17C | 4 | 1 | 0 |  |
| 0x180 | 4 | 1 | 0 |  |
| 0x188 | 4 | 1 | 0 | 0x2D |
| 0x18C | 4 | 1 | 0 |  |
| 0x194 | 4 | 1 | 0 |  |
| 0x198 | 4 | 1 | 0 |  |
| 0x1A0 | 4 | 1 | 0 |  |
| 0x1A4 | 4 | 1 | 0 | 0xA |
| 0x1A8 | 4 | 2 | 0 |  |
| 0x1B0 | 4 | 2 | 0 |  |
| 0x1B4 | 4 | 2 | 0 |  |
| 0x1BC | 4 | 2 | 0 |  |
| 0x1C4 | 4 | 1 | 0 |  |
| 0x1C8 | 1 | 1 | 0 |  |
| 0x1CC | 4 | 1 | 0 |  |
| 0x1D0 | 4 | 1 | 0 |  |
| 0x1D4 | 4 | 1 | 0 |  |
| 0x1D8 | 1 | 1 | 0 |  |
| 0x1DC | 4 | 1 | 0 |  |
| 0x1E0 | 4 | 1 | 0 |  |
| 0x1E8 | 4 | 1 | 0 |  |
| 0x1EC | 4 | 1 | 0 |  |
| 0x1F4 | 4 | 1 | 0 |  |
| 0x1F8 | 1 | 1 | 0 |  |
| 0x1FC | 4 | 1 | 0 |  |
| 0x204 | 4 | 1 | 0 |  |
| 0x208 | 4 | 1 | 0 |  |
| 0x20C | 4 | 1 | 0 |  |
| 0x210 | 4 | 1 | 0 |  |
| 0x214 | 4 | 1 | 0 |  |
| 0x218 | 4 | 1 | 0 |  |
| 0x21C | 4 | 2 | 0 |  |
| 0x220 | 4 | 1 | 0 |  |
| 0x224 | 4 | 2 | 0 |  |
| 0x228 | 1 | 2 | 0 |  |
| 0x22C | 4 | 2 | 0 |  |
| 0x234 | 4 | 2 | 0 |  |
| 0x238 | 4 | 2 | 0 |  |
| 0x23C | 4 | 2 | 0 |  |
| 0x240 | 4 | 1 | 0 |  |
| 0x248 | 4 | 1 | 0 |  |
| 0x24C | 4 | 1 | 0 |  |
| 0x250 | 1 | 1 | 0 |  |
| 0x254 | 4 | 1 | 0 |  |
| 0x258 | 4 | 1 | 0 |  |
| 0x25C | 4 | 1 | 0 |  |
| 0x260 | 4 | 1 | 0 |  |
| 0x264 | 4 | 1 | 0 |  |
| 0x268 | 1 | 1 | 0 |  |
| 0x269 | 1 | 1 | 0 |  |
| 0x26C | 4 | 1 | 0 |  |
| 0x270 | 1 | 1 | 0 |  |
| 0x271 | 1 | 1 | 0 |  |
| 0x272 | 1 | 1 | 0 |  |
| 0x274 | 4 | 2 | 1 |  |
| 0x278 | 4 | 1 | 0 |  |
| 0x27C | 1 | 1 | 0 |  |
| 0x280 | 4 | 1 | 0 |  |
| 0x284 | 4 | 1 | 0 |  |
| 0x288 | 4 | 1 | 0 |  |
| 0x28C | 4 | 1 | 0 |  |
| 0x290 | 4 | 1 | 0 |  |
| 0x294 | 4 | 2 | 1 |  |
| 0x298 | 1 | 1 | 0 |  |
| 0x29C | 4 | 1 | 0 |  |
| 0x2A0 | 4 | 1 | 0 |  |
| 0x2A4 | 1 | 1 | 0 |  |
| 0x2A8 | 4 | 1 | 0 |  |
| 0x2AC | 4 | 1 | 0 |  |
| 0x2B0 | 4 | 1 | 0 |  |
| 0x2B4 | 4 | 1 | 0 |  |
| 0x2B8 | 4 | 1 | 0 |  |
| 0x2BC | 4 | 2 | 1 |  |
| 0x2C0 | 4 | 1 | 0 |  |
| 0x2C4 | 1 | 1 | 0 |  |
| 0x2C8 | 4 | 1 | 0 |  |
| 0x2CC | 4 | 1 | 0 |  |
| 0x2D0 | 4 | 2 | 1 |  |
| 0x2D4 | 4 | 1 | 0 |  |
| 0x2D8 | 4 | 2 | 2 |  |
| 0x2DC | 4 | 1 | 0 |  |
| 0x2E0 | 4 | 1 | 0 |  |
| 0x2E4 | 4 | 1 | 0 |  |
| 0x2E8 | 4 | 1 | 0 |  |
| 0x2EC | 4 | 1 | 0 |  |
| 0x2F4 | 4 | 1 | 0 |  |
| 0x2F8 | 4 | 1 | 0 |  |
| 0x2FC | 4 | 1 | 0 |  |
| 0x300 | 4 | 1 | 0 |  |
| 0x324 | 4 | 1 | 0 |  |
| 0x328 | 4 | 1 | 0 |  |
| 0x32C | 4 | 1 | 0 |  |
| 0x330 | 4 | 1 | 0 |  |
| 0x334 | 4 | 1 | 0 |  |
| 0x338 | 4 | 1 | 0 |  |
| 0x3B8 | 4 | 1 | 0 |  |
| 0x3BC | 4 | 1 | 0 |  |
| 0x3C4 | 4 | 1 | 0 |  |
| 0x3C8 | 2 | 1 | 0 |  |
| 0x3CA | 2 | 1 | 0 |  |
| 0x3CC | 1 | 1 | 0 |  |
| 0x3CD | 1 | 1 | 0 |  |
| 0x3CE | 1 | 1 | 0 |  |
| 0x3CF | 1 | 1 | 0 |  |
| 0x3D0 | 1 | 1 | 0 |  |
| 0x3D1 | 1 | 1 | 0 |  |
| 0x3D2 | 1 | 1 | 0 |  |
| 0x3D3 | 1 | 1 | 0 |  |
| 0x3D4 | 1 | 1 | 0 |  |
| 0x3D5 | 1 | 1 | 0 |  |
| 0x3D8 | 4 | 2 | 0 |  |
| 0x3DC | 4 | 2 | 0 |  |
| 0x3E0 | 4 | 2 | 0 |  |
| 0x3E4 | 4 | 2 | 0 |  |
| 0x3EC | 4 | 2 | 0 |  |
| 0x3F0 | 4 | 2 | 0 |  |
| 0x3F8 | 4 | 2 | 0 |  |
| 0x3FC | 4 | 2 | 0 |  |
| 0x400 | 4 | 2 | 0 |  |
| 0x404 | 4 | 2 | 0 |  |
| 0x40C | 4 | 2 | 0 |  |
| 0x410 | 4 | 2 | 0 |  |
| 0x418 | 1 | 1 | 0 |  |
| 0x419 | 1 | 1 | 0 |  |
| 0x41A | 1 | 2 | 0 |  |
| 0x41B | 1 | 1 | 0 |  |
| 0x41C | 1 | 1 | 0 |  |
| 0x41D | 1 | 1 | 0 |  |
| 0x41E | 1 | 1 | 0 |  |
| 0x41F | 1 | 1 | 0 |  |
| 0x420 | 1 | 1 | 0 |  |
| 0x421 | 1 | 1 | 0 | 0x1 |
| 0x422 | 1 | 1 | 0 | 0x1 |
| 0x423 | 1 | 1 | 0 |  |
| 0x424 | 1 | 1 | 0 |  |
| 0x425 | 1 | 1 | 0 |  |
| 0x426 | 1 | 1 | 0 |  |
| 0x427 | 1 | 1 | 0 |  |
| 0x428 | 4 | 1 | 0 |  |
| 0x42C | 4 | 1 | 0 |  |
| 0x430 | 1 | 1 | 0 |  |
| 0x431 | 1 | 1 | 0 |  |
| 0x432 | 1 | 1 | 0 |  |
| 0x434 | 4 | 1 | 0 |  |
| 0x438 | 1 | 1 | 0 |  |
| 0x439 | 1 | 1 | 0 |  |
| 0x43A | 1 | 1 | 0 |  |
| 0x43C | 4 | 1 | 0 |  |
| 0x440 | 4 | 3 | 0 | 0x7E4E78, 0x7E4DB8 |
| 0x444 | 4 | 2 | 1 |  |
| 0x448 | 4 | 2 | 0 |  |
| 0x44C | 1 | 1 | 0 | 0x1 |
| 0x44D | 1 | 2 | 0 |  |
| 0x450 | 4 | 2 | 0 |  |
| 0x454 | 4 | 2 | 0 |  |
| 0x458 | 4 | 3 | 0 | 0x7E920C, 0x7E91EC |
| 0x45C | 4 | 2 | 1 |  |
| 0x460 | 4 | 2 | 0 |  |
| 0x464 | 1 | 1 | 0 | 0x1 |
| 0x465 | 1 | 2 | 0 |  |
| 0x468 | 4 | 2 | 0 |  |
| 0x46C | 4 | 2 | 0 |  |
| 0x470 | 4 | 3 | 0 | 0x7E920C, 0x7E91EC |
| 0x474 | 4 | 1 | 0 |  |
| 0x478 | 4 | 1 | 0 |  |
| 0x47C | 1 | 1 | 0 | 0x1 |
| 0x47D | 1 | 1 | 0 |  |
| 0x480 | 4 | 2 | 0 |  |
| 0x484 | 4 | 2 | 0 |  |
| 0x49C | 4 | 1 | 0 |  |
| 0x4A0 | 4 | 1 | 0 |  |
| 0x4B8 | 1 | 1 | 0 |  |
| 0x4BC | 4 | 1 | 0 |  |
| 0x4D4 | 1 | 1 | 0 |  |
| 0x4D8 | 4 | 1 | 0 |  |
| 0x4F0 | 4 | 2 | 0 |  |
| 0x4F4 | 4 | 2 | 0 |  |
| 0x4F8 | 1 | 1 | 0 |  |
| 0x4FC | 4 | 1 | 0 |  |
| 0x500 | 4 | 1 | 0 |  |
| 0x504 | 4 | 1 | 0 |  |
| 0x510 | 4 | 1 | 0 |  |
| 0x514 | 4 | 2 | 1 |  |
| 0x518 | 4 | 1 | 0 |  |
| 0x51C | 4 | 1 | 0 |  |

### `FootClass`

sizeof = 未知，写虚表的函数 3 个，字段 98 个，最大末端 0x6B9。

内嵌子对象：0x588 `PAVAbstractClass::?$DynamicVectorClass`，0x588 `PAVAbstractClass::?$VectorClass`，0x5AC `PAVAbstractClass::?$DynamicVectorClass`，0x5AC `PAVAbstractClass::?$VectorClass`。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 3 | 0 | 0x7E8C94 |
| 0x4 | 4 | 3 | 0 | 0x7E8C78 |
| 0x8 | 4 | 3 | 0 | 0x7E8C70 |
| 0xC | 4 | 3 | 0 | 0x7E8C68 |
| 0x14 | 1 | 1 | 1 |  |
| 0x520 | 4 | 1 | 0 | 0xFFFFFFFF |
| 0x524 | 2 | 1 | 0 |  |
| 0x526 | 2 | 1 | 0 |  |
| 0x528 | 2 | 1 | 0 |  |
| 0x52A | 2 | 1 | 0 |  |
| 0x530 | 4 | 1 | 0 |  |
| 0x534 | 4 | 1 | 0 |  |
| 0x538 | 4 | 1 | 0 |  |
| 0x53C | 1 | 1 | 0 |  |
| 0x540 | 4 | 1 | 0 |  |
| 0x558 | 2 | 1 | 0 |  |
| 0x55A | 2 | 1 | 0 |  |
| 0x55C | 2 | 1 | 0 |  |
| 0x55E | 2 | 1 | 0 |  |
| 0x560 | 2 | 1 | 0 |  |
| 0x562 | 2 | 1 | 0 |  |
| 0x564 | 2 | 1 | 1 |  |
| 0x566 | 2 | 1 | 1 |  |
| 0x568 | 4 | 1 | 0 |  |
| 0x56C | 4 | 1 | 0 |  |
| 0x570 | 4 | 1 | 0 |  |
| 0x578 | 4 | 1 | 0 |  |
| 0x57C | 4 | 1 | 0 |  |
| 0x580 | 4 | 1 | 0 |  |
| 0x584 | 4 | 1 | 0 | 0x3FF00000 |
| 0x588 | 4 | 3 | 0 | 0x7E91EC |
| 0x58C | 4 | 1 | 1 |  |
| 0x590 | 4 | 1 | 0 |  |
| 0x595 | 1 | 1 | 0 |  |
| 0x598 | 4 | 1 | 0 |  |
| 0x59C | 4 | 1 | 0 | 0xA |
| 0x5A0 | 4 | 1 | 0 |  |
| 0x5A4 | 4 | 1 | 0 |  |
| 0x5A8 | 4 | 1 | 0 |  |
| 0x5AC | 4 | 3 | 0 | 0x7E91EC |
| 0x5B0 | 4 | 1 | 1 |  |
| 0x5B4 | 4 | 1 | 0 |  |
| 0x5B9 | 1 | 1 | 0 |  |
| 0x5BC | 4 | 1 | 0 |  |
| 0x5C0 | 4 | 1 | 0 |  |
| 0x5C4 | 4 | 1 | 0 | 0xFFFFFFFF |
| 0x5C8 | 4 | 1 | 0 |  |
| 0x5CC | 4 | 1 | 0 |  |
| 0x5D1 | 1 | 1 | 0 |  |
| 0x5D4 | 4 | 1 | 1 |  |
| 0x5D8 | 4 | 1 | 0 |  |
| 0x5DC | 4 | 1 | 0 |  |
| 0x5E0 | 4 | 1 | 0 | 0xFFFFFFFF |
| 0x640 | 4 | 1 | 0 |  |
| 0x648 | 4 | 1 | 0 |  |
| 0x64C | 4 | 1 | 0 |  |
| 0x650 | 4 | 1 | 0 |  |
| 0x658 | 4 | 1 | 0 |  |
| 0x65C | 4 | 1 | 0 |  |
| 0x664 | 4 | 1 | 0 |  |
| 0x668 | 4 | 1 | 0 |  |
| 0x670 | 4 | 1 | 0 |  |
| 0x674 | 4 | 2 | 1 | 0x0 |
| 0x678 | 4 | 1 | 0 |  |
| 0x67C | 4 | 1 | 0 |  |
| 0x680 | 4 | 1 | 0 |  |
| 0x684 | 1 | 1 | 0 | 0xFF |
| 0x685 | 1 | 1 | 0 |  |
| 0x686 | 1 | 1 | 0 |  |
| 0x687 | 1 | 1 | 0 |  |
| 0x688 | 1 | 1 | 0 |  |
| 0x689 | 1 | 1 | 0 |  |
| 0x68A | 1 | 1 | 0 |  |
| 0x68B | 1 | 1 | 0 |  |
| 0x68C | 1 | 1 | 0 |  |
| 0x68D | 1 | 1 | 0 |  |
| 0x68E | 1 | 1 | 0 |  |
| 0x68F | 1 | 1 | 0 |  |
| 0x690 | 1 | 1 | 0 |  |
| 0x691 | 1 | 1 | 0 |  |
| 0x694 | 4 | 1 | 0 |  |
| 0x698 | 4 | 1 | 0 |  |
| 0x69C | 4 | 2 | 1 |  |
| 0x6A0 | 4 | 1 | 0 |  |
| 0x6A8 | 4 | 1 | 0 |  |
| 0x6AC | 1 | 1 | 0 |  |
| 0x6AD | 1 | 1 | 0 |  |
| 0x6AE | 1 | 1 | 0 |  |
| 0x6AF | 1 | 1 | 0 |  |
| 0x6B0 | 1 | 1 | 0 |  |
| 0x6B1 | 1 | 1 | 0 |  |
| 0x6B2 | 1 | 1 | 0 |  |
| 0x6B3 | 1 | 1 | 0 |  |
| 0x6B4 | 1 | 1 | 0 |  |
| 0x6B5 | 1 | 1 | 0 |  |
| 0x6B6 | 1 | 1 | 0 | 0x1 |
| 0x6B7 | 1 | 1 | 0 |  |
| 0x6B8 | 1 | 1 | 0 |  |

### `UnitClass`

sizeof = 2280，写虚表的函数 2 个，字段 30 个，最大末端 0x6E8。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 2 | 1 | 0x7F5C70 |
| 0x4 | 4 | 2 | 0 | 0x7F5C54 |
| 0x8 | 4 | 2 | 0 | 0x7F5C4C |
| 0xC | 4 | 2 | 0 | 0x7F5C44 |
| 0x6C | 4 | 1 | 0 |  |
| 0x70 | 4 | 1 | 0 |  |
| 0x90 | 1 | 1 | 0 | 0x0 |
| 0x1FC | 4 | 1 | 0 |  |
| 0x200 | 4 | 1 | 0 |  |
| 0x204 | 4 | 1 | 0 |  |
| 0x21C | 4 | 0 | 6 |  |
| 0x2FC | 4 | 1 | 0 |  |
| 0x3D2 | 1 | 1 | 0 |  |
| 0x5D4 | 4 | 1 | 1 |  |
| 0x674 | 4 | 1 | 3 |  |
| 0x6C0 | 4 | 1 | 0 |  |
| 0x6C4 | 4 | 1 | 8 |  |
| 0x6C8 | 4 | 1 | 0 |  |
| 0x6CC | 4 | 2 | 1 | 0xFFFFFFFF |
| 0x6D0 | 1 | 1 | 0 |  |
| 0x6D1 | 1 | 1 | 0 |  |
| 0x6D2 | 1 | 1 | 0 |  |
| 0x6D3 | 1 | 1 | 0 |  |
| 0x6D4 | 4 | 1 | 0 |  |
| 0x6D8 | 4 | 1 | 0 |  |
| 0x6DC | 4 | 2 | 1 |  |
| 0x6E0 | 1 | 1 | 0 |  |
| 0x6E1 | 1 | 1 | 0 |  |
| 0x6E2 | 1 | 1 | 0 |  |
| 0x6E4 | 4 | 1 | 0 |  |

### `InfantryClass`

sizeof = 1776，写虚表的函数 2 个，字段 23 个，最大末端 0x6EC。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 2 | 0 | 0x7EB058 |
| 0x4 | 4 | 2 | 0 | 0x7EB03C |
| 0x8 | 4 | 2 | 0 | 0x7EB034 |
| 0xC | 4 | 2 | 0 | 0x7EB02C |
| 0x90 | 1 | 1 | 0 |  |
| 0x21C | 4 | 0 | 3 |  |
| 0x2DC | 4 | 0 | 1 |  |
| 0x5D4 | 4 | 1 | 1 |  |
| 0x674 | 4 | 0 | 3 |  |
| 0x6C0 | 4 | 1 | 2 |  |
| 0x6C4 | 4 | 2 | 0 | 0xFFFFFFFF |
| 0x6C8 | 4 | 1 | 0 |  |
| 0x6D0 | 4 | 1 | 0 |  |
| 0x6D4 | 4 | 1 | 0 |  |
| 0x6D8 | 1 | 1 | 0 |  |
| 0x6D9 | 1 | 1 | 0 |  |
| 0x6DA | 1 | 1 | 0 |  |
| 0x6DB | 1 | 2 | 0 |  |
| 0x6DC | 1 | 1 | 0 |  |
| 0x6DD | 1 | 1 | 0 |  |
| 0x6E0 | 4 | 1 | 0 |  |
| 0x6E4 | 1 | 1 | 0 |  |
| 0x6E8 | 4 | 2 | 0 | 0x2 |

### `AircraftClass`

sizeof = 1752，写虚表的函数 2 个，字段 20 个，最大末端 0x6D6。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 2 | 0 | 0x7E22A4 |
| 0x4 | 4 | 2 | 0 | 0x7E2288 |
| 0x8 | 4 | 2 | 0 | 0x7E2280 |
| 0xC | 4 | 2 | 0 | 0x7E2278 |
| 0x90 | 1 | 1 | 0 | 0x0 |
| 0x21C | 4 | 0 | 3 |  |
| 0x5D4 | 4 | 1 | 1 |  |
| 0x674 | 4 | 0 | 2 |  |
| 0x6C0 | 4 | 2 | 0 | 0x7E2250 |
| 0x6C4 | 4 | 2 | 2 |  |
| 0x6C8 | 1 | 1 | 0 | 0x0 |
| 0x6C9 | 1 | 1 | 0 | 0x0 |
| 0x6CA | 1 | 1 | 0 | 0x0 |
| 0x6CC | 4 | 1 | 0 | 0x0 |
| 0x6D0 | 1 | 1 | 0 | 0x0 |
| 0x6D1 | 1 | 1 | 0 | 0x0 |
| 0x6D2 | 1 | 1 | 0 | 0x0 |
| 0x6D3 | 1 | 1 | 0 | 0x5 |
| 0x6D4 | 1 | 1 | 0 |  |
| 0x6D5 | 1 | 1 | 0 |  |

### `BuildingClass`

sizeof = 未知，写虚表的函数 3 个，字段 117 个，最大末端 0x71C。

内嵌子对象：0x66C `PAVInfantryClass::?$DynamicVectorClass`，0x66C `PAVInfantryClass::?$VectorClass`，0x684 `PAVInfantryClass::?$DynamicVectorClass`，0x684 `PAVInfantryClass::?$VectorClass`。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 3 | 0 | 0x7E3EBC |
| 0x4 | 4 | 3 | 0 | 0x7E3EA0 |
| 0x8 | 4 | 3 | 0 | 0x7E3E98 |
| 0xC | 4 | 3 | 0 | 0x7E3E90 |
| 0x6C | 4 | 0 | 1 |  |
| 0x80 | 1 | 1 | 0 | 0x1 |
| 0x90 | 1 | 1 | 0 | 0x0 |
| 0x21C | 4 | 0 | 5 |  |
| 0x520 | 4 | 2 | 8 |  |
| 0x524 | 4 | 2 | 1 |  |
| 0x528 | 4 | 1 | 0 |  |
| 0x530 | 4 | 1 | 0 |  |
| 0x534 | 4 | 1 | 0 |  |
| 0x538 | 4 | 1 | 0 |  |
| 0x53C | 4 | 1 | 0 |  |
| 0x540 | 4 | 1 | 0 |  |
| 0x544 | 4 | 1 | 1 |  |
| 0x548 | 4 | 1 | 0 |  |
| 0x54C | 4 | 1 | 0 |  |
| 0x550 | 4 | 1 | 0 |  |
| 0x558 | 4 | 1 | 0 |  |
| 0x5B0 | 4 | 1 | 0 |  |
| 0x5B4 | 4 | 1 | 0 |  |
| 0x5B8 | 4 | 1 | 0 |  |
| 0x5BC | 4 | 1 | 0 |  |
| 0x5C0 | 4 | 1 | 0 |  |
| 0x5C4 | 1 | 1 | 0 |  |
| 0x5C8 | 4 | 1 | 1 |  |
| 0x5E8 | 1 | 1 | 0 |  |
| 0x5EC | 4 | 1 | 0 |  |
| 0x5F0 | 4 | 1 | 0 |  |
| 0x5F4 | 4 | 1 | 0 |  |
| 0x5F8 | 4 | 1 | 0 |  |
| 0x5FC | 4 | 1 | 0 |  |
| 0x600 | 4 | 1 | 0 |  |
| 0x604 | 4 | 1 | 0 |  |
| 0x60C | 4 | 1 | 0 |  |
| 0x610 | 4 | 1 | 0 |  |
| 0x614 | 4 | 2 | 2 |  |
| 0x618 | 4 | 1 | 0 |  |
| 0x61C | 4 | 1 | 0 |  |
| 0x620 | 4 | 3 | 0 |  |
| 0x624 | 1 | 2 | 0 |  |
| 0x628 | 4 | 2 | 0 |  |
| 0x630 | 4 | 2 | 0 |  |
| 0x634 | 4 | 2 | 0 |  |
| 0x638 | 4 | 2 | 0 | 0x1 |
| 0x63C | 4 | 1 | 0 |  |
| 0x640 | 4 | 1 | 0 |  |
| 0x644 | 4 | 1 | 0 |  |
| 0x648 | 4 | 1 | 0 |  |
| 0x64C | 4 | 1 | 0 |  |
| 0x650 | 4 | 1 | 0 |  |
| 0x654 | 4 | 1 | 0 |  |
| 0x658 | 4 | 1 | 0 |  |
| 0x65C | 4 | 1 | 0 |  |
| 0x660 | 1 | 1 | 0 | 0x1 |
| 0x661 | 1 | 1 | 0 |  |
| 0x662 | 1 | 1 | 0 |  |
| 0x664 | 4 | 1 | 0 |  |
| 0x668 | 1 | 1 | 0 |  |
| 0x669 | 1 | 1 | 0 |  |
| 0x66C | 4 | 3 | 2 | 0x7E43E8, 0x7E43C8 |
| 0x670 | 4 | 3 | 1 |  |
| 0x674 | 4 | 3 | 1 |  |
| 0x678 | 1 | 2 | 0 | 0x1 |
| 0x679 | 1 | 3 | 1 | 0x0 |
| 0x67C | 4 | 2 | 0 |  |
| 0x680 | 4 | 2 | 0 |  |
| 0x684 | 4 | 3 | 2 | 0x7E43E8, 0x7E43C8 |
| 0x688 | 4 | 3 | 1 |  |
| 0x68C | 4 | 3 | 1 |  |
| 0x690 | 1 | 2 | 0 | 0x1 |
| 0x691 | 1 | 3 | 1 | 0x0 |
| 0x694 | 4 | 2 | 0 |  |
| 0x698 | 4 | 2 | 0 |  |
| 0x69C | 4 | 1 | 0 |  |
| 0x6C8 | 1 | 1 | 0 |  |
| 0x6C9 | 1 | 1 | 0 |  |
| 0x6CA | 1 | 1 | 0 |  |
| 0x6CB | 1 | 1 | 0 |  |
| 0x6CC | 1 | 1 | 0 |  |
| 0x6D0 | 4 | 1 | 0 |  |
| 0x6D8 | 4 | 1 | 0 |  |
| 0x6DC | 1 | 1 | 0 | 0x1 |
| 0x6DD | 1 | 1 | 0 |  |
| 0x6DE | 1 | 1 | 0 |  |
| 0x6DF | 1 | 1 | 0 |  |
| 0x6E0 | 1 | 1 | 0 |  |
| 0x6E1 | 1 | 1 | 0 |  |
| 0x6E2 | 1 | 1 | 0 |  |
| 0x6E3 | 1 | 1 | 0 |  |
| 0x6E4 | 1 | 1 | 0 |  |
| 0x6E5 | 1 | 1 | 0 |  |
| 0x6E6 | 1 | 1 | 0 |  |
| 0x6E7 | 1 | 1 | 0 |  |
| 0x6E8 | 1 | 1 | 0 |  |
| 0x6E9 | 1 | 1 | 0 |  |
| 0x6EA | 1 | 1 | 0 | 0x1 |
| 0x6EB | 1 | 2 | 0 | 0xFF |
| 0x6EC | 1 | 3 | 1 | 0x1 |
| 0x6ED | 1 | 1 | 0 |  |
| 0x6F0 | 4 | 1 | 0 |  |
| 0x6F4 | 4 | 1 | 0 |  |
| 0x6F8 | 1 | 1 | 0 |  |
| 0x6F9 | 1 | 1 | 0 |  |
| 0x6FA | 1 | 1 | 0 |  |
| 0x6FC | 4 | 1 | 0 |  |
| 0x700 | 2 | 1 | 0 | 0x3E8 |
| 0x702 | 1 | 1 | 0 |  |
| 0x703 | 1 | 1 | 0 |  |
| 0x704 | 4 | 1 | 0 |  |
| 0x708 | 4 | 1 | 0 |  |
| 0x70C | 4 | 1 | 0 |  |
| 0x710 | 4 | 1 | 0 |  |
| 0x714 | 4 | 1 | 0 |  |
| 0x718 | 4 | 1 | 0 |  |

### `AbstractTypeClass`

sizeof = 160，写虚表的函数 4 个，字段 8 个，最大末端 0x65。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 4 | 0 | 0x7E2000 |
| 0x4 | 4 | 4 | 0 | 0x7E1FE4 |
| 0x8 | 4 | 4 | 0 | 0x7E1FDC |
| 0xC | 4 | 4 | 0 | 0x7E1FD4 |
| 0x24 | 1 | 1 | 0 | 0x0 |
| 0x3D | 1 | 1 | 1 | 0x0 |
| 0x60 | 4 | 2 | 0 | 0x887734 |
| 0x64 | 1 | 1 | 0 | 0x0 |

### `ObjectTypeClass`

sizeof = 未知，写虚表的函数 3 个，字段 69 个，最大末端 0x294。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 3 | 0 | 0x7EF2D8 |
| 0x4 | 4 | 3 | 0 | 0x7EF2BC |
| 0x8 | 4 | 3 | 0 | 0x7EF2B4 |
| 0xC | 4 | 3 | 0 | 0x7EF2AC |
| 0x98 | 1 | 1 | 0 |  |
| 0x99 | 1 | 1 | 0 |  |
| 0x9A | 1 | 1 | 0 |  |
| 0x9C | 4 | 1 | 0 |  |
| 0xA0 | 4 | 1 | 0 |  |
| 0xA4 | 4 | 3 | 1 |  |
| 0xA8 | 1 | 2 | 1 |  |
| 0xAC | 4 | 1 | 0 |  |
| 0xB0 | 4 | 1 | 0 |  |
| 0xB4 | 4 | 1 | 0 |  |
| 0xB8 | 4 | 1 | 0 |  |
| 0xBC | 4 | 1 | 0 |  |
| 0xC0 | 4 | 1 | 0 |  |
| 0xC4 | 4 | 1 | 0 |  |
| 0xC8 | 4 | 2 | 0 |  |
| 0xCC | 4 | 2 | 0 |  |
| 0x158 | 4 | 2 | 0 |  |
| 0x15C | 4 | 2 | 0 |  |
| 0x1E8 | 1 | 1 | 0 |  |
| 0x1EC | 4 | 1 | 0 |  |
| 0x1F0 | 4 | 1 | 0 |  |
| 0x1F4 | 4 | 1 | 0 |  |
| 0x1F8 | 1 | 1 | 0 |  |
| 0x211 | 1 | 1 | 0 |  |
| 0x212 | 1 | 1 | 0 |  |
| 0x213 | 1 | 1 | 0 |  |
| 0x22C | 1 | 1 | 0 |  |
| 0x22D | 1 | 1 | 0 |  |
| 0x22E | 1 | 1 | 0 |  |
| 0x22F | 1 | 1 | 0 |  |
| 0x230 | 1 | 1 | 0 |  |
| 0x231 | 1 | 1 | 0 |  |
| 0x232 | 1 | 1 | 0 |  |
| 0x233 | 1 | 1 | 0 |  |
| 0x234 | 1 | 1 | 0 |  |
| 0x235 | 1 | 1 | 0 |  |
| 0x236 | 1 | 1 | 0 |  |
| 0x237 | 1 | 1 | 0 |  |
| 0x238 | 1 | 1 | 0 |  |
| 0x239 | 1 | 1 | 0 |  |
| 0x23A | 1 | 1 | 0 |  |
| 0x23B | 1 | 1 | 0 |  |
| 0x23C | 1 | 1 | 0 |  |
| 0x23D | 1 | 1 | 0 |  |
| 0x240 | 4 | 1 | 0 | 0x10 |
| 0x244 | 4 | 3 | 1 |  |
| 0x248 | 4 | 3 | 0 |  |
| 0x24C | 4 | 3 | 0 |  |
| 0x250 | 1 | 3 | 0 |  |
| 0x254 | 4 | 3 | 0 |  |
| 0x258 | 4 | 3 | 1 |  |
| 0x25C | 4 | 3 | 0 |  |
| 0x260 | 4 | 3 | 0 |  |
| 0x264 | 1 | 3 | 0 |  |
| 0x268 | 4 | 3 | 0 |  |
| 0x26C | 4 | 3 | 1 |  |
| 0x270 | 4 | 3 | 0 |  |
| 0x274 | 4 | 3 | 0 |  |
| 0x278 | 1 | 3 | 0 |  |
| 0x27C | 4 | 3 | 0 |  |
| 0x280 | 4 | 3 | 1 |  |
| 0x284 | 4 | 3 | 0 |  |
| 0x288 | 4 | 3 | 0 |  |
| 0x28C | 1 | 3 | 0 |  |
| 0x290 | 4 | 3 | 0 |  |

### `TechnoTypeClass`

sizeof = 未知，写虚表的函数 3 个，字段 455 个，最大末端 0xDF4。

内嵌子对象：0x314 `PBVVoxelAnimTypeClass::?$TypeList`，0x314 `PBVVoxelAnimTypeClass::?$VectorClass`，0x330 `H::?$TypeList`，0x330 `H::?$VectorClass`，0x3E8 `PBVBuildingTypeClass::?$TypeList`，0x3E8 `PBVBuildingTypeClass::?$VectorClass`，0x414 `H::?$TypeList`，0x414 `H::?$VectorClass`，0x430 `H::?$TypeList`，0x430 `H::?$VectorClass`，0x44C `H::?$TypeList`，0x44C `H::?$VectorClass`，0x468 `H::?$TypeList`，0x468 `H::?$VectorClass`，0x484 `H::?$TypeList`，0x484 `H::?$VectorClass`，0x4A0 `H::?$TypeList`，0x4A0 `H::?$VectorClass`，0x4BC `H::?$TypeList`，0x4BC `H::?$VectorClass`，0x4D8 `H::?$TypeList`，0x4D8 `H::?$VectorClass`，0x4F4 `H::?$TypeList`，0x4F4 `H::?$VectorClass`，0x510 `H::?$TypeList`，0x510 `H::?$VectorClass`，0x5C4 `PBVAnimTypeClass::?$TypeList`，0x5C4 `PBVAnimTypeClass::?$VectorClass`，0x638 `H::?$TypeList`，0x638 `H::?$VectorClass`，0x654 `H::?$TypeList`，0x654 `H::?$VectorClass`，0x72C `PBVAnimTypeClass::?$TypeList`，0x72C `PBVAnimTypeClass::?$VectorClass`，0x748 `PBVAnimTypeClass::?$TypeList`，0x748 `PBVAnimTypeClass::?$VectorClass`，0x778 `PBVParticleSystemTypeClass::?$TypeList`，0x778 `PBVParticleSystemTypeClass::?$VectorClass`，0x794 `PBVParticleSystemTypeClass::?$TypeList`，0x794 `PBVParticleSystemTypeClass::?$VectorClass`。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 3 | 0 | 0x7F4ED8 |
| 0x4 | 4 | 3 | 0 | 0x7F4EBC |
| 0x8 | 4 | 3 | 0 | 0x7F4EB4 |
| 0xC | 4 | 3 | 0 | 0x7F4EAC |
| 0x234 | 1 | 1 | 0 | 0x1 |
| 0x294 | 4 | 1 | 0 | 0x1 |
| 0x298 | 4 | 1 | 0 |  |
| 0x29C | 4 | 1 | 0 |  |
| 0x2A0 | 4 | 1 | 0 |  |
| 0x2A4 | 4 | 1 | 0 |  |
| 0x2A8 | 4 | 1 | 0 |  |
| 0x2AC | 2 | 1 | 0 |  |
| 0x2AE | 4 | 1 | 0 |  |
| 0x2B2 | 4 | 1 | 0 |  |
| 0x2B6 | 4 | 1 | 0 |  |
| 0x2BA | 4 | 1 | 0 |  |
| 0x2BE | 2 | 1 | 0 |  |
| 0x2C0 | 4 | 1 | 0 |  |
| 0x2C4 | 4 | 1 | 0 |  |
| 0x2C8 | 4 | 1 | 0 |  |
| 0x2CC | 4 | 1 | 0 |  |
| 0x2D0 | 4 | 1 | 0 |  |
| 0x2D4 | 4 | 1 | 0 |  |
| 0x2D8 | 4 | 1 | 0 |  |
| 0x2DC | 4 | 1 | 0 |  |
| 0x2E0 | 4 | 1 | 0 |  |
| 0x2E4 | 4 | 1 | 0 |  |
| 0x2E8 | 4 | 1 | 0 |  |
| 0x2EC | 4 | 1 | 0 |  |
| 0x2F0 | 4 | 1 | 0 |  |
| 0x2F4 | 4 | 1 | 0 |  |
| 0x2F8 | 4 | 1 | 0 | 0x1F4 |
| 0x300 | 4 | 1 | 0 | 0xD2F1A9FC |
| 0x304 | 4 | 1 | 0 | 0x3F60624D |
| 0x308 | 4 | 1 | 0 | 0xEB851EB8 |
| 0x30C | 4 | 1 | 0 | 0x3F9EB851 |
| 0x310 | 4 | 1 | 0 | 0x7 |
| 0x314 | 4 | 3 | 1 | 0x7F0D3C, 0x7F0D5C |
| 0x318 | 4 | 1 | 1 |  |
| 0x31C | 4 | 1 | 0 |  |
| 0x321 | 1 | 1 | 0 |  |
| 0x324 | 4 | 1 | 0 |  |
| 0x328 | 4 | 1 | 0 |  |
| 0x330 | 4 | 3 | 1 | 0x7E4DD8 |
| 0x334 | 4 | 1 | 1 |  |
| 0x338 | 4 | 1 | 0 |  |
| 0x33D | 1 | 1 | 0 |  |
| 0x340 | 4 | 1 | 0 |  |
| 0x344 | 4 | 1 | 0 |  |
| 0x34C | 4 | 1 | 0 |  |
| 0x350 | 4 | 1 | 0 |  |
| 0x354 | 4 | 1 | 0 |  |
| 0x358 | 4 | 1 | 0 |  |
| 0x360 | 4 | 1 | 0 |  |
| 0x364 | 4 | 1 | 0 |  |
| 0x368 | 4 | 1 | 0 |  |
| 0x36C | 4 | 1 | 0 |  |
| 0x370 | 4 | 1 | 0 |  |
| 0x374 | 4 | 1 | 0 |  |
| 0x378 | 4 | 1 | 0 |  |
| 0x37C | 4 | 1 | 0 | 0x40000000 |
| 0x380 | 4 | 1 | 0 |  |
| 0x384 | 4 | 1 | 0 |  |
| 0x388 | 4 | 1 | 0 |  |
| 0x38C | 4 | 1 | 0 |  |
| 0x390 | 1 | 1 | 0 |  |
| 0x394 | 4 | 1 | 0 |  |
| 0x398 | 4 | 1 | 0 | 0xF |
| 0x3A0 | 4 | 1 | 0 | 0x382D7365 |
| 0x3A4 | 4 | 1 | 0 | 0x3FE0C152 |
| 0x3A8 | 4 | 1 | 0 |  |
| 0x3AC | 4 | 1 | 0 | 0x3FD00000 |
| 0x3B0 | 4 | 1 | 0 | 0x4AE74487 |
| 0x3B4 | 4 | 1 | 0 | 0x3FD65718 |
| 0x3B8 | 4 | 1 | 0 | 0x7FFFFFFF |
| 0x3BC | 4 | 1 | 0 |  |
| 0x3C0 | 4 | 2 | 0 |  |
| 0x3C8 | 4 | 1 | 0 |  |
| 0x3CC | 4 | 1 | 0 |  |
| 0x3D0 | 4 | 1 | 0 | 0x8 |
| 0x3D4 | 4 | 1 | 0 |  |
| 0x3D8 | 1 | 1 | 0 |  |
| 0x3DC | 4 | 1 | 0 | 0x46 |
| 0x3E0 | 4 | 1 | 0 |  |
| 0x3E4 | 4 | 1 | 0 |  |
| 0x3E8 | 4 | 3 | 1 | 0x7ED90C, 0x7EAA08 |
| 0x3EC | 4 | 1 | 1 |  |
| 0x3F0 | 4 | 1 | 0 |  |
| 0x3F5 | 1 | 1 | 0 |  |
| 0x3F8 | 4 | 1 | 0 |  |
| 0x3FC | 4 | 1 | 0 |  |
| 0x404 | 4 | 1 | 0 |  |
| 0x408 | 4 | 1 | 0 |  |
| 0x40C | 4 | 1 | 0 |  |
| 0x410 | 1 | 1 | 0 |  |
| 0x414 | 4 | 3 | 0 | 0x7E4DD8 |
| 0x418 | 4 | 1 | 1 |  |
| 0x41C | 4 | 1 | 0 |  |
| 0x421 | 1 | 1 | 0 |  |
| 0x424 | 4 | 1 | 0 |  |
| 0x428 | 4 | 1 | 0 |  |
| 0x430 | 4 | 3 | 0 | 0x7E4DD8 |
| 0x434 | 4 | 1 | 1 |  |
| 0x438 | 4 | 1 | 0 |  |
| 0x43D | 1 | 1 | 0 |  |
| 0x440 | 4 | 1 | 0 |  |
| 0x444 | 4 | 1 | 0 |  |
| 0x44C | 4 | 3 | 0 | 0x7E4DD8 |
| 0x450 | 4 | 1 | 1 |  |
| 0x454 | 4 | 1 | 0 |  |
| 0x459 | 1 | 1 | 0 |  |
| 0x45C | 4 | 1 | 0 |  |
| 0x460 | 4 | 1 | 0 |  |
| 0x468 | 4 | 3 | 0 | 0x7E4DD8 |
| 0x46C | 4 | 1 | 1 |  |
| 0x470 | 4 | 1 | 0 |  |
| 0x475 | 1 | 1 | 0 |  |
| 0x478 | 4 | 1 | 0 |  |
| 0x47C | 4 | 1 | 0 |  |
| 0x484 | 4 | 3 | 0 | 0x7E4DD8 |
| 0x488 | 4 | 1 | 1 |  |
| 0x48C | 4 | 1 | 0 |  |
| 0x491 | 1 | 1 | 0 |  |
| 0x494 | 4 | 1 | 0 |  |
| 0x498 | 4 | 1 | 0 |  |
| 0x4A0 | 4 | 3 | 0 | 0x7E4DD8 |
| 0x4A4 | 4 | 1 | 1 |  |
| 0x4A8 | 4 | 1 | 0 |  |
| 0x4AD | 1 | 1 | 0 |  |
| 0x4B0 | 4 | 1 | 0 |  |
| 0x4B4 | 4 | 1 | 0 |  |
| 0x4BC | 4 | 3 | 0 | 0x7E4DD8 |
| 0x4C0 | 4 | 1 | 1 |  |
| 0x4C4 | 4 | 1 | 0 |  |
| 0x4C9 | 1 | 1 | 0 |  |
| 0x4CC | 4 | 1 | 0 |  |
| 0x4D0 | 4 | 1 | 0 |  |
| 0x4D8 | 4 | 3 | 0 | 0x7E4DD8 |
| 0x4DC | 4 | 1 | 1 |  |
| 0x4E0 | 4 | 1 | 0 |  |
| 0x4E5 | 1 | 1 | 0 |  |
| 0x4E8 | 4 | 1 | 0 |  |
| 0x4EC | 4 | 1 | 0 |  |
| 0x4F4 | 4 | 3 | 0 | 0x7E4DD8 |
| 0x504 | 4 | 1 | 0 |  |
| 0x508 | 4 | 1 | 0 |  |
| 0x510 | 4 | 3 | 0 | 0x7E4DD8 |
| 0x520 | 4 | 1 | 0 |  |
| 0x524 | 4 | 1 | 0 |  |
| 0x52C | 4 | 1 | 0 |  |
| 0x530 | 4 | 1 | 0 |  |
| 0x534 | 4 | 1 | 0 |  |
| 0x538 | 4 | 1 | 0 |  |
| 0x53C | 4 | 1 | 0 |  |
| 0x540 | 4 | 1 | 0 |  |
| 0x544 | 4 | 1 | 0 |  |
| 0x548 | 4 | 1 | 0 |  |
| 0x54C | 4 | 1 | 0 |  |
| 0x550 | 4 | 1 | 0 |  |
| 0x554 | 4 | 1 | 0 |  |
| 0x558 | 4 | 1 | 0 |  |
| 0x55C | 4 | 1 | 0 |  |
| 0x560 | 4 | 1 | 0 |  |
| 0x564 | 4 | 1 | 0 |  |
| 0x568 | 4 | 1 | 0 |  |
| 0x56C | 4 | 1 | 0 |  |
| 0x570 | 4 | 1 | 0 |  |
| 0x574 | 4 | 1 | 0 |  |
| 0x578 | 4 | 1 | 0 |  |
| 0x57C | 4 | 1 | 0 |  |
| 0x580 | 4 | 1 | 0 |  |
| 0x584 | 4 | 1 | 0 |  |
| 0x588 | 4 | 1 | 0 |  |
| 0x58C | 4 | 1 | 0 |  |
| 0x590 | 4 | 1 | 0 |  |
| 0x594 | 4 | 1 | 0 |  |
| 0x598 | 4 | 1 | 0 |  |
| 0x59C | 4 | 1 | 0 |  |
| 0x5A0 | 4 | 1 | 0 |  |
| 0x5A4 | 4 | 1 | 0 |  |
| 0x5A8 | 4 | 1 | 0 |  |
| 0x5AC | 4 | 1 | 0 |  |
| 0x5B0 | 4 | 1 | 0 |  |
| 0x5B4 | 4 | 1 | 0 |  |
| 0x5B8 | 4 | 1 | 0 |  |
| 0x5BC | 4 | 1 | 0 |  |
| 0x5C0 | 4 | 1 | 0 |  |
| 0x5C4 | 4 | 3 | 1 | 0x7EB6D4, 0x7EB6F4 |
| 0x5D4 | 4 | 1 | 0 |  |
| 0x5D8 | 4 | 1 | 0 |  |
| 0x5E0 | 4 | 1 | 0 |  |
| 0x5E4 | 1 | 1 | 0 |  |
| 0x5E8 | 4 | 1 | 0 |  |
| 0x5EC | 1 | 1 | 0 |  |
| 0x5ED | 1 | 1 | 0 |  |
| 0x5EE | 1 | 1 | 0 |  |
| 0x5EF | 1 | 1 | 0 |  |
| 0x5F0 | 4 | 1 | 0 |  |
| 0x5F4 | 4 | 1 | 0 |  |
| 0x5F8 | 4 | 1 | 0 |  |
| 0x5FC | 4 | 1 | 0 | 0x5 |
| 0x600 | 4 | 1 | 0 |  |
| 0x604 | 4 | 1 | 0 |  |
| 0x608 | 4 | 1 | 0 | 0x3F800000 |
| 0x60C | 4 | 1 | 0 | 0x8C |
| 0x610 | 4 | 1 | 0 |  |
| 0x614 | 4 | 1 | 0 |  |
| 0x618 | 4 | 1 | 0 |  |
| 0x61C | 4 | 1 | 0 |  |
| 0x620 | 4 | 1 | 0 |  |
| 0x624 | 4 | 1 | 0 |  |
| 0x628 | 4 | 1 | 0 |  |
| 0x62C | 4 | 1 | 0 |  |
| 0x630 | 4 | 1 | 0 |  |
| 0x634 | 4 | 1 | 0 | 0xFF |
| 0x638 | 4 | 3 | 0 | 0x7E4DD8 |
| 0x648 | 4 | 1 | 0 |  |
| 0x64C | 4 | 1 | 0 |  |
| 0x654 | 4 | 3 | 0 | 0x7E4DD8 |
| 0x664 | 4 | 1 | 0 |  |
| 0x668 | 4 | 1 | 0 |  |
| 0x670 | 4 | 1 | 0 |  |
| 0x674 | 4 | 1 | 0 |  |
| 0x678 | 4 | 1 | 0 |  |
| 0x67C | 4 | 1 | 0 |  |
| 0x680 | 4 | 1 | 0 |  |
| 0x684 | 4 | 1 | 0 |  |
| 0x688 | 4 | 1 | 0 |  |
| 0x68C | 4 | 1 | 0 |  |
| 0x690 | 1 | 1 | 0 |  |
| 0x691 | 1 | 1 | 0 |  |
| 0x692 | 1 | 1 | 0 |  |
| 0x693 | 1 | 1 | 0 |  |
| 0x694 | 1 | 1 | 0 |  |
| 0x695 | 1 | 1 | 0 |  |
| 0x698 | 4 | 1 | 0 |  |
| 0x69C | 4 | 1 | 0 |  |
| 0x6A0 | 4 | 1 | 0 |  |
| 0x6A4 | 4 | 1 | 0 |  |
| 0x6A8 | 4 | 1 | 0 | 0x1 |
| 0x6AC | 1 | 1 | 0 |  |
| 0x6AD | 1 | 1 | 0 |  |
| 0x6AE | 1 | 1 | 0 | 0x1 |
| 0x6AF | 1 | 1 | 0 |  |
| 0x6B0 | 1 | 1 | 0 |  |
| 0x6B1 | 1 | 1 | 0 |  |
| 0x6B4 | 4 | 1 | 0 |  |
| 0x6B8 | 4 | 1 | 0 |  |
| 0x6BC | 4 | 1 | 0 |  |
| 0x6C0 | 1 | 1 | 0 |  |
| 0x6C1 | 1 | 1 | 0 |  |
| 0x6C4 | 4 | 1 | 0 |  |
| 0x6C8 | 1 | 1 | 0 |  |
| 0x6CC | 4 | 1 | 0 |  |
| 0x6D0 | 4 | 1 | 0 |  |
| 0x6D4 | 1 | 1 | 0 |  |
| 0x6D5 | 1 | 1 | 0 | 0x1 |
| 0x6D6 | 1 | 2 | 0 |  |
| 0x6EE | 1 | 1 | 0 |  |
| 0x6F0 | 4 | 2 | 1 |  |
| 0x6F4 | 1 | 2 | 1 |  |
| 0x6F5 | 1 | 2 | 0 |  |
| 0x70D | 1 | 1 | 0 |  |
| 0x710 | 4 | 2 | 1 |  |
| 0x714 | 1 | 2 | 0 |  |
| 0x718 | 4 | 1 | 0 |  |
| 0x71C | 4 | 1 | 0 |  |
| 0x720 | 4 | 1 | 0 |  |
| 0x724 | 1 | 1 | 0 | 0x1 |
| 0x728 | 4 | 1 | 0 |  |
| 0x72C | 4 | 3 | 0 | 0x7EB6D4, 0x7EB6F4 |
| 0x73C | 4 | 1 | 0 |  |
| 0x740 | 4 | 1 | 0 |  |
| 0x748 | 4 | 3 | 0 | 0x7EB6D4, 0x7EB6F4 |
| 0x758 | 4 | 1 | 0 |  |
| 0x75C | 4 | 1 | 0 |  |
| 0x764 | 4 | 1 | 0 |  |
| 0x768 | 4 | 1 | 0 |  |
| 0x76C | 4 | 1 | 0 |  |
| 0x770 | 4 | 1 | 0 |  |
| 0x774 | 4 | 1 | 0 |  |
| 0x778 | 4 | 3 | 0 | 0x7F4F9C, 0x7E4424 |
| 0x788 | 4 | 1 | 0 |  |
| 0x78C | 4 | 1 | 0 |  |
| 0x794 | 4 | 3 | 0 | 0x7F4F9C, 0x7E4424 |
| 0x7A4 | 4 | 1 | 0 |  |
| 0x7A8 | 4 | 1 | 0 |  |
| 0x7B0 | 4 | 1 | 0 |  |
| 0x7B4 | 4 | 1 | 0 |  |
| 0x7B8 | 4 | 1 | 0 |  |
| 0x7BC | 1 | 1 | 0 |  |
| 0x7C0 | 4 | 1 | 0 |  |
| 0x7C4 | 4 | 1 | 0 |  |
| 0x7C8 | 4 | 1 | 0 |  |
| 0x7CC | 4 | 1 | 0 |  |
| 0x7D0 | 4 | 1 | 0 |  |
| 0x7D4 | 4 | 1 | 0 |  |
| 0x7D8 | 4 | 1 | 0 |  |
| 0x7DC | 4 | 1 | 0 |  |
| 0x7E0 | 4 | 1 | 0 |  |
| 0x7E4 | 4 | 1 | 0 |  |
| 0x7E8 | 4 | 1 | 0 |  |
| 0x7EC | 4 | 1 | 0 |  |
| 0x7F0 | 4 | 1 | 0 |  |
| 0x7F4 | 4 | 1 | 0 |  |
| 0x7F8 | 4 | 1 | 0 |  |
| 0x7FC | 4 | 1 | 0 |  |
| 0x800 | 4 | 1 | 0 |  |
| 0x804 | 1 | 1 | 0 |  |
| 0x805 | 1 | 1 | 0 |  |
| 0x806 | 1 | 1 | 0 |  |
| 0x808 | 4 | 1 | 0 |  |
| 0x810 | 1 | 1 | 0 |  |
| 0x85C | 4 | 1 | 0 |  |
| 0x860 | 4 | 1 | 0 |  |
| 0x864 | 4 | 1 | 0 |  |
| 0x898 | 4 | 1 | 0 |  |
| 0x89C | 4 | 1 | 0 |  |
| 0x8A0 | 4 | 1 | 0 |  |
| 0x8A4 | 4 | 1 | 0 |  |
| 0x8A8 | 4 | 1 | 0 |  |
| 0x8AC | 4 | 1 | 0 |  |
| 0xA90 | 1 | 1 | 0 |  |
| 0xA94 | 4 | 1 | 0 |  |
| 0xA98 | 4 | 1 | 0 |  |
| 0xA9C | 4 | 1 | 0 |  |
| 0xAA0 | 4 | 1 | 0 |  |
| 0xAA4 | 4 | 1 | 0 |  |
| 0xAA8 | 4 | 1 | 0 |  |
| 0xC8C | 1 | 1 | 0 |  |
| 0xC8D | 1 | 1 | 0 | 0x1 |
| 0xC8E | 1 | 1 | 0 | 0x1 |
| 0xC8F | 1 | 1 | 0 |  |
| 0xC90 | 1 | 1 | 0 |  |
| 0xC91 | 1 | 1 | 0 |  |
| 0xC92 | 1 | 1 | 0 |  |
| 0xC93 | 1 | 1 | 0 |  |
| 0xC94 | 1 | 1 | 0 |  |
| 0xC95 | 1 | 1 | 0 |  |
| 0xC96 | 1 | 1 | 0 |  |
| 0xC97 | 1 | 1 | 0 | 0x1 |
| 0xC98 | 1 | 1 | 0 |  |
| 0xC99 | 1 | 1 | 0 |  |
| 0xC9A | 1 | 1 | 0 |  |
| 0xC9B | 1 | 1 | 0 |  |
| 0xC9C | 1 | 1 | 0 |  |
| 0xC9D | 1 | 1 | 0 |  |
| 0xC9E | 1 | 1 | 0 |  |
| 0xC9F | 1 | 1 | 0 |  |
| 0xCA0 | 1 | 1 | 0 |  |
| 0xCA1 | 1 | 1 | 0 |  |
| 0xCA2 | 1 | 1 | 0 |  |
| 0xCA4 | 4 | 2 | 0 |  |
| 0xCA8 | 4 | 2 | 0 |  |
| 0xCAC | 4 | 2 | 0 |  |
| 0xCB0 | 4 | 2 | 0 |  |
| 0xCB4 | 1 | 2 | 0 |  |
| 0xCB8 | 4 | 2 | 0 |  |
| 0xCBC | 4 | 2 | 0 |  |
| 0xCC0 | 4 | 2 | 0 |  |
| 0xCC4 | 4 | 2 | 0 |  |
| 0xCC8 | 1 | 2 | 0 |  |
| 0xCCC | 1 | 1 | 0 |  |
| 0xCCD | 1 | 1 | 0 |  |
| 0xCCE | 1 | 1 | 0 |  |
| 0xCCF | 1 | 1 | 0 |  |
| 0xCD0 | 1 | 1 | 0 |  |
| 0xCD1 | 1 | 1 | 0 |  |
| 0xCD2 | 1 | 1 | 0 |  |
| 0xCD3 | 1 | 1 | 0 |  |
| 0xCD4 | 1 | 1 | 0 |  |
| 0xCD5 | 1 | 1 | 0 |  |
| 0xCD8 | 4 | 1 | 0 |  |
| 0xCDC | 4 | 1 | 0 |  |
| 0xCF4 | 4 | 1 | 0 |  |
| 0xD0C | 4 | 1 | 0 |  |
| 0xD10 | 4 | 1 | 0 |  |
| 0xD14 | 1 | 1 | 0 |  |
| 0xD15 | 1 | 1 | 0 |  |
| 0xD18 | 4 | 1 | 0 |  |
| 0xD1C | 4 | 1 | 0 | 0x3F800000 |
| 0xD20 | 1 | 1 | 0 |  |
| 0xD21 | 1 | 1 | 0 |  |
| 0xD22 | 1 | 1 | 0 |  |
| 0xD23 | 1 | 1 | 0 |  |
| 0xD24 | 1 | 1 | 0 |  |
| 0xD25 | 1 | 1 | 0 |  |
| 0xD26 | 1 | 1 | 0 |  |
| 0xD27 | 1 | 1 | 0 |  |
| 0xD28 | 1 | 1 | 0 |  |
| 0xD29 | 1 | 1 | 0 |  |
| 0xD2A | 1 | 1 | 0 |  |
| 0xD2B | 1 | 1 | 0 |  |
| 0xD2C | 1 | 1 | 0 |  |
| 0xD2D | 1 | 1 | 0 |  |
| 0xD2F | 1 | 1 | 0 |  |
| 0xD30 | 1 | 1 | 0 |  |
| 0xD31 | 1 | 1 | 0 |  |
| 0xD32 | 1 | 1 | 0 |  |
| 0xD33 | 1 | 1 | 0 |  |
| 0xD34 | 1 | 1 | 0 |  |
| 0xD37 | 1 | 1 | 0 |  |
| 0xD39 | 1 | 1 | 0 |  |
| 0xD3A | 1 | 1 | 0 |  |
| 0xD3C | 1 | 1 | 0 |  |
| 0xD3D | 1 | 1 | 0 |  |
| 0xD3E | 1 | 1 | 0 |  |
| 0xD40 | 4 | 1 | 0 |  |
| 0xD44 | 4 | 1 | 0 |  |
| 0xD48 | 4 | 1 | 0 |  |
| 0xD4C | 4 | 1 | 0 |  |
| 0xD50 | 4 | 1 | 0 |  |
| 0xD54 | 1 | 1 | 0 |  |
| 0xD58 | 4 | 1 | 0 |  |
| 0xD5C | 4 | 1 | 0 |  |
| 0xD60 | 4 | 1 | 0 |  |
| 0xD64 | 4 | 1 | 0 |  |
| 0xD68 | 1 | 1 | 0 |  |
| 0xD69 | 1 | 1 | 0 |  |
| 0xD6A | 1 | 1 | 0 |  |
| 0xD6C | 4 | 1 | 0 |  |
| 0xD70 | 4 | 1 | 0 | 0x4 |
| 0xD74 | 4 | 1 | 0 | 0xE |
| 0xD78 | 4 | 1 | 0 |  |
| 0xD7C | 4 | 1 | 0 |  |
| 0xD80 | 4 | 1 | 0 | 0x1F4 |
| 0xD84 | 4 | 1 | 0 | 0x40000000 |
| 0xD88 | 4 | 1 | 0 | 0x3E19999A |
| 0xD8C | 1 | 1 | 0 |  |
| 0xD90 | 4 | 1 | 0 | 0x28 |
| 0xD94 | 1 | 1 | 0 |  |
| 0xD95 | 1 | 1 | 0 |  |
| 0xD98 | 1 | 1 | 0 |  |
| 0xD99 | 1 | 1 | 0 | 0x1 |
| 0xD9A | 1 | 1 | 0 | 0x1 |
| 0xD9B | 1 | 1 | 0 |  |
| 0xD9C | 1 | 1 | 0 |  |
| 0xD9D | 1 | 1 | 0 |  |
| 0xDA0 | 4 | 1 | 0 |  |
| 0xDA4 | 4 | 1 | 0 |  |
| 0xDA8 | 4 | 1 | 0 |  |
| 0xDAC | 1 | 1 | 0 |  |
| 0xDB0 | 4 | 1 | 0 |  |
| 0xDB4 | 4 | 1 | 0 |  |
| 0xDB8 | 4 | 1 | 0 |  |
| 0xDBC | 1 | 1 | 0 |  |
| 0xDBD | 1 | 1 | 0 | 0x1 |
| 0xDBE | 1 | 1 | 0 |  |
| 0xDBF | 1 | 1 | 0 |  |
| 0xDC0 | 4 | 1 | 0 |  |
| 0xDC4 | 4 | 1 | 0 | 0x5 |
| 0xDC8 | 4 | 1 | 0 |  |
| 0xDCC | 4 | 1 | 0 |  |
| 0xDD0 | 1 | 1 | 0 |  |
| 0xDF0 | 4 | 1 | 0 |  |

### `UnitTypeClass`

sizeof = 未知，写虚表的函数 4 个，字段 52 个，最大末端 0xE5F。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 4 | 0 | 0x7F6218 |
| 0x4 | 4 | 4 | 0 | 0x7F61FC |
| 0x8 | 4 | 4 | 0 | 0x7F61F4 |
| 0xC | 4 | 4 | 0 | 0x7F61EC |
| 0x718 | 4 | 1 | 0 | 0x20 |
| 0xD2E | 1 | 1 | 0 | 0x1 |
| 0xD35 | 1 | 1 | 0 |  |
| 0xD36 | 1 | 1 | 0 |  |
| 0xD38 | 1 | 1 | 0 | 0x1 |
| 0xD3B | 1 | 1 | 0 |  |
| 0xD96 | 1 | 1 | 0 |  |
| 0xD97 | 1 | 1 | 0 |  |
| 0xDF8 | 4 | 2 | 0 |  |
| 0xDFC | 4 | 1 | 0 |  |
| 0xE00 | 4 | 1 | 0 |  |
| 0xE04 | 4 | 1 | 0 |  |
| 0xE08 | 4 | 1 | 0 |  |
| 0xE0C | 1 | 1 | 0 |  |
| 0xE0D | 1 | 1 | 0 |  |
| 0xE0E | 1 | 1 | 0 |  |
| 0xE0F | 1 | 1 | 0 |  |
| 0xE10 | 1 | 1 | 0 |  |
| 0xE11 | 1 | 1 | 0 |  |
| 0xE12 | 1 | 1 | 0 |  |
| 0xE13 | 1 | 1 | 0 |  |
| 0xE14 | 1 | 1 | 0 | 0x1 |
| 0xE15 | 1 | 1 | 0 |  |
| 0xE16 | 1 | 1 | 0 |  |
| 0xE17 | 1 | 1 | 0 |  |
| 0xE18 | 1 | 1 | 0 |  |
| 0xE19 | 1 | 1 | 0 |  |
| 0xE1A | 1 | 1 | 0 |  |
| 0xE1B | 1 | 1 | 0 |  |
| 0xE1C | 4 | 1 | 0 |  |
| 0xE20 | 4 | 1 | 0 |  |
| 0xE24 | 4 | 1 | 0 | 0x1 |
| 0xE28 | 4 | 1 | 0 |  |
| 0xE2C | 4 | 1 | 0 |  |
| 0xE30 | 4 | 1 | 0 |  |
| 0xE34 | 4 | 1 | 0 |  |
| 0xE38 | 4 | 1 | 0 |  |
| 0xE3C | 4 | 1 | 0 | 0x8 |
| 0xE40 | 4 | 1 | 0 |  |
| 0xE44 | 4 | 1 | 0 |  |
| 0xE48 | 4 | 1 | 0 |  |
| 0xE4C | 4 | 1 | 0 |  |
| 0xE50 | 4 | 1 | 0 |  |
| 0xE54 | 4 | 1 | 0 |  |
| 0xE58 | 4 | 1 | 0 |  |
| 0xE5C | 1 | 1 | 0 | 0xC |
| 0xE5D | 1 | 1 | 0 |  |
| 0xE5E | 1 | 1 | 0 |  |

### `InfantryTypeClass`

sizeof = 未知，写虚表的函数 4 个，字段 81 个，最大末端 0xECC。

内嵌子对象：0xE50 `PBVAnimTypeClass::?$TypeList`，0xE50 `PBVAnimTypeClass::?$VectorClass`，0xE6C `PBVAnimTypeClass::?$TypeList`，0xE6C `PBVAnimTypeClass::?$VectorClass`，0xE88 `H::?$TypeList`，0xE88 `H::?$VectorClass`。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 4 | 0 | 0x7EB610 |
| 0x4 | 4 | 4 | 0 | 0x7EB5F4 |
| 0x8 | 4 | 4 | 0 | 0x7EB5EC |
| 0xC | 4 | 4 | 0 | 0x7EB5E4 |
| 0x22D | 1 | 1 | 0 | 0x1 |
| 0x718 | 4 | 1 | 0 | 0x8 |
| 0xC9B | 1 | 1 | 0 |  |
| 0xCCC | 1 | 1 | 0 |  |
| 0xCCD | 1 | 1 | 0 |  |
| 0xD2E | 1 | 1 | 0 |  |
| 0xD35 | 1 | 1 | 0 |  |
| 0xD36 | 1 | 1 | 0 |  |
| 0xD38 | 1 | 1 | 0 | 0x1 |
| 0xD3B | 1 | 1 | 0 |  |
| 0xD96 | 1 | 1 | 0 |  |
| 0xD97 | 1 | 1 | 0 | 0x1 |
| 0xDF8 | 4 | 2 | 0 | 0xFFFFFFFF |
| 0xDFC | 4 | 1 | 0 | 0x1 |
| 0xE00 | 4 | 1 | 0 | 0x7 |
| 0xE04 | 4 | 1 | 0 |  |
| 0xE08 | 4 | 1 | 0 |  |
| 0xE0C | 4 | 1 | 0 |  |
| 0xE10 | 4 | 1 | 0 |  |
| 0xE14 | 4 | 1 | 0 |  |
| 0xE18 | 4 | 1 | 0 |  |
| 0xE1C | 1 | 1 | 0 |  |
| 0xE20 | 4 | 1 | 0 |  |
| 0xE24 | 4 | 1 | 0 |  |
| 0xE28 | 4 | 1 | 0 |  |
| 0xE2C | 4 | 1 | 0 |  |
| 0xE30 | 4 | 1 | 0 |  |
| 0xE34 | 4 | 1 | 0 |  |
| 0xE38 | 1 | 1 | 0 |  |
| 0xE3C | 4 | 4 | 7 |  |
| 0xE40 | 4 | 1 | 0 |  |
| 0xE44 | 4 | 1 | 0 |  |
| 0xE48 | 4 | 1 | 0 |  |
| 0xE4C | 4 | 1 | 0 |  |
| 0xE50 | 4 | 4 | 0 | 0x7EB6D4, 0x7EB6F4 |
| 0xE54 | 4 | 2 | 2 |  |
| 0xE58 | 4 | 2 | 0 |  |
| 0xE5D | 1 | 2 | 0 |  |
| 0xE60 | 4 | 1 | 0 |  |
| 0xE64 | 4 | 1 | 0 |  |
| 0xE6C | 4 | 4 | 0 | 0x7EB6D4, 0x7EB6F4 |
| 0xE70 | 4 | 2 | 2 |  |
| 0xE74 | 4 | 2 | 0 |  |
| 0xE79 | 1 | 2 | 0 |  |
| 0xE7C | 4 | 1 | 0 |  |
| 0xE80 | 4 | 1 | 0 |  |
| 0xE88 | 4 | 4 | 0 | 0x7E4DD8, 0x7E4DB8 |
| 0xE8C | 4 | 2 | 2 |  |
| 0xE90 | 4 | 2 | 0 |  |
| 0xE95 | 1 | 2 | 0 |  |
| 0xE98 | 4 | 1 | 0 |  |
| 0xE9C | 4 | 1 | 0 |  |
| 0xEA4 | 4 | 1 | 0 |  |
| 0xEA8 | 4 | 1 | 0 |  |
| 0xEAC | 1 | 1 | 0 |  |
| 0xEAD | 1 | 1 | 0 |  |
| 0xEAE | 1 | 1 | 0 |  |
| 0xEB0 | 4 | 1 | 0 |  |
| 0xEB4 | 1 | 1 | 0 |  |
| 0xEB5 | 1 | 1 | 0 |  |
| 0xEB8 | 4 | 1 | 0 | 0x1 |
| 0xEBC | 1 | 1 | 0 |  |
| 0xEBD | 1 | 1 | 0 | 0x1 |
| 0xEBE | 1 | 1 | 0 |  |
| 0xEBF | 1 | 1 | 0 |  |
| 0xEC0 | 1 | 1 | 0 |  |
| 0xEC1 | 1 | 1 | 0 |  |
| 0xEC2 | 1 | 1 | 0 |  |
| 0xEC3 | 1 | 1 | 0 |  |
| 0xEC4 | 1 | 1 | 0 |  |
| 0xEC5 | 1 | 1 | 0 |  |
| 0xEC6 | 1 | 1 | 0 |  |
| 0xEC7 | 1 | 1 | 0 |  |
| 0xEC8 | 1 | 1 | 0 |  |
| 0xEC9 | 1 | 1 | 0 | 0x1 |
| 0xECA | 1 | 1 | 0 |  |
| 0xECB | 1 | 1 | 0 |  |

### `AircraftTypeClass`

sizeof = 3600，写虚表的函数 4 个，字段 24 个，最大末端 0xE0F。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 4 | 0 | 0x7E2868 |
| 0x4 | 4 | 4 | 0 | 0x7E284C |
| 0x8 | 4 | 4 | 0 | 0x7E2844 |
| 0xC | 4 | 4 | 0 | 0x7E283C |
| 0x718 | 4 | 1 | 0 | 0x20 |
| 0xC8D | 1 | 1 | 0 |  |
| 0xD2E | 1 | 1 | 0 |  |
| 0xD35 | 1 | 1 | 0 |  |
| 0xD36 | 1 | 1 | 0 |  |
| 0xD38 | 1 | 1 | 0 |  |
| 0xD3B | 1 | 1 | 0 |  |
| 0xD96 | 1 | 1 | 0 |  |
| 0xD97 | 1 | 1 | 0 |  |
| 0xDF8 | 4 | 2 | 0 | 0xFFFFFFFF |
| 0xDFC | 1 | 1 | 0 |  |
| 0xE00 | 4 | 1 | 0 |  |
| 0xE04 | 4 | 1 | 0 | 0x3 |
| 0xE08 | 1 | 1 | 0 |  |
| 0xE09 | 1 | 1 | 0 |  |
| 0xE0A | 1 | 1 | 0 |  |
| 0xE0B | 1 | 1 | 0 |  |
| 0xE0C | 1 | 1 | 0 |  |
| 0xE0D | 1 | 1 | 0 |  |
| 0xE0E | 1 | 1 | 0 |  |

### `BuildingTypeClass`

sizeof = 未知，写虚表的函数 3 个，字段 251 个，最大末端 0x1792。

内嵌子对象：0x1784 `H::V?$TPoint3D::?$VectorClass`。

| 偏移 | 宽 | 写 | 读 | 初值样本 |
|---:|---:|---:|---:|---|
| 0x0 | 4 | 3 | 0 | 0x7E4570 |
| 0x4 | 4 | 3 | 0 | 0x7E4554 |
| 0x8 | 4 | 3 | 0 | 0x7E454C |
| 0xC | 4 | 3 | 0 | 0x7E4544 |
| 0xC8E | 1 | 1 | 0 |  |
| 0xD2E | 1 | 1 | 0 |  |
| 0xD35 | 1 | 1 | 0 | 0x1 |
| 0xD36 | 1 | 1 | 0 | 0x1 |
| 0xD38 | 1 | 1 | 0 |  |
| 0xD3B | 1 | 1 | 0 |  |
| 0xD96 | 1 | 1 | 0 |  |
| 0xD97 | 1 | 1 | 0 |  |
| 0xDF8 | 4 | 2 | 0 |  |
| 0xDFC | 4 | 1 | 0 |  |
| 0xE00 | 4 | 3 | 1 |  |
| 0xE04 | 1 | 2 | 1 |  |
| 0xE08 | 4 | 1 | 0 |  |
| 0xE0C | 4 | 1 | 0 |  |
| 0xE10 | 4 | 1 | 0 |  |
| 0xE14 | 4 | 1 | 0 |  |
| 0xE18 | 4 | 1 | 0 |  |
| 0xE1C | 4 | 1 | 0 |  |
| 0xE20 | 4 | 1 | 0 |  |
| 0xE28 | 4 | 1 | 0 |  |
| 0xE2C | 4 | 1 | 0 |  |
| 0xE30 | 4 | 1 | 0 | 0x1388 |
| 0xE34 | 4 | 1 | 0 |  |
| 0xE38 | 4 | 1 | 0 |  |
| 0xE3C | 4 | 1 | 0 |  |
| 0xE40 | 4 | 1 | 0 |  |
| 0xE44 | 4 | 1 | 0 |  |
| 0xE48 | 4 | 1 | 0 |  |
| 0xE4C | 4 | 1 | 0 |  |
| 0xE50 | 4 | 1 | 0 |  |
| 0xE54 | 4 | 1 | 0 |  |
| 0xE58 | 4 | 1 | 0 |  |
| 0xE5C | 1 | 1 | 0 |  |
| 0xE6C | 4 | 1 | 0 |  |
| 0xE70 | 4 | 1 | 0 |  |
| 0xE74 | 4 | 1 | 0 |  |
| 0xE78 | 4 | 1 | 0 |  |
| 0xE7C | 4 | 1 | 0 |  |
| 0xE80 | 4 | 1 | 0 |  |
| 0xE84 | 4 | 1 | 0 |  |
| 0xE88 | 1 | 1 | 0 |  |
| 0xEA0 | 4 | 1 | 0 |  |
| 0xEA4 | 4 | 1 | 0 |  |
| 0xEA8 | 4 | 1 | 0 |  |
| 0xEAC | 4 | 1 | 0 |  |
| 0xEB0 | 4 | 1 | 0 |  |
| 0xEB4 | 4 | 1 | 0 | 0x3 |
| 0xEB8 | 4 | 1 | 0 |  |
| 0xEBC | 4 | 1 | 0 |  |
| 0xEC0 | 4 | 1 | 0 |  |
| 0xEC4 | 4 | 1 | 0 |  |
| 0xEC8 | 4 | 1 | 0 |  |
| 0xECC | 4 | 1 | 0 |  |
| 0xED0 | 4 | 1 | 0 |  |
| 0xED4 | 4 | 1 | 0 |  |
| 0xED8 | 4 | 1 | 0 |  |
| 0xEDC | 4 | 1 | 0 | 0x80 |
| 0xEE0 | 4 | 1 | 0 |  |
| 0xEE4 | 4 | 1 | 0 |  |
| 0xEE8 | 4 | 1 | 0 |  |
| 0xEEC | 4 | 1 | 0 |  |
| 0xEF0 | 4 | 1 | 0 |  |
| 0xEF4 | 4 | 1 | 0 |  |
| 0xEF8 | 4 | 1 | 0 |  |
| 0xEFC | 4 | 1 | 0 |  |
| 0xF00 | 4 | 1 | 0 |  |
| 0xF04 | 4 | 1 | 0 |  |
| 0xF08 | 4 | 1 | 0 |  |
| 0xF0C | 4 | 1 | 0 |  |
| 0xF10 | 4 | 1 | 0 |  |
| 0xF14 | 4 | 1 | 0 |  |
| 0xF18 | 4 | 1 | 0 |  |
| 0xF1C | 4 | 1 | 0 |  |
| 0xF20 | 4 | 1 | 0 |  |
| 0xF24 | 4 | 1 | 0 |  |
| 0xF34 | 4 | 1 | 0 |  |
| 0xF38 | 4 | 1 | 0 |  |
| 0xF3C | 4 | 1 | 0 |  |
| 0xF40 | 4 | 1 | 0 |  |
| 0xF44 | 4 | 1 | 0 |  |
| 0xF48 | 4 | 1 | 0 |  |
| 0xF4C | 4 | 1 | 0 |  |
| 0xF50 | 4 | 1 | 0 |  |
| 0xF54 | 4 | 1 | 0 |  |
| 0xF58 | 4 | 1 | 0 |  |
| 0xF5C | 4 | 1 | 0 |  |
| 0xF60 | 4 | 1 | 0 |  |
| 0xF64 | 4 | 1 | 0 |  |
| 0xF68 | 4 | 1 | 0 |  |
| 0xF6C | 4 | 1 | 0 |  |
| 0xF70 | 4 | 1 | 0 |  |
| 0xF74 | 4 | 1 | 0 |  |
| 0xF78 | 4 | 1 | 0 |  |
| 0x14E0 | 4 | 1 | 0 |  |
| 0x14E4 | 4 | 2 | 1 |  |
| 0x14E8 | 1 | 2 | 0 |  |
| 0x14EC | 4 | 2 | 1 |  |
| 0x14F0 | 1 | 2 | 0 |  |
| 0x14F4 | 4 | 2 | 1 |  |
| 0x14F8 | 1 | 2 | 0 |  |
| 0x14FC | 4 | 2 | 1 |  |
| 0x1500 | 1 | 2 | 0 |  |
| 0x1504 | 4 | 2 | 1 |  |
| 0x1508 | 1 | 2 | 0 |  |
| 0x150C | 4 | 1 | 0 |  |
| 0x1510 | 4 | 1 | 0 |  |
| 0x1514 | 4 | 1 | 0 |  |
| 0x1518 | 4 | 2 | 1 |  |
| 0x151C | 1 | 2 | 0 |  |
| 0x1520 | 4 | 1 | 0 |  |
| 0x1524 | 4 | 1 | 0 |  |
| 0x1528 | 4 | 1 | 0 |  |
| 0x152C | 4 | 1 | 0 |  |
| 0x1530 | 4 | 1 | 0 |  |
| 0x1534 | 4 | 1 | 0 |  |
| 0x1538 | 4 | 1 | 0 |  |
| 0x153C | 4 | 1 | 0 |  |
| 0x1540 | 4 | 1 | 0 |  |
| 0x1544 | 4 | 1 | 0 |  |
| 0x1548 | 2 | 1 | 0 |  |
| 0x154A | 1 | 1 | 0 | 0x1 |
| 0x154B | 1 | 1 | 0 |  |
| 0x154C | 1 | 1 | 0 |  |
| 0x154D | 1 | 1 | 0 |  |
| 0x154E | 1 | 1 | 0 |  |
| 0x154F | 1 | 1 | 0 | 0x1 |
| 0x1550 | 1 | 1 | 0 |  |
| 0x1551 | 1 | 1 | 0 |  |
| 0x1552 | 1 | 1 | 0 |  |
| 0x1554 | 4 | 1 | 0 |  |
| 0x1558 | 4 | 1 | 0 |  |
| 0x155C | 4 | 1 | 0 |  |
| 0x1560 | 4 | 1 | 0 |  |
| 0x1564 | 4 | 1 | 0 |  |
| 0x1568 | 4 | 1 | 0 |  |
| 0x156C | 4 | 1 | 0 | 0x19 |
| 0x1570 | 1 | 1 | 0 |  |
| 0x1571 | 1 | 1 | 0 |  |
| 0x1572 | 1 | 1 | 0 |  |
| 0x1573 | 1 | 1 | 0 |  |
| 0x1574 | 1 | 1 | 0 |  |
| 0x1575 | 1 | 1 | 0 |  |
| 0x1576 | 1 | 1 | 0 |  |
| 0x1577 | 1 | 1 | 0 | 0x1 |
| 0x1578 | 1 | 1 | 0 |  |
| 0x1579 | 1 | 1 | 0 |  |
| 0x157A | 1 | 1 | 0 | 0x1 |
| 0x157B | 1 | 1 | 0 |  |
| 0x157C | 1 | 1 | 0 |  |
| 0x1580 | 4 | 1 | 0 |  |
| 0x1584 | 1 | 1 | 0 | 0x1 |
| 0x1588 | 4 | 1 | 0 |  |
| 0x158C | 4 | 1 | 0 |  |
| 0x15D8 | 4 | 1 | 0 |  |
| 0x15DC | 4 | 1 | 0 |  |
| 0x1618 | 4 | 1 | 0 |  |
| 0x161C | 4 | 1 | 0 |  |
| 0x1620 | 4 | 1 | 0 |  |
| 0x1624 | 4 | 1 | 0 |  |
| 0x1628 | 4 | 1 | 0 |  |
| 0x1664 | 4 | 1 | 0 |  |
| 0x1668 | 4 | 1 | 0 |  |
| 0x16A4 | 1 | 1 | 0 |  |
| 0x16A5 | 1 | 1 | 0 |  |
| 0x16A6 | 1 | 1 | 0 |  |
| 0x16A7 | 1 | 1 | 0 |  |
| 0x16A8 | 1 | 1 | 0 |  |
| 0x16A9 | 1 | 1 | 0 |  |
| 0x16AA | 1 | 1 | 0 |  |
| 0x16AB | 1 | 1 | 0 |  |
| 0x16AC | 1 | 1 | 0 |  |
| 0x16AD | 1 | 1 | 0 |  |
| 0x16AE | 1 | 1 | 0 |  |
| 0x16AF | 1 | 1 | 0 |  |
| 0x16B0 | 1 | 1 | 0 |  |
| 0x16B1 | 1 | 1 | 0 |  |
| 0x16B2 | 1 | 1 | 0 |  |
| 0x16B3 | 1 | 1 | 0 |  |
| 0x16B4 | 1 | 1 | 0 |  |
| 0x16B5 | 1 | 1 | 0 | 0x1 |
| 0x16B6 | 1 | 1 | 0 |  |
| 0x16B7 | 1 | 1 | 0 |  |
| 0x16B8 | 1 | 1 | 0 |  |
| 0x16B9 | 1 | 1 | 0 |  |
| 0x16BA | 1 | 1 | 0 |  |
| 0x16BB | 1 | 1 | 0 |  |
| 0x16BC | 1 | 1 | 0 |  |
| 0x16BD | 1 | 1 | 0 |  |
| 0x16BE | 1 | 1 | 0 |  |
| 0x16BF | 1 | 1 | 0 |  |
| 0x16C0 | 1 | 1 | 0 |  |
| 0x16C1 | 1 | 1 | 0 |  |
| 0x16C2 | 1 | 1 | 0 |  |
| 0x16C3 | 1 | 1 | 0 |  |
| 0x16C4 | 1 | 1 | 0 |  |
| 0x16C5 | 1 | 1 | 0 |  |
| 0x16C6 | 1 | 1 | 0 |  |
| 0x16C7 | 1 | 1 | 0 |  |
| 0x16C8 | 1 | 1 | 0 |  |
| 0x16C9 | 1 | 1 | 0 |  |
| 0x16CA | 1 | 1 | 0 |  |
| 0x16CB | 1 | 1 | 0 |  |
| 0x16CC | 1 | 1 | 0 |  |
| 0x16CD | 1 | 1 | 0 |  |
| 0x16D0 | 4 | 1 | 0 |  |
| 0x16D4 | 4 | 1 | 0 |  |
| 0x16D8 | 4 | 1 | 0 |  |
| 0x16DC | 4 | 1 | 0 |  |
| 0x16E0 | 4 | 1 | 0 |  |
| 0x16E4 | 1 | 1 | 0 |  |
| 0x16E5 | 1 | 1 | 0 |  |
| 0x16E6 | 1 | 1 | 0 |  |
| 0x16E8 | 4 | 1 | 0 | 0x4479C000 |
| 0x16EC | 4 | 1 | 0 |  |
| 0x16F0 | 4 | 1 | 0 |  |
| 0x16F4 | 4 | 1 | 0 |  |
| 0x16F8 | 4 | 1 | 0 | 0x9 |
| 0x16FC | 4 | 1 | 0 |  |
| 0x1700 | 1 | 1 | 0 |  |
| 0x1701 | 1 | 1 | 0 |  |
| 0x1702 | 1 | 1 | 0 |  |
| 0x1703 | 1 | 1 | 0 |  |
| 0x1704 | 1 | 1 | 0 | 0x1 |
| 0x1705 | 1 | 1 | 0 |  |
| 0x1706 | 1 | 1 | 0 |  |
| 0x1707 | 1 | 1 | 0 | 0x14 |
| 0x1708 | 1 | 1 | 0 |  |
| 0x170C | 4 | 1 | 0 |  |
| 0x1710 | 4 | 1 | 0 | 0x40 |
| 0x1714 | 1 | 1 | 0 |  |
| 0x1760 | 1 | 1 | 0 |  |
| 0x1761 | 1 | 1 | 0 |  |
| 0x1762 | 1 | 1 | 0 |  |
| 0x1763 | 1 | 1 | 0 |  |
| 0x1764 | 1 | 1 | 0 |  |
| 0x1765 | 1 | 1 | 0 |  |
| 0x1766 | 1 | 1 | 0 | 0x1 |
| 0x1767 | 1 | 1 | 0 |  |
| 0x1768 | 1 | 1 | 0 |  |
| 0x1769 | 1 | 1 | 0 |  |
| 0x176A | 1 | 1 | 0 |  |
| 0x1780 | 4 | 1 | 0 |  |
| 0x1784 | 4 | 3 | 0 | 0x7E4638 |
| 0x1788 | 4 | 4 | 2 |  |
| 0x178C | 4 | 3 | 0 |  |
| 0x1790 | 1 | 2 | 0 | 0x1 |
| 0x1791 | 1 | 4 | 0 | 0x1 |

