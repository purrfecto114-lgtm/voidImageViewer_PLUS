# experience.md — the engineering logbook

This file is the sediment layer between the README and `Changes.txt`: the
README carries the current version only, `Changes.txt` is the archive of
record, and this file keeps what a round *taught* — the judgment calls, the
methods that worked, and the traps that were stepped in once and pinned
after.

## How this file is maintained

- **Every time an RC round ends** (the tag is pushed and the release
  workflow publishes), the round's description merges into the round
  archive below: a condensed narrative plus the round's own lessons.
- The lessons section grows by accrual, not by rewrite — an entry that
  stops being true gets a correction entry, never a silent deletion.
- The full per-version narrative stays in `Changes.txt`; this file
  condenses, it never duplicates.

## Lessons that outlived their rounds

### External reports and evidence

- **the house rule: every claim is re-verified against the tree before
  anything lands.** audits, user reports, even the maintainer's own
  yesterday-self. rc.3 landed ten one-liners and three navigation fixes
  only after re-verification confirmed them all; rc.6 took the user's own
  judged list and still re-verified each item before it moved — one item
  (the frames claim) failed verification and was answered with a
  refutation instead of a lock.
- **a refutation deserves the same evidence chain as a fix.** "not
  fixing" gets written down with its reasons the way a fix does: rc.5's
  thread-handle claim was factually right and harmfully wrong (the
  ownership chain is closed; the suggested poll-then-close belt was traced
  to a real regression window — a fast open could queue onto a closing
  loader), and rc.6's frames claim fell to the reply queue's by-value
  crossing (the loader thread never touches the slot). both refutations
  live in their changelog entries beside the fixes that did land.
- **a plausible number built on an unverified premise is more dangerous
  than no number at all.** rc.2 opened by retracting one: a bracket-stack
  reparse showed the line at depth 2 where it always was, in build 72, 74
  and 79 alike. structure before arithmetic — the guard suites run on
  that method, and the audits are held to it too.
- **a field report is a symptom, not a diagnosis.** "the corner won't
  drag" was two stacked structural facts — the status bar owns the client
  bottom so the parent's manual resize band can never see it, and comctl
  suppresses the native grip when the thick frame is off — not a broken
  resize handler. "the toolbar swallows buttons" was the overflow contract
  hiding whole groups (the full strip is wider than a default window at
  96 dpi), not a truncation bug. both fixes landed at the layer that owned
  the behavior, and both were found by mapping who owns every pixel
  before touching a handler.

### Guard-suite engineering

- **guards are code, and fresh guards bite their own authors.** the
  round-113 section's first run bit six times on its own pin phrasing
  (cross-line phrases, tab counts, a self-reference paradox); round-114's
  bit fifty-five times. every bite was fixed on the spot — weaken the
  pin, never the discipline. a guard suite that has never failed is a
  guard suite that has never run.
- **never carry verification truth in a bare `assert`.** `python3 -O`
  strips asserts silently: five zoom-math groups went blind under it (the
  conditions ride inside `check()` now), and one born-dead
  `assert m or True` had been masking a never-matching regex since the
  day it was written. if a condition matters, it lives in a check, not an
  assert.
- **extract constants, don't mirror them.** the suites hand-copied the
  ladder step 1.01 while extracting 278 and 1024 — a drift to 1.02 would
  have passed every suite and every golden hash untouched. rc.3 closed
  it: the step is extracted from the init walk and cross-checked against
  the shrink range it spans.
- **deleting code is half guard work.** the options-dialog retirement
  (rc.6) removed 1,400 lines of dialogs and, with them, twelve stale pins
  and five counter recalibrations. the deletion list and the
  pin-retirement list are written together, or the suite goes red on
  purpose.
- **delete the name, keep the number.** resource ids share numeric values
  with live controls (1020 was a dead tree view and a live start-menu row
  alike); the dead ids retired by name only, because renumbering live
  constants is its own regression.
- **a pinned window needs remeasuring every structural round.** the
  splice window has been recalibrated at least eleven times (30548 → 31129
  → 31138 → 31170 → 31410 → 31613 → 30523 → 30741 → 30961 → 31081 → 31118
  → 31188), each time with the
  measurement written into the comment. measure, then pin; never pin a
  hope.

### Equivalent refactors

- **equivalence is contextual, not textual.** the fullscreen toggle's
  1024-entry precomputation retired into two binary searches, and the
  searches had to run in the same geometry the old scans saw — one before
  the 1:1 flag cleared, one after the window resized — or the
  "equivalent" code answers a different question. the ladder's monotonic
  predicate is what makes the search valid at all; a non-monotonic ladder
  would need the scan.
- **a type beats a discipline.** nineteen loose globals across three
  image families worked only because every author enumerated all three at
  every gate; the typed slot made the enumeration a property of the type
  (rc.1). when a rule must be remembered at every call site, move the
  rule into the type.
