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
// the opengl renderer.

#include "viv.h"
#include "hwgl.h"
#include <GL/gl.h>

// the opengl32 export set, resolved at first use: the renderer stays a
// pure option and the exe never grows a static import for it. the pixel
// format and swap calls ride gdi32, which the build already links.
typedef HGLRC (__stdcall *_viv_gl_wglCreateContext_t)(HDC);
typedef BOOL (__stdcall *_viv_gl_wglMakeCurrent_t)(HDC,HGLRC);
typedef BOOL (__stdcall *_viv_gl_wglDeleteContext_t)(HGLRC);
typedef const GLubyte * (__stdcall *_viv_gl_getstring_t)(GLenum);
typedef void (__stdcall *_viv_gl_getintegerv_t)(GLenum,GLint *);
typedef void (__stdcall *_viv_gl_viewport_t)(GLint,GLint,GLsizei,GLsizei);
typedef void (__stdcall *_viv_gl_matrixmode_t)(GLenum);
typedef void (__stdcall *_viv_gl_loadidentity_t)(void);
typedef void (__stdcall *_viv_gl_ortho_t)(GLdouble,GLdouble,GLdouble,GLdouble,GLdouble,GLdouble);
typedef void (__stdcall *_viv_gl_pixelstorei_t)(GLenum,GLint);
typedef void (__stdcall *_viv_gl_gentextures_t)(GLsizei,GLuint *);
typedef void (__stdcall *_viv_gl_bindtexture_t)(GLenum,GLuint);
typedef void (__stdcall *_viv_gl_teximage2d_t)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const GLvoid *);
typedef void (__stdcall *_viv_gl_texparameteri_t)(GLenum,GLenum,GLint);
typedef void (__stdcall *_viv_gl_deletetextures_t)(GLsizei,const GLuint *);
typedef void (__stdcall *_viv_gl_enable_t)(GLenum);
typedef void (__stdcall *_viv_gl_disable_t)(GLenum);
typedef void (__stdcall *_viv_gl_begin_t)(GLenum);
typedef void (__stdcall *_viv_gl_end_t)(void);
typedef void (__stdcall *_viv_gl_texcoord2f_t)(GLfloat,GLfloat);
typedef void (__stdcall *_viv_gl_vertex2f_t)(GLfloat,GLfloat);
typedef void (__stdcall *_viv_gl_clearcolor_t)(GLclampf,GLclampf,GLclampf,GLclampf);
typedef void (__stdcall *_viv_gl_clear_t)(GLbitfield);
typedef void (__stdcall *_viv_gl_readpixels_t)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,GLvoid *);
typedef void (__stdcall *_viv_gl_readbuffer_t)(GLenum);

static HMODULE _viv_gl_module;
static _viv_gl_wglCreateContext_t _viv_gl_wglCreateContext;
static _viv_gl_wglMakeCurrent_t _viv_gl_wglMakeCurrent;
static _viv_gl_wglDeleteContext_t _viv_gl_wglDeleteContext;
static _viv_gl_getstring_t _viv_gl_getstring;
static _viv_gl_getintegerv_t _viv_gl_getintegerv;
static _viv_gl_viewport_t _viv_gl_viewport;
static _viv_gl_matrixmode_t _viv_gl_matrixmode;
static _viv_gl_loadidentity_t _viv_gl_loadidentity;
static _viv_gl_ortho_t _viv_gl_ortho;
static _viv_gl_pixelstorei_t _viv_gl_pixelstorei;
static _viv_gl_gentextures_t _viv_gl_gentextures;
static _viv_gl_bindtexture_t _viv_gl_bindtexture;
static _viv_gl_teximage2d_t _viv_gl_teximage2d;
static _viv_gl_texparameteri_t _viv_gl_texparameteri;
static _viv_gl_deletetextures_t _viv_gl_deletetextures;
static _viv_gl_enable_t _viv_gl_enable;
static _viv_gl_disable_t _viv_gl_disable;
static _viv_gl_begin_t _viv_gl_begin;
static _viv_gl_end_t _viv_gl_end;
static _viv_gl_texcoord2f_t _viv_gl_texcoord2f;
static _viv_gl_vertex2f_t _viv_gl_vertex2f;
static _viv_gl_clearcolor_t _viv_gl_clearcolor;
static _viv_gl_clear_t _viv_gl_clear;
static _viv_gl_readpixels_t _viv_gl_readpixels;
static _viv_gl_readbuffer_t _viv_gl_readbuffer;

