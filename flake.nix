{
  description = "Standard Modern C Development Environment with Sanitizers, LSP, and Tooling";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        # Sanitizer & debugging environment flags
        # ASAN_OPTIONS: Configure AddressSanitizer behavior
        # UBSAN_OPTIONS: Configure UndefinedBehaviorSanitizer behavior
        asanOptions = "detect_leaks=1:abort_on_error=1:symbolize=1:check_initialization_order=true:detect_stack_use_after_return=true";
        ubsanOptions = "print_stacktrace=1:halt_on_error=1";
      in
      {
        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            # Compilers & Toolchains
            clang                  # LLVM C Compiler
            llvmPackages.llvm      # LLVM core tools & symbolizer (llvm-symbolizer for ASan traces)

            # Language Server, Formatting & Linting
            clang-tools            # Contains clangd (LSP), clang-format, clang-tidy
            cppcheck               # Advanced static analyzer for C/C++

            # Debugging & Memory Profiling
            gdb                    # GNU Debugger
            lldb                   # LLVM Debugger
            valgrind               # Comprehensive memory, cache, and thread profiler

            # Build Systems & Compilation Database Generators
            gnumake                # Standard GNU Make
            cmake                  # Cross-platform build system
            meson                  # High-speed modern build system
            ninja                  # Low-level build runner
            bear                   # Generates compile_commands.json for clangd from make
            pkg-config             # Package config helper

            # Watchers & Workflow Automation
            watchexec              # File watcher for auto-compilation & continuous testing
            entr                   # Unix-philosophy file watcher
            shaderc                # GLSL shader compiler (glslc) for Vulkan compute
          ];

          buildInputs = with pkgs; [
            # Standard C development libraries
            zlib                   # Compression library
            openssl                # Cryptography & TLS
            llvmPackages.openmp    # OpenMP support for Clang
            vulkan-headers         # Vulkan API headers
            vulkan-loader          # Vulkan ICD loader
          ];

          # Environment variables exported inside development shell
          env = {
            # Default compiler preference (can be overridden with CC=gcc or CC=clang)
            CC = "clang";

            # Ensure AddressSanitizer and UBSan symbols are properly resolved in backtraces
            ASAN_SYMBOLIZER_PATH = "${pkgs.llvmPackages.llvm}/bin/llvm-symbolizer";
            ASAN_OPTIONS = asanOptions;
            UBSAN_OPTIONS = ubsanOptions;

            # Common strict compilation flags (C23 / C17 standard, warnings as errors)
            CFLAGS = "-std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wfloat-equal -Wundef -Wformat=2";
          };

          shellHook = ''
            echo "⚡ Modern C Development Environment Loaded"
            echo "   • Compiler (Clang): $(clang --version | head -n1)"
            echo "   • Compiler (GCC):   $(gcc --version | head -n1)"
            echo "   • LSP (clangd):     $(clangd --version | head -n1)"
            echo "   • Static Analysis:  $(cppcheck --version)"
            echo "   • Debugger (GDB):   $(gdb --version | head -n1)"
            echo "   • Valgrind:         $(valgrind --version)"
            echo ""
            echo "🔧 Available Make Targets:"
            echo "   make asan      -> Build with AddressSanitizer & UBSan (recommended for dev)"
            echo "   make test      -> Build and run test suite with ASan"
            echo "   make valgrind  -> Run memory leak checks with Valgrind"
            echo "   make tidy      -> Run clang-tidy static analysis"
            echo "   make format    -> Auto-format source code with clang-format"
            echo "   make compdb    -> Generate compile_commands.json for clangd LSP"
            echo "   make watch     -> Continuously re-compile & test on file save"
          '';
        };

        devShells.windows = pkgs.pkgsCross.mingwW64.mkShell {
          nativeBuildInputs = [
            pkgs.gnumake
            pkgs.cmake
            pkgs.ninja
          ];
          buildInputs = [
            pkgs.pkgsCross.mingwW64.windows.pthreads
          ];
          shellHook = ''
            echo "⚡ Windows Cross-Compilation Environment (MinGW-w64) Loaded"
            echo "   • Compiler: $(${pkgs.pkgsCross.mingwW64.stdenv.cc}/bin/x86_64-w64-mingw32-gcc --version | head -n1)"
            echo "   Run 'make windows' to build bin/librarian.exe"
          '';
        };
      }
    );
}