- **pointer keys rot silently.** the nav cache keyed on the frame fd's
  address, stable since the file split — the pointer could never tell one
  file from the next, and the pane froze on the first file it saw. prefer
  keys with identity (names) over keys with stability (addresses).
- **pure moves need proof, not prose.** the split's "function bodies
  byte-identical" claim lived as commit-message prose until the
  conservation tool made it a re-runnable answer (290 old, 290 new, 290
  matched). a structural refactor runs the tool and pastes the output.

### Compilers and warnings

- **compiler warnings are free audit leads.** the message-box bug's three
  incompatible-return warnings sat in the warning census baseline for
  years — the bug was fixed the round someone finally read them instead
  of baselining them away (rc.6). a warning that survives into the
  baseline deserves one honest look before it earns tenure.
- **the baseline-tree diff method.** to attribute a warning delta: stash,
  rebuild the baseline revision in full, line-level diff the two
  censuses. rc.5 and rc.6 both used it to turn "+2 warnings" into "two
  new strings of the pointer-sign class the localization table has
  always generated."
- **editors rewrite bytes silently.** four recorded encounters, every one
  caught by accident of another suite pinning the damaged shape — the
  byte suite (1.1.14-rc.10) exists so there is no fifth unnoticed. the fifth
  came in the logbook round itself: the editor flattened three
  tab-aligned pin strings inside the menu suite, and the damaged guard
  went red on its own pin in the same round — caught by the guard it
  damaged, not by accident, which is the whole reason the suite exists.
  when a new guard bites the same round's own new code (the rc.5
  wide-copy pin caught a real bug in that round's fix), that is the
  system working.
- **watch the tools that watch.** the errors.log append-mode residue from
  a first failed compile poisoned rc.6's warning comparison until a
  single-file rebuild disproved the outlier. measurement pipelines need
  their own hygiene checks.

### Interface judgment

- **the screen is a constraint before the cap is.** the shortcut editor's
  flat list of 118 commands is taller than any display; raising the
  32-row cap would only move the wall. the fix was the cascade — the
  real menu tree walked, one page per menu, no page taller than a screen
  (rc.4).
- **audit features as pairs, not points.** Copy Filename wrote
  `CF_UNICODETEXT` and Paste read images only — an in-app round trip that
  dead-ended silently. the paste path takes the text fallback now:
  trim, extension gate, existence check, open (rc.6).
- **child windows eat hit-tests.** when an edge or a corner goes dead,
  map which window answers each pixel of it before touching the handler
  — the fix may belong to a child's `WM_NCHITTEST` or a comctl
  suppression, not to the parent's resize code at all.
- **overflow contracts hide whole groups.** an overflow rule that drops
  from the right in whole groups makes the rightmost features the first
  casualties on small screens, maximized or not; the icon-only switch
  answers the actual constraint (rc.6).

### Release discipline

- **the prerelease flag is a contract.** an `-rc` release publishes with
  `prerelease=true` so the `latest` endpoint stays on the stable —
  verified against the real endpoint and the real download hash every
  release.
- **the golden bootstrap refuses the fake-green signature.** two renderer
  legs answering the same hash on real imagery is a hardware leg that
  never drew, reading the other's result back; the bootstrap refuses to
  write such a sample into the manifest, and the three-way-distinct
  contract is armed on every textured sample since (rc.2).
- **sweep the version everywhere, every round.** the version strings, the
  build pins, the changelog's own top pointers, the localization tables'
  tails — a round that bumps one and misses another answers to the guard
  suite in red.
- **attestation proves origin, not publisher identity.** the
  unsigned-binary ledger is honest about what each control actually
  buys; code signing remains its own purchase decision, deferred with
  the reason restated each round it stays deferred.

### Repository hygiene

- **`Changes.txt` is the archive of record; the README carries the
  present.** the readme diet started in 1.1.13-rc.1 and deepens every
  arc: full treatment for the current candidate and stable only,
  everything else one line or a pointer here.
- **construction documents retire when the construction ends.** the
  split spec (the argument) stays; the split plan (the per-task work
  order) and the per-agent gui-remake work order retired in the same
  round this file was born — git history and the changelog are the
  archive, live docs answer live questions.
- **session artifacts stay out of the tree.** planning logs, agent work
  orders and scratch state live outside the repository; what deserves
  permanence condenses into `Changes.txt` or this file.

## The 1.1.15 arc — round descriptions
### 1.1.15-rc.11 — the resume and chain round (2026-09-21, build 90)

- Commit: (this round); the resume switch's state-draw case (the pill), the chain walk's
  seat gate, the preload apply's cache coupling, the parked-hit early return, and the
  control matrix walk in the guard suite.
