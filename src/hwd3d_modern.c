// Direct3D 11 and Direct3D 12 share the same image shaders and upload.
// D3D12 supplies the device, queue, and swap chain through D3D11On12.
#define COBJMACROS
#include "viv.h"
#include "hwd3d_modern.h"
#include "hwd3d.h"
#include <initguid.h>
#include <dxgi1_4.h>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#define BACK_BUFFERS 2
#define RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

typedef HRESULT (WINAPI *create11_fn)(IDXGIAdapter *,D3D_DRIVER_TYPE,HMODULE,UINT,
	const D3D_FEATURE_LEVEL *,UINT,UINT,ID3D11Device **,D3D_FEATURE_LEVEL *,ID3D11DeviceContext **);
typedef HRESULT (WINAPI *create12_fn)(IUnknown *,D3D_FEATURE_LEVEL,REFIID,void **);
typedef HRESULT (WINAPI *create_on12_fn)(IUnknown *,UINT,const D3D_FEATURE_LEVEL *,UINT,
	IUnknown *const *,UINT,UINT,ID3D11Device **,ID3D11DeviceContext **,D3D_FEATURE_LEVEL *);
typedef HRESULT (WINAPI *create_factory_fn)(REFIID,void **);
typedef HRESULT (WINAPI *compile_shader_fn)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO *,
	ID3DInclude *,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob **,ID3DBlob **);

static struct
{
	int renderer;
	int warp;
	HWND hwnd;
	int wide,high;
	D3D_FEATURE_LEVEL level;
	UINT swap_count;
	ID3D11Device *device;
	ID3D11DeviceContext *context;
	IDXGIFactory1 *factory;
	IDXGIAdapter3 *budget_adapter;
	IDXGISwapChain *swap;
	IDXGISwapChain3 *swap12;
	ID3D12Device *device12;
	ID3D12CommandQueue *queue;
	ID3D12Fence *fence;
	ID3D11On12Device *on12;
	HANDLE fence_event;
	UINT64 fence_value;
	ID3D11Texture2D *wrapped[BACK_BUFFERS];
	ID3D11RenderTargetView *targets[BACK_BUFFERS];
	ID3D11Texture2D *readback_source;
	ID3D11Texture2D *texture;
	ID3D11ShaderResourceView *image;
	ID3D11VertexShader *vertex_shader;
	ID3D11PixelShader *pixel_shader;
	ID3D11SamplerState *sampler;
	ID3D11RasterizerState *raster;
	ID3D11Buffer *quad;
	HBITMAP last_bitmap;
	int image_wide,image_high;
} gpu;

static HMODULE module11,module12,module_dxgi,module_compiler;
static create11_fn create11;
static create12_fn create12;
static create_on12_fn create_on12;
static create_factory_fn create_factory;
static compile_shader_fn compile_shader;

static BYTE *export_bits;
static int export_wide,export_high,export_filled;

// the render trace (VIV_RENDER_TRACE=1): the first 128 hardware refusals
// append their back end, site and hresult to %TEMP%\viv_render_trace.log.
// the status note names the fallback, but the reason only ever lived in
// the debug_printf channel the shipping binaries do not carry - a field
// report could never say which gate fired (the rc.8 d3d question). the
// env arms the instrument and the cap keeps a wedged repaint loop from
// farming the log; seq 0 answers "write nothing".
void _viv_render_trace(int renderer,const char *site,HRESULT hresult)
{
	static int armed = -1;
	static int written = 0;
	char env_trace[8];
	char line[96];
	wchar_t path[MAX_PATH];
	DWORD length;
	HANDLE file;

	if (armed < 0)
		armed = (GetEnvironmentVariableA("VIV_RENDER_TRACE",env_trace,sizeof(env_trace)) > 0 && env_trace[0] != '0') ? 1 : 0;
	if (!armed || ++written > 128) return;
	length = GetTempPathW(MAX_PATH,path);
	if (!length || length >= MAX_PATH - 24) return;
	lstrcpyW(path + length,L"viv_render_trace.log");
	file = CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if (file == INVALID_HANDLE_VALUE) return;
	SetFilePointer(file,0,NULL,FILE_END);
	length = (DWORD)wsprintfA(line,"%s %s: %08x\r\n",
		renderer == CONFIG_RENDERER_D3D9 ? "d3d9" : renderer == CONFIG_RENDERER_D3D12 ? "d3d12" : "d3d11",
		site,(unsigned int)hresult);
	WriteFile(file,line,length,&length,NULL);
	CloseHandle(file);
}

