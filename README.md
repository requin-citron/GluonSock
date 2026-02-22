# GluonSock

A lightweight, standalone SOCKS5 proxy implementation for Windows with comprehensive unit testing.

## Overview

GluonSock provides a minimal SOCKS5 proxy server implementation designed for C2 (Command & Control) scenarios. It supports IPv4 and domain name resolution with non-blocking socket operations.

## Features

- SOCKS5 protocol implementation (RFC 1928)
- IPv4 address support
- Domain name resolution via `getaddrinfo()`
- Non-blocking socket operations
- Connection multiplexing (up to 100 concurrent connections)
- 512KB receive buffer per connection
- Memory-safe implementation with zero external dependencies

### Supported Commands
- `CONNECT` (IPv4 and Domain)

### Unsupported Commands
- `BIND` - Returns error 0x07 (Command not supported)
- `UDP ASSOCIATE` - Returns error 0x07 (Command not supported)
- IPv6 - Returns error 0x08 (Address type not supported)

## Build

### Requirements
- CMake 3.20+
- MinGW-w64 (x86_64-w64-mingw32-gcc)
- Clang++ (for tests)
- Wine (for running Windows tests on Linux)

### Build Main Binary

```bash
mkdir -p build
cd build
cmake -DDEBUG_BUILD=OFF ..
make
```

Output: `build/gluonsock.exe`

### Build with Debug Symbols

```bash
cmake -DDEBUG_BUILD=ON ..
make
```

Output: `build/gluonsock_dbg.exe`

### Build with Tests

```bash
mkdir -p test/build
cd test/build
cmake -DENABLE_TESTS=ON ..
make
```

## Testing

The project includes 38 unit tests covering all SOCKS5 functionality using Google Test and FFF (Fake Function Framework).

### Run Tests

```bash
cd test/build
make test
# or
wine test_socks.exe --gtest_color=no
```

### Test Coverage

- Protocol handshake validation
- IPv4 CONNECT operations
- Domain name resolution
- Connection management (create, find, remove)
- Data forwarding
- Error handling (invalid requests, connection failures)
- Non-blocking I/O (WSAEWOULDBLOCK handling)

### Test Results

```
[==========] 38 tests from 1 test suite
[  PASSED  ] 38 tests
```

## Project Structure

```
GluonSock/
├── socks/
│   ├── include/
│   │   ├── socks.h          # SOCKS5 API definitions
│   │   ├── debug.h          # Debug macros (API wrapper)
│   │   └── utils.h          # Memory allocation wrappers
│   └── src/
│       └── socks.c          # SOCKS5 implementation (456 lines)
├── test/
│   ├── include/
│   │   └── fff.h            # Fake Function Framework
│   └── src/
│       └── test_socks.cpp   # Unit tests (949 lines)
├── main.c                   # Entry point (placeholder)
└── CMakeLists.txt           # Build configuration
```

## API Reference

### Initialization

```c
PGS_SOCKS_CONTEXT socks_init(void);
```

Initializes the SOCKS context. Returns a context handle.

### Data Parsing

```c
BOOL socks_parse_data(
    PGS_SOCKS_CONTEXT ctx,
    UINT32 server_id,
    PBYTE data,
    UINT32 data_len,
    PBYTE *data_out,
    UINT32 *data_out_len
);
```

Parses incoming SOCKS5 data. Returns `TRUE` on success with a response in `data_out`.

### Receive Data

```c
BOOL socks_recv_data(
    PGS_SOCKS_CONTEXT ctx,
    UINT32 server_id,
    PBYTE *data_out,
    UINT32 *data_out_len
);
```

Receives data from an established connection. Returns `FALSE` if connection closed or errored.

### Connection Management

```c
PGLUON_SOCKS_CONN socks_find_connection(PGS_SOCKS_CONTEXT ctx, UINT32 server_id);
BOOL socks_remove(PGS_SOCKS_CONTEXT ctx, UINT32 server_id);
```

Find and remove connections by server ID.

## Memory Management

The implementation uses custom memory allocation wrappers:

- **Windows**: `HeapAlloc()` / `HeapFree()` with `HEAP_ZERO_MEMORY`
- **Linux**: `malloc()` / `free()` (for testing)

All allocations are zero-initialized. Memory is freed on:
- Connection closure (graceful or error)
- Request completion
- Context cleanup

## Error Handling

### SOCKS5 Error Codes

| Code | Meaning | When Returned |
|------|---------|---------------|
| 0x00 | Success | Connection established |
| 0x01 | General failure | Protocol errors, connection failures |
| 0x07 | Command not supported | BIND, UDP requests |
| 0x08 | Address type not supported | IPv6 requests |

### Return Values

Functions return:
- `TRUE` with valid SOCKS5 response on protocol success (including error responses)
- `FALSE` only when unable to generate a response (memory allocation failure, invalid data)

## Performance Characteristics

- **Buffer size**: 512KB per connection
- **Max connections**: 100 concurrent
- **Connect timeout**: 5 seconds
- **Non-blocking I/O**: Retry on `WSAEWOULDBLOCK` with 100ms sleep
- **Memory overhead**: ~16 bytes per connection (linked list node)

## License

See project documentation for licensing information.

## Contributing

This is a specialized implementation. Contact the project maintainer for contribution guidelines.
