#include <time.h>

typedef int32_t LONG;
typedef struct { DWORD biSize; int biWidth,biHeight; unsigned short biPlanes,biBitCount; DWORD biCompression; } BITMAPINFOHEADER;
typedef struct { BITMAPINFOHEADER bmiHeader; } BITMAPINFO;
typedef struct { BITMAP bm; BYTE bytes[]; } Surface;
#define BI_RGB 0
#define DIB_RGB_COLORS 0
#define VIV_MAX_ROTATION_BYTES (128u * 1024u * 1024u)
#define os_zero_memory(p,n) memset(p,0,n)
static size_t safe_size_mul(size_t a,size_t b) { return b && a>SIZE_MAX/b?SIZE_MAX:a*b; }
static size_t safe_size_add(size_t a,size_t b) { return a>SIZE_MAX-b?SIZE_MAX:a+b; }
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG expected) { return *p; }
static int live_surfaces,created,fail_after=-1;
static void *mem_try_alloc(size_t n) { return malloc(n); }
static void mem_free(void *p) { free(p); }
static Surface *surface(int w,int h,int bpp) {
    int stride=((w*bpp+31)/32)*4;
    Surface *s=malloc(sizeof(*s)+(size_t)stride*h);
    assert(s);
    s->bm=(BITMAP){w,h,stride,1,bpp,s->bytes};
    memset(s->bytes,0xcc,(size_t)stride*h);
    live_surfaces++;
    return s;
}

#define GetObject orientation_get_object
static int orientation_get_object(HBITMAP b,int size,void *out) {
    assert(size==sizeof(BITMAP));
    memcpy(out,&((Surface *)b)->bm,size);
    return size;
}
static BOOL GdiFlush(void) { return TRUE; }
static HBITMAP CreateDIBSection(HDC dc,const BITMAPINFO *b,UINT u,void **bits,void *mapped,DWORD offset) {
    Surface *s;
    if (fail_after==0) return 0;
    if (fail_after>0) fail_after--;
    s=surface(b->bmiHeader.biWidth,-b->bmiHeader.biHeight,b->bmiHeader.biBitCount);
    *bits=s->bytes;
    created++;
    return (HBITMAP)s;
}
static BOOL DeleteObject(HBITMAP b) { live_surfaces--; free((void *)b); return TRUE; }
static BYTE *pixel(Surface *s,int x,int y) {
    return s->bytes+(size_t)y*s->bm.bmWidthBytes+(size_t)x*(s->bm.bmBitsPixel/8);
}
static void fill(Surface *s) {
    for (int y=0; y<s->bm.bmHeight; y++)
        for (int x=0; x<s->bm.bmWidth; x++) {
            BYTE *p=pixel(s,x,y);
            for (int c=0; c<s->bm.bmBitsPixel/8; c++) p[c]=(BYTE)(x*(7+c) + y*(13-c) + 55*c);
        }
}

/* PRODUCTION */

int main(void) {
    const int expected[9][6]={{0},{1,2,3,4,5,6},{2,1,4,3,6,5},{6,5,4,3,2,1},{5,6,3,4,1,2},
                             {1,3,5,2,4,6},{5,3,1,6,4,2},{6,4,2,5,3,1},{2,4,6,1,3,5}};
    for (int bpp=24; bpp<=32; bpp+=8) {
        Surface *src=surface(2,3,bpp);
        for (int y=0; y<3; y++)
            for (int x=0; x<2; x++)
                for (int c=0; c<bpp/8; c++) pixel(src,x,y)[c]=(BYTE)(1+y*2+x+c);
        for (int o=1; o<=8; o++) {
            int w=o>=5?3:2,h=o>=5?2:3;
            Surface *dst=(Surface *)_viv_orientate_hbitmap((HBITMAP)src,o);
            assert(dst && dst->bm.bmWidth==w && dst->bm.bmHeight==h);
            for (int y=0; y<h; y++) {
                for (int x=0; x<w; x++)
                    for (int c=0; c<bpp/8; c++) assert(pixel(dst,x,y)[c]==expected[o][y*w+x]+c);
                for (int p=w*(bpp/8); p<dst->bm.bmWidthBytes; p++) assert(dst->bytes[y*dst->bm.bmWidthBytes+p]==0);
            }
            DeleteObject((HBITMAP)dst);
            fail_after=0;
            assert(!_viv_orientate_hbitmap((HBITMAP)src,o));
            fail_after=-1;
            assert(src->bytes[0]==1 && pixel(src,0,2)[0]==5);
        }
        DeleteObject((HBITMAP)src);
    }

    for (int bpp=24; bpp<=32; bpp+=8) {
        Surface *src=surface(3841,2160,bpp);
        double quickest=999;
        fill(src);
        for (int i=0; i<3; i++) {
            clock_t start=clock();
            Surface *dst=(Surface *)_viv_orientate_hbitmap((HBITMAP)src,6);
            double ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;
            if (ms<quickest) quickest=ms;
            assert(dst && dst->bm.bmWidth==2160 && dst->bm.bmHeight==3841);
            for (int y=0; y<2160; y++)
                for (int x=0; x<3841; x++)
                    assert(!memcmp(pixel(src,x,y),pixel(dst,2159-y,x),bpp/8));
            DeleteObject((HBITMAP)dst);
        }
        printf("3841 x 2160 %dbpp: all pixels correct; best of 3 host rotations %.1f ms\n",bpp,quickest);

#ifdef VIV_TEST_ORIENTED_FRAMES
        {
            BITMAP inputs[6]={src->bm,src->bm,src->bm,src->bm,src->bm,src->bm};
            volatile LONG cancelled=1;
            int before=created;
            assert(!_viv_prepare_oriented_frames(inputs,6,6,NULL) && created==before);
            assert(!_viv_prepare_oriented_frames(inputs,2,6,&cancelled) && created==before);
            fail_after=1;
            assert(!_viv_prepare_oriented_frames(inputs,2,6,NULL) && live_surfaces==1);
            fail_after=-1;
        }
#endif
        DeleteObject((HBITMAP)src);
    }
    assert(!live_surfaces);
    puts("orientation: eight transforms, 24/32bpp, row padding, allocation, memory cap and cancellation PASS");
    return 0;
}