static HGLRC _viv_gl_context;
static HWND _viv_gl_pixel_format_hwnd;
static GLuint _viv_gl_texture;
static HBITMAP _viv_gl_last_hbitmap;
static int _viv_gl_failed;
static int _viv_gl_max_texture;
static int _viv_gl_bgra;
static GLfloat _viv_gl_u;
static GLfloat _viv_gl_v;
static int _viv_gl_bottom_up;

// the export harness readback state: a top-down bgra buffer armed before
// the paint, filled by the render instead of a present.
static BYTE *_viv_gl_export_bits;
static int _viv_gl_export_wide;
static int _viv_gl_export_high;
static int _viv_gl_export_filled;

static int _viv_gl_procs(void)
{
	if (!_viv_gl_module)
	{
		_viv_gl_module = LoadLibraryA("opengl32.dll");
		if (!_viv_gl_module)
		{
			debug_printf("opengl: opengl32.dll is not on this system\r\n");
			
			return 0;
		}
		
		_viv_gl_wglCreateContext = (_viv_gl_wglCreateContext_t)GetProcAddress(_viv_gl_module,"wglCreateContext");
		_viv_gl_wglMakeCurrent = (_viv_gl_wglMakeCurrent_t)GetProcAddress(_viv_gl_module,"wglMakeCurrent");
		_viv_gl_wglDeleteContext = (_viv_gl_wglDeleteContext_t)GetProcAddress(_viv_gl_module,"wglDeleteContext");
		_viv_gl_getstring = (_viv_gl_getstring_t)GetProcAddress(_viv_gl_module,"glGetString");
		_viv_gl_getintegerv = (_viv_gl_getintegerv_t)GetProcAddress(_viv_gl_module,"glGetIntegerv");
		_viv_gl_viewport = (_viv_gl_viewport_t)GetProcAddress(_viv_gl_module,"glViewport");
		_viv_gl_matrixmode = (_viv_gl_matrixmode_t)GetProcAddress(_viv_gl_module,"glMatrixMode");
		_viv_gl_loadidentity = (_viv_gl_loadidentity_t)GetProcAddress(_viv_gl_module,"glLoadIdentity");
		_viv_gl_ortho = (_viv_gl_ortho_t)GetProcAddress(_viv_gl_module,"glOrtho");
		_viv_gl_pixelstorei = (_viv_gl_pixelstorei_t)GetProcAddress(_viv_gl_module,"glPixelStorei");
		_viv_gl_gentextures = (_viv_gl_gentextures_t)GetProcAddress(_viv_gl_module,"glGenTextures");
		_viv_gl_bindtexture = (_viv_gl_bindtexture_t)GetProcAddress(_viv_gl_module,"glBindTexture");
		_viv_gl_teximage2d = (_viv_gl_teximage2d_t)GetProcAddress(_viv_gl_module,"glTexImage2D");
		_viv_gl_texparameteri = (_viv_gl_texparameteri_t)GetProcAddress(_viv_gl_module,"glTexParameteri");
		_viv_gl_deletetextures = (_viv_gl_deletetextures_t)GetProcAddress(_viv_gl_module,"glDeleteTextures");
		_viv_gl_enable = (_viv_gl_enable_t)GetProcAddress(_viv_gl_module,"glEnable");
		_viv_gl_disable = (_viv_gl_disable_t)GetProcAddress(_viv_gl_module,"glDisable");
		_viv_gl_begin = (_viv_gl_begin_t)GetProcAddress(_viv_gl_module,"glBegin");
		_viv_gl_end = (_viv_gl_end_t)GetProcAddress(_viv_gl_module,"glEnd");
		_viv_gl_texcoord2f = (_viv_gl_texcoord2f_t)GetProcAddress(_viv_gl_module,"glTexCoord2f");
		_viv_gl_vertex2f = (_viv_gl_vertex2f_t)GetProcAddress(_viv_gl_module,"glVertex2f");
		_viv_gl_clearcolor = (_viv_gl_clearcolor_t)GetProcAddress(_viv_gl_module,"glClearColor");
		_viv_gl_clear = (_viv_gl_clear_t)GetProcAddress(_viv_gl_module,"glClear");
		_viv_gl_readpixels = (_viv_gl_readpixels_t)GetProcAddress(_viv_gl_module,"glReadPixels");
		_viv_gl_readbuffer = (_viv_gl_readbuffer_t)GetProcAddress(_viv_gl_module,"glReadBuffer");
	}
	
	return (_viv_gl_wglCreateContext) && (_viv_gl_wglMakeCurrent) && (_viv_gl_wglDeleteContext) && (_viv_gl_getstring) && (_viv_gl_getintegerv) && (_viv_gl_viewport) && (_viv_gl_matrixmode) && (_viv_gl_loadidentity) && (_viv_gl_ortho) && (_viv_gl_pixelstorei) && (_viv_gl_gentextures) && (_viv_gl_bindtexture) && (_viv_gl_teximage2d) && (_viv_gl_texparameteri) && (_viv_gl_deletetextures) && (_viv_gl_enable) && (_viv_gl_disable) && (_viv_gl_begin) && (_viv_gl_end) && (_viv_gl_texcoord2f) && (_viv_gl_vertex2f) && (_viv_gl_clearcolor) && (_viv_gl_clear);
}

