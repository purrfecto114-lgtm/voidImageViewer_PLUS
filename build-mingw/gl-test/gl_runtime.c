#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
typedef uint8_t BYTE;
typedef uint32_t UINT,DWORD,COLORREF,ULONG;
typedef int32_t HRESULT;
typedef int BOOL;
typedef intptr_t HBITMAP,HDC,HWND,HGDIOBJ;
typedef size_t SIZE_T;
typedef struct { int left,top,right,bottom; } RECT;
typedef struct { int bmWidth,bmHeight,bmWidthBytes,bmPlanes,bmBitsPixel;void *bmBits; } BITMAP;
typedef struct { BITMAP dsBm; } DIBSECTION;
#define TRUE 1
#define FALSE 0
#define SUCCEEDED(x) ((HRESULT)(x)>=0)
#define FAILED(x) ((HRESULT)(x)<0)
#define GetRValue(x) ((x)&255)
#define GetGValue(x) (((x)>>8)&255)
#define GetBValue(x) (((x)>>16)&255)
#define debug_printf(...) ((void)0)
#define VIV_MAX_IMAGE_BYTES 2400000000ULL
static DWORD GetLastError(void) { return 8; }
static BOOL GetClientRect(HWND h,RECT *r) { *r=(RECT){0,0,4,4};return 1; }
static int GetObject(HBITMAP b,int size,void *out) {
    static BYTE pixels[64];DIBSECTION ds={{4,4,16,1,32,pixels}};
    memcpy(out,&ds,(size_t)size<sizeof(ds)?(size_t)size:sizeof(ds));return size;
}
typedef unsigned GLuint,GLenum;
typedef int GLint,GLsizei;
typedef float GLfloat;
typedef double GLdouble;
#define GL_TEXTURE_2D 0x0de1
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_BGR_EXT 0x80e0
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNPACK_ALIGNMENT 0x0cf5
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP 0x2900
#define GL_CLAMP_TO_EDGE 0x812f
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_QUADS 7
#define GL_MAX_TEXTURE_SIZE 0x0d33
#define GL_NO_ERROR 0
#define VIV_MAX_IMAGE_PIXELS 400000000
#define VIV_MAX_HARDWARE_BYTES (512u*1024u*1024u)
extern void glViewport(GLint,GLint,GLsizei,GLsizei);
extern void glMatrixMode(GLenum);
extern void glLoadIdentity(void);
extern void glOrtho(GLdouble,GLdouble,GLdouble,GLdouble,GLdouble,GLdouble);
extern void glClearColor(GLfloat,GLfloat,GLfloat,GLfloat);
extern void glClear(GLenum);
extern void glEnable(GLenum);
extern void glDisable(GLenum);
extern void glBegin(GLenum);
extern void glEnd(void);
extern void glTexCoord2f(GLfloat,GLfloat);
extern void glVertex2f(GLfloat,GLfloat);
extern void glPixelStorei(GLenum,GLint);
extern void glBindTexture(GLenum,GLuint);
extern void glTexParameteri(GLenum,GLenum,GLint);
extern void glTexImage2D(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*);
extern void glTexSubImage2D(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void*);
extern void glGenTextures(GLsizei,GLuint*);
extern void glGetIntegerv(GLenum,GLint*);
extern GLenum glGetError(void);
extern const BYTE*glGetString(GLenum);
extern void glReadPixels(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
extern void glFinish(void);
#define _viv_gl_geterror glGetError
#define _viv_gl_viewport glViewport
#define _viv_gl_matrixmode glMatrixMode
#define _viv_gl_loadidentity glLoadIdentity
#define _viv_gl_ortho glOrtho
#define _viv_gl_clearcolor glClearColor
#define _viv_gl_clear glClear
#define _viv_gl_enable glEnable
#define _viv_gl_disable glDisable
#define _viv_gl_begin glBegin
#define _viv_gl_end glEnd
#define _viv_gl_texcoord2f glTexCoord2f
#define _viv_gl_vertex2f glVertex2f
#define _viv_gl_pixelstorei glPixelStorei
#define _viv_gl_bindtexture glBindTexture
#define _viv_gl_texparameteri glTexParameteri
#define _viv_gl_teximage2d glTexImage2D
#define _viv_gl_texsubimage2d glTexSubImage2D
static int _viv_gl_failed,_viv_gl_max_texture,_viv_gl_bgra=1,_viv_gl_npot=1;
static int _viv_gl_last_pot_wide,_viv_gl_last_pot_high;
static GLenum _viv_gl_last_format;
static GLuint _viv_gl_texture;
static intptr_t _viv_gl_context=1;
static HBITMAP _viv_gl_last_hbitmap;
static BYTE *_viv_gl_stage_buf,*_viv_gl_export_bits;
static uintptr_t _viv_gl_stage_size;
static GLfloat _viv_gl_u,_viv_gl_v;
int _viv_gl_export_filled;
static BYTE white[16];
#define GetObject runtime_GetObject
static int runtime_GetObject(HBITMAP b,int size,DIBSECTION*d){d->dsBm=(BITMAP){2,2,8,1,32,white};return sizeof(*d);}
static int fail_alloc;
static void*mem_try_alloc(uintptr_t n){return fail_alloc?NULL:malloc(n);}
static void*mem_alloc(uintptr_t n){return malloc(n);}
static void mem_free(void*p){free(p);}
static size_t safe_size_mul(size_t a,size_t b){return b && a>SIZE_MAX/b?SIZE_MAX:a*b;}
static int _viv_gl_procs(void){return 1;}
static int _viv_gl_context_create(HWND h,HDC d){return 1;}
static int _viv_gl_wglMakeCurrent(HDC d,intptr_t c){return 1;}
static int SwapBuffers(HDC d){return 1;}
static void _viv_gl_export_readback(int w,int h){}
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


	if ((wide <= 0) || (high <= 0))
	{
		return 0;
	}

	pot_wide = wide;
	pot_high = high;
	if (!_viv_gl_npot)
	{
		pot_wide = 1;
		while ((pot_wide < wide) && (pot_wide < _viv_gl_max_texture)) pot_wide <<= 1;
		pot_high = 1;
		while ((pot_high < high) && (pot_high < _viv_gl_max_texture)) pot_high <<= 1;
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

	if (safe_size_mul(safe_size_mul((SIZE_T)pot_wide,(SIZE_T)pot_high),8) > VIV_MAX_HARDWARE_BYTES)
	{
		debug_printf("opengl: the stage + texture exceed the hardware byte budget\r\n");

		return 0;
	}

	// The byte gate bounds the row; calculate before narrowing.
	stride = (int)((((SIZE_T)wide * ds->dsBm.bmBitsPixel + 31) / 32) * 4);

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

	// Reuse equal-sized frames; shrink high-water allocations on a size change.
	if (_viv_gl_stage_size != size)
	{
		if (_viv_gl_stage_buf) mem_free(_viv_gl_stage_buf);
		_viv_gl_stage_buf = NULL;
		_viv_gl_stage_size = 0;
		_viv_gl_stage_buf = (BYTE *)mem_try_alloc(size);
		if (_viv_gl_stage_buf) _viv_gl_stage_size = size;
	}

	buf = _viv_gl_stage_buf;
	if (!buf)
	{
		_viv_gl_stage_size = 0;

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

	// the d3d twin's reuse gate, translated: a same-shape frame (every
	// frame of an animation) refills the texture with glTexSubImage2D
	// instead of re-specifying it - the re-spec makes the driver
	// reallocate the storage behind the name. a dimension or format
	// change (or the first upload on a fresh texture name) takes the
	// full glTexImage2d path and re-pins the filter and wrap states.
	if ((pot_wide == _viv_gl_last_pot_wide) && (pot_high == _viv_gl_last_pot_high) && (format == _viv_gl_last_format) && (_viv_gl_last_pot_wide))
	{
		_viv_gl_texsubimage2d(GL_TEXTURE_2D,0,0,0,pot_wide,pot_high,format,GL_UNSIGNED_BYTE,buf);
	}
	else
	{
		_viv_gl_teximage2d(GL_TEXTURE_2D,0,GL_RGBA8,pot_wide,pot_high,0,format,GL_UNSIGNED_BYTE,buf);
		_viv_gl_texparameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
		_viv_gl_texparameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		_viv_gl_texparameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
		_viv_gl_texparameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);

		_viv_gl_last_pot_wide = pot_wide;
		_viv_gl_last_pot_high = pot_high;
		_viv_gl_last_format = format;
	}

	if (_viv_gl_geterror() != GL_NO_ERROR)
	{
		_viv_gl_last_hbitmap = 0;
		_viv_gl_last_pot_wide = 0;
		_viv_gl_last_pot_high = 0;
		return 0;
	}
	_viv_gl_u = (GLfloat)wide / (GLfloat)pot_wide;
	_viv_gl_v = (GLfloat)high / (GLfloat)pot_high;
	_viv_gl_last_hbitmap = hbitmap;

	return 1;
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
		// the refusal names its reason: a silent return 0 is how an
		// entire decoder family (the gdi+ frames, back when they were
		// ddb surfaces) hid behind this gate for rounds while the pixel
		// oracle kept recording nulls that read as "no renderer on this
		// machine".
		if (!ds.dsBm.bmBits)
		{
			debug_printf("opengl: the frame is not a dib section (no bits answered) - the gdi path paints it\r\n");
		}
		else
		{
			debug_printf("opengl: the frame is %d bpp (24 or 32 answer) - the gdi path paints it\r\n",ds.dsBm.bmBitsPixel);
		}

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

	// every frame this app builds is a top-down dib, and the sign that
	// says so is unrecoverable through getobject - it answers the
	// absolute height for both orientations (the wine test suite
	// asserts it, reactos implements it, and the r96 read believed it
	// could tell: the flag was always 1 on a real machine and every
	// image rode the flip - the field report's upside-down launch).
	// the one mapping answers them all: the screen's top edge samples
	// the texture's row zero, the image's own top.
	_viv_gl_texcoord2f(0.0f,0.0f);
	_viv_gl_vertex2f((GLfloat)dst_x,(GLfloat)dst_y);

	_viv_gl_texcoord2f(_viv_gl_u,0.0f);
	_viv_gl_vertex2f((GLfloat)(dst_x + dst_wide),(GLfloat)dst_y);

	_viv_gl_texcoord2f(_viv_gl_u,_viv_gl_v);
	_viv_gl_vertex2f((GLfloat)(dst_x + dst_wide),(GLfloat)(dst_y + dst_high));

	_viv_gl_texcoord2f(0.0f,_viv_gl_v);
	_viv_gl_vertex2f((GLfloat)dst_x,(GLfloat)(dst_y + dst_high));

	_viv_gl_end();

	_viv_gl_disable(GL_TEXTURE_2D);
	if (_viv_gl_geterror() != GL_NO_ERROR) return 0;

	if (_viv_gl_export_bits)
	{
		_viv_gl_export_readback(wide,high);
		if (!_viv_gl_export_filled) return 0;
	}
	else
	{
		if (!SwapBuffers(hdc)) return 0;
	}

	return 1;
}
typedef void*EGLDisplay,*EGLConfig,*EGLContext,*EGLSurface;
extern void*eglGetProcAddress(const char*);
extern int eglInitialize(EGLDisplay,int*,int*);
extern int eglBindAPI(unsigned);
extern int eglChooseConfig(EGLDisplay,const int*,EGLConfig*,int,int*);
extern EGLSurface eglCreatePbufferSurface(EGLDisplay,EGLConfig,const int*);
extern EGLContext eglCreateContext(EGLDisplay,EGLConfig,EGLContext,const int*);
extern int eglMakeCurrent(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
extern int eglDestroyContext(EGLDisplay,EGLContext);
extern int eglDestroySurface(EGLDisplay,EGLSurface);
extern int eglTerminate(EGLDisplay);
int main(void){
 EGLDisplay(*platform)(unsigned,void*,const int*)=(void*)eglGetProcAddress("eglGetPlatformDisplayEXT");assert(platform);
 EGLDisplay display=platform(0x31dd,NULL,NULL);int major,minor,count;assert(display && eglInitialize(display,&major,&minor));assert(eglBindAPI(0x30a2));
 const int attrs[]={0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3038};EGLConfig config;assert(eglChooseConfig(display,attrs,&config,1,&count) && count==1);
 const int pbuffer[]={0x3057,4,0x3056,4,0x3038};EGLSurface surface=eglCreatePbufferSurface(display,config,pbuffer);assert(surface);
 EGLContext context=eglCreateContext(display,config,NULL,NULL);assert(context);assert(eglMakeCurrent(display,surface,surface,context));
 printf("EGL %d.%d; GL %s; renderer %s\n",major,minor,glGetString(0x1f02),glGetString(0x1f01));
 glGetIntegerv(GL_MAX_TEXTURE_SIZE,&_viv_gl_max_texture);printf("GL_MAX_TEXTURE_SIZE %d\n",_viv_gl_max_texture);glGenTextures(1,&_viv_gl_texture);assert(_viv_gl_texture);glDisable(0x0bd0);memset(white,255,sizeof(white));
 BYTE rendered[64];assert(_viv_hwgl_render(1,2,3,0,0,4,4,0)==1);glFinish();glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,rendered);assert(!glGetError());
 printf("white upscale red values:");for(int i=0;i<16;i++)printf(" %u",rendered[i*4]);puts("");fflush(stdout);
 for(int i=0;i<16;i++)assert(rendered[i*4]==255 && rendered[i*4+1]==255 && rendered[i*4+2]==255);
 BYTE pixels[64];memset(pixels,255,sizeof(pixels));DIBSECTION ds={{3,5,12,1,32,pixels}};assert(_viv_gl_texture_upload(4,&ds));assert(_viv_gl_last_pot_wide==3 && _viv_gl_last_pot_high==5 && _viv_gl_stage_size==60);
 ds.dsBm.bmBitsPixel=24;assert(_viv_gl_texture_upload(5,&ds));assert(_viv_gl_stage_size==45);
 ds.dsBm.bmBitsPixel=32;ds.dsBm.bmWidth=2;ds.dsBm.bmHeight=2;assert(_viv_gl_texture_upload(6,&ds));assert(_viv_gl_stage_size==16);
 ds.dsBm.bmWidth=10000;ds.dsBm.bmHeight=10000;assert(!_viv_gl_texture_upload(7,&ds));
 _viv_gl_npot=0;ds.dsBm.bmWidth=3;ds.dsBm.bmHeight=5;assert(_viv_gl_texture_upload(8,&ds));assert(_viv_gl_last_pot_wide==4 && _viv_gl_last_pot_high==8);
 fail_alloc=1;ds.dsBm.bmWidth=2;ds.dsBm.bmHeight=2;assert(!_viv_gl_texture_upload(9,&ds));assert(!_viv_gl_stage_buf && !_viv_gl_stage_size);assert(!glGetError());
 puts("real Mesa GL: edge pixels, 24/32bpp NPOT/POT uploads, shrink, byte refusal and allocation refusal PASS; not Windows/WGL");
 free(_viv_gl_stage_buf);eglMakeCurrent(display,NULL,NULL,NULL);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
}
