//
// Copyright 2026 voidtools / David Carpenter
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// the direct3d 11 renderer: the same shape the d3d9 module rides (one
// window device, one live image texture, the sticky refusal, the export
// readback) with the d3d11 answers to the same questions. the memory
// story the rc.3 round was asked to mind: exactly one image texture
// lives at a time (released the moment another image shows), uploads
// ride UpdateSubresource straight from the dib section's own bits (no
// staging pool, no managed system copy - the driver holds the bytes
// once), and the shader pair and the quad buffer are the whole
// per-window state. the module never touches the exe's import table
// (the os.c gdi+ table precedent): d3d11.dll and d3dcompiler_47.dll
// resolve through getprocaddress at first use.
#include "viv.h"
#include "hwd3d11.h"

#include <d3d11.h>
#include <d3dcompiler.h>

// the session refusal flag lives in viv_state.h (this module includes
// viv.h, not viv_state.h).
extern BYTE _viv_hw_render_fallback;

// the two guid constants the create and the getbuffer calls want (local,
// so the uuid import stays out of the link line).
static const GUID _viv_d3d11_iid_device = {0xdb6f6ddb,0xac77,0x4e88,{0x82,0x53,0x81,0x9d,0xf9,0xbb,0xf1,0x40}};
static const GUID _viv_d3d11_iid_texture = {0x6f88a357,0x34ba,0x4d65,{0xa1,0x1e,0xd0,0x06,0x3e,0x6f,0x4e,0x93}};

typedef HRESULT (WINAPI *_viv_d3d11_create_t)(IDXGIAdapter *pAdapter,D3D_DRIVER_TYPE DriverType,HMODULE Software,UINT Flags,const D3D_FEATURE_LEVEL *pFeatureLevels,UINT FeatureLevels,UINT SDKVersion,const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,IDXGISwapChain **ppSwapChain,ID3D11Device **ppDevice,D3D_FEATURE_LEVEL *pFeatureLevel,ID3D11DeviceContext **ppImmediateContext);
typedef HRESULT (WINAPI *_viv_d3d11_compile_t)(LPCSTR pSrcData,SIZE_T SrcDataSize,const char *pSourceName,const D3D_SHADER_MACRO *pDefines,ID3DInclude *pInclude,const char *pEntrypoint,const char *pTarget,UINT Flags,UINT Flags2,ID3DBlob **ppCode,ID3DBlob **ppErrorMsgs);

static HMODULE _viv_d3d11_module;
static HMODULE _viv_d3d11_compiler_module;
static _viv_d3d11_create_t _viv_d3d11_create;
static _viv_d3d11_compile_t _viv_d3d11_compile;

static IDXGISwapChain *_viv_d3d11_swap;
static ID3D11Device *_viv_d3d11_device;
static ID3D11DeviceContext *_viv_d3d11_context;
static ID3D11RenderTargetView *_viv_d3d11_rt;
static ID3D11VertexShader *_viv_d3d11_vs;
static ID3D11PixelShader *_viv_d3d11_ps;
static ID3D11InputLayout *_viv_d3d11_layout;
static ID3D11Buffer *_viv_d3d11_quad;
static ID3D11SamplerState *_viv_d3d11_sampler;
static ID3D11BlendState *_viv_d3d11_blend;

static ID3D11Texture2D *_viv_d3d11_texture;
static ID3D11ShaderResourceView *_viv_d3d11_srv;

static HWND _viv_d3d11_hwnd;
static int _viv_d3d11_client_wide;
static int _viv_d3d11_client_high;
static int _viv_d3d11_failed;
static HBITMAP _viv_d3d11_last_hbitmap;
static int _viv_d3d11_tex_wide;
static int _viv_d3d11_tex_high;
static unsigned _viv_d3d11_selfcheck_done;

static BYTE *_viv_d3d11_scratch;
static SIZE_T _viv_d3d11_scratch_size;

static BYTE *_viv_d3d11_export_bits;
static int _viv_d3d11_export_wide;
static int _viv_d3d11_export_high;
static int _viv_d3d11_export_filled;

// the probe's process-lifetime cache (the d3d9 module's shape): a machine
// does not change its mind about d3d11 while the viewer runs.
static int _viv_d3d11_probe_answered;
static int _viv_d3d11_probe_ok;

// the quad's shader pair: the vertex shader passes the clip space pair
// the render math computed straight through, the pixel shader is one
// sample. the 9_1 targets keep the floor hardware in the ladder.
static const char _viv_d3d11_vs_src[] =
"struct v_in { float2 p : P; float2 t : T; };"
"struct v_out { float4 p : SV_Position; float2 t : T; };"
"v_out main(v_in i) { v_out o; o.p = float4(i.p, 0.0f, 1.0f); o.t = i.t; return o; }";

static const char _viv_d3d11_ps_src[] =
"Texture2D tex : register(t0);"
"SamplerState smp : register(s0);"
"float4 main(float4 p : SV_Position, float2 t : T) : SV_Target { return tex.Sample(smp, t); }";

// the quad vertex: clip space position plus the texture coordinate.
typedef struct _viv_d3d11_vertex_s
{
	float x;
	float y;
	float u;
	float v;
}_viv_d3d11_vertex_t;

static int _viv_d3d11_retarget(void);
static int _viv_d3d11_selfcheck(HBITMAP hbitmap,DIBSECTION *ds);
static void _viv_d3d11_export_readback(void);

