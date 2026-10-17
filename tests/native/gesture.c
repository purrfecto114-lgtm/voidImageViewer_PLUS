#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t DWORD;
typedef unsigned char BYTE;
typedef void *HWND;
typedef long long __int64;
typedef struct { int left, top, right, bottom; } RECT;
typedef struct { int x, y; } POINT;
typedef struct {
    unsigned cbSize, dwFlags, dwID;
    struct { short x, y; } ptsLocation;
    uint64_t ullArguments;
} os_GestureInfo_t;
#define FALSE 0
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
static int os_logical_wide = 96, _viv_1to1, _viv_zoom_pos;
static int _viv_view_x, _viv_view_y, _viv_doing, captured;
static int _viv_is_fullscreen, config_fullscreen_fill_window, config_fill_window;
static int config_keep_aspect_ratio = 1, config_allow_shrinking = 1;
static struct { int image_wide, image_high; } _viv_slot_current;
static float _viv_zoom_scales[_VIV_ZOOM_MAX];
static int client_wide = 1600, client_high = 1000;
static HWND _viv_hwnd;
static int closed, invalidated, status_updates, navigated;
static uintptr_t message_extra;
static os_GestureInfo_t next_info;
static int get_info(void *handle, os_GestureInfo_t *info) { *info = next_info; return 1; }
static int close_info(void *handle) { closed++; return 1; }
static int (*os_GetGestureInfo)(void *, os_GestureInfo_t *) = get_info;
static int (*os_CloseGestureInfoHandle)(void *) = close_info;
static int GetSystemMetrics(int which) { return 4096; }
static uintptr_t GetMessageExtraInfo(void) { return message_extra; }
static int GetClientRect(HWND hwnd, RECT *rect) { *rect = (RECT){0, 0, client_wide, client_high}; return 1; }
static int ScreenToClient(HWND hwnd, POINT *point) { return 1; }
static void os_zero_memory(void *p, size_t size) { memset(p, 0, size); }
static int _viv_get_status_high(void) { return 0; }
static int _viv_get_view_top(void) { return 0; }
static void _viv_next(int prev, int reset, int preload, int wait) { navigated += prev ? -1 : 1; }
static void _viv_view_scroll(int x, int y) { _viv_view_x -= x; _viv_view_y -= y; }
static void _viv_view_set(int x, int y, int update) { _viv_view_x = x; _viv_view_y = y; }
static void InvalidateRect(HWND hwnd, void *rect, int erase) { invalidated++; }
static void _viv_status_update_temp_pos_zoom(void) { status_updates++; }
static void _viv_doing_cancel(void) { _viv_doing = captured = 0; }

/* PRODUCTION */

static int send(unsigned id, unsigned flags, unsigned distance)
{
    next_info.dwID = id; next_info.dwFlags = flags; next_info.ullArguments = distance;
    return _viv_on_gesture(NULL, NULL);
}
static void geometry(int iw, int ih, int cw, int ch, int fill)
{
    _viv_slot_current.image_wide = iw; _viv_slot_current.image_high = ih;
    client_wide = cw; client_high = ch;
    os_logical_wide = 96;
    config_fill_window = fill; config_allow_shrinking = 1;
    config_keep_aspect_ratio = 1; _viv_is_fullscreen = config_fullscreen_fill_window = 0;
    _viv_1to1 = _viv_zoom_pos = _viv_view_x = _viv_view_y = _viv_doing = 0;
    invalidated = status_updates = navigated = 0;
    _viv_gesture_reset();
}
static void policy(void)
{
    int dpi;
    for (dpi = 96; dpi <= 288; dpi += 96)
    {
        unsigned floor = 36 * dpi / 96;
        os_logical_wide = dpi;
        _viv_gesture_reset();
        _viv_gesture_zoom_ratio = 1.009;
        assert(send(3, 1, floor - 1) == 1);
        assert(_viv_gesture_zoom_dist == 0 && _viv_gesture_zoom_ratio == 1.0);
        _viv_zoom_pos = 0;
        send(3, 0, floor); /* first valid sample establishes a baseline */
        assert(!_viv_zoom_pos && _viv_gesture_zoom_dist == floor);
        send(3, 0, floor * 11 / 10);
        assert(_viv_zoom_pos > 0);
        _viv_gesture_zoom_ratio = 1.008;
        send(3, 0, floor - 1);
        assert(_viv_gesture_zoom_ratio == 1.0);
    }
    _viv_doing = captured = 1;
    closed = 0;
    assert(send(1, 0, 0) == 0); /* DefWindowProc owns BEGIN and END handles */
    assert(!_viv_doing && !captured && !closed);
    assert(send(2, 0, 0) == 0 && !closed);
    assert(send(999, 0, 0) == 0 && !closed);
    assert(send(3, 1, 100) == 1 && closed == 1);

    geometry(4000, 3000, 1600, 1000, 0);
    send(3, 1, 20); send(3, 0, 30);
    assert(!_viv_zoom_pos); /* current policy: sub-floor input is frozen */
    send(3, 0, 100); assert(!_viv_zoom_pos);
    send(3, 0, 150);
    assert(_viv_zoom_pos >= 40 && _viv_zoom_pos <= 41); /* a real 1.5x distance change */
    puts("gesture policy: floor/rebase, ratio, DPI, drag cancellation and handle ownership passed");
}
/* A linear walk through the real renderer is independent of the binary
   search used by the action. The native image's longer axis avoids a
   one-pixel width hiding a large change in a tall image (and vice versa). */
