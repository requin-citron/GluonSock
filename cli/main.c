#include <windows.h>
#include "debug.h"
#include "socks.h"

INT main(INT argc, PCHAR* argv) {
    _inf("GluonSock CLI started");

    PGS_SOCKS_CONTEXT context = socks_init();

    return EXIT_SUCCESS;
}