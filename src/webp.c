//
// Copyright 2025 voidtools / David Carpenter
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
// webp layer

#include "viv.h"
#include "viv_state.h"
#include <src/webp/decode.h>
#include <src/webp/demux.h>
#include <assert.h>

// the animation frame-array gate (the viv_load.c twin): the canvas
// ceilings bound one frame, the array holds them all - the frame count
// and the total frame bytes carry their own ceilings.
static int _animation_budget_refused(DWORD frame_count,SIZE_T canvas_pixels)
{
	if (frame_count > VIV_MAX_ANIMATION_FRAMES)
	{
		debug_printf("animation budget: refusing %u frames (ceiling %u)\r\n",(unsigned int)frame_count,(unsigned int)VIV_MAX_ANIMATION_FRAMES);
		
		_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);
		
		return 1;
	}
	
	// 16/3 bytes per canvas pixel per frame: the frames plus the
	// mipmap chain's extra third (the loader's gate prices the same).
	if ((VIV_UINT64)frame_count * (VIV_UINT64)canvas_pixels * 16 / 3 > VIV_MAX_ANIMATION_TOTAL_BYTES)
	{
		debug_printf("animation budget: refusing %u frames of a %u mp canvas (%u mb of frames, ceiling %u mb)\r\n",(unsigned int)frame_count,(unsigned int)(canvas_pixels / 1000000),(unsigned int)(((VIV_UINT64)frame_count * (VIV_UINT64)canvas_pixels * 16 / 3) / 1000000),(unsigned int)(VIV_MAX_ANIMATION_TOTAL_BYTES / 1000000));
		
		_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);
		
		return 1;
	}
	
	return 0;
}

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

