static intptr_t _viv_gl_context=42;
static unsigned _viv_gl_texture=3,_viv_gl_last_format=4;
static HBITMAP _viv_gl_last_hbitmap=7;
static int _viv_gl_last_pot_wide=4096,_viv_gl_last_pot_high=4096;
static BYTE *_viv_gl_stage_buf,*_viv_gl_export_bits;
static uintptr_t _viv_gl_stage_size=64;
static int unbound,deleted,freed;
static BOOL _viv_gl_wglMakeCurrent(HDC d,intptr_t r){assert(!d && !r);unbound++;return 1;}
static BOOL _viv_gl_wglDeleteContext(intptr_t r){assert(unbound && r==42);deleted++;return 1;}
static void mem_free(void*p){freed++;free(p);}
/* PRODUCTION */
int main(void){BYTE export_buffer[16];_viv_gl_export_bits=export_buffer;_viv_gl_stage_buf=malloc(64);_viv_hwgl_release_image();assert(unbound==1 && deleted==1 && freed==1);assert(!_viv_gl_context && !_viv_gl_texture && !_viv_gl_last_hbitmap && !_viv_gl_stage_buf && !_viv_gl_stage_size && !_viv_gl_last_pot_wide && !_viv_gl_last_pot_high && !_viv_gl_last_format);assert(_viv_gl_export_bits==export_buffer);_viv_hwgl_release_image();assert(deleted==1 && freed==1);puts("GL image teardown and export arming preservation PASS");}