HMODULE _viv_gpu_system_dll(const wchar_t *name)
{
	wchar_t path[MAX_PATH];
	UINT len = GetSystemDirectoryW(path,MAX_PATH);
	if (!len || len >= MAX_PATH - lstrlenW(name) - 1) return NULL;
	path[len++] = L'\\';
	lstrcpyW(path + len,name);
	return LoadLibraryW(path);
}

static int load_apis(int renderer)
{
	if (!module11)
	{
		module11 = _viv_gpu_system_dll(L"d3d11.dll");
		if (module11)
		{
			create11 = (create11_fn)GetProcAddress(module11,"D3D11CreateDevice");
			create_on12 = (create_on12_fn)GetProcAddress(module11,"D3D11On12CreateDevice");
		}
	}
	if (!module_compiler)
	{
		module_compiler = _viv_gpu_system_dll(L"d3dcompiler_47.dll");
		if (module_compiler) compile_shader = (compile_shader_fn)GetProcAddress(module_compiler,"D3DCompile");
	}
	if (renderer == CONFIG_RENDERER_D3D11) return create11 && compile_shader;

	if (!module12)
	{
		module12 = _viv_gpu_system_dll(L"d3d12.dll");
		if (module12) create12 = (create12_fn)GetProcAddress(module12,"D3D12CreateDevice");
	}
	if (!module_dxgi)
	{
		module_dxgi = _viv_gpu_system_dll(L"dxgi.dll");
		if (module_dxgi) create_factory = (create_factory_fn)GetProcAddress(module_dxgi,"CreateDXGIFactory1");
	}
	return create12 && create_on12 && create_factory && compile_shader;
}

int _viv_modern_available(int renderer)
{
	static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
	static int available[2] = {-1,-1};
	int i = renderer == CONFIG_RENDERER_D3D12;
	if (available[i] < 0)
	{
		available[i] = 0;
		if (load_apis(renderer))
		{
			HRESULT hr = i ? create12(NULL,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,NULL) :
				create11(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
					levels,ARRAYSIZE(levels),D3D11_SDK_VERSION,NULL,NULL,NULL);
			available[i] = SUCCEEDED(hr);
			if (!available[i]) _viv_render_trace(renderer,"probe",hr);
		}
		else
		{
			_viv_render_trace(renderer,"apis",E_FAIL);
		}
	}
	return available[i];
}

static HRESULT create_devices(int renderer,int warp)
{
	static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
	IDXGIDevice *dxgi = NULL;
	IDXGIDevice1 *latency = NULL;
	IDXGIAdapter *adapter = NULL;
	HRESULT hr;
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

	if (renderer == CONFIG_RENDERER_D3D12)
	{
		D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
		IUnknown *queues[1];
		IDXGIFactory4 *factory4 = NULL;
		hr = create_factory(&IID_IDXGIFactory1,(void **)&gpu.factory);
		if (SUCCEEDED(hr) && warp)
		{
			hr = IDXGIFactory1_QueryInterface(gpu.factory,&IID_IDXGIFactory4,(void **)&factory4);
			if (SUCCEEDED(hr)) hr = IDXGIFactory4_EnumWarpAdapter(factory4,&IID_IDXGIAdapter,(void **)&adapter);
			RELEASE(factory4);
		}
		if (SUCCEEDED(hr)) hr = create12((IUnknown *)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void **)&gpu.device12);
		RELEASE(adapter);
		if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandQueue(gpu.device12,&desc,&IID_ID3D12CommandQueue,(void **)&gpu.queue);
		if (SUCCEEDED(hr)) hr = ID3D12Device_CreateFence(gpu.device12,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void **)&gpu.fence);
		if (FAILED(hr)) return hr;
		gpu.fence_event = CreateEventW(NULL,FALSE,FALSE,NULL);
		if (!gpu.fence_event) return E_FAIL;

		queues[0] = (IUnknown *)gpu.queue;
		hr = create_on12((IUnknown *)gpu.device12,flags,levels,1,queues,1,0,&gpu.device,&gpu.context,&gpu.level);
		if (SUCCEEDED(hr)) hr = ID3D11Device_QueryInterface(gpu.device,&IID_ID3D11On12Device,(void **)&gpu.on12);
	}
	else
	{
		hr = create11(NULL,warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,NULL,
			flags,levels,ARRAYSIZE(levels),D3D11_SDK_VERSION,&gpu.device,&gpu.level,&gpu.context);
	}
	if (FAILED(hr)) return hr;

	hr = ID3D11Device_QueryInterface(gpu.device,&IID_IDXGIDevice,(void **)&dxgi);
	if (SUCCEEDED(hr)) hr = IDXGIDevice_GetAdapter(dxgi,&adapter);
	if (!gpu.factory)
	{
		if (SUCCEEDED(hr)) hr = IDXGIAdapter_GetParent(adapter,&IID_IDXGIFactory1,(void **)&gpu.factory);
		if (FAILED(hr))
		{
			RELEASE(adapter);
			RELEASE(dxgi);
			return hr;
		}
	}
	if (adapter && !warp)
		IDXGIAdapter_QueryInterface(adapter,&IID_IDXGIAdapter3,(void **)&gpu.budget_adapter);
	if (dxgi && SUCCEEDED(IDXGIDevice_QueryInterface(dxgi,&IID_IDXGIDevice1,(void **)&latency)))
		IDXGIDevice1_SetMaximumFrameLatency(latency,1);
	RELEASE(latency);
	RELEASE(adapter);
	RELEASE(dxgi);
	return S_OK;
}

