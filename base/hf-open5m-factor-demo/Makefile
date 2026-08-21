#=============================================================================
# 顶层 Makefile - 构建入口说明
#=============================================================================
# 本 Makefile 为项目顶层构建入口：根据是否存在 app_live / app_factor / app_model 目录，
# 提供 build、run、clean、release、clangd-db 等目标，通过调用 CMake 完成实际编译。
# 详细用法与选项说明请执行 make help。
#=============================================================================

# 检测目录存在性
HAS_APP_LIVE := $(shell test -d app_live && echo 1 || echo 0)
HAS_APP_FACTOR := $(shell test -d app_factor && echo 1 || echo 0)
HAS_APP_MODEL := $(shell test -d app_model && echo 1 || echo 0)
HAS_MODELS := $(shell test -d models && echo 1 || echo 0)

# 配置变量
BUILD_DIR := build
RELEASE_DIR := release
CMAKE := cmake
MAKE := make
PYTHON := python3.8
JOBS := $$(( ($$(nproc --all) + 1) / 2 ))  # 使用一半的核心数

# build-live / build-factor / build-model 在未指定命令行 TIME_STATS= 时使用的默认值 ON / OFF
DEFAULT_LIVE_TIME_STATS_VAL := ON
DEFAULT_FACTOR_TIME_STATS_VAL := ON
DEFAULT_MODEL_TIME_STATS_VAL := ON

# C++ 标准（传给 CMake 的 CMAKE_CXX_STANDARD）
# DEFAULT_CMAKE_CXX_STANDARD：工程默认标准；若希望全局默认改为 14/17 等，改此值即可。
# 单次构建覆盖：make build CMAKE_CXX_STANDARD=17
# 未在命令行指定 CMAKE_CXX_STANDARD 时，使用下面的默认值。
# 穿透说明：该变量写入根 CMakeLists.txt 后，对未单独设 CXX_STANDARD 的目标（如 app_live/app_factor/app_model）
# 由 CMake 作为目录树默认值生效；因子/模型静态库见 cmake/ModuleCxxStandard.cmake 显式设置。
# 兼容性：CMake 配置阶段（根 CMakeLists.txt）检查 C++ 标准与 g++/Clang、CMake 是否匹配，见 cmake/CxxStandardCompatCheck.cmake；
# 不匹配仅 WARNING，不中断构建。勿在 Makefile 重复 -P 预检（与 configure 重复且编译器探测可能不一致）。
DEFAULT_CMAKE_CXX_STANDARD := 11
CMAKE_CXX_STANDARD ?= $(DEFAULT_CMAKE_CXX_STANDARD)

# ONNX Runtime 全局版本（传给 CMake 的 ONNX_RUNTIME）；所有 ONNX 模型统一使用，禁止单模型覆盖。
# 短标签 gpu-1.19.2 解析为 onnxruntime-linux-x64-gpu-1.19.2；也可传完整目录名。
# 单次覆盖：make build-model ONNX_RUNTIME=gpu-1.24.4
DEFAULT_ONNX_RUNTIME := gpu-1.19.2
ONNX_RUNTIME ?= $(DEFAULT_ONNX_RUNTIME)

# DISABLE_AVX512：设为 1 / ON / yes 时传给 CMake（-DDISABLE_AVX512=ON），强制不向 Release 追加 -mavx512f
ifneq ($(filter 1 ON YES yes,$(DISABLE_AVX512)),)
DISABLE_AVX512_CMAKE := -DDISABLE_AVX512=ON
else
DISABLE_AVX512_CMAKE := -DDISABLE_AVX512=OFF
endif

# 策略会话模式（STRATEGY_SESSION_MODE）
# 可选值: OPEN (盘前集合竞价), CLOSE (收盘集合竞价), CONTINUOUS (盘中连续竞价)
# 支持在命令行覆盖: make build-live STRATEGY_SESSION_MODE=CLOSE
THIS_FOLDER_NAME := $(word $(words $(subst /, ,$(shell pwd))),$(subst /, ,$(shell pwd)))
ifneq ($(findstring open5m,$(THIS_FOLDER_NAME))$(findstring Open5m,$(THIS_FOLDER_NAME))$(findstring continuous,$(THIS_FOLDER_NAME))$(findstring Continuous,$(THIS_FOLDER_NAME))$(findstring allday,$(THIS_FOLDER_NAME))$(findstring Allday,$(THIS_FOLDER_NAME)),)
    STRATEGY_SESSION_MODE := CONTINUOUS
    STRATEGY_SESSION_MODE_FOLDER_MATCH := 1
