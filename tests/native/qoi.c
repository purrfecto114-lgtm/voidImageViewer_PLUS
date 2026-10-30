// qoi decoder host harness: real production bodies, fake win32 edges.
//
// the adapters below stand in for the win32 surface qoi_load touches -
// the hglobal-backed stream, the interlocked stage marker, the debug
// allocator macros and the pixel budget family. everything between
// the production marker and main() bracket what is extracted
// src/safe_size.c at every run by tests/qoi_decode_test.py, so the
// bytes under test are the working tree's own.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t DWORD;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uintptr_t SIZE_T;
typedef void *PVOID;
typedef void *HGLOBAL;
typedef void *IStream;
typedef long HRESULT;
typedef int BOOL;

#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define TRUE 1

// the stream under test is a plain memory block; the hglobal facade
// hands qoi_load the same bytes the real stream would.
static const BYTE *test_stream_data;
static SIZE_T test_stream_size;

// knobs the hostile cases turn: the canvas ceiling, the cooperative
// cancel flag, and a terminate-at-chunk counter.
static SIZE_T test_budget_ceiling = 100000000;
static SIZE_T test_working_set_ceiling = 1200000000;
static int test_cancel_flag;
static int test_cancel_after_chunks = -1;
static int test_chunks_read;

// the loader's stage marker and the interlocked write that sets it.
static PVOID _viv_load_stage;
static PVOID InterlockedExchangePointer(PVOID volatile *target, PVOID value)
{
    PVOID old = *target;
    *target = value;
    return old;
}
#define _VIV_LOAD_TERMINATED() (test_cancel_flag || \
    (test_cancel_after_chunks >= 0 && ++test_chunks_read > test_cancel_after_chunks))

// the pixel budget family: both gates the real viv_load.c carries,
// ceiling and working set, driven by the test knobs.
int _viv_pixel_budget_refused(SIZE_T pixels, SIZE_T ceiling)
{
    (void)ceiling;
    if (pixels > test_budget_ceiling)
    {
        return 1;
    }
    if ((unsigned long long)pixels * 12 > (unsigned long long)test_working_set_ceiling)
    {
        return 1;
    }
    return 0;
}

// the debug allocator: plain malloc with an outstanding count, so a
// leaked decode buffer fails the suite instead of vanishing.
static long mem_outstanding;
#define mem_alloc(size) host_alloc(size)
#define mem_free(ptr) host_free(ptr)
static void *host_alloc(SIZE_T size)
{
    void *p = malloc(size);
    assert(p);
    mem_outstanding++;
    return p;
}
static void host_free(void *ptr)
{
    if (ptr)
    {
        mem_outstanding--;
        free(ptr);
    }
}
#define VIV_MAX_IMAGE_PIXELS 100000000
static void os_zero_memory(void *data, SIZE_T size)
{
    memset(data, 0, size);
}
#define debug_printf(...) ((void)0)

// the hglobal-backed stream facade.
static HRESULT GetHGlobalFromStream(IStream *stream, HGLOBAL *hglobal)
{
    (void)stream;
    *hglobal = (HGLOBAL)&test_stream_data;
    return 0;
}
static void *GlobalLock(HGLOBAL hglobal)
{
    (void)hglobal;
    return (void *)test_stream_data;
}
static SIZE_T GlobalSize(HGLOBAL hglobal)
{
    (void)hglobal;
    return test_stream_size;
}
static BOOL GlobalUnlock(HGLOBAL hglobal)
{
    (void)hglobal;
    return TRUE;
}

/* PRODUCTION */

// ---------------------------------------------------------------------------
// byte stream construction: the encoder side of the format, in the same
// wire dialect the decoder reads (big endian header, chunk tags).
static BYTE wire[4096];
static SIZE_T wire_len;

static void wire_reset(void)
{
    wire_len = 0;
    test_stream_data = wire;
    test_stream_size = 0; // set by wire_done
    test_chunks_read = 0;
    test_cancel_after_chunks = -1;
    test_cancel_flag = 0;
}

