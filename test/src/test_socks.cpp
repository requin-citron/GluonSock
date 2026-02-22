// test_socks.cpp - Unit tests for GluonSock SOCKS5 implementation

#include "fff.h"
#include <gtest/gtest.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <cstring>

DEFINE_FFF_GLOBALS;

// ============================================================
// Mock WinSock functions with FFF
// ============================================================
FAKE_VALUE_FUNC(INT, WSAStartup, WORD, LPWSADATA);
FAKE_VALUE_FUNC(INT, WSACleanup);
FAKE_VALUE_FUNC(SOCKET, WSASocketA, INT, INT, INT, LPWSAPROTOCOL_INFOA, GROUP, DWORD);
FAKE_VALUE_FUNC(INT, WSAGetLastError);
FAKE_VALUE_FUNC(INT, closesocket, SOCKET);
FAKE_VALUE_FUNC(INT, ioctlsocket, SOCKET, LONG, u_long*);
FAKE_VALUE_FUNC(INT, connect, SOCKET, const struct sockaddr*, INT);
FAKE_VALUE_FUNC(INT, send, SOCKET, const char*, INT, INT);
FAKE_VALUE_FUNC(INT, recv, SOCKET, char*, INT, INT);
FAKE_VALUE_FUNC(INT, select, INT, fd_set*, fd_set*, fd_set*, const struct timeval*);
FAKE_VALUE_FUNC(INT, getsockopt, SOCKET, INT, INT, PCHAR, INT*);
FAKE_VALUE_FUNC(INT, getaddrinfo, const char*, const char*, const struct addrinfo*, struct addrinfo**);
FAKE_VOID_FUNC(freeaddrinfo, struct addrinfo*);
FAKE_VALUE_FUNC(u_short, ntohs, u_short);

// Redefine API macro to use our fakes
#undef API
#define API(func) func

// Define Rtl* functions to use standard C equivalents
#define RtlCopyMemory(dst, src, len) memcpy(dst, src, len)
#define RtlMoveMemory(dst, src, len) memmove(dst, src, len)
#define RtlZeroMemory(dst, len) memset(dst, 0, len)

// ============================================================
// Include source file AFTER all redirections (as C, not C++)
// ============================================================
extern "C" {
#include "../../socks/src/socks.c"
}

// ============================================================
// Custom fakes for WinSock
// ============================================================
INT WSAStartup_success(WORD wVersionRequested, LPWSADATA lpWSAData) {
    (void)wVersionRequested;
    if (lpWSAData) {
        memset(lpWSAData, 0, sizeof(WSADATA));
    }
    return 0;
}

SOCKET WSASocketA_success(INT af, INT type, INT protocol, LPWSAPROTOCOL_INFOA lpProtocolInfo, GROUP g, DWORD dwFlags) {
    (void)af; (void)type; (void)protocol; (void)lpProtocolInfo; (void)g; (void)dwFlags;
    return 123; // Fake socket handle
}

INT ioctlsocket_success(SOCKET s, LONG cmd, u_long* argp) {
    (void)s; (void)cmd;
    if (argp && cmd == FIONBIO) {
        // Non-blocking mode
    }
    return 0;
}

INT connect_success(SOCKET s, const struct sockaddr* name, INT namelen) {
    (void)s; (void)name; (void)namelen;
    return 0; // Success
}

INT connect_would_block(SOCKET s, const struct sockaddr* name, INT namelen) {
    (void)s; (void)name; (void)namelen;
    return SOCKET_ERROR;
}

INT WSAGetLastError_would_block() {
    return WSAEWOULDBLOCK;
}

INT select_ready(INT nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const struct timeval* timeout) {
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds; (void)timeout;
    return 1; // Ready
}

INT getsockopt_success(SOCKET s, INT level, INT optname, PCHAR optval, INT* optlen) {
    (void)s; (void)level; (void)optname;
    if (optval && optlen && *optlen >= sizeof(INT)) {
        *(INT*)optval = 0; // No error (SO_ERROR)
    }
    return 0;
}

u_short ntohs_passthrough(u_short netshort) {
    return ((netshort & 0xFF00) >> 8) | ((netshort & 0x00FF) << 8);
}

// For getaddrinfo mock
static struct addrinfo test_addrinfo;
static struct sockaddr_in test_sockaddr;

