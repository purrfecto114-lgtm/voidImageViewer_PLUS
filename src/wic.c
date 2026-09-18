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
// wic layer - the windows imaging component fallback.
// gdi+ answers the classic formats (bmp, gif, jpeg, png, tiff, emf, wmf,
// ico) and libwebp answers webp; this layer catches what both decline:
// jpeg-xr (wdp, hdp, jxr - the win7+ wmp codec), dds (bc1-3, win8.1+) and
// heif/avif wherever the os carries the store codec extensions. the decode
// happens inside windowscodecs.dll, so nothing static rides the exe; on a
// system with no imaging factory registered the class simply refuses to
// create and the file fails like any unloadable file.

#include "viv.h"
#include "viv_state.h"
#include <wincodec.h>

static int _pixel_budget_refused(SIZE_T pixels,SIZE_T ceiling)
{
	if (pixels > ceiling)
	{
		debug_printf("pixel budget: refusing a %u mp canvas (ceiling %u mp)\r\n",(unsigned int)(pixels / 1000000),(unsigned int)(ceiling / 1000000));
		
		_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);
		
		return 1;
	}
	
	// the working-set gate (the viv_load.c twin): the load holds the
	// decode canvas, the display dib and the renderer staging at once -
	// 12 bytes per pixel priced against the byte ceiling.
	if ((VIV_UINT64)pixels * VIV_IMAGE_WORKING_SET_BYTES_PER_PIXEL > VIV_MAX_IMAGE_BYTES)
	{
		debug_printf("working set budget: refusing a %u mp canvas (%u mb estimated, ceiling %u mb)\r\n",(unsigned int)(pixels / 1000000),(unsigned int)(((VIV_UINT64)pixels * VIV_IMAGE_WORKING_SET_BYTES_PER_PIXEL) / 1000000),(unsigned int)(VIV_MAX_IMAGE_BYTES / 1000000));
		
		_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);
		
		return 1;
	}
	
	return 0;
}

// self-defined guids (the os.c file-dialog precedent): the canonical bytes
// come from the platform sdk's wincodec.h, spelled out here so neither
// build line needs a uuid library to resolve them.
// {cacaf262-9370-4615-a13b-9f5539da4c0a} CLSID_WICImagingFactory
static const GUID _wic_clsid_imaging_factory = {0xcacaf262,0x9370,0x4615,{0xa1,0x3b,0x9f,0x55,0x39,0xda,0x4c,0x0a}};
// {ec5ec8a9-c395-4314-9c77-54d7a935ff70} IID_IWICImagingFactory
static const GUID _wic_iid_imaging_factory = {0xec5ec8a9,0xc395,0x4314,{0x9c,0x77,0x54,0xd7,0xa9,0x35,0xff,0x70}};
// {6fddc324-4e03-4bfe-b185-3d77768dc90f} GUID_WICPixelFormat32bppBGRA
static const GUID _wic_pixel_format_32bpp_bgra = {0x6fddc324,0x4e03,0x4bfe,{0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x0f}};

int wic_load(IStream *stream,void *user_data,int (*info_callback)(void *user_data,DWORD frame_count,DWORD wide,DWORD high,int has_alpha),int (*frame_callback)(void *user_data,BYTE *pixels,int delay))
{
	int ret;
	IWICImagingFactory *factory;
	
	ret = 0;
	factory = NULL;
	
	// com is already initialized on the load thread. a single frame serves
	// this layer: the formats wic uniquely adds (jpeg-xr, dds, heif, avif)
	// are stills, and the multi-frame containers already answer through
	// gdi+.
	// the loader's stage marker: the exit timeout names the decoder that
	// was running when the wait expired.
	_viv_load_stage = "wic";

	if (SUCCEEDED(CoCreateInstance(&_wic_clsid_imaging_factory,NULL,CLSCTX_INPROC_SERVER,&_wic_iid_imaging_factory,(void **)&factory)))
	{
		IWICBitmapDecoder *decoder;
		LARGE_INTEGER zero;
		
		decoder = NULL;
		
		// gdi+ may have left the read head anywhere on its failure path; the
		// wic decoder sniffs from the current position, so the head goes back
		// to the start of the image first.
		zero.QuadPart = 0;
		stream->lpVtbl->Seek(stream,zero,STREAM_SEEK_SET,NULL);
		
		if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromStream(factory,stream,NULL,WICDecodeMetadataCacheOnDemand,&decoder)))
		{
			UINT frame_count;
			
			frame_count = 0;
			
			if ((SUCCEEDED(IWICBitmapDecoder_GetFrameCount(decoder,&frame_count))) && (frame_count))
			{
				IWICBitmapFrameDecode *frame;
				
				frame = NULL;
				
				if (SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder,0,&frame)))
				{
					UINT wide;
					UINT high;
					
					wide = 0;
					high = 0;
					
					if ((SUCCEEDED(IWICBitmapFrameDecode_GetSize(frame,&wide,&high))) && (wide) && (high))
					{
						// pixel budget: refuse a hostile canvas before wic allocates
						// the converter or the copy buffer.
						if (!_pixel_budget_refused(safe_size_mul((SIZE_T)wide,(SIZE_T)high),VIV_MAX_IMAGE_PIXELS))
						{
							SIZE_T buffer_size;
							
							buffer_size = safe_size_mul(safe_size_mul((SIZE_T)wide,(SIZE_T)high),4);
							
							if (buffer_size)
							{
								IWICFormatConverter *converter;
								
								converter = NULL;
								
								if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory,&converter)))
								{
									// 32bpp bgra is the one pixel format every wic codec can
									// convert to; the delivery callbacks want rgba, so the
									// channels swap in the copy pass below.
									if (SUCCEEDED(IWICFormatConverter_Initialize(converter,(IWICBitmapSource *)frame,&_wic_pixel_format_32bpp_bgra,WICBitmapDitherTypeNone,NULL,0.0,WICBitmapPaletteTypeCustom)))
									{
										BYTE *pixels;
										
										pixels = (BYTE *)mem_alloc(buffer_size);
										
										if (pixels)
										{
											// the copy is a full-image read (the rc is null); the
											// stride of 32bpp is wide * 4.
											if (SUCCEEDED(IWICBitmapSource_CopyPixels((IWICBitmapSource *)converter,NULL,(UINT)(wide * 4),(UINT)buffer_size,pixels)))
											{
												int has_alpha;
												BYTE *p;
												BYTE *end;
												
												has_alpha = 0;
												p = pixels;
												end = pixels + buffer_size;
												
												// bgra to rgba in place, with the alpha scan riding
												// the same pass: a fully opaque canvas skips the
												// backdrop blend in the delivery.
												while (p < end)
												{
													BYTE temp;
													
													if (p[3] != 255)
													{
														has_alpha = 1;
													}
													
													temp = p[0];
													p[0] = p[2];
													p[2] = temp;
													
													p += 4;
												}
												
												if (info_callback(user_data,1,wide,high,has_alpha))
												{
													if (frame_callback(user_data,pixels,0))
													{
														ret = 1;
													}
												}
											}
											
											mem_free(pixels);
										}
									}
									
										IWICFormatConverter_Release(converter);
								}
							}
						}
					}
					
					IWICBitmapFrameDecode_Release(frame);
				}
			}
			
			IWICBitmapDecoder_Release(decoder);
		}
		
		IWICImagingFactory_Release(factory);
	}
	
	return ret;
}