else ifneq ($(findstring open,$(THIS_FOLDER_NAME))$(findstring Open,$(THIS_FOLDER_NAME)),)
    STRATEGY_SESSION_MODE := OPEN
    STRATEGY_SESSION_MODE_FOLDER_MATCH := 1
else ifneq ($(findstring close,$(THIS_FOLDER_NAME))$(findstring Close,$(THIS_FOLDER_NAME)),)
    STRATEGY_SESSION_MODE := CLOSE
    STRATEGY_SESSION_MODE_FOLDER_MATCH := 1
else
    STRATEGY_SESSION_MODE := OPEN
    STRATEGY_SESSION_MODE_FOLDER_MATCH := 0
endif

# DEBUG 选项处理
# 如果命令行指定了 DEBUG=1 或 DEBUG=ON，则使用 Debug 构建类型
ifneq ($(filter 1 ON,$(DEBUG)),)
    CMAKE_BUILD_TYPE := Debug
else
    CMAKE_BUILD_TYPE := Release
endif

# 目标颜色
COLOR_GREEN := \033[0;32m
COLOR_YELLOW := \033[0;33m
COLOR_RED := \033[0;31m
COLOR_BLUE := \033[0;34m
COLOR_RESET := \033[0m

# 确定默认目标（优先级：live > factor > model）
ifeq ($(HAS_APP_LIVE),1)
    DEFAULT_BUILD := build-live
    DEFAULT_RUN := run-live
    DEFAULT_CLEAN := clean
else ifeq ($(HAS_APP_FACTOR),1)
    DEFAULT_BUILD := build-factor
    DEFAULT_RUN := run-factor
    DEFAULT_CLEAN := clean
else ifeq ($(HAS_APP_MODEL),1)
    DEFAULT_BUILD := build-model
    DEFAULT_RUN := run-model
    DEFAULT_CLEAN := clean
else
    DEFAULT_BUILD := help
    DEFAULT_RUN := help
    DEFAULT_CLEAN := clean
endif

.DEFAULT_GOAL := $(DEFAULT_BUILD)

# help/info：列出 models/_third_party_libs 下已解压的 ONNX Runtime 短标签（与 OnnxRuntimeResolve.cmake 目录名规则一致）
define echo_onnx_runtime_help
	@if [ "$(HAS_MODELS)" = "1" ]; then \
		echo -e "  ONNX_RUNTIME=...                              - 全局 ONNX Runtime 版本，默认 $(DEFAULT_ONNX_RUNTIME)（如 gpu-1.19.2）"; \
		if [ -d models/_third_party_libs ]; then \
			tags=$$(for d in models/_third_party_libs/onnxruntime-linux-x64-*; do \
				[ -d "$$d" ] || continue; \
				basename "$$d" | sed 's/^onnxruntime-linux-x64-//'; \
			done | LC_ALL=C sort -u); \
			if [ -n "$$tags" ]; then \
				echo -e "${COLOR_BLUE}  > 本机 ONNX Runtime 可选版本（models/_third_party_libs，ONNX_RUNTIME 短标签）:${COLOR_RESET}"; \
				echo "$$tags" | while read -r tag; do echo -e "    - $$tag"; done; \
				echo -e "${COLOR_YELLOW}    请自行判断是否与本机 glibc、CUDA/cuDNN、g++ 等环境适配；详见 models/_third_party_libs/README.md${COLOR_RESET}"; \
				echo -e "    用法: make build-model ONNX_RUNTIME=<上表短标签>  （当前默认 $(DEFAULT_ONNX_RUNTIME)）"; \
			fi; \
		fi; \
	fi
endef

# 帮助信息（动态生成）
.PHONY: help
help:
	@echo -e "${COLOR_BLUE}HF Stock Cross Section Framework - 构建系统${COLOR_RESET}"
	@echo -e "${COLOR_GREEN}构建工具:${COLOR_RESET}"
	@echo -e "  $(shell $(MAKE) --version 2>/dev/null | head -n 1)"
	@echo -e "  $(shell $(CMAKE) --version 2>/dev/null | head -n 1)"
	@echo -e "  $(shell gcc --version 2>/dev/null | head -n 1)"
	@echo -e "  $(shell g++ --version 2>/dev/null | head -n 1)"
	@echo ""
	@echo -e "${COLOR_GREEN}scl / Devtoolset（可选，需系统已安装 softwarecollections）:${COLOR_RESET}"
	@echo -e "  ${COLOR_GREEN}scl -l${COLOR_RESET}                                                 - 列出本机可用的集合名（如 devtoolset-11）"
	@echo -e "  ${COLOR_GREEN}scl enable devtoolset-11 bash${COLOR_RESET}                          - 进入子 shell，使用该集合内的 gcc/g++；exit 退出后环境复原"
	@echo -e "  ${COLOR_GREEN}scl enable devtoolset-11 'make clean && make build'${COLOR_RESET}    - 在 scl 环境下单次构建"
	@echo -e "  更换编译器后删除 build（或清空 CMake 缓存）再执行 make build*，否则可能仍沿用 CMakeCache 里旧编译器路径。"
	@echo ""
	@echo -e "${COLOR_GREEN}主要目标:${COLOR_RESET}"
	@if [ "$(HAS_APP_LIVE)" = "1" ]; then \
		echo -e "  ${COLOR_GREEN}build-live${COLOR_RESET}        - 构建Live应用"; \
	fi
	@if [ "$(HAS_APP_FACTOR)" = "1" ]; then \
		echo -e "  ${COLOR_GREEN}build-factor${COLOR_RESET}      - 构建因子应用"; \
	fi
	@if [ "$(HAS_APP_MODEL)" = "1" ]; then \
		echo -e "  ${COLOR_GREEN}build-model${COLOR_RESET}       - 构建模型应用"; \
	fi
	@if [ "$(HAS_APP_LIVE)" = "1" ] || [ "$(HAS_APP_FACTOR)" = "1" ] || [ "$(HAS_APP_MODEL)" = "1" ]; then \
		echo -e "  ${COLOR_GREEN}build${COLOR_RESET}             - 构建应用 (默认: $(DEFAULT_BUILD))"; \
		echo -e "  ${COLOR_GREEN}clangd-db${COLOR_RESET}         - 为 IDE 生成编译数据库，供代码分析/补全/跳转用"; \
		echo -e "  ${COLOR_GREEN}info${COLOR_RESET}              - 显示当前构建环境、参数与目标信息"; \
		echo -e "  ${COLOR_GREEN}release${COLOR_RESET}           - 构建并打包所有应用到release文件夹"; \
	fi
	@echo ""
	@echo -e "${COLOR_GREEN}运行目标:${COLOR_RESET}"
	@if [ "$(HAS_APP_LIVE)" = "1" ] || [ "$(HAS_APP_FACTOR)" = "1" ] || [ "$(HAS_APP_MODEL)" = "1" ]; then \
		echo -e "  ${COLOR_GREEN}run${COLOR_RESET}               - 构建并运行应用 (默认: $(DEFAULT_RUN))"; \
	fi
	@if [ "$(HAS_APP_LIVE)" = "1" ]; then \
		echo -e "  ${COLOR_GREEN}run-live${COLOR_RESET}          - 构建并运行Live应用"; \
	fi
	@if [ "$(HAS_APP_FACTOR)" = "1" ]; then \
		echo -e "  ${COLOR_GREEN}run-factor${COLOR_RESET}        - 构建并运行因子应用"; \
	fi
	@if [ "$(HAS_APP_MODEL)" = "1" ]; then \
		echo -e "  ${COLOR_GREEN}run-model${COLOR_RESET}         - 构建并运行模型应用"; \
	fi
	@echo ""
	@echo -e "${COLOR_GREEN}清理目标:${COLOR_RESET}"
	@echo -e "  ${COLOR_GREEN}clean${COLOR_RESET}             - 清理构建文件"
	@echo -e "  ${COLOR_GREEN}clean-release${COLOR_RESET}     - 清理release文件夹"
	@echo -e "  ${COLOR_GREEN}clean-all${COLOR_RESET}         - 清理所有生成文件"
	@echo ""
	@echo -e "${COLOR_GREEN}配置选项:${COLOR_RESET}"
	@echo -e "  BUILD_DIR=...                                 - 设置构建目录，默认值 ${BUILD_DIR}"
	@echo -e "  RELEASE_DIR=...                               - 设置发布目录，默认值 ${RELEASE_DIR}"
	@echo -e "  JOBS=...                                      - 设置编译/链接并行线程数，默认值 ${JOBS}"
	@echo -e "  PYTHON=...                                    - 设置Python版本，默认值 ${PYTHON}"
	@echo -e "  TIME_STATS=ON/OFF                             - 设置是否强制规定开关时间统计功能"
	@echo -e "  NATIVE=ON/OFF                                 - 设置是否启用 -march=native，默认值 OFF"
	@echo -e "  DEBUG=1/ON                                    - 启用Debug构建类型，默认值 $(if $(DEBUG),$(DEBUG),OFF)"
	@echo -e "  DISABLE_AVX512=1|ON|yes                       - 强制禁用 AVX-512（不向 Release 追加 -mavx512f），即使 GCC/CPU 支持"
	@echo -e "  CMAKE_CXX_STANDARD=...                        - C++ 标准（传给 CMake），默认 $(DEFAULT_CMAKE_CXX_STANDARD)"
	$(echo_onnx_runtime_help)
	@echo ""
ifeq ($(STRATEGY_SESSION_MODE_FOLDER_MATCH),0)
	@echo -e "  STRATEGY_SESSION_MODE=OPEN|CLOSE|CONTINUOUS   - 策略会话模式；当前默认值 $(STRATEGY_SESSION_MODE) ，可编译时覆盖，例如:"
	@echo -e "                                                    make build-live STRATEGY_SESSION_MODE=CLOSE"
endif
	@echo ""
	@echo -e "${COLOR_GREEN}常用示例:${COLOR_RESET}"
	@echo -e "  make build                                    - 默认Release构建（NATIVE=OFF）"
	@echo -e "  make build NATIVE=ON                          - Release构建并启用 -march=native"
	@echo -e "  make build DEBUG=1                            - Debug构建（NATIVE默认OFF）"
	@echo -e "  make build DEBUG=1 NATIVE=ON                  - Debug构建并启用 -march=native"
	@echo -e "  make build CMAKE_CXX_STANDARD=17              - 使用 C++17 标准"
	@echo -e "  make build-model DISABLE_AVX512=1             - GCC11+ 与 CPU 支持 AVX-512 时仍不追加 -mavx512f"
	@echo ""
	@echo -e "${COLOR_YELLOW}注意事项:${COLOR_RESET}"
	@echo -e "  ${COLOR_YELLOW}[clangd-db]${COLOR_RESET} 给 IDE/编辑器用，不是给编译器用。生成 compile_commands.json，供 VSCode/Cursor 的"
	@echo -e "    clangd 做代码补全、跳转、实时语法检查。结果在 .clangd-build/，不影响 build/。"
	@echo -e "    IDE 里 C++ 报错不准或头文件找不到时，可执行 make clangd-db 后重载窗口。"
	@if [ "$(HAS_APP_LIVE)" = "1" ]; then \
		echo -e "  ${COLOR_YELLOW}[Live 应用]${COLOR_RESET} 编译产物为 libstrategy.so，通过 Python 脚本（如 run_strategy.py）加载运行。"; \
		echo -e "    build-live 固定 -DGLIBCXX_CXX11_ABI=0（与 simu 预编译 st_wrapper 的 st_config_t 对齐，非 glibc 问题）。"; \
		echo -e "    切换 CMAKE_CXX_STANDARD 后请 make clean && make build-live，避免 CMake 缓存旧 ABI。"; \
	fi
	@if [ "$(HAS_APP_FACTOR)" = "1" ] || [ "$(HAS_APP_MODEL)" = "1" ]; then \
		echo -e "  ${COLOR_YELLOW}[因子/模型应用]${COLOR_RESET} 编译为 main 可执行文件，直接运行。"; \
	fi
	@echo -e "  ${COLOR_YELLOW}[Release]${COLOR_RESET} 使用较高优化等级的编译选项（如 -O3、向量化等），适合生产环境。"
	@echo -e "  ${COLOR_YELLOW}[NATIVE]${COLOR_RESET} 控制是否启用 -march=native（按当前 CPU 指令集优化）。"
	@echo -e "    编译后要在不同机器/不同 CPU 架构或不同系统上部署运行时，建议 NATIVE=OFF（默认），"
	@echo -e "    以保证兼容性；若确定构建与运行环境一致（同一台机或同架构同系统），可设 NATIVE=ON，"
	@echo -e "    以最大化运行性能。"

# 构建成功后打印主产物绝对路径（绿色）
# $(1): live | factor | model — 与 build_target 第三参数一致
define report_build_artifacts
	@case "$(1)" in \
		live) f="$(BUILD_DIR)/app_live/libstrategy.so" ;; \
		factor) f="$(BUILD_DIR)/app_factor/main" ;; \
		model) f="$(BUILD_DIR)/app_model/main" ;; \
		*) f="" ;; \
	esac; \
	_abs() { \
		if command -v realpath >/dev/null 2>&1; then realpath "$$1"; \
		elif readlink -f "$$1" >/dev/null 2>&1; then readlink -f "$$1"; \
		else cd "$$(dirname "$$1")" && echo "$$(pwd)/$$(basename "$$1")"; \
		fi; \
	}; \
	if [ -n "$$f" ] && [ -e "$$f" ]; then \
		echo -e "${COLOR_GREEN}$$(_abs "$$f")${COLOR_RESET}"; \
	elif [ -n "$$f" ]; then \
		echo -e "${COLOR_YELLOW}未找到预期产物: $$f${COLOR_RESET}"; \
	fi
endef

# 通用构建函数
# $1: CMake参数
# $2: 应用名称（显示用）
# $3: 产物类型 — live | factor | model（用于 report_build_artifacts）
define build_target
	@echo -e "${COLOR_GREEN}构建工具:${COLOR_RESET}"
	@echo -e "  $(shell $(MAKE) --version 2>/dev/null | head -n 1)"
	@echo -e "  $(shell $(CMAKE) --version 2>/dev/null | head -n 1)"
	@echo -e "  $(shell gcc --version 2>/dev/null | head -n 1)"
	@echo -e "  $(shell g++ --version 2>/dev/null | head -n 1)"
	@echo -e "${COLOR_BLUE}开始构建: $(2)${COLOR_RESET}"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. $(1) \
		-DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) \
		-DCMAKE_CXX_STANDARD=$(CMAKE_CXX_STANDARD) \
		-DONNX_RUNTIME=$(ONNX_RUNTIME) \
		-DSTRATEGY_SESSION_MODE=$(STRATEGY_SESSION_MODE) \
		$(DISABLE_AVX512_CMAKE) \
		|| { echo -e "${COLOR_RED}CMake 配置失败${COLOR_RESET}"; exit 1; }
	@cd $(BUILD_DIR) && $(MAKE) -j$(JOBS) || { echo -e "${COLOR_RED}编译失败${COLOR_RESET}"; exit 1; }
	@echo -e "${COLOR_GREEN}构建完成: $(2)${COLOR_RESET}"
	@$(call report_build_artifacts,$(3))
