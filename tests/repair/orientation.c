typedef struct {DWORD biSize;int biWidth,biHeight;unsigned short biPlanes,biBitCount;DWORD biCompression;} BITMAPINFOHEADER;
typedef struct {BITMAPINFOHEADER bmiHeader;} BITMAPINFO;
#define BI_RGB 0
#define DIB_RGB_COLORS 0
#define os_zero_memory(p,n) memset(p,0,n)
#define os_copy_memory(p,q,n) memcpy(p,q,n)
static size_t safe_size_mul(size_t a,size_t b){return b && a>SIZE_MAX/b?SIZE_MAX:a*b;}
static size_t safe_size_add(size_t a,size_t b){return a>SIZE_MAX-b?SIZE_MAX:a+b;}
static int heap_allocations,fail_dib;
static void *mem_alloc(size_t n){heap_allocations++;void*p=malloc(n);assert(p);return p;}
static void mem_free(void*p){free(p);}
typedef struct {BITMAP bm;BYTE bytes[64];} Surface;
#define GetObject orientation_get_object
static int orientation_get_object(HBITMAP b,int size,void*out){memcpy(out,(void*)b,size);return size;}
static HDC GetDC(HWND h){return 1;}
static HDC CreateCompatibleDC(HDC h){return 2;}
static int ReleaseDC(HWND h,HDC d){return 1;}
static BOOL DeleteDC(HDC h){return 1;}
static BOOL GdiFlush(void){return 1;}
static HBITMAP CreateDIBSection(HDC h,const BITMAPINFO*b,UINT u,void**bits,void*m,DWORD o){
 if(fail_dib) return 0;
 Surface*s=calloc(1,sizeof(*s));assert(s);int w=b->bmiHeader.biWidth,hi=-b->bmiHeader.biHeight,bpp=b->bmiHeader.biBitCount;
 s->bm=(BITMAP){w,hi,((w*bpp+31)/32)*4,1,bpp,s->bytes};*bits=s->bytes;return (HBITMAP)s;
}
static BOOL DeleteObject(HBITMAP b){free((void*)b);return 1;}
static int GetDIBits(HDC dc,HBITMAP b,UINT f,UINT rows,void*out,BITMAPINFO*bi,UINT u){
 Surface*s=(Surface*)b;DWORD*d=out;for(int y=0;y<s->bm.bmHeight;y++)for(int x=0;x<s->bm.bmWidth;x++){BYTE*p=s->bytes+y*s->bm.bmWidthBytes+x*(s->bm.bmBitsPixel/8);*d++=p[0]|((DWORD)p[1]<<8)|((DWORD)p[2]<<16);}return rows;
}
/* PRODUCTION */
int main(void){
 const int expected[9][6]={{0},{1,2,3,4,5,6},{2,1,4,3,6,5},{6,5,4,3,2,1},{5,6,3,4,1,2},{1,3,5,2,4,6},{5,3,1,6,4,2},{6,4,2,5,3,1},{2,4,6,1,3,5}};
 for(int bpp=24;bpp<=32;bpp+=8)for(int o=2;o<=8;o++){
  Surface src={0};src.bm=(BITMAP){2,3,8,1,bpp,src.bytes};for(int y=0;y<3;y++)for(int x=0;x<2;x++)for(int c=0;c<3;c++)src.bytes[y*8+x*(bpp/8)+c]=(BYTE)(1+y*2+x);
  heap_allocations=0;HBITMAP b=_viv_orientate_hbitmap((HBITMAP)&src,o);assert(b);Surface*dst=(Surface*)b;int w=o>=5?3:2,hi=o>=5?2:3;
  assert(dst->bm.bmWidth==w && dst->bm.bmHeight==hi);
  for(int y=0;y<hi;y++)for(int x=0;x<w;x++)for(int c=0;c<3;c++)assert(dst->bytes[y*dst->bm.bmWidthBytes+x*(dst->bm.bmBitsPixel/8)+c]==expected[o][y*w+x]);
  assert(heap_allocations==0);DeleteObject(b);fail_dib=1;assert(!_viv_orientate_hbitmap((HBITMAP)&src,o));fail_dib=0;assert(src.bytes[0]==1 && src.bytes[16]==5);
 }
 puts("24/32bpp transforms, padded rows, allocation failure and no full-image scratch PASS");
}
