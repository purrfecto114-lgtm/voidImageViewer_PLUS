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
// the direct3d 12 renderer: the d3d9 module's shape (one window device,
// one live image texture, the sticky refusal, the export readback) with
// the d3d12 answers. the memory story the rc.3 round was asked to mind:
// the texture is a committed default-heap resource released the moment
// another image shows; the upload buffer is ONE committed upload-heap
// allocation reused across uploads (grown once per size class, never per
// frame; the fence holds the next upload back until the previous copy
// landed, so the reuse is safe); the quad is a static unit buffer and
// the destination rectangle rides four root constants - the frame
// command stream is a reset, two barriers and a draw. d3d12.dll,
// dxgi.dll and d3dcompiler_47.dll resolve through getprocaddress (the
// os.c gdi+ table precedent: the exe's import table never changes).
#include "viv.h"
#include "hwd3d12.h"

#include <d3d12.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

// the interface guid constants the create calls want (local, so the uuid
// import stays out of the link line).
static const GUID _viv_d3d12_iid_device = {0x696442be,0xa72e,0x4059,{0xbc,0x79,0x5d,0xb5,0x77,0x9d,0x3a,0x14}};
static const GUID _viv_d3d12_iid_factory2 = {0x50c83a1c,0xe072,0x4c48,{0x87,0xb0,0x36,0x30,0xfa,0x36,0xa6,0xd0}};
static const GUID _viv_d3d12_iid_swapchain1 = {0xf6fa8db6,0xead2,0x4c46,{0x8b,0x76,0xd1,0x11,0x46,0xc6,0x77,0x0f}};
static const GUID _viv_d3d12_iid_queue = {0x8c1d3cae,0xa0a4,0x454d,{0x84,0x05,0x8b,0x1c,0x37,0x36,0xd4,0x1f}};
static const GUID _viv_d3d12_iid_heap = {0x8efb9919,0x2d43,0x4396,{0xbe,0xef,0x16,0x58,0x60,0xd4,0x56,0x4b}};
static const GUID _viv_d3d12_iid_allocator = {0x6102dee4,0xaf59,0x4b09,{0xb9,0x19,0xb2,0x74,0x66,0xfd,0x12,0x56}};
static const GUID _viv_d3d12_iid_list = {0x5b160d0f,0xac1b,0x418d,{0x99,0x8e,0xbe,0x53,0x9e,0x3a,0x66,0x0b}};
static const GUID _viv_d3d12_iid_fence = {0x0a675a90,0x1f51,0x4c5a,{0xa2,0x52,0x2f,0x7a,0x48,0x3d,0x50,0x91}};
static const GUID _viv_d3d12_iid_rootsig = {0xc54a6b66,0x72df,0x4ee8,{0x8b,0xe5,0xa9,0x46,0xa1,0x42,0x92,0x14}};
static const GUID _viv_d3d12_iid_pso = {0x765a30f3,0xf624,0x4c6f,{0xa8,0x28,0xac,0xe9,0xe6,0x39,0xf2,0x66}};
static const GUID _viv_d3d12_iid_resource = {0x9d5e227a,0x446e,0x4137,{0xb2,0xb0,0x7e,0x1e,0x3c,0xa9,0xac,0x49}};

// the session refusal flag lives in viv_state.h (this module includes
// viv.h, not viv_state.h).
extern BYTE _viv_hw_render_fallback;

typedef HRESULT (WINAPI *_viv_d3d12_create_t)(const GUID *adapter_ref,HWND reserved,D3D_FEATURE_LEVEL minimum_feature_level,const GUID *riid,void **ppv_device);
typedef HRESULT (WINAPI *_viv_d3d12_factory1_t)(UINT flags,const GUID *riid,void **pp_factory);
typedef HRESULT (WINAPI *_viv_d3d12_serialize_t)(const D3D12_ROOT_SIGNATURE_DESC *p_root_signature,D3D_ROOT_SIGNATURE_VERSION version,ID3DBlob **pp_blob,ID3DBlob **pp_error_blob);
typedef HRESULT (WINAPI *_viv_d3d12_compile_t)(LPCSTR p_src_data,SIZE_T src_data_size,const char *p_source_name,const D3D_SHADER_MACRO *p_defines,ID3DInclude *p_include,const char *p_entrypoint,const char *p_target,UINT flags,UINT flags2,ID3DBlob **pp_code,ID3DBlob **pp_error_msgs);

static HMODULE _viv_d3d12_module;
static HMODULE _viv_d3d12_dxgi_module;
static HMODULE _viv_d3d12_compiler_module;
static _viv_d3d12_create_t _viv_d3d12_create;
static _viv_d3d12_factory1_t _viv_d3d12_factory1;
static _viv_d3d12_serialize_t _viv_d3d12_serialize;
static _viv_d3d12_compile_t _viv_d3d12_compile;

static IDXGIFactory2 *_viv_d3d12_factory;
static ID3D12Device *_viv_d3d12_device;
static ID3D12CommandQueue *_viv_d3d12_queue;
static IDXGISwapChain1 *_viv_d3d12_swap;
static ID3D12DescriptorHeap *_viv_d3d12_rtv_heap;
static ID3D12DescriptorHeap *_viv_d3d12_res_heap;
static ID3D12CommandAllocator *_viv_d3d12_allocator;
static ID3D12GraphicsCommandList *_viv_d3d12_list;
static ID3D12RootSignature *_viv_d3d12_rootsig;
static ID3D12PipelineState *_viv_d3d12_pso;
static ID3D12Resource *_viv_d3d12_quad;
static ID3D12Resource *_viv_d3d12_vb_upload;
static ID3D12Fence *_viv_d3d12_fence;
static HANDLE _viv_d3d12_fence_event;
static UINT64 _viv_d3d12_fence_value;
static UINT _viv_d3d12_rtv_stride;
static UINT _viv_d3d12_rtv_index;

static ID3D12Resource *_viv_d3d12_backbuffer[2];
static ID3D12Resource *_viv_d3d12_texture;
static ID3D12Resource *_viv_d3d12_upload;
static SIZE_T _viv_d3d12_upload_size;
static int _viv_d3d12_upload_pending;

static HWND _viv_d3d12_hwnd;
static int _viv_d3d12_client_wide;
static int _viv_d3d12_client_high;
static int _viv_d3d12_failed;
static HBITMAP _viv_d3d12_last_hbitmap;
static int _viv_d3d12_tex_wide;
static int _viv_d3d12_tex_high;
static unsigned _viv_d3d12_selfcheck_done;

static BYTE *_viv_d3d12_export_bits;
static int _viv_d3d12_export_wide;
static int _viv_d3d12_export_high;
static int _viv_d3d12_export_filled;

// the probe's process-lifetime cache (the d3d9 module's shape): a machine
// does not change its mind about d3d12 while the viewer runs.
static int _viv_d3d12_probe_answered;
static int _viv_d3d12_probe_ok;

// the quad's shader pair: the unit quad's positions ride the static
// buffer, the destination rectangle arrives as four root constants (the
// frame stream posts them per draw - no per frame buffer writes), the
// pixel shader is one sample. the flip model blits top-down rows, the
// same order the dib carries, so the screen's top edge samples the
// image's top (the one mapping every frame this app builds rides).
static const char _viv_d3d12_vs_src[] =
"cbuffer rect_c : register(b0) { float4 rect; };"
"struct v_out { float4 p : SV_Position; float2 t : T; };"
"v_out main(float2 p : P, float2 t : T) { v_out o; o.p = float4(rect.x + p.x * (rect.z - rect.x), rect.y + p.y * (rect.w - rect.y), 0.0f, 1.0f); o.t = float2(t.x, t.y); return o; }";