static void wire_u32be(DWORD v)
{
    wire[wire_len++] = (BYTE)(v >> 24);
    wire[wire_len++] = (BYTE)(v >> 16);
    wire[wire_len++] = (BYTE)(v >> 8);
    wire[wire_len++] = (BYTE)v;
}

static void wire_header(DWORD wide, DWORD high, BYTE channels)
{
    wire_u32be(0x716f6966); // "qoif"
    wire_u32be(wide);
    wire_u32be(high);
    wire[wire_len++] = channels;
    wire[wire_len++] = 0; // srgb colorspace
}

static void wire_byte(BYTE b)
{
    wire[wire_len++] = b;
}

static void wire_rgb(BYTE r, BYTE g, BYTE b)
{
    wire[wire_len++] = 0xfe;
    wire[wire_len++] = r;
    wire[wire_len++] = g;
    wire[wire_len++] = b;
}

static void wire_rgba(BYTE r, BYTE g, BYTE b, BYTE a)
{
    wire[wire_len++] = 0xff;
    wire[wire_len++] = r;
    wire[wire_len++] = g;
    wire[wire_len++] = b;
    wire[wire_len++] = a;
}

static int qoi_hash(BYTE r, BYTE g, BYTE b, BYTE a)
{
    return ((r * 3) + (g * 5) + (b * 7) + (a * 11)) & 63;
}

static void wire_index(BYTE r, BYTE g, BYTE b, BYTE a)
{
    wire[wire_len++] = (BYTE)(qoi_hash(r, g, b, a) & 0x3f);
}

static void wire_run(int repeats)
{
    // tag 0b11: run-1 repeats of the running pixel.
    wire[wire_len++] = (BYTE)(0xc0 | (repeats - 1));
}

static void wire_diff(int dr, int dg, int db)
{
    wire[wire_len++] = (BYTE)(0x40 | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2));
}

static void wire_luma(int vg, int dr, int db)
{
    wire[wire_len++] = (BYTE)(0x80 | (vg + 32));
    wire[wire_len++] = (BYTE)(((dr - vg + 8) << 4) | (db - vg + 8));
}

static void wire_done(void)
{
    static const BYTE marker[8] = {0, 0, 0, 0, 0, 0, 0, 1};
    memcpy(wire + wire_len, marker, 8);
    wire_len += 8;
    test_stream_size = wire_len;
}

// ---------------------------------------------------------------------------
// decode result capture.
static int info_calls, frame_calls;
static DWORD got_wide, got_high, got_frame_count;
static int got_alpha;
static BYTE got_pixels[4096];

static int info_callback(void *user_data, DWORD frame_count, DWORD wide, DWORD high, int has_alpha)
{
    (void)user_data;
    info_calls++;
    got_frame_count = frame_count;
    got_wide = wide;
    got_high = high;
    got_alpha = has_alpha;
    return 1;
}

static int frame_callback(void *user_data, BYTE *pixels, int delay)
{
    (void)user_data;
    (void)delay;
    frame_calls++;
    memcpy(got_pixels, pixels, 4096);
    return 1;
}

static int decode_current(void)
{
    info_calls = frame_calls = 0;
    got_wide = got_high = got_frame_count = 0;
    got_alpha = 0;
    memset(got_pixels, 0xcc, sizeof(got_pixels));
    return qoi_load((IStream)&test_stream_data, NULL, info_callback, frame_callback);
}

static int pixel_is(int n, BYTE r, BYTE g, BYTE b, BYTE a)
{
    return got_pixels[n * 4 + 0] == r && got_pixels[n * 4 + 1] == g &&
           got_pixels[n * 4 + 2] == b && got_pixels[n * 4 + 3] == a;
}

static void expect_refused(const char *name)
{
    int ret = decode_current();
    if (ret || info_calls || frame_calls)
    {
        fprintf(stderr, "FAIL %s: decoder accepted a hostile stream (ret=%d info=%d frame=%d)\n",
                name, ret, info_calls, frame_calls);
        exit(1);
    }
    printf("ok  %s\n", name);
}

static void expect_fail(const char *name)
{
    expect_refused(name);
}

