# 对象模型：从三张表铺成能编译的结构体

生成工具 `tools/layout.py`，产物 `src/re/ObjectModel.h`（全量数据 `db/layout.json`）。基线镜像 D:\westwood\RA2YR\gamemd.exe。

## 1. 这一步解决什么

前三轮的产物都是**查询表**：

| 表 | 内容 | 谁生成 |
|---|---|---|
| `src/re/FieldOffsets.h` | (类, 偏移) -> 宽度、被写次数 | `tools/fieldscan.py` |
| `src/re/FieldNames.h` | (类, 偏移) -> INI 键名 | `tools/fieldname.py` |
| `src/re/ObjectSizes.h` | (类) -> sizeof | `tools/sizeofscan.py` |

查询表能回答"0x610 是什么"，但写不出 `obj.COST = 1000`，也说不出 `sizeof(TechnoTypeClass)`。
把 (偏移, 宽度, 名字) 按 RTTI 的继承链铺成**连续内存布局**之后，对象模型才第一次能被代码直接用上：

* 每个已知字段落在它**实测出来的偏移**上，由 `static_assert(offsetof(...) == 偏移)` 钉死；
* 字段之间的空隙是写出来的 `u8 _pad_XXXX[n]`，不是"看不见"；
* 类的末端是已知数据的直接结果，也就是 `sizeof` 的**下界**（新数据）；
* 没名字但有证据的偏移是 `_unk_XXXX`，保留着"这里确实有一个 N 字节字段"这条信息。

## 2. 怎么铺的

1. **字段集合** = 构造函数写过的偏移（`fields.json`）∪ Read_INI 读写过的偏移（`fieldnames.json`）。两边都出现的偏移宽度必须一致，不一致直接报错终止（本轮实跑：0 处不一致）。
2. **继承链**取自 RTTI 的 `bases[0]`：`AbstractClass -> AbstractTypeClass -> ObjectTypeClass -> {TechnoTypeClass -> {Building,Unit,Infantry}, IsometricTileTypeClass}`。
   子类只声明"偏移 ≥ 父类末端"的部分，父类区一律算父类的。
3. **不满就填**：字段之间用 `_pad_XXXX[n]` 补到下一个字段的偏移，使每个已知字段恰好落在它的偏移上。
4. **`#pragma pack(push,1)`** 是故意的：二进制里的布局是事实，不能让编译器的对齐规则改写它。pack(1) 下 MSVC 的 `offsetof`/`sizeof` 精确等于铺出来的偏移。这一点不是推测 —— `tools/_probe_offsetof.cpp` 在本机 MSVC 14.x 上实测过（4 层继承 + 混合宽度 + pack(1)，`sizeof` 与 `offsetof` 全部对上）。

## 3. 本轮算出来的数

| 类 | 父类 | 本体起点 | 末端 | 已知字段 | 有名字 | 填充字节 | 已覆盖 |
|---|---|---|---|---|---|---|---|
| `AbstractClass` | （根） | 0x0 | 0x21 | 9 | 0 | 3 | 90.9% |
| `AbstractTypeClass` | `AbstractClass` | 0x21 | 0x65 | 4 | 0 | 61 | 10.3% |
| `ObjectTypeClass` | `AbstractTypeClass` | 0x65 | 0x294 | 65 | 15 | 392 | 29.9% |
| `IsometricTileTypeClass` | `ObjectTypeClass` | 0x294 | 0x30C | 33 | 0 | 18 | 85.0% |
| `TechnoTypeClass` | `ObjectTypeClass` | 0x294 | 0xDF4 | 460 | 178 | 1490 | 48.8% |
| `BuildingTypeClass` | `TechnoTypeClass` | 0xDF4 | 0x1792 | 311 | 170 | 1640 | 33.4% |
| `InfantryTypeClass` | `TechnoTypeClass` | 0xDF4 | 0xECC | 65 | 22 | 34 | 84.3% |
| `UnitTypeClass` | `TechnoTypeClass` | 0xDF4 | 0xE5F | 40 | 10 | 4 | 96.3% |
| **合计** | | | | **987** | **395** | **3642** | |

**末端是 `sizeof` 的下界，不是 `sizeof`。** 它是"最后一个有证据的字段结束在哪"；二进制里完全可能再往后还有我们没看到的字节（对齐填充、只在别的路径赋值的字段）。能说出口的是：`sizeof(TechnoTypeClass) >= 0xDF4`。

**覆盖率的含义要说清**：`已覆盖` = 有证据的字节 / 本体字节。`UnitTypeClass` 只有 3.5% 不代表它只有 130 字节有内容，只代表**构造函数和 Read_INI 这两条通道**在这些偏移上留下了痕迹。剩下的是"暂时没有证据"，不是"空的"。

**父类末端 → 子类第一个自有字段之间的缝**（表里最后一列，`layout.json` 的 `align_gap`）：

| 类 | 父类末端 | 第一个自有字段 | 缝（字节） |
|---|---|---|---|
| `AbstractTypeClass` | 0x21 | 0x24 | 3 |
| `ObjectTypeClass` | 0x65 | 0x98 | 51 |
| `IsometricTileTypeClass` | 0x294 | 0x294 | 0 |
| `TechnoTypeClass` | 0x294 | 0x294 | 0 |
| `BuildingTypeClass` | 0xDF4 | 0xDF8 | 4 |
| `InfantryTypeClass` | 0xDF4 | 0xDF8 | 4 |
| `UnitTypeClass` | 0xDF4 | 0xDF8 | 4 |