static const char _viv_d3d12_ps_src[] =
"Texture2D tex : register(t0);"
"SamplerState smp : register(s0);"
"float4 main(float4 p : SV_Position, float2 t : T) : SV_Target { return tex.Sample(smp, t); }";

// the unit quad: positions and texture coordinates run 0..1 (the root
// constants carry the destination rectangle in clip space).
typedef struct _viv_d3d12_vertex_s
{
	float x;
	float y;
	float u;
	float v;
}_viv_d3d12_vertex_t;

static const _viv_d3d12_vertex_t _viv_d3d12_quad_verts[4] =
{
	{0.0f,0.0f,0.0f,0.0f},
	{1.0f,0.0f,1.0f,0.0f},
	{0.0f,1.0f,0.0f,1.0f},
	{1.0f,1.0f,1.0f,1.0f}
};

static int _viv_d3d12_retarget(void);
static void _viv_d3d12_drop_backbuffers(void);
static int _viv_d3d12_selfcheck(HBITMAP hbitmap,DIBSECTION *ds);
static void _viv_d3d12_export_readback(void);

int _viv_hwd3d12_available(void)
{
	if (_viv_d3d12_probe_answered)
	{
		return _viv_d3d12_probe_ok;
	}

	_viv_d3d12_probe_answered = 1;
	_viv_d3d12_probe_ok = 0;

	if (!_viv_d3d12_module)
	{
		_viv_d3d12_module = LoadLibraryA("d3d12.dll");

		if (!_viv_d3d12_module)
		{
			debug_printf("direct3d12 probe: d3d12.dll is not on this system\r\n");

			return 0;
		}
	}

	if (!_viv_d3d12_create)
	{
		_viv_d3d12_create = (_viv_d3d12_create_t)GetProcAddress(_viv_d3d12_module,"D3D12CreateDevice");

		if (!_viv_d3d12_create)
		{
			return 0;
		}
	}

	_viv_d3d12_probe_ok = 1;

	return 1;
}

// the fence wait: the upload buffer's reuse (and every readback) holds
// on the copy's fence value - the GPU consumes before the CPU rewrites.
static void _viv_d3d12_wait_fence(UINT64 value)
{
	if (_viv_d3d12_fence)
	{
		if (_viv_d3d12_fence->lpVtbl->GetCompletedValue(_viv_d3d12_fence) < value)
		{
			_viv_d3d12_fence->lpVtbl->SetEventOnCompletion(_viv_d3d12_fence,value,_viv_d3d12_fence_event);

			WaitForSingleObject(_viv_d3d12_fence_event,INFINITE);
		}
	}
}

static void _viv_d3d12_submit(void)
{
	HRESULT hresult;

	if ((!_viv_d3d12_queue) || (!_viv_d3d12_list) || (!_viv_d3d12_fence))
	{
		return;
	}

	hresult = _viv_d3d12_list->lpVtbl->Close(_viv_d3d12_list);

	if (FAILED(hresult))
	{
		return;
	}

	_viv_d3d12_queue->lpVtbl->ExecuteCommandLists(_viv_d3d12_queue,1,(ID3D12CommandList *const *)&_viv_d3d12_list);

	_viv_d3d12_fence_value++;

	_viv_d3d12_queue->lpVtbl->Signal(_viv_d3d12_queue,_viv_d3d12_fence,_viv_d3d12_fence_value);
}

static void _viv_d3d12_drop_texture(void)
{
	if (_viv_d3d12_texture)
	{
		_viv_d3d12_texture->lpVtbl->Release(_viv_d3d12_texture);

		_viv_d3d12_texture = NULL;
	}

	_viv_d3d12_last_hbitmap = 0;
	_viv_d3d12_tex_wide = 0;
	_viv_d3d12_tex_high = 0;
	_viv_d3d12_selfcheck_done = 0;
}

static void _viv_d3d12_drop_swapchain(void)
{
	if (_viv_d3d12_swap)
	{
		_viv_d3d12_swap->lpVtbl->Release(_viv_d3d12_swap);

		_viv_d3d12_swap = NULL;
	}
}

static void _viv_d3d12_drop_device(void)
{
	// the texture dies before the fence wait: a pending upload's signal
	// still lands (the queue outlives this call) and the readback paths
	// wait on their own values inside their callers.
	_viv_d3d12_drop_texture();
	_viv_d3d12_drop_backbuffers();
	_viv_d3d12_drop_swapchain();

	if (_viv_d3d12_upload)
	{
		_viv_d3d12_upload->lpVtbl->Release(_viv_d3d12_upload);

		_viv_d3d12_upload = NULL;
	}

	_viv_d3d12_upload_size = 0;
	_viv_d3d12_upload_pending = 0;

	if (_viv_d3d12_quad)
	{
		_viv_d3d12_quad->lpVtbl->Release(_viv_d3d12_quad);

		_viv_d3d12_quad = NULL;
	}

	if (_viv_d3d12_vb_upload)
	{
		_viv_d3d12_vb_upload->lpVtbl->Release(_viv_d3d12_vb_upload);

		_viv_d3d12_vb_upload = NULL;
	}

	if (_viv_d3d12_pso)
	{
		_viv_d3d12_pso->lpVtbl->Release(_viv_d3d12_pso);

		_viv_d3d12_pso = NULL;
	}

	if (_viv_d3d12_rootsig)
	{
		_viv_d3d12_rootsig->lpVtbl->Release(_viv_d3d12_rootsig);

		_viv_d3d12_rootsig = NULL;
	}

	if (_viv_d3d12_list)
	{
		_viv_d3d12_list->lpVtbl->Release(_viv_d3d12_list);

		_viv_d3d12_list = NULL;
	}

	if (_viv_d3d12_allocator)
	{
		_viv_d3d12_allocator->lpVtbl->Release(_viv_d3d12_allocator);

		_viv_d3d12_allocator = NULL;
	}

	if (_viv_d3d12_res_heap)
	{
		_viv_d3d12_res_heap->lpVtbl->Release(_viv_d3d12_res_heap);

		_viv_d3d12_res_heap = NULL;
	}

	if (_viv_d3d12_rtv_heap)
	{
		_viv_d3d12_rtv_heap->lpVtbl->Release(_viv_d3d12_rtv_heap);

		_viv_d3d12_rtv_heap = NULL;
	}

	if (_viv_d3d12_fence)
	{
		_viv_d3d12_fence->lpVtbl->Release(_viv_d3d12_fence);

		_viv_d3d12_fence = NULL;
	}

	if (_viv_d3d12_fence_event)
	{
		CloseHandle(_viv_d3d12_fence_event);

		_viv_d3d12_fence_event = NULL;
	}

	if (_viv_d3d12_queue)
	{
		_viv_d3d12_queue->lpVtbl->Release(_viv_d3d12_queue);

		_viv_d3d12_queue = NULL;
	}

	if (_viv_d3d12_factory)
	{
		_viv_d3d12_factory->lpVtbl->Release(_viv_d3d12_factory);

		_viv_d3d12_factory = NULL;
	}

	if (_viv_d3d12_device)
	{
		_viv_d3d12_device->lpVtbl->Release(_viv_d3d12_device);

		_viv_d3d12_device = NULL;
	}

	_viv_d3d12_hwnd = 0;
	_viv_d3d12_client_wide = 0;
	_viv_d3d12_client_high = 0;
	_viv_d3d12_fence_value = 0;
}