- Lessons:
  - **the resurrection audit had its own blind spot.** round-121 resurrected the three
    dead dropdown machines and pinned their shape - but the audit checklist read
    "dropdown machines," not "control machines." the resume switch shipped in the same
    lost surgery with its pill missing, one page over from the resurrected dropdowns,
    and the round that taught the lesson walked past it. a same-class audit must
    enumerate the class, not the instance.
  - **a promise that needs capacity must name its capacity.** the preload chain
    promised n images ahead into a ring the cache count sizes - and the default cache
    is one seat. three ahead on one seat walked fine, promoted twice, and evicted the
    chain's own head: every next was a disk load. the fix is not a bigger default; it
    is the walk refusing to promote past the seats it has (graceful) and the apply
    raising the cache to what the promise costs (visible) - the degradation is never
    silent again.
  - **the matrix walk is the guard the class deserved.** "every switch against
    activate/label/state-draw, every dropdown against run/label/value, no dead cases
    either way" is 40 checks of pure structure - and it would have caught the rc.9
    dropdowns, the rc.9 pill, and any future control added without its machines. the
    census numbers (seven switches, thirteen dropdowns) force every future control
    addition to walk through this section.
  - **don't pay a decode the machine already owns.** the ring hit's settle used to
    clear the parked preload's fd, walk one file forward, and re-decode the very file
    it had just thrown away. the early return - "the walk asked for what the slot
    holds; the slot answers" - is six lines. identity discipline is not only about
    which fd a reader binds; it is about asking whether the work is already done.

### 1.1.15-rc.10 — the gui limits round (2026-09-20, build 89)

- Commit: (this round); the status bar pair's breathing gap, the priority give-way order,
  the stamp/decimal buffer budgets, the pane budget shared by the layout and the texts,
  the count dropdowns' three machines wired alive, the zoom editor's four-digit limit,
  the message box heap copy, and the comment truths.
- Lessons:
  - **green guards over a dead machine.** rc.9's changelog claimed "the dropdown's own
    apply clears the ring" - the apply never existed. the guards pinned the control rows
    (the easy, visible part) and the suite stayed green while every interactive path was
    dead. a feature's guards must pin its *machines* (the popup case, the label case,
    the value case, the apply), not just its furniture.
  - **the lost-surgery tail.** round-120 recorded three wndproc edits lost to a failed
    script and caught them by count; the settings machine lost three edits the same way
    and nothing counted them. when a round's own log says "lost edits, caught by
    guards," audit what the guards did not count.
  - **a limit that cannot fire is still worth writing - once, on both sides.** the pane
    budget can never fire against today's eight-slot worst case; it exists so the ninth
    pane cannot silently walk the array. the same edit gated the text section, because
    a budget that only guards the layout half would let the texts drift from it.
  - **spacing is a decision, not a rounding error.** "too close together" was not the
    ten pixels of stock padding being wrong - it was that nobody had ever *decided*
    the gap. the pair now carries a named, dpi-scaled, commented twelve logical
    pixels.
  - **stale comments are load-bearing lies.** the six-button overlay comment described
    a ui two retirements old; the double strip-height comment carried both eras stacked.
    every reader after the truth paid the comment's tax.

### 1.1.15-rc.9 — the memory and cache round (2026-09-20, build 88)

- Commit: (this round); the cache ring (eight seats, settings-sized) replaces the single
  last slot, the preload walks a chain into that ring, the recent guard reads both
  identities, the rename family binds to the displayed slot, the resume switch captures
  the session's last file, and the animation frames stop building their mipmaps eagerly.
- Lessons:
  - **the census found promises, not leaks.** the "memory too high" field report led to
    a residency census, and the top findings were all *design* - the cache set riding the
    image ceiling, the eager per-frame mipmap chain, the 4-bytes-per-pixel animation
    gate under-pricing its own product. none of it was a leak; all of it was a budget
    written for a machine with no owner. fixing memory sometimes means fixing promises.
  - **the ring's one invariant carries every walk.** dense active run at [0..count-1]
    is what lets the insert shift blind, the activation close its hole, and the trim
    walk oldest-first without a single hole check. the invariant is *maintained* at the
    two places the count changes (the dropdown apply and the settings rewind) - both
    clear the ring from zero. one invariant, centrally maintained, beats defensive
    checks at every walk.
  - **take never moves the state field - on purpose.** the slot's state is the preload
    role's own; the ring takes fd/frames/counts/dims and leaves state behind, and the
    next dispatch resets it. building the promote path on take meant the promoted seat
    carries no stale state, and the in-flight decode never pays the trim's price
    (state-1 only).
  - **the identity lesson has a third chapter.** rc.8 bound the destructive commands to
    the displayed slot; this round's recent bug was the same binding question in the
    *guard* (compare against the request only, miss the in-flight window) and the rename
    family (seed from the request, retitle the wrong file). "which identity does this
    reader mean" is now a question every new file-acting site has to answer out loud.
  - **byte-level surgery still bites its own tail.** one bare LF in 4,000 lines of
    pasted C (the byte suite caught it), a walk function pasted twice (the compiler
    caught it), and three wndproc edits lost to a failed script's never-written file
    (the guard count caught it). the tools all worked; the discipline is trusting none
    of them alone.