int _viv_hwd3d11_available(void)
{
	if (_viv_d3d11_probe_answered)
	{
		return _viv_d3d11_probe_ok;
	}

	_viv_d3d11_probe_answered = 1;
	_viv_d3d11_probe_ok = 0;

	if (!_viv_d3d11_module)
	{
		_viv_d3d11_module = LoadLibraryA("d3d11.dll");

		if (!_viv_d3d11_module)
		{
			debug_printf("direct3d11 probe: d3d11.dll is not on this system\r\n");

			return 0;
		}
	}

	if (!_viv_d3d11_create)
	{
		_viv_d3d11_create = (_viv_d3d11_create_t)GetProcAddress(_viv_d3d11_module,"D3D11CreateDeviceAndSwapChain");

		if (!_viv_d3d11_create)
		{
			return 0;
		}
	}

	_viv_d3d11_probe_ok = 1;

	return 1;
}

static void _viv_d3d11_drop_rt(void)
{
	if (_viv_d3d11_rt)
	{
		_viv_d3d11_rt->lpVtbl->Release(_viv_d3d11_rt);

		_viv_d3d11_rt = NULL;
	}
}

static void _viv_d3d11_drop_swapchain(void)
{
	_viv_d3d11_drop_rt();

	if (_viv_d3d11_swap)
	{
		_viv_d3d11_swap->lpVtbl->Release(_viv_d3d11_swap);

		_viv_d3d11_swap = NULL;
	}
}

static void _viv_d3d11_drop_device(void)
{
	_viv_d3d11_drop_swapchain();

	if (_viv_d3d11_blend)
	{
		_viv_d3d11_blend->lpVtbl->Release(_viv_d3d11_blend);

		_viv_d3d11_blend = NULL;
	}

	if (_viv_d3d11_sampler)
	{
		_viv_d3d11_sampler->lpVtbl->Release(_viv_d3d11_sampler);

		_viv_d3d11_sampler = NULL;
	}

	if (_viv_d3d11_quad)
	{
		_viv_d3d11_quad->lpVtbl->Release(_viv_d3d11_quad);

		_viv_d3d11_quad = NULL;
	}

	if (_viv_d3d11_layout)
	{
		_viv_d3d11_layout->lpVtbl->Release(_viv_d3d11_layout);

		_viv_d3d11_layout = NULL;
	}

	if (_viv_d3d11_ps)
	{
		_viv_d3d11_ps->lpVtbl->Release(_viv_d3d11_ps);

		_viv_d3d11_ps = NULL;
	}

	if (_viv_d3d11_vs)
	{
		_viv_d3d11_vs->lpVtbl->Release(_viv_d3d11_vs);

		_viv_d3d11_vs = NULL;
	}

	if (_viv_d3d11_context)
	{
		_viv_d3d11_context->lpVtbl->Release(_viv_d3d11_context);

		_viv_d3d11_context = NULL;
	}

	if (_viv_d3d11_device)
	{
		_viv_d3d11_device->lpVtbl->Release(_viv_d3d11_device);

		_viv_d3d11_device = NULL;
	}

	_viv_d3d11_hwnd = 0;
	_viv_d3d11_client_wide = 0;
	_viv_d3d11_client_high = 0;
}

// the render target view over the swapchain's back buffer: created once
// per buffer size (the resize answer drops the view, keeps the buffers).
static int _viv_d3d11_retarget(void)
{
	ID3D11Texture2D *backbuffer;
	HRESULT hresult;
	D3D11_VIEWPORT viewport;

	_viv_d3d11_drop_rt();

	backbuffer = NULL;

	hresult = _viv_d3d11_swap->lpVtbl->GetBuffer(_viv_d3d11_swap,0,&_viv_d3d11_iid_texture,(void **)&backbuffer);

	if (FAILED(hresult))
	{
		return 0;
	}

	hresult = _viv_d3d11_device->lpVtbl->CreateRenderTargetView(_viv_d3d11_device,(ID3D11Resource *)backbuffer,NULL,&_viv_d3d11_rt);

	backbuffer->lpVtbl->Release(backbuffer);

	if (FAILED(hresult))
	{
		return 0;
	}

	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = (float)_viv_d3d11_client_wide;
	viewport.Height = (float)_viv_d3d11_client_high;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	_viv_d3d11_context->lpVtbl->RSSetViewports(_viv_d3d11_context,1,&viewport);

	return 1;
}

