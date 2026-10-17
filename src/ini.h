//
// Copyright 2025 voidtools / David Carpenter
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// load and sort ini files

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ini_s ini_t;

// the parse-anomaly classes ini_open records. the reader stays tolerant -
// it ignores what it does not understand and never refuses a line - but it
// now says what it saw, so the caller can decide what an anomaly means
// before it writes the file back. the bits are a mask: a file can trip
// more than one.
enum
{
    // nothing at all went wrong with the last parse. the reader reports
    // zero here, so "the mask is zero" and "the mask was never set" are
    // one answer, not two.
    INI_ANOMALY_NONE = 0,
    // the requested section header never appeared: whatever parsed came
    // from some other section, so every key the caller wanted fell
    // through to its compiled-in default.
    INI_ANOMALY_SECTION_MISSING = 1,
    // a non-blank line inside the section carried no '='.
    INI_ANOMALY_MALFORMED_LINE = 2,
    // the same key twice with two different values. the sort keeps the
    // last one (the ini contract); this only says the file disagreed
    // with itself, which is what a half-written or merged file looks
    // like. identical values are harmless and do not count.
    INI_ANOMALY_DUPLICATE_KEY = 4,
    // a nul byte inside the payload. the line walk stops at the first
    // nul, so every key after it is invisible to the reader and silently
    // keeps its default - the exact shape of the corruption this
    // reporting exists to catch.
    INI_ANOMALY_EMBEDDED_NUL = 8,
    // a key or a value past any real settings value (see _INI_MAX_FIELD
    // in ini.c). the value is still copied - the reader stays tolerant -
    // but the caller hears about it.
    INI_ANOMALY_OVERSIZED_VALUE = 16
};

// why the last ini_open found nothing (or found something). exactly one
// of these, held beside the anomaly mask above.
enum
{
    // the file opened and every line parsed cleanly.
    INI_LOAD_OK = 0,
    // the file is not there. a fresh profile answers this on every run,
    // so it is not a defect - but it must never bury a real answer from
    // the same call.
    INI_LOAD_FILE_MISSING = 1,
    // the file is there and could not be read: a refused share, a size
    // the reader refuses, a short read. "there but unreadable" is a
    // different answer from "not there" - the first is a file the user
    // must not lose, the second is nothing at all.
    INI_LOAD_UNREADABLE = 2
};

ini_t *ini_open(const wchar_t *filename,const utf8_t *ascii_section);
int ini_get_int(ini_t *ini,const utf8_t *key,int default_int_value);
const utf8_t *ini_get_string(ini_t *ini,const utf8_t *key);
void ini_close(ini_t *ini);

// the outcome of the last ini_open: INI_LOAD_OK / INI_LOAD_FILE_MISSING /
// INI_LOAD_UNREADABLE.
int ini_get_last_state(void);

// the anomaly mask of the last ini_open: zero, or one or more of the
// INI_ANOMALY_* bits.
int ini_get_last_anomaly(void);

// a cheap structural pre-check over the head of the file, asked BEFORE
// the tolerant parse runs: does the file open, carry the requested
// section header and stay free of embedded nul bytes? the settings loader
// uses the answer to decide whether the on-disk file is a safe source for
// its last-known-good backup - promoting a file that cannot be good would
// leave the restore offering the damage back as the good copy. returns 1
// when the file is a candidate, 0 otherwise.
int ini_precheck(const wchar_t *filename,const utf8_t *ascii_section);

#ifdef __cplusplus
}
#endif

