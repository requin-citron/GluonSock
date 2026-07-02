#include <winsock2.h>
#include <windows.h>
#include "debug.h"
#include "socks.h"
#include "utils.h"

#define SOCKS_PORT 7000
#define BUFFER_SIZE 4096

static PGS_SOCKS_CONTEXT g_context = NULL;
static CRITICAL_SECTION g_cs;

static VOID on_connect(UINT32 server_id, BOOL success) {
    BYTE response[10] = {0x05, success ? 0x00 : 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    send((SOCKET)server_id, (char*)response, 10, 0);
    _inf("Sent SOCKS5 %s for Server ID: %u", success ? "success" : "failure", server_id);
}

static DWORD WINAPI client_thread(LPVOID param) {
    SOCKET client_sock = (SOCKET)param;
    UINT32 server_id   = (UINT32)client_sock;
    BYTE buffer[BUFFER_SIZE];

    while (TRUE) {
        EnterCriticalSection(&g_cs);
        socks_check_pending(g_context, on_connect);
        PGLUON_SOCKS_CONN conn = socks_find_connection(g_context, server_id);
        SOCKET remote_sock = INVALID_SOCKET;
        if (conn && conn->state == GS_CONN_CONNECTED) {
            remote_sock = conn->socket;
        }
        LeaveCriticalSection(&g_cs);

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_sock, &read_fds);
        if (remote_sock != INVALID_SOCKET) {
            FD_SET(remote_sock, &read_fds);
        }

        struct timeval timeout = {0, 100000};
        INT sel = select(0, &read_fds, NULL, NULL, &timeout);
        if (sel == SOCKET_ERROR) break;
        if (sel == 0) continue;

        if (FD_ISSET(client_sock, &read_fds)) {
            INT recv_len = recv(client_sock, (char*)buffer, BUFFER_SIZE, 0);

            if (recv_len == 0) {
                _inf("Client disconnected (Server ID: %u)", server_id);
                break;
            }
            if (recv_len < 0) {
                INT err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK) {
                    _err("Recv failed: %d", err);
                    break;
                }
                continue;
            }

            _inf("Received %d bytes from client (Server ID: %u)", recv_len, server_id);

            PBYTE response      = NULL;
            UINT32 response_len = 0;

            EnterCriticalSection(&g_cs);
            BOOL ret = socks_parse_data(g_context, server_id, buffer, recv_len, &response, &response_len);
            LeaveCriticalSection(&g_cs);

            if (response && response_len > 0) {
                send(client_sock, (char*)response, response_len, 0);
                mcfree(response);
            }

            if (!ret) break;
        }

        if (remote_sock != INVALID_SOCKET && FD_ISSET(remote_sock, &read_fds)) {
            PBYTE data_out      = NULL;
            UINT32 data_out_len = 0;

            EnterCriticalSection(&g_cs);
            BOOL recv_ok = socks_recv_data(g_context, server_id, &data_out, &data_out_len);
            LeaveCriticalSection(&g_cs);

            if (!recv_ok) {
                _inf("Remote closed (Server ID: %u)", server_id);
                break;
            }

            if (data_out_len > 0) {
                _inf("Forwarding %u bytes remote -> client (Server ID: %u)", data_out_len, server_id);
                send(client_sock, (char*)data_out, data_out_len, 0);
                mcfree(data_out);
            }
        }
    }

    EnterCriticalSection(&g_cs);
    socks_remove(g_context, server_id);
    LeaveCriticalSection(&g_cs);
    closesocket(client_sock);
    _inf("Client thread exiting (Server ID: %u)", server_id);
    return 0;
}

INT main(INT argc, PCHAR* argv) {
    _inf("GluonSock CLI started");

    g_context = socks_init();
    InitializeCriticalSection(&g_cs);

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        _err("WSAStartup failed: %d", WSAGetLastError());
        return EXIT_FAILURE;
    }
    _inf("Winsock initialized");

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        _err("Failed to create socket: %d", WSAGetLastError());
        WSACleanup();
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    ZeroMemory(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SOCKS_PORT);

    if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        _err("Bind failed: %d", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        _err("Listen failed: %d", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    _inf("SOCKS server listening on port %d", SOCKS_PORT);

    while (TRUE) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);

        SOCKET client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) continue;

        u_long mode = 1;
        ioctlsocket(client_sock, FIONBIO, &mode);

        _inf("Client connected from %s:%d",
             inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        CreateThread(NULL, 0, client_thread, (LPVOID)client_sock, 0, NULL);
    }

    closesocket(listen_sock);
    WSACleanup();

    return EXIT_SUCCESS;
}