static HRESULT create_pipeline(void)
{
	static const char shader[] =
		"cbuffer Quad : register(b0) { float4 corners; };"
		"struct V { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };"
		"V vs(uint id : SV_VertexID) { V o; o.uv=float2(id&1,id>>1);"
		"o.pos=float4(lerp(corners.xy,corners.zw,o.uv),0,1); return o; }"
		"Texture2D image : register(t0); SamplerState filter0 : register(s0);"
		"float4 ps(V i) : SV_TARGET { return float4(image.Sample(filter0,i.uv).rgb,1); }";
	D3D11_SAMPLER_DESC sampler = {0};
	D3D11_RASTERIZER_DESC raster = {0};
	D3D11_BUFFER_DESC buffer = {0};
	ID3DBlob *blob = NULL;
	HRESULT hr = compile_shader(shader,sizeof(shader)-1,NULL,NULL,NULL,"vs","vs_4_0",0,0,&blob,NULL);
	if (SUCCEEDED(hr)) hr = ID3D11Device_CreateVertexShader(gpu.device,
		ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),NULL,&gpu.vertex_shader);
	RELEASE(blob);
	if (FAILED(hr)) return hr;

	hr = compile_shader(shader,sizeof(shader)-1,NULL,NULL,NULL,"ps","ps_4_0",0,0,&blob,NULL);
	if (SUCCEEDED(hr)) hr = ID3D11Device_CreatePixelShader(gpu.device,
		ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),NULL,&gpu.pixel_shader);
	RELEASE(blob);
	if (FAILED(hr)) return hr;

	sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler.MaxAnisotropy = 1;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	raster.FillMode = D3D11_FILL_SOLID;
	raster.CullMode = D3D11_CULL_NONE;
	raster.DepthClipEnable = TRUE;
	buffer.ByteWidth = sizeof(float) * 4;
	buffer.Usage = D3D11_USAGE_DEFAULT;
	buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hr = ID3D11Device_CreateSamplerState(gpu.device,&sampler,&gpu.sampler);
	if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRasterizerState(gpu.device,&raster,&gpu.raster);
	if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(gpu.device,&buffer,NULL,&gpu.quad);
	return hr;
}

static HRESULT wait_for_queue(void)
{
	UINT64 value;
	HRESULT hr;
	if (!gpu.queue || !gpu.fence) return S_OK;
	value = ++gpu.fence_value;
	hr = ID3D12CommandQueue_Signal(gpu.queue,gpu.fence,value);
	if (FAILED(hr)) return hr;
	if (ID3D12Fence_GetCompletedValue(gpu.fence) >= value) return ID3D12Device_GetDeviceRemovedReason(gpu.device12);
	hr = ID3D12Fence_SetEventOnCompletion(gpu.fence,value,gpu.fence_event);
	if (FAILED(hr)) return hr;
	return WaitForSingleObject(gpu.fence_event,1500) == WAIT_OBJECT_0 ? S_OK : E_FAIL;
}

