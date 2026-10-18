/* the ini reader's parse core, read from the production tree and
 * executed host-side. the rc.1 field report's ask on every exit was
 * this walk running its "buffer terminator" malformed-line check
 * inside the character loop, where it read the first character of
 * every well-formed key line as a one-character non-blank span: any
 * settings file with a key line under the section answered
 * parse-anomaly, and the exit gate asked about it on every exit.
 *
 * the pin feeds the reader the legacy shapes (the forms the 1.1.16
 * writer emits - a minimal key line, blank lines before and between
 * key lines, lf-only endings, the double-lf file, empty values, a
 * slice of the real field-report file) and the hostile ones (a
 * genuinely malformed line, duplicate keys, an embedded nul, an
 * oversized field, a missing section), and asserts the verdicts and
 * the key/value pairs the walk produces.
 *
 * the semantic bodies (the blank test, the byte compare, the key
 * compare, the bom skip, the nul scan, the walk, the dedup) are the
 * production reader's own code. the allocation and sort layers below
 * are host adapters, the way the mru template adapts them: they are
 * not what this pin is about. */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char utf8_t;
typedef size_t SIZE_T;
typedef unsigned long DWORD;

/* the reader's file-static report, the way ini.c owns it. */
static int _ini_last_state;
static int _ini_last_anomaly;

/* host adapters for the layers the reader sits on. */
static int allocated;
static void *mem_alloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    assert(p);
    allocated++;
    return p;
}
static void mem_free(void *ptr)
{
    if (ptr) { allocated--; free(ptr); }
}
static utf8_t *utf8_alloc_utf8_n(const utf8_t *s, size_t size_in_bytes)
{
    utf8_t *p = mem_alloc(size_in_bytes + 1);
    memcpy(p, s, size_in_bytes);
    p[size_in_bytes] = 0;
    return p;
}
static size_t safe_size_mul_sizeof_pointer(size_t a) { return a * sizeof(void *); }
static int (*os_qsort_comp)(const void *, const void *);
static int os_qsort_shim(const void *a, const void *b)
{
    /* os_qsort's comparator receives the elements themselves; c's
     * qsort hands its comparator pointers to the elements. the deref
     * is the whole difference. */
    return os_qsort_comp(*(void * const *)a, *(void * const *)b);
}
static void os_qsort(void **base, size_t count, int (*comp)(const void *, const void *))
{
    if (count < 2) return;
    os_qsort_comp = comp;
    qsort(base, count, sizeof(void *), os_qsort_shim);
}

/* PRODUCTION */

/* ---- the scenarios ----
 *
 * every scenario: read a payload (a copy, the way ini_open reads a
 * file into its own buffer), assert the report, assert the key/value
 * pairs (or the absence of a key), release, and answer for every
 * allocation the parse made. */

static _ini_t *readn(const char *payload, size_t size)
{
    utf8_t *buf;
    _ini_t *result;
    buf = malloc(size + 1);
    assert(buf);
    memcpy(buf, payload, size);
    buf[size] = 0;             /* the terminator the reader writes */
    the_walk(buf, size, (const utf8_t *)"voidImageViewer");
    result = the_result();
    free(buf);
    return result;
}

static _ini_t *readz(const char *payload)
{
    return readn(payload, strlen(payload));
}

static const char *value_of(_ini_t *ini, const char *key)
{
    int i;
    if (!ini) return 0;
    for (i = 0; i < ini->count; i++)
        if (strcmp((const char *)ini->indexes[i]->key, key) == 0)
            return (const char *)ini->indexes[i]->value;
    return 0;
}

static void release(_ini_t *ini)
{
    int i;
    if (!ini)
    {
        assert(allocated == 0);
        return;
    }
    for (i = 0; i < ini->count; i++)
    {
        mem_free(ini->indexes[i]->key);
        mem_free(ini->indexes[i]->value);
        mem_free(ini->indexes[i]);
    }
    mem_free(ini->indexes);
    mem_free(ini);
    assert(allocated == 0);
}

