#ifndef GS_NETUTILS_H
#define GS_NETUTILS_H

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

BOOL resolve_domain_name(const PCHAR, PCHAR, PSIZE_T);

#endif //GS_NETUTILS_H