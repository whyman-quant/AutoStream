## raw_data_reader：本地逐笔行情文件读取

面向磁盘上的 **`.npq` 等逐笔级数据**：纯 C++ 直读、经嵌入 Python（`my.data`）读取，或编译期二选一/并存（`READER_BACKEND`）。结构体定义在 `raw_data_types/`（本仓库内由旧目录 `include/` 重命名并整理而来）。本目录下自维护的 `.cc`/`.h` 说明注释统一为行注释 `//`。

源码导读：**`backend_config.h`**（编译宏 `READER_BACKEND_*`、`BackendTarget`、`both` 下内联 `quote_reader`）→ `data_path_config.*`（根路径、`MYDATA_BUSINESS_TYPE`、**`local_path`**）→ `ticks_data.h` / `ticks_data.cc`（含 `SetMydataPath` / `Get*` 调度与路径/MC）→ `ticks_data_io_*`（`ViaCpp` / `ViaPython` 直读）→ `merge_sort.*`。**演示可执行文件不在本目录**，见 **`../examples/demo_ticks.cc`**。

## 阅读导航

- [使用者说明](#使用者说明)
  - [编译方式](#编译方式)
  - [`both` 运行时优先顺序](#both-运行时优先顺序)
  - [`.myzst` 的本质与读取机制](#myzst-的本质与读取机制)
  - [路径：mount 与 local（与 Python 对齐）](#路径mount-与-local与-python-对齐)
  - [读取用法：日期、品种与「范围」](#读取用法日期品种与范围)
  - [Python 版本与 `my` 包路径（工程约束）](#python-版本与-my-包路径工程约束)
  - [Python 头路径](#python-头路径)
  - [给其它工程用：CMake `add_subdirectory`](#给其它工程用cmake-add_subdirectory)
- [开发者说明](#开发者说明)
  - [接口与函数映射](#接口与函数映射)
  - [`examples` / `tests`](#examples--tests)
- 相关目录：
  - [`../examples/`](../examples/)
  - [`../tests/`](../tests/)
  - [`../README.md`](../README.md)

## 使用者说明

### 编译方式

三选一（产物在**仓库根**下 **`<BUILD_DIR_REL>/<cpp|python|both>/`**，默认 **`build/`**；输出**静态库** **`libraw_data_reader_<backend>.a`**）：

```bash
make -C raw_data_reader READER_BACKEND=cpp     # 仅 .npq 直读，不链接 Python
make -C raw_data_reader READER_BACKEND=python  # 仅嵌入 Python，不编译直读 TU
make -C raw_data_reader READER_BACKEND=both    # 两套直读；`Get*` 末尾可省略 `BackendTarget`（both 下缺省为 `Both`：按 `quote_reader` 顺序双端尝试；显式 `Cpp`/`Python` 则单端）
```

默认 **`READER_BACKEND=cpp`**（与 `CMakeLists.txt` 中 **`RAW_DATA_READER_BACKEND`** 默认一致）。

**演示程序**在仓库根执行：

```bash
make -C examples                    # 默认 cpp → build/examples/demo_cpp
READER_BACKEND=both make -C examples
```

可执行路径示例：`build/examples/demo_cpp` / `build/examples/demo_both`（相对仓库根）。

### `both` 运行时优先顺序

**默认** **`quote_reader::Order::CppFirst`**：先 C++ 直读，失败再走嵌入 Python。  
在 **`both`** 构建下可调用 **`quote_reader::set_order(quote_reader::Order::PythonFirst)`**（或设回 **`CppFirst`**）修改**进程内全局**顺序；此后所有省略后端参数而落到 **`BackendTarget::Both`**（或与 **`Both` 同义的 `Default`**）的 **`Get*`**、**`MyData::GetBasicData`** 按新顺序调度。  
**`cpp_only` / `python_only`** 库中不包含 `quote_reader` 符号，该接口对它们无意义。

若两种方式都失败，相应接口返回 `false`，且 **`both`** 下 **`std::cerr`** 会提示先后尝试与双端均未成功（具体打不开路径等原因在 C++ 直读内层另有说明）。

### `.myzst` 的本质与读取机制

**本质**：`.myzst` 是 **zstd 压缩容器**（文件后缀约定），不是未压缩的 `.npq` 本体。  
解压后内容再按业务 dtype 解释：常见场景是“可按 `np.frombuffer` 解释的二进制数据体”。

**路径规则**：QUOTE 类数据在 C++ 侧与 Python **`NpqData._path_parse`** 对齐：由 **`BuildPythonQuoteStyleRelativePaths`** 生成同 stem 的 **`.myzst` / `.npq`** 相对路径（其中 stem 指“去掉扩展名后的同一路径前缀”，含 **QUOTE / QUOTE_NEW** 分支，见 `data_path_config.h`）。

**当前 C++ 侧（`ticks_data_io_cpp.cc`）**：
- 同 stem 下**优先尝试 `.npq` 直读**（减少解压与进程创建开销）。
- 若 `.npq` 不存在，再尝试 `.myzst`：通过 `zstd -d -q -c` 解压到**内存字节**（不落盘临时 `.npq`）。
- `.myzst` 解压成功后按目标结构体大小做字节对齐检查并装载到 `std::vector<T>`。

**Python `my.data` 侧（参考 `my/data/quote.py`）**：
- 逻辑顺序（以 `quote.data()` 常规 QUOTE 路径为例）：
  1) 先构造压缩路径 `NpqData.COMPRESS_QUOTE.path_parse(...)`；
  2) 若压缩文件存在：`streamdecompress.decompress(...)` 解压到内存，再 `np.frombuffer(...)` 按 dtype 解释；
  3) 若压缩文件不存在：回退 `NpqData.QUOTE.path_parse(...)`，并用 `np.fromfile(...)` 读取 `.npq`。
- `old_trade` / `old_order` 等路径也采用同类顺序（先压缩、后未压缩）。
- 不是所有函数都走该顺序：`position` / `get_dynamic_cash` 直接 `np.fromfile(.npq)`，`exotic` 走 `.arw` 解析。
- `quote.py` 中**没有名为** `new_order` / `new_trade` 的独立读取函数；这类数据通常通过 `quote.data()` 按 `mi_type` 进入常规分支处理，因此一般也遵循“先 `.myzst`、后 `.npq`”。

> 顺序差异说明：Python `quote.data()` 对同类数据通常“先看压缩文件，再回退未压缩”；本项目 C++ 侧有意采用“先 `.npq`，再 `.myzst`”以优先命中现成未压缩文件。

> 备注：`factor_v2` 等部分压缩文件在 Python 侧会先解析自定义头（如 `factor_len/meta_len/meta_info`）后再解读数据区；与普通 QUOTE 的“直接 frombuffer”路径不同。

### 路径：mount 与 local（与 Python 对齐）

- **根**：`MYDATA_BUSINESS_TYPE` → 与 `NpqData` 默认规则一致（见 `DataPathConfig::RootFromEnvironment`）。
- **本地覆盖**：`TicksData::SetLocalPath` 对应 Python `NpqData.local_path` 语义；C++ **直读**在 `ResolveReadableRelative` 下若本地存在同名常规文件则优先读本地。嵌入 Python 路径仍由 `my.data` 配置；二者可分别设置。

### 读取用法：日期、品种与「范围」

一次调用对应**一个交易日** + **一个逻辑文件**（由类型与模板拼出的 `.npq` 等路径），**没有**「一次加载多日区间」或「仅加载盘中某时间段」的独立参数。

- **交易日 `date`**：`int`，与路径模板中的 `{date}` 一致（一般为 `YYYYMMDD` 形式的整数）。需要多日数据时，在业务代码里**按日循环**多次调用 **`GetTick`** 等接口即可。
- **品种 / 合约代码 `symbol`**：`std::string`，写入路径中的 `{symbol}`，一次调用只读**该代码**对应文件。多品种 = **多次调用**，每次传入不同 `symbol`。与 Python 侧一致时还可使用约定字面量如 **`all`**、**`all_other`** 等（单文件聚合语义，取决于 `my.data` 与落盘）。
- **因子**：参数名为 `fid`（字符串），对应模板中的 `{fid}`，语义上仍是「单日 + 单一标识」。
- **期货**：除 `date`、`symbol` 外还有 **`mi_type`（int）**，参与 `{mi_type}` 路径段；**`Get200Futures`** / **`Get212Futures`** / **`Get225Futures`** 为固定 `mi_type` 的便捷封装。
- **日内时间或记录条数**：C++ 直读实现（如 `GetTickViaCpp`）对匹配到的文件**从开头顺序读到 EOF**，**没有** `start_time`、`end_time`、`offset`、`max_rows` 等参数。若只要盘中一段，请在读入 `std::vector` 之后按结构体里的时间字段自行过滤，或扩展读取逻辑。
- **数据根与布局**：**`SetMydataPath`**（末尾可省略 **`BackendTarget`**，缺省为 **`io::kDefaultPath`**）设置**数据根**；**`SetLocalPath`** 设置**本地优先根**（见上一节）。TICK/逐笔/委托/期货等 QUOTE 系路径由 **`TicksData::FormatPath`** 经 **`BuildPythonQuoteStyleRelativePaths`** 与 **`ResolveReadableRelative`** 解析（与 Python **QUOTE / QUOTE_NEW** 对齐）；MC/ESMC、231 委托链等仍见 `ResolvePathTemplate` / `FillPathTemplate` / `NpqOrderTransRelativeArtifacts`。
- **主连表**：**`GetSymbolFromMc`** 读取 MC/ESMC 落盘表（**MC** = Main Contract，主连/连续合约映射）。

**对外仅大驼峰**：**`TicksData`**、**`MyData`** 的读数接口均在末尾带 **`BackendTarget`**，可省略；缺省由 **`io::kDefaultRead`** / **`io::kDefaultPath`** 决定（**`cpp_only`→Cpp**，**`python_only`→Python**，**`both`→Both**：双端按序尝试；**`Default`** 与 **`Both`** 在读数/改路径上同义，保留兼容）。路径占位符见 **`data_path_config.h`**。

### Python 版本与 `my` 包路径（工程约束）

- **解释器版本**：嵌入 Python 读数路径与链接选项按 **Python 3.8.10** 约定；**Makefile** 在 `READER_BACKEND=python|both` 时强制 **`sys.version_info` 为 3.8.x**（默认命令 **`python3.8`** / **`python3.8-config`**，与 **`origin/Makefile`** 一致）。**CMake** 在 `python|both` 时要求 **`Python3_VERSION` 匹配 `3.8.*`**。若本机布局不同，可 `make PYTHON3_8=/path/to/python3.8 PYTHON3_8_CONFIG=...`，或设置 **`Python3_ROOT_DIR`** 指向 3.8 安装前缀。
- **业务包位置**：C++ 嵌入 Python 时调用的 **`my` 系库**（如 `my.data`）在本部署中位于  
  **`/usr/local/python3.8.10/lib/python3.8/site-packages/my/`**。  
  若你的环境前缀不同，请将解释器安装到等价布局，或调整 `PYTHONPATH` / 软链接 / 安装路径，使运行时能 `import my...` 且与上述语义一致。

### Python 头路径

若遇 `pyconfig.h` 找不到，Makefile 已通过 `python3.8 -c "import sysconfig; ..."` 追加 include；应与上节 **3.8.10** 的安装前缀一致。

### 给其它工程用：CMake `add_subdirectory`

本目录提供 **`CMakeLists.txt`**，父工程 **`add_subdirectory(raw_data_reader)`** 后链接 **`raw_data_reader::core`**（从源码编静态库）：

```cmake
find_package(Threads REQUIRED)
add_subdirectory(third_party/raw_data_reader)   # 路径按实际放置修改
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE raw_data_reader::core)
```

- 缓存变量 **`RAW_DATA_READER_BACKEND`**：`cpp` / `python` / `both`（默认 **`cpp`**），与 **`Makefile`** / **`backend_config.h`** 一致。
- **`target_link_libraries(... raw_data_reader::core)`** 会传递 **PUBLIC** 的 include 根为 **`raw_data_reader/`** 目录本身：业务代码写 **`#include "ticks_data.h"`**、**`#include "raw_data_types/my_stock.h"`**，无需 **`..`**，也不要求再单独 **` -I raw_data_types`**。
- **仅 include 头文件不够**：必须链接实现（CMake **静态库目标**，或在本仓库内用 **`make -C raw_data_reader`** 得到 **`build/<backend>/libraw_data_reader_<backend>.a`** 后自行链到父工程）。
- **`python` / `both`**：需本机 **Python 3.8.10 + numpy**，且 **`my` 包**在 **`site-packages/my/`**（见上文「Python 版本与 `my` 包路径」）；CMake 需 **Development + Development.Embed**。若 `find_package(Python3)` 失败，可尝试 **`Python3_ROOT_DIR=/usr/local/python3.8.10`**（或你的等价前缀）等变量。

仓库根可以没有顶层 `CMakeLists.txt`；本演示工程主构建入口仍是 **Make**。

## 开发者说明

### 接口与函数映射

- 对外仅保留 PascalCase 新接口（如 `GetTick`、`GetTransactionV2`、`Get200Futures`），不再提供 snake_case 旧接口别名。
- 三个 hf-demo 的函数映射与实现归类见仓库根 **`HF_DEMO_FUNCTION_MAPPING.md`**。

### `examples` / `tests`

| 目录 | 方式 |
|------|------|
| **`../examples/`** | 在 **`build/examples/raw_data_reader_<backend>_objs/`** 编译与本 Makefile 同源的 **`.cc`**，再与 demo 链成可执行文件。 |
| **`../tests/`** | 依赖 **`both`** 的 parity / read_speed / auto_dispatch：在 **`build/both/`** 编译读库 **`.o`** 后与测试链。 |

说明见 **`examples/README.md`**、**`tests/README.md`**。
