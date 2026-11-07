<h1 align="center">psdpimg</h1>

<p align="center"><a href="#english">English</a> | <a href="#简体中文">简体中文</a></p>

## English

A `krkrz pimg` ⇄ `psd` conversion tool.

- `-pimg`: PSD → pimg (a PSB container, layer bitmaps encoded as TLG5)
- `-psd`: pimg → layered PSD

### Usage

#### Single file

```pwsh
psdpimg.exe -pimg <input.psd>  [output.pimg] [import options]
psdpimg.exe -psd  <input.pimg> [output.psd]  [export options]
```

- `-pimg` is PSD → pimg, `-psd` is pimg → PSD. Giving both at once is a conflict.
- For a single file either switch may be omitted; the direction is then taken from the input
  magic (`8BPS` = PSD, `PSB\0` = pimg). A switch that disagrees with the magic fails with
  `the direction does not match the input signature`.
- The first positional argument is the input, the second one the output. When the output is
  omitted, the input path is reused with only the extension changed.
- Everything after `--` is treated as positional; `-h` / `--help` prints the usage.
- **`psdpimg` takes exactly one input**: the second positional is the output (an output file
  for a single file, an output directory for a directory), and a third one is rejected with
  `too many arguments`. To convert several inputs at once, use the directory mode below or
  rename the executable to `psd2pimg` / `pimg2psd`.

#### Directory

```pwsh
psdpimg.exe -pimg <directory>  [output directory] [import options]
psdpimg.exe -psd  <directory>  [output directory]  [export options]
```

- A directory **requires** a direction switch: it may hold both `.psd` and `.pimg`, so the
  direction cannot be guessed. Otherwise `Argument error (a directory needs -pimg or -psd)`,
  exit code `2`.
- No recursion. `-pimg` picks `*.psd` only, `-psd` picks `*.pimg` only; the extension is
  matched case-insensitively.
- When the output directory is omitted, results go to a **sibling** directory named after the
  input directory plus a suffix: `art\ui` → `art\ui_pimg` (`art\ui_psd` for `-psd`).
  A trailing separator is dropped, so `art\ui\` also yields `art\ui_pimg`.
- A missing output directory is created, including intermediate levels.
- A directory with no match at all → `Scan failed (no matching file in the input directory)`,
  exit code `1`.

#### Renaming to psd2pimg / pimg2psd

Rename the executable and the direction comes from the **file name**; `-pimg` / `-psd` are no
longer needed:

```pwsh
psd2pimg.exe a.psd b.psd art\ui        -> a.pimg b.pimg art\ui_pimg\*.pimg
pimg2psd.exe a.pimg art\ui             -> a.psd  art\ui_psd\*.psd
```

- The name is read from the process module path (`PEB → Ldr → InLoadOrderModuleList`,
  `FullDllName`), never from `argv[0]`, so shortcuts or `start` cannot change the decision.
  The extension is stripped and the stem is compared case-insensitively against `psd2pimg` /
  `pimg2psd`.
- **Every positional argument is an input**; no output path is accepted.
- Files and directories may be **mixed**: a single file is written next to itself
  (`a.psd` → `a.pimg`), a directory goes to `<name>_pimg` / `<name>_psd`.
- `import options` / `export options` still apply.

#### Log output

Every conversion prints a block of labelled lines. `layers` counts layer records (including
the group open/close entries), `bytes` is the size written:

```text
Convert succeeded:
[layers: 50, bytes: 4927889]
-  in: D:\work\opt_1system.pimg
- out: D:\work\opt_1system.psd
```

On failure the reason and the failing side are given:

```text
Convert failed (cannot open the input):
- in: D:\work\a.psd
```

The reason is one of `cannot open the input`, `cannot read the input`, `unsupported input`,
`the direction does not match the input signature`, `cannot create the output`,
`cannot write the output`.

A batch (more than one task) ends with one extra block:

```text
Batch finished:
- converted: 48
```

Batching is **fail-fast**: the first failure aborts the whole run with exit code `1`; files
already written are not rolled back — just rerun it.

Argument problems print the reason first, then the usage, exit code `2`:

```text
Argument error (no input file):
usage:
  ...
