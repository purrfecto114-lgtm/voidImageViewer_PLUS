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
// qoi layer - the "quite ok image" format (qoiformat.org).
// no system codec knows qoi anywhere, so this is the only way a qoi file
// ever opens - and it works on every windows the viewer runs on, 95 up.
// the opcode semantics are ported verbatim from the reference decoder
// (phoboslab's mit-licensed qoi.h): the index hash, the diff and luma
// deltas, the run bias of -1, and rgb chunks preserving the running alpha
// (the reference's exact behavior, not the intuitive one). the fork's
// hostile-input discipline rides on top: every read bounds-checks against
// the chunk end, the end marker is verified, and the pixel budget gates
// the canvas.

#include "viv.h"
#include "viv_state.h"
#include <string.h>

// the qoi magic: "qoif", big endian on the wire. the constant reads
// the four bytes as one big-endian word (the reference's
// 'q'<<24|'o'<<16|'i'<<8|'f'). the constant the format horizons
// round shipped spelled the bytes in little-endian order, so the
// big-endian reader never matched and every valid qoi file died at
// the gate - the fixture round's real-byte host harness caught it.
#define _QOI_MAGIC 0x716f6966

// the reference end marker: seven zero bytes then 0x01.
static const BYTE _qoi_end_marker[8] = {0,0,0,0,0,0,0,1};

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

static DWORD _qoi_read_u32_be(const BYTE *p)
{
	return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | (DWORD)p[3];
}