endef

# 运行Live应用
define run_live
	@if [ ! -f $(BUILD_DIR)/app_live/libstrategy.so ]; then \
		echo -e "${COLOR_RED}错误: Live应用未构建，请先运行 'make build-live'${COLOR_RESET}"; \
		exit 1; \
	fi
	@if [ ! -f app_live/run_strategy.py ]; then \
		echo -e "${COLOR_RED}错误: run_strategy.py 不存在${COLOR_RESET}"; \
		exit 1; \
	fi
	@echo -e "${COLOR_BLUE}运行Live应用...${COLOR_RESET}"
	@$(PYTHON) app_live/run_strategy.py
endef

# 运行因子/模型应用
define run_main
	@if [ ! -f $(BUILD_DIR)/$(1)/main ]; then \
		echo -e "${COLOR_RED}错误: $(2)应用未构建，请先运行 'make build-$(3)'${COLOR_RESET}"; \
		exit 1; \
	fi
	@echo -e "${COLOR_BLUE}运行$(2)应用...${COLOR_RESET}"
	@./$(BUILD_DIR)/$(1)/main
endef

# =============================================================================
# 构建目标
# =============================================================================

# build-live
# GLIBCXX_CXX11_ABI=0：simu 的 st_wrapper 按旧版 libstdc++ 布局构造 st_config_t（含 std::string），
# libstrategy.so 必须使用同一 ABI，否则 my_st_init_v3 读 st_name 等字段会段错误。
# 与 C++ 语言标准（CMAKE_CXX_STANDARD）无关；CentOS7/GCC4.8 仅旧布局，显式 0 与改前行为一致。
ifeq ($(HAS_APP_LIVE),1)
.PHONY: build-live
build-live:
	$(eval TIME_STATS_VAL := $(if $(TIME_STATS),$(TIME_STATS),$(DEFAULT_LIVE_TIME_STATS_VAL)))
	$(eval NATIVE_VAL := $(if $(NATIVE),$(NATIVE),OFF))
	$(call build_target,-DBUILD_APP_LIVE=ON -DBUILD_APP_FACTOR=OFF -DBUILD_APP_MODEL=OFF -DGLIBCXX_CXX11_ABI=0 -DTIME_STATS=$(TIME_STATS_VAL) -DENABLE_NATIVE_MARCH=$(NATIVE_VAL),Live应用,live)
