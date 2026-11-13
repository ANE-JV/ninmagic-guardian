#!/usr/bin/env python3
"""Regression guard: never invalidate a live C stack frame in the rescue."""
from pathlib import Path
import argparse,json,re,subprocess
ROOT=Path(__file__).resolve().parent.parent
def check_sources():
    source=(ROOT/"rescue/main.c").read_text()
    entry=(ROOT/"rescue/guardian_entry.S").read_text()
    if re.search(r"\bpivot_stack\s*\(",source):
        raise RuntimeError("A stack pivot inside the C runtime invalidates its frame")
    if re.search(r"\bvoid\s+ipl_main\s*\(",source):
        raise RuntimeError("ipl_main must be the assembly trampoline, not a C frame")
    if not re.search(r"BL\s+guardian_hw_init[\s\S]+LDR\s+SP,\s*=0x4003F000[\s\S]+BL\s+guardian_main",entry,re.I):
        raise RuntimeError("Required HW -> dedicated IRAM stack -> C entry sequence missing")
def check_binary(elf,objdump):
    disassembly=subprocess.check_output([str(objdump),"-d","--disassemble=ipl_main",str(elf)],text=True)
    match=re.search(r"<ipl_main>:\n([\s\S]+)",disassembly)
    if not match: raise RuntimeError("Assembly entry is missing from linked ELF")
    body=match.group(1)
    if re.search(r"\b(push|pop)\b|\b(add|sub)\w*\s+sp",body):
        raise RuntimeError("Assembly entry unexpectedly creates/uses a C-like stack frame")
    hw=body.find("guardian_hw_init"); sp=re.search(r"\bldr\s+sp,",body); runtime=body.find("guardian_main")
    if hw<0 or not sp or runtime<0 or not hw<sp.start()<runtime or "4003f000" not in body.lower():
        raise RuntimeError("Linked trampoline does not set the IRAM stack before C runtime")
    return {"entry":"assembly","stack_bottom":"0x40031000","stack_top":"0x4003f000","stack_bytes":57344,"c_stack_pivot":False}
def main():
    ap=argparse.ArgumentParser();ap.add_argument("--elf",type=Path);ap.add_argument("--objdump",type=Path);args=ap.parse_args()
    check_sources()
    if args.elf:
        if not args.objdump:ap.error("--objdump required with --elf")
        print(json.dumps(check_binary(args.elf,args.objdump)))
    else:print("PASS: C runtime cannot pivot its live frame; assembly entry uses dedicated IRAM stack")
if __name__=="__main__":main()
