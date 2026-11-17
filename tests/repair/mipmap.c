typedef struct mip { HBITMAP hbitmap;struct mip*mipmap; } _viv_mipmap_t;
#define HALFTONE 4
#define SRCCOPY 0xcc0020u
#define HGDI_ERROR ((HGDIOBJ)-1)
static int fail_bitmap=1,bitmap_calls;
static void *mem_alloc(size_t n){void*p=malloc(n);assert(p);return p;}
static void *mem_try_alloc(size_t n){return mem_alloc(n);}
static void mem_free(void*p){free(p);}
static HDC GetDC(HWND h){return 1;}
static HDC CreateCompatibleDC(HDC h){return 2;}
static HBITMAP CreateCompatibleBitmap(HDC h,int w,int t){bitmap_calls++;return fail_bitmap?0:99;}
static HGDIOBJ SelectObject(HDC h,HGDIOBJ o){return o?88:0;}
static int SetStretchBltMode(HDC d,int m){return 1;}
static BOOL _viv_StretchBltStitch(HDC a,int b,int c,int d,int e,HDC f,int g,int h,int i,int j,DWORD k,int l,int m,int n,int o){return !fail_bitmap;}
static BOOL DeleteDC(HDC h){return 1;}
static BOOL DeleteObject(HGDIOBJ h){return 1;}
static int ReleaseDC(HWND w,HDC d){return 1;}
/* PRODUCTION */
int main(void){_viv_mipmap_t*m=0;int w,h;assert(_viv_get_mipmap(7,8,8,3,3,&w,&h,&m)==7);assert(!m && bitmap_calls==1);fail_bitmap=0;assert(_viv_get_mipmap(7,8,8,3,3,&w,&h,&m)==99);assert(m && m->hbitmap==99 && bitmap_calls==2);free(m);puts("mipmap allocation failure preserves source and retries after recovery PASS");}