static void scenarios_legacy_forms(void)
{
    _ini_t *ini;

    /* the minimal form the 1.1.16 writer emits: one key line under
     * the section, trailing crlf. this is the payload that answered
     * parse-anomaly before the walk's check moved out of the loop -
     * the rc.1 field report's ask on every exit. */
    ini = readz("[voidImageViewer]\r\nx=1\r\n");
    assert(_ini_last_state == INI_LOAD_OK);
    assert(_ini_last_anomaly == INI_ANOMALY_NONE);
    assert(ini && ini->count == 1);
    assert(value_of(ini, "x") && strcmp(value_of(ini, "x"), "1") == 0);
    release(ini);

    /* a legal blank line before a key line: no anomaly, and the key
     * is the key - the blank line's bytes do not fold into it. */
    ini = readz("[voidImageViewer]\r\n \r\nx=1\r\n");
    assert(_ini_last_anomaly == INI_ANOMALY_NONE);
    assert(ini && ini->count == 1);
    assert(value_of(ini, "x") && strcmp(value_of(ini, "x"), "1") == 0);
    assert(value_of(ini, "\r\nx") == 0);
    release(ini);

    /* a bare-lf blank between two crlf blanks - the trap shape that
     * used to double-flag a legal empty line. */
    ini = readz("[voidImageViewer]\r\nx=1\r\n\n\r\ny=2\r\n");
    assert(_ini_last_anomaly == INI_ANOMALY_NONE);
    assert(ini && ini->count == 2);
    assert(value_of(ini, "x") && strcmp(value_of(ini, "x"), "1") == 0);
    assert(value_of(ini, "y") && strcmp(value_of(ini, "y"), "2") == 0);
    release(ini);

    /* lf-only line endings read clean with the same keys. */
    ini = readz("[voidImageViewer]\nx=0\ny=2\n");
    assert(_ini_last_anomaly == INI_ANOMALY_NONE);
    assert(ini && ini->count == 2);
    assert(value_of(ini, "x") && strcmp(value_of(ini, "x"), "0") == 0);
    assert(value_of(ini, "y") && strcmp(value_of(ini, "y"), "2") == 0);
    release(ini);

    /* the double-lf file: every line followed by an empty line, byte
     * count preserved. the keys are the keys, not \n-prefixed ones. */
    ini = readz("[voidImageViewer]\n\nx=0\n\ny=2\n\n");
    assert(_ini_last_anomaly == INI_ANOMALY_NONE);
    assert(ini && ini->count == 2);
    assert(value_of(ini, "x") && strcmp(value_of(ini, "x"), "0") == 0);
    assert(value_of(ini, "y") && strcmp(value_of(ini, "y"), "2") == 0);
    assert(value_of(ini, "\nx") == 0);
    release(ini);

    /* a trailing blank line at the buffer terminator is legal. */
    ini = readz("[voidImageViewer]\r\n\r\n");
    assert(_ini_last_anomaly == INI_ANOMALY_NONE);
    release(ini);

    /* a bom header is skipped and the file reads clean. */
    ini = readz("\xef\xbb\xbf[voidImageViewer]\r\nx=1\r\n");
    assert(_ini_last_anomaly == INI_ANOMALY_NONE);
    assert(ini && ini->count == 1);
    assert(value_of(ini, "x") && strcmp(value_of(ini, "x"), "1") == 0);
    release(ini);

    /* the field report's file form, a slice of the real 1.1.16
     * output: window geometry, an empty-valued key, the real tail
     * keys. empty values are values. */
    ini = readz("[voidImageViewer]\r\nx=0\r\ny=0\r\nwide=1008\r\nhigh=630\r\n"
                "maximized=0\r\n"
                "animation_count_down_remaining_frames_keys=\r\n"
                "help_home_page_keys=\r\n"
                "help_donate_keys=\r\n"
                "help_about_keys=368\r\n");
    assert(_ini_last_state == INI_LOAD_OK);
    assert(_ini_last_anomaly == INI_ANOMALY_NONE);
    assert(ini && ini->count == 9);
    assert(value_of(ini, "wide") && strcmp(value_of(ini, "wide"), "1008") == 0);
    assert(value_of(ini, "animation_count_down_remaining_frames_keys") != 0);
    assert(*value_of(ini, "animation_count_down_remaining_frames_keys") == 0);
    assert(value_of(ini, "help_about_keys") && strcmp(value_of(ini, "help_about_keys"), "368") == 0);
    release(ini);
}

