#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef unsigned char utf8_t;
typedef struct { const char *values[CONFIG_RECENT_FILE_COUNT]; } ini_t;
static wchar_t *config_recent_files[CONFIG_RECENT_FILE_COUNT];
static int config_recent_file_count;
static int allocated, save_calls, menu_calls;

static wchar_t *string_alloc(const wchar_t *text)
{
    size_t size = (wcslen(text) + 1) * sizeof(wchar_t);
    wchar_t *copy = malloc(size);
    assert(copy);
    allocated++;
    return memcpy(copy, text, size);
}
static wchar_t *string_alloc_utf8(const utf8_t *text)
{
    /* Fixtures are ASCII; Windows UTF-8 conversion is outside this test. */
    wchar_t buffer[128];
    size_t i;
    for (i = 0; text[i]; ++i) { assert(i < 127); buffer[i] = text[i]; }
    buffer[i] = 0;
    return string_alloc(buffer);
}
static void mem_free(void *ptr) { assert(ptr); allocated--; free(ptr); }
static void os_move_memory(void *d, const void *s, size_t size) { memmove(d, s, size); }
static void _viv_recent_save_defer(void) { save_calls++; }
static void _viv_recent_menu_update(void) { menu_calls++; }
static const utf8_t *ini_get_string(ini_t *ini, const utf8_t *key)
{
    int index = atoi((const char *)key + strlen("recent_file_"));
    assert(index >= 0 && index < CONFIG_RECENT_FILE_COUNT);
    return (const utf8_t *)ini->values[index];
}

/* PRODUCTION */

static void expect(int count, const wchar_t **paths)
{
    int i;
    assert(config_recent_file_count == count);
    for (i = 0; i < count; ++i) assert(wcscmp(config_recent_files[i], paths[i]) == 0);
    for (i = count; i < CONFIG_RECENT_FILE_COUNT; ++i) assert(!config_recent_files[i]);
    assert(allocated == count);
}
static void reset(void)
{
    _viv_recent_file_clear();
    assert(!allocated);
    save_calls = menu_calls = 0;
}
static void rename_tests(void)
{
    const wchar_t *head[] = {L"B", L"C"};
    const wchar_t *middle[] = {L"Y", L"X", L"B", L"C"};
    const wchar_t *unchanged[] = {L"A", L"B", L"C"};
    _viv_recent_file_push(L"C"); _viv_recent_file_push(L"B"); _viv_recent_file_push(L"A");
    _viv_recent_file_rename(L"A", L"B");
    expect(2, head);
    reset();
    _viv_recent_file_push(L"C"); _viv_recent_file_push(L"A");
    _viv_recent_file_push(L"X"); _viv_recent_file_push(L"B"); _viv_recent_file_push(L"Y");
    /* There is another comparison after the aliased target is removed. */
    _viv_recent_file_rename(L"A", config_recent_files[1]);
    expect(4, middle);
    reset();
    _viv_recent_file_push(L"C"); _viv_recent_file_push(L"B"); _viv_recent_file_push(L"A");
    _viv_recent_file_rename(L"missing", L"B");
    expect(3, unchanged);
    assert(save_calls == 3 && menu_calls == 3);
    _viv_recent_file_rename(L"A", L"a");
    assert(config_recent_file_count == 3 && wcscmp(config_recent_files[0], L"a") == 0);
    _viv_recent_file_push(L"B");
    assert(config_recent_file_count == 3 && wcscmp(config_recent_files[0], L"B") == 0);
    _viv_recent_file_remove_filename(L"b");
    assert(config_recent_file_count == 2);
    reset();
}
static void load_tests(void)
{
    ini_t ini = {{"A", "a", "", "B", NULL, "C"}};
    ini_t overlay = {{"b", "D", "A"}};
    const wchar_t *first[] = {L"A", L"B", L"C"};
    const wchar_t *second[] = {L"A", L"B", L"C", L"D"};
    int i;
    load_recent(&ini);
    expect(3, first);
    assert(!save_calls && !menu_calls); /* config load must not mutate menus/timers */
    load_recent(&overlay);
    expect(4, second);
    reset();
    for (i = 0; i < CONFIG_RECENT_FILE_COUNT + 3; ++i)
    {
        wchar_t name[32];
        swprintf(name, 32, L"file-%d", i);
        _viv_recent_file_push(name);
    }
    assert(config_recent_file_count == CONFIG_RECENT_FILE_COUNT);
    assert(allocated == CONFIG_RECENT_FILE_COUNT);
    reset();
}
int main(int argc, char **argv)
{
    if (argc == 1 || strcmp(argv[1], "rename") == 0) rename_tests();
    if (argc == 1 || strcmp(argv[1], "load") == 0) load_tests();
    puts("MRU: rename collision, alias, load holes/duplicates, overlay, capacity and ownership passed");
    return 0;
}
