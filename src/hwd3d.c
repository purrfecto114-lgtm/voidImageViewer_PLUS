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
// the direct3d renderer.

#include "viv.h"
#include "hwd3d.h"
#include <d3d9.h>

typedef IDirect3D9 * (__stdcall *_viv_d3d_create9_t)(UINT sdk_version);

// the pretransformed strip vertex: screen coordinates and a sampling
// pair, no matrices and no lighting (the d3d9 fvf contract).
typedef struct _viv_d3d_vertex_s
{
	float x;
	float y;
	float z;
	float rhw;
	float u;
	float v;
}_viv_d3d_vertex_t;

static HMODULE _viv_d3d_module;
static _viv_d3d_create9_t _viv_d3d_create9;
static IDirect3D9 *_viv_d3d;
static IDirect3DDevice9 *_viv_d3d_device;
static IDirect3DTexture9 *_viv_d3d_texture;
static D3DPRESENT_PARAMETERS _viv_d3d_params;
static HBITMAP _viv_d3d_last_hbitmap;
static int _viv_d3d_failed;
static int _viv_d3d_need_pot;
static int _viv_d3d_need_square;
static int _viv_d3d_last_pot_wide;
static int _viv_d3d_last_pot_high;
static int _viv_d3d_max_wide;
static int _viv_d3d_max_high;
static int _viv_d3d_client_wide;
static int _viv_d3d_client_high;
static float _viv_d3d_u;
static float _viv_d3d_v;

// the export harness readback state: a top-down bgra buffer armed before
// the paint, filled by the render instead of a present.
static BYTE *_viv_d3d_export_bits;
static int _viv_d3d_export_wide;
static int _viv_d3d_export_high;
static int _viv_d3d_export_filled;

static void _viv_d3d_export_readback(void);

