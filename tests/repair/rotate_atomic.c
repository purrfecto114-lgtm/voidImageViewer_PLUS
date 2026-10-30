typedef struct mip {int unused;} _viv_mipmap_t;
typedef struct {HBITMAP hbitmap;_viv_mipmap_t*mipmap;int delay;} _viv_frame_t;
static struct {struct {wchar_t cFileName[260];} fd;int frame_count,frame_loaded_count,image_wide,image_high;_viv_frame_t*frames;} _viv_slot_current;
static HWND _viv_hwnd=1;
static int _viv_view_x,_viv_view_y,shell_calls,old_deleted,new_deleted,oriented_calls,scenario,release_calls;
#define STRING_SIZE 260
#define LOCALIZATION_ID_ROTATE_IMAGE_CAPTION 1
#define LOCALIZATION_ID_ROTATE_IMAGE_MESSAGE 2
#define LOCALIZATION_ID_ROTATE_IMAGE_FAILED 3
#define MB_OKCANCEL 1
#define MB_ICONQUESTION 2
#define MB_OK 0
#define MB_ICONERROR 16
#define IDOK 1
#define VIV_MAX_ROTATION_BYTES (512u*1024u*1024u)
static const char*localization_get_string(int i){return "test";}
static void string_copy_utf8_string(wchar_t*d,const char*s){*d=0;}
static int viv_msgbox(HWND w,const wchar_t*c,const wchar_t*m,UINT f){return scenario==1?0:IDOK;}
static int os_shell_execute(HWND h,const wchar_t*p,int w,const char*v,const wchar_t*s){shell_calls++;return scenario!=2;}
static HBITMAP _viv_orientate_hbitmap(HBITMAP h,int o){int i=oriented_calls++;return scenario==0 && i==1?0:100+i;}
static BOOL DeleteObject(HBITMAP h){if(h<100)old_deleted++;else new_deleted++;return 1;}
static void _viv_mipmap_free(_viv_mipmap_t*m){assert(!m);}
static void _viv_view_set(int x,int y,int force){}
static void _viv_update_src_pixel(int force,int status){}
static void _viv_status_update(void){}
static BOOL InvalidateRect(HWND h,const void*r,BOOL e){return 1;}
static void _viv_hw_release_image(void){release_calls++;}
static void*mem_try_alloc(size_t n){return scenario==4?NULL:malloc(n);}
static void mem_free(void*p){free(p);}
static size_t safe_size_mul(size_t a,size_t b){return b && a>SIZE_MAX/b?SIZE_MAX:a*b;}
static size_t safe_size_add(size_t a,size_t b){return a>SIZE_MAX-b?SIZE_MAX:a+b;}
/* PRODUCTION */
int main(void){
 for(scenario=0;scenario<=4;scenario++){
  _viv_frame_t frames[2]={{7,0,100},{8,0,100}};shell_calls=old_deleted=new_deleted=oriented_calls=release_calls=0;
  _viv_slot_current.frames=frames;_viv_slot_current.frame_count=_viv_slot_current.frame_loaded_count=2;_viv_slot_current.image_wide=2;_viv_slot_current.image_high=3;wcscpy(_viv_slot_current.fd.cFileName,L"image.jpg");
  _viv_edit_rotate(0);
  if(scenario==3){assert(frames[0].hbitmap==100 && frames[1].hbitmap==101);assert(_viv_slot_current.image_wide==3 && _viv_slot_current.image_high==2);assert(shell_calls==1 && old_deleted==2 && new_deleted==0 && release_calls==1);}
  else{assert(frames[0].hbitmap==7 && frames[1].hbitmap==8);assert(_viv_slot_current.image_wide==2 && _viv_slot_current.image_high==3);assert(!old_deleted && !release_calls);
   if(scenario==0)assert(!shell_calls && new_deleted==1);
   if(scenario==1 || scenario==4)assert(!shell_calls && !new_deleted && !oriented_calls);
   if(scenario==2)assert(shell_calls==1 && new_deleted==2);
  }
 }
 puts("rotation cancel, allocation/transform/shell failure and successful atomic commit PASS");
}
