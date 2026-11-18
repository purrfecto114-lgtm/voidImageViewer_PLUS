typedef int32_t LONG;
typedef void *LPVOID;
typedef struct { int64_t QuadPart; } LARGE_INTEGER;
typedef struct { DWORD biSize; int biWidth,biHeight; uint16_t biPlanes,biBitCount; DWORD biCompression,biSizeImage; int tail[4]; } BITMAPINFOHEADER;
#pragma pack(push,2)
typedef struct { uint16_t bfType; DWORD bfSize; uint16_t reserved[2]; DWORD bfOffBits; } BITMAPFILEHEADER;
#pragma pack(pop)
typedef struct { int open; BYTE bytes[128]; size_t written; } File;
typedef File *HANDLE;
typedef DWORD (*Worker)(void *);
typedef DWORD (*Progress)(LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,DWORD,DWORD,HANDLE,HANDLE,LPVOID);

#define WINAPI
#define CALLBACK
#define MAX_PATH 260
#define STRING_SIZE 260
#define VIV_MAX_ROTATION_BYTES (512u * 1024u * 1024u)
#define LOCALIZATION_ID_ROTATE_IMAGE_CAPTION 1
#define LOCALIZATION_ID_ROTATE_IMAGE_FAILED 2
#define VIV_ID_ROTATION_TIMER 90
#define MB_OK 0
#define MB_ICONERROR 16
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define PROGRESS_CONTINUE 0
#define PROGRESS_CANCEL 1
#define FILE_ATTRIBUTE_TEMPORARY 256
#define FILE_ATTRIBUTE_NORMAL 128
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_SHARE_DELETE 4
#define DELETE 0x10000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define GENERIC_WRITE 0x40000000
#define OPEN_EXISTING 3
#define TRUNCATE_EXISTING 5
#define BI_RGB 0

typedef struct mip { int unused; } _viv_mipmap_t;
typedef struct { HBITMAP hbitmap; _viv_mipmap_t *mipmap; int delay; } _viv_frame_t;
static struct {
    struct { wchar_t cFileName[STRING_SIZE]; } fd;
    int frame_count,frame_loaded_count,image_wide,image_high;
    _viv_frame_t *frames;
} _viv_slot_current;

static HWND _viv_hwnd=1;
static int _viv_hw_render_fallback,_viv_image_is_low_res,_viv_frame_position,_viv_view_x,_viv_view_y;
static enum { NONE, ALLOC, TIMER, THREAD, COPY, TRANSFORM, WRITE } fault;
static int dialogs,deleted,rotations,snapshots,released,timer,thread_started,done,cleared,orientation;
static Worker worker;
static void *worker_arg;
static File thread, files[8];
static BYTE pixels[24] = {40,70,90};
static int next_file;

