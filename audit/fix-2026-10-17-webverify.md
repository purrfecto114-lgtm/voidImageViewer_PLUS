# Web verification — 2026-10-17

Scope: official Microsoft Learn docs for the Win32 APIs the 3 fix lanes are about to use.
Project baseline verified locally: `src\viv.h:49` → `#define _WIN32_WINNT 0x0501` (XP SDK baseline),
`src\viv.h:45` → `#define _WIN32_IE 0x0501`. Existing house style for newer APIs is self-declaring the
prototype rather than relying on SDK gating (`src\os.h:255-257`).

---

## ⚠ TOP FINDINGS THAT CHANGE LANE WORK

1. **FIND_FIRST_EX_LARGE_FETCH is NOT supported on XP, Server 2003, Vista or Server 2008.**
   It is Windows 7 / Server 2008 R2 and later only. It compiles fine at `_WIN32_WINNT=0x0501`
   (it is a plain `minwinbase.h` constant, not gated), so it is a *silent runtime* hazard on the
   project's declared minimum OS. LANE-A must version-gate it at the call site.
   Also: Microsoft documents **nothing** about what happens when the flag is passed to an
   unsupported OS — no "fails with X", no "flag is ignored". Treat as UNDEFINED, not as
   "gracefully degrades". (item 2)

2. **`SetTimer(hwnd, id, 0, NULL)` does NOT silently never fire.** `uElapse` below
   `USER_TIMER_MINIMUM` is *clamped to 10 ms*, and `WM_TIMER` is posted. The claimed bug
   ("rate=0 runs but never advances because no WM_TIMER arrives") is **wrong per the docs**.
   A 0-interval slideshow therefore hammers the message loop at ~100 Hz and *does* advance
   frames — the correct fix is to stop arming the timer, not to interpret 0 as "disabled".
   (item 7)

3. **`CopyFileW` + `FlushFileBuffers(handle_to_dst)` is not implementable as written.**
   `CopyFileW` returns only a `BOOL` — it opens, copies and **closes** the destination itself,
   so no destination handle exists to flush afterwards, and `CopyFileW` provides no atomicity
   guarantee whatsoever. Use write-temp + `MoveFileEx(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)`
   (or `ReplaceFileW`). (item 3)

4. **Deleting a timer-queue timer from inside its own callback with a blocking wait deadlocks.**
   Documented explicitly. (item 4)

---

## 1. `CommandLineToArgvW` (shell32) — CONFIRMED

- **Signature:** `LPWSTR * CommandLineToArgvW([in] LPCWSTR lpCmdLine, [out] int *pNumArgs);`
- **Min client:** Windows 2000 Professional / Windows XP (desktop apps only). Exists on Win7 —
  confirmed, DLL is `Shell32.dll` version 6.0 or later, library `Shell32.lib`.
- **Quote (ownership + correct free):**
  > "CommandLineToArgvW allocates a block of contiguous memory for pointers to the argument
  > strings, and for the argument strings themselves; the calling application must free the memory
  > used by the argument list when it is no longer needed. To free the memory, use a single call to
  > the LocalFree function."
- **Vet of the caller pattern (what fixers must not get wrong):**
  - `LocalFree(argv)` on the *returned pointer* is correct. **`HeapFree` / `free()` / per-string
    frees are all wrong** — it is ONE contiguous block.
  - The Microsoft example frees with `LocalFree(szArglist)` and nothing else; each `argv[i]` must
    **not** be freed individually.
  - Failure returns `NULL` (not `INVALID_HANDLE_VALUE`); check for NULL.
  - Empty-string input "returns the path to the current executable file" — an edge case for
    validation code.
  - Freeing via `LocalFree` is required even for a 1-argument command line.
  - Linking note: `Shell32.lib` (the pattern must survive link in whatever build file is used).
- **URL:** https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw

---

## 2. `FindFirstFileExW` + `FIND_FIRST_EX_LARGE_FETCH` — PARTIAL (⚠ flag not WinXP/WinVista)

