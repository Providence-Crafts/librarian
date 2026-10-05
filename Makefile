# ==============================================================================
# Librarian: Pure C99 Embedded RAG System Makefile
# ==============================================================================

# `CC ?=` does not work here: make predefines CC, so ?= never fires.
# Only override when the value is make's own default.
# clang when it can link -fopenmp, then gcc, then cc, so a
# plain Ubuntu or MSYS2 install builds without extra flags. The link probe
# matters: Ubuntu's clang without libomp-dev compiles fine but fails at -lomp.
OPENMP_PROBE = echo 'int main(void){return 0;}' | $(1) -fopenmp -x c - -o /dev/null 2>/dev/null && echo $(1)
ifeq ($(origin CC),default)
  CC = $(firstword $(foreach c,clang gcc,$(shell $(call OPENMP_PROBE,$(c)))) cc)
endif
STD = -std=c99

TARGET_NAME = librarian
BUILD_DIR = build
BIN_DIR = bin
SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
VENDOR_DIR = vendor

# Strict Warning Flags for librarian source code
WARNING_FLAGS = \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wshadow \
	-Wconversion \
	-Wsign-conversion \
	-Wnull-dereference \
	-Wdouble-promotion \
	-Wformat=2 \
	-Wformat-security \
	-Wundef \
	-Wstrict-prototypes \
	-Wmissing-prototypes \
	-Wredundant-decls \
	-Wmissing-declarations \
	-Wcast-align \
	-Wcast-qual \
	-Wwrite-strings

# `make WARNINGS_AS_ERRORS=1` (the gate does) turns every warning into an error.
ifeq ($(WARNINGS_AS_ERRORS),1)
WARNING_FLAGS += -Werror
endif

INCLUDES = \
	-I$(INC_DIR) \
	-isystem $(VENDOR_DIR)/sqlite \
	-isystem $(VENDOR_DIR)/sqlite-vec \
	-isystem $(VENDOR_DIR)/tomlc99 \
	-isystem $(VENDOR_DIR)/miniz \
	-isystem $(VENDOR_DIR)/llama.cpp/include \
	-isystem $(VENDOR_DIR)/llama.cpp/ggml/include

# Vendor static libraries & objects
VENDOR_OBJS = \
	$(BUILD_DIR)/vendor/sqlite3.o \
	$(BUILD_DIR)/vendor/sqlite-vec.o \
	$(BUILD_DIR)/vendor/toml.o \
	$(BUILD_DIR)/vendor/miniz.o

LLAMA_BUILD_DIR = $(VENDOR_DIR)/llama.cpp/build
LLAMA_LIBS = \
	$(LLAMA_BUILD_DIR)/src/libllama.a \
	$(LLAMA_BUILD_DIR)/ggml/src/libggml.a \
	$(LLAMA_BUILD_DIR)/ggml/src/libggml-base.a \
	$(LLAMA_BUILD_DIR)/ggml/src/libggml-cpu.a

SYS_LIBS = -lstdc++ -lm -lpthread -ldl -fopenmp

# Release binaries: link the GCC runtime (libstdc++, libgomp, libgcc)
# statically so the executable needs only glibc. gcc only; clang's OpenMP
# runtime is libomp.
ifeq ($(STATIC_RUNTIME),1)
SYS_LIBS = -static-libgcc -Wl,-Bstatic -lstdc++ -lgomp -Wl,-Bdynamic -lm -lpthread -ldl
endif

# Vulkan backend support
ifneq ($(wildcard $(LLAMA_BUILD_DIR)/ggml/src/ggml-vulkan/libggml-vulkan.a),)
# 1. Try pkg-config
VULKAN_LDFLAGS ?= $(shell pkg-config --libs vulkan 2>/dev/null)
# 2. Try direct compiler link test
ifeq ($(strip $(VULKAN_LDFLAGS)),)
VULKAN_LDFLAGS := $(shell $(CC) -lvulkan -x c -shared /dev/null -o /dev/null 2>/dev/null && echo "-lvulkan")
endif
# 3. Fallback for NixOS outside nix develop: search /nix/store for vulkan-loader
ifeq ($(strip $(VULKAN_LDFLAGS)),)
NIX_VULKAN_DIR := $(lastword $(sort $(wildcard /nix/store/*-vulkan-loader-*/lib)))
ifneq ($(NIX_VULKAN_DIR),)
VULKAN_LDFLAGS := -L$(NIX_VULKAN_DIR) -Wl,-rpath,$(NIX_VULKAN_DIR) -lvulkan
endif
endif

ifneq ($(strip $(VULKAN_LDFLAGS)),)
LLAMA_LIBS += $(LLAMA_BUILD_DIR)/ggml/src/ggml-vulkan/libggml-vulkan.a
SYS_LIBS += $(VULKAN_LDFLAGS)
endif
endif

