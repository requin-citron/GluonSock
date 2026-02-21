#ifndef GS_UTILS_H
#define GS_UTILS_H

#if !defined(mcalloc) && !defined(mcfree)

#ifdef _WIN32 // windows cross compile

#include <windows.h>

#define mcalloc(size)  HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size)
#define mcfree(ptr)    HeapFree(GetProcessHeap(), 0, ptr)

#else // linux

#include <stdlib.h>
#define mcalloc(size)  malloc(size)
#define mcfree(ptr)    free(ptr)

#endif

#endif // mcalloc && mcfree

#endif //GS_UTILS_H