static void scenarios_hostile_forms(void)
{
    _ini_t *ini;

    /* a genuinely malformed line - no '=' before its end - reports
     * MALFORMED_LINE, and the line that follows parses as its own
     * key: the malformed line costs one line, not two. */
    ini = readz("[voidImageViewer]\r\ngarbage\r\nx=1\r\n");
    assert(_ini_last_anomaly == INI_ANOMALY_MALFORMED_LINE);
    assert(ini && ini->count == 1);
    assert(value_of(ini, "x") && strcmp(value_of(ini, "x"), "1") == 0);
    assert(value_of(ini, "garbage\r\nx") == 0);
    release(ini);

    /* the same class at the buffer terminator: an unterminated last
     * line without '=' is as malformed as any other. this is the
     * check's own case, the one that lived inside the loop. */
    ini = readz("[voidImageViewer]\r\ntrailing\r");
    assert(_ini_last_anomaly == INI_ANOMALY_MALFORMED_LINE);
    assert(ini == 0);
    release(ini);

    /* duplicate keys with different values: the file disagrees with
     * itself. the sort keeps the last one, and the report says so. */
    ini = readz("[voidImageViewer]\r\nk=1\r\nk=2\r\n");
    assert(_ini_last_anomaly == INI_ANOMALY_DUPLICATE_KEY);
    assert(ini && ini->count == 1);
    assert(value_of(ini, "k") && strcmp(value_of(ini, "k"), "2") == 0);
    release(ini);

    /* duplicate keys with identical values are harmless. */
    ini = readz("[voidImageViewer]\r\nk=1\r\nk=1\r\n");
    assert(_ini_last_anomaly == INI_ANOMALY_NONE);
    assert(ini && ini->count == 1);
    assert(value_of(ini, "k") && strcmp(value_of(ini, "k"), "1") == 0);
    release(ini);

    /* an embedded nul hides everything after it: the report says
     * EMBEDDED_NUL and the hidden key stays invisible. */
    {
        char payload[64];
        size_t n = 0;
        memcpy(payload, "[voidImageViewer]\r\nx=1\r\n", 24); n = 24;
        payload[n++] = 0;
        memcpy(payload + n, "y=2\r\n", 5); n += 5;
        ini = readn(payload, n);
        assert(_ini_last_anomaly == INI_ANOMALY_EMBEDDED_NUL);
        assert(ini && ini->count == 1);
        assert(value_of(ini, "x") && strcmp(value_of(ini, "x"), "1") == 0);
        assert(value_of(ini, "y") == 0);
        release(ini);
    }

    /* a field past any real settings value: the reader stays tolerant
     * - the value is still copied - but the caller hears about it. */
    {
        static char payload[70064];
        size_t n = 0;
        _ini_t *big;
        memcpy(payload, "[voidImageViewer]\r\nk=", 21); n = 21;
        memset(payload + n, 'a', 70000); n += 70000;
        memcpy(payload + n, "\r\n", 2); n += 2;
        big = readn(payload, n);
        assert(_ini_last_anomaly == INI_ANOMALY_OVERSIZED_VALUE);
        assert(big && big->count == 1);
        assert(value_of(big, "k") != 0 && strlen(value_of(big, "k")) == 70000);
        release(big);
    }

    /* the requested section never appeared: whatever parsed belonged
     * to some other section, and none of it leaks into ours. */
    ini = readz("[other]\r\nx=1\r\n");
    assert(_ini_last_anomaly == INI_ANOMALY_SECTION_MISSING);
    assert(ini == 0 || value_of(ini, "x") == 0);
    release(ini);

    /* a foreign section after ours does not leak keys into ours. */
    ini = readz("[voidImageViewer]\r\nx=1\r\n[other]\r\nz=3\r\n");
    assert(_ini_last_anomaly == INI_ANOMALY_NONE);
    assert(ini && ini->count == 1);
    assert(value_of(ini, "x") && strcmp(value_of(ini, "x"), "1") == 0);
    assert(value_of(ini, "z") == 0);
    release(ini);
}

int main(void)
{
    scenarios_legacy_forms();
    scenarios_hostile_forms();
    printf("ini reader walk: %d allocations answered, report ends at state %d anomaly %d\n",
           allocated, _ini_last_state, _ini_last_anomaly);
    return 0;
}
