typedef struct { const wchar_t *file; } ITEMIDLIST;
typedef const ITEMIDLIST *LPCITEMIDLIST;
#define E_ABORT (-2)
#define STRING_SIZE 260
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define os_copy_memory memcpy
static struct { struct { wchar_t cFileName[STRING_SIZE]; } fd; } _viv_slot_current;
static HWND _viv_hwnd;
static HRESULT shell_result;
static int selects,launches;
static ITEMIDLIST allocation;

static const wchar_t *string_get_filename_part(const wchar_t *path)
{
    const wchar_t *last=wcsrchr(path,L'\\');
    return last ? last+1 : path;
}
static void string_get_path_part(wchar_t *out,const wchar_t *path)
{
    const wchar_t *end=wcsrchr(path,L'\\');
    size_t n=end ? (size_t)(end-path) : 0;
    wmemcpy(out,path,n);
    out[n]=0;
}
static ITEMIDLIST *os_ILCreateFromPath(const wchar_t *path)
{
    allocation.file=path;
    return &allocation;
}
static void CoTaskMemFree(void *p) { assert(p==&allocation); }
static HRESULT select_item(const ITEMIDLIST *item,UINT count,LPCITEMIDLIST *children,DWORD flags)
{
    assert(count==0 && !children && "an absolute file PIDL selects the item directly");
    assert(wcscmp(item->file,L"C:\\test.png")==0);
    ++selects;
    return shell_result;
}
static HRESULT (*os_SHOpenFolderAndSelectItems)(const ITEMIDLIST *,UINT,LPCITEMIDLIST *,DWORD)=select_item;
static BOOL os_shell_execute(HWND h,const wchar_t *path,int wait,const char *verb,const wchar_t *args)
{
    assert(wcscmp(path,L"C:\\")==0 && "a drive-root fallback needs its trailing separator");
    ++launches;
    return TRUE;
}

/* PRODUCTION */

int main(void)
{
    _viv_open_file_location();
    assert(!selects && !launches);
    wcscpy(_viv_slot_current.fd.cFileName,L"C:\\test.png");
    _viv_open_file_location();
    assert(selects==1 && !launches);
    shell_result=-1;
    _viv_open_file_location();
    assert(selects==2 && launches==1);
    os_SHOpenFolderAndSelectItems=NULL;
    _viv_open_file_location();
    assert(selects==2 && launches==2);
    puts("filename click selects the original file and opens drive-root folders on fallback PASS");
    return 0;
}
