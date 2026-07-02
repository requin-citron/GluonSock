#include "debug.h"
#include "utils.h"
#include "netutils.h"


BOOL resolve_domain_name(const PCHAR domain_name, PCHAR ip_addr, PSIZE_T ip_addr_len) {
    BOOL ret_val = FALSE;
    struct addrinfo hints, *result = NULL;
    struct sockaddr_in *addr       = NULL;
    
    API(RtlZeroMemory)(&hints, sizeof(hints)); // Zero out the hints structure
    
    hints.ai_family   = AF_INET; // IPv4 only for now
    hints.ai_socktype = SOCK_STREAM;

    INT res = API(getaddrinfo)(domain_name, NULL, &hints, &result);
    
    if (res != 0 || result == NULL) { // Host resolution failed
        goto exit;
    }

    addr = (struct sockaddr_in *)result->ai_addr;
    
    API(RtlCopyMemory)(ip_addr, &addr->sin_addr, 4); // 4 is the size of an IPv4 addr
    
    if (ip_addr_len) { // if not NULL, update the length of the IP string
        *ip_addr_len = 4; // Update the length of the IP string
    }
    
    ret_val = TRUE;


    exit:
    ;

    if (result) API(freeaddrinfo)(result); // Free the result after use

    return ret_val;
}