### 1.1.15-rc.8 — the seventh audit response round (build 87, 2026-09-19)

the report arrived as a full-project review: twenty findings with line
numbers across the clipboard, the threading, the file identity, the
build chain and the release chain. every claim re-verified before
anything moved - sixteen confirmed as written, one stale (a
reproducible-build overpromise this readme never carried), the
accessibility items continuing the deferred ledger they have ridden
since the fifth audit.

the clipboard: the CF_DIB path read its header before it measured the
global, and CreateDIBSection consumed the palette before the size
check proved it existed - the length gate now runs before the lock
and the whole dib is proven before the section is created, with the
stride on the overflow-checked math and INT_MIN falling through with
the malformed palettes. the CF_BITMAP path answered no budget: the
source's own geometry now prices the copy before CopyImage allocates.

the identity: the requested file and the displayed file shared one
variable, so a fast next-then-delete during a load could delete the
file the user never saw. the displayed identity already existed - the
current slot's fd, committed by the first frame, moved by every slot
transition - and the fix bound the file-acting commands to it; the
navigation keeps the requested file.

the threading: the preload flag captured as an immutable job snapshot
at thread start; the stage marker on interlocked pointer forms. the
reply queue's tail repost hands a refused post's duty back (rc.5
closed the enqueue side; the tail carried the same hole for three
rounds). the settings save checks every write, keeps the last good
ini on failure, and moves write-through behind a flush.

the chain: the zig builds wipe their object directories (no ghost
objects from deleted sources), ci's actions:write lives on the one
uploading job, releases serialize per tag, NSIS pins 3.12.0, the
bilingual encoding check runs check-only in ci, sha256.txt joins the
attested subjects, and the installer wrapper's parameters are
whitelisted. the readme's floating-controls row now describes the
seven-cell row that exists, and README_CN.md arrived with the
language links.

the lessons: size before shape - a length is the fact every
dereference hangs on, and the validation order is the security
boundary. the best identity fix adds no new state - the displayed
identity was already maintained everywhere; only the readers were
bound to the wrong one. protocol ordering is not a c contract - the
preload flag's write/read pairs were ordered by the message queue's
own protocol, but formal data races want formal forms, not arguments.
audit your fixes for their own tails - rc.5 closed the refused post
on the enqueue side and the drain's tail repost carried the same hole
until the seventh audit found it. and one stale claim in twenty says
the auditor gets verified too.


### 1.1.15-rc.7 — the reentry state round (build 86, 2026-09-19)

the corrected field report: "the program minimizes to the taskbar, the
restore to the foreground loses the size data." the correction moved
the observation, not the cause - the damage lands at the forwarded show
command, the restore is where the eye catches it.

- the show-window state machine re-derived line by line against the two
  executable specs (wine win32u, reactos win32ss): a forwarded
  sw_shownormal restores a live maximized window to its normal rect
  (both agree - the demotion every re-open carried), sw_restore answers
  a minimized window from its placement (pinned on real windows by
  wine's own test_window_placement), and the one case no test pins on
  real windows - sw_shownormal onto a minimized window - is routed
  around: the app answers sw_restore there, which every spec and every
  real windows agrees on.
- the copydata activation is state-aware (iconic: sw_restore, or the
  run-maximized word; live: only the grow word; every other launcher
  word answers with the foreground call - the bare 0 a startf-less
  launcher forwards never hides the window).
- the minimized window runs no size sweep at all: the iconic client is
  degenerate (zero, or a sliver the strips subtract negative), and the
  old sweep rewrote the view anchors through rw=1 / rh-negative -
  nonzero, so the anchor guards passed and the anchors landed
  seventy-five image widths off. the restore's own wm_size re-runs the
  sweep against the real client.
- the iconic geometry reads all go through the placement now: the
  /x /y /width /height defaults, the fullscreen capture (iszoomed
  answers false for a maximized-minimized window; the old capture
  restored the window to the -32000 sliver on the fullscreen exit) and
  the /minimal //compact restyle (styles and strips only, frame-only
  setwindowpos, the popping sw_restore live-only).
- the round's lesson: when two executable specs agree but no
  real-windows test pins the exact case, do not bet on either - route
  around the ambiguity with the call every side agrees on.

