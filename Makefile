CXX := g++

BUILD_DIR := build

SRC_FILES := $(wildcard src/*.cc)
EXE_FILE := tracesim

INC_DIR := inc

COMPILER_FLAGS_COMMON := -std=gnu++11 -Wall -o $(BUILD_DIR)/$(EXE_FILE) -I$(INC_DIR)/

COMPILER_FLAGS_DEBUG := $(COMPILER_FLAGS_COMMON) -g
COMPILER_FLAGS_RELEASE := $(COMPILER_FLAGS_COMMON) -O3

LINKER_FLAGS = -lz

# all: debug
all: release

debug: build_dir
	$(CXX) $(COMPILER_FLAGS_DEBUG) $(SRC_FILES) $(LINKER_FLAGS)

release: build_dir
	$(CXX) $(COMPILER_FLAGS_RELEASE) $(SRC_FILES) $(LINKER_FLAGS)

build_dir:
	@mkdir -p $(BUILD_DIR)

clean:
	@rm -rf build