static HRESULT release_targets(void)
{
	unsigned int i;
	if (gpu.context) ID3D11DeviceContext_OMSetRenderTargets(gpu.context,0,NULL,NULL);
	for (i = 0; i < BACK_BUFFERS; i++)
	{
		RELEASE(gpu.targets[i]);
		RELEASE(gpu.wrapped[i]);
	}
	RELEASE(gpu.readback_source);
	if (gpu.context) ID3D11DeviceContext_Flush(gpu.context);
	return wait_for_queue();
}

void _viv_modern_release_image(void)
{
	if (gpu.context)
	{
		ID3D11ShaderResourceView *none = NULL;
		ID3D11DeviceContext_PSSetShaderResources(gpu.context,0,1,&none);
	}
	RELEASE(gpu.image);
	RELEASE(gpu.texture);
	gpu.last_bitmap = NULL;
	gpu.image_wide = gpu.image_high = 0;
}

void _viv_modern_shutdown(void)
{
	_viv_modern_release_image();
	release_targets();
	if (gpu.context)
	{
		ID3D11DeviceContext_ClearState(gpu.context);
		ID3D11DeviceContext_Flush(gpu.context);
	}
	RELEASE(gpu.swap12);
	RELEASE(gpu.swap);
	RELEASE(gpu.quad);
	RELEASE(gpu.sampler);
	RELEASE(gpu.raster);
	RELEASE(gpu.vertex_shader);
	RELEASE(gpu.pixel_shader);
	RELEASE(gpu.budget_adapter);
	RELEASE(gpu.factory);
	RELEASE(gpu.on12);
	RELEASE(gpu.context);
	RELEASE(gpu.device);
	RELEASE(gpu.fence);
	RELEASE(gpu.queue);
	RELEASE(gpu.device12);
	if (gpu.fence_event) CloseHandle(gpu.fence_event);
	ZeroMemory(&gpu,sizeof(gpu));
}

static HRESULT create_swap(HWND hwnd,int wide,int high)
{
	IDXGIFactory2 *factory2 = NULL;
	IDXGISwapChain1 *swap1 = NULL;
	HRESULT hr = IDXGIFactory1_QueryInterface(gpu.factory,&IID_IDXGIFactory2,(void **)&factory2);
	gpu.swap_count = BACK_BUFFERS;
	if (SUCCEEDED(hr))
	{
		DXGI_SWAP_CHAIN_DESC1 desc = {0};
		desc.Width = (UINT)wide;
		desc.Height = (UINT)high;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = gpu.swap_count;
		desc.SwapEffect = gpu.on12 ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		hr = IDXGIFactory2_CreateSwapChainForHwnd(factory2,
			gpu.on12 ? (IUnknown *)gpu.queue : (IUnknown *)gpu.device,hwnd,&desc,NULL,NULL,&swap1);
		if (SUCCEEDED(hr)) hr = IDXGISwapChain1_QueryInterface(swap1,&IID_IDXGISwapChain,(void **)&gpu.swap);
		if (SUCCEEDED(hr) && gpu.on12) hr = IDXGISwapChain1_QueryInterface(swap1,&IID_IDXGISwapChain3,(void **)&gpu.swap12);
		RELEASE(swap1);
		RELEASE(factory2);
	}
	if (FAILED(hr) && !gpu.on12)
	{
		DXGI_SWAP_CHAIN_DESC desc = {0};
		gpu.swap_count = 1;
		desc.BufferDesc.Width = (UINT)wide;
		desc.BufferDesc.Height = (UINT)high;
		desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = gpu.swap_count;
		desc.OutputWindow = hwnd;
		desc.Windowed = TRUE;
		desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		hr = IDXGIFactory1_CreateSwapChain(gpu.factory,(IUnknown *)gpu.device,&desc,&gpu.swap);
	}
	if (SUCCEEDED(hr))
		IDXGIFactory1_MakeWindowAssociation(gpu.factory,GetAncestor(hwnd,GA_ROOT),DXGI_MWA_NO_ALT_ENTER);
	return hr;
}

static void texture_desc(D3D11_TEXTURE2D_DESC *desc,int wide,int high)
{
	ZeroMemory(desc,sizeof(*desc));
	desc->Width = (UINT)wide;
	desc->Height = (UINT)high;
	desc->MipLevels = desc->ArraySize = desc->SampleDesc.Count = 1;
	desc->Format = DXGI_FORMAT_B8G8R8A8_UNORM;
}