endif

# build-factor
# GLIBCXX_CXX11_ABI=1：独立 main，不经 simu 传入 st_config_t，可与本机 GCC 7+ 默认新 string 布局一致。
ifeq ($(HAS_APP_FACTOR),1)
.PHONY: build-factor
build-factor:
	$(eval TIME_STATS_VAL := $(if $(TIME_STATS),$(TIME_STATS),$(DEFAULT_FACTOR_TIME_STATS_VAL)))
	$(eval NATIVE_VAL := $(if $(NATIVE),$(NATIVE),OFF))
	$(call build_target,-DBUILD_APP_LIVE=OFF -DBUILD_APP_FACTOR=ON -DBUILD_APP_MODEL=OFF -DGLIBCXX_CXX11_ABI=1 -DTIME_STATS=$(TIME_STATS_VAL) -DENABLE_NATIVE_MARCH=$(NATIVE_VAL),因子应用,factor)
endif

# build-model
# GLIBCXX_CXX11_ABI=1：同 build-factor，见上。
ifeq ($(HAS_APP_MODEL),1)
.PHONY: build-model
build-model:
	$(eval TIME_STATS_VAL := $(if $(TIME_STATS),$(TIME_STATS),$(DEFAULT_MODEL_TIME_STATS_VAL)))
	$(eval NATIVE_VAL := $(if $(NATIVE),$(NATIVE),OFF))
	$(call build_target,-DBUILD_APP_LIVE=OFF -DBUILD_APP_FACTOR=OFF -DBUILD_APP_MODEL=ON -DGLIBCXX_CXX11_ABI=1 -DTIME_STATS=$(TIME_STATS_VAL) -DENABLE_NATIVE_MARCH=$(NATIVE_VAL),模型应用,model)