// the rtvs over the two flip buffers: created once per buffer size (the
// resize answer releases the heap and answers it fresh).
static void _viv_d3d12_drop_backbuffers(void)
{
	UINT i;

	for(i=0;i<2;i++)
	{
		if (_viv_d3d12_backbuffer[i])
		{
			_viv_d3d12_backbuffer[i]->lpVtbl->Release(_viv_d3d12_backbuffer[i]);

			_viv_d3d12_backbuffer[i] = NULL;
		}
	}
}

static int _viv_d3d12_retarget(void)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle;
	UINT i;

	_viv_d3d12_drop_backbuffers();

	handle.ptr = 0;

	if (_viv_d3d12_rtv_heap)
	{
		handle = *_viv_d3d12_rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(_viv_d3d12_rtv_heap,&handle);
	}

	for(i=0;i<2;i++)
	{
		if (FAILED(_viv_d3d12_swap->lpVtbl->GetBuffer(_viv_d3d12_swap,i,&_viv_d3d12_iid_resource,(void **)&_viv_d3d12_backbuffer[i])))
		{
			_viv_d3d12_backbuffer[i] = NULL;

			return 0;
		}

		_viv_d3d12_device->lpVtbl->CreateRenderTargetView(_viv_d3d12_device,_viv_d3d12_backbuffer[i],NULL,handle);

		handle.ptr += _viv_d3d12_rtv_stride;
	}

	_viv_d3d12_rtv_index = 0;

	return 1;
}