```

Reasons: `no input file`, `unknown switch`, `conflicting switches`, `too many arguments`,
`invalid option value`, `a directory needs -pimg or -psd`.

An uncaught exception:

```text
Convert failed:
<exception text>
```

Before exiting, the program also prints a one-line version banner and **waits for any key**
(redirecting stdin makes it return immediately).

#### Exit codes

| Code | Meaning |
| --- | --- |
| `0` | everything succeeded |
| `1` | conversion / scan failure (including an empty directory and a magic mismatch) |
| `2` | argument error |

### import options

Valid in the `-pimg` direction only. **All optional**: with none of them the output is
byte-identical to a run without this whole section (verified by regression).

| Switch | Match condition |
| --- | --- |
| `-type=ui` | preset, equal to `-ignore-include=rect -ignore-prefix=# -ignoregroup-prefix=# -trim -crop` |
| `-ignore=<text>` | layer name is **exactly** the text |
| `-ignore-prefix=<text>` | layer name **starts with** the text |
| `-ignore-suffix=<text>` | layer name **ends with** the text |
| `-ignore-include=<text>` | layer name **contains** the text |
| `-ignoregroup=<text>` | some ancestor group name is exactly the text |
| `-ignoregroup-prefix=<text>` | some ancestor group name starts with the text |
| `-ignoregroup-suffix=<text>` | some ancestor group name ends with the text |
| `-ignoregroup-include=<text>` | some ancestor group name contains the text |
| `-ignore-list=<file>` | one regex per line of the file, matched against layer names |
| `-ignoregroup-list=<file>` | same, but matched against **ancestor group** names |
| `-trim` | strip leading and trailing blanks from layer names |
| `-crop` | crop every kept layer to its non-transparent bounds, fixing `left`/`top`/`width`/`height` |

The text is a **literal**: not a regex, no escapes, **not split on commas**, case-sensitive,
no Unicode normalisation. To add more rules, repeat the same switch:

```pwsh
psdpimg -pimg a.psd b.pimg -ignore-prefix=# -ignore-prefix=; -ignore-prefix=,
```

Those three prefixes are `#`, `;` and `,` themselves — no escaping or quoting needed
(quoting required by your shell is a separate matter).

#### Rule files (`-ignore-list=` / `-ignoregroup-list=`)

When the rules do not fit on the command line, put them in a file, **one ECMAScript regex per
line**, substring match (`regex_search`):

```text
^#
^[0-9]+$
rect
```

- Blank lines are skipped; there is no comment syntax (`#` is itself a vendor group prefix, so
  treating it as a comment would drop real layers).
- BOM, CRLF and LF are all accepted; the file is read as **UTF-8**, Japanese layer names can be
  written as-is.
- A missing file or an invalid pattern fails the run and reports the line number, exit code `2`:
  `invalid pattern in rules.txt on line 3: [0-9`.
- File rules and command-line switches are **cumulative**; both may be given.

Each line is a regex, so matching a literal requires escaping: layer `a.b` needs `a\.b`,
`(x)` needs `\(x\)`. That is the only difference from the command-line switches (pure
literals).

#### What "no bitmap" means