endif

# 默认build目标
.PHONY: build
build: $(DEFAULT_BUILD)

# 生成 clangd 用 compile_commands.json（不影响默认 build 目录）
.PHONY: clangd-db
clangd-db:
	@echo -e "${COLOR_BLUE}生成 clangd compile_commands.json...${COLOR_RESET}"
	@chmod +x ./.tools/clangd/update_compdb.sh
	@bash ./.tools/clangd/update_compdb.sh || { echo -e "${COLOR_RED}生成 clangd compile DB 失败${COLOR_RESET}"; exit 1; }
	@echo -e "${COLOR_GREEN}clangd compile DB 生成完成${COLOR_RESET}"

# =============================================================================
# Release构建
# =============================================================================

.PHONY: release
release:
	@echo -e "${COLOR_BLUE}开始Release构建...${COLOR_RESET}"
	@$(MAKE) clean-release
	@$(MAKE) clean
	@mkdir -p $(RELEASE_DIR)
	@echo -e "${COLOR_GREEN}创建release文件夹${COLOR_RESET}"
	@mkdir -p $(RELEASE_DIR)/live
	@mkdir -p $(RELEASE_DIR)/factor
	@mkdir -p $(RELEASE_DIR)/model
	@echo -e "${COLOR_GREEN}创建release子目录${COLOR_RESET}"
	@if [ "$(HAS_APP_LIVE)" = "1" ]; then \
		echo -e "${COLOR_BLUE}构建Live应用...${COLOR_RESET}"; \
		$(MAKE) build-live || exit 1; \
		if [ -f $(BUILD_DIR)/app_live/libstrategy.so ]; then \
			cp $(BUILD_DIR)/app_live/libstrategy.so $(RELEASE_DIR)/live/; \
			echo -e "${COLOR_GREEN}复制Live库文件: libstrategy.so${COLOR_RESET}"; \
		else \
			echo -e "${COLOR_RED}错误: Live应用构建失败${COLOR_RESET}"; \
			exit 1; \
		fi; \
		if [ -f app_live/run_strategy.py ]; then \
			cp app_live/run_strategy.py $(RELEASE_DIR)/live/; \
			echo -e "${COLOR_GREEN}复制Python运行脚本: run_strategy.py${COLOR_RESET}"; \
		else \
			echo -e "${COLOR_YELLOW}警告: run_strategy.py 不存在${COLOR_RESET}"; \
		fi; \
		if [ -f config_live.json ]; then \
			cp config_live.json $(RELEASE_DIR)/live/; \
		echo -e "${COLOR_GREEN}复制配置文件: config_live.json${COLOR_RESET}"; \
	else \
		echo -e "${COLOR_YELLOW}警告: config_live.json 不存在${COLOR_RESET}"; \
		fi; \
		$(MAKE) clean; \
	fi
	@if [ "$(HAS_APP_FACTOR)" = "1" ]; then \
		echo -e "${COLOR_BLUE}构建因子应用...${COLOR_RESET}"; \
		$(MAKE) build-factor || exit 1; \
		if [ -f $(BUILD_DIR)/app_factor/main ]; then \
			cp $(BUILD_DIR)/app_factor/main $(RELEASE_DIR)/factor/; \
			echo -e "${COLOR_GREEN}复制因子可执行文件: main${COLOR_RESET}"; \
	else \
			echo -e "${COLOR_RED}错误: 因子应用构建失败${COLOR_RESET}"; \
		exit 1; \
		fi; \
		if [ -f config_factor.json ]; then \
			cp config_factor.json $(RELEASE_DIR)/factor/; \
			echo -e "${COLOR_GREEN}复制配置文件: config_factor.json${COLOR_RESET}"; \
		else \
			echo -e "${COLOR_YELLOW}警告: config_factor.json 不存在${COLOR_RESET}"; \
		fi; \
		$(MAKE) clean; \
	fi
	@if [ "$(HAS_APP_MODEL)" = "1" ]; then \
		echo -e "${COLOR_BLUE}构建模型应用...${COLOR_RESET}"; \
		$(MAKE) build-model || exit 1; \
		if [ -f $(BUILD_DIR)/app_model/main ]; then \
			cp $(BUILD_DIR)/app_model/main $(RELEASE_DIR)/model/; \
			echo -e "${COLOR_GREEN}复制模型可执行文件: main${COLOR_RESET}"; \
		else \
			echo -e "${COLOR_RED}错误: 模型应用构建失败${COLOR_RESET}"; \
			exit 1; \
		fi; \
		if [ -f config_model.json ]; then \
			cp config_model.json $(RELEASE_DIR)/model/; \
			echo -e "${COLOR_GREEN}复制配置文件: config_model.json${COLOR_RESET}"; \
	else \
			echo -e "${COLOR_YELLOW}警告: config_model.json 不存在${COLOR_RESET}"; \
		fi; \
		$(MAKE) clean; \
	fi
	@echo -e "${COLOR_GREEN}Release构建完成，文件已复制到$(RELEASE_DIR)文件夹${COLOR_RESET}"

