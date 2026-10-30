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
/* PRODUCTION */
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