The layer **stays in the layer table**; `left`/`top`/`width`/`height` keep their PSD values,
only the resource table has no `.tlg` for it, and the record's `layer_id` becomes `-1`
(`psdconv`'s `無視レイヤ` marker).

Why: inside `uipsd` such layers are **pure positioning boxes / mount points** (`#` groups,
`rect` layers). The engine looks the rectangle up by name to place UI and never uses the
bitmap. Vendor pimg files carry no pixels for them, so copying this rule is what makes the
output match the vendor files, and it saves a lot of space (`file.psd`: 3636857 → 3636851
with `-ignore-include=rect` alone → 2487679 bytes with `-type=ui`).

#### Layer-name filters vs group-name filters

```text
#base                ← group
    bg_rect          ← layer, name contains rect
    common_bg
button               ← group
    icon
```

| Option | Result |
| --- | --- |
| `-ignore-include=rect` | only `bg_rect` loses its bitmap; `common_bg` and `icon` unaffected |
| `-ignoregroup-prefix=#` | `bg_rect` and `common_bg` under `#base` lose theirs; `icon` unaffected (its group is `button`) |
| both | cumulative |

In short: `-ignore*` looks at the layer's own name, `-ignoregroup*` looks at the groups it sits
in (walking up from the direct parent; any hit counts). Both families may be used together or
on their own.

`-trim` **affects matching**: names are stripped of surrounding blanks before being compared.
Layers in `dialog.psd` are called `box ` (with a trailing space), so `-ignore-suffix=box` alone
does not match — only with `-trim` does it.

This behaviour mirrors krkrz's `psdconv -type=ui` (`tools/build/Build.mak:1683` invokes it that
way). The group rules are `defaultParams` in `psdconv`, independent of `-type`. Note that
`trim` only touches **names**, it is not about cropping; cropping is the unconditional
`trimLayer` applied to kept layers inside `fix()`.

Not replicated: `cropLayer` canvas normalisation, `ooze` expansion, `clearLayerAlpha` (zeroing
RGB where alpha=0, which affects compression only, not the picture). So those three are the
only remaining differences from `psdconv`.

### export options

Valid in the `-psd` direction only.

| Switch | Effect |
| --- | --- |
| `-expand-markers` | write every sizing layer as its own layer, with `#lyid#-1` appended to the name |

Passing the switch in the `-pimg` direction neither fails nor does anything.

#### The two forms of a positioning (sizing) layer

A sizing layer in a pimg has only `left`/`top`/`width`/`height` and `layer_id = -1`, no bitmap.
PSD cannot express "a rectangle without pixels", so on export it is filled with a translucent
colour as a placeholder (`RGBA = 0, 0, 0, 128`) and written in one of two forms:

**Default (merged)**: the sizing layers of one `##` group are merged back into a **single
`##xxx` positioning layer**, sized to the bounding box of all members, with one translucent
block per member rectangle. This is exactly the shape of the vendor PSD, so a round trip keeps
names and rectangles aligned.

```text
##frame            1359 × 705  @ (276,230)      ← one layer holding several blocks
```

**`-expand-markers` (expanded)**: no merging; one layer per sizing layer, name = original name
+ `#lyid#-1`, filled entirely with the translucent colour.

```text
#base#lyid#-1      1920 × 1080
3#lyid#-1           160 × 165  @ (276,770)
```

Both forms convert back with `-pimg` into the same set of sizing layers (same count, same
geometry, same bitmaps). Use `-expand-markers` when each sizing layer must keep its original id
and the round trip must be strict; use the default when you want a file Photoshop can keep
editing that is structurally identical to the vendor PSD.

Known trade-off: after a default round trip the `layer_id` order of the sizing layers inside a
group may differ from the original pimg (e.g. `0..8` becomes `8..0`). Count and geometry are
unaffected, and the engine only looks rectangles up by name when `layer_id = -1`, never by id,
so runtime behaviour is identical.

Merging has three hard conditions: the group name starts with `##`, every member is a sizing
layer and there are 2–64 of them, and the resulting blocks line up **one-to-one** with the
member rectangles. If any condition fails the group is left unmerged — better a few extra
layers than a lost one.

`#lyid#-1` is this tool's own round-trip marker; official `psdconv` does not know it.

### Building

Requires Visual Studio 2022 (with the C++ x86/x64 toolset), CMake 3.21 or newer, and Ninja.

```pwsh
git clone https://github.com/cokkeijigen/psdpimg.git
.\psdpimg\build.bat
```

`build.bat` locates the newest Visual Studio through `vswhere`, runs
`call VsDevCmd.bat -arch=x86`, then drives the CMake preset. The binary lands in
`.build\<preset>\psdpimg.exe`. The default preset is `windows-msvc-x86-release`; another one
can be named explicitly:

```pwsh
.\psdpimg\build.bat windows-msvc-x86-debug
```

Available presets: `windows-msvc-x86-release` / `-debug` / `-relwithdebinfo`, the matching
`x64` set, and the `windows-clang-*` family.

---

## 简体中文

`krkrz pimg` 与 `psd` 互转工具。

- `-pimg`：PSD → pimg（PSB 容器，图层位图编码为 TLG5）
- `-psd`：pimg → 分层 PSD

### 用法

#### 单文件

```pwsh
psdpimg.exe -pimg <input.psd>  [output.pimg] [import options]
psdpimg.exe -psd  <input.pimg> [output.psd]  [export options]
```

- `-pimg` = PSD → pimg，`-psd` = pimg → PSD，两个同时给视为冲突。
- 单文件时两个开关都可以不给，方向按输入魔数判断（`8BPS` = PSD、`PSB\0` = pimg）。
  开关与魔数不符时报 `the direction does not match the input signature`。
- 第 1 个位置参数是输入，第 2 个是输出；输出省略时取输入路径、只换扩展名。
- `--` 之后一律按位置参数处理；`-h` / `--help` 打印用法。
- **`psdpimg` 只接受一个输入**：第 2 个位置参数是输出（单文件时是输出文件，目录时是输出目录），
  再多给一个就报 `too many arguments`。想一次转多个请用下面的目录模式，
  或者把可执行文件改名成 `psd2pimg` / `pimg2psd`。

#### 目录

```pwsh
psdpimg.exe -pimg <directory>  [output directory] [import options]
psdpimg.exe -psd  <directory>  [output directory]  [export options]
```

- 目录**必须**给方向开关：目录里可能同时有 `.psd` 和 `.pimg`，猜不出来。
  否则 `Argument error (a directory needs -pimg or -psd)`，退出码 `2`。
- 不递归。`-pimg` 只取 `*.psd`，`-psd` 只取 `*.pimg`，扩展名大小写不敏感。
- 输出目录省略时，落在输入目录的**同级**、名字加后缀：`art\ui` → `art\ui_pimg`（`-psd` 则是 `art\ui_psd`）。
  末尾的分隔符会被吃掉，`art\ui\` 一样得到 `art\ui_pimg`。
- 输出目录不存在会自动创建（含多级）。
- 目录里一个匹配都没有 → `Scan failed (no matching file in the input directory)`，退出码 `1`。

#### 改名成 psd2pimg / pimg2psd

把可执行文件改名，方向就由**文件名**决定，不再需要 `-pimg` / `-psd`：

```pwsh
psd2pimg.exe a.psd b.psd art\ui        -> a.pimg b.pimg art\ui_pimg\*.pimg
pimg2psd.exe a.pimg art\ui             -> a.psd  art\ui_psd\*.psd
```

- 名字取自进程模块路径（`PEB → Ldr → InLoadOrderModuleList` 的 `FullDllName`），不是 `argv[0]`，
  所以快捷方式、`start` 之类改不了判断结果。去掉扩展名后不区分大小写比较 `psd2pimg` / `pimg2psd`。
- **所有位置参数都是输入**，不再接受输出路径。
- 文件和目录可以**混着**给：单个文件就地生成（`a.psd` → `a.pimg`，与输入同目录），
  目录则产出到 `<目录名>_pimg` / `<目录名>_psd`。
- `import options` / `export options` 照样生效。

#### 输出的日志

每次转换打一组标签行，`layers` 是图层记录的条数（含组的开/闭条目），`bytes` 是写出的字节数：

```text
Convert succeeded:
[layers: 50, bytes: 4927889]
-  in: D:\work\opt_1system.pimg
- out: D:\work\opt_1system.psd
```

失败时给出原因和出错的那一侧：

```text
Convert failed (cannot open the input):
- in: D:\work\a.psd
```

原因是这几个之一：`cannot open the input`、`cannot read the input`、`unsupported input`、
`the direction does not match the input signature`、`cannot create the output`、`cannot write the output`。

批量时（任务数大于 1）末尾补一行：

```text
Batch finished:
- converted: 48
```

批处理是 **fail-fast**：某个文件失败就整批中止、退出码 `1`，已经写出的文件不回滚，重跑即可。

参数问题会先打原因、再打用法，退出码 `2`：

```text
Argument error (no input file):
usage:
  ...
