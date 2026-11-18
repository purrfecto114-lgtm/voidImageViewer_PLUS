#ifndef VIV_HWD3D_MODERN_H
#define VIV_HWD3D_MODERN_H

HMODULE _viv_gpu_system_dll(const wchar_t *name);
int _viv_modern_available(int renderer);
int _viv_modern_render(int renderer,int warp,HWND hwnd,HBITMAP bitmap,int wide,int high,
	int x,int y,int dst_wide,int dst_high,COLORREF color);
void _viv_modern_release_image(void);
void _viv_modern_shutdown(void);
void _viv_modern_export_begin(BYTE *bits,int wide,int high);
int _viv_modern_export_filled(void);
void _viv_modern_export_end(void);

#endif
