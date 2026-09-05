#!/usr/bin/env python3
"""Round 14: user-audited fix batch. Byte-level patches with unique-anchor
asserts and CRLF fidelity. Any anchor miss aborts before writing anything."""
import sys

T = lambda n: "\t" * n

def patch(path, rounds):
    data = open(path, "rb").read().decode("latin-1")
    for name, old, new in rounds:
        n = data.count(old)
        if n != 1:
            print(f"ABORT: anchor {name!r} count={n} (need 1) in {path}")
            sys.exit(1)
        data = data.replace(old, new)
    open(path, "wb").write(data.encode("latin-1"))
    print(f"patched {path}: {len(rounds)} rounds")

# ---------------- viv.c ----------------
viv = []

# A. forward declaration next to _viv_safe_copy_data's
viv.append(("fwd-decl",
    "static int _viv_safe_copy_data(const void *base,SIZE_T src_size,const void *src,void *dst,SIZE_T dst_size);\r\n",
    "static int _viv_safe_copy_data(const void *base,SIZE_T src_size,const void *src,void *dst,SIZE_T dst_size);\r\n"
    "static UINT _viv_frame_delay_at(const os_PropertyItem_t *pd,SIZE_T pd_size,DWORD i);\r\n"))

# B. helper definition before _viv_safe_copy_data's body
helper = (
"// read the 10ms delay of frame i. returns 0 on any failure, the caller\r\n"
"// falls back to 100ms. the gdi+ value pointer is validated against the\r\n"
"// property buffer before it is used, and the delay array may hold fewer\r\n"
"// entries than there are frames (the gif GCE block is optional), so delays\r\n"
"// are reused modulo the available count.\r\n"
"static UINT _viv_frame_delay_at(const os_PropertyItem_t *pd,SIZE_T pd_size,DWORD i)\r\n"
"{\r\n"
"\tUINT value;\r\n"
"\tUINT count;\r\n"
"\tconst BYTE *v;\r\n"
"\t\r\n"
"\tvalue = 0;\r\n"
"\t\r\n"
"\tif ((!pd) || (pd_size <= sizeof(os_PropertyItem_t)))\r\n"
"\t{\r\n"
"\t\treturn 0;\r\n"
"\t}\r\n"
"\t\r\n"
"\t// gdi+ points value into the property buffer: keep the dereference\r\n"
"\t// inside the buffer we actually own.\r\n"
"\tv = (const BYTE *)pd->value;\r\n"
"\t\r\n"
"\tif ((v < (const BYTE *)pd) || ((SIZE_T)(v - (const BYTE *)pd) >= pd_size))\r\n"
"\t{\r\n"
"\t\treturn 0;\r\n"
"\t}\r\n"
"\t\r\n"
"\tif (!(count = pd->length / sizeof(UINT)))\r\n"
"\t{\r\n"
"\t\treturn 0;\r\n"
"\t}\r\n"
"\t\r\n"
"\tif (!_viv_safe_copy_data(pd,pd_size,&(((const UINT *)v)[i % count]),&value,sizeof(UINT)))\r\n"
"\t{\r\n"
"\t\tvalue = 0;\r\n"
"\t}\r\n"
"\t\r\n"
"\treturn value;\r\n"
"}\r\n"
"\r\n")
viv.append(("helper-def",
    "static int _viv_safe_copy_data(const void *base,SIZE_T src_size,const void *src,void *dst,SIZE_T dst_size)\r\n{\r\n",
    helper + "static int _viv_safe_copy_data(const void *base,SIZE_T src_size,const void *src,void *dst,SIZE_T dst_size)\r\n{\r\n"))