INT getaddrinfo_success(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
    (void)node; (void)service; (void)hints;

    memset(&test_sockaddr, 0, sizeof(test_sockaddr));
    test_sockaddr.sin_family = AF_INET;
    test_sockaddr.sin_addr.s_addr = 0x0100007F; // 127.0.0.1 in network byte order

    memset(&test_addrinfo, 0, sizeof(test_addrinfo));
    test_addrinfo.ai_family = AF_INET;
    test_addrinfo.ai_socktype = SOCK_STREAM;
    test_addrinfo.ai_addr = (struct sockaddr*)&test_sockaddr;
    test_addrinfo.ai_addrlen = sizeof(test_sockaddr);

    *res = &test_addrinfo;
    return 0;
}

INT getaddrinfo_failure(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
    (void)node; (void)service; (void)hints; (void)res;
    return WSAHOST_NOT_FOUND;
}

// ============================================================
// Test Fixture
// ============================================================
class SocksTest : public ::testing::Test {
protected:
    PGS_SOCKS_CONTEXT ctx;

    void SetUp() override {
        // Reset all fakes
        RESET_FAKE(WSAStartup);
        RESET_FAKE(WSACleanup);
        RESET_FAKE(WSASocketA);
        RESET_FAKE(WSAGetLastError);
        RESET_FAKE(closesocket);
        RESET_FAKE(ioctlsocket);
        RESET_FAKE(connect);
        RESET_FAKE(send);
        RESET_FAKE(recv);
        RESET_FAKE(select);
        RESET_FAKE(getsockopt);
        RESET_FAKE(getaddrinfo);
        RESET_FAKE(freeaddrinfo);
        RESET_FAKE(ntohs);
        FFF_RESET_HISTORY();

        // Set default custom fakes
        WSAStartup_fake.custom_fake = WSAStartup_success;
        WSASocketA_fake.custom_fake = WSASocketA_success;
        ioctlsocket_fake.custom_fake = ioctlsocket_success;
        connect_fake.custom_fake = connect_success;
        select_fake.custom_fake = select_ready;
        getsockopt_fake.custom_fake = getsockopt_success;
        ntohs_fake.custom_fake = ntohs_passthrough;
        getaddrinfo_fake.custom_fake = getaddrinfo_success;

        // Reset winsock_initialized flag
        winsock_initialized = FALSE;

        // Initialize context
        ctx = socks_init();
    }

    void TearDown() override {
        // Clean up all connections
        while (ctx->connections != NULL) {
            PGLUON_SOCKS_CONN next = ctx->connections->next;
            mcfree(ctx->connections);
            ctx->connections = next;
        }
        mcfree(ctx);
    }
};

// ============================================================
// Initialization Tests
// ============================================================

TEST_F(SocksTest, InitContext_Success) {
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->connection_count, 0u);
    EXPECT_EQ(ctx->connections, nullptr);
}

TEST_F(SocksTest, InitContext_ZeroCount) {
    EXPECT_EQ(ctx->connection_count, 0u);
}

// ============================================================
// Connection Management Tests
// ============================================================

TEST_F(SocksTest, FindConnection_NotFound) {
    PGLUON_SOCKS_CONN conn = socks_find_connection(ctx, 999);
    EXPECT_EQ(conn, nullptr);
}

TEST_F(SocksTest, FindConnection_EmptyList) {
    EXPECT_EQ(ctx->connections, nullptr);
    PGLUON_SOCKS_CONN conn = socks_find_connection(ctx, 1);
    EXPECT_EQ(conn, nullptr);
}

TEST_F(SocksTest, FindConnection_Found) {
    // Manually create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 42;
    test_conn->socket = 100;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    PGLUON_SOCKS_CONN found = socks_find_connection(ctx, 42);

    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->server_id, 42u);
    EXPECT_EQ(found->socket, 100);
}