int webp_load(IStream *stream,void *user_data,int (*info_callback)(void *user_data,DWORD frame_count,DWORD wide,DWORD high,int has_alpha),int (*frame_callback)(void *user_data,BYTE *pixels,int delay))
{
	int ret;
	HGLOBAL hglobal;
	
	ret = 0;
	
//	_wassert(L"test",TEXT(__FILE__),__LINE__);
	
	// the loader's stage marker: the exit timeout names the decoder that
	// was running when the wait expired.
	InterlockedExchangePointer(&_viv_load_stage,(PVOID)"webp");

	if (SUCCEEDED(GetHGlobalFromStream(stream,&hglobal)))
	{
		void *data_ptr;
		
		data_ptr = GlobalLock(hglobal);
		if (data_ptr)
		{
			WebPBitstreamFeatures features;
			SIZE_T data_size;
			
			data_size = GlobalSize(hglobal);	
			
			if (WebPGetFeatures(data_ptr,data_size,&features) == VP8_STATUS_OK)
			{
				if (features.has_animation)
				{
					WebPAnimDecoderOptions anim_decoder_options;
					WebPAnimDecoder *anim_decoder;
					
					if (WebPAnimDecoderOptionsInit(&anim_decoder_options))
					{
						WebPData webp_data;

						webp_data.bytes = data_ptr;
						webp_data.size = data_size;
						
						anim_decoder = WebPAnimDecoderNew(&webp_data,&anim_decoder_options);
						if (anim_decoder)
						{
							WebPAnimInfo anim_info;
				DWORD *frame_delays = 0;
							
							if (WebPAnimDecoderGetInfo(anim_decoder,&anim_info))
							{
								// pixel budget: refuse a hostile canvas before libwebp
								// allocates the frame buffers of the animation.
								if ((!_pixel_budget_refused(safe_size_mul((SIZE_T)anim_info.canvas_width,(SIZE_T)anim_info.canvas_height),VIV_MAX_ANIMATION_PIXELS)) && (!_animation_budget_refused(anim_info.frame_count,safe_size_mul((SIZE_T)anim_info.canvas_width,(SIZE_T)anim_info.canvas_height))) && (info_callback(user_data,anim_info.frame_count,anim_info.canvas_width,anim_info.canvas_height,features.has_alpha)))
								{
									uint8_t *frame;
									int timestamp; // out-param of webpanimdecodergetnext: libwebp dereferences the pointer unconditionally, it is not dead
									DWORD frame_run;
									DWORD frame_index;
																			
									frame = NULL;
									frame_run = anim_info.frame_count;
									frame_index = 0;
									
									ret = 1;

									// pre-scan the container with the demuxer: the per-frame durations live
									// in the chunk headers and the anim decoder api only reports start
									// timestamps. falls back to zero delays if the demuxer refuses the
									// data (the caller already handles zero-delay frames).
									{
										WebPDemuxer *demux;

										demux = WebPDemux(&webp_data);
										if (demux)
										{
											WebPIterator iter;

											// the frame count is a container-declared uint32: run it through
											// the safe multiply so a wrapped product can not hand a tiny
											// allocation to a loop that indexes by the declared count.
											{
													SIZE_T frame_delay_bytes;

													frame_delay_bytes = safe_size_mul((SIZE_T)anim_info.frame_count,sizeof(DWORD));

													if (frame_delay_bytes)
													{
																frame_delays = (DWORD *)mem_alloc(frame_delay_bytes);

																if (frame_delays)
																{
																		os_zero_memory(frame_delays,(int)frame_delay_bytes);
																}
															}
													}

											// the write side matches the read side's null guard (the read
										// at the frame callback checks frame_delays too): a failed
										// delay allocation skips the demux scan instead of writing
										// through the null the allocation left behind.
										if ((frame_delays) && (WebPDemuxGetFrame(demux,1,&iter)))
											{
												DWORD delay_index;

												delay_index = 0;
												do
												{
													if (delay_index < anim_info.frame_count)
													{
														// a zero-duration chunk still needs a tick of its own.
														frame_delays[delay_index] = iter.duration ? (DWORD)iter.duration : 1;
														delay_index++;
													}
												}
												while (WebPDemuxNextFrame(&iter));

												WebPDemuxReleaseIterator(&iter);
											}

											WebPDemuxDelete(demux);
										}
									}

									while (frame_run) 
									{
										if (WebPAnimDecoderGetNext(anim_decoder, &frame, &timestamp)) 
										{
											DWORD delay;
											
											// `frame` is a RGBA image of size: canvas_width * canvas_height * 4
											// `timestamp` is in milliseconds
											// Process the frame (copy/store/display)
											
											// deliver each frame with its own duration from the container scan:
											// the old timestamp-gap arithmetic handed every frame its
											// predecessors duration and the last frames duration never existed
											// at all (the anim decoder api reports no total duration).
											delay = 0;
											if ((frame_delays) && (frame_index < anim_info.frame_count))
											{
												delay = frame_delays[frame_index];
											}
											
											if (!frame_callback(user_data,frame,delay))
											{
												ret = 0;
												break;
											}
											
											frame_index++;
										}
										else
										{
											// expected frame?
											ret = 0;
											break;
										}
										
										frame_run--;
									}
								}
							}
							
							if (frame_delays)
							{
								mem_free(frame_delays);
							}
							
							WebPAnimDecoderDelete(anim_decoder);
						}
					}
				}
				else
				{
					// pixel budget: refuse a hostile canvas before libwebp
					// allocates the rgba buffer (the decode itself is the
					// multi gigabyte allocation the budget exists for).
					if (!_pixel_budget_refused(safe_size_mul((SIZE_T)features.width,(SIZE_T)features.height),VIV_MAX_IMAGE_PIXELS))
					{
						BYTE *pixels;
						int width;
						int height;

						pixels = WebPDecodeRGBA(data_ptr,data_size,&width,&height);
						if (pixels)
						{
							if (info_callback(user_data,1,width,height,features.has_alpha))
							{
								if (frame_callback(user_data,pixels,0))
								{
									ret = 1;
								}
							}
					
							WebPFree(pixels);
						}
					}
				}
			}
		
			GlobalUnlock(hglobal);
		}
	}
	
	return ret;
}

void __cdecl _wassert(const wchar_t * _Message, const wchar_t *_File, unsigned _Line)
{
	debug_fatal("%S(%d) : %S",_File,_Line,_Message);
}