```

原因：`no input file`、`unknown switch`、`conflicting switches`、`too many arguments`、
`invalid option value`、`a directory needs -pimg or -psd`。

未捕获的异常：

```text
Convert failed:
<异常文本>
```

程序结束前还会打印一行版本横幅，并**等待任意按键**才退出（stdin 被重定向时不会卡住）。

#### 退出码

| 码 | 含义 |
| --- | --- |
| `0` | 全部成功 |
| `1` | 转换 / 扫描失败（含目录无匹配、方向与魔数不符） |
| `2` | 参数错误 |

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

### export options

只在 `-psd` 方向有效。

| 开关 | 作用 |
| --- | --- |
| `-expand-markers` | 每个尺寸层单独写成一层，层名追加 `#lyid#-1` |

在 `-pimg` 方向给这个开关不会报错，也不会有任何效果。

#### 定位层（尺寸层）的两种形态

pimg 里的尺寸层只有 `left`/`top`/`width`/`height` 和 `layer_id = -1`，没有位图。PSD 表达不了
「有矩形没像素」，所以导出时给它填一块半透明色当占位（`RGBA = 0, 0, 0, 128`），并按两种形态之一写出：

**默认（合并）**：同一 `##` 组里的尺寸层合并回**单个 `##xxx` 定位层**，尺寸取所有成员的外接矩形，
每个成员原来的矩形在层内填一块半透明色 —— 这正是厂商 PSD 的输入形态，往返后层名、层框都对得上。

```text
##frame            1359 × 705  @ (276,230)      ← 一层，内含多个色块
```

**`-expand-markers`（展开）**：不合并，一层对一个尺寸层，层名 = 原名 + `#lyid#-1`，整层填满半透明色。

```text
#base#lyid#-1      1920 × 1080
3#lyid#-1           160 × 165  @ (276,770)
```

两种形态喂回 `-pimg` 都能还原成同样的尺寸层集合（数量、几何、位图一致）。要保留每个尺寸层各自的原始 ID、
做严格往返就用 `-expand-markers`；要一份能直接用 Photoshop 接着改、和厂商 PSD 同构的文件就用默认。

已知取舍：默认形态往返之后，组内各尺寸层的 `layer_id` 顺序可能与原 pimg 不同（例如 `0..8` 变成 `8..0`），
数量和几何不受影响；引擎在 `layer_id = -1` 时只按名字取矩形，不读 ID，所以运行结果一样。

合并有三条硬条件：组名以 `##` 开头、组内成员全部是尺寸层且数量在 2–64 之间、
合并出来的色块与各成员矩形**逐个对得上**。任一不满足就退回不合并 —— 宁可多出几层，也不丢层。

`#lyid#-1` 是本工具自己的往返标记，官方 `psdconv` 不认识它。

### 构建

需要 Visual Studio 2022（含 C++ x86/x64 工具集）、CMake 3.21 以上、Ninja。

```pwsh
git clone https://github.com/cokkeijigen/psdpimg.git
.\psdpimg\build.bat
```

`build.bat` 会用 `vswhere` 找最新的 Visual Studio、`call VsDevCmd.bat -arch=x86`，然后跑 CMake preset。
产物在 `.build\<preset>\psdpimg.exe`。默认 preset 是 `windows-msvc-x86-release`，也可以显式指定：

```pwsh
.\psdpimg\build.bat windows-msvc-x86-debug
```

已有 preset：`windows-msvc-x86-release` / `-debug` / `-relwithdebinfo`，以及对应的 `x64` 与
`windows-clang-*` 系列。
