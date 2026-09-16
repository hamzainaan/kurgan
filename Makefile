ENGINE   := kurgan
CXX      ?= g++
CXXFLAGS ?=

SRC_DIR   := src
BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj/$(MODE)

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

# ------------------------------------------------------------------
# Platform detection (Unix, macOS, Windows)
# ------------------------------------------------------------------
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(OS),Windows_NT)
  PLATFORM := windows
else ifneq (,$(findstring MINGW,$(UNAME_S)))
  PLATFORM := windows
else ifneq (,$(findstring MSYS,$(UNAME_S)))
  PLATFORM := windows
else ifneq (,$(findstring CYGWIN,$(UNAME_S)))
  PLATFORM := windows
else
  PLATFORM := unix
endif

# ------------------------------------------------------------------
# Compiler detection: g++/clang++ vs MSVC (cl / clang-cl)
# ------------------------------------------------------------------
ifeq ($(CXX),cl)
  COMPILER := msvc
else ifeq ($(CXX),clang-cl)
  COMPILER := msvc
else
  COMPILER := gcc
endif

# ------------------------------------------------------------------
# Build modes
# ------------------------------------------------------------------
MODE ?= release

ifeq ($(COMPILER),msvc)
  EXE      := $(ENGINE).exe
  CPPFLAGS := /I$(SRC_DIR) /nologo /EHsc
  CXXSTD   := /std:c++latest
  WARNINGS := /W3
  MKDIR    := mkdir -p
  DEPS     :=

  ifeq ($(MODE),release)
    OPT := /O2 /DNDEBUG /GL
  else ifeq ($(MODE),debug)
    OPT := /Od /Zi
  else ifeq ($(MODE),profile-gen)
    OPT := /O2 /GL /GENPROFILE
  else ifeq ($(MODE),profile-use)
    OPT := /O2 /GL /USEPROFILE
  else
    $(error Unknown MODE: $(MODE))
  endif

  ALLFLAGS := $(CXXSTD) $(WARNINGS) $(OPT) $(CPPFLAGS) $(CXXFLAGS)
  LDFLAGS  := /link /LTCG
  COMPILE  = $(CXX) $(ALLFLAGS) /c $< /Fo$@
  LINK     = $(CXX) $(OBJS) /Fe$@ $(LDFLAGS)
else
  ifeq ($(PLATFORM),windows)
    EXE := $(ENGINE).exe
  else
    EXE := $(ENGINE)
    LDFLAGS += -pthread
  endif

  CPPFLAGS := -I$(SRC_DIR)
  CXXSTD   := -std=c++23
  WARNINGS := -Wall -Wextra -Wshadow -Wconversion
  LTO      ?= -flto
  MKDIR    := mkdir -p
  DEPS     := $(OBJS:.o=.d)

  TIER ?= native
  ifeq ($(TIER),native)
    ARCHFLAGS := -march=native
  else ifeq ($(TIER),baseline)
    ARCHFLAGS :=
  else ifeq ($(TIER),sse42)
    ARCHFLAGS := -march=x86-64-v2
  else ifeq ($(TIER),avx2)
    ARCHFLAGS := -march=x86-64-v3
  else ifeq ($(TIER),avx512)
    ARCHFLAGS := -march=x86-64-v4
  else ifeq ($(TIER),neoverse)
    ARCHFLAGS := -mcpu=neoverse-n1
  else ifeq ($(TIER),apple)
    ARCHFLAGS := -mcpu=apple-m1
  else
    $(error Unknown TIER: $(TIER))
  endif

  ifeq ($(MODE),release)
    OPT := -O3 -DNDEBUG $(ARCHFLAGS) $(LTO)
  else ifeq ($(MODE),debug)
    OPT := -O0 -g3 -DDEBUG
  else ifeq ($(MODE),profile-gen)
    OPT := -O3 -DNDEBUG $(ARCHFLAGS) -fprofile-generate=$(BUILD_DIR)/pgodata
  else ifeq ($(MODE),profile-use)
    OPT := -O3 -DNDEBUG $(ARCHFLAGS) -flto -fprofile-use=$(BUILD_DIR)/pgodata -fprofile-correction
  else
    $(error Unknown MODE: $(MODE))
  endif

  ALLFLAGS := $(CXXSTD) $(WARNINGS) $(OPT) $(CPPFLAGS) $(CXXFLAGS)
  COMPILE  = $(CXX) $(ALLFLAGS) -MMD -MP -c $< -o $@
  LINK     = $(CXX) $(ALLFLAGS) $(OBJS) -o $@ $(LDFLAGS)
endif

# ------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------
all: $(EXE)

$(EXE): $(OBJS)
	$(LINK)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(COMPILE)

$(OBJ_DIR):
	$(MKDIR) $(OBJ_DIR)

# Profile-guided optimisation flow:
#   1. make MODE=profile-gen
#   2. make bench
#   3. make MODE=profile-use
bench: $(EXE)
	printf 'bench\nquit\n' | ./$(EXE)

test: $(EXE)
	printf 'perft suite\nbench\nquit\n' | ./$(EXE)

clean:
	rm -rf $(BUILD_DIR) $(ENGINE) $(ENGINE).exe

.PHONY: all bench test clean
-include $(DEPS)
