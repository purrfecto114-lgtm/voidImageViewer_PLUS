typedef struct IDirect3DDevice9 IDirect3DDevice9;
typedef struct { int unused; } IDirect3DBaseTexture9;
typedef struct { UINT BackBufferWidth,BackBufferHeight; } D3DPRESENT_PARAMETERS;
typedef struct { float x,y,z,rhw,u,v; } _viv_d3d_vertex_t;
typedef struct {
 HRESULT (*Reset)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
 HRESULT (*Clear)(IDirect3DDevice9*,DWORD,const void*,DWORD,DWORD,float,DWORD);
 HRESULT (*BeginScene)(IDirect3DDevice9*);
 HRESULT (*SetRenderState)(IDirect3DDevice9*,DWORD,DWORD);
 HRESULT (*SetTexture)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*);
 HRESULT (*SetSamplerState)(IDirect3DDevice9*,DWORD,DWORD,DWORD);
 HRESULT (*SetFVF)(IDirect3DDevice9*,DWORD);
 HRESULT (*DrawPrimitiveUP)(IDirect3DDevice9*,DWORD,UINT,const void*,UINT);
 HRESULT (*EndScene)(IDirect3DDevice9*);
 HRESULT (*Present)(IDirect3DDevice9*,const void*,const void*,HWND,const void*);
 HRESULT (*TestCooperativeLevel)(IDirect3DDevice9*);
} VTABLE;
struct IDirect3DDevice9 { VTABLE *lpVtbl; };
#define D3DERR_DEVICELOST ((HRESULT)0x88760868u)
#define D3DERR_DEVICENOTRESET ((HRESULT)0x88760869u)
#define D3DCLEAR_TARGET 1
#define D3DCOLOR_XRGB(r,g,b) (0xff000000u|((r)<<16)|((g)<<8)|(b))
#define D3DRS_CULLMODE 22
#define D3DCULL_NONE 1
#define D3DRS_LIGHTING 137
#define D3DSAMP_MAGFILTER 5
#define D3DSAMP_MINFILTER 6
#define D3DSAMP_ADDRESSU 1
#define D3DSAMP_ADDRESSV 2
#define D3DTEXF_LINEAR 2
#define D3DTADDRESS_CLAMP 3
#define D3DFVF_XYZRHW 4
#define D3DFVF_TEX1 256
#define D3DPT_TRIANGLESTRIP 5
static int fault,draw_count,present_count,end_count,reset_count;
static _viv_d3d_vertex_t captured[4];
static HRESULT reset(IDirect3DDevice9*d,D3DPRESENT_PARAMETERS*p){reset_count++;return 0;}
static HRESULT clear(IDirect3DDevice9*d,DWORD n,const void*r,DWORD f,DWORD c,float z,DWORD s){return (fault==4||fault==7)?-1:0;}
static HRESULT begin(IDirect3DDevice9*d){return fault==1?-1:0;}
static HRESULT state(IDirect3DDevice9*d,DWORD a,DWORD b){return fault==6?-1:0;}
static HRESULT texture(IDirect3DDevice9*d,DWORD a,IDirect3DBaseTexture9*t){return 0;}
static HRESULT sampler(IDirect3DDevice9*d,DWORD a,DWORD b,DWORD c){return 0;}
static HRESULT fvf(IDirect3DDevice9*d,DWORD a){return 0;}
static HRESULT draw(IDirect3DDevice9*d,DWORD a,UINT n,const void*v,UINT stride){assert(stride==sizeof(captured[0]));memcpy(captured,v,sizeof(captured));draw_count++;return (fault==2||fault==8)?-1:0;}
static HRESULT end(IDirect3DDevice9*d){end_count++;return fault==5?-1:0;}
static HRESULT present(IDirect3DDevice9*d,const void*s,const void*r,HWND h,const void*region){present_count++;return fault==3?-1:0;}
static HRESULT cooperative(IDirect3DDevice9*d){return (fault==7||fault==8)?D3DERR_DEVICENOTRESET:0;}
static VTABLE vt={reset,clear,begin,state,texture,sampler,fvf,draw,end,present,cooperative};
static IDirect3DDevice9 device={&vt},*_viv_d3d_device=&device;
static IDirect3DBaseTexture9 *_viv_d3d_texture;
static int _viv_d3d_failed,_viv_d3d_client_wide=4,_viv_d3d_client_high=4;
static HBITMAP _viv_d3d_last_hbitmap;
static D3DPRESENT_PARAMETERS _viv_d3d_params;
static float _viv_d3d_u=1,_viv_d3d_v=1;
static BYTE *_viv_d3d_export_bits;
int _viv_d3d_export_filled;
static int _viv_d3d_init(HWND h){return 1;}
static int _viv_d3d_texture_upload(HBITMAP h,DIBSECTION*d){_viv_d3d_last_hbitmap=h;return 1;}
static void _viv_d3d_export_readback(void){_viv_d3d_export_filled=1;}
/* PRODUCTION */
int main(void){
 for(fault=0;fault<=8;fault++){
  draw_count=present_count=end_count=reset_count=0;
  int result=_viv_hwd3d_render(1,2,3,0,0,4,4,0);
  assert(result==(fault==0));
  if(fault==0){assert(captured[0].x==-0.5f && captured[0].y==-0.5f && captured[3].x==3.5f && captured[3].y==3.5f);assert(draw_count==1 && present_count==1);}
  if(fault==1 || fault==4 || fault==7)assert(end_count==0);else assert(end_count==1);
  if(fault==3){assert(captured[3].x==3.5f && captured[3].y==3.5f);}
  if(fault==7 || fault==8){assert(reset_count==1);assert((int)_viv_d3d_params.BackBufferWidth==_viv_d3d_client_wide && (int)_viv_d3d_params.BackBufferHeight==_viv_d3d_client_high);}else{assert(reset_count==0);}
 }
 puts("D3D half-pixel vertices, failure propagation, lost-device recovery PASS");
}