# =============================================================================
# 运行目标
# =============================================================================

# run-live
ifeq ($(HAS_APP_LIVE),1)
.PHONY: run-live
run-live: build-live
	$(call run_live)
endif

# run-factor
ifeq ($(HAS_APP_FACTOR),1)
.PHONY: run-factor
run-factor: build-factor
	$(call run_main,app_factor,因子,factor)
endif

# run-model
ifeq ($(HAS_APP_MODEL),1)
.PHONY: run-model
run-model: build-model
	$(call run_main,app_model,模型,model)
endif

# 默认run目标
.PHONY: run
run: $(DEFAULT_RUN)

# =============================================================================
# Order Builder V2 专项测试
# =============================================================================

.PHONY: test-order-builder
test-order-builder:
	@mkdir -p build-tests/order-builder
	$(CXX) -std=c++17 -O2 -I. \
		tests/demofw00/order_synthesizer_test.cpp \
		factors/demofw00/tools/market/order_synthesizer.cpp \
		-o build-tests/order-builder/order_synthesizer_test
	@build-tests/order-builder/order_synthesizer_test

.PHONY: bench-order-builder
bench-order-builder:
	@mkdir -p build-tests/order-builder
	$(CXX) -std=c++17 -O3 -DNDEBUG -I. \
		tests/demofw00/order_synthesizer_npq_replay.cpp \
		factors/demofw00/tools/market/order_synthesizer.cpp \
		-o build-tests/order-builder/order_synthesizer_npq_replay

