#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef uint32_t DWORD;
typedef int CRITICAL_SECTION;
static int locked, heap_calls, fail_at, copies, outstanding, incomplete;
static uintptr_t mem_usage;
static void *GetProcessHeap(void) { return NULL; }
static void *HeapAlloc(void *heap, int flags, size_t size)
{
    void *p;
    if (++heap_calls == fail_at) return NULL;
    p = malloc(size); assert(p); outstanding++; return p;
}
static void HeapFree(void *heap, int flags, void *p) { outstanding--; free(p); }
static void EnterCriticalSection(CRITICAL_SECTION *cs) { assert(!locked); locked = 1; }
static void LeaveCriticalSection(CRITICAL_SECTION *cs) { assert(locked); locked = 0; }
static void os_copy_memory(void *d, const void *s, size_t size)
{
    if (!d) { fputs("snapshot copied to NULL after HeapAlloc failure\n", stderr); exit(2); }
    copies++; memcpy(d, s, size);
}
static void debug_printf(const char *fmt, ...)
{
    assert(!locked);
    if (strstr(fmt, "incomplete")) incomplete++;
}
static void DebugBreak(void) {}

/* PRODUCTION */

int main(void)
{
    mem_debug_t first, second;
    int failure;
    memset(&first, 0, sizeof(first)); memset(&second, 0, sizeof(second));
    first.next = &second; first.filename = second.filename = "fixture";
    mem_debug_start = &first;
    for (failure = 1; failure <= 3; ++failure)
    {
        heap_calls = copies = incomplete = 0; fail_at = failure;
        mem_debug();
        assert(!locked && !outstanding);
        assert(copies == (failure < 3 ? failure - 1 : 2));
        assert(incomplete == (failure < 3 ? 1 : 0));
        assert(first.next == &second && second.next == NULL);
    }
    puts("memory: snapshot allocation failure at first/second node, lock release and cleanup passed");
    return 0;
}
