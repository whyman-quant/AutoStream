# raw_data_reader：本地逐笔/行情文件读取库（静态库 libraw_data_reader_<backend>.a，无 main）；演示见 ../examples/。
#
# 默认值（命令行可覆盖，便于父工程或 CI 一层层传入）：
#   BUILD_DIR_REL ?= build     — 相对「仓库根」的产物根目录（与 ../examples、../tests 中同名变量应对齐）
#   READER_BACKEND ?= cpp      — cpp | python | both，须与 backend_config.h 顶部宏、各 .cc 中 #if 一致
#   CXX_STD ?= c++11           — C++ 标准版本（默认 c++11；可手动改为 c++14/c++17）
#
# 嵌入 Python（python / both）时与 origin/Makefile 一致：必须用 CPython 3.8.x（默认命令名 python3.8），
# 且 python3.8-config 可用；可覆盖：make PYTHON3_8=/path/to/python3.8 PYTHON3_8_CONFIG=/path/to/python3.8-config
#
# 拷贝本目录到其它项目时：只改本 Makefile（或 CMakeLists.txt）里的默认值即可；亦可始终用命令行：
#   make BUILD_DIR_REL=out READER_BACKEND=cpp

REPO_ROOT := $(abspath ..)
BUILD_DIR_REL ?= build
READER_BACKEND ?= cpp
CXX_STD ?= c++11
PYTHON3_8 ?= python3.8
PYTHON3_8_CONFIG ?= python3.8-config

MAIN_DIR = .
TYPES_DIR = ./raw_data_types
# 编译只 -I 本目录；头文件写 raw_data_types/xxx.h（与 CMakeLists PUBLIC include 一致）
BUILD_ROOT := $(REPO_ROOT)/$(BUILD_DIR_REL)
OBJDIR := $(BUILD_ROOT)/$(READER_BACKEND)
STATIC_LIB := $(OBJDIR)/libraw_data_reader_$(READER_BACKEND).a
AR := ar

# ---------- 后端分支：决定编译哪些 .cc、定义哪条 READER_BACKEND_* 宏；须与 backend_config.h 中 #if 一致 ----------
ifeq ($(READER_BACKEND),cpp)
  BACKEND_DEFINE := -DREADER_BACKEND_CPP_ONLY
  LIB_SOURCES := merge_sort.cc data_path_config.cc ticks_data.cc ticks_data_io_cpp.cc
  LDFLAGS_LIB :=
  CXXFLAGS_EXTRA :=
else ifeq ($(READER_BACKEND),python)
  BACKEND_DEFINE := -DREADER_BACKEND_PYTHON_ONLY
  LIB_SOURCES := merge_sort.cc data_path_config.cc ticks_data.cc ticks_data_io_python.cc my_data.cc
  PY_INCLUDE := $(shell $(PYTHON3_8) -c 'import sysconfig; print(sysconfig.get_path("include"))' 2>/dev/null)
  NUMPY_PATH := $(shell $(PYTHON3_8) -c 'import numpy;print(numpy.get_include())' 2>/dev/null)
  PYCFLAGS := $(filter-out -Wstrict-prototypes,$(shell $(PYTHON3_8_CONFIG) --cflags 2>/dev/null))
  CXXFLAGS_EXTRA := -I$(PY_INCLUDE) -I$(NUMPY_PATH) $(PYCFLAGS)
  LDFLAGS_LIB :=
else ifeq ($(READER_BACKEND),both)
  BACKEND_DEFINE := -DREADER_BACKEND_BOTH
  LIB_SOURCES := merge_sort.cc data_path_config.cc ticks_data.cc ticks_data_io_cpp.cc ticks_data_io_python.cc my_data.cc
  PY_INCLUDE := $(shell $(PYTHON3_8) -c 'import sysconfig; print(sysconfig.get_path("include"))' 2>/dev/null)
  NUMPY_PATH := $(shell $(PYTHON3_8) -c 'import numpy;print(numpy.get_include())' 2>/dev/null)
  PYCFLAGS := $(filter-out -Wstrict-prototypes,$(shell $(PYTHON3_8_CONFIG) --cflags 2>/dev/null))
  CXXFLAGS_EXTRA := -I$(PY_INCLUDE) -I$(NUMPY_PATH) $(PYCFLAGS)
  LDFLAGS_LIB :=