# =============================================================================
# 清理目标
# =============================================================================

.PHONY: clean
clean:
	@echo -e "${COLOR_BLUE}清理构建文件...${COLOR_RESET}"
	@rm -rf $(BUILD_DIR)
	@echo -e "${COLOR_GREEN}清理完成${COLOR_RESET}"

.PHONY: clean-release
clean-release:
	@echo -e "${COLOR_BLUE}清理release文件夹...${COLOR_RESET}"
	@rm -rf $(RELEASE_DIR)
	@echo -e "${COLOR_GREEN}清理release文件夹完成${COLOR_RESET}"

.PHONY: clean-all
clean-all: clean clean-release
	@echo -e "${COLOR_BLUE}清理所有生成文件...${COLOR_RESET}"
	@find . -name "*.o" -delete 2>/dev/null || true
	@find . -name "*.so" -delete 2>/dev/null || true
	@find . -name "*.a" -delete 2>/dev/null || true
	@find . -name "core" -delete 2>/dev/null || true
	@echo -e "${COLOR_GREEN}清理所有文件完成${COLOR_RESET}"

# =============================================================================
# 信息目标
# =============================================================================

.PHONY: info
info:
	@echo -e "${COLOR_BLUE}项目信息:${COLOR_RESET}"
	@echo -e "${COLOR_GREEN}工具版本:${COLOR_RESET}"
	@echo -e "  $(shell $(MAKE) --version 2>/dev/null | head -n 1)"
	@echo -e "  $(shell $(CMAKE) --version 2>/dev/null | head -n 1)"
	@echo -e "  $(shell gcc --version 2>/dev/null | head -n 1)"
	@echo -e "  $(shell g++ --version 2>/dev/null | head -n 1)"
	@echo ""
	@echo -e "${COLOR_GREEN}构建参数:${COLOR_RESET}"
	@echo -e "  构建目录(BUILD_DIR): $(BUILD_DIR)"
	@echo -e "  发布目录(RELEASE_DIR): $(RELEASE_DIR)"
	@echo -e "  并行任务数(JOBS): $(JOBS)"
	@echo -e "  Python解释器(PYTHON): $(PYTHON)"
	@echo -e "  构建类型(CMAKE_BUILD_TYPE): $(CMAKE_BUILD_TYPE)"
	@echo -e "  C++标准(CMAKE_CXX_STANDARD): $(CMAKE_CXX_STANDARD)"
	@echo -e "  ONNX Runtime(ONNX_RUNTIME): $(ONNX_RUNTIME)"
	@echo -e "  策略会话模式(STRATEGY_SESSION_MODE): $(STRATEGY_SESSION_MODE)"
	@echo -e "  强制禁用AVX512(DISABLE_AVX512→CMake): $(if $(filter 1 ON YES yes,$(DISABLE_AVX512)),ON,OFF)"
	@echo ""
	@echo -e "${COLOR_GREEN}目标状态:${COLOR_RESET}"
	@echo -e "  默认构建目标: $(DEFAULT_BUILD)"
	@echo -e "  默认运行目标: $(DEFAULT_RUN)"
	@echo -e "  可用应用:"
	@if [ "$(HAS_APP_LIVE)" = "1" ]; then echo -e "    - app_live"; fi
	@if [ "$(HAS_APP_FACTOR)" = "1" ]; then echo -e "    - app_factor"; fi
	@if [ "$(HAS_APP_MODEL)" = "1" ]; then echo -e "    - app_model"; fi