static HRESULT create_targets(HWND hwnd,int wide,int high)
{
	HRESULT hr;
	unsigned int i;
	if (gpu.targets[0] && gpu.wide == wide && gpu.high == high) return S_OK;
	hr = release_targets();
	if (FAILED(hr)) return hr;

	if (export_bits)
	{
		D3D11_TEXTURE2D_DESC desc;
		texture_desc(&desc,wide,high);
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		hr = ID3D11Device_CreateTexture2D(gpu.device,&desc,NULL,&gpu.readback_source);
		if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(gpu.device,
			(ID3D11Resource *)gpu.readback_source,NULL,&gpu.targets[0]);
	}
	else
	{
		hr = gpu.swap ? IDXGISwapChain_ResizeBuffers(gpu.swap,gpu.swap_count,wide,high,DXGI_FORMAT_UNKNOWN,0) : create_swap(hwnd,wide,high);
		for (i = 0; SUCCEEDED(hr) && i < (gpu.swap12 ? gpu.swap_count : 1); i++)
		{
			ID3D11Texture2D *buffer = NULL;
			if (gpu.swap12)
			{
				ID3D12Resource *surface = NULL;
				D3D11_RESOURCE_FLAGS flags = {D3D11_BIND_RENDER_TARGET};
				hr = IDXGISwapChain3_GetBuffer(gpu.swap12,i,&IID_ID3D12Resource,(void **)&surface);
				if (SUCCEEDED(hr)) hr = ID3D11On12Device_CreateWrappedResource(gpu.on12,(IUnknown *)surface,&flags,
					D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT,
					&IID_ID3D11Texture2D,(void **)&gpu.wrapped[i]);
				RELEASE(surface);
				buffer = gpu.wrapped[i];
			}
			else
			{
				hr = IDXGISwapChain_GetBuffer(gpu.swap,0,&IID_ID3D11Texture2D,(void **)&buffer);
			}
			if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(gpu.device,(ID3D11Resource *)buffer,NULL,&gpu.targets[i]);
			if (!gpu.swap12) RELEASE(buffer);
		}
	}
	if (SUCCEEDED(hr))
	{
		gpu.wide = wide;
		gpu.high = high;
	}
	return hr;
}

static int within_adapter_budget(SIZE_T bytes)
{
	DXGI_QUERY_VIDEO_MEMORY_INFO memory;
	if (!gpu.budget_adapter || FAILED(IDXGIAdapter3_QueryVideoMemoryInfo(gpu.budget_adapter,0,
		DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory)) || !memory.Budget) return 1;
	return memory.CurrentUsage < memory.Budget && (UINT64)bytes <= memory.Budget - memory.CurrentUsage;
}

static HRESULT upload_image(HBITMAP bitmap,const DIBSECTION *dib,SIZE_T bytes)
{
	const BITMAP *bm = &dib->dsBm;
	D3D11_MAPPED_SUBRESOURCE map;
	HRESULT hr;
	int y,wide = bm->bmWidth,high = bm->bmHeight;
	int bpp = bm->bmBitsPixel / 8;
	if (bitmap == gpu.last_bitmap) return S_OK;

	if (!gpu.texture || wide != gpu.image_wide || high != gpu.image_high)
	{
		D3D11_TEXTURE2D_DESC desc;
		_viv_modern_release_image();
		if (!within_adapter_budget(bytes)) return E_OUTOFMEMORY;
		texture_desc(&desc,wide,high);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		hr = ID3D11Device_CreateTexture2D(gpu.device,&desc,NULL,&gpu.texture);
		if (SUCCEEDED(hr)) hr = ID3D11Device_CreateShaderResourceView(gpu.device,(ID3D11Resource *)gpu.texture,NULL,&gpu.image);
		if (FAILED(hr)) return hr;
		gpu.image_wide = wide;
		gpu.image_high = high;
	}

	if (!GdiFlush()) return E_FAIL;
	hr = ID3D11DeviceContext_Map(gpu.context,(ID3D11Resource *)gpu.texture,0,D3D11_MAP_WRITE_DISCARD,0,&map);
	if (FAILED(hr)) return hr;
	for (y = 0; y < high; y++)
	{
		// The loader and clipboard normalize every displayed DIB to top-down.
		const BYTE *src = (const BYTE *)bm->bmBits + (SIZE_T)y * bm->bmWidthBytes;
		BYTE *dst = (BYTE *)map.pData + (SIZE_T)y * map.RowPitch;
		if (bpp == 4)
		{
			memcpy(dst,src,(SIZE_T)wide * 4);
		}
		else
		{
			int x;
			for (x = 0; x < wide; x++,src += 3,dst += 4)
			{
				dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255;
			}
		}
	}
	ID3D11DeviceContext_Unmap(gpu.context,(ID3D11Resource *)gpu.texture,0);
	gpu.last_bitmap = bitmap;
	return S_OK;
}