TEST_F(SocksTest, FindConnection_MultipleConnections) {
    // Create multiple connections
    PGLUON_SOCKS_CONN conn1 = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    conn1->server_id = 1;
    conn1->socket = 100;
    conn1->connected = TRUE;

    PGLUON_SOCKS_CONN conn2 = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    conn2->server_id = 2;
    conn2->socket = 200;
    conn2->connected = TRUE;

    PGLUON_SOCKS_CONN conn3 = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    conn3->server_id = 3;
    conn3->socket = 300;
    conn3->connected = TRUE;

    conn1->next = conn2;
    conn2->next = conn3;
    conn3->next = NULL;

    ctx->connections = conn1;
    ctx->connection_count = 3;

    PGLUON_SOCKS_CONN found = socks_find_connection(ctx, 3);

    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->server_id, 3u);
    EXPECT_EQ(found->socket, 300);
}

TEST_F(SocksTest, RemoveConnection_NotFound) {
    BOOL result = socks_remove(ctx, 999);
    EXPECT_FALSE(result);
}

TEST_F(SocksTest, RemoveConnection_Success) {
    // Create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 42;
    test_conn->socket = 100;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    BOOL result = socks_remove(ctx, 42);

    EXPECT_TRUE(result);
    EXPECT_EQ(ctx->connection_count, 0u);
    EXPECT_EQ(ctx->connections, nullptr);
    EXPECT_EQ(closesocket_fake.call_count, 1u);
}

TEST_F(SocksTest, RemoveConnection_Middle) {
    // Create three connections
    PGLUON_SOCKS_CONN conn1 = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    conn1->server_id = 1;
    conn1->socket = 100;
    conn1->connected = TRUE;

    PGLUON_SOCKS_CONN conn2 = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    conn2->server_id = 2;
    conn2->socket = 200;
    conn2->connected = TRUE;

    PGLUON_SOCKS_CONN conn3 = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    conn3->server_id = 3;
    conn3->socket = 300;
    conn3->connected = TRUE;

    conn1->next = conn2;
    conn2->next = conn3;
    conn3->next = NULL;

    ctx->connections = conn1;
    ctx->connection_count = 3;

    BOOL result = socks_remove(ctx, 2);

    EXPECT_TRUE(result);
    EXPECT_EQ(ctx->connection_count, 2u);
    EXPECT_EQ(ctx->connections, conn1);
    EXPECT_EQ(conn1->next, conn3);
    EXPECT_EQ(closesocket_fake.call_count, 1u);
}

TEST_F(SocksTest, RemoveConnection_DecrementCount) {
    // Create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 1;
    test_conn->socket = 100;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    socks_remove(ctx, 1);

    EXPECT_EQ(ctx->connection_count, 0u);
}

TEST_F(SocksTest, RemoveConnection_ClosesSocket) {
    // Create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 1;
    test_conn->socket = 123;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    socks_remove(ctx, 1);

    EXPECT_EQ(closesocket_fake.call_count, 1u);
    EXPECT_EQ(closesocket_fake.arg0_val, 123);
}

// ============================================================
// SOCKS5 Protocol Tests
// ============================================================

TEST_F(SocksTest, ParseData_SOCKS5Greeting) {
    // SOCKS5 greeting: VER=0x05, NMETHODS=1, METHODS=[0x00]
    BYTE greeting[] = {0x05, 0x01, 0x00};
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_parse_data(ctx, 1, greeting, sizeof(greeting), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response_len, 2u);
    EXPECT_EQ(response[0], 0x05); // SOCKS version 5
    EXPECT_EQ(response[1], 0x00); // No authentication

    mcfree(response);
}

TEST_F(SocksTest, ParseData_SOCKS5InvalidVersion) {
    // SOCKS4 greeting (invalid for SOCKS5)
    BYTE greeting[] = {0x04, 0x01, 0x00};
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_parse_data(ctx, 1, greeting, sizeof(greeting), &response, &response_len);

    EXPECT_FALSE(result);
}

TEST_F(SocksTest, ParseData_GreetingTooShort) {
    // Incomplete greeting (only version byte)
    BYTE greeting[] = {0x05};
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_parse_data(ctx, 1, greeting, sizeof(greeting), &response, &response_len);

    EXPECT_FALSE(result);
}