// probes the extension string for one name (the microsoft software
// implementation answers "GL_WIN_swap_hint GL_EXT_bgra
// GL_EXT_paletted_texture" - the archived kb154877 fact, second-verified
// online; a driver without bgra falls back to the manual rgba repack).
static int _viv_gl_has_extension(const char *name)
{
	const GLubyte *extensions;
	const char *needle;
	size_t len;
	
	extensions = _viv_gl_getstring(GL_EXTENSIONS);
	if (!extensions)
	{
		return 0;
	}
	
	needle = (const char *)extensions;
	len = strlen(name);
	
	while (*needle)
	{
		const char *space;
		size_t run;
		
		space = strchr(needle,' ');
		run = space ? (size_t)(space - needle) : strlen(needle);
		
		if ((run == len) && (memcmp(needle,name,len) == 0))
		{
			return 1;
		}
		
		needle += run;
		while (*needle == ' ')
		{
			needle++;
		}
	}
	
	return 0;
}

// the pixel format answers once per window: the flag set keeps gdi alive
// on the same window (the renderer is an option, the gdi path must keep
// painting whenever the option is off). a window change re-runs the
// format: setpixelformat binds to the window the dc belongs to, and a
// fresh window without its own format fails every wglmakecurrent (the
// defensive path - the viewer's main window lives as long as the
// session, the guard keeps a hypothetical recreate honest) - and the
// context rebuilds with it, the same-format contract leaves the old
// rc unusable on a differently formatted dc.
static int _viv_gl_context_create(HWND hwnd,HDC hdc)
{
	if (hwnd != _viv_gl_pixel_format_hwnd)
	{
		PIXELFORMATDESCRIPTOR pfd;
		int format;
		
		ZeroMemory(&pfd,sizeof(pfd));
		pfd.nSize = sizeof(pfd);
		pfd.nVersion = 1;
		pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_SUPPORT_GDI;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 24;
		pfd.cDepthBits = 0;
		pfd.cStencilBits = 0;
		pfd.iLayerType = PFD_MAIN_PLANE;
		
		format = ChoosePixelFormat(hdc,&pfd);
		if (!format)
		{
			debug_printf("opengl: no pixel format answered\r\n");
			
			return 0;
		}
		
		if (!SetPixelFormat(hdc,format,&pfd))
		{
			debug_printf("opengl: SetPixelFormat failed %d\r\n",GetLastError());
			
			return 0;
		}
		
		_viv_gl_pixel_format_hwnd = hwnd;
		
		// a fresh window means a fresh context. the wglMakeCurrent
		// contract demands the dc answer "the same device and the
		// same pixel format" the rc was created against (the msdn
		// wording, second-verified), and choosepixelformat owes no
		// index stability across windows - keeping the old rc on
		// the new dc leaves every later wglMakeCurrent one format
		// mismatch away from a session-wide _viv_gl_failed (the
		// original refusal wearing a more hidden shell). the
		// context delete frees its texture with it; the last
		// hbitmap reset makes the next frame rebuild the whole
		// state against the new context.
		if (_viv_gl_context)
		{
			_viv_gl_wglMakeCurrent(NULL,NULL);
			_viv_gl_wglDeleteContext(_viv_gl_context);
			_viv_gl_context = 0;
			_viv_gl_texture = 0;
			_viv_gl_last_hbitmap = 0;
		}
	}
	
	if (!_viv_gl_context)
	{
		_viv_gl_context = _viv_gl_wglCreateContext(hdc);
		if (!_viv_gl_context)
		{
			debug_printf("opengl: wglCreateContext failed %d\r\n",GetLastError());
			
			return 0;
		}
		
		if (!_viv_gl_wglMakeCurrent(hdc,_viv_gl_context))
		{
			debug_printf("opengl: wglMakeCurrent failed %d\r\n",GetLastError());
			
			_viv_gl_wglDeleteContext(_viv_gl_context);
			_viv_gl_context = 0;
			
			return 0;
		}
		
		_viv_gl_max_texture = 0;
		_viv_gl_getintegerv(GL_MAX_TEXTURE_SIZE,&_viv_gl_max_texture);
		if (_viv_gl_max_texture <= 0)
		{
			_viv_gl_max_texture = 256;
		}
		
		_viv_gl_bgra = _viv_gl_has_extension("GL_EXT_bgra");
		if (!_viv_gl_bgra)
		{
			debug_printf("opengl: GL_EXT_bgra missing, the upload repacks to rgba\r\n");
		}
		
		_viv_gl_texture = 0;
		_viv_gl_gentextures(1,&_viv_gl_texture);
		if (!_viv_gl_texture)
		{
			debug_printf("opengl: the texture name did not answer\r\n");
			
			_viv_gl_wglMakeCurrent(NULL,NULL);
			_viv_gl_wglDeleteContext(_viv_gl_context);
			_viv_gl_context = 0;
			
			return 0;
		}
	}
	
	return 1;
}

