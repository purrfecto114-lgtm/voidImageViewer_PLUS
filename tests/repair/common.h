#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
typedef uint8_t BYTE;
typedef uint32_t UINT,DWORD,COLORREF,ULONG;
typedef int32_t HRESULT;
typedef int BOOL;
typedef intptr_t HBITMAP,HDC,HWND,HGDIOBJ;
typedef size_t SIZE_T;
typedef struct { int left,top,right,bottom; } RECT;
typedef struct { int bmWidth,bmHeight,bmWidthBytes,bmPlanes,bmBitsPixel;void *bmBits; } BITMAP;
typedef struct { BITMAP dsBm; } DIBSECTION;
#define TRUE 1
#define FALSE 0
#define SUCCEEDED(x) ((HRESULT)(x)>=0)
#define FAILED(x) ((HRESULT)(x)<0)
#define GetRValue(x) ((x)&255)
#define GetGValue(x) (((x)>>8)&255)
#define GetBValue(x) (((x)>>16)&255)
#define debug_printf(...) ((void)0)
#define VIV_MAX_IMAGE_BYTES 2400000000ULL
static DWORD GetLastError(void) { return 8; }
static BOOL GetClientRect(HWND h,RECT *r) { *r=(RECT){0,0,4,4};return 1; }
static int GetObject(HBITMAP b,int size,void *out) {
    static BYTE pixels[64];DIBSECTION ds={{4,4,16,1,32,pixels}};
    memcpy(out,&ds,(size_t)size<sizeof(ds)?(size_t)size:sizeof(ds));return size;
}