TEST_F(SocksTest, DataCommand_SOCKS5ConnectIPv4) {
    // SOCKS5 CONNECT request for 192.168.1.1:8080
    // VER=0x05, CMD=0x01 (CONNECT), RSV=0x00, ATYP=0x01 (IPv4)
    // ADDR=192.168.1.1 (0xC0A80101), PORT=8080 (0x1F90)
    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x01,           // VER, CMD, RSV, ATYP
        0xC0, 0xA8, 0x01, 0x01,           // 192.168.1.1
        0x1F, 0x90                         // Port 8080
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response_len, 10u);
    EXPECT_EQ(response[0], 0x05); // SOCKS version
    EXPECT_EQ(response[1], 0x00); // Success
    EXPECT_EQ(response[3], 0x01); // IPv4

    // Verify connection was created
    EXPECT_EQ(ctx->connection_count, 1u);
    PGLUON_SOCKS_CONN conn = socks_find_connection(ctx, 1);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->server_id, 1u);
    EXPECT_TRUE(conn->connected);

    mcfree(response);
}

TEST_F(SocksTest, Connect_IPv4TooShort) {
    // Truncated IPv4 CONNECT request (missing port)
    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x01,           // VER, CMD, RSV, ATYP
        0xC0, 0xA8                         // Incomplete address
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result); // Returns TRUE but with error response
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response[1], 0x01); // General failure

    mcfree(response);
}

TEST_F(SocksTest, Connect_IPv4SocketCreationFails) {
    WSASocketA_fake.custom_fake = nullptr;
    WSASocketA_fake.return_val = INVALID_SOCKET;

    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x01,
        0xC0, 0xA8, 0x01, 0x01,
        0x1F, 0x90
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response[1], 0x01); // General failure
    EXPECT_EQ(ctx->connection_count, 0u);

    mcfree(response);
}

TEST_F(SocksTest, Connect_IPv4ConnectSuccess) {
    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x01,
        0x0A, 0x00, 0x00, 0x01,           // 10.0.0.1
        0x01, 0xBB                         // Port 443
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 2, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result);
    EXPECT_EQ(ctx->connection_count, 1u);

    PGLUON_SOCKS_CONN conn = socks_find_connection(ctx, 2);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->server_id, 2u);
    EXPECT_EQ(conn->socket, 123);
    EXPECT_TRUE(conn->connected);

    mcfree(response);
}

TEST_F(SocksTest, Connect_IPv4ResponseFormat) {
    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x01,
        0x08, 0x08, 0x08, 0x08,           // 8.8.8.8
        0x00, 0x35                         // Port 53
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 3, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response_len, 10u);
    EXPECT_EQ(response[0], 0x05); // VER
    EXPECT_EQ(response[1], 0x00); // Success
    EXPECT_EQ(response[2], 0x00); // RSV
    EXPECT_EQ(response[3], 0x01); // ATYP IPv4

    mcfree(response);
}

TEST_F(SocksTest, DataCommand_SOCKS5ConnectDomain) {
    // SOCKS5 CONNECT request for example.com:80
    // Domain name: "example.com" (11 bytes)
    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x03,           // VER, CMD, RSV, ATYP (domain)
        0x0B,                              // Domain length (11)
        'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm',
        0x00, 0x50                         // Port 80
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response[0], 0x05);
    EXPECT_EQ(response[1], 0x00); // Success
    EXPECT_EQ(getaddrinfo_fake.call_count, 1u);
    EXPECT_EQ(freeaddrinfo_fake.call_count, 1u);

    mcfree(response);
}

TEST_F(SocksTest, Connect_DomainTooShort) {
    // Missing domain length byte
    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x03            // VER, CMD, RSV, ATYP
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response[1], 0x01); // General failure

    mcfree(response);
}

TEST_F(SocksTest, Connect_DomainLengthMismatch) {
    // Domain length says 20 but only 5 bytes provided
    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x03,
        0x14,                              // Length = 20
        't', 'e', 's', 't', '.'           // Only 5 bytes
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response[1], 0x01); // General failure

    mcfree(response);
}

TEST_F(SocksTest, Connect_DomainResolutionFails) {
    getaddrinfo_fake.custom_fake = getaddrinfo_failure;

    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x03,
        0x07,
        'b', 'a', 'd', '.', 'c', 'o', 'm',
        0x00, 0x50
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response[1], 0x01); // General failure
    EXPECT_EQ(getaddrinfo_fake.call_count, 1u);

    mcfree(response);
}

