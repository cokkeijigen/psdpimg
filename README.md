# psdpimg
`krkrz pimg`与`psd`互转工具
## 用法

```pwsh
psdpimg -pimg <input.psd>  [output.pimg] [import options]
psdpimg -psd  <input.pimg> [output.psd]
```

- `-pimg` = PSD → pimg，`-psd` = pimg → PSD，两个同时给视为冲突。
- 都不给时按输入魔数判断方向（`8BPS` / `PSB\0`）。开关与输入魔数不符时报
  `the switch does not match the input signature`。
- 第 1 个位置参数是输入，第 2 个是输出。输出省略时取输入路径、换扩展名。
- `--` 之后一律按位置参数处理；`-h` / `--help` 打印用法。
- 成功时 stdout 输出一行：`<输出路径>  <图层数> layers  <字节数> bytes`。
- 退出码：`0` 成功、`1` 转换失败、`2` 参数错误。

### import options

只在 `-pimg` 方向有效，**全部可选**，一个都不给时行为与不带这节完全一致（字节回归可验证）。

| 开关 | 命中条件 |
| --- | --- |
| `-type=ui` | 预设，等价于 `-ignore-include=rect -ignore-prefix=# -ignoregroup-prefix=# -trim -crop` |
| `-ignore=<文本>` | 层名**完全等于**文本 |
| `-ignore-prefix=<文本>` | 层名以文本**开头** |
| `-ignore-suffix=<文本>` | 层名以文本**结尾** |
| `-ignore-include=<文本>` | 层名**包含**文本 |
| `-ignoregroup=<文本>` | 某一层祖先组名完全等于文本 |
| `-ignoregroup-prefix=<文本>` | 某一层祖先组名以文本开头 |
| `-ignoregroup-suffix=<文本>` | 某一层祖先组名以文本结尾 |
| `-ignoregroup-include=<文本>` | 某一层祖先组名包含文本 |
| `-ignore-list=<文件>` | 文件里**一行一条正则**，命中层名的都算（层名版） |
| `-ignoregroup-list=<文件>` | 同上，但匹配**祖先组名** |
| `-trim` | 去掉层名首尾空白 |
| `-crop` | 把保留层裁到非透明包围盒，同步修正 `left`/`top`/`width`/`height` |

文本是**字面量**：不是正则、没有转义、**不按逗号切分**，区分大小写，也不做 Unicode 归一化。
要加多条规则就重复给同一个开关：

```pwsh
psdpimg -pimg a.psd b.pimg -ignore-prefix=# -ignore-prefix=; -ignore-prefix=,
```

上面三条前缀里就有 `#`、`;`、`,` 本身，都不需要转义或引号（shell 自己需要的引号另算）。

#### 规则文件（`-ignore-list=` / `-ignoregroup-list=`）

规则多得写不下命令行时用文件，**一行一条 ECMAScript 正则**，子串匹配（`regex_search`）：

```text
^#
^[0-9]+$
rect
```

- 空行忽略；没有注释语法（`#` 本身是厂商组名前缀，当注释会误伤）；
- BOM、CRLF、LF 都吃；编码按 **UTF-8**，日文层名直接写即可；
- 文件不存在、某行正则非法都会**直接失败**并报出行号，退出码 `2`：
  `invalid pattern in rules.txt on line 3: [0-9`；
- 文件规则与命令行开关是**叠加**关系，两者可以同时给。

文件里的一行按正则解释，所以要匹配字面量就得转义：匹配层名 `a.b` 写 `a\.b`，
匹配 `(x)` 写 `\(x\)`。这是它和命令行开关（纯字面量）的唯一差别。

#### 「不写像素」是什么意思

层**仍然在层表里**，`left`/`top`/`width`/`height` 保持 PSD 里的原值，只是资源表里没有它的 `.tlg`，
图层记录的 `layer_id` 写成 `-1`（`psdconv` 里 `無視レイヤ` 的标记）。

原因：`uipsd` 里这类层是**纯粹的定位框 / 挂载点**（`#` 组、`rect` 层），引擎按名字取那个矩形来摆 UI，
从来不用它的图。厂商 pimg 里它们本来就没有像素，所以照抄这套规则才能产出和厂商件一致的结果，
体积也小得多（`file.psd`：3636857 → 只加 `-ignore-include=rect` 3636851 → `-type=ui` 2487679 字节）。

#### 层名过滤与组名过滤的区别

```text
#base                ← 组
    bg_rect          ← 层，名字含 rect
    common_bg
button               ← 组
    icon
```

| 参数 | 结果 |
| --- | --- |
| `-ignore-include=rect` | 只有 `bg_rect` 不写像素；`common_bg`、`icon` 照常 |
| `-ignoregroup-prefix=#` | `#base` 下的 `bg_rect`、`common_bg` 都不写像素；`icon` 照常（它的组叫 `button`） |
| 两组同时给 | 叠加 |

简单说：`-ignore*` 只看这一层自己的名字，`-ignoregroup*` 只看它挂在哪些组下面（从直接父组一路往上，
任一层命中即算命中）。两组可以同时给，也可以只用其中一组。

`-trim` 会**影响匹配**：它先把名字首尾空白去掉，再拿结果去比。`dialog.psd` 里那几层叫 `box `（带尾随空格），
所以 `-ignore-suffix=box` 单独用不命中，加上 `-trim` 才命中。

这组行为对齐 krkrz 官方的 `psdconv -type=ui`（`tools/build/Build.mak:1683` 就是这么调的），
其中组名规则在 `psdconv` 里是 `defaultParams` 的默认值，与 `-type` 无关。
另注：`trim` 只处理**名字**，与裁边无关；裁边是 `fix()` 里对保留层无条件执行的 `trimLayer`。

未复刻的部分：`cropLayer` 的画布归一化、`ooze` 外扩、`clearLayerAlpha`（把 alpha=0 处的
RGB 清零，只影响压缩率不影响画面）。所以产物与 `psdconv` 的差距只剩这三项。

## 构建

```pwsh
git clone https://github.com/cokkeijigen/psdpimg.git
cd .\psdpimg
.\build.bat
```