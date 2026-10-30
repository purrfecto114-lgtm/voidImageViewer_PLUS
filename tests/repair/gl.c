typedef float GLfloat;
#define GL_PROJECTION 1
#define GL_MODELVIEW 2
#define GL_COLOR_BUFFER_BIT 3
#define GL_TEXTURE_2D 4
#define GL_QUADS 5
#define GL_NO_ERROR 0
static int _viv_gl_failed;int _viv_gl_export_filled;
static HBITMAP _viv_gl_last_hbitmap;
static intptr_t _viv_gl_context=1;
static float _viv_gl_u=1,_viv_gl_v=1;
static BYTE *_viv_gl_export_bits;
static unsigned _viv_gl_geterror(void){return 0;}
static int _viv_gl_procs(void){return 1;}
static int _viv_gl_context_create(HWND h,HDC d){return 1;}
static int _viv_gl_wglMakeCurrent(HDC d,intptr_t c){return 1;}
static int _viv_gl_texture_upload(HBITMAP h,DIBSECTION*d){return 1;}
static void _viv_gl_viewport(int a,int b,int c,int d){}
static void _viv_gl_matrixmode(int a){}
static void _viv_gl_loadidentity(void){}
static void _viv_gl_ortho(double a,double b,double c,double d,double e,double f){}
static void _viv_gl_clearcolor(float a,float b,float c,float d){}
static void _viv_gl_clear(int a){}
static void _viv_gl_enable(int a){}
static void _viv_gl_begin(int a){}
static void _viv_gl_texcoord2f(float a,float b){}
static void _viv_gl_vertex2f(float a,float b){}
static void _viv_gl_end(void){}
static void _viv_gl_disable(int a){}
static void _viv_gl_export_readback(int a,int b){}
static int swap_calls;
static BOOL SwapBuffers(HDC d){swap_calls++;return 0;}
/* PRODUCTION */
int main(void){assert(!_viv_hwgl_render(1,2,3,0,0,4,4,0));assert(swap_calls==1);puts("GL failed swap permits GDI fallback PASS");}