// the upload: the GL 1.1 contract takes power of two sides, so the image
// pads into the next power of two and the quad samples the sub-rectangle
// (the mipmap builder route would rescale
// non-square images to fit - the distortion is not acceptable for a
// viewer, and the extra dll ride is not either).
static int _viv_gl_texture_upload(HBITMAP hbitmap,DIBSECTION *ds)
{
	int wide;
	int high;
	int stride;
	int pot_wide;
	int pot_high;
	GLenum format;
	BYTE *buf;
	uintptr_t size;
	
	wide = ds->dsBm.bmWidth;
	high = ds->dsBm.bmHeight;
	stride = ((wide * ds->dsBm.bmBitsPixel + 31) / 32) * 4;
	
	if ((wide <= 0) || (high <= 0))
	{
		return 0;
	}
	
	pot_wide = 1;
	while ((pot_wide < wide) && (pot_wide < _viv_gl_max_texture))
	{
		pot_wide <<= 1;
	}
	
	pot_high = 1;
	while ((pot_high < high) && (pot_high < _viv_gl_max_texture))
	{
		pot_high <<= 1;
	}
	
	// the max clause answers the overshoot: a non power of two max texture size
	// can sit between two powers, and the loop above lands past it (the
	// refusal check below only compares against the image, the texture the
	// device would refuse answers here instead - silently, at draw time).
	if ((pot_wide < wide) || (pot_high < high) || (pot_wide > _viv_gl_max_texture) || (pot_high > _viv_gl_max_texture))
	{
		debug_printf("opengl: the image exceeds the max texture size (%d)\r\n",_viv_gl_max_texture);
		
		return 0;
	}
	
	if ((uintptr_t)pot_wide * (uintptr_t)pot_high > VIV_MAX_IMAGE_PIXELS)
	{
		debug_printf("opengl: the padded canvas exceeds the pixel budget\r\n");
		
		return 0;
	}
	
	if (_viv_gl_bgra && (ds->dsBm.bmBitsPixel == 24))
	{
		format = GL_BGR_EXT;
		size = (uintptr_t)pot_wide * pot_high * 3;
	}
	else
	{
		format = GL_RGBA;
		size = (uintptr_t)pot_wide * pot_high * 4;
	}
	
	buf = (BYTE *)mem_alloc(size);
	if (!buf)
	{
		return 0;
	}
	
	{
		const BYTE *s;
		BYTE *d;
		int y;
		
		s = (const BYTE *)ds->dsBm.bmBits;
		d = buf;
		
		for(y=0;y<high;y++)
		{
			if (format == GL_BGR_EXT)
			{
				memcpy(d,s,(uintptr_t)wide * 3);
				d += pot_wide * 3;
			}
			else
			{
				const BYTE *p;
				int x;
				
				p = s;
				
				for(x=0;x<wide;x++)
				{
					if (ds->dsBm.bmBitsPixel == 24)
					{
						d[0] = p[2]; // r
						d[1] = p[1]; // g
						d[2] = p[0]; // b
						p += 3;
				}
					else
					{
						d[0] = p[2]; // r
						d[1] = p[1]; // g
						d[2] = p[0]; // b
						p += 4;
				}
					
				// the frames arrive pre-flattened: the load path composites
				// the alpha onto the backdrop before the frame ever answers,
				// so the opaque write matches the gdi path's own semantics.
				d[3] = 255;
				d += 4;
			}
				
				d += (pot_wide - wide) * 4;
			}
			
			s += stride;
		}
	}
	
	// the pad must answer as if the texture border sat at the image
	// border. the linear sampler's half texel at the sub-rectangle
	// edge reads the neighbouring texel, and the clamp modes only
	// answer at the texture's own border - never at a sub-rectangle
	// inside it. so the right-hand gutter carries the image's last
	// column and every row under the image carries the image's last
	// row (gutter included, so the corner holds the bottom-right
	// pixel). a merely zeroed pad would stop the stale garbage but
	// still average black into the outermost destination pixels on
	// a heavy upscale; the replication is the clamp the image's own
	// edges imply.
	if ((pot_wide > wide) || (pot_high > high))
	{
		int bpp;
		int y;
		
		bpp = (format == GL_BGR_EXT) ? 3 : 4;
		
		for(y=0;y<high;y++)
		{
			BYTE *row;
			const BYTE *last;
			int x;
			
			row = buf + (uintptr_t)y * (uintptr_t)pot_wide * (uintptr_t)bpp;
			last = row + (uintptr_t)(wide - 1) * (uintptr_t)bpp;
			
			for(x=wide;x<pot_wide;x++)
			{
				memcpy(row + (uintptr_t)x * (uintptr_t)bpp,last,(size_t)bpp);
			}
		}
		
		for(y=high;y<pot_high;y++)
		{
			memcpy(buf + (uintptr_t)y * (uintptr_t)pot_wide * (uintptr_t)bpp,
			       buf + (uintptr_t)(y - 1) * (uintptr_t)pot_wide * (uintptr_t)bpp,
			       (uintptr_t)pot_wide * (uintptr_t)bpp);
		}
	}
	
	_viv_gl_pixelstorei(GL_UNPACK_ALIGNMENT,1);
	_viv_gl_bindtexture(GL_TEXTURE_2D,_viv_gl_texture);
	_viv_gl_teximage2d(GL_TEXTURE_2D,0,GL_RGB,pot_wide,pot_high,0,format,GL_UNSIGNED_BYTE,buf);
	_viv_gl_texparameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	_viv_gl_texparameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	_viv_gl_texparameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
	_viv_gl_texparameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
	
	mem_free(buf);
	
	_viv_gl_u = (GLfloat)wide / (GLfloat)pot_wide;
	_viv_gl_v = (GLfloat)high / (GLfloat)pot_high;
	_viv_gl_bottom_up = (ds->dsBmih.biHeight > 0) ? 1 : 0;
	_viv_gl_last_hbitmap = hbitmap;
	
	return 1;
}