TEST_F(SocksTest, Connect_DomainSuccess) {
    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x03,
        0x04,
        't', 'e', 's', 't',
        0x00, 0x50
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 5, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result);
    EXPECT_EQ(getaddrinfo_fake.call_count, 1u);
    EXPECT_EQ(ctx->connection_count, 1u);

    PGLUON_SOCKS_CONN conn = socks_find_connection(ctx, 5);
    ASSERT_NE(conn, nullptr);

    mcfree(response);
}

TEST_F(SocksTest, Connect_DomainFreeaddrinfo) {
    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x03,
        0x09,
        'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't',
        0x1F, 0x90
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    socks_open_conn(ctx, 6, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_EQ(freeaddrinfo_fake.call_count, 1u);

    mcfree(response);
}

TEST_F(SocksTest, DataCommand_SOCKS5IPv6NotSupported) {
    // SOCKS5 CONNECT with IPv6 (ATYP=0x04)
    BYTE connect_req[] = {
        0x05, 0x01, 0x00, 0x04,           // VER, CMD, RSV, ATYP (IPv6)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // ::1
        0x00, 0x50                         // Port 80
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, connect_req, sizeof(connect_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response[1], 0x08); // Address type not supported

    mcfree(response);
}

TEST_F(SocksTest, OpenConn_BindNotSupported) {
    // SOCKS5 BIND command (CMD=0x02)
    BYTE bind_req[] = {
        0x05, 0x02, 0x00, 0x01,           // CMD=0x02 (BIND)
        0xC0, 0xA8, 0x01, 0x01,
        0x1F, 0x90
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, bind_req, sizeof(bind_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response[1], 0x07); // Command not supported

    mcfree(response);
}

TEST_F(SocksTest, OpenConn_UDPNotSupported) {
    // SOCKS5 UDP ASSOCIATE command (CMD=0x03)
    BYTE udp_req[] = {
        0x05, 0x03, 0x00, 0x01,           // CMD=0x03 (UDP)
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, udp_req, sizeof(udp_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response[1], 0x07); // Command not supported

    mcfree(response);
}

TEST_F(SocksTest, OpenConn_UnknownCommand) {
    // Unknown SOCKS5 command (CMD=0xFF)
    BYTE unknown_req[] = {
        0x05, 0xFF, 0x00, 0x01,
        0xC0, 0xA8, 0x01, 0x01,
        0x1F, 0x90
    };
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_open_conn(ctx, 1, unknown_req, sizeof(unknown_req), &response, &response_len);

    EXPECT_TRUE(result);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(response[1], 0x07); // Command not supported

    mcfree(response);
}

// ============================================================
// Data Forwarding Tests
// ============================================================

TEST_F(SocksTest, DataCommand_ForwardDataToExistingConnection) {
    // First create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 10;
    test_conn->socket = 123;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    // Mock send to succeed
    send_fake.return_val = 5;

    // Forward some data
    BYTE data[] = {'H', 'e', 'l', 'l', 'o'};
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_parse_data(ctx, 10, data, sizeof(data), &response, &response_len);

    EXPECT_TRUE(result);
    EXPECT_EQ(send_fake.call_count, 1u);
    EXPECT_EQ(send_fake.arg0_val, 123); // Correct socket
    EXPECT_EQ(send_fake.arg2_val, 5);   // Data length
}

TEST_F(SocksTest, ParseData_SendWouldBlock) {
    // Create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 11;
    test_conn->socket = 123;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    // First send returns WOULD_BLOCK, second succeeds
    send_fake.return_val_seq = (INT[]){SOCKET_ERROR, 4};
    send_fake.return_val_seq_len = 2;
    WSAGetLastError_fake.custom_fake = WSAGetLastError_would_block;

    BYTE data[] = {'T', 'e', 's', 't'};
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_parse_data(ctx, 11, data, sizeof(data), &response, &response_len);

    EXPECT_TRUE(result);
    EXPECT_EQ(send_fake.call_count, 2u);
    // Note: Sleep is not mocked, it will actually sleep 100ms
}

TEST_F(SocksTest, ParseData_SendFails) {
    // Create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 12;
    test_conn->socket = 123;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    // send fails with non-WOULDBLOCK error
    send_fake.return_val = SOCKET_ERROR;
    WSAGetLastError_fake.return_val = WSAECONNRESET;

    BYTE data[] = {'F', 'a', 'i', 'l'};
    PBYTE response = NULL;
    UINT32 response_len = 0;

    BOOL result = socks_parse_data(ctx, 12, data, sizeof(data), &response, &response_len);

    EXPECT_FALSE(result);
    // Connection should be removed
    EXPECT_EQ(ctx->connection_count, 0u);
}

// ============================================================
// Create Connection Tests
// ============================================================

TEST_F(SocksTest, CreateConnection_Success) {
    CHAR ip[] = {(CHAR)0xC0, (CHAR)0xA8, 0x01, 0x01}; // 192.168.1.1
    UINT16 port = 0x1F90; // 8080 in network byte order

    BOOL result = socks_create_conn(ctx, 20, ip, port);

    EXPECT_TRUE(result);
    EXPECT_EQ(ctx->connection_count, 1u);

    PGLUON_SOCKS_CONN conn = socks_find_connection(ctx, 20);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->server_id, 20u);
    EXPECT_TRUE(conn->connected);

    EXPECT_EQ(WSASocketA_fake.call_count, 1u);
    EXPECT_EQ(ioctlsocket_fake.call_count, 1u);
    EXPECT_EQ(connect_fake.call_count, 1u);
}

TEST_F(SocksTest, CreateConnection_WouldBlock) {
    connect_fake.custom_fake = connect_would_block;
    WSAGetLastError_fake.custom_fake = WSAGetLastError_would_block;
    select_fake.custom_fake = select_ready;
    getsockopt_fake.custom_fake = getsockopt_success;

    CHAR ip[] = {0x0A, 0x00, 0x00, 0x01}; // 10.0.0.1
    UINT16 port = 0x01BB; // 443

    BOOL result = socks_create_conn(ctx, 21, ip, port);

    EXPECT_TRUE(result);
    EXPECT_EQ(select_fake.call_count, 1u);
    EXPECT_EQ(getsockopt_fake.call_count, 1u);
}

// ============================================================
// Receive Data Tests
// ============================================================

TEST_F(SocksTest, RecvData_Success) {
    // Create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 30;
    test_conn->socket = 456;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    // Mock recv to return data
    recv_fake.return_val = 10;

    PBYTE data = NULL;
    UINT32 data_len = 0;

    BOOL result = socks_recv_data(ctx, 30, &data, &data_len);

    EXPECT_TRUE(result);
    EXPECT_EQ(recv_fake.call_count, 1u);
    EXPECT_EQ(recv_fake.arg0_val, 456);

    if (data) {
        mcfree(data);
    }
}

TEST_F(SocksTest, RecvData_ConnectionClosed) {
    // Create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 31;
    test_conn->socket = 456;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    // recv returns 0 (connection closed)
    recv_fake.return_val = 0;

    PBYTE data = NULL;
    UINT32 data_len = 0;

    BOOL result = socks_recv_data(ctx, 31, &data, &data_len);

    EXPECT_FALSE(result);
    EXPECT_EQ(ctx->connection_count, 0u); // Connection removed
}

TEST_F(SocksTest, RecvData_WouldBlock) {
    // Create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 32;
    test_conn->socket = 456;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    // recv returns WOULD_BLOCK
    recv_fake.return_val = SOCKET_ERROR;
    WSAGetLastError_fake.custom_fake = WSAGetLastError_would_block;

    PBYTE data = NULL;
    UINT32 data_len = 0;

    BOOL result = socks_recv_data(ctx, 32, &data, &data_len);

    EXPECT_TRUE(result); // WOULD_BLOCK is not an error
    EXPECT_EQ(ctx->connection_count, 1u); // Connection still exists
}

TEST_F(SocksTest, RecvData_SocketError) {
    // Create a connection
    PGLUON_SOCKS_CONN test_conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    test_conn->server_id = 33;
    test_conn->socket = 456;
    test_conn->connected = TRUE;
    test_conn->next = NULL;

    ctx->connections = test_conn;
    ctx->connection_count = 1;

    // recv fails with error
    recv_fake.return_val = SOCKET_ERROR;
    WSAGetLastError_fake.return_val = WSAECONNRESET;

    PBYTE data = NULL;
    UINT32 data_len = 0;

    BOOL result = socks_recv_data(ctx, 33, &data, &data_len);

    EXPECT_FALSE(result);
    EXPECT_EQ(ctx->connection_count, 0u); // Connection removed
}