int main(void)
{
    // --- happy paths -----------------------------------------------------
    wire_reset();
    wire_header(1, 1, 3);
    wire_rgb(10, 20, 30);
    wire_done();
    if (!decode_current() || !frame_calls || !pixel_is(0, 10, 20, 30, 255) || got_alpha)
    {
        fprintf(stderr, "FAIL rgb_single\n");
        return 1;
    }
    printf("ok  rgb_single\n");

    wire_reset();
    wire_header(1, 1, 4);
    wire_rgba(1, 2, 3, 4);
    wire_done();
    if (!decode_current() || !pixel_is(0, 1, 2, 3, 4) || !got_alpha)
    {
        fprintf(stderr, "FAIL rgba_single\n");
        return 1;
    }
    printf("ok  rgba_single\n");

    wire_reset();
    wire_header(1, 4, 3);
    wire_rgba(9, 9, 9, 128); // rgb stream pins alpha at 255 below
    wire_run(3);
    wire_done();
    if (!decode_current() || got_alpha)
    {
        // ch3 stream: the rgba chunk set the running alpha to 128, but the
        // write site pins d[3] to 255 - has_alpha must stay 0.
        fprintf(stderr, "FAIL three_channel_alpha_pinned (alpha=%d)\n", got_alpha);
        return 1;
    }
    if (!pixel_is(0, 9, 9, 9, 255) || !pixel_is(3, 9, 9, 9, 255))
    {
        fprintf(stderr, "FAIL three_channel_alpha_pinned pixels\n");
        return 1;
    }
    printf("ok  three_channel_alpha_pinned\n");

    wire_reset();
    wire_header(2, 1, 3);
    wire_rgb(100, 100, 100);
    wire_diff(1, 0, -1);
    wire_done();
    if (!decode_current() || !pixel_is(1, 101, 100, 99, 255))
    {
        fprintf(stderr, "FAIL diff_path\n");
        return 1;
    }
    printf("ok  diff_path\n");

    wire_reset();
    wire_header(2, 1, 3);
    wire_rgb(100, 100, 100);
    wire_luma(5, 3, -2);
    wire_done();
    if (!decode_current() || !pixel_is(1, 103, 105, 98, 255))
    {
        fprintf(stderr, "FAIL luma_path\n");
        return 1;
    }
    printf("ok  luma_path\n");

    wire_reset();
    wire_header(2, 1, 3);
    wire_rgb(5, 6, 7);
    wire_index(5, 6, 7, 255);
    wire_done();
    if (!decode_current() || !pixel_is(1, 5, 6, 7, 255))
    {
        fprintf(stderr, "FAIL index_path\n");
        return 1;
    }
    printf("ok  index_path\n");

    // four distinct pixels across two rows: row advance is just d += 4.
    wire_reset();
    wire_header(2, 2, 4);
    wire_rgba(1, 1, 1, 255);
    wire_rgba(2, 2, 2, 255);
    wire_rgba(3, 3, 3, 254);
    wire_rgba(4, 4, 4, 255);
    wire_done();
    if (!decode_current() || !pixel_is(0, 1, 1, 1, 255) || !pixel_is(1, 2, 2, 2, 255) ||
        !pixel_is(2, 3, 3, 3, 254) || !pixel_is(3, 4, 4, 4, 255) || !got_alpha ||
        got_wide != 2 || got_high != 2 || got_frame_count != 1)
    {
        fprintf(stderr, "FAIL two_rows\n");
        return 1;
    }
    printf("ok  two_rows\n");

    // reference leniency: chunks past a full canvas are ignored.
    wire_reset();
    wire_header(1, 1, 3);
    wire_rgb(8, 8, 8);
    wire_rgb(99, 99, 99); // never read: canvas is full
    wire_done();
    if (!decode_current() || !pixel_is(0, 8, 8, 8, 255))
    {
        fprintf(stderr, "FAIL trailing_chunks_ignored\n");
        return 1;
    }
    printf("ok  trailing_chunks_ignored\n");

    // a run longer than the canvas must stop at d_end (asan/ubsan guard).
    wire_reset();
    wire_header(1, 1, 3);
    wire_rgb(7, 7, 7);
    wire_run(62);
    wire_done();
    if (!decode_current() || !pixel_is(0, 7, 7, 7, 255))
    {
        fprintf(stderr, "FAIL run_overrun_guard\n");
        return 1;
    }
    printf("ok  run_overrun_guard\n");

    // --- hostile streams -------------------------------------------------
    wire_reset();
    wire_header(1, 1, 3);
    wire_rgb(1, 2, 3);
    wire_done();
    wire[0] = 'x';
    expect_refused("bad_magic");

    wire_reset();
    wire_header(1, 1, 5); // channels outside 3..4
    wire_rgb(1, 2, 3);
    wire_done();
    expect_refused("channels_5");

    wire_reset();
    wire_header(1, 1, 3);
    wire_rgb(1, 2, 3);
    wire_done();
    wire[13] = 2; // colorspace outside 0..1
    expect_refused("colorspace_2");

    wire_reset();
    wire_header(0, 1, 3);
    wire_rgb(1, 2, 3);
    wire_done();
    expect_refused("zero_width");

    wire_reset();
    wire_header(1, 0, 3);
    wire_rgb(1, 2, 3);
    wire_done();
    expect_refused("zero_height");

    wire_reset();
    wire_header(1, 1, 3);
    wire_rgb(1, 2, 3);
    wire_done();
    test_stream_size = 20; // shorter than header + end marker
    expect_refused("too_short");

    wire_reset();
    wire_header(1, 1, 3);
    wire_rgb(1, 2, 3);
    wire_done();
    wire[wire_len - 1] = 0; // corrupted end marker
    expect_refused("no_end_marker");

    wire_reset();
    wire_header(1, 1, 3);
    wire_rgb(1, 2, 3);
    wire_done();
    test_stream_size = wire_len - 3; // rgb payload cut
    expect_refused("truncated_rgb");

    // rgb payload leaning on the end marker: only r and g precede the
    // marker, so the third byte would read into it. the marker itself
    // stays aligned - only the payload bound refuses this stream.
    wire_reset();
    wire_header(1, 1, 3);
    wire_byte(0xfe);
    wire_byte(1);
    wire_byte(2);
    wire_done();
    expect_refused("rgb_payload_boundary");

    wire_reset();
    wire_header(2, 1, 3);
    wire_rgb(1, 2, 3);
    wire_luma(5, 3, -2);
    wire_done();
    test_stream_size = wire_len - 9; // luma's second byte cut
    expect_refused("truncated_luma");

    wire_reset();
    wire_header(4, 1, 3);
    wire_rgb(1, 2, 3);
    wire_done(); // canvas needs 4 pixels, stream supplies 1
    expect_refused("input_exhausted");

    wire_reset();
    wire_header(2, 1, 3);
    wire_rgb(1, 2, 3);
    wire_rgb(4, 5, 6);
    wire_done();
    test_budget_ceiling = 1; // 2 pixels over a 1 pixel ceiling
    expect_refused("pixel_budget_refused");
    test_budget_ceiling = 100000000;

    // working set gate: a mid-size canvas over a tiny working set ceiling.
    wire_reset();
    wire_header(4, 1, 3);
    wire_rgb(1, 2, 3);
    wire_run(3);
    wire_done();
    test_working_set_ceiling = 1;
    expect_refused("working_set_refused");
    test_working_set_ceiling = 1200000000;

    // cooperative cancel mid-decode: no callbacks, no partial delivery.
    wire_reset();
    wire_header(4, 1, 3);
    wire_rgb(1, 2, 3);
    wire_run(3);
    wire_done();
    test_cancel_after_chunks = 1;
    expect_refused("cancel_terminates");

    if (mem_outstanding)
    {
        fprintf(stderr, "FAIL leak: %ld allocations outstanding\n", mem_outstanding);
        return 1;
    }
    printf("ok  no_leaks\n");

    printf("qoi host suite: all cases passed\n");
    return 0;
}
