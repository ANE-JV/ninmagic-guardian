#!/usr/bin/env python3
"""Prevent accidental reuse of Hekate's incompatible frontend format parser."""
from pathlib import Path
import re
root=Path(__file__).resolve().parent
for source in root.glob("*.c"):
    code=source.read_text()
    if re.search(r"\bgfx_(printf|puts)\s*\(",code):
        raise SystemExit(f"Frontend text must use guarded Guardian sink: {source.name}")
    if source.name!="guardian_ui_hekate.c" and re.search(r"\bgfx_putc\s*\(",code):
        raise SystemExit(f"Only the checked backend may call gfx_putc: {source.name}")
if "guardian_ui_backup_summary(&r.info)" not in (root/"main.c").read_text():
    raise SystemExit("Result screen must use the exact summary function covered by frontend tests")
print("PASS: all Guardian frontend text uses the guarded sink; only checked backend emits glyphs")
