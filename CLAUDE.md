# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

GluonSock is a minimal SOCKS5 proxy implementation for Windows designed for C2 scenarios. The codebase uses obfuscated compilation for production builds and includes comprehensive unit testing with mocked Windows APIs.

## Build System Architecture

### Dual Build Modes

The project has **two distinct build modes** controlled by `DEBUG_BUILD`:

1. **Production/Release** (`DEBUG_BUILD=OFF`):
   - Uses Polaris obfuscator: `/opt/Polaris-Obfuscator/src/build/bin/clang` with LLVM passes (`-mllvm -passes=fla,mba,indcall,bcf,sub`)
   - Freestanding: `-nostdlib -nostartfiles -ffreestanding -fno-builtin`
   - No stdlib, no CRT, only kernel32 + ws2_32
   - All debug logging disabled (`_DEBUG=0`)
   - Output: `gluonsock_release.exe`

2. **Debug** (`DEBUG_BUILD=ON`):
   - Standard clang (no obfuscation)
   - With stdlib: `-lmsvcrt -lkernel32 -lws2_32`
   - Debug logging enabled (`_DEBUG=1`)
   - Output: `gluonsock_debug.exe`

### Commands

```bash
# Production build (obfuscated, no stdlib)
mkdir -p build && cd build
cmake -DDEBUG_BUILD=OFF ..
make

# Debug build (with symbols and logging)
cmake -DDEBUG_BUILD=ON ..
make

# Test suite (separate build system)
mkdir -p test/build && cd test/build
cmake ..
make
wine test_socks.exe --gtest_color=no

# Run single test
wine test_socks.exe --gtest_filter="*TestName*" --gtest_color=no

# Run all tests via CTest
make test
```

## Code Architecture

### Core Components

**socks/src/socks.c** (456 lines) - Main SOCKS5 implementation
- Stateful context-based design (`PGS_SOCKS_CONTEXT`)
- Connection multiplexing via linked list (max 100 concurrent)
- Key functions:
  - `socks_parse_data()` - Entry point: handles greeting + command dispatch
  - `socks_open_conn()` - Command dispatcher (CONNECT/BIND/UDP)
  - `socks_connect()` - CONNECT handler (IPv4/Domain/IPv6)
  - `socks_create_conn()` - Socket creation with non-blocking connect + timeout
  - `socks_recv_data()` - Data reception with 512KB buffer
  - `socks_find_connection()` / `socks_remove()` - Connection lifecycle

**Connection State Machine:**
1. Client sends greeting → `socks_parse_data()` detects no existing connection
2. If `data_len < 6`: greeting response `0x05 0x00`
3. If `data_len >= 6`: `socks_open_conn()` → `socks_connect()`
4. `socks_connect()` validates ATYP, creates socket, returns response
5. Subsequent data → forwarded via `send()` to target socket

### Error Handling Philosophy

**CRITICAL DISTINCTION:**
- Return `FALSE`: Only when unable to generate a SOCKS5 response (malloc failure, corrupted data)
- Return `TRUE` + error response: All protocol-level errors (invalid requests, connection failures)

Functions return `TRUE` even on failure if they can generate a valid SOCKS5 error response (e.g., `0x05 0x01` = general failure, `0x05 0x07` = command not supported).

### Debug Logging System

Controlled by `_DEBUG` macro (debug.h):
- `_DEBUG=1`: `_inf()`, `_err()`, `_wrn()`, `_dbg()` expand to printf
- `_DEBUG=0`: All macros become no-ops (zero overhead)

**Important:** Debug builds require msvcrt for printf. Production builds have no logging.

### API Wrapper Pattern

All Windows API calls wrapped with `API()` macro:
```c
API(WSAStartup)(...)  // Expands to WSAStartup(...)
```

This enables:
1. Unit tests to redefine `API(x)` for mocking
2. Future API obfuscation/resolution without touching core code

## Testing Architecture

### Test Strategy (test/src/test_socks.cpp)

Uses **FFF (Fake Function Framework)** + Google Test:

1. **Include socks.c directly** (not linking):
   ```cpp
   extern "C" {
   #include "../../socks/src/socks.c"
   }
   ```

2. **Mock all WinSock APIs** via FFF macros:
   ```cpp
   FAKE_VALUE_FUNC(SOCKET, WSASocketA, ...)
   ```

3. **Redefine API() macro** to use fakes:
   ```cpp
   #undef API
   #define API(func) func
   ```

4. **Custom fakes** for realistic behavior:
   - `WSASocketA_success` returns fake handle 123
   - `connect_would_block` simulates non-blocking connect
   - `getaddrinfo_success` returns mock addrinfo

### C++ Compatibility Issues

**Switch statement variables:** C++ requires `{}` around cases declaring variables:
```c
case 0x03: {  // MUST have braces in C++
    struct addrinfo *result = NULL;
    // ...
    break;
}
```

If socks.c is modified, ensure all `case` blocks with variable declarations have braces when testing.

### Test Execution

Tests run via **Wine** (Windows binary on Linux):
- Compiled with MinGW (`x86_64-w64-mingw32`)
- Linked statically with gtest
- 38 tests covering all SOCKS5 paths

**Sleep() handling:** Not mocked (linker conflict). Tests actually sleep 100ms on WOULDBLOCK scenarios.

## Memory Management

**socks/include/utils.h** defines platform-specific allocators:
- **Windows**: `HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size)`
- **Linux (tests)**: `malloc()` + manual zero

All allocations zero-initialized. Free paths:
- Connection removed: `socks_remove()` calls `mcfree(conn)` + `closesocket()`
- Response buffers: Caller must `mcfree(data_out)`
- Context: Manual cleanup (iterate connections, free each)

## Key Constants

```c
GS_SOCKS_BUFFER_SIZE     512KB   // recv buffer per connection
GS_SOCKS_CONNECT_TIMEOUT 5s      // select() timeout for non-blocking connect
GS_SOCKS_MAX_CONNECTIONS 100     // Max concurrent connections
```

## Modifying Code

### When changing socks.c:

1. **Preserve API() wrappers** - Never call WinSock directly
2. **Respect error conventions** - Return TRUE with error response, not FALSE
3. **Test in debug mode first** - Use `_inf()` logging to trace execution
4. **Run full test suite** - All 38 tests must pass
5. **Check C++ compatibility** - Ensure switch cases with variables have braces

### When adding tests:

1. **Use FFF custom fakes** - Don't rely on default return values
2. **Mock recv() sequences** - Use `return_val_seq` to simulate multi-call scenarios
3. **Check winsock_initialized** - Reset in SetUp() if needed
4. **Free response buffers** - Always `mcfree(response)` after assertions

### Build troubleshooting:

- **Linker errors in production**: Check that `-nostdlib` is active and no printf/stdlib used
- **Obfuscator crash**: Verify Polaris path `/opt/Polaris-Obfuscator/src/build/bin/clang` exists
- **Test compilation errors**: Likely C++ incompatibility (missing braces in switch)
- **Wine test failures**: Check that test expects `TRUE` for error responses (not `FALSE`)