int qoi_load(IStream *stream,void *user_data,int (*info_callback)(void *user_data,DWORD frame_count,DWORD wide,DWORD high,int has_alpha),int (*frame_callback)(void *user_data,BYTE *pixels,int delay))
{
	int ret;
	HGLOBAL hglobal;
	
	ret = 0;
	
	// the loader's stage marker: the exit timeout names the decoder that
	// was running when the wait expired.
	_viv_load_stage = "qoi";

	if (SUCCEEDED(GetHGlobalFromStream(stream,&hglobal)))
	{
		void *data_ptr;
		
		data_ptr = GlobalLock(hglobal);
		if (data_ptr)
		{
			SIZE_T data_size;
			const BYTE *bytes;
			
			data_size = GlobalSize(hglobal);
			bytes = (const BYTE *)data_ptr;
			
			// the header is 14 bytes and the end marker 8 more; anything
			// shorter cannot be a qoi file.
			if ((data_size >= 14 + 8) && (_qoi_read_u32_be(bytes) == _QOI_MAGIC))
			{
				DWORD wide;
				DWORD high;
				BYTE channels;
				BYTE colorspace;
				
				wide = _qoi_read_u32_be(bytes + 4);
				high = _qoi_read_u32_be(bytes + 8);
				channels = bytes[12];
				colorspace = bytes[13];
				
				// channels is 3 (rgb) or 4 (rgba); the colorspace byte only
				// tags intent (srgb or linear) and never changes the decode.
				if (((channels == 3) || (channels == 4)) && (colorspace <= 1) && (wide) && (high))
				{
					// pixel budget: refuse a hostile canvas before the rgba
					// buffer allocates.
					if (!_pixel_budget_refused(safe_size_mul((SIZE_T)wide,(SIZE_T)high),VIV_MAX_IMAGE_PIXELS))
					{
						SIZE_T buffer_size;
						
						buffer_size = safe_size_mul(safe_size_mul((SIZE_T)wide,(SIZE_T)high),4);
						
						if (buffer_size)
						{
							BYTE *pixels;
							
							pixels = (BYTE *)mem_alloc(buffer_size);
							
							if (pixels)
							{
								BYTE index[64 * 4];
								BYTE r;
								BYTE g;
								BYTE b;
								BYTE a;
								const BYTE *p;
								const BYTE *chunks_end;
								BYTE *d;
								const BYTE *d_end;
								DWORD run;
								int decode_ok;
								
								os_zero_memory(index,sizeof(index));
								
								// the running pixel starts opaque (the reference's
								// zero state: rgb zero, alpha 255).
								r = 0;
								g = 0;
								b = 0;
								a = 255;
								
								p = bytes + 14;
								chunks_end = bytes + data_size - 8;
								d = pixels;
								d_end = pixels + buffer_size;
								run = 0;
								
								// a qoi stream always closes with the end marker; the
								// trailing-chunk leniency below matches the reference
								// (chunks after the canvas fills are ignored).
								decode_ok = (memcmp(chunks_end,_qoi_end_marker,8) == 0);
								
								while ((decode_ok) && (d < d_end))
								{
									// cooperative cancel: a quit or a navigation away must not
									// wait out a huge decode. the torn buffer never ships -
									// decode_ok drops with the break and the delivery below
									// gates on it.
									if (_VIV_LOAD_TERMINATED())
									{
										decode_ok = 0;
										
										break;
									}
									
									if (run)
									{
										run--;
									}
									else if (p < chunks_end)
									{
										BYTE b1;
										
										b1 = *p++;
										
										if (b1 == 0xfe) // rgb
										{
											if ((chunks_end - p) < 3)
											{
												decode_ok = 0;
												
												break;
											}
											
											r = p[0];
											g = p[1];
											b = p[2];
											p += 3;
											
											// the reference preserves the running alpha.
										}
										else if (b1 == 0xff) // rgba
										{
											if ((chunks_end - p) < 4)
											{
												decode_ok = 0;
												
												break;
											}
											
											r = p[0];
											g = p[1];
											b = p[2];
											a = p[3];
											p += 4;
										}
										else if ((b1 & 0xc0) == 0x00) // index
										{
											const BYTE *px;
											
											px = index + ((SIZE_T)(b1 & 0x3f) * 4);
											
											r = px[0];
											g = px[1];
											b = px[2];
											a = px[3];
										}
										else if ((b1 & 0xc0) == 0x40) // diff
										{
											// the deltas are signed; the byte fields wrap
											// exactly like the reference's unsigned chars.
											r += (((b1 >> 4) & 0x03) - 2);
											g += (((b1 >> 2) & 0x03) - 2);
											b += ((b1 & 0x03) - 2);
										}
										else if ((b1 & 0xc0) == 0x80) // luma
										{
											BYTE b2;
												int vg;
												
												if (p >= chunks_end)
												{
													decode_ok = 0;
													
													break;
												}
												
												b2 = *p++;
												
												vg = (b1 & 0x3f) - 32;
												
												r += (vg - 8 + ((b2 >> 4) & 0x0f));
												g += vg;
												b += (vg - 8 + (b2 & 0x0f));
										}
										else // run: the 0xc0 mask
										{
											// the run length carries the bias of -1: the
											// chunk's own pixel plus run repeats.
											run = (b1 & 0x3f);
										}
										
										// the index update rides every chunk the reference
										// reads (run-consumed pixels never touch it).
										{
											int hash;
											
											hash = ((r * 3) + (g * 5) + (b * 7) + (a * 11)) & 63;
											
											index[hash * 4 + 0] = r;
											index[hash * 4 + 1] = g;
											index[hash * 4 + 2] = b;
											index[hash * 4 + 3] = a;
										}
									}
									else
									{
										// the input ran out before the canvas filled.
										decode_ok = 0;
										
										break;
									}
									
									d[0] = r;
									d[1] = g;
									d[2] = b;
									d[3] = (channels == 4) ? a : 255;
									d += 4;
								}
								
								if (decode_ok)
								{
									int has_alpha;
									
									has_alpha = 0;
									
									// a 3-channel stream is opaque by construction; a
									// 4-channel one may still be fully opaque, and the
									// scan skips the backdrop blend when it is.
									if (channels == 4)
									{
										const BYTE *scan;
										const BYTE *scan_end;
										
										scan = pixels;
										scan_end = pixels + buffer_size;
										
										while (scan < scan_end)
										{
											if (scan[3] != 255)
											{
												has_alpha = 1;
												
												break;
											}
											
											scan += 4;
										}
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
					}
				}
			}
			
			GlobalUnlock(hglobal);
		}
	}
	
	return ret;
}
