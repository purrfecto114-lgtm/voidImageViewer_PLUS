#!/usr/bin/env python3
"""the installer's native-architecture gate, proven as behaviour.

the guard suite pins the installer's arch gates by quoting them. a quote
pins the line, not the meaning: the x64 gate sat next to a pinned arm64
gate for a full release with a literal from the wrong enumeration (9 is
PROCESSOR_ARCHITECTURE_AMD64, not the IMAGE_FILE_MACHINE_AMD64 the
getter answers), and the arm64 quote stayed green while every real x64
machine was told it was not x64.

this tool reads the gate instead. it lifts each !ifdef arch block out
of nsis/installer.nsi, models the instructions the blocks are written in
and runs every value ${GetNativeMachineArchitecture} can return through
it, then asks the only question that matters: does the machine land on
the ok label by itself, or on a warning?

both spellings of the gate answer. the script may compare the register
by hand (IntCmp $0 <machine value>) or ask x64.nsh's own predicates
(${If} ${IsNativeAMD64}), and the two are the same gate - so the tool
models the predicates, not the spelling.

the class guard rides along: every raw literal compared against the
machine answer has to be a member of the IMAGE_FILE_MACHINE enumeration.
the two enumerations a windows author reaches for share nothing - amd64
is 34404 in one and 9 in the other - which is why a gate written against
the wrong one matches no machine at all, quietly, on every machine,
forever.

Usage: python3 tools/nsis_arch_gate_test.py  (from the repository root)
Exit status is nonzero when the gate misbehaves or leaks a foreign literal.
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
os.chdir(ROOT)

NSI = os.path.join("nsis", "installer.nsi")

# winnt.h IMAGE_FILE_MACHINE_*. the getter answers exactly this
# enumeration: nsis's x64.nsh derives it from IsWow64Process2's
# NativeMachine out-param and falls back to the ntdll
# NtQuerySystemInformation(NativeMachine) path, both IMAGE_FILE_MACHINE_*.
# PROCESSOR_ARCHITECTURE_* is a different enum and belongs to
# GetNativeSystemInfo, which this installer must not use (it answers the
# emulation view to an x86 process on arm64).
IMAGE_FILE_MACHINE = {
    0: "UNKNOWN",
    8: "MIPS32",
    9: "PROCESSOR_ARCHITECTURE_AMD64 (wrong enum - not a machine value)",
    10: "MIPS32R6",
    12: "PROCESSOR_ARCHITECTURE_ARM64 (wrong enum - not a machine value)",
    20: "POWERPCFP",
    21: "POWERPC",
    40: "ARM",
    81: "CE",
    88: "M32R",
    256: "SH4",
    260: "SH5",
    332: "I386",
    448: "ARMNT",
    1848: "IA64",
    4192: "ARM64EC",
    8876: "ARM64X",
    1312: "TRICORE",
    16962: "WCEMIPSV2",
    34404: "AMD64",
    43620: "ARM64",
}

# the x64.nsh predicates, by the machine value they answer for.
PREDICATE = {
    "IsNativeIA32": 332,
    "IsNativeAMD64": 34404,
    "IsNativeARM64": 43620,
}

# the policy the gate has to implement, not a description of what it
# happens to do. the key is the machine, the value is (expected verdict,
# expected warning id).
POLICY = {
    "x64": {
        34404: ("ok", None),
        43620: ("warn", "MsgOsArm64Emulation"),
        332: ("warn", "MsgOsNotX64"),
        1848: ("warn", "MsgOsNotX64"),
    },
    "arm64": {
        43620: ("ok", None),
        34404: ("warn", "MsgOsNotArm64"),
        332: ("warn", "MsgOsNotArm64"),
        1848: ("warn", "MsgOsNotArm64"),
    },
}


class Failure(Exception):
    pass


def strip_comments(text):
    """nsis comments: a leading ';' runs to end of line."""
    return "\n".join(re.sub(r";.*$", "", line) for line in text.split("\n"))


def load_blocks(text):
    """lift each !ifdef <arch> ... !endif pair out of the script.

    returns {arch: [line, ...]}."""
    lines = strip_comments(text).split("\n")
    blocks = {}
    arch = None
    depth = 0
    cur = []
    for line in lines:
        s = line.strip()
        m = re.match(r"^!ifdef\s+(x64|arm64)\s*$", s)
        if m and depth == 0:
            arch = m.group(1)
            depth = 1
            cur = []
            continue
        if arch is not None:
            if s.startswith("!ifdef"):
                depth += 1
            elif s.startswith("!endif"):
                depth -= 1
                if depth == 0:
                    blocks[arch] = cur
                    arch = None
                    continue
            cur.append(s)
    return blocks


class Program(object):
    """the gate as a little program.

    instructions:
      ('if', predicate, negate)   - ${If}/${IfNot} ${IsNativeXXX}
      ('intcmp', literal, eq, ne, lt, gt)
      ('msgbox', message_id, yes_label)
      ('abort',)
      ('label', name)
      ('other', text)
    """

    def __init__(self, lines):
        self.prog = []
        self.labels = {}
        for s in lines:
            if s.endswith(":") and re.match(r"^[A-Za-z_][A-Za-z0-9_]*:$", s):
                self.labels[s[:-1]] = len(self.prog)
                self.prog.append(("label", s[:-1]))
                continue
            m = re.match(r"^\$\{(IfNot|If)\}\s+\$\{([A-Za-z0-9_]+)\}$", s)
            if m:
                self.prog.append(("if", m.group(2), m.group(1) == "IfNot"))
                continue
            m = re.match(r"^IntCmp\s+\$(\w+)\s+(\S+)\s*(\S*)\s*(\S*)\s*(\S*)\s*(\S*)\s*$", s)
            if m:
                self.prog.append(("intcmp", m.group(2), m.group(3),
                                  m.group(4), m.group(5), m.group(6)))
                continue
            if s.startswith("MessageBox"):
                mid = re.search(r'\$\((Msg\w+)\)', s)
                yes = re.search(r"IDYES\s+(\S+)", s)
                self.prog.append(("msgbox", mid.group(1) if mid else "?",
                                  yes.group(1) if yes else None))
                continue
            if s in ("Abort", "Abort 0"):
                self.prog.append(("abort",))
                continue
            if s.startswith("${EndIf}"):
                self.prog.append(("endif",))
                continue
            if s.startswith("${ElseIf}") or s.startswith("${Else}"):
                # the gate never branches both ways here; an else arm
                # would need the else body modelled, so it is reported.
                self.prog.append(("other", s))
                continue
            self.prog.append(("other", s))

        # the matching EndIf for every if: a false condition has to skip
        # the whole body, not just the first instruction of it.
        stack = []
        for i, ins in enumerate(self.prog):
            if ins[0] == "if":
                stack.append(i)
            elif ins[0] == "endif":
                if stack:
                    self.prog[stack.pop()] = ("if", self.prog[stack[-1]][1],
                                              self.prog[stack[-1]][2], i)
        if stack:
            raise Failure("an ${If} in the gate has no matching ${EndIf}")

    def run(self, machine):
        """walk the program for one native machine value.

        returns (verdict, message_id) where verdict is 'ok' when control
        reaches an ok label on its own, 'warn' when it reaches a
        MessageBox first, or 'abort' / 'fell-through' when it reaches
        neither."""
        pc = 0
        steps = 0
        while 0 <= pc < len(self.prog):
            steps += 1
            if steps > 2000:
                raise Failure("the gate did not terminate for machine %d" % machine)
            ins = self.prog[pc]
            kind = ins[0]
            if kind == "label":
                # reaching the block's own ok label IS the ok answer.
                if ins[1].endswith("_ok"):
                    return "ok", None
                pc += 1
                continue
            if kind == "if":
                _, pred, negate, end_if = ins
                if pred not in PREDICATE:
                    raise Failure("the gate asks for the unknown predicate %s" % pred)
                value = PREDICATE[pred]
                hit = (machine == value)
                if negate:
                    hit = not hit
                if not hit:
                    # the body does not run: step over it to the EndIf.
                    pc = end_if + 1
                    continue
                # the body runs: the next instruction, and the EndIf
                # falls through on its own.
                pc += 1
                continue
            if kind == "intcmp":
                _, literal, eq, ne, lt, gt = ins
                try:
                    val = int(literal, 0)
                except ValueError:
                    raise Failure("IntCmp against the non-numeric %r" % literal)
                if machine == val:
                    target = eq
                elif machine < val:
                    target = lt or ne
                else:
                    target = gt or ne
                if target and target not in ("0", ""):
                    if target not in self.labels:
                        raise Failure("jump to the undefined label %r" % target)
                    if target.endswith("_ok"):
                        return "ok", None
                    pc = self.labels[target]
                    continue
                pc += 1
                continue
            if kind == "msgbox":
                return "warn", ins[1]
            if kind == "abort":
                return "abort", None
            pc += 1
        return "fell-through", None


def main():
    raw = open(NSI, "rb").read().decode("utf-8", errors="replace")
    if "x64.nsh" not in raw:
        raise Failure("the installer no longer includes x64.nsh - the native "
                      "machine answer it rides is gone")

    blocks = load_blocks(raw)
    failures = []
    for arch in sorted(POLICY):
        print("== the %s installer gate ==" % arch)
        if arch not in blocks:
            print("  --   no !ifdef %s block found (skipped)" % arch)
            continue
        prog = Program(blocks[arch])

        # the literal lint: every raw comparison against the register the
        # native machine answer lands in must compare a machine value.
        compared = 0
        predicates = 0
        for ins in prog.prog:
            if ins[0] == "intcmp":
                try:
                    literal = int(ins[1], 0)
                except ValueError:
                    failures.append("%s: IntCmp against the non-numeric %r"
                                    % (arch, ins[1]))
                    continue
                if literal not in IMAGE_FILE_MACHINE:
                    failures.append(
                        "%s: IntCmp compares against %d, which is not an "
                        "IMAGE_FILE_MACHINE value - the native machine answer "
                        "never answers that, so the gate matches no machine"
                        % (arch, literal))
                elif "wrong enum" in IMAGE_FILE_MACHINE[literal]:
                    failures.append(
                        "%s: IntCmp compares against %d (%s) - the gate is "
                        "written against the PROCESSOR_ARCHITECTURE enum, not "
                        "the machine one the getter answers"
                        % (arch, literal, IMAGE_FILE_MACHINE[literal]))
                compared += 1
            elif ins[0] == "if":
                if ins[1] not in PREDICATE:
                    failures.append("%s: the gate asks for the unknown "
                                    "predicate %s" % (arch, ins[1]))
                else:
                    predicates += 1
        if compared + predicates == 0:
            failures.append("%s: the gate never asks whether the machine is "
                            "the one it wants" % arch)
        else:
            print("  ok   %d raw comparison(s), %d predicate(s), no foreign literal"
                  % (compared, predicates))

        print("  %-8s %-12s %-12s %-24s %s" % ("machine", "reaches", "policy",
                                              "warning", "verdict"))
        for machine in sorted(POLICY[arch]):
            name = IMAGE_FILE_MACHINE.get(machine, str(machine))
            try:
                verdict, mid = prog.run(machine)
            except Failure as exc:
                failures.append("%s / machine %d: %s" % (arch, machine, exc))
                print("  %-8s %-12s %-12s %-24s %s" % (name, "-", "-", "-",
                                                      "ERROR " + str(exc)))
                continue

            want, want_msg = POLICY[arch][machine]
            ok = (verdict == want) and (mid == want_msg)
            if not ok:
                failures.append(
                    "%s installer on %s reaches %r (warning %r); the policy "
                    "asks for %r (warning %r)"
                    % (arch, name, verdict, mid, want, want_msg))
            print("  %-8s %-12s %-12s %-24s %s"
                  % (name, verdict, want, mid or "-", "ok" if ok else "MISMATCH"))

    if failures:
        print("\nFAILED:")
        for f in failures:
            print("  - %s" % f)
        return 1
    print("\nALL NSIS ARCH GATE TESTS PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failure as exc:
        print("FAILED: %s" % exc)
        sys.exit(1)