static int _viv_d3d12_init(HWND hwnd)
{
	D3D12_COMMAND_QUEUE_DESC queue_desc;
	DXGI_SWAP_CHAIN_DESC1 swap_desc;
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc;
	D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
	HRESULT hresult;
	RECT rect;
	ID3DBlob *vs_blob;
	ID3DBlob *ps_blob;
	ID3DBlob *sig_blob;
	ID3DBlob *sig_error;
	D3D12_ROOT_SIGNATURE_DESC sig_desc;
	D3D12_ROOT_PARAMETER params[2];
	D3D12_DESCRIPTOR_RANGE range;
	D3D12_STATIC_SAMPLER_DESC sampler_desc;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
	D3D12_INPUT_ELEMENT_DESC elements[2];
	D3D12_HEAP_PROPERTIES heap_desc_props;
	D3D12_RESOURCE_DESC buf_desc;
	D3D12_RANGE read_none;
	BYTE *vb_map;

	if (_viv_d3d12_device)
	{
		if (_viv_d3d12_hwnd == hwnd) return 1;

		// a window change retires the whole device (the d3d9 init's
		// shape): the swapchain binds to its hwnd by contract.
		_viv_d3d12_drop_device();
	}

	if (!_viv_d3d12_module)
	{
		_viv_d3d12_module = LoadLibraryA("d3d12.dll");
	}

	if (!_viv_d3d12_dxgi_module)
	{
		_viv_d3d12_dxgi_module = LoadLibraryA("dxgi.dll");
	}

	if (!_viv_d3d12_compiler_module)
	{
		_viv_d3d12_compiler_module = LoadLibraryA("d3dcompiler_47.dll");
	}

	if ((!_viv_d3d12_module) || (!_viv_d3d12_dxgi_module) || (!_viv_d3d12_compiler_module))
	{
		debug_printf("direct3d12: d3d12.dll / dxgi.dll / d3dcompiler_47.dll did not answer\r\n");

		_viv_d3d12_failed = 1;

		return 0;
	}

	if ((!_viv_d3d12_create) || (!_viv_d3d12_factory1) || (!_viv_d3d12_serialize) || (!_viv_d3d12_compile))
	{
		_viv_d3d12_create = (_viv_d3d12_create_t)GetProcAddress(_viv_d3d12_module,"D3D12CreateDevice");
		_viv_d3d12_factory1 = (_viv_d3d12_factory1_t)GetProcAddress(_viv_d3d12_dxgi_module,"CreateDXGIFactory1");
		_viv_d3d12_serialize = (_viv_d3d12_serialize_t)GetProcAddress(_viv_d3d12_module,"D3D12SerializeRootSignature");
		_viv_d3d12_compile = (_viv_d3d12_compile_t)GetProcAddress(_viv_d3d12_compiler_module,"D3DCompile");

		if ((!_viv_d3d12_create) || (!_viv_d3d12_factory1) || (!_viv_d3d12_serialize) || (!_viv_d3d12_compile))
		{
			_viv_d3d12_failed = 1;

			return 0;
		}
	}

	GetClientRect(hwnd,&rect);
	_viv_d3d12_client_wide = rect.right - rect.left;
	_viv_d3d12_client_high = rect.bottom - rect.top;

	// the device at the 11.0 floor: the quad needs nothing the first
	// dx12-capable machine already carries.
	hresult = _viv_d3d12_create(NULL,NULL,D3D_FEATURE_LEVEL_11_0,&_viv_d3d12_iid_device,(void **)&_viv_d3d12_device);

	if (FAILED(hresult))
	{
		debug_printf("direct3d12: the device did not come up %x\r\n",(unsigned int)hresult);

		_viv_d3d12_failed = 1;

		return 0;
	}

	ZeroMemory(&queue_desc,sizeof(queue_desc));
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	hresult = _viv_d3d12_device->lpVtbl->CreateCommandQueue(_viv_d3d12_device,&queue_desc,&_viv_d3d12_iid_queue,(void **)&_viv_d3d12_queue);

	if (FAILED(hresult))
	{
		debug_printf("direct3d12: the queue did not come up %x\r\n",(unsigned int)hresult);

		_viv_d3d12_drop_device();

		_viv_d3d12_failed = 1;

		return 0;
	}

	_viv_d3d12_hwnd = hwnd;

	hresult = _viv_d3d12_factory1(0,&_viv_d3d12_iid_factory2,(void **)&_viv_d3d12_factory);

	if (SUCCEEDED(hresult))
	{
		ZeroMemory(&swap_desc,sizeof(swap_desc));
		swap_desc.Width = (UINT)_viv_d3d12_client_wide;
		swap_desc.Height = (UINT)_viv_d3d12_client_high;
		swap_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		swap_desc.SampleDesc.Count = 1;
		swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swap_desc.BufferCount = 2;
		swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		ZeroMemory(&fullscreen_desc,sizeof(fullscreen_desc));

		hresult = _viv_d3d12_factory->lpVtbl->CreateSwapChainForHwnd(_viv_d3d12_factory,(IUnknown *)_viv_d3d12_queue,hwnd,&swap_desc,&fullscreen_desc,NULL,&_viv_d3d12_swap);

		if (SUCCEEDED(hresult))
		{
			// alt-enter belongs to the viewer's own fullscreen answer,
			// never the swapchain's.
			_viv_d3d12_factory->lpVtbl->MakeWindowAssociation(_viv_d3d12_factory,hwnd,DXGI_MWA_NO_ALT_ENTER);
		}
	}

	if (SUCCEEDED(hresult))
	{
		ZeroMemory(&heap_desc,sizeof(heap_desc));
		heap_desc.NumDescriptors = 2;
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

		hresult = _viv_d3d12_device->lpVtbl->CreateDescriptorHeap(_viv_d3d12_device,&heap_desc,&_viv_d3d12_iid_heap,(void **)&_viv_d3d12_rtv_heap);

		if (SUCCEEDED(hresult))
		{
			_viv_d3d12_rtv_stride = _viv_d3d12_device->lpVtbl->GetDescriptorHandleIncrementSize(_viv_d3d12_device,D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}
	}

	if (SUCCEEDED(hresult))
	{
		heap_desc.NumDescriptors = 1;
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

		hresult = _viv_d3d12_device->lpVtbl->CreateDescriptorHeap(_viv_d3d12_device,&heap_desc,&_viv_d3d12_iid_heap,(void **)&_viv_d3d12_res_heap);
	}

	if (SUCCEEDED(hresult))
	{
		hresult = _viv_d3d12_device->lpVtbl->CreateCommandAllocator(_viv_d3d12_device,D3D12_COMMAND_LIST_TYPE_DIRECT,&_viv_d3d12_iid_allocator,(void **)&_viv_d3d12_allocator);
	}

	if (SUCCEEDED(hresult))
	{
		hresult = _viv_d3d12_device->lpVtbl->CreateCommandList(_viv_d3d12_device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,_viv_d3d12_allocator,NULL,&_viv_d3d12_iid_list,(void **)&_viv_d3d12_list);

		if (SUCCEEDED(hresult))
		{
			// a fresh list lands in the recording state: close it, the
			// frame's reset opens it again.
			_viv_d3d12_list->lpVtbl->Close(_viv_d3d12_list);
		}
	}

	if (SUCCEEDED(hresult))
	{
		hresult = _viv_d3d12_device->lpVtbl->CreateFence(_viv_d3d12_device,0,D3D12_FENCE_FLAG_NONE,&_viv_d3d12_iid_fence,(void **)&_viv_d3d12_fence);

		if (SUCCEEDED(hresult))
		{
			_viv_d3d12_fence_event = CreateEvent(NULL,FALSE,FALSE,NULL);

			if (!_viv_d3d12_fence_event)
			{
				hresult = E_FAIL;
			}
		}
	}

	if (SUCCEEDED(hresult))
	{
		// the root signature: one descriptor table (the texture's srv,
		// pixel stage), one static sampler, four root constants for the
		// destination rectangle (vertex stage, b0).
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 1;
		range.BaseShaderRegister = 0;
		range.RegisterSpace = 0;
		range.OffsetInDescriptorsFromTableStart = 0;

		ZeroMemory(&params[0],sizeof(params[0]));
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[0].DescriptorTable.NumDescriptorRanges = 1;
		params[0].DescriptorTable.pDescriptorRanges = &range;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		ZeroMemory(&params[1],sizeof(params[1]));
		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		params[1].Constants.Num32BitValues = 4;
		params[1].Constants.ShaderRegister = 0;
		params[1].Constants.RegisterSpace = 0;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		ZeroMemory(&sampler_desc,sizeof(sampler_desc));
		sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler_desc.MaxAnisotropy = 0;
		sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler_desc.MinLOD = 0.0f;
		sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
		sampler_desc.ShaderRegister = 0;
		sampler_desc.RegisterSpace = 0;
		sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		ZeroMemory(&sig_desc,sizeof(sig_desc));
		sig_desc.NumParameters = 2;
		sig_desc.pParameters = params;
		sig_desc.NumStaticSamplers = 1;
		sig_desc.pStaticSamplers = &sampler_desc;
		sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		sig_blob = NULL;
		sig_error = NULL;

		hresult = _viv_d3d12_serialize(&sig_desc,D3D_ROOT_SIGNATURE_VERSION_1,&sig_blob,&sig_error);

		if (SUCCEEDED(hresult))
		{
			hresult = _viv_d3d12_device->lpVtbl->CreateRootSignature(_viv_d3d12_device,0,sig_blob->lpVtbl->GetBufferPointer(sig_blob),sig_blob->lpVtbl->GetBufferSize(sig_blob),&_viv_d3d12_iid_rootsig,(void **)&_viv_d3d12_rootsig);

			sig_blob->lpVtbl->Release(sig_blob);
		}

		if (sig_error)
		{
			sig_error->lpVtbl->Release(sig_error);
		}
	}

	if (SUCCEEDED(hresult))
	{
		vs_blob = NULL;
		ps_blob = NULL;

		hresult = _viv_d3d12_compile(_viv_d3d12_vs_src,sizeof(_viv_d3d12_vs_src) - 1,"viv",NULL,NULL,"main","vs_5_0",0,0,&vs_blob,NULL);

		if (SUCCEEDED(hresult))
		{
			hresult = _viv_d3d12_compile(_viv_d3d12_ps_src,sizeof(_viv_d3d12_ps_src) - 1,"viv",NULL,NULL,"main","ps_5_0",0,0,&ps_blob,NULL);
		}

		if (SUCCEEDED(hresult))
		{
			elements[0].SemanticName = "P";
			elements[0].SemanticIndex = 0;
			elements[0].Format = DXGI_FORMAT_R32G32_FLOAT;
			elements[0].InputSlot = 0;
			elements[0].AlignedByteOffset = 0;
			elements[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			elements[0].InstanceDataStepRate = 0;

			elements[1].SemanticName = "T";
			elements[1].SemanticIndex = 0;
			elements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
			elements[1].InputSlot = 0;
			elements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
			elements[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			elements[1].InstanceDataStepRate = 0;

			ZeroMemory(&pso_desc,sizeof(pso_desc));
			pso_desc.InputLayout.pInputElementDescs = elements;
			pso_desc.InputLayout.NumElements = 2;
			pso_desc.pRootSignature = _viv_d3d12_rootsig;
			pso_desc.VS.pShaderBytecode = vs_blob->lpVtbl->GetBufferPointer(vs_blob);
			pso_desc.VS.BytecodeLength = vs_blob->lpVtbl->GetBufferSize(vs_blob);
			pso_desc.PS.pShaderBytecode = ps_blob->lpVtbl->GetBufferPointer(ps_blob);
			pso_desc.PS.BytecodeLength = ps_blob->lpVtbl->GetBufferSize(ps_blob);
			pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			pso_desc.SampleMask = 0xffffffff;
			pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
			pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
			pso_desc.RasterizerState.DepthClipEnable = TRUE;
			pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			pso_desc.NumRenderTargets = 1;
			pso_desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
			pso_desc.SampleDesc.Count = 1;

			hresult = _viv_d3d12_device->lpVtbl->CreateGraphicsPipelineState(_viv_d3d12_device,&pso_desc,&_viv_d3d12_iid_pso,(void **)&_viv_d3d12_pso);
		}

		if (vs_blob)
		{
			vs_blob->lpVtbl->Release(vs_blob);
		}

		if (ps_blob)
		{
			ps_blob->lpVtbl->Release(ps_blob);
		}
	}

	if (SUCCEEDED(hresult))
	{
		// the quad: an upload-heap staging copy into a default-heap
		// buffer, one time per device (the contents never change - the
		// unit corners and the unit texture coordinates).
		ZeroMemory(&heap_desc_props,sizeof(heap_desc_props));
		heap_desc_props.Type = D3D12_HEAP_TYPE_UPLOAD;

		ZeroMemory(&buf_desc,sizeof(buf_desc));
		buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		buf_desc.Width = sizeof(_viv_d3d12_quad_verts);
		buf_desc.Height = 1;
		buf_desc.DepthOrArraySize = 1;
		buf_desc.MipLevels = 1;
		buf_desc.Format = DXGI_FORMAT_UNKNOWN;
		buf_desc.SampleDesc.Count = 1;
		buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		hresult = _viv_d3d12_device->lpVtbl->CreateCommittedResource(_viv_d3d12_device,&heap_desc_props,D3D12_HEAP_FLAG_NONE,&buf_desc,D3D12_RESOURCE_STATE_GENERIC_READ,NULL,&_viv_d3d12_iid_resource,(void **)&_viv_d3d12_vb_upload);

		if (SUCCEEDED(hresult))
		{
			read_none.Begin = 0;
			read_none.End = 0;

			if (SUCCEEDED(_viv_d3d12_vb_upload->lpVtbl->Map(_viv_d3d12_vb_upload,0,&read_none,(void **)&vb_map)))
			{
				memcpy(vb_map,_viv_d3d12_quad_verts,sizeof(_viv_d3d12_quad_verts));

				_viv_d3d12_vb_upload->lpVtbl->Unmap(_viv_d3d12_vb_upload,0,&read_none);
			}
			else
			{
				hresult = E_FAIL;
			}
		}

		if (SUCCEEDED(hresult))
		{
			ZeroMemory(&heap_desc_props,sizeof(heap_desc_props));
			heap_desc_props.Type = D3D12_HEAP_TYPE_DEFAULT;

			hresult = _viv_d3d12_device->lpVtbl->CreateCommittedResource(_viv_d3d12_device,&heap_desc_props,D3D12_HEAP_FLAG_NONE,&buf_desc,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,NULL,&_viv_d3d12_iid_resource,(void **)&_viv_d3d12_quad);

			if (SUCCEEDED(hresult))
			{
				// the one-time staging copy rides its own submission (the
				// allocator is open for nothing else this early).
				_viv_d3d12_list->lpVtbl->Reset(_viv_d3d12_list,_viv_d3d12_allocator,NULL);
				_viv_d3d12_list->lpVtbl->CopyBufferRegion(_viv_d3d12_list,(ID3D12Resource *)_viv_d3d12_quad,0,(ID3D12Resource *)_viv_d3d12_vb_upload,0,sizeof(_viv_d3d12_quad_verts));
				_viv_d3d12_submit();
				_viv_d3d12_wait_fence(_viv_d3d12_fence_value);
			}
		}
	}

	if (FAILED(hresult))
	{
		debug_printf("direct3d12: the device state did not come up %x\r\n",(unsigned int)hresult);

		_viv_d3d12_drop_device();

		_viv_d3d12_failed = 1;

		return 0;
	}

	if (!_viv_d3d12_retarget())
	{
		_viv_d3d12_drop_device();

		_viv_d3d12_failed = 1;

		return 0;
	}

	return 1;
}

// the upload: a default-heap bgra texture fed from the dib's rows through
// the ONE upload buffer (reused, grown once per size class, the fence
// holds the rewrite back until the previous copy landed).
static int _viv_d3d12_texture_upload(HBITMAP hbitmap,DIBSECTION *ds)
{
	int wide;
	int high;
	int stride;
	int i;
	D3D12_HEAP_PROPERTIES default_heap;
	D3D12_HEAP_PROPERTIES upload_heap;
	D3D12_RESOURCE_DESC tex_desc;
	D3D12_RESOURCE_DESC buf_desc;
	D3D12_RANGE read_none;
	BYTE *upload_map;
	HRESULT hresult;

	wide = ds->dsBm.bmWidth;
	high = ds->dsBm.bmHeight;

	if ((wide <= 0) || (high <= 0))
	{
		return 0;
	}

	if ((wide > 16384) || (high > 16384))
	{
		debug_printf("direct3d12: the image exceeds the max texture size\r\n");

		return 0;
	}

	// the byte gate bounds the row and the upload copy; calculate before
	// narrowing.
	stride = (int)((((SIZE_T)wide * ds->dsBm.bmBitsPixel + 31) / 32) * 4);

	// exactly one image texture lives at a time: the dimension change
	// releases the old one first (the d3d9 reuse gate's shape).
	if ((_viv_d3d12_texture) && ((wide != _viv_d3d12_tex_wide) || (high != _viv_d3d12_tex_high)))
	{
		_viv_hwd3d12_release_image();
	}

	// the 24bpp dib rows do not speak bgra: like the d3d11 module, the
	// expansion runs through the upload map (the same pass that copies).
	if (ds->dsBm.bmBitsPixel != 24)
	{
		if (ds->dsBm.bmBitsPixel != 32)
		{
			return 0;
		}
	}

	if ((safe_size_mul((SIZE_T)stride,(SIZE_T)high) == 0) || (safe_size_mul((SIZE_T)stride,(SIZE_T)high) > VIV_MAX_HARDWARE_BYTES))
	{
		debug_printf("direct3d12: the image exceeds the hardware byte budget\r\n");

		return 0;
	}

	// the fence: the previous upload's copy consumed before this one
	// rewrites the shared buffer (the reuse is the memory saving).
	if (_viv_d3d12_upload_pending)
	{
		_viv_d3d12_wait_fence(_viv_d3d12_fence_value);

		_viv_d3d12_upload_pending = 0;
	}

	if (!_viv_d3d12_texture)
	{
		ZeroMemory(&default_heap,sizeof(default_heap));
		default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;

		ZeroMemory(&tex_desc,sizeof(tex_desc));
		tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		tex_desc.Width = (UINT64)wide;
		tex_desc.Height = (UINT)high;
		tex_desc.DepthOrArraySize = 1;
		tex_desc.MipLevels = 1;
		tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		tex_desc.SampleDesc.Count = 1;

		hresult = _viv_d3d12_device->lpVtbl->CreateCommittedResource(_viv_d3d12_device,&default_heap,D3D12_HEAP_FLAG_NONE,&tex_desc,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&_viv_d3d12_iid_resource,(void **)&_viv_d3d12_texture);

		if (FAILED(hresult))
		{
			debug_printf("direct3d12: the texture did not come up %x\r\n",(unsigned int)hresult);

			return 0;
		}

		_viv_d3d12_tex_wide = wide;
		_viv_d3d12_tex_high = high;
	}

	{
		SIZE_T need;

		need = (SIZE_T)stride * (SIZE_T)high;

		if ((!_viv_d3d12_upload) || (_viv_d3d12_upload_size < need))
		{
			if (_viv_d3d12_upload)
			{
				_viv_d3d12_upload->lpVtbl->Release(_viv_d3d12_upload);

				_viv_d3d12_upload = NULL;
				_viv_d3d12_upload_size = 0;
			}

			ZeroMemory(&upload_heap,sizeof(upload_heap));
			upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

			ZeroMemory(&buf_desc,sizeof(buf_desc));
			buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			buf_desc.Width = (UINT64)need;
			buf_desc.Height = 1;
			buf_desc.DepthOrArraySize = 1;
			buf_desc.MipLevels = 1;
			buf_desc.Format = DXGI_FORMAT_UNKNOWN;
			buf_desc.SampleDesc.Count = 1;
			buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			hresult = _viv_d3d12_device->lpVtbl->CreateCommittedResource(_viv_d3d12_device,&upload_heap,D3D12_HEAP_FLAG_NONE,&buf_desc,D3D12_RESOURCE_STATE_GENERIC_READ,NULL,&_viv_d3d12_iid_resource,(void **)&_viv_d3d12_upload);

			if (FAILED(hresult))
			{
				debug_printf("direct3d12: the upload buffer did not come up %x\r\n",(unsigned int)hresult);

				_viv_hwd3d12_release_image();

				return 0;
			}

			_viv_d3d12_upload_size = need;
		}

		read_none.Begin = 0;
		read_none.End = 0;

		if (FAILED(_viv_d3d12_upload->lpVtbl->Map(_viv_d3d12_upload,0,&read_none,(void **)&upload_map)))
		{
			_viv_hwd3d12_release_image();

			return 0;
		}

		if (ds->dsBm.bmBitsPixel == 24)
		{
			// the expansion: the dib's 3-byte rows become b,g,r,255 quads
			// in the upload map (the d3d9 lock loop's own pass).
			const BYTE *srow;
			BYTE *drow;

			srow = (const BYTE *)ds->dsBm.bmBits;
			drow = upload_map;

			for(i=0;i<high;i++)
			{
				const BYTE *sp;
				BYTE *dp;
				int x;

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
		else
		{
			memcpy(upload_map,ds->dsBm.bmBits,need);
		}

		_viv_d3d12_upload->lpVtbl->Unmap(_viv_d3d12_upload,0,&read_none);
	}

	{
		D3D12_SUBRESOURCE_FOOTPRINT footprint;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed;
		D3D12_TEXTURE_COPY_LOCATION source;
		D3D12_TEXTURE_COPY_LOCATION dest;

		ZeroMemory(&footprint,sizeof(footprint));
		footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		footprint.Width = (UINT)wide;
		footprint.Height = (UINT)high;
		footprint.Depth = 1;
		footprint.RowPitch = (UINT)((ds->dsBm.bmBitsPixel == 24) ? wide * 4 : stride);

		ZeroMemory(&placed,sizeof(placed));
		placed.Footprint = footprint;

		source.pResource = _viv_d3d12_upload;
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = placed;

		dest.pResource = _viv_d3d12_texture;
		dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dest.SubresourceIndex = 0;

		_viv_d3d12_list->lpVtbl->Reset(_viv_d3d12_list,_viv_d3d12_allocator,NULL);
		_viv_d3d12_list->lpVtbl->CopyTextureRegion(_viv_d3d12_list,&dest,0,0,0,&source,NULL);

		{
			D3D12_RESOURCE_BARRIER barrier;

			ZeroMemory(&barrier,sizeof(barrier));
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = _viv_d3d12_texture;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			_viv_d3d12_list->lpVtbl->ResourceBarrier(_viv_d3d12_list,1,&barrier);
		}

		_viv_d3d12_submit();

		_viv_d3d12_upload_pending = 1;
	}

	_viv_d3d12_last_hbitmap = hbitmap;
	_viv_d3d12_selfcheck_done = 0;

	return 1;
}

int _viv_hwd3d12_render(HWND hwnd,HDC hdc,HBITMAP hbitmap,int dst_x,int dst_y,int dst_wide,int dst_high,COLORREF clear_color)
{
	DIBSECTION ds;
	D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
	D3D12_GPU_DESCRIPTOR_HANDLE res_handle;
	D3D12_GPU_VIRTUAL_ADDRESS vb_addr;
	D3D12_VERTEX_BUFFER_VIEW vb_view;
	D3D12_VIEWPORT viewport;
	D3D12_RECT scissor;
	D3D12_RESOURCE_BARRIER barrier;
	float clear_rgba[4];
	float rect_constants[4];
	HRESULT hresult;
	HRESULT present_result;

	if (_viv_d3d12_failed)
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
			debug_printf("direct3d12: the frame is not a dib section (no bits answered) - the gdi path paints it\r\n");
		}
		else
		{
			debug_printf("direct3d12: the frame is %d bpp (24 or 32 answer) - the gdi path paints it\r\n",ds.dsBm.bmBitsPixel);
		}

		return 0;
	}

	if (!_viv_d3d12_init(hwnd))
	{
		return 0;
	}

	// the resize answer: the buffers resize (the heap releases and
	// answers fresh, the texture survives).
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

		if ((wide != _viv_d3d12_client_wide) || (high != _viv_d3d12_client_high))
		{
			if (_viv_d3d12_swap->lpVtbl->ResizeBuffers(_viv_d3d12_swap,0,(UINT)wide,(UINT)high,DXGI_FORMAT_UNKNOWN,0) == DXGI_ERROR_DEVICE_REMOVED)
			{
				debug_printf("direct3d12: the device left during the resize\r\n");

				_viv_d3d12_drop_device();

				return 0;
			}

			_viv_d3d12_client_wide = wide;
			_viv_d3d12_client_high = high;

			if (!_viv_d3d12_retarget())
			{
				return 0;
			}
		}
	}

	if (hbitmap != _viv_d3d12_last_hbitmap)
	{
		// an oversized image is a per-image refusal, not a session failure.
		if (!_viv_d3d12_texture_upload(hbitmap,&ds))
		{
			return 0;
		}
	}

	rtv_handle = *_viv_d3d12_rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(_viv_d3d12_rtv_heap,&rtv_handle);

	rtv_handle.ptr += (SIZE_T)_viv_d3d12_rtv_index * (SIZE_T)_viv_d3d12_rtv_stride;

	res_handle = *_viv_d3d12_res_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(_viv_d3d12_res_heap,&res_handle);

	// the destination rectangle in clip space (the -0.5 texel offset the
	// raster wants, the d3d9 vertex math's own shape).
	rect_constants[0] = (((float)dst_x - 0.5f) / (float)_viv_d3d12_client_wide) * 2.0f - 1.0f;
	rect_constants[1] = 1.0f - (((float)dst_y - 0.5f) / (float)_viv_d3d12_client_high) * 2.0f;
	rect_constants[2] = (((float)(dst_x + dst_wide) - 0.5f) / (float)_viv_d3d12_client_wide) * 2.0f - 1.0f;
	rect_constants[3] = 1.0f - (((float)(dst_y + dst_high) - 0.5f) / (float)_viv_d3d12_client_high) * 2.0f;

	clear_rgba[0] = (float)GetRValue(clear_color) / 255.0f;
	clear_rgba[1] = (float)GetGValue(clear_color) / 255.0f;
	clear_rgba[2] = (float)GetBValue(clear_color) / 255.0f;
	clear_rgba[3] = 1.0f;

	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = (float)_viv_d3d12_client_wide;
	viewport.Height = (float)_viv_d3d12_client_high;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	scissor.left = 0;
	scissor.top = 0;
	scissor.right = _viv_d3d12_client_wide;
	scissor.bottom = _viv_d3d12_client_high;

	_viv_d3d12_list->lpVtbl->Reset(_viv_d3d12_list,_viv_d3d12_allocator,_viv_d3d12_pso);

	ZeroMemory(&barrier,sizeof(barrier));
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	// the frame barrier answers the buffer the rtv index owns (the
	// flip model's own resources, cached at retarget).
	barrier.Transition.pResource = _viv_d3d12_backbuffer[_viv_d3d12_rtv_index];
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	_viv_d3d12_list->lpVtbl->ResourceBarrier(_viv_d3d12_list,1,&barrier);

	_viv_d3d12_list->lpVtbl->RSSetViewports(_viv_d3d12_list,1,&viewport);
	_viv_d3d12_list->lpVtbl->RSSetScissorRects(_viv_d3d12_list,1,&scissor);
	_viv_d3d12_list->lpVtbl->OMSetRenderTargets(_viv_d3d12_list,1,&rtv_handle,FALSE,NULL);
	_viv_d3d12_list->lpVtbl->ClearRenderTargetView(_viv_d3d12_list,rtv_handle,clear_rgba,0,NULL);
	_viv_d3d12_list->lpVtbl->SetDescriptorHeaps(_viv_d3d12_list,1,&_viv_d3d12_res_heap);
	_viv_d3d12_list->lpVtbl->SetGraphicsRootSignature(_viv_d3d12_list,_viv_d3d12_rootsig);
	_viv_d3d12_list->lpVtbl->SetGraphicsRootDescriptorTable(_viv_d3d12_list,0,res_handle);
	_viv_d3d12_list->lpVtbl->SetGraphicsRoot32BitConstants(_viv_d3d12_list,1,4,rect_constants,0);

	vb_addr = _viv_d3d12_quad->lpVtbl->GetGPUVirtualAddress(_viv_d3d12_quad);

	vb_view.BufferLocation = vb_addr;
	vb_view.SizeInBytes = (UINT)sizeof(_viv_d3d12_quad_verts);
	vb_view.StrideInBytes = (UINT)sizeof(_viv_d3d12_vertex_t);

	_viv_d3d12_list->lpVtbl->IASetVertexBuffers(_viv_d3d12_list,0,1,&vb_view);
	_viv_d3d12_list->lpVtbl->IASetPrimitiveTopology(_viv_d3d12_list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	_viv_d3d12_list->lpVtbl->DrawInstanced(_viv_d3d12_list,4,1,0,0);

	ZeroMemory(&barrier,sizeof(barrier));
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = _viv_d3d12_backbuffer[_viv_d3d12_rtv_index];
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	_viv_d3d12_list->lpVtbl->ResourceBarrier(_viv_d3d12_list,1,&barrier);

	_viv_d3d12_submit();

	// the render self-check (the rc.3 render self-verification): the
	// first frame of a new texture draws once 1:1 into an offscreen
	// target and the sampled pixels answer against the dib's own bits -
	// a lying accelerator hands the session to the gdi path (the same
	// fallback flag the refusal face names).
	if (!_viv_d3d12_selfcheck_done)
	{
		_viv_d3d12_selfcheck_done = 1;

		if (!_viv_d3d12_selfcheck(hbitmap,&ds))
		{
			debug_printf("direct3d12: the render self-check did not match the source\r\n");

			_viv_hw_render_fallback = 1;

			return 0;
		}
	}

	if (_viv_d3d12_export_bits)
	{
		_viv_d3d12_export_readback();

		return _viv_d3d12_export_filled;
	}

	present_result = _viv_d3d12_swap->lpVtbl->Present(_viv_d3d12_swap,1,0);

	_viv_d3d12_rtv_index ^= 1;

	return SUCCEEDED(present_result);
}

void _viv_hwd3d12_release_image(void)
{
	// the pending upload's copy consumed before the texture dies (the
	// fence is the answer, the same one the reuse holds on).
	if (_viv_d3d12_upload_pending)
	{
		_viv_d3d12_wait_fence(_viv_d3d12_fence_value);

		_viv_d3d12_upload_pending = 0;
	}

	_viv_d3d12_drop_texture();
}

void _viv_hwd3d12_shutdown(void)
{
	_viv_hwd3d12_release_image();
	_viv_d3d12_drop_device();

	_viv_d3d12_failed = 0;

	// the export pointer dies with the caller's buffer - the shutdown
	// clears it so no later session state can point at freed memory.
	_viv_d3d12_export_bits = 0;
}

// the export readback: the back buffer holds the frame (the clear, the
// quad, the whole scene). the buffer transitions to the copy source, a
// readback heap takes it, the fence lands it and the map walks the rows
// into top-down bgra with the reserved byte pinned to zero.
static void _viv_d3d12_export_readback(void)
{
	ID3D12Resource *readback;
	D3D12_HEAP_PROPERTIES heap_props;
	D3D12_RESOURCE_DESC buf_desc;
	D3D12_TEXTURE_COPY_LOCATION source;
	D3D12_TEXTURE_COPY_LOCATION dest;
	D3D12_RESOURCE_BARRIER barrier;
	D3D12_RANGE read_range;
	BYTE *map;
	HRESULT hresult;
	UINT backi;

	readback = NULL;

	if ((!_viv_d3d12_device) || (!_viv_d3d12_backbuffer[_viv_d3d12_rtv_index ^ 1]))
	{
		return;
	}

	// the presented buffer is the other index (the present flipped the
	// pair's answer).
	backi = _viv_d3d12_rtv_index ^ 1;

	ZeroMemory(&heap_props,sizeof(heap_props));
	heap_props.Type = D3D12_HEAP_TYPE_READBACK;

	ZeroMemory(&buf_desc,sizeof(buf_desc));
	buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	buf_desc.Width = (UINT64)_viv_d3d12_export_wide * (UINT64)_viv_d3d12_export_high * 4;
	buf_desc.Height = 1;
	buf_desc.DepthOrArraySize = 1;
	buf_desc.MipLevels = 1;
	buf_desc.Format = DXGI_FORMAT_UNKNOWN;
	buf_desc.SampleDesc.Count = 1;
	buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	hresult = _viv_d3d12_device->lpVtbl->CreateCommittedResource(_viv_d3d12_device,&heap_props,D3D12_HEAP_FLAG_NONE,&buf_desc,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&_viv_d3d12_iid_resource,(void **)&readback);

	if (FAILED(hresult))
	{
		return;
	}

	_viv_d3d12_list->lpVtbl->Reset(_viv_d3d12_list,_viv_d3d12_allocator,NULL);

	ZeroMemory(&barrier,sizeof(barrier));
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = _viv_d3d12_backbuffer[backi];
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	_viv_d3d12_list->lpVtbl->ResourceBarrier(_viv_d3d12_list,1,&barrier);

	ZeroMemory(&source,sizeof(source));
	source.pResource = _viv_d3d12_backbuffer[backi];
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	source.SubresourceIndex = 0;

	ZeroMemory(&dest,sizeof(dest));
	dest.pResource = readback;
	dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dest.PlacedFootprint.Offset = 0;
	dest.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	dest.PlacedFootprint.Footprint.Width = (UINT)_viv_d3d12_export_wide;
	dest.PlacedFootprint.Footprint.Height = (UINT)_viv_d3d12_export_high;
	dest.PlacedFootprint.Footprint.Depth = 1;
	dest.PlacedFootprint.Footprint.RowPitch = (UINT)_viv_d3d12_export_wide * 4;

	_viv_d3d12_list->lpVtbl->CopyTextureRegion(_viv_d3d12_list,&dest,0,0,0,&source,NULL);

	ZeroMemory(&barrier,sizeof(barrier));
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = _viv_d3d12_backbuffer[backi];
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	_viv_d3d12_list->lpVtbl->ResourceBarrier(_viv_d3d12_list,1,&barrier);

	_viv_d3d12_submit();
	_viv_d3d12_wait_fence(_viv_d3d12_fence_value);

	read_range.Begin = 0;
	read_range.End = 0;

	if (SUCCEEDED(readback->lpVtbl->Map(readback,0,&read_range,(void **)&map)))
	{
		int y;
		int x;

		for(y=0;y<_viv_d3d12_export_high;y++)
		{
			const BYTE *sp;
			BYTE *dp;

			sp = map + (uintptr_t)y * (uintptr_t)dest.PlacedFootprint.Footprint.RowPitch;
			dp = _viv_d3d12_export_bits + (uintptr_t)y * (uintptr_t)_viv_d3d12_export_wide * 4;

			for(x=0;x<_viv_d3d12_export_wide;x++)
			{
				dp[0] = sp[0];
				dp[1] = sp[1];
				dp[2] = sp[2];
				dp[3] = 0;
				sp += 4;
				dp += 4;
			}
		}

		readback->lpVtbl->Unmap(readback,0,&read_range);

		// the disarm: the buffer belongs to the caller (freed right
		// after the run) - a second render must never touch it.
		_viv_d3d12_export_bits = 0;
		_viv_d3d12_export_filled = 1;
	}

	readback->lpVtbl->Release(readback);
}

// the render self-check: the image draws once 1:1 into an offscreen
// target (capped at 4096 - the transient target dies before the next
// paint), five sample boxes copy back through a readback buffer and
// every sample answers against the dib's own bits. the 1:1 quad with
// the half texel offset puts the texel centers on the destination
// centers, so the linear sampler answers the exact source pixels.
static int _viv_d3d12_selfcheck(HBITMAP hbitmap,DIBSECTION *ds)
{
	ID3D12Resource *target;
	ID3D12Resource *readback;
	D3D12_HEAP_PROPERTIES heap_props;
	D3D12_RESOURCE_DESC desc;
	D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
	D3D12_GPU_DESCRIPTOR_HANDLE res_handle;
	D3D12_GPU_VIRTUAL_ADDRESS vb_addr;
	D3D12_VERTEX_BUFFER_VIEW vb_view;
	D3D12_VIEWPORT viewport;
	D3D12_RECT scissor;
	D3D12_TEXTURE_COPY_LOCATION source;
	D3D12_TEXTURE_COPY_LOCATION dest;
	D3D12_RESOURCE_BARRIER barrier;
	D3D12_RANGE read_range;
	float clear_rgba[4];
	float rect_constants[4];
	BYTE *map;
	HRESULT hresult;
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
		debug_printf("direct3d12: the render self-check skipped (size %dx%d)\r\n",wide,high);

		return 1;
	}

	stride = (int)((((SIZE_T)wide * ds->dsBm.bmBitsPixel + 31) / 32) * 4);

	target = NULL;
	readback = NULL;
	ok = 0;

	ZeroMemory(&heap_props,sizeof(heap_props));
	heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

	ZeroMemory(&desc,sizeof(desc));
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = (UINT64)wide;
	desc.Height = (UINT)high;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;

	hresult = _viv_d3d12_device->lpVtbl->CreateCommittedResource(_viv_d3d12_device,&heap_props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&_viv_d3d12_iid_resource,(void **)&target);

	if (FAILED(hresult))
	{
		return 1; // the check cannot run - the frame itself already answered, this is not a refusal
	}

	ZeroMemory(&heap_props,sizeof(heap_props));
	heap_props.Type = D3D12_HEAP_TYPE_READBACK;

	ZeroMemory(&desc,sizeof(desc));
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = (UINT64)wide * (UINT64)high * 4;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	hresult = _viv_d3d12_device->lpVtbl->CreateCommittedResource(_viv_d3d12_device,&heap_props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&_viv_d3d12_iid_resource,(void **)&readback);

	if (FAILED(hresult))
	{
		target->lpVtbl->Release(target);

		return 1;
	}

	rtv_handle = *_viv_d3d12_rtv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(_viv_d3d12_rtv_heap,&rtv_handle);
	res_handle = *_viv_d3d12_res_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(_viv_d3d12_res_heap,&res_handle);

	rect_constants[0] = -1.0f + (-0.5f / (float)wide) * 2.0f;
	rect_constants[1] = 1.0f - (-0.5f / (float)high) * 2.0f;
	rect_constants[2] = 1.0f + (-0.5f / (float)wide) * 2.0f;
	rect_constants[3] = -1.0f + (-0.5f / (float)high) * 2.0f;

	clear_rgba[0] = 0.0f;
	clear_rgba[1] = 1.0f;
	clear_rgba[2] = 0.0f;
	clear_rgba[3] = 1.0f;

	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = (float)wide;
	viewport.Height = (float)high;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	scissor.left = 0;
	scissor.top = 0;
	scissor.right = wide;
	scissor.bottom = high;

	_viv_d3d12_list->lpVtbl->Reset(_viv_d3d12_list,_viv_d3d12_allocator,_viv_d3d12_pso);

	_viv_d3d12_list->lpVtbl->RSSetViewports(_viv_d3d12_list,1,&viewport);
	_viv_d3d12_list->lpVtbl->RSSetScissorRects(_viv_d3d12_list,1,&scissor);
	_viv_d3d12_list->lpVtbl->OMSetRenderTargets(_viv_d3d12_list,1,&rtv_handle,FALSE,NULL);
	_viv_d3d12_list->lpVtbl->ClearRenderTargetView(_viv_d3d12_list,rtv_handle,clear_rgba,0,NULL);
	_viv_d3d12_list->lpVtbl->SetDescriptorHeaps(_viv_d3d12_list,1,&_viv_d3d12_res_heap);
	_viv_d3d12_list->lpVtbl->SetGraphicsRootSignature(_viv_d3d12_list,_viv_d3d12_rootsig);
	_viv_d3d12_list->lpVtbl->SetGraphicsRootDescriptorTable(_viv_d3d12_list,0,res_handle);
	_viv_d3d12_list->lpVtbl->SetGraphicsRoot32BitConstants(_viv_d3d12_list,1,4,rect_constants,0);

	vb_addr = _viv_d3d12_quad->lpVtbl->GetGPUVirtualAddress(_viv_d3d12_quad);

	vb_view.BufferLocation = vb_addr;
	vb_view.SizeInBytes = (UINT)sizeof(_viv_d3d12_quad_verts);
	vb_view.StrideInBytes = (UINT)sizeof(_viv_d3d12_vertex_t);

	_viv_d3d12_list->lpVtbl->IASetVertexBuffers(_viv_d3d12_list,0,1,&vb_view);
	_viv_d3d12_list->lpVtbl->IASetPrimitiveTopology(_viv_d3d12_list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	_viv_d3d12_list->lpVtbl->DrawInstanced(_viv_d3d12_list,4,1,0,0);

	ZeroMemory(&barrier,sizeof(barrier));
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = target;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	_viv_d3d12_list->lpVtbl->ResourceBarrier(_viv_d3d12_list,1,&barrier);

	ZeroMemory(&source,sizeof(source));
	source.pResource = target;
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	source.SubresourceIndex = 0;

	ZeroMemory(&dest,sizeof(dest));
	dest.pResource = readback;
	dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dest.PlacedFootprint.Offset = 0;
	dest.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	dest.PlacedFootprint.Footprint.Width = (UINT)wide;
	dest.PlacedFootprint.Footprint.Height = (UINT)high;
	dest.PlacedFootprint.Footprint.Depth = 1;
	dest.PlacedFootprint.Footprint.RowPitch = (UINT)wide * 4;

	_viv_d3d12_list->lpVtbl->CopyTextureRegion(_viv_d3d12_list,&dest,0,0,0,&source,NULL);

	_viv_d3d12_submit();
	_viv_d3d12_wait_fence(_viv_d3d12_fence_value);

	read_range.Begin = 0;
	read_range.End = 0;

	if (SUCCEEDED(readback->lpVtbl->Map(readback,0,&read_range,(void **)&map)))
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
			answer = map + (uintptr_t)py * (uintptr_t)dest.PlacedFootprint.Footprint.RowPitch + (uintptr_t)px * 4;

			if ((answer[0] != expect[0]) || (answer[1] != expect[1]) || (answer[2] != expect[2]))
			{
				ok = 0;

				break;
			}
		}

		readback->lpVtbl->Unmap(readback,0,&read_range);
	}

	readback->lpVtbl->Release(readback);
	target->lpVtbl->Release(target);

	return ok;
}

// the export harness hooks: arm a top-down bgra buffer before the paint,
// the next render fills it instead of presenting. the query lets the
// harness tell a refused renderer (the gdi fallback painted instead)
// from a successful readback.
int _viv_hwd3d12_export_begin(BYTE *bits,int wide,int high)
{
	_viv_d3d12_export_bits = bits;
	_viv_d3d12_export_wide = wide;
	_viv_d3d12_export_high = high;
	_viv_d3d12_export_filled = 0;

	return 1;
}

int _viv_hwd3d12_export_filled(void)
{
	return _viv_d3d12_export_filled;
}

void _viv_hwd3d12_export_end(void)
{
	_viv_d3d12_export_bits = NULL;
}