static HRESULT read_pixels(void)
{
	D3D11_TEXTURE2D_DESC desc;
	D3D11_MAPPED_SUBRESOURCE map;
	ID3D11Texture2D *staging = NULL;
	HRESULT hr;
	int y,x;
	texture_desc(&desc,export_wide,export_high);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	hr = ID3D11Device_CreateTexture2D(gpu.device,&desc,NULL,&staging);
	if (FAILED(hr)) return hr;
	ID3D11DeviceContext_CopyResource(gpu.context,(ID3D11Resource *)staging,(ID3D11Resource *)gpu.readback_source);
	hr = ID3D11DeviceContext_Map(gpu.context,(ID3D11Resource *)staging,0,D3D11_MAP_READ,0,&map);
	if (SUCCEEDED(hr))
	{
		for (y = 0; y < export_high; y++)
		{
			BYTE *row = export_bits + (SIZE_T)y * export_wide * 4;
			memcpy(row,(BYTE *)map.pData + (SIZE_T)y * map.RowPitch,(SIZE_T)export_wide * 4);
			for (x = 0; x < export_wide; x++) row[x * 4 + 3] = 0;
		}
		ID3D11DeviceContext_Unmap(gpu.context,(ID3D11Resource *)staging,0);
		export_filled = 1;
	}
	RELEASE(staging);
	return hr;
}

