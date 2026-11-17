typedef int CRITICAL_SECTION;
static int fail_heap,locks;
static size_t requested;
static void InitializeCriticalSection(CRITICAL_SECTION*c){*c=0;}
static void EnterCriticalSection(CRITICAL_SECTION*c){locks++;}
static void LeaveCriticalSection(CRITICAL_SECTION*c){locks--;}
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,DWORD f,size_t n){requested=n;return fail_heap?NULL:malloc(n);}
static BOOL HeapFree(void*h,DWORD f,void*p){free(p);return 1;}
static size_t HeapSize(void*h,DWORD f,void*p){return requested;}
static void debug_fatal(const char*f,...){abort();}
static size_t safe_size_add(size_t a,size_t b){return a>SIZE_MAX-b?SIZE_MAX:a+b;}
static size_t safe_size_mul_sizeof_pointer(size_t a){return a>SIZE_MAX/sizeof(void*)?SIZE_MAX:a*sizeof(void*);}
static uintptr_t mem_usage;
/* PRODUCTION */
int main(void){fail_heap=1;assert(!mem_try_alloc_debug("test",1,3));assert(!mem_debug_count && !locks);fail_heap=0;assert(!mem_try_alloc_debug("test",1,SIZE_MAX));for(int n=1;n<=17;n++){BYTE*p=mem_try_alloc_debug("test",1,n);assert(p && mem_debug_count==1 && !locks);memset(p,0x13,n);mem_free_debug("test",2,p);assert(!mem_debug_count && !mem_debug_start && !mem_debug_last && !locks && !mem_usage);}puts("nonfatal allocation, debug ownership, unaligned payload sizes 1..17 PASS");}
