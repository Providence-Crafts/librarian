# ==============================================================================
# Librarian: Pure C99 Embedded RAG System Makefile
# ==============================================================================

CC ?= clang
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

INCLUDES = \
	-I$(INC_DIR) \
	-isystem $(VENDOR_DIR)/sqlite \
	-isystem $(VENDOR_DIR)/sqlite-vec \
	-isystem $(VENDOR_DIR)/tomlc99 \
	-isystem $(VENDOR_DIR)/llama.cpp/include \
	-isystem $(VENDOR_DIR)/llama.cpp/ggml/include

# Vendor static libraries & objects
VENDOR_OBJS = \
	$(BUILD_DIR)/vendor/sqlite3.o \
	$(BUILD_DIR)/vendor/sqlite-vec.o \
	$(BUILD_DIR)/vendor/toml.o

LLAMA_BUILD_DIR = $(VENDOR_DIR)/llama.cpp/build
LLAMA_LIBS = \
	$(LLAMA_BUILD_DIR)/src/libllama.a \
	$(LLAMA_BUILD_DIR)/ggml/src/libggml.a \
	$(LLAMA_BUILD_DIR)/ggml/src/libggml-base.a \
	$(LLAMA_BUILD_DIR)/ggml/src/libggml-cpu.a

SYS_LIBS = -lstdc++ -lm -lpthread -ldl -fopenmp

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

.PHONY: all clean release debug asan tsan test valgrind tidy cppcheck format compdb watch help

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

# Header dependencies
-include $(OBJS:.o=.d)
-include $(TEST_OBJS:.o=.d)

# ------------------------------------------------------------------------------
# Test Suite Target
# ------------------------------------------------------------------------------

test: $(BUILD_DIR)/tests/test_runner
	@echo "\n🧪 Running test suite..."
	@ASAN_OPTIONS="detect_leaks=1:abort_on_error=1" ./$(BUILD_DIR)/tests/test_runner

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

tidy:
	@echo "🧹 Running clang-tidy analysis..."
	clang-tidy $(SRCS) -- $(INCLUDES) $(DEFINES) $(STD) $(NIX_CFLAGS_COMPILE)

cppcheck:
	@echo "🛡️ Running cppcheck..."
	cppcheck --enable=warning,style,performance,portability \
	         --suppress=missingIncludeSystem \
	         --suppress=checkersReport \
	         --suppress=unusedFunction \
	         --suppress='*:vendor/*' \
	         --error-exitcode=1 \
	         -I$(INC_DIR) -I$(VENDOR_DIR)/sqlite -I$(VENDOR_DIR)/sqlite-vec -I$(VENDOR_DIR)/tomlc99 -I$(VENDOR_DIR)/llama.cpp/include -I$(VENDOR_DIR)/llama.cpp/ggml/include \
	         $(SRC_DIR) $(INC_DIR)

format:
	@echo "✨ Formatting source code..."
	clang-format -i $(wildcard $(SRC_DIR)/*.c) $(wildcard $(INC_DIR)/*.h) $(wildcard $(TEST_DIR)/*.c) $(wildcard $(TEST_DIR)/*.h)

compdb: clean
	@echo "📝 Generating compile_commands.json via bear..."
	bear -- make debug

watch:
	watchexec -e c,h -c -- make test

# ------------------------------------------------------------------------------
# Housekeeping
# ------------------------------------------------------------------------------

$(BUILD_DIR) $(BIN_DIR) $(BUILD_DIR)/tests $(BUILD_DIR)/vendor:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)/*.o $(BUILD_DIR)/*.d $(BUILD_DIR)/tests $(BIN_DIR) compile_commands.json

distclean: clean
	rm -rf $(BUILD_DIR)

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
	@echo "  make compdb     - Generate compile_commands.json for clangd"
	@echo "  make watch      - Automatically rerun tests on change"
