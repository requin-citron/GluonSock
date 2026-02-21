#include <windows.h>
#include <winsock2.h>
#include "debug.h"
#include "socks.h"

#define SOCKS_PORT 7000
#define BUFFER_SIZE 4096

INT main(INT argc, PCHAR* argv) {
    _inf("GluonSock CLI started");

    PGS_SOCKS_CONTEXT context = socks_init();

    // Initialize Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        _err("WSAStartup failed: %d", WSAGetLastError());
        return EXIT_FAILURE;
    }
    _inf("Winsock initialized");

    // Create listening socket
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        _err("Failed to create socket: %d", WSAGetLastError());
        WSACleanup();
        return EXIT_FAILURE;
    }

    // Bind to port
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

    // Start listening
    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        _err("Listen failed: %d", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return EXIT_FAILURE;
    }

    _inf("SOCKS server listening on port %d", SOCKS_PORT);

    // Main server loop
    while (TRUE) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);

        SOCKET client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) {
            _err("Accept failed: %d", WSAGetLastError());
            continue;
        }

        _inf("Client connected from %s:%d",
             inet_ntoa(client_addr.sin_addr),
             ntohs(client_addr.sin_port));

        // Handle client connection
        BYTE buffer[BUFFER_SIZE];
        UINT32 server_id = (UINT32)client_sock; // Use socket as unique ID

        while (TRUE) {
            int recv_len = recv(client_sock, (char*)buffer, BUFFER_SIZE, 0);

            if (recv_len <= 0) {
                if (recv_len == 0) {
                    _inf("Client disconnected");
                } else {
                    _err("Recv failed: %d", WSAGetLastError());
                }
                break;
            }

            _inf("Received %d bytes from client", recv_len);

            // Process SOCKS data
            PBYTE response      = NULL;
            UINT32 response_len = 0;

            BOOL ret_parse = socks_parse_data(context, server_id, buffer, recv_len, &response, &response_len);
            _inf("socks_parse_data returned: %s", ret_parse ? "TRUE" : "FALSE");

            // Send response if any
            if (response && response_len > 0) {
                int sent = send(client_sock, (char*)response, response_len, 0);
                if (sent == SOCKET_ERROR) {
                    _err("Send failed: %d", WSAGetLastError());
                    free(response);
                    break;
                }
                _inf("Sent %d bytes to client", sent);
                free(response);
            }
            
            PCHAR data_out      = NULL;
            UINT32 data_out_len = 0;

            socks_recv_data(context, server_id, (PBYTE*)&data_out, &data_out_len);
            _inf("socks_recv_data returned data_out_len: %u", data_out_len);
            send(client_sock, (char*)data_out, data_out_len, 0);
        
        }

        closesocket(client_sock);
        _inf("Client socket closed");
    }

    closesocket(listen_sock);
    WSACleanup();

    return EXIT_SUCCESS;
}