// the export readback: the back buffer holds the frame, the read buffer
// is pinned to GL_BACK (the double buffered default, made explicit), and
// the rows arrive bottom-up - the export buffer is top-down bgra with
// the reserved byte pinned to zero (the hash must never see undefined
// bytes). bgra rides the same extension the upload probes; the rgba
// fallback swizzles as it copies.
static void _viv_gl_export_readback(int wide,int high)
{
	BYTE *tmp;
	
	if ((!_viv_gl_readpixels) || (wide != _viv_gl_export_wide) || (high != _viv_gl_export_high))
	{
		return;
	}
	
	tmp = (BYTE *)mem_alloc((uintptr_t)wide * (uintptr_t)high * 4);
	if (!tmp)
	{
		return;
	}
	
	if (_viv_gl_readbuffer)
	{
		_viv_gl_readbuffer(GL_BACK);
	}
	
	_viv_gl_readpixels(0,0,wide,high,_viv_gl_bgra ? GL_BGRA_EXT : GL_RGBA,GL_UNSIGNED_BYTE,tmp);
	
	{
		int y;
		
		for(y=0;y<high;y++)
		{
			const BYTE *s;
			BYTE *d;
			int x;
			
			s = tmp + (uintptr_t)(high - 1 - y) * (uintptr_t)wide * 4;
			d = _viv_gl_export_bits + (uintptr_t)y * (uintptr_t)wide * 4;
			
			if (_viv_gl_bgra)
			{
				memcpy(d,s,(uintptr_t)wide * 4);
				
				for(x=0;x<wide;x++)
				{
					d[x * 4 + 3] = 0;
				}
			}
			else
			{
				for(x=0;x<wide;x++)
				{
					d[0] = s[2];
					d[1] = s[1];
					d[2] = s[0];
					d[3] = 0;
					s += 4;
					d += 4;
				}
			}
		}
	}
	
	mem_free(tmp);
	
	// the disarm: the buffer belongs to the caller (freed right after the
	// run) - a second render must never touch it again, and a session that
	// ever grew a second caller must keep its presents.
	_viv_gl_export_bits = 0;
	_viv_gl_export_filled = 1;
}