- **Signature:**
  ```c
  HANDLE FindFirstFileExW(LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId,
                          LPVOID lpFindFileData, FINDEX_SEARCH_OPS fSearchOp,
                          LPVOID lpSearchFilter, DWORD dwAdditionalFlags);
  ```
- **Min client for the function:** Windows XP [desktop apps | UWP apps] — so **yes, it is available
  at `_WIN32_WINNT=0x0501`** and needs no header workaround. Server: Server 2003.
- **⚠ Min OS for the FLAG itself:**
  > "FIND_FIRST_EX_LARGE_FETCH — 2: Uses a larger buffer for directory queries, which can increase
  > performance of the find operation. **Windows Server 2008, Windows Vista, Windows Server 2003 and
  > Windows XP: This value is not supported until Windows Server 2008 R2 and Windows 7.**"
- **Safe on Win7 SP1?** YES — it is supported from Windows 7 (no KB/pack dependency is mentioned
  anywhere in the docs; the gate is the OS version itself, not a hotfix). So the correct guard is
  a plain `IsWindows7OrGreater()`-style version check, *not* a KB check.
- **What happens on an unsupported OS? UNCONFIRMED / undefined.** The docs only say the value "is
  not supported"; they never state that the call fails, and never state that the flag is ignored.
  The only `ERROR_NOT_SUPPORTED` sentence in the page is about *filtering* support, not flags:
  > "If the underlying file system does not support the specified type of filtering, other than
  > directory filtering, FindFirstFileEx fails with the error ERROR_NOT_SUPPORTED."
  **Lane guidance: do not rely on graceful degradation — pass 0 for the flags on XP/Vista.**
- **Other traps that must not be got wrong (same page):**
  - `lpFileName` must **not** end in a trailing backslash: "An attempt to open a search with a
    trailing backslash (\) always fails." To list a directory you must append `\*`.
  - Failure returns `INVALID_HANDLE_VALUE` (**not NULL**).
  - `lpSearchFilter` **must be NULL** with current `fSearchOp` values.
  - Docs claim the doc paragraph equivalence with `FindFirstFile` only for flags == 0.
- **URL:** https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw

---

## 3. Atomic backup write: `CopyFileW` vs `ReplaceFileW` vs `MoveFileEx` — PARTIAL

- `CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists)` — min
  client Windows XP; `winbase.h`, `Kernel32.lib`. **`bFailIfExists=FALSE` means "overwrites the
  existing file and succeeds"** — i.e. it destroys the good old copy first, so a crash mid-copy
  leaves a **torn** destination. This is the opposite of the required semantics.
- **`CopyFileW` + `FlushFileBuffers(handle_to_dst)` cannot be written as specified:** the function
  returns only `BOOL` and closes the destination itself; no destination handle is ever handed to
  the caller. `FlushFileBuffers` requires "a handle to the open file" with `GENERIC_WRITE`
  (https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers).
- **Better idiom #1 — `ReplaceFileW`** (min client Windows XP, same volume requirement):
  > "An application can call **ReplaceFile** instead of calling separate functions to save the data
  > to a new file, rename the original file using a temporary name, rename the new file to have the
  > same name as the original file, and delete the original file."
  > "The replacement file assumes the name of the replaced file and its identity."
  URL: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew
  - **⚠ trap:** `REPLACEFILE_WRITE_THROUGH` — "This value is not supported." You cannot ask
    `ReplaceFile` to flush; you must `FlushFileBuffers` the *temp* file yourself before calling it.
  - `REPLACEFILE_IGNORE_ACL_ERRORS` needs `_WIN32_WINNT >= 0x0600` — **not available at this
    project's 0x0501**. Use `REPLACEFILE_IGNORE_MERGE_ERRORS` instead, or nothing.
  - "The backup file, replaced file, and replacement file must all reside on the same volume."