int _viv_modern_render(int renderer,int warp,HWND hwnd,HBITMAP bitmap,int wide,int high,
	int x,int y,int dst_wide,int dst_high,COLORREF color)
{
	DIBSECTION dib;
	D3D11_VIEWPORT viewport = {0};
	ID3D11Resource *wrapped = NULL;
	UINT index = 0;
	HRESULT hr = E_FAIL;
	SIZE_T texture_bytes;
	float quad[4], clear[4];
	int max_texture;

	if (wide <= 0 || high <= 0 || dst_wide <= 0 || dst_high <= 0 ||
		GetObjectW(bitmap,sizeof(dib),&dib) != sizeof(dib) || !dib.dsBm.bmBits ||
		dib.dsBm.bmWidth <= 0 || dib.dsBm.bmHeight <= 0 ||
		(dib.dsBm.bmBitsPixel != 24 && dib.dsBm.bmBitsPixel != 32) ||
		dib.dsBm.bmWidthBytes <= 0 ||
		(SIZE_T)dib.dsBm.bmWidthBytes < safe_size_mul((SIZE_T)dib.dsBm.bmWidth,dib.dsBm.bmBitsPixel/8))
	{
		_viv_render_trace(renderer,"frame-contract",E_FAIL);
		return 0;
	}

	texture_bytes = safe_size_mul(safe_size_mul((SIZE_T)dib.dsBm.bmWidth,dib.dsBm.bmHeight),8);
	// Allow one upload rename plus two swap/readback surfaces.
	if (safe_size_add(texture_bytes,safe_size_mul(safe_size_mul((SIZE_T)wide,high),8)) > VIV_MAX_HARDWARE_BYTES)
	{
		_viv_render_trace(renderer,"byte-budget",E_FAIL);
		return 0;
	}

	if (!gpu.device || renderer != gpu.renderer || warp != gpu.warp || hwnd != gpu.hwnd)
	{
		_viv_modern_shutdown();
		if (!load_apis(renderer))
		{
			_viv_render_trace(renderer,"apis",E_FAIL);
			return 0;
		}
		hr = create_devices(renderer,warp);
		if (FAILED(hr))
		{
			_viv_render_trace(renderer,"create-device",hr);
			goto failed;
		}
		hr = create_pipeline();
		if (FAILED(hr))
		{
			_viv_render_trace(renderer,"pipeline",hr);
			goto failed;
		}
		gpu.renderer = renderer;
		gpu.warp = warp;
		gpu.hwnd = hwnd;
	}

	max_texture = gpu.level >= D3D_FEATURE_LEVEL_11_0 ? 16384 : 8192;
	if (dib.dsBm.bmWidth > max_texture || dib.dsBm.bmHeight > max_texture)
	{
		_viv_render_trace(renderer,"max-texture",E_FAIL);
		return 0;
	}
	hr = create_targets(hwnd,wide,high);
	if (FAILED(hr))
	{
		_viv_render_trace(renderer,"targets",hr);
		goto failed;
	}
	hr = upload_image(bitmap,&dib,texture_bytes);
	if (FAILED(hr))
	{
		_viv_render_trace(renderer,"upload",hr);
		goto failed;
	}

	if (gpu.swap12)
	{
		index = IDXGISwapChain3_GetCurrentBackBufferIndex(gpu.swap12);
		if (index >= gpu.swap_count) { hr = E_FAIL; _viv_render_trace(renderer,"backbuffer-index",hr); goto failed; }
		wrapped = (ID3D11Resource *)gpu.wrapped[index];
		ID3D11On12Device_AcquireWrappedResources(gpu.on12,&wrapped,1);
	}

	quad[0] = 2.0f * (float)x / wide - 1.0f;
	quad[1] = 1.0f - 2.0f * (float)y / high;
	quad[2] = 2.0f * ((float)x + dst_wide) / wide - 1.0f;
	quad[3] = 1.0f - 2.0f * ((float)y + dst_high) / high;
	clear[0] = GetRValue(color) / 255.0f;
	clear[1] = GetGValue(color) / 255.0f;
	clear[2] = GetBValue(color) / 255.0f;
	clear[3] = 1.0f;
	viewport.Width = (float)wide;
	viewport.Height = (float)high;
	viewport.MaxDepth = 1.0f;
	ID3D11DeviceContext_OMSetRenderTargets(gpu.context,1,&gpu.targets[index],NULL);
	ID3D11DeviceContext_ClearRenderTargetView(gpu.context,gpu.targets[index],clear);
	ID3D11DeviceContext_RSSetState(gpu.context,gpu.raster);
	ID3D11DeviceContext_RSSetViewports(gpu.context,1,&viewport);
	ID3D11DeviceContext_IASetPrimitiveTopology(gpu.context,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	ID3D11DeviceContext_UpdateSubresource(gpu.context,(ID3D11Resource *)gpu.quad,0,NULL,quad,0,0);
	ID3D11DeviceContext_VSSetShader(gpu.context,gpu.vertex_shader,NULL,0);
	ID3D11DeviceContext_VSSetConstantBuffers(gpu.context,0,1,&gpu.quad);
	ID3D11DeviceContext_PSSetShader(gpu.context,gpu.pixel_shader,NULL,0);
	ID3D11DeviceContext_PSSetSamplers(gpu.context,0,1,&gpu.sampler);
	ID3D11DeviceContext_PSSetShaderResources(gpu.context,0,1,&gpu.image);
	ID3D11DeviceContext_Draw(gpu.context,4,0);
	if (wrapped)
	{
		ID3D11DeviceContext_OMSetRenderTargets(gpu.context,0,NULL,NULL);
		ID3D11On12Device_ReleaseWrappedResources(gpu.on12,&wrapped,1);
		ID3D11DeviceContext_Flush(gpu.context);
	}
	hr = export_bits ? read_pixels() : IDXGISwapChain_Present(gpu.swap,1,0);
	if (SUCCEEDED(hr)) return 1;
	_viv_render_trace(renderer,export_bits ? "readback" : "present",hr);

failed:
	debug_printf("Direct3D %d render failed: %08x\r\n",renderer == CONFIG_RENDERER_D3D12 ? 12 : 11,(unsigned int)hr);
	_viv_modern_shutdown();
	return 0;
}

void _viv_modern_export_begin(BYTE *bits,int wide,int high)
{
	export_bits = bits;
	export_wide = wide;
	export_high = high;
	export_filled = 0;
}

int _viv_modern_export_filled(void)
{
	return export_filled;
}

void _viv_modern_export_end(void)
{
	export_bits = NULL;
}

static HWND viewport_window;
static int viewport_top,viewport_wide,viewport_high;
static int export_active,export_warp,viewport_warp;

void _viv_gpu_enable_warp(void)
{
	viewport_warp = 1;
}

int _viv_gpu_renderer_available(int renderer)
{
	switch (renderer)
	{
		case CONFIG_RENDERER_GDI: return 1;
		case CONFIG_RENDERER_D3D9: return _viv_hwd3d_available();
		case CONFIG_RENDERER_D3D11:
		case CONFIG_RENDERER_D3D12: return _viv_modern_available(renderer);
	}
	return 0;
}

int _viv_gpu_preferred(void)
{
	if (_viv_modern_available(CONFIG_RENDERER_D3D11)) return CONFIG_RENDERER_D3D11;
	if (_viv_hwd3d_available()) return CONFIG_RENDERER_D3D9;
	return CONFIG_RENDERER_GDI;
}

static LRESULT CALLBACK viewport_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
	if (msg == WM_NCHITTEST) return HTTRANSPARENT;
	if (msg == WM_ERASEBKGND) return 1;
	if (msg == WM_NCDESTROY && hwnd == viewport_window) viewport_window = NULL;
	return DefWindowProcW(hwnd,msg,wp,lp);
}