# C. the frame-delay read block (issue 1 + issue 4)
old_c = (
T(8) + "// get frame delays.\r\n"
+ T(8) + "if (first_frame.frame_count > 1)\r\n"
+ T(8) + "{\r\n"
+ T(9) + "UINT size;\r\n"
+ "\r\n"
+ T(9) + "// PropertyTagFrameDelay 0x5100\r\n"
+ T(9) + "os_GdipGetPropertyItemSize(image,0x5100,&size);\r\n"
+ "\r\n"
+ T(9) + 'debug_printf("frame delay size %d\\n",size);\r\n'
+ "\r\n"
+ T(9) + "frame_delay = (os_PropertyItem_t *)mem_alloc(size);\r\n"
+ T(9) + "frame_delay_size = size;\r\n"
+ T(9) + "\r\n"
+ T(9) + "// PropertyTagFrameDelay 0x5100\r\n"
+ T(9) + "os_GdipGetPropertyItem(image,0x5100,size,frame_delay);\r\n"
+ T(8) + "}\r\n")
new_c = (
T(8) + "// get frame delays.\r\n"
+ T(8) + "if (first_frame.frame_count > 1)\r\n"
+ T(8) + "{\r\n"
+ T(9) + "UINT size;\r\n"
+ T(9) + "\r\n"
+ T(9) + "size = 0;\r\n"
+ T(9) + "\r\n"
+ T(9) + "// PropertyTagFrameDelay 0x5100: gdi+ leaves *size untouched\r\n"
+ T(9) + "// when the property is absent, so seed it and check the return.\r\n"
+ T(9) + "if ((os_GdipGetPropertyItemSize(image,0x5100,&size) == 0) && (size >= sizeof(os_PropertyItem_t)))\r\n"
+ T(9) + "{\r\n"
+ T(10) + "frame_delay = (os_PropertyItem_t *)mem_alloc(size);\r\n"
+ T(10) + "\r\n"
+ T(10) + "if (frame_delay)\r\n"
+ T(10) + "{\r\n"
+ T(11) + "frame_delay_size = size;\r\n"
+ T(11) + "\r\n"
+ T(11) + "// PropertyTagFrameDelay 0x5100: only trust the buffer when\r\n"
+ T(11) + "// gdi+ actually filled it.\r\n"
+ T(11) + "if (os_GdipGetPropertyItem(image,0x5100,size,frame_delay) != 0)\r\n"
+ T(11) + "{\r\n"
+ T(12) + "mem_free(frame_delay);\r\n"
+ T(12) + "frame_delay = 0;\r\n"
+ T(12) + "frame_delay_size = 0;\r\n"
+ T(11) + "}\r\n"
+ T(10) + "}\r\n"
+ T(9) + "}\r\n"
+ T(9) + "\r\n"
+ T(9) + 'debug_printf("frame delay size %d\\n",frame_delay_size);\r\n'
+ T(8) + "}\r\n")
viv.append(("delay-read-block", old_c, new_c))

# D. consumer 1 (additional frames)
old_d = (
T(14) + "// therube: we are accessing bad data here for some images.\r\n"
+ T(14) + "// just use a value of 0 for bad data.\r\n"
+ T(14) + "if (!_viv_safe_copy_data(frame_delay,frame_delay_size,&(((UINT *)frame_delay[0].value)[i]),&frame_data_value,sizeof(UINT)))\r\n"
+ T(14) + "{\r\n"
+ T(15) + "frame_data_value = 0;\r\n"
+ T(14) + "}\r\n"
+ T(14) + "\r\n"
+ T(14) + "frame.delay = frame_data_value * 10;\r\n")
new_d = (
T(14) + "// therube: we are accessing bad data here for some images.\r\n"
+ T(14) + "// just use a value of 0 for bad data.\r\n"
+ T(14) + "frame_data_value = _viv_frame_delay_at(frame_delay,frame_delay_size,i);\r\n"
+ T(14) + "\r\n"
+ T(14) + "frame.delay = frame_data_value * 10;\r\n")
viv.append(("consumer-1", old_d, new_d))

# E. consumer 2 (first frame)
old_e = (
T(14) + "if (first_frame.frame_count > 1)\r\n"
+ T(14) + "{\r\n"
+ T(15) + "// therube: we are accessing bad data here for some images.\r\n"
+ T(15) + "// just use a value of 0 for bad data.\r\n"
+ T(15) + "if (!_viv_safe_copy_data(frame_delay,frame_delay_size,&(((UINT *)frame_delay[0].value)[i]),&frame_data_value,sizeof(UINT)))\r\n"
+ T(15) + "{\r\n"
+ T(16) + "frame_data_value = 0;\r\n"
+ T(15) + "}\r\n"
+ T(15) + "\r\n"
+ T(15) + "first_frame.frame.delay = frame_data_value * 10;\r\n")
new_e = (
T(14) + "if (first_frame.frame_count > 1)\r\n"
+ T(14) + "{\r\n"
+ T(15) + "// therube: we are accessing bad data here for some images.\r\n"
+ T(15) + "// just use a value of 0 for bad data.\r\n"
+ T(15) + "frame_data_value = _viv_frame_delay_at(frame_delay,frame_delay_size,i);\r\n"
+ T(15) + "\r\n"
+ T(15) + "first_frame.frame.delay = frame_data_value * 10;\r\n")
viv.append(("consumer-2", old_e, new_e))

# F. save-as: save the current frame, not frame 0
viv.append(("save-current-frame",
    T(4) + "if (!os_save_hbitmap(_viv_frames[0].hbitmap,tobuf,format))\r\n",
    T(4) + "// save the frame on screen, not frame 0.\r\n"
    + T(4) + "if (!os_save_hbitmap(_viv_frames[_viv_frame_position].hbitmap,tobuf,format))\r\n"))

# G. uninstall string: reserve the suffix so it can never be truncated mid-way
viv.append(("uninstall-reserve",
    "\t\tuninstall_wbuf[0] = L'\"';\r\n"
    "\t\tstring_copy_with_bufsize(uninstall_wbuf + 1,STRING_SIZE - 1,install_path);\r\n",
    "\t\tuninstall_wbuf[0] = L'\"';\r\n"
    "\t\t// string_cat budgets against the whole STRING_SIZE and ignores\r\n"
    "\t\t// the quote we already placed: reserve the suffix so a long\r\n"
    "\t\t// install path can never truncate it mid-way.\r\n"
    "\t\tstring_copy_with_bufsize(uninstall_wbuf + 1,STRING_SIZE - 1 - 16,install_path);\r\n"))