# Application sources & objects
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/tests/%.o, $(TEST_SRCS))

DEFINES = -D_GNU_SOURCE -DSQLITE_CORE=1 -DSQLITE_VEC_STATIC=1

# Base flags
CFLAGS := $(STD) $(WARNING_FLAGS) $(INCLUDES) $(DEFINES) -O3 -DNDEBUG -march=native
LDFLAGS :=

# Vendor compile flags (relaxed warnings for 3rd-party code)
VENDOR_CFLAGS = -std=c99 -O3 -isystem $(VENDOR_DIR)/sqlite -isystem $(VENDOR_DIR)/sqlite-vec -isystem $(VENDOR_DIR)/tomlc99 -DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_NORMALIZE -DSQLITE_ENABLE_FTS5 $(DEFINES)

.PHONY: all clean release debug asan tsan test valgrind tidy cppcheck format format-check gate logo compdb watch windows help

all: release

# ------------------------------------------------------------------------------
# Build Targets
# ------------------------------------------------------------------------------

release: CFLAGS = $(STD) $(WARNING_FLAGS) $(INCLUDES) $(DEFINES) -O3 -DNDEBUG
release: $(BIN_DIR)/$(TARGET_NAME)

debug: CFLAGS = $(STD) $(WARNING_FLAGS) $(INCLUDES) $(DEFINES) -Og -g3 -DDEBUG
debug: $(BIN_DIR)/$(TARGET_NAME)

asan: CFLAGS = $(STD) $(WARNING_FLAGS) $(INCLUDES) $(DEFINES) -fsanitize=address,undefined -fno-omit-frame-pointer -g3 -O1 -DDEBUG
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(BIN_DIR)/$(TARGET_NAME)

tsan: CFLAGS = $(STD) $(WARNING_FLAGS) $(INCLUDES) $(DEFINES) -fsanitize=thread -fno-omit-frame-pointer -g3 -O1 -DDEBUG
tsan: LDFLAGS += -fsanitize=thread
tsan: $(BIN_DIR)/$(TARGET_NAME)

# Windows MinGW Cross-Compilation Target
# Natively under MSYS2 the toolchain is plain gcc; elsewhere it is the cross one.
ifeq ($(OS),Windows_NT)
WIN_CC ?= gcc
WIN_CXX ?= g++
else
WIN_CC ?= x86_64-w64-mingw32-gcc
WIN_CXX ?= x86_64-w64-mingw32-g++
endif
WIN_BUILD_DIR = $(BUILD_DIR)/win
# C99 printf (%zu and friends) from the MinGW runtime rather than msvcrt's.
WIN_DEFINES = -D__USE_MINGW_ANSI_STDIO=1
WIN_OBJS = $(patsubst $(SRC_DIR)/%.c, $(WIN_BUILD_DIR)/%.o, $(SRCS))
WIN_VENDOR_OBJS = \
	$(WIN_BUILD_DIR)/vendor/sqlite3.o \
	$(WIN_BUILD_DIR)/vendor/sqlite-vec.o \
	$(WIN_BUILD_DIR)/vendor/toml.o \
	$(WIN_BUILD_DIR)/vendor/miniz.o
WIN_LLAMA_DIR = $(VENDOR_DIR)/llama.cpp/build-win
WIN_LLAMA_LIBS = \
	$(WIN_LLAMA_DIR)/src/libllama.a \
	$(WIN_LLAMA_DIR)/ggml/src/ggml.a \
	$(WIN_LLAMA_DIR)/ggml/src/ggml-base.a \
	$(WIN_LLAMA_DIR)/ggml/src/ggml-cpu.a

windows: $(BIN_DIR)/$(TARGET_NAME).exe

$(BIN_DIR)/$(TARGET_NAME).exe: $(WIN_OBJS) $(WIN_VENDOR_OBJS) $(WIN_LLAMA_LIBS) | $(BIN_DIR)
	$(WIN_CC) -static $(WIN_OBJS) $(WIN_VENDOR_OBJS) $(WIN_LLAMA_LIBS) -o $@ -lstdc++ -lm -lpthread

