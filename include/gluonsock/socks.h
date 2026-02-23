#pragma once
#ifndef GLUONSOCK_H
#define GLUONSOCK_H

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#define GS_SOCKS_BUFFER_SIZE        524288   // 512KB
#define GS_SOCKS_CONNECT_TIMEOUT    5        // Seconds
#define GS_SOCKS_MAX_CONNECTIONS    100      // Max concurrent

typedef struct _GLUON_SOCKS_CONN {
    UINT32  server_id;
    SOCKET  socket;
    BOOL    connected;
    struct _GLUON_SOCKS_CONN* next;
} GLUON_SOCKS_CONN, *PGLUON_SOCKS_CONN;

typedef struct GS_SOCKS_CONTEXT {
    PGLUON_SOCKS_CONN connections;
    UINT32            connection_count;
} GS_SOCKS_CONTEXT, *PGS_SOCKS_CONTEXT;

#ifdef __cplusplus
extern "C" {
#endif

PGS_SOCKS_CONTEXT socks_init();
PGLUON_SOCKS_CONN socks_find_connection(PGS_SOCKS_CONTEXT, UINT32);
BOOL socks_remove(PGS_SOCKS_CONTEXT, UINT32);
BOOL socks_parse_data(PGS_SOCKS_CONTEXT, UINT32, PBYTE, UINT32, PBYTE*, UINT32*);
BOOL socks_create_conn(PGS_SOCKS_CONTEXT, UINT32, PCHAR, UINT16);
BOOL socks_recv_data(PGS_SOCKS_CONTEXT, UINT32, PBYTE *, UINT32 *);

#ifdef __cplusplus
}
#endif

#endif
