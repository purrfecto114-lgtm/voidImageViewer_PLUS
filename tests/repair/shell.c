typedef void ITEMIDLIST;
#define __unaligned
#define STRING_SIZE 260
#define SEE_MASK_INVOKEIDLIST 12
#define SEE_MASK_NOCLOSEPROCESS 64
#define SW_SHOWNORMAL 1
#define INFINITE 0xffffffffu
#define WAIT_OBJECT_0 0
#define WAIT_FAILED 0xffffffffu
typedef struct {DWORD cbSize,fMask;HWND hwnd;wchar_t*lpVerb;ITEMIDLIST*lpIDList;const wchar_t*lpParameters;int nShow;intptr_t hProcess;} SHELLEXECUTEINFO;
static int child_exit=23,wait_calls,exit_query_calls,close_calls,fail_query,no_process;
static DWORD wait_result;
static ITEMIDLIST *os_ILCreateFromPath(const wchar_t*p){return malloc(1);}
static void string_copy_utf8_string(wchar_t*p,const char*s){while((*p++=(unsigned char)*s++));}
static void os_zero_memory(void*p,size_t n){memset(p,0,n);}
static BOOL ShellExecuteEx(SHELLEXECUTEINFO*s){s->hProcess=no_process?0:42;return 1;}
static DWORD WaitForSingleObject(intptr_t h,DWORD timeout){wait_calls++;return wait_result;}
static BOOL GetExitCodeProcess(intptr_t h,DWORD*out){exit_query_calls++;*out=child_exit;return !fail_query;}
static BOOL CloseHandle(intptr_t h){close_calls++;return 1;}
static void CoTaskMemFree(void*p){free(p);}
/* PRODUCTION */
int main(void){
 assert(!os_shell_execute(1,L"viewer.exe",1,"runas",L"/install"));assert(wait_calls==1 && exit_query_calls==1);
 child_exit=0;assert(os_shell_execute(1,L"viewer.exe",1,"runas",0));
 wait_result=WAIT_FAILED;assert(!os_shell_execute(1,L"viewer.exe",1,"runas",0));
 wait_result=WAIT_OBJECT_0;fail_query=1;assert(!os_shell_execute(1,L"viewer.exe",1,"runas",0));
 fail_query=0;no_process=1;int before=close_calls;assert(!os_shell_execute(1,L"viewer.exe",1,"runas",0));assert(close_calls==before);
 assert(os_shell_execute(1,L"viewer.exe",0,"open",0));puts("shell child exit, wait/query errors, missing process and async launch PASS");
}
