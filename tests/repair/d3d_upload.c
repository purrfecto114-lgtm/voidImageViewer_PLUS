typedef struct IDirect3DDevice9 IDirect3DDevice9;
typedef struct IDirect3DTexture9 IDirect3DTexture9;
typedef IDirect3DTexture9 IDirect3DBaseTexture9;
typedef struct {int Pitch;void*pBits;} D3DLOCKED_RECT;
typedef struct {ULONG(*Release)(IDirect3DTexture9*);HRESULT(*LockRect)(IDirect3DTexture9*,UINT,D3DLOCKED_RECT*,void*,DWORD);HRESULT(*UnlockRect)(IDirect3DTexture9*,UINT);} TVTABLE;
struct IDirect3DTexture9 {TVTABLE*lpVtbl;int refs,wide,high;BYTE data[1024];};
typedef struct {HRESULT(*SetTexture)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*);HRESULT(*CreateTexture)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,int,int,IDirect3DTexture9**,void*);} DVTABLE;
struct IDirect3DDevice9 {DVTABLE*lpVtbl;};
#define D3DFMT_X8R8G8B8 22
#define D3DPOOL_MANAGED 1
#define VIV_MAX_HARDWARE_BYTES (512u*1024u*1024u)
#define VIV_MAX_IMAGE_PIXELS 400000000
static int live,fail_create,fail_lock,fail_unlock;
static IDirect3DTexture9*bound;
static ULONG release(IDirect3DTexture9*t){assert(t->refs>0);int refs=--t->refs;if(!refs){live--;free(t);}return refs;}
static HRESULT lock(IDirect3DTexture9*t,UINT l,D3DLOCKED_RECT*r,void*p,DWORD f){if(fail_lock)return -1;*r=(D3DLOCKED_RECT){t->wide*4,t->data};return 0;}
static HRESULT unlock(IDirect3DTexture9*t,UINT l){return fail_unlock?-1:0;}
static TVTABLE tvt={release,lock,unlock};
static HRESULT set_texture(IDirect3DDevice9*d,DWORD s,IDirect3DBaseTexture9*t){if(t)t->refs++;if(bound)release(bound);bound=t;return 0;}
static HRESULT create_texture(IDirect3DDevice9*d,UINT w,UINT h,UINT l,DWORD f,int fmt,int pool,IDirect3DTexture9**out,void*shared){if(fail_create)return -1;assert(w*h*4<=1024);IDirect3DTexture9*t=calloc(1,sizeof(*t));assert(t);t->lpVtbl=&tvt;t->wide=w;t->high=h;t->refs=1;live++;*out=t;return 0;}
static DVTABLE dvt={set_texture,create_texture};static IDirect3DDevice9 device={&dvt},*_viv_d3d_device=&device;
static IDirect3DTexture9*_viv_d3d_texture;
static int _viv_d3d_need_pot,_viv_d3d_need_square,_viv_d3d_last_pot_wide,_viv_d3d_last_pot_high,_viv_d3d_max_wide=16384,_viv_d3d_max_high=16384;
static HBITMAP _viv_d3d_last_hbitmap;static float _viv_d3d_u,_viv_d3d_v;
static size_t safe_size_mul(size_t a,size_t b){return b && a>SIZE_MAX/b?SIZE_MAX:a*b;}
/* PRODUCTION */
int main(void){
 BYTE pixels[256];memset(pixels,255,sizeof(pixels));DIBSECTION ds={{3,5,12,1,32,pixels}};
 assert(_viv_d3d_texture_upload(7,&ds));assert(live==1);set_texture(&device,0,_viv_d3d_texture);assert(_viv_d3d_texture->refs==2);
 ds.dsBm.bmWidth=4;ds.dsBm.bmHeight=6;assert(_viv_d3d_texture_upload(8,&ds));assert(live==1 && !bound);set_texture(&device,0,_viv_d3d_texture);
 fail_create=1;ds.dsBm.bmWidth=5;ds.dsBm.bmHeight=7;assert(!_viv_d3d_texture_upload(9,&ds));assert(!live && !bound && !_viv_d3d_texture);fail_create=0;
 fail_lock=1;assert(!_viv_d3d_texture_upload(10,&ds));assert(!live && !_viv_d3d_texture);fail_lock=0;
 fail_unlock=1;assert(!_viv_d3d_texture_upload(11,&ds));assert(!live && !_viv_d3d_last_hbitmap);fail_unlock=0;
 ds.dsBm.bmWidth=10000;ds.dsBm.bmHeight=10000;assert(!_viv_d3d_texture_upload(12,&ds));assert(!live);
 _viv_d3d_need_pot=1;ds.dsBm.bmWidth=3;ds.dsBm.bmHeight=5;assert(_viv_d3d_texture_upload(13,&ds));assert(_viv_d3d_last_pot_wide==4 && _viv_d3d_last_pot_high==8);assert(_viv_d3d_texture->data[(7*4+3)*4]==255);
 set_texture(&device,0,_viv_d3d_texture);_viv_hwd3d_release_image();assert(!live && !bound);puts("D3D refs, allocation/lock/unlock failures, POT padding and byte budget PASS");
}