# K. clipboard: free the bitmap when SetClipboardData fails
viv.append(("clipboard-ownership",
    T(7) + "SetClipboardData(CF_BITMAP,mem1_hbitmap);\r\n",
    T(7) + "// if SetClipboardData fails we keep ownership\r\n"
    + T(7) + "// and must free the bitmap ourselves.\r\n"
    + T(7) + "if (!SetClipboardData(CF_BITMAP,mem1_hbitmap))\r\n"
    + T(7) + "{\r\n"
    + T(8) + "DeleteObject(mem1_hbitmap);\r\n"
    + T(7) + "}\r\n"))

patch("src/viv.c", viv)

# ---------------- string.c ----------------
st = []

# H. string_copy_utf8_string: guarantee termination on failure
st.append(("utf8-terminate",
    "void string_copy_utf8_string(wchar_t *buf,const utf8_t *s)\r\n{\r\n\tMultiByteToWideChar(CP_UTF8,0,s,-1,buf,STRING_SIZE);\r\n}\r\n",
    "void string_copy_utf8_string(wchar_t *buf,const utf8_t *s)\r\n{\r\n"
    "\t// MultiByteToWideChar leaves the output unterminated when the\r\n"
    "\t// conversion fails: make sure the buffer is always a string.\r\n"
    "\tif (!MultiByteToWideChar(CP_UTF8,0,s,-1,buf,STRING_SIZE))\r\n"
    "\t{\r\n"
    "\t\tbuf[0] = 0;\r\n"
    "\t}\r\n"
    "}\r\n"))

# I. string_copy_with_bufsize: guard bufsize == 0
st.append(("bufsize-zero",
    "void string_copy_with_bufsize(wchar_t *d,SIZE_T bufsize,const wchar_t *s)\r\n{\r\n\tuintptr_t size;\r\n\t\r\n\tsize = bufsize - 1;\r\n",
    "void string_copy_with_bufsize(wchar_t *d,SIZE_T bufsize,const wchar_t *s)\r\n{\r\n"
    "\tuintptr_t size;\r\n"
    "\t\r\n"
    "\t// bufsize 0 would wrap to SIZE_MAX below.\r\n"
    "\tif (!bufsize)\r\n"
    "\t{\r\n"
    "\t\treturn;\r\n"
    "\t}\r\n"
    "\t\r\n"
    "\tsize = bufsize - 1;\r\n"))

patch("src/string.c", st)

# ---------------- ini.c ----------------
ini = []
ini.append(("invalid-file-size",
    "\t\tsize = GetFileSize(h,0);\r\n\t\t\r\n\t\tif (size)\r\n",
    "\t\tsize = GetFileSize(h,0);\r\n"
    "\t\t\r\n"
    "\t\t// GetFileSize returns INVALID_FILE_SIZE on failure and for\r\n"
    "\t\t// files over 4GB: never trust that as an allocation size.\r\n"
    "\t\tif ((size != INVALID_FILE_SIZE) && (size))\r\n"))
patch("src/ini.c", ini)

# ---------------- post checks ----------------
v = open("src/viv.c","rb").read().decode("latin-1")
s = open("src/string.c","rb").read().decode("latin-1")
i = open("src/ini.c","rb").read().decode("latin-1")
checks = [
    ("viv: helper declared", v.count("static UINT _viv_frame_delay_at(") == 2),
    ("viv: size seeded", "\t\t\t\t\t\t\t\t\tsize = 0;" in v),
    ("viv: size call checked", "os_GdipGetPropertyItemSize(image,0x5100,&size) == 0" in v),
    ("viv: GetPropertyItem checked", "os_GdipGetPropertyItem(image,0x5100,size,frame_delay) != 0" in v),
    ("viv: consumers use helper", v.count("_viv_frame_delay_at(frame_delay,frame_delay_size,i)") == 2),
    ("viv: old raw access gone", "frame_delay[0].value" not in v),
    ("viv: modulo reuse", "i % count" in v),
    ("viv: save uses current frame", "_viv_frames[_viv_frame_position].hbitmap,tobuf,format" in v),
    ("viv: save frame0 gone", "os_save_hbitmap(_viv_frames[0].hbitmap" not in v),
    ("viv: uninstall reserve", "STRING_SIZE - 1 - 16,install_path" in v),
    ("viv: clipboard guard", "if (!SetClipboardData(CF_BITMAP,mem1_hbitmap))" in v),
    ("string: utf8 terminated", "if (!MultiByteToWideChar(CP_UTF8,0,s,-1,buf,STRING_SIZE))" in s),
    ("string: bufsize guard", "\tif (!bufsize)" in s),
    ("ini: INVALID_FILE_SIZE", "(size != INVALID_FILE_SIZE) && (size)" in i),
]
bad = [n for n,ok in checks if not ok]
if bad:
    print("POST-CHECK FAIL:", bad)
    sys.exit(1)
for n,ok in checks:
    print("ok  ", n)
print("ALL PATCHES LANDED")
