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
static int _viv_d3d_max_wide;
static int _viv_d3d_max_high;
static int _viv_d3d_client_wide;
static int _viv_d3d_client_high;
static float _viv_d3d_u;
static float _viv_d3d_v;
static int _viv_d3d_bottom_up;

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
	
	if ((pot_wide < wide) || (pot_high < high))
	{
		debug_printf("direct3d: the image exceeds the max texture size\r\n");
		
		return 0;
	}
	
	if ((uintptr_t)pot_wide * (uintptr_t)pot_high > VIV_MAX_IMAGE_PIXELS)
	{
		debug_printf("direct3d: the padded canvas exceeds the pixel budget\r\n");
		
		return 0;
	}
	
	if (_viv_d3d_texture)
	{
		_viv_d3d_texture->lpVtbl->Release(_viv_d3d_texture);
		
		_viv_d3d_texture = 0;
	}
	
	hresult = _viv_d3d_device->lpVtbl->CreateTexture(_viv_d3d_device,pot_wide,pot_high,1,0,D3DFMT_X8R8G8B8,D3DPOOL_MANAGED,&_viv_d3d_texture,0);
	
	if (FAILED(hresult))
	{
		debug_printf("direct3d: CreateTexture failed %x\r\n",(unsigned int)hresult);
		
		return 0;
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
	
	_viv_d3d_texture->lpVtbl->UnlockRect(_viv_d3d_texture,0);
	
	_viv_d3d_u = (float)wide / (float)pot_wide;
	_viv_d3d_v = (float)high / (float)pot_high;
	_viv_d3d_bottom_up = (ds->dsBmih.biHeight > 0) ? 1 : 0;
	_viv_d3d_last_hbitmap = hbitmap;
	
	return 1;
}

int _viv_hwd3d_render(HWND hwnd,HDC hdc,HBITMAP hbitmap,int dst_x,int dst_y,int dst_wide,int dst_high,COLORREF clear_color)
{
	DIBSECTION ds;
	_viv_d3d_vertex_t verts[4];
	HRESULT hresult;
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
		// the palette-bitmap frames stay on the gdi path.
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
	
	if (SUCCEEDED(hresult))
	{
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
		
		if (_viv_d3d_bottom_up)
		{
			verts[0].u = 0.0f;
			verts[0].v = _viv_d3d_v;
			verts[1].u = _viv_d3d_u;
			verts[1].v = _viv_d3d_v;
			verts[2].u = 0.0f;
			verts[2].v = 0.0f;
			verts[3].u = _viv_d3d_u;
			verts[3].v = 0.0f;
		}
		else
		{
			verts[0].u = 0.0f;
			verts[0].v = 0.0f;
			verts[1].u = _viv_d3d_u;
			verts[1].v = 0.0f;
			verts[2].u = 0.0f;
			verts[2].v = _viv_d3d_v;
			verts[3].u = _viv_d3d_u;
			verts[3].v = _viv_d3d_v;
		}
		
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
	
	hresult = _viv_d3d_device->lpVtbl->Present(_viv_d3d_device,0,0,0,0);
	
	if (hresult == D3DERR_DEVICELOST)
	{
		// the lost-device discipline: not-reset resets now, a plain lost
		// waits for the next paint (the managed texture survives either
		// way; the gdi path already painted this frame).
		hresult = _viv_d3d_device->lpVtbl->TestCooperativeLevel(_viv_d3d_device);
		
		if (hresult == D3DERR_DEVICENOTRESET)
		{
			_viv_d3d_params.BackBufferWidth = _viv_d3d_client_wide;
			_viv_d3d_params.BackBufferHeight = _viv_d3d_client_high;
			
			_viv_d3d_device->lpVtbl->Reset(_viv_d3d_device,&_viv_d3d_params);
		}
	}
	
	return 1;
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
	_viv_d3d_failed = 0;
	_viv_d3d_client_wide = 0;
	_viv_d3d_client_high = 0;
}
