# 编造审计清单（fabrication-audit.md）

纪律：凡无证据链的"看起来像"实现 = 编造。代码内以 `【编造-待逆向】`/`【半 RE】`
标记，本文件是总账。状态：已删 / 已标记待重构。

## 已删除（编造行为，撤除）

| 点 | 原编造内容 | 撤除原因 |
|---|---|---|
| 菜单悬停动画 | SDBTNANM 17 帧升起动画按 [AnimTest] Rate=5 播 | [AnimTest] 是**测试条目**，不是按钮交互规范；用户实证悬停播动画是错的 |
| 菜单悬停音效 | 悬停起始播 0x74C8E6F2（"点击音"） | 0x74C8E6F2 的身份（UI 点击音）是按时长猜的；Choice1.AUD 才是 HighlightSound 语义且本体在未破解容器 |
| 菜单按钮双帧切换 | 青铜板帧 0/1 当正常/高亮 | 帧序语义是视觉猜测 |

## 已标记待重构（保留但显式标记）

| 点 | 现状 | 缺的逆向证据 | 任务 |
|---|---|---|---|
| 菜单背景 | 0x2FB23764（视觉工具认的"尤里图"） | GraphicMenu 的菜单 INI 数据源（节名 0x889f64 运行期构造） | 找到 INI → 读 Background 真值 |
| LOGO 叠层 | LOGO.PCX 全屏同比例叠加 | 'Logo' 键属 0x7681E0（战役选图屏），不是主菜单 | 主菜单 Logo 真源与坐标 |
| 菜单按钮图形 | SDBTNANM 帧 0（尺寸相似推断） | dialog 226 控件 → 图形资产的证据链 | GDlgSupp 一族按钮 owner-draw 链 |
| 按钮兜底板 | 0x1BB65278 青铜板（尺寸相似推断） | 同上 | 同上 |
| 战场音乐触发 | 进战场立即挑 Normal=yes 曲循环 | ThemeClass 触发时机/选曲策略（@0x752800 一族） | 逆向 ThemeClass::Queue_Song |
| UI 颜色组 | kUiBackdrop/kUiSelect/kUiRadarDot… 自配色 | 原版色值（RadarClass/TacticalClass 绘制函数） | 逆向绘制函数取色 |
| 边缘滚屏速度 | 600px/s | ScrollRate 机制（RA2MD.INI ScrollRate=0 档位映射） | 逆向滚动速度表 |
| 步兵行走速率 | 1/3 格/相位 | FootClass 步进计时 | 逆向 walk 帧推进 |
| 体素比例 1.25 | 48 vox/格换算（半 RE） | 0x7586F0 入口的实际乘数 | 逆向投影链常数 |
| 朝向 32 档 | =HVA 帧数（推断） | 原版无烘焙、动态渲染 | 确认量化粒度是否影响观感 |
| Skirmish 默认图 | Arena.mmx（本地存在即选） | 原版是 BATTLEMD 选图 UI | 逆向 Skirmish 流程 |
| 起手资金从 rules | StartCredits | 属 RE（rules.ini 实测） | — |

## 确认为 RE（有证据链，非编造）

- AUD 格式/解码器（0x40AA70/0x40ACD0 + 表 0x816558/0x816518，28353 样本零差异）
- 光向量 normalize(-1,-1,2)（0x754C00 方位 + 0x52BDD1 仰角 55°）
- 光照/阴影数学（0x7586F0 逐点验算）
- TMP 行序 / SHP 格式 / MIX 三格式（含 THEME.MIX 老格式，逐字节验算）
- 步兵 SHP 帧序 [站8 + 走8×6]（宽度签名 + 蒙太奇 + 帧高突变三重验证）
- 侧栏矩形链（0x72fc60）与雷达内缩（RadarClass::One_Time 0x652CF0）
- 触发器 TEvent/TAction 表与分发（Dispatch_TAction 对齐 0x6DFDEC 跳表）
- 窗口客户区修正（AdjustWindowRect——工程正确性，非 RE 项）
