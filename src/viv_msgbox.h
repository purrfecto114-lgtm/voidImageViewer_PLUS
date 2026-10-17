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

// viv_msgbox.h - the themed message box.

#ifndef VIV_MSGBOX_H
#define VIV_MSGBOX_H

// the classic message box, on the theme tokens. type takes the MB_* subset
// the app uses (MB_OK / MB_OKCANCEL / MB_YESNO with an MB_ICON* tone) and
// the reply is the matching IDOK / IDCANCEL / IDYES / IDNO.
int viv_msgbox(HWND parent,const wchar_t *caption,const wchar_t *text,unsigned int type);

// the three-button form of the SAME box - the corrupted-settings ask at
// the exit needs three faces and the MessageBox subset above tops out at
// two. this does not grow a second dialog to carry them: the window
// class, the theme tokens, the focus ring, the accent fill and the modal
// pump are all the ones above.
//
// labels[] are the button faces in this box's own order: index 0 is the
// right most (the primary slot the two-button box gives its OK/YES), the
// last index is the left most. ids[] is the matching reply - the returned
// value is ids[default_index] until the user answers. default_index also
// carries the accent fill, exactly as the two-button box carries it on
// its own default, and escape answers ids[cancel_index] (the box has no
// "always 0" escape for a custom row: the caller names it).
int viv_msgbox3(HWND parent,const wchar_t *caption,const wchar_t *text,unsigned int type,const wchar_t *const *labels,const int *ids,int label_count,int default_index,int cancel_index);

#endif // VIV_MSGBOX_H