### 1.1.15-rc.6 — the judged-fixes round (362e718, build 85, 2026-09-18)

the round took the user's own judged fix list: three things worth doing,
one concurrency gap of the class rc.5 had just closed, two field-reported
interface defects, and the one audit line kept from the self-corrections.

- the message box buttons drew their labels as raw utf-8 bytes fed to the
  wide-char api ("ok" answered u+4b4f and read a byte past the
  terminator; the chinese "确定" answered u+a1e7) — three lines through
  the same utf-8-to-wide bridge every other face uses close the last raw
  path in the tree, and the three incompatible-return warnings that had
  sat in the census baseline for years go silent with the bug.
- the classic options dialogs retire for real: sixteen procedures, four
  templates and seventy-eight resource ids that nothing could open
  anymore (the remake settings window is the options surface since the
  gui round), the ids retired by name only — they share numbers with
  live controls.
- the fullscreen toggle's 1024-entry precomputation (two 4 kb stack
  arrays, up to 2049 render measurements per toggle) retires into the
  binary searches the wheel has used since beta.8, measured in the same
  geometry the old scans saw.
- the borderless corner resize is back: the status bar now answers the
  grip box itself and hands the size loop to the parent — mouse and
  touch — and the manual edge band widens to the padded-border metric.
- the toolbar gains an icon-only switch (settings and the right-click
  menu) — the full strip is wider than a default window, so the overflow
  contract hid whole groups on small screens.
- paste understands a copied path (the text fallback behind the image
  formats), and the two loader refusal flags join the interlocked forms
  rc.5 gave the cancel flag.

the round's lessons: the user's judged list gets the same verification
discipline as an external audit — the frames claim failed re-verification
and was answered with the refutation (the loader never touches the slot;
every frame crosses the reply queue by value) while the real gap the claim
brushed against (the two refusal flags) took the lock-class fix. deleting
dead code is half guard work: twelve stale pins and five counters
recalibrated with the fourteen hundred lines. and the compiler had been
reporting the message-box bug for years — three return-type warnings, one
per bridged label, waiting in the baseline for someone to read them.

### 1.1.15-rc.5 — the sixth audit response round (3bb4e22, build 84, 2026-09-18)

the report arrived as a question about the system's own keys — do the
viewer's shortcuts fight them? they cannot: the tree registers no global
hotkey, installs no keyboard hook, owns no accelerator table; every
binding matches window-locally, consumed only on a hit. the real fight
was the input method editor: an open chinese ime rewrites letter keys
into `vk_processkey` before any window sees them, so letter bindings went
dead on the canvas and the key-capture dialogs could store a key that can
never be pressed again. the windows that never compose text — the canvas,
the zoom pill and its digits-only editor, the settings window, the
edit-key capture — now run dissociated from the ime; the text-input
dialogs keep theirs, chinese filenames are real input there.

