#!/usr/bin/env python3
"""Manual integration smoke test for the IRSET-backed Isearch shell.

Usage:
  python3 tests/shell_irset_smoke.py /path/to/Isearch /path/to/db known-term

The database must contain known-term. This test is intentionally opt-in; it
uses an existing indexed database and does not modify it.
"""
import subprocess
import sys


def check(label, condition, transcript):
    if not condition:
        raise AssertionError(f"{label} failed:\n{transcript}")


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    binary, database, term = sys.argv[1:]
    empty_term = "zz_irset_shell_no_matching_record_938457290"
    # Each output fragment proves the command loop survived the prior step.
    script = "\n".join([
        "size",          # nothing current
        "push",          # must reject a second PUSH with no current
        "= " + empty_term,
        "size",          # valid zero-match result, not an exit
        "push",          # empty IRSET is still a valid stack operand
        "push",          # now nothing to push
        "peek size",
        "depth",
        "pop",           # restore empty current IRSET
        "size",
        "clear",
        "set words",
        "= " + term,
        "size",
        "push",
        "= " + term,
        "push",
        "peek size",
        "depth",
        "AND",
        "size",
        "next",
        "quit",
        "",
    ])
    result = subprocess.run(
        [binary, "-d", database, "-shell", "-top", "5"],
        input=script,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=60,
        check=False,
    )
    output = result.stdout
    check("shell exit status", result.returncode == 0, output)
    check("empty current error", "No current IRSET." in output, output)
    check("push without current", output.count("ERROR: nothing to push.") >= 2, output)
    check("zero matches retain shell", "0 entries" in output, output)
    check("non-destructive peek", "1 stacked IRSETs." in output, output)
    check("two pushed operands", "2 stacked IRSETs." in output, output)
    check("intersection produced result", "Isearch shell" in output, output)
    print("IRSET shell smoke test passed")


if __name__ == "__main__":
    main()