int _viv_hwgl_render(HWND hwnd,HDC hdc,HBITMAP hbitmap,int dst_x,int dst_y,int dst_wide,int dst_high,COLORREF clear_color)
{
	DIBSECTION ds;
	RECT rect;
	int wide;
	int high;
	
	if (_viv_gl_failed)
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
	
	GetClientRect(hwnd,&rect);
	wide = rect.right - rect.left;
	high = rect.bottom - rect.top;
	
	if ((!wide) || (!high))
	{
		return 0;
	}
	
	if (!_viv_gl_procs())
	{
		_viv_gl_failed = 1;
		
		return 0;
	}
	
	if (!_viv_gl_context_create(hwnd,hdc))
	{
		_viv_gl_failed = 1;
		
		return 0;
	}
	
	if (!_viv_gl_wglMakeCurrent(hdc,_viv_gl_context))
	{
		debug_printf("opengl: wglMakeCurrent failed %d\r\n",GetLastError());
		
		_viv_gl_failed = 1;
		
		return 0;
	}
	
	if (hbitmap != _viv_gl_last_hbitmap)
	{
		if (!_viv_gl_texture_upload(hbitmap,&ds))
		{
			// an oversized image is a per-image refusal, not a session
			// failure: the gdi path keeps this one and the next image
			// retries.
			return 0;
		}
	}
	
	_viv_gl_viewport(0,0,wide,high);
	_viv_gl_matrixmode(GL_PROJECTION);
	_viv_gl_loadidentity();
	_viv_gl_ortho(0,wide,high,0,-1,1);
	_viv_gl_matrixmode(GL_MODELVIEW);
	_viv_gl_loadidentity();
	
	_viv_gl_clearcolor(GetRValue(clear_color) / 255.0f,GetGValue(clear_color) / 255.0f,GetBValue(clear_color) / 255.0f,1.0f);
	_viv_gl_clear(GL_COLOR_BUFFER_BIT);
	
	_viv_gl_enable(GL_TEXTURE_2D);
	
	_viv_gl_begin(GL_QUADS);
	
	if (_viv_gl_bottom_up)
	{
		_viv_gl_texcoord2f(0.0f,_viv_gl_v);
		_viv_gl_vertex2f((GLfloat)dst_x,(GLfloat)dst_y);
		
		_viv_gl_texcoord2f(_viv_gl_u,_viv_gl_v);
		_viv_gl_vertex2f((GLfloat)(dst_x + dst_wide),(GLfloat)dst_y);
		
		_viv_gl_texcoord2f(_viv_gl_u,0.0f);
		_viv_gl_vertex2f((GLfloat)(dst_x + dst_wide),(GLfloat)(dst_y + dst_high));
		
		_viv_gl_texcoord2f(0.0f,0.0f);
		_viv_gl_vertex2f((GLfloat)dst_x,(GLfloat)(dst_y + dst_high));
	}
	else
	{
		_viv_gl_texcoord2f(0.0f,0.0f);
		_viv_gl_vertex2f((GLfloat)dst_x,(GLfloat)dst_y);
		
		_viv_gl_texcoord2f(_viv_gl_u,0.0f);
		_viv_gl_vertex2f((GLfloat)(dst_x + dst_wide),(GLfloat)dst_y);
		
		_viv_gl_texcoord2f(_viv_gl_u,_viv_gl_v);
		_viv_gl_vertex2f((GLfloat)(dst_x + dst_wide),(GLfloat)(dst_y + dst_high));
		
		_viv_gl_texcoord2f(0.0f,_viv_gl_v);
		_viv_gl_vertex2f((GLfloat)dst_x,(GLfloat)(dst_y + dst_high));
	}
	
	_viv_gl_end();
	
	_viv_gl_disable(GL_TEXTURE_2D);
	
	if (_viv_gl_export_bits)
	{
		_viv_gl_export_readback(wide,high);
	}
	else
	{
		SwapBuffers(hdc);
	}
	
	return 1;
}