static int _viv_d3d_init(HWND hwnd)
{
	D3DCAPS9 caps;
	D3DDEVTYPE device_type;
	RECT rect;
	HRESULT hresult;
	
	if (_viv_d3d_device)
	{
		return 1;
	}
	
	if (!_viv_d3d_module)
	{
		_viv_d3d_module = LoadLibraryA("d3d9.dll");
		if (!_viv_d3d_module)
		{
			debug_printf("direct3d: d3d9.dll is not on this system\r\n");
			
			_viv_d3d_failed = 1;
			
			return 0;
		}
		
		_viv_d3d_create9 = (_viv_d3d_create9_t)GetProcAddress(_viv_d3d_module,"Direct3DCreate9");
		if (!_viv_d3d_create9)
		{
			_viv_d3d_failed = 1;
			
			return 0;
		}
	}
	
	if (!_viv_d3d)
	{
		_viv_d3d = _viv_d3d_create9(D3D_SDK_VERSION);
		if (!_viv_d3d)
		{
			_viv_d3d_failed = 1;
			
			return 0;
		}
	}
	
	// the caps answer the npot question (the d3d9 contract: the managed
	// pool never waives it) - pow2-only devices pad, conditional devices
	// take the raw dims under clamp addressing with no mipmaps, full npot
	// devices take the raw dims outright.
	device_type = D3DDEVTYPE_HAL;
	
	ZeroMemory(&caps,sizeof(caps));
	hresult = _viv_d3d->lpVtbl->GetDeviceCaps(_viv_d3d,D3DADAPTER_DEFAULT,device_type,&caps);
	
	if (FAILED(hresult))
	{
		device_type = D3DDEVTYPE_REF;
		
		hresult = _viv_d3d->lpVtbl->GetDeviceCaps(_viv_d3d,D3DADAPTER_DEFAULT,device_type,&caps);
		
		if (FAILED(hresult))
		{
			debug_printf("direct3d: the caps did not answer\r\n");
			
			_viv_d3d_failed = 1;
			
			return 0;
		}
	}
	
	_viv_d3d_need_pot = 0;
	_viv_d3d_need_square = 0;
	
	if (caps.TextureCaps & D3DPTEXTURECAPS_POW2)
	{
		if (!(caps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL))
		{
			_viv_d3d_need_pot = 1;
		}
	}
	
	if (caps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
	{
		_viv_d3d_need_square = 1;
	}
	
	_viv_d3d_max_wide = caps.MaxTextureWidth;
	_viv_d3d_max_high = caps.MaxTextureHeight;
	
	if ((_viv_d3d_max_wide <= 0) || (_viv_d3d_max_high <= 0))
	{
		_viv_d3d_max_wide = 256;
		_viv_d3d_max_high = 256;
	}
	
	GetClientRect(hwnd,&rect);
	_viv_d3d_client_wide = rect.right - rect.left;
	_viv_d3d_client_high = rect.bottom - rect.top;
	
	ZeroMemory(&_viv_d3d_params,sizeof(_viv_d3d_params));
	_viv_d3d_params.Windowed = TRUE;
	_viv_d3d_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
	_viv_d3d_params.BackBufferFormat = D3DFMT_UNKNOWN;
	_viv_d3d_params.BackBufferWidth = _viv_d3d_client_wide;
	_viv_d3d_params.BackBufferHeight = _viv_d3d_client_high;
	_viv_d3d_params.hDeviceWindow = hwnd;
	_viv_d3d_params.EnableAutoDepthStencil = FALSE;
	
	// FPU_PRESERVE: gdi+ and the crt float math keep their precision (the
	// default single-precision device mode corrupts them).
	hresult = _viv_d3d->lpVtbl->CreateDevice(_viv_d3d,D3DADAPTER_DEFAULT,device_type,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,&_viv_d3d_params,&_viv_d3d_device);
	
	if (FAILED(hresult))
	{
		debug_printf("direct3d: CreateDevice failed %x\r\n",(unsigned int)hresult);
		
		_viv_d3d_failed = 1;
		
		return 0;
	}
	
	return 1;
}

// the upload: a managed x8r8g8b8 texture the device re-uploads itself
// after a reset (the managed pool survives resets by contract), the rows
// copied from the dib section (24bpp triples and 32bpp quads alike
// become b,g,r,255 quads - the memory order the format wants).
static int _viv_d3d_texture_upload(HBITMAP hbitmap,DIBSECTION *ds)
{
	int wide;
	int high;
	int stride;
	int pot_wide;
	int pot_high;
	D3DLOCKED_RECT locked;
	HRESULT hresult;
	
	wide = ds->dsBm.bmWidth;
	high = ds->dsBm.bmHeight;
	stride = ((wide * ds->dsBm.bmBitsPixel + 31) / 32) * 4;
	
	if ((wide <= 0) || (high <= 0))
	{
		return 0;
	}
	
	pot_wide = wide;
	pot_high = high;
	
	if (_viv_d3d_need_pot)
	{
		pot_wide = 1;
		while ((pot_wide < wide) && (pot_wide < _viv_d3d_max_wide))
		{
			pot_wide <<= 1;
		}
		
		pot_high = 1;
		while ((pot_high < high) && (pot_high < _viv_d3d_max_high))
		{
			pot_high <<= 1;
		}
	}
	
	if (_viv_d3d_need_square)
	{
		if (pot_wide > pot_high)
		{
			pot_high = pot_wide;
		}
		else
		{
			pot_wide = pot_high;
		}
	}
	
	// the max clauses answer the overshoots: a non power of two max can sit
	// between two powers (the loop lands past it) and the square promote can
	// lift the short side past its own max - the device would refuse the
	// create either way, with the diagnostic blaming the wrong gate.
	if ((pot_wide < wide) || (pot_high < high) || (pot_wide > _viv_d3d_max_wide) || (pot_high > _viv_d3d_max_high))
	{
		debug_printf("direct3d: the image exceeds the max texture size\r\n");
		
		return 0;
	}
	
	if ((uintptr_t)pot_wide * (uintptr_t)pot_high > VIV_MAX_IMAGE_PIXELS)
	{
		debug_printf("direct3d: the padded canvas exceeds the pixel budget\r\n");
		
		return 0;
	}
	
	// the reuse gate: an animation frame with the same padded
	// dimensions refills the managed texture in place - the
	// release/create cycle only answers a dimension change (the
	// copy loop only rewrites the image's own texels, the edge
	// replication below rewrites the pad every upload, and the
	// managed pool survives the resets by contract).
	if ((_viv_d3d_texture) && ((pot_wide != _viv_d3d_last_pot_wide) || (pot_high != _viv_d3d_last_pot_high)))
	{
		_viv_d3d_texture->lpVtbl->Release(_viv_d3d_texture);
		
		_viv_d3d_texture = 0;
	}
	
	if (!_viv_d3d_texture)
	{
		hresult = _viv_d3d_device->lpVtbl->CreateTexture(_viv_d3d_device,pot_wide,pot_high,1,0,D3DFMT_X8R8G8B8,D3DPOOL_MANAGED,&_viv_d3d_texture,0);
		
		if (FAILED(hresult))
		{
			debug_printf("direct3d: CreateTexture failed %x\r\n",(unsigned int)hresult);
			
			return 0;
		}
		
		_viv_d3d_last_pot_wide = pot_wide;
		_viv_d3d_last_pot_high = pot_high;
	}
	
	hresult = _viv_d3d_texture->lpVtbl->LockRect(_viv_d3d_texture,0,&locked,0,0);
	
	if (FAILED(hresult))
	{
		_viv_d3d_texture->lpVtbl->Release(_viv_d3d_texture);
		_viv_d3d_texture = 0;
		
		return 0;
	}
	
	{
		const BYTE *s;
		BYTE *drow;
		int y;
		
		s = (const BYTE *)ds->dsBm.bmBits;
		drow = (BYTE *)locked.pBits;
		
		for(y=0;y<high;y++)
		{
			const BYTE *p;
			BYTE *d;
			int x;
			
			p = s;
			d = drow;
			
			for(x=0;x<wide;x++)
			{
				d[0] = p[0]; // b
				d[1] = p[1]; // g
				d[2] = p[2]; // r
				d[3] = 255;
				d += 4;
				p += (ds->dsBm.bmBitsPixel == 24) ? 3 : 4;
			}
			
			s += stride;
			drow += locked.Pitch;
		}
	}
	
	// the pad must answer as if the texture border sat at the image
	// border. the linear sampler's half texel at the sub-rectangle
	// edge reads the neighbouring texel (d3d9 maps the address range
	// onto the [-0.5, n-0.5] texel row), and clamp only answers at
	// the texture's own border - never at a sub-rectangle inside it.
	// so the right-hand gutter carries the image's last column and
	// every row under the image carries the image's last row (gutter
	// included, so the corner holds the bottom-right pixel). a
	// reused texture cannot bleed the previous frame's pixels back
	// in, and the outermost destination pixels stop averaging into
	// whatever the pad used to hold.
	if ((pot_wide > wide) || (pot_high > high))
	{
		const BYTE *last_row;
		BYTE *base;
		int y;
		int x;
		
		base = (BYTE *)locked.pBits;
		
		for(y=0;y<high;y++)
		{
			const BYTE *last;
			BYTE *d;
			
			last = base + (uintptr_t)y * (uintptr_t)locked.Pitch + (uintptr_t)(wide - 1) * 4;
			d = base + (uintptr_t)y * (uintptr_t)locked.Pitch + (uintptr_t)wide * 4;
			
			for(x=wide;x<pot_wide;x++)
			{
				d[0] = last[0];
				d[1] = last[1];
				d[2] = last[2];
				d[3] = 255;
				d += 4;
			}
		}
		
		last_row = base + (uintptr_t)(high - 1) * (uintptr_t)locked.Pitch;
		
		for(y=high;y<pot_high;y++)
		{
			memcpy(base + (uintptr_t)y * (uintptr_t)locked.Pitch,last_row,(uintptr_t)pot_wide * 4);
		}
	}
	
	_viv_d3d_texture->lpVtbl->UnlockRect(_viv_d3d_texture,0);
	
	_viv_d3d_u = (float)wide / (float)pot_wide;
	_viv_d3d_v = (float)high / (float)pot_high;
	_viv_d3d_last_hbitmap = hbitmap;
	
	return 1;
}

int _viv_hwd3d_render(HWND hwnd,HDC hdc,HBITMAP hbitmap,int dst_x,int dst_y,int dst_wide,int dst_high,COLORREF clear_color)
{
	DIBSECTION ds;
	_viv_d3d_vertex_t verts[4];
	HRESULT hresult;
	int scene_ok;
	float x;
	float y;
	float w;
	float h;
	
	if (_viv_d3d_failed)
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
		// the refusal names its reason (the gl twin carries the full
		// story): a silent return 0 is how an entire decoder family hid
		// behind this gate while the pixel oracle recorded nulls.
		if (!ds.dsBm.bmBits)
		{
			debug_printf("direct3d: the frame is not a dib section (no bits answered) - the gdi path paints it\r\n");
		}
		else
		{
			debug_printf("direct3d: the frame is %d bpp (24 or 32 answer) - the gdi path paints it\r\n",ds.dsBm.bmBitsPixel);
		}
		
		return 0;
	}
	
	if (!_viv_d3d_init(hwnd))
	{
		return 0;
	}
	
	// the resize answer: the back buffer matches the window or resets
	// (the managed texture survives the reset by contract).
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
		
		if ((wide != _viv_d3d_client_wide) || (high != _viv_d3d_client_high))
		{
			_viv_d3d_params.BackBufferWidth = wide;
			_viv_d3d_params.BackBufferHeight = high;
			
			hresult = _viv_d3d_device->lpVtbl->Reset(_viv_d3d_device,&_viv_d3d_params);
			
			if (FAILED(hresult))
			{
				debug_printf("direct3d: the reset failed %x\r\n",(unsigned int)hresult);
				
				return 0;
			}
			
			_viv_d3d_client_wide = wide;
			_viv_d3d_client_high = high;
		}
	}
	
	if (hbitmap != _viv_d3d_last_hbitmap)
	{
		// an oversized image is a per-image refusal, not a session failure.
		if (!_viv_d3d_texture_upload(hbitmap,&ds))
		{
			return 0;
		}
	}
	
	_viv_d3d_device->lpVtbl->Clear(_viv_d3d_device,0,0,D3DCLEAR_TARGET,D3DCOLOR_XRGB(GetRValue(clear_color),GetGValue(clear_color),GetBValue(clear_color)),1.0f,0);
	
	hresult = _viv_d3d_device->lpVtbl->BeginScene(_viv_d3d_device);
	
	scene_ok = 0;
	
	if (SUCCEEDED(hresult))
	{
		scene_ok = 1;
		
		x = (float)dst_x;
		y = (float)dst_y;
		w = (float)dst_wide;
		h = (float)dst_high;
		
		verts[0].x = x;
		verts[0].y = y;
		verts[0].z = 0.0f;
		verts[0].rhw = 1.0f;
		
		verts[1].x = x + w;
		verts[1].y = y;
		verts[1].z = 0.0f;
		verts[1].rhw = 1.0f;
		
		verts[2].x = x;
		verts[2].y = y + h;
		verts[2].z = 0.0f;
		verts[2].rhw = 1.0f;
		
		verts[3].x = x + w;
		verts[3].y = y + h;
		verts[3].z = 0.0f;
		verts[3].rhw = 1.0f;
		
		// every frame this app builds is a top-down dib, and the sign
		// that says so is unrecoverable through getobject - it answers
		// the absolute height for both orientations (the wine test suite
		// asserts it, reactos implements it, and the r96 read believed it
		// could tell: the flag was always 1 on a real machine and every
		// image rode the flip - the field report's upside-down launch).
		// the one mapping answers them all: the screen's top edge samples
		// the texture's row zero, the image's own top.
		verts[0].u = 0.0f;
		verts[0].v = 0.0f;
		verts[1].u = _viv_d3d_u;
		verts[1].v = 0.0f;
		verts[2].u = 0.0f;
		verts[2].v = _viv_d3d_v;
		verts[3].u = _viv_d3d_u;
		verts[3].v = _viv_d3d_v;
		
		_viv_d3d_device->lpVtbl->SetRenderState(_viv_d3d_device,D3DRS_CULLMODE,D3DCULL_NONE);
		_viv_d3d_device->lpVtbl->SetRenderState(_viv_d3d_device,D3DRS_LIGHTING,FALSE);
		_viv_d3d_device->lpVtbl->SetTexture(_viv_d3d_device,0,(IDirect3DBaseTexture9 *)_viv_d3d_texture);
		_viv_d3d_device->lpVtbl->SetSamplerState(_viv_d3d_device,0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
		_viv_d3d_device->lpVtbl->SetSamplerState(_viv_d3d_device,0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
		_viv_d3d_device->lpVtbl->SetSamplerState(_viv_d3d_device,0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
		_viv_d3d_device->lpVtbl->SetSamplerState(_viv_d3d_device,0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
		_viv_d3d_device->lpVtbl->SetFVF(_viv_d3d_device,D3DFVF_XYZRHW | D3DFVF_TEX1);
		_viv_d3d_device->lpVtbl->DrawPrimitiveUP(_viv_d3d_device,D3DPT_TRIANGLESTRIP,2,verts,sizeof(_viv_d3d_vertex_t));
		
		_viv_d3d_device->lpVtbl->EndScene(_viv_d3d_device);
	}
	
	// the scene gate: a refused beginscene leaves the back buffer with the
	// clear only - the harness must see the refusal, not a blank hash.
	if ((_viv_d3d_export_bits) && (scene_ok))
	{
		_viv_d3d_export_readback();
	}
	else
	{
		hresult = _viv_d3d_device->lpVtbl->Present(_viv_d3d_device,0,0,0,0);
		
		if (hresult == D3DERR_DEVICELOST)
		{
			// the lost-device discipline: not-reset resets now, a plain lost
			// waits for the next paint (the managed texture survives either
			// way; this frame is dropped, the next paint answers it).
			hresult = _viv_d3d_device->lpVtbl->TestCooperativeLevel(_viv_d3d_device);
		
			if (hresult == D3DERR_DEVICENOTRESET)
			{
				_viv_d3d_params.BackBufferWidth = _viv_d3d_client_wide;
				_viv_d3d_params.BackBufferHeight = _viv_d3d_client_high;
				
				_viv_d3d_device->lpVtbl->Reset(_viv_d3d_device,&_viv_d3d_params);
			}
		}
	}
	
	return 1;
}

// the export readback: the back buffer holds the frame (the clear, the
// quad, the whole scene). getrendertargetdata lands it in a system
// memory surface, the lock walks the pitch, and the export buffer takes
// top-down bgra rows with the reserved byte pinned to zero - the x byte
// of an x8r8g8b8 back buffer answers undefined values and the hash must
// never see them.
static void _viv_d3d_export_readback(void)
{
	IDirect3DSurface9 *backbuffer;
	IDirect3DSurface9 *sysmem;
	
	backbuffer = 0;
	sysmem = 0;
	
	if (SUCCEEDED(_viv_d3d_device->lpVtbl->GetBackBuffer(_viv_d3d_device,0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer)))
	{
		D3DSURFACE_DESC desc;
		
		ZeroMemory(&desc,sizeof(desc));
		
		// the format guard: the walk below assumes 4 byte pixels - a 16bpp
		// desktop would hand the walk garbage with a clean fill flag.
		if ((SUCCEEDED(backbuffer->lpVtbl->GetDesc(backbuffer,&desc))) && (desc.Width == (UINT)_viv_d3d_export_wide) && (desc.Height == (UINT)_viv_d3d_export_high) && ((desc.Format == D3DFMT_X8R8G8B8) || (desc.Format == D3DFMT_A8R8G8B8)))
		{
			if (SUCCEEDED(_viv_d3d_device->lpVtbl->CreateOffscreenPlainSurface(_viv_d3d_device,desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&sysmem,0)))
			{
				if (SUCCEEDED(_viv_d3d_device->lpVtbl->GetRenderTargetData(_viv_d3d_device,backbuffer,sysmem)))
				{
					D3DLOCKED_RECT sys_locked;
					
					if (SUCCEEDED(sysmem->lpVtbl->LockRect(sysmem,&sys_locked,0,0)))
					{
						int y;
						int x;
						
						for(y=0;y<_viv_d3d_export_high;y++)
						{
							const BYTE *s;
							BYTE *d;
							
							s = (const BYTE *)sys_locked.pBits + (uintptr_t)y * (uintptr_t)sys_locked.Pitch;
							d = _viv_d3d_export_bits + (uintptr_t)y * (uintptr_t)_viv_d3d_export_wide * 4;
							
							for(x=0;x<_viv_d3d_export_wide;x++)
							{
								d[0] = s[0];
								d[1] = s[1];
								d[2] = s[2];
								d[3] = 0;
								s += 4;
								d += 4;
							}
						}
						
						sysmem->lpVtbl->UnlockRect(sysmem);
						
						// the disarm: the buffer belongs to the caller (freed right
						// after the run) - a second render must never touch it.
						_viv_d3d_export_bits = 0;
						_viv_d3d_export_filled = 1;
					}
				}
			}
		}
	}
	
	if (sysmem)
	{
		sysmem->lpVtbl->Release(sysmem);
	}
	
	if (backbuffer)
	{
		backbuffer->lpVtbl->Release(backbuffer);
	}
}

void _viv_hwd3d_shutdown(void)
{
	if (_viv_d3d_texture)
	{
		_viv_d3d_texture->lpVtbl->Release(_viv_d3d_texture);
		
		_viv_d3d_texture = 0;
	}
	
	if (_viv_d3d_device)
	{
		_viv_d3d_device->lpVtbl->Release(_viv_d3d_device);
		
		_viv_d3d_device = 0;
	}
	
	if (_viv_d3d)
	{
		_viv_d3d->lpVtbl->Release(_viv_d3d);
		
		_viv_d3d = 0;
	}
	
	_viv_d3d_last_hbitmap = 0;
	_viv_d3d_last_pot_wide = 0;
	_viv_d3d_last_pot_high = 0;
	_viv_d3d_failed = 0;
	_viv_d3d_client_wide = 0;
	_viv_d3d_client_high = 0;
	// the export pointer dies with the caller's buffer - the shutdown
	// clears it so no later session state can point at freed memory.
	_viv_d3d_export_bits = 0;
}

// the export harness hooks: arm a top-down bgra buffer before the paint,
// the next render fills it instead of presenting. the query lets the
// harness tell a refused renderer (the gdi fallback painted instead)
// from a successful readback.
int _viv_hwd3d_export_begin(BYTE *bits,int wide,int high)
{
	_viv_d3d_export_bits = bits;
	_viv_d3d_export_wide = wide;
	_viv_d3d_export_high = high;
	_viv_d3d_export_filled = 0;
	
	return 1;
}

int _viv_hwd3d_export_filled(void)
{
	return _viv_d3d_export_filled;
}