$(WIN_BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(WIN_BUILD_DIR)
	$(WIN_CC) $(STD) $(WARNING_FLAGS) $(INCLUDES) $(DEFINES) $(WIN_DEFINES) -O3 -DNDEBUG -MMD -MP -c $< -o $@

$(WIN_BUILD_DIR)/vendor/sqlite3.o: $(VENDOR_DIR)/sqlite/sqlite3.c | $(WIN_BUILD_DIR)/vendor
	$(WIN_CC) $(VENDOR_CFLAGS) -c $< -o $@

$(WIN_BUILD_DIR)/vendor/sqlite-vec.o: $(VENDOR_DIR)/sqlite-vec/sqlite-vec.c | $(WIN_BUILD_DIR)/vendor
	$(WIN_CC) $(VENDOR_CFLAGS) -c $< -o $@

$(WIN_BUILD_DIR)/vendor/toml.o: $(VENDOR_DIR)/tomlc99/toml.c | $(WIN_BUILD_DIR)/vendor
	$(WIN_CC) $(VENDOR_CFLAGS) -c $< -o $@

$(WIN_BUILD_DIR)/vendor/miniz.o: $(VENDOR_DIR)/miniz/miniz_all.c | $(WIN_BUILD_DIR)/vendor
	$(WIN_CC) $(VENDOR_CFLAGS) -I$(VENDOR_DIR)/miniz -c $< -o $@

$(WIN_LLAMA_LIBS):
	@mkdir -p $(WIN_LLAMA_DIR)
	cmake -B $(WIN_LLAMA_DIR) -S $(VENDOR_DIR)/llama.cpp -G Ninja \
	  -DCMAKE_SYSTEM_NAME=Windows \
	  -DCMAKE_C_COMPILER=$(WIN_CC) \
	  -DCMAKE_CXX_COMPILER=$(WIN_CXX) \
	  -DGGML_BUILD_EXAMPLES=OFF \
	  -DGGML_BUILD_TESTS=OFF \
	  -DLLAMA_BUILD_EXAMPLES=OFF \
	  -DLLAMA_BUILD_TESTS=OFF \
	  -DLLAMA_BUILD_SERVER=OFF \
	  -DGGML_VULKAN=OFF \
	  -DBUILD_SHARED_LIBS=OFF
	ninja -C $(WIN_LLAMA_DIR) llama ggml ggml-base ggml-cpu

$(BIN_DIR)/$(TARGET_NAME): $(OBJS) $(VENDOR_OBJS) $(LLAMA_LIBS) | $(BIN_DIR)
	$(CC) $(OBJS) $(VENDOR_OBJS) $(LLAMA_LIBS) -o $@ $(LDFLAGS) $(SYS_LIBS)

# Application compilation
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Vendor compilation
$(BUILD_DIR)/vendor/sqlite3.o: $(VENDOR_DIR)/sqlite/sqlite3.c | $(BUILD_DIR)/vendor
	$(CC) $(VENDOR_CFLAGS) -c $< -o $@

$(BUILD_DIR)/vendor/sqlite-vec.o: $(VENDOR_DIR)/sqlite-vec/sqlite-vec.c | $(BUILD_DIR)/vendor
	$(CC) $(VENDOR_CFLAGS) -c $< -o $@

$(BUILD_DIR)/vendor/toml.o: $(VENDOR_DIR)/tomlc99/toml.c | $(BUILD_DIR)/vendor
	$(CC) $(VENDOR_CFLAGS) -c $< -o $@

$(BUILD_DIR)/vendor/miniz.o: $(VENDOR_DIR)/miniz/miniz_all.c | $(BUILD_DIR)/vendor
	$(CC) $(VENDOR_CFLAGS) -I$(VENDOR_DIR)/miniz -c $< -o $@

# Header dependencies
-include $(OBJS:.o=.d)
-include $(TEST_OBJS:.o=.d)

# ------------------------------------------------------------------------------
# Test Suite Target
# ------------------------------------------------------------------------------

test: $(BUILD_DIR)/tests/test_runner
	@echo "\n🧪 Running test suite..."
	@ASAN_OPTIONS="detect_leaks=1:abort_on_error=1" LSAN_OPTIONS="suppressions=lsan.supp" ./$(BUILD_DIR)/tests/test_runner

$(BUILD_DIR)/tests/test_runner: LDFLAGS += -fsanitize=address,undefined
$(BUILD_DIR)/tests/test_runner: $(filter-out $(BUILD_DIR)/main.o, $(OBJS)) $(TEST_OBJS) $(VENDOR_OBJS) $(LLAMA_LIBS) | $(BUILD_DIR)/tests
	$(CC) $(filter-out $(BUILD_DIR)/main.o, $(OBJS)) $(TEST_OBJS) $(VENDOR_OBJS) $(LLAMA_LIBS) -o $@ $(LDFLAGS) $(SYS_LIBS)

$(BUILD_DIR)/tests/%.o: CFLAGS = $(STD) $(WARNING_FLAGS) $(INCLUDES) $(DEFINES) -fsanitize=address,undefined -fno-omit-frame-pointer -g3 -O1 -DDEBUG
$(BUILD_DIR)/tests/%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)/tests
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# ------------------------------------------------------------------------------
# Quality & Static Analysis Tools
# ------------------------------------------------------------------------------

valgrind: debug
	@echo "🔍 Running Valgrind leak check..."
	valgrind --leak-check=full \
	         --show-leak-kinds=all \
	         --track-origins=yes \
	         --verbose \
	         --error-exitcode=1 \
	         ./$(BIN_DIR)/$(TARGET_NAME) --version