static HWND viewport(HWND parent,int wide,int high,int top)
{
	static const wchar_t class_name[] = L"VIV.D3D.Viewport";
	if (!viewport_window || GetParent(viewport_window) != parent)
	{
		WNDCLASSW wc = {0};
		wc.lpfnWndProc = viewport_proc;
		wc.hInstance = os_hinstance;
		wc.lpszClassName = class_name;
		if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			return NULL;
		viewport_window = CreateWindowExW(WS_EX_NOPARENTNOTIFY,class_name,L"",
			WS_CHILD | WS_DISABLED | WS_CLIPSIBLINGS,0,0,0,0,parent,NULL,os_hinstance,NULL);
		if (!viewport_window) return NULL;
		viewport_wide = 0;
	}
	if (viewport_wide != wide || viewport_high != high || viewport_top != top || !IsWindowVisible(viewport_window))
	{
		if (!SetWindowPos(viewport_window,HWND_BOTTOM,0,top,wide,high,SWP_NOACTIVATE | SWP_SHOWWINDOW))
			return NULL;
		viewport_wide = wide;
		viewport_high = high;
		viewport_top = top;
	}
	return viewport_window;
}

int _viv_gpu_render(int renderer,HWND parent,HDC hdc,HBITMAP bitmap,int wide,int high,
	int top,int x,int y,int dst_wide,int dst_high,COLORREF color)
{
	HWND target = parent;
	if (!export_active)
	{
		target = viewport(parent,wide,high,top);
		if (!target)
		{
			_viv_render_trace(renderer,"viewport",E_FAIL);
			return 0;
		}
	}
	if (renderer == CONFIG_RENDERER_D3D9)
		return _viv_hwd3d_render(target,hdc,bitmap,x,y,dst_wide,dst_high,color);
	return _viv_modern_render(renderer,export_active ? export_warp : viewport_warp,target,bitmap,wide,high,x,y,dst_wide,dst_high,color);
}

void _viv_gpu_release_image(void)
{
	_viv_hwd3d_release_image();
	_viv_modern_release_image();
	if (viewport_window) ShowWindow(viewport_window,SW_HIDE);
}

void _viv_gpu_shutdown(void)
{
	_viv_hwd3d_shutdown();
	_viv_modern_shutdown();
	if (viewport_window) DestroyWindow(viewport_window);
	viewport_window = NULL;
}

void _viv_gpu_export_begin(int renderer,int warp,BYTE *bits,int wide,int high)
{
	export_active = 1;
	export_warp = warp;
	if (renderer == CONFIG_RENDERER_D3D9) _viv_hwd3d_export_begin(bits,wide,high);
	else _viv_modern_export_begin(bits,wide,high);
}

int _viv_gpu_export_filled(int renderer)
{
	return renderer == CONFIG_RENDERER_D3D9 ? _viv_hwd3d_export_filled() : _viv_modern_export_filled();
}

void _viv_gpu_export_end(void)
{
	export_active = 0;
	export_warp = 0;
	_viv_hwd3d_export_end();
	_viv_modern_export_end();
}