void _viv_hwgl_shutdown(void)
{
	if (_viv_gl_context)
	{
		_viv_gl_wglMakeCurrent(NULL,NULL);
		
		if (_viv_gl_texture)
		{
			_viv_gl_deletetextures(1,&_viv_gl_texture);
			_viv_gl_texture = 0;
		}
		
		_viv_gl_wglDeleteContext(_viv_gl_context);
		_viv_gl_context = 0;
	}
	
	_viv_gl_last_hbitmap = 0;
	_viv_gl_failed = 0;
	// the pixel format hwnd stays: the window keeps its format (the
	// the export pointer dies with the caller's buffer - the shutdown
	// clears it so no later session state can point at freed memory.
	_viv_gl_export_bits = 0;
	// setPixelFormat contract answers once), a recreated window sets its
	// own.
}

// the export harness hooks: arm a top-down bgra buffer before the paint,
// the next render fills it instead of presenting. the query lets the
// harness tell a refused renderer (the gdi fallback painted instead)
// from a successful readback.
int _viv_hwgl_export_begin(BYTE *bits,int wide,int high)
{
	_viv_gl_export_bits = bits;
	_viv_gl_export_wide = wide;
	_viv_gl_export_high = high;
	_viv_gl_export_filled = 0;
	
	return 1;
}

int _viv_hwgl_export_filled(void)
{
	return _viv_gl_export_filled;
}