static int first_position(int direction)
{
    int pos, first = 0, found = 0, rw, rh;
    _viv_1to1 = 0;
    for (pos = _viv_zoom_pos_floor(); pos <= _viv_zoom_pos_max(); pos++)
    {
        int actual, native;
        _viv_zoom_pos = pos;
        _viv_get_render_size(&rw, &rh);
        actual = _viv_slot_current.image_wide >= _viv_slot_current.image_high ? rw : rh;
        native = _viv_slot_current.image_wide >= _viv_slot_current.image_high ? _viv_slot_current.image_wide : _viv_slot_current.image_high;
        if ((direction > 0 && actual > native) || (direction < 0 && actual < native))
        {
            first = pos; found = 1;
            if (direction > 0) break;
        }
    }
    assert(found);
    return first;
}
static void zoom(void)
{
    const int images[][5] = {
        {4000, 3000, 1600, 1000, 0},
        {800, 600, 1600, 1200, 1}, /* fit is larger than 1:1 */
        {1, 4000, 1600, 1000, 0},
        {4000, 1, 1600, 1000, 0},
    };
    unsigned i;
    int direction, notch;
    for (i = 0; i < sizeof(images) / sizeof(images[0]); i++)
        for (direction = -1; direction <= 1; direction += 2)
            for (notch = 0; notch <= 1; notch++)
            {
                int steps = notch ? _VIV_ZOOM_STEPS_PER_NOTCH : 1;
                int delta = notch ? 120 : (120 + _VIV_ZOOM_STEPS_PER_NOTCH - 1) / _VIV_ZOOM_STEPS_PER_NOTCH;
                int expected;
                geometry(images[i][0], images[i][1], images[i][2], images[i][3], images[i][4]);
                expected = _viv_clamp_zoom_pos(first_position(direction) + direction * (steps - 1));
                _viv_1to1 = 1;
                _viv_zoom_pos = expected; /* stale position must not suppress redraw */
                _viv_do_mousewheel_action(0, direction * delta, client_wide / 2, client_high / 2);
                fprintf(stderr, "1:1 image %u direction %d steps %d: position %d expected %d\n", i, direction, steps, _viv_zoom_pos, expected);
                assert(!_viv_1to1 && _viv_zoom_pos == expected);
                assert(invalidated == 1 && status_updates == 1);
            }
    geometry(4000, 3000, 1600, 1000, 0);
    _viv_do_mousewheel_action(0, 120, 800, 500);
    assert(_viv_zoom_pos == _VIV_ZOOM_STEPS_PER_NOTCH);
    _viv_do_mousewheel_action(0, -120, 800, 500);
    assert(!_viv_zoom_pos);
    puts("zoom: 1:1 single/notch steps, below-fit mapping, thin images and redraw passed");
}
static void zero(void)
{
    geometry(4000, 3000, 1600, 1000, 0);
    _viv_1to1 = 1; _viv_zoom_pos = 42;
    _viv_do_mousewheel_action(0, 0, 800, 500);
    assert(_viv_1to1 && _viv_zoom_pos == 42 && !invalidated && !status_updates);
    _viv_do_mousewheel_action(1, 0, 800, 500);
    assert(!navigated);
    _viv_do_mousewheel_action(2, 0, 800, 500);
    assert(!navigated);
    puts("zoom: zero wheel delta is a no-op");
}
static void begin(void)
{
    const unsigned invalid[] = {0, 9000};
    unsigned i;
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
    {
        geometry(4000, 3000, 1600, 1000, 0);
        send(3, 1, 1000); send(3, 0, 1100);
        _viv_gesture_zoom_ratio = 1.009;
        send(3, 1, invalid[i]);
        assert(!_viv_gesture_zoom_dist && _viv_gesture_zoom_ratio == 1.0);
        invalidated = 0;
        send(3, 0, 100); /* the new gesture cannot reuse the old distance */
        assert(!invalidated && _viv_gesture_zoom_dist == 100);
    }
    puts("gesture: invalid per-type BEGIN cannot keep a stale baseline");
}
static void touch(void)
{
    message_extra = 0; assert(!_viv_is_touch_click());
    message_extra = 0xff515701u; assert(!_viv_is_touch_click()); /* pen */
    message_extra = 0xff515780u; assert(_viv_is_touch_click());
    message_extra = 0xff5157ffu; assert(_viv_is_touch_click());
    message_extra = 0x12340080u; assert(!_viv_is_touch_click());
    puts("input: mouse, pen and promoted touch signatures passed");
}
static void wheel(void)
{
    const int parts[] = {1, 12, 40};
    int direction;
    unsigned p;
    for (direction = -1; direction <= 1; direction += 2)
        for (p = 0; p < sizeof(parts) / sizeof(parts[0]); p++)
        {
            int i, expected, part = parts[p];
            geometry(4000, 3000, 1600, 1000, 0);
            expected = direction * _VIV_ZOOM_STEPS_PER_NOTCH;
            for (i = 0; i < 120 / part; i++)
                _viv_do_mousewheel_action(0, direction * part, 800, 500);
            fprintf(stderr, "wheel direction %d partition %d: position %d expected %d\n", direction, part, _viv_zoom_pos, expected);
            assert(_viv_zoom_pos == expected);
        }
    geometry(4000, 3000, 1600, 1000, 0);
    _viv_do_mousewheel_action(0, 1, 800, 500);
    _viv_do_mousewheel_action(0, -1, 800, 500);
    assert(!_viv_zoom_pos && !invalidated);
    puts("wheel: partition-independent scaling and sub-step reversal passed");
}
static void bounds(void)
{
    int shrink;
    for (shrink = 0; shrink <= 1; shrink++)
    {
        geometry(1, 1, 1000, 1000, 1);
        config_allow_shrinking = shrink;
        _viv_1to1 = 1; _viv_zoom_pos = 42;
        _viv_do_mousewheel_action(0, -120, 500, 500);
        assert(_viv_1to1 && _viv_zoom_pos == 42 && !invalidated);
    }
    geometry(800, 600, 1600, 1200, 1);
    config_allow_shrinking = 0;
    _viv_1to1 = 1; _viv_zoom_pos = 42;
    _viv_do_mousewheel_action(0, -120, 800, 600);
    assert(_viv_1to1 && _viv_zoom_pos == 42 && !invalidated);
    geometry(4000, 1, 1600, 1000, 1);
    config_keep_aspect_ratio = 0;
    _viv_1to1 = 1; _viv_zoom_pos = 42;
    _viv_do_mousewheel_action(0, 120, 800, 500);
    assert(_viv_1to1 && _viv_zoom_pos == 42 && !invalidated);
    puts("zoom: an unavailable ladder size cannot reverse the requested direction");
}
int main(int argc, char **argv)
{
    const char *selected = argc > 1 ? argv[1] : "";
    init_scales();
    geometry(4000, 3000, 1600, 1000, 0);
    if (!*selected || !strcmp(selected, "policy")) policy();
    if (!*selected || !strcmp(selected, "zoom")) zoom();
    if (!*selected || !strcmp(selected, "zero")) zero();
    if (!*selected || !strcmp(selected, "begin")) begin();
    if (!*selected || !strcmp(selected, "touch")) touch();
    if (!*selected || !strcmp(selected, "wheel")) wheel();
    if (!*selected || !strcmp(selected, "bounds")) bounds();
    return 0;
}
