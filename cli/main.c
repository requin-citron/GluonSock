#include <windows.h>
#include <winsock2.h>
#include "debug.h"
#include "socks.h"
#include "utils.h"

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

    // Set listen socket to non-blocking
    u_long mode = 1;
    ioctlsocket(listen_sock, FIONBIO, &mode);

    // Main server loop
    while (TRUE) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int select_result = select(0, &read_fds, NULL, NULL, &timeout);

        if (select_result == SOCKET_ERROR) {
            _err("Select failed: %d", WSAGetLastError());
            break;
        }

        if (select_result == 0) {
            // Timeout, continue loop
            continue;
        }

        // Check if we have a new connection
        if (FD_ISSET(listen_sock, &read_fds)) {
            struct sockaddr_in client_addr;
            int client_len = sizeof(client_addr);

            SOCKET client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock == INVALID_SOCKET) {
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK) {
                    _err("Accept failed: %d", err);
                }
                continue;
            }

            // Set client socket to non-blocking
            u_long client_mode = 1;
            ioctlsocket(client_sock, FIONBIO, &client_mode);

            _inf("Client connected from %s:%d",
                 inet_ntoa(client_addr.sin_addr),
                 ntohs(client_addr.sin_port));

            // Handle client connection
            BYTE buffer[BUFFER_SIZE];
            UINT32 server_id = (UINT32)client_sock; // Use socket as unique ID

            while (TRUE) {
                // Get remote socket if connection exists
                PGLUON_SOCKS_CONN conn = socks_find_connection(context, server_id);
                SOCKET remote_sock = (conn != NULL) ? conn->socket : INVALID_SOCKET;

                fd_set client_read_fds;
                FD_ZERO(&client_read_fds);
                FD_SET(client_sock, &client_read_fds);

                // Also monitor remote socket if it exists
                if (remote_sock != INVALID_SOCKET) {
                    FD_SET(remote_sock, &client_read_fds);
                }

                struct timeval client_timeout;
                client_timeout.tv_sec = 5;
                client_timeout.tv_usec = 0;

                int client_select = select(0, &client_read_fds, NULL, NULL, &client_timeout);

                if (client_select == SOCKET_ERROR) {
                    _err("Client select failed: %d", WSAGetLastError());
                    break;
                }

                if (client_select == 0) {
                    // Timeout, continue to check if connection is still alive
                    continue;
                }

                // Check client socket
                if (FD_ISSET(client_sock, &client_read_fds)) {
                    _inf("Data available from client, attempting to recv");
                    int recv_len = recv(client_sock, (char*)buffer, BUFFER_SIZE, 0);

                    if (recv_len <= 0) {
                        if (recv_len == 0) {
                            _inf("Client disconnected");
                        } else {
                            int err = WSAGetLastError();
                            if (err != WSAEWOULDBLOCK) {
                                _err("Recv failed: %d", err);
                            }
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
                            mcfree(response);
                            break;
                        }
                        _inf("Sent %d bytes to client", sent);
                        mcfree(response);
                    }
                }

                // Check remote socket for data
                if (remote_sock != INVALID_SOCKET && FD_ISSET(remote_sock, &client_read_fds)) {
                    _inf("Data available from remote server");
                    PCHAR data_out      = NULL;
                    UINT32 data_out_len = 0;

                    socks_recv_data(context, server_id, (PBYTE*)&data_out, &data_out_len);
                    if (data_out_len > 0) {
                        _inf("Received %d bytes from remote server, forwarding to client", data_out_len);
                        int sent = send(client_sock, (char*)data_out, data_out_len, 0);
                        if (sent == SOCKET_ERROR) {
                            _err("Failed to send remote data to client: %d", WSAGetLastError());
                            mcfree(data_out);
                            break;
                        }
                        mcfree(data_out);
                    }
                }
            }

            closesocket(client_sock);
            _inf("Client socket closed");
        }
    }

    closesocket(listen_sock);
    WSACleanup();

    return EXIT_SUCCESS;
}