static LONG InterlockedExchange(volatile LONG *p,LONG value) { LONG old=*p; *p=value; return old; }
static int _viv_rotation_cancelled(volatile LONG *p) { return p && *p; }
static SIZE_T safe_size_mul(SIZE_T a,SIZE_T b) { return b && a>SIZE_MAX/b?SIZE_MAX:a*b; }
static void *mem_try_alloc(size_t n) { return fault==ALLOC?NULL:malloc(n); }
static void mem_free(void *p) { free(p); }
#define os_zero_memory(p,n) memset(p,0,n)
static BOOL GdiFlush(void) { return TRUE; }
static BOOL SetTimer(HWND h,UINT id,UINT delay,void *fn) {
    assert(!timer && id==VIV_ID_ROTATION_TIMER && delay<=50);
    return timer=fault!=TIMER;
}
static BOOL KillTimer(HWND h,UINT id) {
    assert(timer && id==VIV_ID_ROTATION_TIMER);
    return !(timer=0);
}
static HANDLE CreateThread(void *attrs,SIZE_T size,Worker fn,void *arg,DWORD flags,DWORD *id) {
    assert(timer && !thread.open && !worker);
    if (fault==THREAD) return NULL;
    worker=fn; worker_arg=arg; thread.open=1; done=0; thread_started++;
    return &thread;
}
static void finish_worker(void) {
    assert(worker && !done);
    worker(worker_arg);
    worker=NULL;
    done=1;
}
static DWORD WaitForSingleObject(HANDLE h,DWORD wait) {
    assert(h==&thread && h->open);
    if (wait && !done) finish_worker();
    return done?WAIT_OBJECT_0:WAIT_TIMEOUT;
}
static void ExitProcess(UINT code) { abort(); }
static BOOL CloseHandle(HANDLE h) { assert(h->open); h->open=0; return TRUE; }
static DWORD GetTempPathW(DWORD count,wchar_t *dir) { wcscpy(dir,L"temp"); return 4; }
static UINT GetTempFileNameW(const wchar_t *dir,const wchar_t *prefix,UINT id,wchar_t *path) {
    assert(thread.open && next_file < (int)(sizeof(files)/sizeof(*files)));
    path[0]=L'0'+next_file++;
    path[1]=0;
    return TRUE;
}
static BOOL CopyFileExW(const wchar_t *src,const wchar_t *dst,Progress progress,LPVOID context,BOOL *stop,DWORD flags) {
    LARGE_INTEGER size={0};
    assert(thread.open && !wcsncmp(src,L"image",5) && snapshots<=2);
    snapshots++;
    return fault!=COPY && progress(size,size,size,size,0,0,NULL,NULL,context)==PROGRESS_CONTINUE;
}
static BOOL SetFileAttributesW(const wchar_t *path,DWORD attrs) { return TRUE; }
static HANDLE CreateFileW(const wchar_t *path,DWORD access,DWORD share,void *a,DWORD d,DWORD flags,HANDLE t) {
    File *file=&files[path[0]-L'0'];
    assert(thread.open && !file->open && (flags&FILE_FLAG_DELETE_ON_CLOSE));
    file->open=1;
    return file;
}
static BOOL DeleteFileW(const wchar_t *path) { return TRUE; }
static BOOL WriteFile(HANDLE file,const void *data,DWORD size,DWORD *written,void *ov) {
    assert(file->open && size + file->written <= sizeof(file->bytes));
    memcpy(file->bytes+file->written,data,size);
    file->written+=size;
    *written=fault==WRITE && file->written>54?0:size;
    return TRUE;
}
static BOOL string_path_long(wchar_t *dest,SIZE_T count,const wchar_t *src) { wcscpy(dest,src); return TRUE; }
static const char *localization_get_string(int i) { return "test"; }
static void string_copy_utf8_string(wchar_t *out,const char *s) { *out=0; }
static int viv_msgbox(HWND h,const wchar_t *caption,const wchar_t *text,UINT flags) {
    assert(flags==MB_ICONERROR && !thread.open); dialogs++; return 0;
}
static BOOL DeleteObject(HBITMAP bitmap) { assert(bitmap); deleted++; return TRUE; }
static void _viv_clear_frames(_viv_frame_t *frames,int count) {
    cleared++;
    while (count) DeleteObject(frames[--count].hbitmap);
    free(frames);
}
static void _viv_mipmap_free(_viv_mipmap_t *mip) {}
static void _viv_view_set(int x,int y,int force) {}
static void _viv_update_src_pixel(int force,int status) {}
static void _viv_status_update(void) {}
static BOOL InvalidateRect(HWND h,const void *r,BOOL erase) { return TRUE; }
static void _viv_hw_release_image(void) { assert(!released || rotations>released); released++; }

#define GetObject rotation_get_object
static int rotation_get_object(HBITMAP bitmap,int size,void *out) {
    assert(size==sizeof(BITMAP) && bitmap);
    if (bitmap>=100 && orientation!=3) *(BITMAP *)out=(BITMAP){3,2,12,1,32,pixels};
    else *(BITMAP *)out=(BITMAP){2,3,8,1,32,pixels};
    return size;
}
static HBITMAP *_viv_prepare_oriented_frames(const BITMAP *images,int count,int orient,volatile LONG *stop) {
    HBITMAP *frames;
    assert(thread.open && count==2 && images[0].bmBits==pixels);
    if (_viv_rotation_cancelled(stop)) return NULL;
    orientation=orient;
    rotations++;
    if (fault==TRANSFORM) return NULL;
    frames=malloc(2*sizeof(*frames));
    assert(frames);
    frames[0]=100; frames[1]=101;
    return frames;
}

/* PRODUCTION */

static void set_image(const wchar_t *name) {
    _viv_slot_current.frames=calloc(2,sizeof(*_viv_slot_current.frames));
    assert(_viv_slot_current.frames);
    _viv_slot_current.frames[0]=(_viv_frame_t){7,NULL,100};
    _viv_slot_current.frames[1]=(_viv_frame_t){8,NULL,200};
    _viv_slot_current.frame_count=_viv_slot_current.frame_loaded_count=2;
    _viv_slot_current.image_wide=2; _viv_slot_current.image_high=3;
    wcscpy(_viv_slot_current.fd.cFileName,name);
}
static void reset(void) {
    _viv_rotation_clear();
    if (_viv_slot_current.frames) _viv_clear_frames(_viv_slot_current.frames,2);
    _viv_slot_current.frames=NULL;
    _viv_rotation_shutdown();
    assert(!thread.open && !timer && !worker && !_viv_rotation_busy());
    for (int i=0; i<next_file; i++) assert(!files[i].open);
    memset(files,0,sizeof(files));
    next_file=dialogs=deleted=rotations=snapshots=released=thread_started=cleared=0;
    fault=NONE;
    set_image(L"image.jpg");
}

