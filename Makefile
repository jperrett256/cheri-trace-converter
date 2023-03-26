CXX := g++

SRC_FILES := trace_converter.cc
# OBJ_FILES := $(patsubst %.cc,%.o,$(SRC_FILES))
EXE_FILE := trace_converter

BUILD_DIR := build

COMPILER_FLAGS_COMMON := -std=gnu++11 -Wall -o $(BUILD_DIR)/$(EXE_FILE)

COMPILER_FLAGS_DEBUG := $(COMPILER_FLAGS_COMMON) -g
COMPILER_FLAGS_RELEASE := $(COMPILER_FLAGS_COMMON) -O2

LINKER_FLAGS = -lz -lboost_iostreams

all: debug

debug: build_dir
	$(CXX) $(COMPILER_FLAGS_DEBUG) $(SRC_FILES) $(LINKER_FLAGS)

release: build_dir
	$(CXX) $(COMPILER_FLAGS_RELEASE) $(SRC_FILES) $(LINKER_FLAGS)

build_dir:
	@mkdir -p $(BUILD_DIR)