- **Better idiom #2 — `MoveFileExW` + write-through** (min client Windows XP):
  > "MOVEFILE_WRITE_THROUGH — 8: The function does not return until the file is actually moved on
  > the disk. Setting this value guarantees that a move performed as a copy and delete operation is
  > flushed to disk before the function returns."
  Combine with `MOVEFILE_REPLACE_EXISTING`. Same-volume rename, which is the atomic step; the
  write-through covers the cross-volume `MOVEFILE_COPY_ALLOWED` fallback.
  URL: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw
- **Microsoft's own canonical recipe** (temp file, close, then `MoveFileEx(... ,
  MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)`):
  https://learn.microsoft.com/en-us/windows/win32/fileio/creating-and-using-a-temporary-file
- **Verdict:** the "either fully-old or fully-new" guarantee is **not documented for `CopyFileW`**
  (no atomicity claim exists on that page) and is only approximated by the rename step of
  `ReplaceFile`/`MoveFileEx`. **Unconfirmed** that any Win32 API gives you a documented
  crash-atomic swap. Recommended shape: write `X.tmp` in the *same directory*, `FlushFileBuffers`,
  close, then `MoveFileEx(..., REPLACE_EXISTING|WRITE_THROUGH)` (or `ReplaceFileW`), then delete the
  `.tmp` on any failure.

---

## 4. `CreateTimerQueueTimer` / `DeleteTimerQueueTimer` — CONFIRMED (with a deadlock trap)

- **Signatures (both min client Windows XP; compile with `_WIN32_WINNT >= 0x0500`, so OK here):**
  ```c
  BOOL CreateTimerQueueTimer(PHANDLE phNewTimer, HANDLE TimerQueue,
                             WAITORTIMERCALLBACK Callback, PVOID Parameter,
                             DWORD DueTime, DWORD Period, ULONG Flags);
  BOOL DeleteTimerQueueTimer(HANDLE TimerQueue, HANDLE Timer, HANDLE CompletionEvent);
  ```
  URLs: https://learn.microsoft.com/en-us/windows/win32/api/threadpoollegacyapiset/nf-threadpoollegacyapiset-createtimerqueuetimer
        https://learn.microsoft.com/en-us/windows/win32/api/threadpoollegacyapiset/nf-threadpoollegacyapiset-deletetimerqueuetimer
- **Deleting from the creating thread:** **no thread-affinity rule exists.** The callback is
  dispatched to a *thread-pool worker*, never to the creator, so pause/resume handled on the UI
  thread can call `DeleteTimerQueueTimer` safely. The only documented restriction is
  "This function cannot be called while the thread is using impersonation. The resulting behavior
  is undefined."
- **⚠ Deleting from the timer's own callback:**
  > "Furthermore, you cannot make a blocking deletion call on a timer associated with the callback."
  and "a deadlock may occur if two callback functions attempt a blocking DeleteTimerQueueTimer call
  on each others' timers" / "If the timer being deleted was created with
  WT_EXECUTEINPERSISTENTTHREAD, a deadlock may occur."
- **The `NULL` / blocking pattern — quote:**
  > "If this parameter is **INVALID_HANDLE_VALUE**, the function waits for any running timer callback
  > functions to complete before returning.
  > If this parameter is **NULL**, the function marks the timer for deletion and returns immediately.
  > If the timer has already expired, the timer callback function will run to completion. However,
  > there is no notification sent when the timer callback function has completed. Most callers
  > should not use this option, and should wait for running timer callback functions to complete so
  > they can perform any needed cleanup."
  and
  > "If there are outstanding callback functions and CompletionEvent is NULL, the function will fail
  > and set the error code to **ERROR_IO_PENDING**. ... For any other error, you should retry the
  > call." / "If the error code is ERROR_IO_PENDING, it is not necessary to call this function again."
  - **Verdict on the proposed "NULL + try once, then block":** the *documented* consequence of the
    NULL attempt is that it **FAILS with `ERROR_IO_PENDING`** when a callback is in flight — it is
    not a free "try". Handle three cases: (a) success → done; (b) `ERROR_IO_PENDING` → an
    in-flight callback still owns your state, so wait on **your own** completion event/flag (do not
    re-call expecting it to block); (c) any other error → retry. From a thread that is *not* the
    callback, `INVALID_HANDLE_VALUE` is the blocking variant.
- **Race to watch for re-create on resume:** a periodic timer's callbacks can overlap
  ("The callback is called every time the period elapses, whether or not the previous callback has
  finished executing"), so a 1 ms timer can have several callbacks queued at once. Use a
  generation/stop-flag under a lock so a stale callback cannot post after resume, and remember
  `Period = 0` means one-shot, not disabled.
- **Timer drift caveat if the lane also touches timing accuracy:** "These threads are subject to
  scheduling delays, so the timing can vary depending on what else is happening in the application
  or the system."

---

## 5. `PeekMessage` / `MsgWaitForMultipleObjects`

Skipped as instructed.

---

## 6. Reset-to-defaults confirmation pattern — CONFIRMED

Reference pattern (Microsoft Windows UX guidelines, "Confirmations"):
> "A confirmation is a modal dialog box that asks if the user wants to proceed with an action."
> "**Default response for a confirmation is based on its design pattern:** Routine confirmations →
> Proceed. Risky action confirmations → **Don't proceed (or the safe choice).** Unintended
> consequence confirmations → If consequences are significant, don't proceed; otherwise, proceed."
> "Is there a clear reason not to proceed and a reasonable chance that sometimes users won't?"
> "Do users perform the action frequently? If so, consider an alternative design. Frequent
> confirmations are annoying and have little value because users learn to respond without thinking."

URL: https://learn.microsoft.com/en-us/windows/win32/uxguide/mess-confirm
(companion checklist: https://learn.microsoft.com/en-us/windows/win32/uxguide/top-violations)

**How to apply to a "Reset all settings" button:** losing user customisation is a *significant,
though recoverable-in-effort* consequence, not data loss — treat it as a routine/risky confirm:
ask a yes/no question, make the safe choice (**Cancel / No**) the default button, and place the
destructive affirmative action second. Caveat: the page carries a Microsoft banner — "This design
guide was created for Windows 7 and has not been updated for newer versions of Windows" — so cite it
as the platform convention, not as current Fluent guidance.

---

## 7. `SetTimer` with `uElapse = 0` — UNCONFIRMED as a bug (⚠ claim is wrong)

- **Signature:** `UINT_PTR SetTimer([in, optional] HWND hWnd, [in] UINT_PTR nIDEvent,
  [in] UINT uElapse, [in, optional] TIMERPROC lpTimerFunc);` — min client Windows 2000 Professional.
- **The one line that decides it:**
  > "If *uElapse* is less than **USER_TIMER_MINIMUM (0x0000000A)**, the timeout is set to
  > **USER_TIMER_MINIMUM**."
- **Consequence:** `uElapse == 0` does **not** disable the timer and does **not** make `SetTimer`
  fail ("If the function fails to create a timer, the return value is zero" — a 0 interval is not
  a documented failure). The timer fires every **10 ms** and, with `lpTimerFunc == NULL`, the
  system "posts a WM_TIMER message to the application queue".
- **Verdict on the claimed bug:** **UNCONFIRMED / interpretation is inaccurate.** A slideshow
  configured with rate=0 will *not* "run but never advance"; it will advance ~100 frames/second.
  Any lane patching this as "no WM_TIMER arrives at rate 0" is fixing a non-existent symptom — the
  real defect (if any) is an unthrottled 100 Hz timer loop, which must be fixed by **not arming the
  timer** (or guarding the handler), not by special-casing 0 as "off".
- URL: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer
  (related: https://learn.microsoft.com/en-us/windows/win32/winmsg/using-timers)

---

## 8. DPI ladder: `SetProcessDPIAware` → `SetProcessDpiAwarenessContext` — CONFIRMED

- `SetProcessDPIAware()` — min client **Windows Vista**; `BOOL`, no parameters; "Sets the
  process-default DPI awareness to system-DPI awareness."
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setprocessdpiaware
- `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT value)` — min client **Windows 10, version
  1703** (Server 2016); `BOOL SetProcessDpiAwarenessContext(value)`; it is "a more advanced version
  of the previously existing SetProcessDpiAwareness API" and is the only way to get **Per Monitor v2**.
  https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setprocessdpiawarenesscontext
- **Ladder verdict:** legitimate. Dynamic-resolve `SetProcessDpiAwarenessContext` at runtime and
  fall back to `SetProcessDPIAware()` when absent — the fallback is exactly `SYSTEM_AWARE`
  semantics ("This is equivalent to calling SetProcessDpiAwarenessContext with a value of
  DPI_AWARENESS_CONTEXT_SYSTEM_AWARE"), so there is no behavioural cliff.
- **Traps:** both are **one-shot per process** — "any future calls to this API will fail" with
  `ERROR_ACCESS_DENIED` if awareness was already set; must be called before creating any UI; and
  both docs repeat "It is recommended that you set the process-default DPI awareness via application
  manifest, not an API call" — for a Win32 app that ships a manifest, the manifest is the better
  mechanism and this ladder is the compatibility fallback. `_WIN32_WINNT=0x0501` also means
  `DPI_AWARENESS_CONTEXT` / `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` are not declared by
  older SDK headers — self-declare the pointer typedef (house style, `src\os.h:255-257`).

---

## 9. `\\?\` long-path prefix — CONFIRMED (and slash normalization IS required)

- **One clear official citation, normalization + rules:**
  > "File I/O functions in the Windows API convert '/' to '\' as part of converting the name to an
  > NT-style name, **except when using the '\\?\' prefix** ... These prefixes are not used as part
  > of the path itself. They indicate that the path should be passed to the system with minimal
  > modification, which means that **you cannot use forward slashes to represent path separators,
  > or a period to represent the current directory, or double dots to represent the parent
  > directory**. Because you cannot use the '\\?\' prefix with a relative path, relative paths are
  > always limited to a total of MAX_PATH characters."
  URL: https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
- **Namespace semantics:**
  > "For file I/O, the '\\?\' prefix to a path string tells the Windows APIs to disable all string
  > parsing and to send the string that follows it straight to the file system."
  > "Many but not all file I/O APIs support '\\?\'; you should look at the reference topic for each
  > API to be sure."
  > "Note that Unicode APIs should be used to make sure the '\\?\' prefix allows you to exceed the
  > MAX_PATH."
  URL: https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
- **Per-API confirmation that the four functions in question accept `\\?\` (all quote the same
  sentence, "By default, the name is limited to MAX_PATH characters. To extend this limit to 32,767
  wide characters, prepend '\\?\' to the path."):**
  - `CreateFileW` / `GetFileAttributesExW` — https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileattributesexw
  - `CopyFileW` — https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfilew
  - `MoveFileExW`, `ReplaceFileW` — see item 3 URLs
  - `FindFirstFileExW` / `FindFirstFileW` — https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw
- **⚠ traps the fixers must not get wrong:**
  1. **Normalize `/` → `\` before prepending.** After the prefix, `/` is a legal *filename*
     character, not a separator — `\\?\C:/dir/file` does not mean `C:\dir\file`.
  2. `..` and `.` are **not** collapsed for you after the prefix. Resolve to a fully qualified,
     canonical path *first*.
  3. **Cannot be combined with a relative path** — call `GetFullPathNameW` first.
  4. UNC needs its own prefix: `\\?\UNC\server\share`, not `\\?\\server\share`.
  5. `\\?\` cannot reach the **root directory**: "Prepending the string '\\?\' does not allow access
     to the root directory."
  6. For `FindFirstFileExW`, the trailing `\*` is still mandatory (see item 2).
  7. No Unicode normalization of the path is needed — the file system treats names as opaque
     `WCHAR` sequences (same page).
  8. On Windows 10 1607+ an alternative exists (manifest `longPathAware` + `LongPathsEnabled`
     registry), but that requires an admin-level registry change and does nothing for XP/Win7
     users, so the `\\?\` route is the correct one for this project's support matrix.