two p0s from the same audit: the init path swallowed its failures (a
refused class registration meant a windowless zombie — every step answers
out loud now, with the last error and a clean teardown), and the reply
queue's wakeup posted exactly once and dropped the post's return value
(one refused `postmessage` and the queue stalled until the exit timeout —
the wakeup is a duty flag now; a refused post hands the duty back and the
drain's tail re-posts for stragglers). the hygiene batch: interlocked
forms for the cancel flag (plain `volatile` carries no barrier on the arm
legs), com and gdi+ teardown pairing, the offset-based copydata
validator, the uninstaller's fresh-directory second stage, the qoi
attribution completing, and security.md catching up.

the round's lessons: the house rule held — nineteen key claims
re-verified before anything landed, three refuted with the evidence
recorded (the thread-handle claim's suggested belt was traced to a real
regression window and stays out for that reason, written down beside the
fixes). the guard section's first run bit its own authors six times —
guards are code. and the audit's test-trust claims fell nine-for-nine
against the ci's actual gate: the suites run directly, the failure paths
exit non-zero, and the two real defects (the `-o` blindness, the
born-dead assert) were fixed as found.

### 1.1.15-rc.4 — the command picker round (0272846, build 83, 2026-09-18)

the report read like a bug report — keyboard shortcuts could not be
added — and the tree agreed: the settings window's command dropdown was a
flat popup capped at the label store's thirty-two rows while the command
table holds one hundred and eighteen pickable commands. eighty-six
commands were unreachable by any path the interface offers. the deeper
constraint is the screen: a flat list of 118 rows is taller than any
display, so raising the cap alone would only move the wall. the cascade
is the real menu tree — the same walk the frame menu runs, every leaf
owner drawn with its label and live shortcut, the current command
radio-checked, one page per menu so no page can outgrow the screen.

the report's second ask was the wider question — every feature entry
point checked and every code path behind it verified usable — and the
entry-point census landed in the changelog: all 118 command rows dispatch,
all 62 default keys install and name real commands, the toolbar, the
context menu and the menubar share one table and one dispatcher, all
thirty settings control ids answer, and the ini round-trip regenerates
all 118 names uniquely.

the round's lessons: a usability cap and a display constraint are
different problems — the fix that raises one without answering the other
ships the same bug one cap higher. and a census is a walk, not a shape:
it goes in the changelog, not the guard suite, and the next table edit
answers to the dispatcher guards that already exist.

### 1.1.15-rc.3 — the fifth audit response round (ada0932, build 82, 2026-09-18)

the fifth external audit arrived as a fusion report — a first pass that
claimed a delivered fix package no tree ever received, and a second pass
of eight parallel sweeps. every claim re-verified before anything
landed: all three navigation defects, all ten one-liners and both guard
blind spots real.

the navigation trio, two of them ordering defects the eye cannot see:
the full-path sort compared bare scan names against a full path (a mixed
key whose order is noise — twelve images with ten unreachable); the
random gate asked the file gate before the random fallback while the
walk answers random first (the menu's step faces grayed while the keys
still fetched); and the playlist lookup matched strangers to the first
entry through the reserved id pair both carry — the identity is the file
name now, the id pair kept as the sort tiebreak. the golden
discrimination closed at the source: the bootstrap refuses to write a
textured sample where two legs answered the same hash — the fake-green
signature. the blind spots: the ladder step extracted and cross-checked
(a drift to 1.02 had passed every suite), and a deleted golden manifest
fails the push gate red.

the round's lessons: extracted constants over hand-copied mirrors — the
suite that pins 278 and 1024 while hand-copying 1.01 guards nothing it
claims to. and an audit that reports a delivered fix is reporting about
a tree that never received it — the fusion report's first pass was
archaeology, not news; the re-verification discipline is what kept the
second pass honest.

### 1.1.15-rc.2 — the fourth audit response round (2c06bef, build 81, 2026-09-17)

the fourth external audit landed against the stable and opened with the
retraction its author earned the hard way: the round-3 core finding never
existed — a mechanical bracket-stack reparse shows the line at depth 2
where it always was, in build 72, 74 and 79 alike. a plausible number
built on an unverified premise is more dangerous than no number at all.

the three live findings: the golden set's two controls were single solid
fills, and one answered bit-identically on gdi and d3d — a coincidence
nobody could rule out from linux, and the exact signature of a d3d leg
that never drew reading the gdi result back. the textured png
adjudicates: 12,290 distinct pixel values with the floor pinned inside
its own generator, hand-encoded with the standard library only, and the
moment its hashes landed the new contract armed — every textured sample
must answer three-way distinct, and a pinned hash that regresses to a
renderer refusal goes red with it. the menu's navigation pair gates on
one neighbor rule now (a lone image in an empty folder used to light the
menu's next while the toolbar's face stayed gray), and the cache-set
ceiling's comment tells the unit-price truth the audit asked for.

the round's lessons: a coincidence you cannot rule out is not a finding,
but it is not nothing either — it is a fixture away from a contract. and
the retraction at the top of the report was the most valuable line in
it: the method that caught the false finding (reparse, don't re-read) is
the same method the guard suites run on.

### 1.1.15-rc.1 — the image slot architecture round (0cb2bf7, build 80, 2026-09-17)

the split era moved the code into files without ever asking what the
moved code was made of, and nowhere did that show more than in the three
places an image lives: the current image, the last-image cache and the
preload slot — three parallel families of loose globals, nineteen
variables, every transition a hand-written field list, every budget gate
naming each family's variables one by one. one typed slot describes a
held image; three instances replace the nineteen; the lifecycle
collapses into two primitives (the take moves a whole slot, the clear
empties one); and the cache-set ceiling prices the three slots through
one slot-bytes helper, so a fourth slot the future wants prices itself
by joining the sum.

the walk past the status code caught a real one: the playlist position
pane cached its walk on the frame fd's address, stable since the file
split — the pointer key could never tell one file from the next, and the
pane froze on the first file the walk ever saw. the cache keys on the
file name now.

the round's lessons: a type beats a discipline — the round-108 ceiling
worked because its author enumerated all three families at every fill
point; the slot makes that enumeration a property of the type instead of
a habit of whoever edits next. and pointer keys rot silently: an address
that is merely stable is not an identity.

## Earlier arcs — one line each

full per-version narrative at each tag in `Changes.txt`.

- **1.1.14** — the stable promotion round: the cache-set ceiling (three
  slots priced together at every fill point), the defaults pinned as a
  guard, the already-loading answer, the settings blank.
- **1.1.14-rc.10** — the corrections round: the byte-invariant class
  gates (`.editorconfig`, the byte suite, the pushed-range whitespace
  check), the re-runnable split conservation proof (290/290), the qoi
  fuzz smoke (36 deterministic mutants), the arm64 compile leg, the
  renderer fallback naming itself on the status line.
- **1.1.14-rc.9** — the navigation faces round: the toolbar's
  previous/next enable rule reads the navigation's own two paths instead
  of a cache only the jump-to dialog ever fills.
- **1.1.14-rc.8** — the renderer parity round: the gdi+ frames answer as
  dib sections so the hardware renderers render every decoder family,
  the shape dimension reaches the pixel oracle, the ceiling gate proves
  the refusal through the export oracle.
- **1.1.14-rc.7** — the input ceiling round: `getfilesizeex` and the
  1 gb/512 mb whole-file ceilings, the hard kill retiring into the
  recorded process exit, thread-creation unwinding, the codeql full
  attack-surface leg, the sparse 4-gb-plus smoke stage.
- **1.1.14-rc.6** — the budget and baseline round: the working-set and
  animation frame budgets, the recorded exit timeout, the v145 security
  baseline with pe binary assertions, the release trust chain
  (attestation, codeql, the collaboration pack).
- **1.1.14-rc.5** — the pixel oracle round: the gl context rebuild
  across window changes, the renderer pad edge replication, the hidden
  render export and the golden hash ci.
- **1.1.14-rc.4** — the corner and audit response round: the fullscreen
  sharp-corner fix, the caption text color (attribute 36) and the
  evidence-first audit response (the gl pad zero, the per-window pixel
  format, the d3d same-size reuse, the sdl elevation catches).
- **1.1.14-rc.3** — the navigation visibility round: the next/previous
  fix for the new formats — a supported-extension table carries the
  viewer's open universe and the navigation filter answers it.
- **1.1.14-rc.2** — the fixture round: the test sample set commits (38
  anomaly samples plus eight real imagery fixtures, two of them
  hand-encoded animated gifs) and the qoi magic fix — the host
  verification caught a shipped constant spelled in the wrong byte
  order.
- **1.1.14-rc.1** — the todo closure round: the upstream todo list
  closes — the opengl and direct3d renderers, the toolbar customization
  and the shell context menu all land.
- **1.1.13** — the format horizons round: the wic layer defers to the
  system codecs when gdi+ and libwebp both decline (jpeg-xr, dds,
  heif/avif wherever the os carries them), and a hand-rolled qoi decoder
  opens `.qoi` with no codec dependency at all; the fork's first stable
  to carry code.
- **1.1.13-rc.7** — the field sweep round: the cold-start slideshow
  fix, the startup shortcut retirement, the no-image menu gate, the
  single keyboard focus ring and the icon payload diet (the ico drops
  80%).
- **1.1.13-rc.6** — the peripheral residue round: the closing sweep
  retires the never-built wine dpi probe, four zero-reference api
  functions, the year-stringize pair and three never-requested
  localization strings.
- **1.1.13-rc.5** — the dead residue round: the carpet sweep retires
  three dead functions, four dead macro families and fourteen
  vs-generated resource ids; the frozen corners stay pinned by the
  round-91 guard.
- **1.1.13-rc.4** — the high dpi icons round: the ico grows the dpi
  ladder (lanczos-resampled frames down from 256px, all
  alpha-carrying) and the frame icons ride the window's own dpi
  (`wm_seticon` pair, `wm_dpichanged` re-pin).
- **1.1.13-rc.3** — the halftone palette round: 256-color mode gets
  `graphics::gethalftonepalette` — the classic palette contract
  (foreground/background realization, the paint-dc selection, the
  display-change resync).
- **1.1.13-rc.2** — the association guard round: `.bmp`/`.jpg` taken
  over only when the effective default is the windows canonical class
  or already ours — a foreign viewer's association is left completely
  alone.
- **1.1.13-rc.1** — the readme diet round: the news section demoted to
  the one-line list (full treatment only for the current stable and
  candidate; `Changes.txt` stays the archive of record).
- **1.1.12** — the remake arc: the full gui remake, the structure
  split, the platform guardrails and the field fixes — seventeen
  candidates, the stable promotion carried no code.
- **1.1.12-rc.17** — the review absorption round: the external carpet
  review absorbed (the itemid carrier, the token strip, the settings
  dpi correction, the resize escape hatch, the startup dpi sync).
- **1.1.12-rc.16** — the open intent round: the recent-list trade
  refunded (the open intent declared where it is knowable — a reload is
  not a recent open).
- **1.1.12-rc.15** — the field report round: the rotate-into-recent
  reordering, the 4k 225% text proportions, the capture-hint mojibake,
  the menu process check.
- **1.1.12-rc.14** — the zoom pane editor round: the corner percent
  becomes an in-place editor (the drag moved off the pane, the 1998
  dialog retired).
- **1.1.12-rc.13** — the non-win11 field round: the play/pause face,
  the first-zoom snap and the dead gate, the non-win11 look (drop
  shadow, owner-drawn dropdowns, token recalibration).
- **1.1.12-rc.12** — the platform guardrails round: the windows 8.1
  manifest guid, the unicows clean, the `wm_dpichanged` lparam guard.
- **1.1.12-rc.11** — the carpet repair round: the menu bar roots and
  the status date pane, the view-top origin, the theme-flip flush, the
  win7 dpi ladder.
- **1.1.12-rc.10** — the dpi correctness round: the per-monitor-v2
  claim fixed at the root (three guarded layers plus the zig-side
  manifest).
- **1.1.12-rc.9** — the gui remake round 2: one palette for everything
  (the 17 theme tokens, the owner-drawn menus, the themed message boxes
  and dialogs).
- **1.1.12-rc.7** — the zoom pill rework: one self-drawn window,
  stadium caps at every dpi, click-through outside the capsules.
- **1.1.12-rc.6** — the top bar remake: the menu bar is a fully
  self-drawn child window (`viv_menubar`), the status bar owns its dark
  face, the toolbar button spacing pinned uniform in both themes.
- **1.1.12-rc.5** — the structure round: the gesture cluster home in
  the view domain, the 2,208-line window procedure split into 42
  per-message handlers in the new `viv_wndproc.c`, the splice guard's
  file manifest pinned.
- **1.1.12-rc.4** — the white band after opening options or switching
  the theme fixed at the root (the dark re-apply lost its flip gate;
  the sweep is now unconditional and idempotent); the options
  navigation tree reads in the dark ui.
- **1.1.12-rc.3** — the monolith is gone: `viv.c` (21,129 lines) is now
  a 4,718-line core plus eleven domain modules and a `viv_state.h`
  shared-context layer — a pure physical move (function bodies
  byte-identical; `/gl` whole-program optimization keeps cross-module
  inlining, performance unchanged); the setup offers the emf/wmf
  associations; the vendored libwebp pruned of non-windows build
  systems, fuzzers and docs (62 files — `copying`/`patents`/`authors`
  kept).
- **1.1.12-rc.2** — the about dialog's band and title move into the
  resource template (correct at every dpi and in both themes); the
  post-theme-flip white band fixed (the frame repaint joined the sweep,
  the toolbar strip relayouts with the system metrics).
- **1.1.11** — one type system for the whole ui (the system message
  font per dialog at its own window dpi — cjk-safe, no more
  fallback-font mismatch); emf/wmf in every association surface
  (metafiles rasterize via gdi+ — display-level support); recent files,
  transparency backdrop, complete dark dialogs.
- **1.1.02–1.1.10** — the fork's early arc: the touch gestures and
  pinch zoom, the floating zoom controls, the dark ui reaching every
  strip, the bilingual localization with the live language switcher,
  and the field-fix rounds (the full per-version narrative lives in
  `Changes.txt` at each tag).

## round 123 - the report fusion round (1.1.15-rc.12)

the third fusion report arrived as a fourteen-sweep audit of the rc.10
tree with the main thread re-verifying eight of eight headline claims.
the round took its whole ledger: two p1 guard teeth, sixteen p2 line
fixes, the p3/p4 debts the deferrals kept, and the account corrections.
five lessons worth keeping:

- **a pin satisfied by a comment is not a pin.** the rc.7 restore pin
  matched a comment that explains the line; reverting the code alone
  passed every suite. the fusion report's E4 proved it with a live
  mutation. the replacement pins whole code shapes - a ternary with its
  own arms - that no prose can satisfy.
- **a census of instances needs a census of bindings.** round 122's
  matrix walk proved every control had its machines; the fusion report
  then swapped two label ids and the walk stayed green. a case is not a
  binding: the round's census now demands each activate body and each
  pill body carry its own config variable.
- **the version sweep has its own failure shape.** the rc.11 sweep
  renamed assertions and dragged four historical round docstrings along
  (round 114/118/120/121 all claimed to be rc.11), and one check name
  kept an old build number its assertion had already left. the sweep
  now carries two rules: the name and the assertion move together, and
  history's docstrings stay as their rounds wrote them.
- **write-token hygiene is measurable.** the tests workflow carried
  actions:write on both matrix legs for a step that never needed any
  grant - upload-artifact runs on the default token. the fusion round
  removed the grant entirely; the pin refuses its return.
- **same-family fixes travel in packs.** the request-fd family (menu
  face, title bar, cursor gate, save-as seed, delete scan anchor), the
  stop pair (paste and blank), the exit codes (init, debug_fatal) -
  each family had one member fixed in an earlier round while its
  siblings kept the defect. the report's matrix audit found them by
  class, and the round fixed them by class.