缝 = 0 的那几个类说明父类末端和子类第一个自有字段**严丝合缝**；缝 > 0 说明中间还有我们没看到的字节（对齐填充，或者只在别处赋值的字段）。两种都是数据，不是错误。

## 4. 独立对账（不是自说自话）

### 4.1 继承边界精确吻合 —— 两套独立证据撞在一起

`ObjectTypeClass` 的末端（它自己构造函数写过的最远字段）是 **0x294**；`TechnoTypeClass` 的**自有**命名字段里最小的偏移也正好是 **0x294**。

两组数据来源完全不同：前者扫 `ObjectTypeClass` 的构造函数，后者扫 `TechnoTypeClass` 的 `Read_INI`（读 INI 键的路径）。RTTI 只说"TechnoTypeClass 继承 ObjectTypeClass"，**不给出**边界在哪 —— 边界是这两条通道各自算出来又正好对上的。

### 4.2 子类在父类区留下的痕迹，祖先必须已经认识

子类构造函数会内联父类的初始化，子类 `Read_INI` 也会去写父类的字段（例如 `IsometricTileTypeClass` 的 15 个键**全部**落在 `ObjectTypeClass` 的 0x9C~0x238 里，一个自有字段都没有）。这些偏移必须能在祖先链的字段表里找到，且名字一致 —— 否则就说明要么祖先漏了字段，要么这条记录被误归到了子类。

本轮结果：

* 无异常：子类在祖先区写过或命名过的偏移，祖先链全都认识，名字也一致。
* BuildingTypeClass 的构造函数写了祖先区的 0x0D2E, 0x0D35, 0x0D36, 0x0D38, 0x0D3B, 0x0D96, 0x0D97：祖先的**构造函数**通道没有这些字节，名字来自 TechnoTypeClass 的 Read_INI 通道
* InfantryTypeClass 的构造函数写了祖先区的 0x0D2E, 0x0D35, 0x0D36, 0x0D38, 0x0D3B, 0x0D96, 0x0D97：祖先的**构造函数**通道没有这些字节，名字来自 TechnoTypeClass 的 Read_INI 通道
* UnitTypeClass 的构造函数写了祖先区的 0x0D2E, 0x0D35, 0x0D36, 0x0D38, 0x0D3B, 0x0D96, 0x0D97：祖先的**构造函数**通道没有这些字节，名字来自 TechnoTypeClass 的 Read_INI 通道
* UnitTypeClass 的 Read_INI 读的 1 个键落在祖先本体上（字段归祖先）：SPEEDTYPE@0x067C→TechnoTypeClass

### 4.3 同一偏移两套宽度必须一致

构造函数用 `mov dword` 写、Read_INI 用 `ReadInteger` 收 —— 同一偏移上两边的宽度必须相等。本轮 0 处不一致（不一致会在 `tools/layout.py` 里直接终止）。

### 4.4 类内不得有重叠字段

同一类的字段集合里任意两个字段的字节区间不得相交；重叠说明至少有一条写记录被归错了（例如把一个 4 字节写读成两个 1 字节）。本轮 0 处重叠。

### 4.5 编译期 + 运行期两道闸门

`src/re/ObjectModel.h` 里每条字段一条 `static_assert`；`ra2core` 冒烟测试里的 `Model_Check()` 再在运行期对一遍（`FieldNames.h` 的每条命名字段必须能在模型里找到同名成员、偏移与宽度一致；反向 `ModelOf("不存在的类")` 必须查不到）。

## 5. 明确没做到的

* **`static_assert` 不是"偏移的验证"**。偏移来自反汇编；`static_assert` 保证的是"这份 C++ 模型不会悄悄漂移"。把它当取证读就错了 —— 它的价值在回归。
* **`offsetof` 用在非 standard-layout 类型上是条件支持的**（本例每一层基类都有数据成员，整个链不是 standard-layout）。MSVC 对单继承非虚基类给出正确结果，且这里的结果被 `static_assert` 逐条钉过 —— 是"在目标编译器上已验证"，不是"标准保证"。换编译器要重跑这一层。
* **只覆盖 8 个类**（4 层继承链上的 8 个 TypeClass）。`BuildingClass`/`CellClass`/`HouseClass` 这些静态对象池里的类`sizeof` 拿不到（不是 `operator new` 出来的），字段证据也还没挖，本轮不进模型。
* **虚拟函数表不在模型里**。结构体里没有虚函数、也没有 vptr 声明；虚表信息在 `src/re/ClassHierarchy.h`（按槽位）与 `docs/vtables.md`。
* **没有语义**。`_unk_XXXX` 是什么、`_pad_XXXX` 里有没有东西，本模型一律不知道。名字只表示"这个偏移是从这个 INI 键读出来的"。
* **成员名用的就是 INI 键名**（原样大写），没有换成 CamelCase 的"引擎内部名字"。键名有证据可查（反汇编里的字符串字面量），编出来的内部名字没有 —— 少一个可以错的地方。
* **父类末端与子类第一个自有字段之间可能有对齐缝隙**。本模型把它算成子类本体开头的 `_pad_`；真实的 `sizeof(父类)` 可能比其末端大 1~3 字节。上面表里的"末端"因此是**下界**而不是精确 `sizeof`。