static int _viv_d3d11_init(HWND hwnd)
{
	DXGI_SWAP_CHAIN_DESC swap_desc;
	D3D_FEATURE_LEVEL feature_levels[4];
	D3D_FEATURE_LEVEL feature_answered;
	HRESULT hresult;
	RECT rect;
	ID3DBlob *vs_blob;
	ID3DBlob *ps_blob;
	D3D11_INPUT_ELEMENT_DESC layout[2];
	D3D11_BUFFER_DESC quad_desc;
	D3D11_SAMPLER_DESC sampler_desc;
	D3D11_BLEND_DESC blend_desc;

	if (_viv_d3d11_device)
	{
		if (_viv_d3d11_hwnd == hwnd) return 1;

		// a window change retires the whole device (the d3d9 init's
		// shape): the swapchain binds to its hwnd by contract.
		_viv_hwd3d11_release_image();
		_viv_d3d11_drop_device();
	}

	if (!_viv_d3d11_module)
	{
		_viv_d3d11_module = LoadLibraryA("d3d11.dll");

		if (!_viv_d3d11_module)
		{
			debug_printf("direct3d11: d3d11.dll is not on this system\r\n");

			_viv_d3d11_failed = 1;

			return 0;
		}
	}

	if ((!_viv_d3d11_create) || (!_viv_d3d11_compile))
	{
		_viv_d3d11_create = (_viv_d3d11_create_t)GetProcAddress(_viv_d3d11_module,"D3D11CreateDeviceAndSwapChain");

		if (!_viv_d3d11_compile)
		{
			if (!_viv_d3d11_compiler_module)
			{
				_viv_d3d11_compiler_module = LoadLibraryA("d3dcompiler_47.dll");
			}

			if (_viv_d3d11_compiler_module)
			{
				_viv_d3d11_compile = (_viv_d3d11_compile_t)GetProcAddress(_viv_d3d11_compiler_module,"D3DCompile");
			}
		}

		if ((!_viv_d3d11_create) || (!_viv_d3d11_compile))
		{
			_viv_d3d11_failed = 1;

			return 0;
		}
	}

	GetClientRect(hwnd,&rect);
	_viv_d3d11_client_wide = rect.right - rect.left;
	_viv_d3d11_client_high = rect.bottom - rect.top;

	ZeroMemory(&swap_desc,sizeof(swap_desc));
	swap_desc.BufferDesc.Width = _viv_d3d11_client_wide;
	swap_desc.BufferDesc.Height = _viv_d3d11_client_high;
	swap_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swap_desc.SampleDesc.Count = 1;
	swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_desc.BufferCount = 1;
	swap_desc.OutputWindow = hwnd;
	swap_desc.Windowed = TRUE;
	swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	// feature levels walk 11.0 down to 9.1: the same ladder a browser
	// walks - the quad needs nothing newer than the 9.1 floor.
	feature_levels[0] = D3D_FEATURE_LEVEL_11_0;
	feature_levels[1] = D3D_FEATURE_LEVEL_10_0;
	feature_levels[2] = D3D_FEATURE_LEVEL_9_3;
	feature_levels[3] = D3D_FEATURE_LEVEL_9_1;

	hresult = _viv_d3d11_create(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,feature_levels,4,D3D11_SDK_VERSION,&swap_desc,&_viv_d3d11_swap,&_viv_d3d11_device,&feature_answered,&_viv_d3d11_context);

	if (FAILED(hresult))
	{
		debug_printf("direct3d11: the device did not come up %x\r\n",(unsigned int)hresult);

		_viv_d3d11_failed = 1;

		return 0;
	}

	_viv_d3d11_hwnd = hwnd;

	// the shaders compile once per device (they die with it).
	vs_blob = NULL;
	ps_blob = NULL;

	hresult = _viv_d3d11_compile(_viv_d3d11_vs_src,sizeof(_viv_d3d11_vs_src) - 1,"viv",NULL,NULL,"main","vs_4_0_level_9_1",0,0,&vs_blob,NULL);

	if (SUCCEEDED(hresult))
	{
		hresult = _viv_d3d11_compile(_viv_d3d11_ps_src,sizeof(_viv_d3d11_ps_src) - 1,"viv",NULL,NULL,"main","ps_4_0_level_9_1",0,0,&ps_blob,NULL);
	}

	if (SUCCEEDED(hresult))
	{
		hresult = _viv_d3d11_device->lpVtbl->CreateVertexShader(_viv_d3d11_device,vs_blob->lpVtbl->GetBufferPointer(vs_blob),vs_blob->lpVtbl->GetBufferSize(vs_blob),NULL,&_viv_d3d11_vs);
	}

	if (SUCCEEDED(hresult))
	{
		hresult = _viv_d3d11_device->lpVtbl->CreatePixelShader(_viv_d3d11_device,ps_blob->lpVtbl->GetBufferPointer(ps_blob),ps_blob->lpVtbl->GetBufferSize(ps_blob),NULL,&_viv_d3d11_ps);
	}

	if (SUCCEEDED(hresult))
	{
		// two elements, one per semantic slot: the position pair and the
		// texture pair the vertex shader passes through.
		layout[0].SemanticName = "P";
		layout[0].SemanticIndex = 0;
		layout[0].Format = DXGI_FORMAT_R32G32_FLOAT;
		layout[0].InputSlot = 0;
		layout[0].AlignedByteOffset = 0;
		layout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		layout[0].InstanceDataStepRate = 0;

		layout[1].SemanticName = "T";
		layout[1].SemanticIndex = 0;
		layout[1].Format = DXGI_FORMAT_R32G32_FLOAT;
		layout[1].InputSlot = 0;
		layout[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		layout[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		layout[1].InstanceDataStepRate = 0;

		hresult = _viv_d3d11_device->lpVtbl->CreateInputLayout(_viv_d3d11_device,layout,2,vs_blob->lpVtbl->GetBufferPointer(vs_blob),vs_blob->lpVtbl->GetBufferSize(vs_blob),&_viv_d3d11_layout);
	}

	if (SUCCEEDED(hresult))
	{
		quad_desc.ByteWidth = sizeof(_viv_d3d11_vertex_t) * 4;
		quad_desc.Usage = D3D11_USAGE_DYNAMIC;
		quad_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		quad_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		quad_desc.MiscFlags = 0;
		quad_desc.StructureByteStride = 0;

		hresult = _viv_d3d11_device->lpVtbl->CreateBuffer(_viv_d3d11_device,&quad_desc,NULL,&_viv_d3d11_quad);
	}

	if (SUCCEEDED(hresult))
	{
		ZeroMemory(&sampler_desc,sizeof(sampler_desc));
		sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

		hresult = _viv_d3d11_device->lpVtbl->CreateSamplerState(_viv_d3d11_device,&sampler_desc,&_viv_d3d11_sampler);
	}

	if (SUCCEEDED(hresult))
	{
		ZeroMemory(&blend_desc,sizeof(blend_desc));
		blend_desc.RenderTarget[0].BlendEnable = FALSE;
		blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		hresult = _viv_d3d11_device->lpVtbl->CreateBlendState(_viv_d3d11_device,&blend_desc,&_viv_d3d11_blend);
	}

	if (vs_blob)
	{
		vs_blob->lpVtbl->Release(vs_blob);
	}

	if (ps_blob)
	{
		ps_blob->lpVtbl->Release(ps_blob);
	}

	if (FAILED(hresult))
	{
		debug_printf("direct3d11: the device state did not come up %x\r\n",(unsigned int)hresult);

		_viv_d3d11_drop_device();

		_viv_d3d11_failed = 1;

		return 0;
	}

	if (!_viv_d3d11_retarget())
	{
		_viv_d3d11_drop_device();

		_viv_d3d11_failed = 1;

		return 0;
	}

	return 1;
}

// the upload: a b8g8r8a8 default-heap texture fed straight from the dib
// section's bits through UpdateSubresource - the driver copies once, no
// staging lingers, no managed system copy doubles the memory (the d3d9
// module's managed pool answer, minus the copy the pool keeps).
static int _viv_d3d11_texture_upload(HBITMAP hbitmap,DIBSECTION *ds)
{
	int wide;
	int high;
	int stride;
	D3D11_TEXTURE2D_DESC tex_desc;
	D3D11_SUBRESOURCE_DATA init_data;
	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
	HRESULT hresult;

	wide = ds->dsBm.bmWidth;
	high = ds->dsBm.bmHeight;

	if ((wide <= 0) || (high <= 0))
	{
		return 0;
	}

	if ((wide > 16384) || (high > 16384))
	{
		debug_printf("direct3d11: the image exceeds the max texture size\r\n");

		return 0;
	}

	if (safe_size_mul(safe_size_mul((SIZE_T)wide,(SIZE_T)high),4) > VIV_MAX_HARDWARE_BYTES)
	{
		debug_printf("direct3d11: the texture exceeds the hardware byte budget\r\n");

		return 0;
	}

	// the byte gate bounds the row; calculate before narrowing.
	stride = (int)((((SIZE_T)wide * ds->dsBm.bmBitsPixel + 31) / 32) * 4);

	// a 24bpp source does not speak bgra: the rows expand into the
	// session's one scratch buffer (b,g,r,255 quads - the same expansion
	// the d3d9 lock loop runs) before the upload. the scratch lives
	// across the session, grows once per size class and dies with the
	// module - no per-upload malloc, no second copy past the expansion.
	if (ds->dsBm.bmBitsPixel == 24)
	{
		SIZE_T need;

		need = safe_size_mul(safe_size_mul((SIZE_T)wide,(SIZE_T)high),4);

		if ((safe_size_mul((SIZE_T)wide,(SIZE_T)high) == 0) || (need == 0))
		{
			return 0;
		}

		if ((safe_size_mul((SIZE_T)wide,(SIZE_T)high) == 0) || (need > VIV_MAX_HARDWARE_BYTES))
		{
			debug_printf("direct3d11: the 24bpp expansion exceeds the hardware byte budget\r\n");

			return 0;
		}

		if (_viv_d3d11_scratch_size < need)
		{
			BYTE *grown;

			// the pool carries no realloc: a fresh block, the old one
			// freed (the scratch's contents are rebuilt every upload).
			grown = (BYTE *)mem_alloc(need);

			if (!grown)
			{
				return 0;
			}

			if (_viv_d3d11_scratch)
			{
				mem_free(_viv_d3d11_scratch);
			}

			_viv_d3d11_scratch = grown;
			_viv_d3d11_scratch_size = need;
		}

		{
			const BYTE *srow;
			BYTE *drow;
			int y;
			int x;

			srow = (const BYTE *)ds->dsBm.bmBits;
			drow = _viv_d3d11_scratch;

			for(y=0;y<high;y++)
			{
				const BYTE *sp;
				BYTE *dp;

				sp = srow;
				dp = drow;

				for(x=0;x<wide;x++)
				{
					dp[0] = sp[0]; // b
					dp[1] = sp[1]; // g
					dp[2] = sp[2]; // r
					dp[3] = 255;
					dp += 4;
					sp += 3;
				}

				srow += stride;
				drow += (uintptr_t)wide * 4;
			}
		}
	}

	// exactly one image texture lives at a time: the dimension change
	// releases the old one first (the d3d9 reuse gate's shape - the same
	// dimensions refill in place through the update below).
	if ((_viv_d3d11_texture) && ((wide != _viv_d3d11_tex_wide) || (high != _viv_d3d11_tex_high)))
	{
		_viv_hwd3d11_release_image();
	}

	if (!_viv_d3d11_texture)
	{
		ZeroMemory(&tex_desc,sizeof(tex_desc));
		tex_desc.Width = (UINT)wide;
		tex_desc.Height = (UINT)high;
		tex_desc.MipLevels = 1;
		tex_desc.ArraySize = 1;
		tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		tex_desc.SampleDesc.Count = 1;
		tex_desc.Usage = D3D11_USAGE_DEFAULT;
		tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		// the source: the scratch's expanded quads for 24bpp, the dib's
		// own rows for 32bpp (b,g,r,x is the format's own byte order).
		init_data.pSysMem = (ds->dsBm.bmBitsPixel == 24) ? (const void *)_viv_d3d11_scratch : (const void *)ds->dsBm.bmBits;
		init_data.SysMemPitch = (UINT)((ds->dsBm.bmBitsPixel == 24) ? wide * 4 : stride);
		init_data.SysMemSlicePitch = 0;

		hresult = _viv_d3d11_device->lpVtbl->CreateTexture2D(_viv_d3d11_device,&tex_desc,&init_data,&_viv_d3d11_texture);

		if (FAILED(hresult))
		{
			debug_printf("direct3d11: CreateTexture2D failed %x\r\n",(unsigned int)hresult);

			return 0;
		}

		_viv_d3d11_tex_wide = wide;
		_viv_d3d11_tex_high = high;
	}
	else
	{
		D3D11_BOX box;

		box.left = 0;
		box.top = 0;
		box.front = 0;
		box.right = (UINT)wide;
		box.bottom = (UINT)high;
		box.back = 1;

		// the refill: rows straight from the create's source (scratch or
		// dib), the same pitch - no staging, no map, no copies past the
		// one the driver does.
		_viv_d3d11_context->lpVtbl->UpdateSubresource(_viv_d3d11_context,(ID3D11Resource *)_viv_d3d11_texture,0,&box,(ds->dsBm.bmBitsPixel == 24) ? (const void *)_viv_d3d11_scratch : (const void *)ds->dsBm.bmBits,(UINT)((ds->dsBm.bmBitsPixel == 24) ? wide * 4 : stride),0);
	}

	if (!_viv_d3d11_srv)
	{
		ZeroMemory(&srv_desc,sizeof(srv_desc));
		srv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.MipLevels = 1;

		hresult = _viv_d3d11_device->lpVtbl->CreateShaderResourceView(_viv_d3d11_device,(ID3D11Resource *)_viv_d3d11_texture,&srv_desc,&_viv_d3d11_srv);

		if (FAILED(hresult))
		{
			debug_printf("direct3d11: CreateShaderResourceView failed %x\r\n",(unsigned int)hresult);

			_viv_hwd3d11_release_image();

			return 0;
		}
	}

	_viv_d3d11_last_hbitmap = hbitmap;
	_viv_d3d11_selfcheck_done = 0;

	return 1;
}

// the quad into clip space: the pixel offset -0.5 the raster wants so the
// texel centers land on the destination centers (the d3d9 vertex math's
// own shape), the half texel the linear sampler reads at the sub
// rectangle edge stays honest through the clamp addressing (the uv runs
// 0..1 over the image's own texels - the texture holds no pad).
static void _viv_d3d11_quad_fill(_viv_d3d11_vertex_t *verts,int dst_x,int dst_y,int dst_wide,int dst_high,int client_wide,int client_high,float *u,float *v)
{
	float x0;
	float y0;
	float x1;
	float y1;

	x0 = (((float)dst_x - 0.5f) / (float)client_wide) * 2.0f - 1.0f;
	y0 = 1.0f - (((float)dst_y - 0.5f) / (float)client_high) * 2.0f;
	x1 = (((float)(dst_x + dst_wide) - 0.5f) / (float)client_wide) * 2.0f - 1.0f;
	y1 = 1.0f - (((float)(dst_y + dst_high) - 0.5f) / (float)client_high) * 2.0f;

	verts[0].x = x0;
	verts[0].y = y0;
	verts[0].u = 0.0f;
	verts[0].v = 0.0f;

	verts[1].x = x1;
	verts[1].y = y0;
	verts[1].u = *u;
	verts[1].v = 0.0f;

	verts[2].x = x0;
	verts[2].y = y1;
	verts[2].u = 0.0f;
	verts[2].v = *v;

	verts[3].x = x1;
	verts[3].y = y1;
	verts[3].u = *u;
	verts[3].v = *v;
}

int _viv_hwd3d11_render(HWND hwnd,HDC hdc,HBITMAP hbitmap,int dst_x,int dst_y,int dst_wide,int dst_high,COLORREF clear_color)
{
	DIBSECTION ds;
	_viv_d3d11_vertex_t verts[4];
	D3D11_MAPPED_SUBRESOURCE mapped;
	float u;
	float v;
	float clear_rgba[4];
	HRESULT hresult;

	if (_viv_d3d11_failed)
	{
		return 0;
	}

	if ((!hbitmap) || (dst_wide <= 0) || (dst_high <= 0))
	{
		return 0;
	}

	if (GetObject(hbitmap,sizeof(DIBSECTION),&ds) == 0)
	{
		return 0;
	}

	if ((!ds.dsBm.bmBits) || ((ds.dsBm.bmBitsPixel != 24) && (ds.dsBm.bmBitsPixel != 32)))
	{
		// the refusal names its reason (the d3d9 twin carries the full
		// story): a silent return 0 is how an entire decoder family hid
		// behind this gate while the pixel oracle recorded nulls.
		if (!ds.dsBm.bmBits)
		{
			debug_printf("direct3d11: the frame is not a dib section (no bits answered) - the gdi path paints it\r\n");
		}
		else
		{
			debug_printf("direct3d11: the frame is %d bpp (24 or 32 answer) - the gdi path paints it\r\n",ds.dsBm.bmBitsPixel);
		}

		return 0;
	}

	if (!_viv_d3d11_init(hwnd))
	{
		return 0;
	}

	// the resize answer: the buffers resize (the view drops with them,
	// the texture survives - a default heap resource owes the device
	// nothing across a buffer rotate).
	{
		RECT rect;
		int wide;
		int high;

		GetClientRect(hwnd,&rect);
		wide = rect.right - rect.left;
		high = rect.bottom - rect.top;

		if ((!wide) || (!high))
		{
			return 0;
		}

		if ((wide != _viv_d3d11_client_wide) || (high != _viv_d3d11_client_high))
		{
			_viv_d3d11_drop_rt();

			hresult = _viv_d3d11_swap->lpVtbl->ResizeBuffers(_viv_d3d11_swap,0,(UINT)wide,(UINT)high,DXGI_FORMAT_UNKNOWN,0);

			if (FAILED(hresult))
			{
				debug_printf("direct3d11: the buffers did not resize %x\r\n",(unsigned int)hresult);

				return 0;
			}

			_viv_d3d11_client_wide = wide;
			_viv_d3d11_client_high = high;

			if (!_viv_d3d11_retarget())
			{
				return 0;
			}
		}
	}

	if (hbitmap != _viv_d3d11_last_hbitmap)
	{
		// an oversized image is a per-image refusal, not a session failure.
		if (!_viv_d3d11_texture_upload(hbitmap,&ds))
		{
			return 0;
		}
	}

	// the d3d9 flip contract: every frame this app builds is a top-down
	// dib and the sign is unrecoverable through getobject - the screen's
	// top edge samples the texture's row zero, the image's own top.
	u = 1.0f;
	v = 1.0f;

	_viv_d3d11_quad_fill(verts,dst_x,dst_y,dst_wide,dst_high,_viv_d3d11_client_wide,_viv_d3d11_client_high,&u,&v);

	clear_rgba[0] = (float)GetRValue(clear_color) / 255.0f;
	clear_rgba[1] = (float)GetGValue(clear_color) / 255.0f;
	clear_rgba[2] = (float)GetBValue(clear_color) / 255.0f;
	clear_rgba[3] = 1.0f;

	_viv_d3d11_context->lpVtbl->OMSetRenderTargets(_viv_d3d11_context,1,&_viv_d3d11_rt,NULL);
	_viv_d3d11_context->lpVtbl->ClearRenderTargetView(_viv_d3d11_context,_viv_d3d11_rt,clear_rgba);

	_viv_d3d11_context->lpVtbl->IASetInputLayout(_viv_d3d11_context,_viv_d3d11_layout);
	_viv_d3d11_context->lpVtbl->IASetPrimitiveTopology(_viv_d3d11_context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	{
		ID3D11Buffer *quad = _viv_d3d11_quad;
		UINT stride = (UINT)sizeof(_viv_d3d11_vertex_t);
		UINT offset = 0;

		_viv_d3d11_context->lpVtbl->IASetVertexBuffers(_viv_d3d11_context,0,1,&quad,&stride,&offset);
	}

	_viv_d3d11_context->lpVtbl->VSSetShader(_viv_d3d11_context,_viv_d3d11_vs,NULL,0);
	_viv_d3d11_context->lpVtbl->PSSetShader(_viv_d3d11_context,_viv_d3d11_ps,NULL,0);
	_viv_d3d11_context->lpVtbl->PSSetShaderResources(_viv_d3d11_context,0,1,&_viv_d3d11_srv);
	_viv_d3d11_context->lpVtbl->PSSetSamplers(_viv_d3d11_context,0,1,&_viv_d3d11_sampler);
	_viv_d3d11_context->lpVtbl->OMSetBlendState(_viv_d3d11_context,_viv_d3d11_blend,NULL,0xffffffff);

	hresult = _viv_d3d11_context->lpVtbl->Map(_viv_d3d11_context,(ID3D11Resource *)_viv_d3d11_quad,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);

	if (FAILED(hresult))
	{
		return 0;
	}

	memcpy(mapped.pData,verts,sizeof(verts));

	_viv_d3d11_context->lpVtbl->Unmap(_viv_d3d11_context,(ID3D11Resource *)_viv_d3d11_quad,0);

	_viv_d3d11_context->lpVtbl->Draw(_viv_d3d11_context,4,0);

	// the render self-check (the rc.3 render self-verification): the
	// first frame of a new texture draws once 1:1 into an offscreen
	// target and the sampled pixels answer against the dib's own bits -
	// a lying accelerator hands the session to the gdi path (the same
	// fallback flag the refusal face names).
	if (!_viv_d3d11_selfcheck_done)
	{
		_viv_d3d11_selfcheck_done = 1;

		if (!_viv_d3d11_selfcheck(hbitmap,&ds))
		{
			debug_printf("direct3d11: the render self-check did not match the source\r\n");

			_viv_hw_render_fallback = 1;

			return 0;
		}
	}

	if (_viv_d3d11_export_bits)
	{
		_viv_d3d11_export_readback();

		return _viv_d3d11_export_filled;
	}

	hresult = _viv_d3d11_swap->lpVtbl->Present(_viv_d3d11_swap,1,0);

	return SUCCEEDED(hresult);
}

// the export readback: the back buffer holds the frame (the clear, the
// quad, the whole scene). a staging texture takes the copy, the map walks
// the pitch, and the export buffer takes top-down bgra rows with the
// reserved byte pinned to zero - the format's x byte answers undefined
// values and the hash must never see them.
static void _viv_d3d11_export_readback(void)
{
	ID3D11Texture2D *staging;

	staging = NULL;

	if (_viv_d3d11_swap)
	{
		ID3D11Texture2D *backbuffer;
		HRESULT hresult;

		backbuffer = NULL;

		if (SUCCEEDED(_viv_d3d11_swap->lpVtbl->GetBuffer(_viv_d3d11_swap,0,&_viv_d3d11_iid_texture,(void **)&backbuffer)))
		{
			D3D11_TEXTURE2D_DESC desc;

			backbuffer->lpVtbl->GetDesc(backbuffer,&desc);

			// the format guard: the walk below assumes 4 byte pixels and
			// the size the harness armed - anything else refuses the fill
			// and the harness reads the refusal, not garbage.
			if ((desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) && (desc.Width == (UINT)_viv_d3d11_export_wide) && (desc.Height == (UINT)_viv_d3d11_export_high))
			{
				D3D11_TEXTURE2D_DESC staging_desc;

				staging_desc = desc;
				staging_desc.Usage = D3D11_USAGE_STAGING;
				staging_desc.BindFlags = 0;
				staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				staging_desc.MiscFlags = 0;
				staging_desc.MipLevels = 1;
				staging_desc.ArraySize = 1;
				staging_desc.SampleDesc.Count = 1;
				staging_desc.SampleDesc.Quality = 0;

				hresult = _viv_d3d11_device->lpVtbl->CreateTexture2D(_viv_d3d11_device,&staging_desc,NULL,&staging);

				if (SUCCEEDED(hresult))
				{
					_viv_d3d11_context->lpVtbl->CopyResource(_viv_d3d11_context,(ID3D11Resource *)staging,(ID3D11Resource *)backbuffer);

					{
						D3D11_MAPPED_SUBRESOURCE mapped;

						if (SUCCEEDED(_viv_d3d11_context->lpVtbl->Map(_viv_d3d11_context,(ID3D11Resource *)staging,0,D3D11_MAP_READ,0,&mapped)))
						{
							int y;
							int x;

							for(y=0;y<_viv_d3d11_export_high;y++)
							{
								const BYTE *sp;
								BYTE *dp;

								sp = (const BYTE *)mapped.pData + (uintptr_t)y * (uintptr_t)mapped.RowPitch;
								dp = _viv_d3d11_export_bits + (uintptr_t)y * (uintptr_t)_viv_d3d11_export_wide * 4;

								for(x=0;x<_viv_d3d11_export_wide;x++)
								{
									dp[0] = sp[0];
									dp[1] = sp[1];
									dp[2] = sp[2];
									dp[3] = 0;
									sp += 4;
									dp += 4;
								}
							}

							_viv_d3d11_context->lpVtbl->Unmap(_viv_d3d11_context,(ID3D11Resource *)staging,0);

							// the disarm: the buffer belongs to the caller (freed
							// right after the run) - a second render must never
							// touch it.
							_viv_d3d11_export_bits = 0;
							_viv_d3d11_export_filled = 1;
						}
					}
				}
			}

			backbuffer->lpVtbl->Release(backbuffer);
		}
	}

	if (staging)
	{
		staging->lpVtbl->Release(staging);
	}
}

// the render self-check: the image draws once 1:1 into an offscreen
// target (capped at 4096 - the transient target dies before the next
// paint, the cap keeps a huge image from paying a second full texture
// for a check), five sample boxes copy back through a staging texture
// and every sample answers against the dib's own bits. the 1:1 quad with
// the half texel offset puts the texel centers on the destination
// centers, so the linear sampler answers the exact source pixels.
static int _viv_d3d11_selfcheck(HBITMAP hbitmap,DIBSECTION *ds)
{
	ID3D11Texture2D *target;
	ID3D11RenderTargetView *rt;
	ID3D11Texture2D *staging;
	int wide;
	int high;
	int stride;
	int i;
	int ok;
	static const int points[5][2] = {{4,4},{60,4},{4,60},{60,60},{32,32}};

	wide = ds->dsBm.bmWidth;
	high = ds->dsBm.bmHeight;

	if ((wide <= 8) || (high <= 8) || (wide > 4096) || (high > 4096))
	{
		// nothing to prove on a sliver; the size cap keeps the check
		// from paying a second full texture for a huge image (the
		// diagnostic names the skip so the silence is a decision).
		debug_printf("direct3d11: the render self-check skipped (size %dx%d)\r\n",wide,high);

		return 1;
	}

	stride = (int)((((SIZE_T)wide * ds->dsBm.bmBitsPixel + 31) / 32) * 4);

	target = NULL;
	rt = NULL;
	staging = NULL;
	ok = 0;

	{
		D3D11_TEXTURE2D_DESC desc;

		ZeroMemory(&desc,sizeof(desc));
		desc.Width = (UINT)wide;
		desc.Height = (UINT)high;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;

		if (FAILED(_viv_d3d11_device->lpVtbl->CreateTexture2D(_viv_d3d11_device,&desc,NULL,&target)))
		{
			goto selfcheck_exit;
		}

		if (FAILED(_viv_d3d11_device->lpVtbl->CreateRenderTargetView(_viv_d3d11_device,(ID3D11Resource *)target,NULL,&rt)))
		{
			goto selfcheck_exit;
		}

		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		if (FAILED(_viv_d3d11_device->lpVtbl->CreateTexture2D(_viv_d3d11_device,&desc,NULL,&staging)))
		{
			goto selfcheck_exit;
		}
	}

	{
		_viv_d3d11_vertex_t verts[4];
		float u;
		float v;
		float clear_rgba[4];
		D3D11_VIEWPORT viewport;
		D3D11_MAPPED_SUBRESOURCE mapped;
		ID3D11Buffer *quad;
		UINT stride_vb;
		UINT offset;

		u = 1.0f;
		v = 1.0f;

		_viv_d3d11_quad_fill(verts,0,0,wide,high,wide,high,&u,&v);

		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = (float)wide;
		viewport.Height = (float)high;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;

		clear_rgba[0] = 0.0f;
		clear_rgba[1] = 1.0f;
		clear_rgba[2] = 0.0f;
		clear_rgba[3] = 1.0f;

		_viv_d3d11_context->lpVtbl->RSSetViewports(_viv_d3d11_context,1,&viewport);
		_viv_d3d11_context->lpVtbl->OMSetRenderTargets(_viv_d3d11_context,1,&rt,NULL);
		_viv_d3d11_context->lpVtbl->ClearRenderTargetView(_viv_d3d11_context,rt,clear_rgba);

		_viv_d3d11_context->lpVtbl->IASetInputLayout(_viv_d3d11_context,_viv_d3d11_layout);
		_viv_d3d11_context->lpVtbl->IASetPrimitiveTopology(_viv_d3d11_context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

		quad = _viv_d3d11_quad;
		stride_vb = (UINT)sizeof(_viv_d3d11_vertex_t);
		offset = 0;

		_viv_d3d11_context->lpVtbl->IASetVertexBuffers(_viv_d3d11_context,0,1,&quad,&stride_vb,&offset);
		_viv_d3d11_context->lpVtbl->VSSetShader(_viv_d3d11_context,_viv_d3d11_vs,NULL,0);
		_viv_d3d11_context->lpVtbl->PSSetShader(_viv_d3d11_context,_viv_d3d11_ps,NULL,0);
		_viv_d3d11_context->lpVtbl->PSSetShaderResources(_viv_d3d11_context,0,1,&_viv_d3d11_srv);
		_viv_d3d11_context->lpVtbl->PSSetSamplers(_viv_d3d11_context,0,1,&_viv_d3d11_sampler);
		_viv_d3d11_context->lpVtbl->OMSetBlendState(_viv_d3d11_context,_viv_d3d11_blend,NULL,0xffffffff);

		if (FAILED(_viv_d3d11_context->lpVtbl->Map(_viv_d3d11_context,(ID3D11Resource *)_viv_d3d11_quad,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
		{
			goto selfcheck_exit;
		}

		memcpy(mapped.pData,verts,sizeof(verts));

		_viv_d3d11_context->lpVtbl->Unmap(_viv_d3d11_context,(ID3D11Resource *)_viv_d3d11_quad,0);

		_viv_d3d11_context->lpVtbl->Draw(_viv_d3d11_context,4,0);
	}

	_viv_d3d11_context->lpVtbl->CopyResource(_viv_d3d11_context,(ID3D11Resource *)staging,(ID3D11Resource *)target);

	{
		D3D11_MAPPED_SUBRESOURCE mapped;

		if (SUCCEEDED(_viv_d3d11_context->lpVtbl->Map(_viv_d3d11_context,(ID3D11Resource *)staging,0,D3D11_MAP_READ,0,&mapped)))
		{
			ok = 1;

			for(i=0;i<5;i++)
			{
				int px;
				int py;
				const BYTE *expect;
				const BYTE *answer;

				px = (points[i][0] * (wide - 8)) / 64;
				py = (points[i][1] * (high - 8)) / 64;

				expect = (const BYTE *)ds->dsBm.bmBits + (uintptr_t)py * (uintptr_t)stride + (uintptr_t)px * (uintptr_t)((ds->dsBm.bmBitsPixel == 24) ? 3 : 4);
				answer = (const BYTE *)mapped.pData + (uintptr_t)py * (uintptr_t)mapped.RowPitch + (uintptr_t)px * 4;

				if ((answer[0] != expect[0]) || (answer[1] != expect[1]) || (answer[2] != expect[2]))
				{
					ok = 0;

					break;
				}
			}

			_viv_d3d11_context->lpVtbl->Unmap(_viv_d3d11_context,(ID3D11Resource *)staging,0);
		}
	}

selfcheck_exit:

	if (staging)
	{
		staging->lpVtbl->Release(staging);
	}

	if (rt)
	{
		rt->lpVtbl->Release(rt);
	}

	if (target)
	{
		target->lpVtbl->Release(target);
	}

	return ok;
}

void _viv_hwd3d11_release_image(void)
{
	if (_viv_d3d11_context)
	{
		ID3D11ShaderResourceView *none = NULL;

		_viv_d3d11_context->lpVtbl->PSSetShaderResources(_viv_d3d11_context,0,1,&none);
	}

	if (_viv_d3d11_srv)
	{
		_viv_d3d11_srv->lpVtbl->Release(_viv_d3d11_srv);

		_viv_d3d11_srv = NULL;
	}

	if (_viv_d3d11_texture)
	{
		_viv_d3d11_texture->lpVtbl->Release(_viv_d3d11_texture);

		_viv_d3d11_texture = NULL;
	}

	_viv_d3d11_last_hbitmap = 0;
	_viv_d3d11_tex_wide = 0;
	_viv_d3d11_tex_high = 0;
	_viv_d3d11_selfcheck_done = 0;
}

void _viv_hwd3d11_shutdown(void)
{
	_viv_hwd3d11_release_image();
	_viv_d3d11_drop_device();

	if (_viv_d3d11_scratch)
	{
		mem_free(_viv_d3d11_scratch);

		_viv_d3d11_scratch = NULL;
	}

	_viv_d3d11_scratch_size = 0;
	_viv_d3d11_failed = 0;

	// the export pointer dies with the caller's buffer - the shutdown
	// clears it so no later session state can point at freed memory.
	_viv_d3d11_export_bits = 0;
}

// the export harness hooks: arm a top-down bgra buffer before the paint,
// the next render fills it instead of presenting. the query lets the
// harness tell a refused renderer (the gdi fallback painted instead)
// from a successful readback.
int _viv_hwd3d11_export_begin(BYTE *bits,int wide,int high)
{
	_viv_d3d11_export_bits = bits;
	_viv_d3d11_export_wide = wide;
	_viv_d3d11_export_high = high;
	_viv_d3d11_export_filled = 0;

	return 1;
}

int _viv_hwd3d11_export_filled(void)
{
	return _viv_d3d11_export_filled;
}

void _viv_hwd3d11_export_end(void)
{
	_viv_d3d11_export_bits = NULL;
}
