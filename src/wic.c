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

// the frame-array budget (the webp.c twin, standing beside this layer's
// own pixel budget for the multi-frame delivery): every decoded frame is
// held at once, so the count and the total frame bytes carry their own
// ceilings beyond the canvas gate. on refusal the load fails like any
// unloadable file.
static int _animation_budget_refused(DWORD frame_count,SIZE_T canvas_pixels)
{
		if (frame_count > VIV_MAX_ANIMATION_FRAMES)
		{
				debug_printf("animation budget: refusing %u frames (ceiling %u)\r\n",(unsigned int)frame_count,(unsigned int)VIV_MAX_ANIMATION_FRAMES);
				
				_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);
				
				return 1;
		}
		
		// 16/3 bytes per canvas pixel per frame: the 32bpp DIB frames the
		// loader holds plus the mipmap chain's extra third the lazy build
		// still fills in as the animation plays.
		if ((VIV_UINT64)frame_count * (VIV_UINT64)canvas_pixels * 16 / 3 > VIV_MAX_ANIMATION_TOTAL_BYTES)
		{
				debug_printf("animation budget: refusing %u frames of a %u mp canvas (%u mb of frames, ceiling %u mb)\r\n",(unsigned int)frame_count,(unsigned int)(canvas_pixels / 1000000),(unsigned int)(((VIV_UINT64)frame_count * (VIV_UINT64)canvas_pixels * 16 / 3) / 1000000),(unsigned int)(VIV_MAX_ANIMATION_TOTAL_BYTES / 1000000));
				
				_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);
				
				return 1;
		}
		
		return 0;
}

// decode one wic frame into the shared rgba buffer (the copy lands bgra;
// the swap pass turns it rgba in place). the alpha scan rides only the
// first frame's pass - the info callback's has_alpha flag is
// canvas-level, exactly like the webp path's features flag - later
// frames pass NULL and only swap channels.
static int _wic_frame_to_rgba(IWICImagingFactory *factory,IWICBitmapDecoder *decoder,UINT frame_index,BYTE *pixels,SIZE_T buffer_size,UINT wide,UINT high,int *has_alpha)
{
		IWICBitmapFrameDecode *frame;
		
		frame = NULL;
		
		if (SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder,frame_index,&frame)))
		{
				IWICFormatConverter *converter;
				
				converter = NULL;
				
				if (SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory,&converter)))
				{
					// 32bpp bgra is the one pixel format every wic codec can
					// convert to; the delivery callbacks want rgba, so the
					// channels swap in the pass below.
					if (SUCCEEDED(IWICFormatConverter_Initialize(converter,(IWICBitmapSource *)frame,&_wic_pixel_format_32bpp_bgra,WICBitmapDitherTypeNone,NULL,0.0,WICBitmapPaletteTypeCustom)))
					{
						// the copy is a full-image read (the rc is null); the
						// stride of 32bpp is wide * 4.
						if (SUCCEEDED(IWICBitmapSource_CopyPixels((IWICBitmapSource *)converter,NULL,(UINT)(wide * 4),(UINT)buffer_size,pixels)))
						{
							BYTE *p;
							BYTE *end;
							
							p = pixels;
							end = pixels + buffer_size;
							
							while (p < end)
							{
								BYTE temp;
								
								if ((has_alpha) && (p[3] != 255))
								{
										*has_alpha = 1;
									}
								
								temp = p[0];
								p[0] = p[2];
								p[2] = temp;
								
								p += 4;
							}
							
							IWICFormatConverter_Release(converter);
							IWICBitmapFrameDecode_Release(frame);
							
							return 1;
						}
					}
					
					IWICFormatConverter_Release(converter);
				}
				
				IWICBitmapFrameDecode_Release(frame);
		}
		
		return 0;
}

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
	InterlockedExchangePointer(&_viv_load_stage,(PVOID)"wic");

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
						SIZE_T canvas_pixels;
						int budget_ok;
						
						canvas_pixels = safe_size_mul((SIZE_T)wide,(SIZE_T)high);
						budget_ok = 0;
						
						// pixel budget: refuse a hostile canvas before wic allocates
						// the converter or the copy buffer. a multi-frame file answers
						// the stricter animation ceilings instead - the same pair the
						// webp path runs (the animation canvas plus the frame-array
						// budget), so a thousand-frame heif sequence cannot ride the
						// still limits. the merged report's confirmed finding: the
						// system codecs can report sequences, and the old code
						// flattened every one of them into a frame 0 still.
						if (frame_count == 1)
						{
							budget_ok = !_pixel_budget_refused(canvas_pixels,VIV_MAX_IMAGE_PIXELS);
						}
						else
						{
							if (!_pixel_budget_refused(canvas_pixels,VIV_MAX_ANIMATION_PIXELS))
							{
								budget_ok = !_animation_budget_refused(frame_count,canvas_pixels);
							}
						}
						
						if (budget_ok)
						{
							SIZE_T buffer_size;
							
							buffer_size = safe_size_mul(canvas_pixels,4);
							
							if (buffer_size)
							{
								BYTE *pixels;
								
								pixels = (BYTE *)mem_alloc(buffer_size);
								
								if (pixels)
								{
									int has_alpha;
									UINT i;
									
									has_alpha = 0;
									
									// frame 0 answers the canvas flag (the webp path's
									// features flag is canvas-level the same way).
									if (_wic_frame_to_rgba(factory,decoder,0,pixels,buffer_size,wide,high,&has_alpha))
									{
										// the multi-frame contract: the old code read frame 0
										// and dropped the rest - a still of an animation. the
										// info callback now declares the real count and every
										// frame rides the same generic delivery the webp path
										// uses; the animation machinery downstream loads,
										// times and plays them. frame timing lives in
										// container metadata this layer does not parse, so
										// the frames carry the uniform 100ms gif-default
										// delay - an honest stand-in, spelled here.
										if (info_callback(user_data,frame_count,wide,high,has_alpha))
										{
											DWORD delay;
											
											delay = (frame_count > 1) ? 100 : 0;
											
											if (frame_callback(user_data,pixels,delay))
											{
												ret = 1;
												
												for(i=1;i<frame_count;i++)
												{
													if (!_wic_frame_to_rgba(factory,decoder,i,pixels,buffer_size,wide,high,NULL))
													{
														break;
													}
													
													if (!frame_callback(user_data,pixels,delay))
													{
														break;
													}
												}
											}
										}
									}
									
									mem_free(pixels);
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
