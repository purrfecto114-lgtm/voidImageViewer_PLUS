typedef struct { UINT Width, Height, Usage, BindFlags, CPUAccessFlags; } D3D11_TEXTURE2D_DESC;
typedef struct { void *pData; UINT RowPitch; } D3D11_MAPPED_SUBRESOURCE;
typedef struct { int alive; } ID3D11Texture2D;
typedef ID3D11Texture2D ID3D11Resource;
typedef struct { int alive; } ID3D11ShaderResourceView;

#define D3D11_USAGE_DYNAMIC 2
#define D3D11_BIND_SHADER_RESOURCE 8
#define D3D11_CPU_ACCESS_WRITE 65536
#define D3D11_MAP_WRITE_DISCARD 4
#define S_OK 0
#define E_FAIL (-1)
#define E_OUTOFMEMORY (-2)
#define RELEASE(p) do { if (p) { assert((p)->alive); (p)->alive=0; (p)=NULL; } } while (0)
#define GdiFlush() (1)

static int limits,fail_map,create_calls,map_calls;
static BYTE texture_rows[32];
static ID3D11Texture2D texture;
static ID3D11ShaderResourceView view;
static struct {
    int device,context;
    ID3D11Texture2D *texture;
    ID3D11ShaderResourceView *image;
    HBITMAP last_bitmap;
    int image_wide,image_high;
} gpu = {1,1,0,0,0,0,0};

static void ID3D11DeviceContext_PSSetShaderResources(int ctx,UINT start,UINT count,ID3D11ShaderResourceView **views) {}
static void texture_desc(D3D11_TEXTURE2D_DESC *desc,int wide,int high) {
    *desc=(D3D11_TEXTURE2D_DESC){(UINT)wide,(UINT)high,0,0,0};
}
static int within_adapter_budget(size_t bytes) { return limits; }
static HRESULT ID3D11Device_CreateTexture2D(int device,const D3D11_TEXTURE2D_DESC *desc,
    void *init,ID3D11Texture2D **out) {
    assert(!texture.alive && desc->Width==3 && desc->Height==2);
    assert(desc->Usage==D3D11_USAGE_DYNAMIC && desc->BindFlags==D3D11_BIND_SHADER_RESOURCE);
    ++create_calls;
    texture.alive=1;
    *out=&texture;
    return S_OK;
}
static HRESULT ID3D11Device_CreateShaderResourceView(int device,ID3D11Resource *resource,
    void *desc,ID3D11ShaderResourceView **out) {
    assert(resource->alive && !view.alive);
    view.alive=1;
    *out=&view;
    return S_OK;
}
static HRESULT ID3D11DeviceContext_Map(int ctx,ID3D11Resource *resource,UINT sub,
    int kind,UINT flags,D3D11_MAPPED_SUBRESOURCE *map) {
    assert(resource->alive && kind==D3D11_MAP_WRITE_DISCARD);
    ++map_calls;
    if (fail_map) return E_FAIL;
    *map=(D3D11_MAPPED_SUBRESOURCE){texture_rows,16};
    return S_OK;
}
static void ID3D11DeviceContext_Unmap(int ctx,ID3D11Resource *resource,UINT sub) {}

/* PRODUCTION */

int main(void) {
    BYTE input[32] = {1,2,3, 4,5,6, 7,8,9, 88,89,90,
                      21,22,23, 24,25,26, 27,28,29, 91,92,93};
    static const BYTE top[]={1,2,3,255,4,5,6,255,7,8,9,255};
    static const BYTE bottom[]={21,22,23,255,24,25,26,255,27,28,29,255};
    DIBSECTION dib={{3,2,12,1,24,input}};

    assert(FAILED(upload_image(1,&dib,48)) && !create_calls); /* adapter budget */
    limits=1;
    assert(SUCCEEDED(upload_image(1,&dib,48)));
    assert(create_calls==1 && map_calls==1 && gpu.last_bitmap==1);
    assert(!memcmp(texture_rows,top,sizeof(top)) && !memcmp(texture_rows+16,bottom,sizeof(bottom)));
    assert(SUCCEEDED(upload_image(1,&dib,48)) && map_calls==1); /* same static frame */

    fail_map=1;
    assert(FAILED(upload_image(2,&dib,48)) && gpu.last_bitmap==1);
    fail_map=0;
    dib.dsBm.bmBitsPixel=32;
    for (int i=0;i<24;i++) input[i]=(BYTE)(60+i);
    assert(SUCCEEDED(upload_image(2,&dib,48)) && gpu.last_bitmap==2);
    assert(create_calls==1 && map_calls==3);
    assert(!memcmp(texture_rows,input,12) && !memcmp(texture_rows+16,input+12,12));
    _viv_modern_release_image();
    assert(!texture.alive && !view.alive && !gpu.last_bitmap);
    puts("D3D11/12 upload: budget, 24/32-bit stride and orientation, reuse, failed map and release PASS");
    return 0;
}