int main(void) {
    const unsigned bmp_header_size=54;
    reset();
    _viv_image_is_low_res=1;
    _viv_edit_rotate(0);
    assert(!thread_started && !dialogs);
    _viv_image_is_low_res=0;

    for (int step=ALLOC; step<=WRITE; step++) {
        fault=step;
        _viv_edit_rotate(0);
        assert(!rotations && !snapshots && !released && !deleted);
        if (step<=THREAD) assert(dialogs==1 && !_viv_rotation_busy());
        else {
            assert(!dialogs && _viv_rotation_busy());
            _viv_rotation_poll();
            assert(_viv_rotation_busy());
            finish_worker();
            _viv_rotation_poll();
            assert(dialogs==1 && !_viv_rotation_active() && !_viv_rotation_busy());
        }
        assert(_viv_slot_current.frames[0].hbitmap==7 && _viv_slot_current.image_wide==2);
        assert(!released);
        reset();
    }

    _viv_edit_rotate(0);
    assert(_viv_rotation_busy() && !rotations && !snapshots && !dialogs && !deleted);
    finish_worker();
    assert(!_viv_rotation_active() && _viv_slot_current.frames[0].hbitmap==7);
    _viv_rotation_poll();
    assert(_viv_rotation_active() && !_viv_rotation_busy() && snapshots==1 && rotations==1);
    assert(_viv_slot_current.frames[0].hbitmap==100 && _viv_slot_current.image_wide==3 && _viv_slot_current.image_high==2);
    assert(_viv_slot_current.frames[1].delay==200 && deleted==2 && released==1 && orientation==6);
    {
        BITMAPFILEHEADER file_header;
        BITMAPINFOHEADER info;
        File *f=_viv_rotation_preview;
        memcpy(&file_header,f->bytes,sizeof(file_header));
        memcpy(&info,f->bytes+sizeof(file_header),sizeof(info));
        assert(file_header.bfType==0x4d42 && file_header.bfOffBits==bmp_header_size);
        assert(info.biWidth==3 && info.biHeight==-2 && info.biBitCount==32);
        assert(file_header.bfSize==f->written && !memcmp(f->bytes+bmp_header_size,pixels,sizeof(pixels)));
    }
    {
        HANDLE shown_preview=_viv_rotation_preview;
        HANDLE original_copy=_viv_rotation_source;
        fault=WRITE;
        _viv_edit_rotate(1);
        finish_worker();
        _viv_rotation_poll();
        assert(dialogs==1 && !_viv_rotation_busy() && released==1 && snapshots==1);
        assert(_viv_rotation_preview==shown_preview && shown_preview->open);
        assert(_viv_rotation_source==original_copy && original_copy->open);
        assert(_viv_slot_current.frames[0].hbitmap==100 && _viv_slot_current.image_wide==3);
        fault=NONE;
        _viv_edit_rotate(1);
        finish_worker();
        _viv_rotation_poll();
        assert(dialogs==1 && !_viv_rotation_busy() && released==2 && snapshots==1);
        assert(_viv_rotation_preview!=shown_preview && !shown_preview->open);
        assert(_viv_slot_current.image_wide==2 && _viv_slot_current.image_high==3);
    }
    reset();

    _viv_edit_rotate(0);
    finish_worker();
    _viv_edit_rotate(1);
    _viv_rotation_poll();
    assert(!_viv_rotation_busy() && !_viv_rotation_active() && !released && deleted==2);
    assert(_viv_slot_current.frames[0].hbitmap==7 && thread_started==1);
    reset();

    _viv_edit_rotate(0);
    for (int turn=0; turn<3; turn++) _viv_edit_rotate(0);
    finish_worker();
    _viv_rotation_poll();
    assert(!_viv_rotation_busy() && !_viv_rotation_active() && !rotations && !snapshots);
    reset();

    _viv_edit_rotate(0);
    finish_worker();
    _viv_edit_rotate(0);
    _viv_rotation_poll();
    assert(_viv_rotation_busy() && !released && thread_started==2 && snapshots==1);
    finish_worker();
    _viv_rotation_poll();
    assert(_viv_rotation_active() && snapshots==1 && rotations==2 && released==1 && orientation==3);
    assert(_viv_slot_current.image_wide==2 && _viv_slot_current.image_high==3);
    _viv_edit_rotate(1);
    finish_worker();
    _viv_rotation_poll();
    assert(snapshots==1 && orientation==8 && _viv_slot_current.image_wide==3);
    reset();

    _viv_edit_rotate(0);
    finish_worker();
    _viv_rotation_clear();
    assert(!_viv_slot_current.frames && !cleared && !_viv_rotation_busy());
    set_image(L"image2.gif");
    _viv_edit_rotate(1);
    assert(thread_started==1);
    _viv_rotation_poll();
    assert(cleared==1 && thread_started==2 && !released && _viv_slot_current.frames[0].hbitmap==7);
    finish_worker();
    _viv_rotation_poll();
    assert(released==1 && orientation==8 && snapshots==2 && !_viv_rotation_busy());
    reset();

    _viv_edit_rotate(0);
    _viv_rotation_clear();
    _viv_rotation_shutdown();
    assert(cleared==1 && deleted==2 && !dialogs && !rotations && !snapshots && !thread.open);
    puts("rotation: dispatch, BMP bytes, failure and retry, turn coalescing, navigation, and shutdown PASS");
    return 0;
}
