typedef struct { wchar_t cFileName[16]; } WIN32_FIND_DATA;
typedef struct { int id; } _viv_frame_t;
typedef struct {
    WIN32_FIND_DATA fd;
    _viv_frame_t *frames;
    int frame_count,frame_loaded_count,image_wide,image_high;
    BYTE state;
    int alpha_baked;
} _viv_image_slot_t;

#define os_copy_memory memcpy
static int config_cache_count=3;
static int _viv_nav_folder_neighbor;
static int rotating,rotated,freed,first_frames,_viv_image_is_low_res;
static _viv_image_slot_t _viv_slot_current,_viv_slot_cache[3];
static WIN32_FIND_DATA current_fd;
static WIN32_FIND_DATA *_viv_current_fd=&current_fd;
static _viv_frame_t first,second,third;
static int _viv_rotation_busy(void) { return rotating; }
static int _viv_rotation_active(void) { return rotated; }
static void _viv_clear_frames(_viv_frame_t *frames,int count) {
    assert(count>0);
    freed++;
}
static void _viv_clear(void) {
    if (_viv_slot_current.frames) _viv_clear_frames(_viv_slot_current.frames,_viv_slot_current.frame_loaded_count);
    memset(&_viv_slot_current,0,sizeof(_viv_slot_current));
    rotating=rotated=_viv_image_is_low_res=0;
}
static void _viv_cache_set_trim(void) {}
static void _viv_start_first_frame(void) { first_frames++; }
static void _viv_process_pending_clear(void) {}
static void _viv_update_title(void) {}
static void _viv_status_update(void) {}

/* PRODUCTION */

static _viv_image_slot_t image(const wchar_t *name,_viv_frame_t *frames,int alpha) {
    _viv_image_slot_t slot={0};
    wcscpy(slot.fd.cFileName,name);
    slot.frames=frames;
    slot.frame_count=slot.frame_loaded_count=1;
    slot.image_wide=20; slot.image_high=10;
    slot.alpha_baked=alpha;
    return slot;
}

int main(void) {
    _viv_slot_current=image(L"one",&first,1);
    _viv_slot_current.state=7;
    _viv_slot_cache[0].state=11;
    viv_copy_current_image_to_last_image();
    assert(!_viv_slot_current.frames && !_viv_slot_current.alpha_baked && _viv_slot_current.state==7);
    assert(_viv_slot_cache[0].frames==&first && _viv_slot_cache[0].alpha_baked==1 && _viv_slot_cache[0].state==11);
    _viv_slot_clear_frames(&_viv_slot_cache[0]);
    assert(!_viv_slot_cache[0].alpha_baked && freed==1);

    for (int scenario=0; scenario<6; scenario++) {
        memset(_viv_slot_cache,0,sizeof(_viv_slot_cache));
        _viv_slot_cache[0]=image(L"two",&second,1);
        _viv_slot_cache[1]=image(L"three",&third,0);
        _viv_slot_current=image(L"one",&first,1);
        rotating=scenario==1;
        rotated=scenario==2;
        _viv_image_is_low_res=scenario==5;
        if (scenario==3) _viv_slot_current.frame_count=2;
        if (scenario==4) memset(&_viv_slot_current,0,sizeof(_viv_slot_current));
        freed=0;
        _viv_cache_activate(0);
        assert(_viv_slot_current.frames==&second && _viv_slot_current.alpha_baked &&
            !wcscmp(_viv_slot_current.fd.cFileName,L"two") && !wcscmp(current_fd.cFileName,L"two"));
        if (scenario==0) {
            assert(_viv_slot_cache[0].frames==&first && _viv_slot_cache[0].alpha_baked && !freed);
            assert(_viv_slot_cache[1].frames==&third && !_viv_slot_cache[2].frames);
        } else {
            assert(_viv_slot_cache[0].frames==&third && !_viv_slot_cache[1].frames);
            assert(freed==(scenario==4?0:1));
        }
    }
    assert(first_frames==6);
    puts("cache activation: no holes, alpha preserved, and rotated, partial or thumbnail frames stay out of cache PASS");
    return 0;
}