else
  $(error READER_BACKEND must be cpp, python, or both (got $(READER_BACKEND)))
endif

# ---------- 嵌入 Python 时的环境校验：保证头文件/链接标志与 3.8 一致；目标为 help 时跳过，便于只看说明 ----------
# python / both：强制 CPython 3.8.x（与 origin/Makefile 使用 python3.8 的约定一致），避免误用 3.9+ 链接嵌入 API
ifneq ($(READER_BACKEND),cpp)
  ifneq ($(filter help,$(MAKECMDGOALS)),help)
    ifeq ($(shell $(PYTHON3_8) -c 'import sys; assert sys.version_info[:2]==(3,8); import numpy,sysconfig' 2>/dev/null && echo ok),)
      $(error READER_BACKEND=$(READER_BACKEND) 需要 $(PYTHON3_8) 为 CPython 3.8.x 且已安装 numpy；并需 $(PYTHON3_8_CONFIG)。见 README.md。)
    endif
    ifeq ($(shell command -v $(PYTHON3_8_CONFIG) >/dev/null 2>&1 && echo ok),)
      $(error 未找到 $(PYTHON3_8_CONFIG)。请安装 python3.8 开发包或设置 PYTHON3_8_CONFIG。)
    endif
    ifeq ($(strip $(PY_INCLUDE)),)
      $(error 无法取得 Python include 路径（$(PYTHON3_8) sysconfig）。)
    endif
    ifeq ($(strip $(NUMPY_PATH)),)
      $(error 无法取得 numpy 头路径（$(PYTHON3_8) import numpy）。)
    endif
    ifeq ($(strip $(PYCFLAGS)),)
      $(error $(PYTHON3_8_CONFIG) --cflags 为空，请检查是否安装 python3.8 开发头文件。)
    endif
  endif
endif

# ---------- 编译命令：始终 -I 本目录；源码里用 "raw_data_types/xxx.h"，不把 raw_data_types 单独加进 -I ----------
CXX = g++
CFLAGS := -fPIC $(CXXFLAGS_EXTRA) -c -Wall -g -std=$(CXX_STD) -pthread
CFLAGS += -I$(MAIN_DIR)
CFLAGS += $(BACKEND_DEFINE)
CXXFLAGS = $(CFLAGS)

HEADS := $(wildcard $(MAIN_DIR)/*.h) $(wildcard $(TYPES_DIR)/*.h)
LIB_OBJS := $(addprefix $(OBJDIR)/,$(LIB_SOURCES:.cc=.o))

.PHONY: all clean help

all: $(STATIC_LIB)

help:
	@echo "raw_data_reader — 静态库 libraw_data_reader_<backend>.a"
	@echo ""
	@echo "目标:"
	@echo "  all (默认)     编译 $(STATIC_LIB)"
	@echo "  clean          删除 $(BUILD_ROOT)/{cpp,python,both}"
	@echo "  help           本说明"
	@echo ""
	@echo "变量（命令行覆盖）:"
	@echo "  READER_BACKEND=cpp|python|both  默认 cpp；与 backend_config.h 一致"
	@echo "  BUILD_DIR_REL=build             相对仓库根的产物根"
	@echo "  CXX_STD=c++11                   C++ 标准版本（默认 c++11，可改 c++14/c++17）"
	@echo "  PYTHON3_8 / PYTHON3_8_CONFIG    python|both 时使用，默认 python3.8 / python3.8-config"
	@echo ""
	@echo "示例:"
	@echo "  make -C raw_data_reader READER_BACKEND=cpp"

# 将当前 backend 目录下全部 .o 打入静态库，供外部 -L/-l 或拆出 .o 链接
$(STATIC_LIB): $(LIB_OBJS) | $(OBJDIR)
	$(AR) rcs $@ $(LIB_OBJS)

$(OBJDIR)/%.o: %.cc $(HEADS) | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(BUILD_ROOT)/cpp $(BUILD_ROOT)/python $(BUILD_ROOT)/both
	rm -rf $(MAIN_DIR)/build
	rm -f main_cpp main_python main_both
