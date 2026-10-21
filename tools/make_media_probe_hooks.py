"""Generate an UNAPPLIED repository patch for the next reviewed GUI build.

Reads current owner files, records their hashes and writes only patch/manifest
artifacts. Never edits the USB/debug/profile/touch files or starts a device.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"hook context absent/ambiguous: {old[:80]!r}")
    return text.replace(old, new, 1)


def recount_patch(text):
    lines = text.splitlines(keepends=True)
    for i, line in enumerate(lines):
        match = re.match(r"@@ -(\d+),\d+ \+(\d+),\d+ @@(.*)\n", line)
        if not match:
            continue
        old_count = new_count = 0
        for body in lines[i + 1:]:
            if body.startswith(("@@ ", "--- a/", "diff --git ")):
                break
            if body.startswith((" ", "-")):
                old_count += 1
            if body.startswith((" ", "+")):
                new_count += 1
        lines[i] = (f"@@ -{match[1]},{old_count} +{match[2]},{new_count} @@"
                    f"{match[3]}\n")
    return "".join(lines)


def changes(repo):
    paths = ("xnu/vermeer/vermeer_debug.c", "tools/prepare_vermeer.py",
             "xnu/vermeer/0067-vermeer-touch-hook.patch")
    old = {p: (repo / p).read_text(encoding="utf-8") for p in paths}
    new = dict(old)
    p = paths[0]
    if "vermeer_debug_media_register" in new[p]:
        raise ValueError("media route already present; do not replay hooks")
    new[p] = replace_once(new[p], "static vermeer_control_request_fn _Atomic control_request;",
                         "static vermeer_control_request_fn _Atomic control_request;\n"
                         "static vermeer_control_request_fn _Atomic media_request;\n"
                         "void vermeer_debug_media_register(vermeer_control_request_fn);")
    new[p] = replace_once(new[p], "int vermeer_debug_control_request(const char *text,unsigned n)\n{\n",
                         "void vermeer_debug_media_register(vermeer_control_request_fn callback)\n"
                         "{os_atomic_store(&media_request,callback,release);}\n"
                         "int vermeer_debug_control_request(const char *text,unsigned n)\n{\n"
                         "    vermeer_control_request_fn media=os_atomic_load(&media_request,acquire);\n"
                         "    if(media && media(text,n))return 1;\n")
    p = paths[1]
    match = re.search(r"^    for name in \('vermeer_touch.c'.*\):$", new[p], re.M)
    if not match:
        raise ValueError("GUI source staging list not found")
    if "vermeer_media_probe.c" in match[0]:
        raise ValueError("media sources already staged")
    replacement = match[0][:-2] + ",'vermeer_media_probe.c','vermeer_media_probe.h','vermeer_media_tlmm.h'):"
    new[p] = replace_once(new[p], match[0], replacement)
    p = paths[2]
    new[p] = replace_once(new[p], "+bsd/dev/vermeer_touch.c\t\t\tstandard\n",
                         "+bsd/dev/vermeer_touch.c\t\t\tstandard\n"
                         "+bsd/dev/vermeer_media_probe.c\t\t\tstandard\n")
    new[p] = replace_once(new[p], "+\tvermeer_touch_start();\n",
                         "+\tvermeer_touch_start();\n"
                         "+\textern void vermeer_media_probe_start(void);\n"
                         "+\tvermeer_media_probe_start();\n")
    new[p] = recount_patch(new[p])
    return old, new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    old, new = changes(args.repo)
    patch = "".join("".join(difflib.unified_diff(old[p].splitlines(keepends=True),
                    new[p].splitlines(keepends=True), fromfile="a/" + p, tofile="b/" + p)) for p in old)
    args.output.write_text(patch, encoding="utf-8", newline="\n")
    hashes = {p: hashlib.sha256((args.repo / p).read_bytes()).hexdigest() for p in old}
    args.output.with_suffix(args.output.suffix + ".json").write_text(json.dumps({
        "applied": False, "source_file_sha256": hashes, "boot_arg": "vermeer_media_probe=1",
        "commands": ["media bt", "media camera", "media all"],
        "default": "no provider -> gated VMT1; no UART/CCI/PMIC/reset access",
    }, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote unapplied hooks and baseline hashes: {args.output}")


if __name__ == "__main__":
    main()