# Under nix, the cc-wrapper injects glibc's include directory but clang-tidy
# runs the unwrapped clang, so <stdio.h> goes missing. Ask the compiler where
# its headers are. -U_FORTIFY_SOURCE silences glibc's #warning at -O0.
TIDY_SYS_INCLUDES = $(shell $(CC) -E -Wp,-v -xc /dev/null 2>&1 | \
	sed -n 's|^ \(/[^ ]*\)$$|--extra-arg-before=-isystem\1|p')
TIDY_EXTRA = --extra-arg=-U_FORTIFY_SOURCE

tidy:
	clang-tidy --quiet --warnings-as-errors='*' $(TIDY_SYS_INCLUDES) $(TIDY_EXTRA) $(SRCS) $(TEST_SRCS) -- \
	           $(STD) $(INCLUDES) $(DEFINES) -I$(TEST_DIR)

cppcheck:
	@echo "🛡️ Running cppcheck..."
	cppcheck --enable=warning,style,performance,portability \
	         --suppress=missingIncludeSystem \
	         --suppress=checkersReport \
	         --suppress=unusedFunction \
	         --suppress='*:vendor/*' \
	         --error-exitcode=1 --std=c99 --inline-suppr \
	         -I$(INC_DIR) -I$(VENDOR_DIR)/sqlite -I$(VENDOR_DIR)/sqlite-vec -I$(VENDOR_DIR)/tomlc99 -I$(VENDOR_DIR)/llama.cpp/include -I$(VENDOR_DIR)/llama.cpp/ggml/include \
	         $(SRC_DIR) $(INC_DIR)

FORMAT_SRCS = $(wildcard $(SRC_DIR)/*.c $(SRC_DIR)/*.h $(INC_DIR)/*.h $(TEST_DIR)/*.c $(TEST_DIR)/*.h tools/*.c)

format:
	@echo "✨ Formatting source code..."
	clang-format -i $(FORMAT_SRCS)

format-check:
	@clang-format --dry-run --Werror $(FORMAT_SRCS)

# docs/logo.svg is generated from the banner art in src/brand_art.h.
logo: docs/logo.svg

docs/logo.svg: tools/logo.c $(SRC_DIR)/brand_art.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(STD) -Wall -Wextra -o $(BUILD_DIR)/logo tools/logo.c
	./$(BUILD_DIR)/logo > $@

# The single definition of done (docs/family.md): run it in `nix develop`.
gate:
	@echo "== gate: format ==";   $(MAKE) --no-print-directory format-check
	@echo "== gate: build ==";    $(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory release WARNINGS_AS_ERRORS=1
	@echo "== gate: tests (asan+ubsan) =="; $(MAKE) --no-print-directory test WARNINGS_AS_ERRORS=1
	@echo "== gate: cppcheck =="; $(MAKE) --no-print-directory cppcheck
	@echo "== gate: clang-tidy =="; $(MAKE) --no-print-directory tidy
	@echo "PASS"

compdb: clean
	@echo "📝 Generating compile_commands.json via bear..."
	bear -- make debug

watch:
	watchexec -e c,h -c -- make test

# ------------------------------------------------------------------------------
# Housekeeping
# ------------------------------------------------------------------------------

$(BUILD_DIR) $(BIN_DIR) $(BUILD_DIR)/tests $(BUILD_DIR)/vendor $(WIN_BUILD_DIR) $(WIN_BUILD_DIR)/vendor:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)/*.o $(BUILD_DIR)/*.d $(BUILD_DIR)/tests $(BUILD_DIR)/win $(BIN_DIR) compile_commands.json

distclean: clean
	rm -rf $(BUILD_DIR) $(WIN_LLAMA_DIR)

help:
	@echo "Librarian Build System"
	@echo "  make (release)  - Compile optimized release binary"
	@echo "  make debug      - Compile debug binary with symbols"
	@echo "  make asan       - Compile with AddressSanitizer & UBSan"
	@echo "  make tsan       - Compile with ThreadSanitizer"
	@echo "  make test       - Build and execute unit tests (ASan enabled)"
	@echo "  make valgrind   - Run memory leak audit via Valgrind"
	@echo "  make tidy       - Run clang-tidy static analysis"
	@echo "  make cppcheck   - Run cppcheck static analyzer"
	@echo "  make format     - Auto-format code with clang-format"
	@echo "  make logo       - Regenerate docs/logo.svg from src/brand_art.h"
	@echo "  make gate       - Format check, -Werror build, tests, cppcheck, clang-tidy"
	@echo "  make compdb     - Generate compile_commands.json for clangd"
	@echo "  make watch      - Automatically rerun tests on change"
	@echo "  make windows    - Cross-compile Windows binary (bin/librarian.exe)"

