#!/usr/bin/env python3
"""Build an isolated, SD-only Hekate-derived payload from pinned source."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

COMMIT="8e1392df4569ac501a115867056b13c361429e9e"
ARCHIVE_SHA256="c25d3138182d29973c332bc9e448f5bcf9ab133a0d4cc5b75f282adcf36198b4"
URL=f"https://codeload.github.com/CTCaer/hekate/zip/{COMMIT}"
ROOT=Path(__file__).resolve().parent.parent
TMP=Path.home()/".codex"/"tmp"
UPSTREAM=TMP/"guardian-hekate-upstream"
STAGE=TMP/"guardian-rescue-build"
DIST=ROOT/"dist"/"rescue"

def pinned_upstream():
    TMP.mkdir(parents=True,exist_ok=True)
    archive=TMP/f"guardian-hekate-{COMMIT}.zip"
    if not archive.exists():
        with urllib.request.urlopen(URL,timeout=90) as response:
            archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=ARCHIVE_SHA256:
        raise RuntimeError("Pinned Hekate source archive checksum does not match")
    # Always stage from the immutable commit archive, never from installed files.
    return archive

def extract(archive):
    if STAGE.resolve().parent!=TMP.resolve() or STAGE.name!="guardian-rescue-build":
        raise RuntimeError("Unsafe build staging path")
    if STAGE.exists():
        shutil.rmtree(STAGE)
    STAGE.mkdir()
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            parts=Path(item.filename).parts[1:]
            if not parts or item.is_dir(): continue
            if any(x in ("..","") for x in parts): raise RuntimeError("Unsafe source archive")
            path=STAGE.joinpath(*parts)
            if not path.resolve().is_relative_to(STAGE.resolve()): raise RuntimeError("Unsafe source path")
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(z.read(item))

def find_source(name):
    paths=[p for p in ROOT.rglob(name) if "dist" not in p.relative_to(ROOT).parts]
    if len(paths)!=1: raise RuntimeError(f"Expected one {name}, found {paths}")
    return paths[0]

def patch():
    dest=STAGE/"bootloader"/"guardian"
    dest.mkdir()
    for name in ("guardian_core.c","guardian_sha256.c","guardian_core.h","guardian_sha256.h","guardian_addons.c","guardian_addons.h"):
        shutil.copy2(find_source(name),dest/name)
    for name in ("guardian_fatfs.c","guardian_fatfs.h","guardian_format.c","test_format.c","guardian_entry.S","guardian_cid.c","guardian_cid.h","test_cid.c"):
        shutil.copy2(ROOT/"rescue"/name,dest/name)
    shutil.copy2(ROOT/"rescue"/"main.c",STAGE/"bootloader"/"main.c")
    makefile=STAGE/"Makefile"
    source=makefile.read_text()
    begin=source.index("# Main and graphics.")
    end=source.index("GFX_INC",begin)
    source=source[:begin]+'''# Guardian build: SD-only. No eMMC/emuMMC, HOS, L4T, UMS or Nyx.
OBJS = start exception_handlers guardian_entry main heap gfx \\
 bpmp ccplex clock di i2c irq timer mc sdram minerva smmu \\
 gpio pinmux pmc se tsec uart fuse kfuse sdmmc sdmmc_driver sd \\
 bq24193 max17050 max7762x max77620-rtc hw_init btn ianos util config \\
 diskio ff ffunicode ffsystem elfload elfreloc_arm \\
 guardian_core guardian_sha256 guardian_addons guardian_fatfs guardian_format guardian_cid
OBJS := $(addsuffix .o, $(OBJS))
OBJS := $(addprefix $(BUILDTDIR)/, $(OBJS))
BDKINC += -I./bootloader/guardian

'''+source[end:]
    source=source.replace(" -DBDK_EMUMMC_ENABLE","")
    source=source.replace("# BDK defines.","CUSTOMDEFINES += -Dsnprintf=guardian_snprintf\n\n# BDK defines.")
    source=source.replace("-fno-inline -std=gnu11","-fno-inline -fstack-usage -Wstack-usage=8192 -std=gnu11")
    source=source.replace("MODULEDIRS := $(wildcard modules/*)","MODULEDIRS := modules/hekate_libsys_minerva")
    source=source.replace("NYXDIR := $(wildcard nyx)","NYXDIR :=")
    source=source.replace("TARGET := hekate","TARGET := ninmagic-guardian-rescue")
    makefile.write_text(source)
    minerva=STAGE/"bdk"/"mem"/"minerva.c"
    minerva.write_text(minerva.read_text().replace("bootloader/sys/libsys_minerva.bso","config/NinMagicGuardian/rescue/libsys_minerva.bso"))
    # This particular low-level driver exposes drive zero only, the SD card.
    diskio=STAGE/"bootloader"/"libs"/"fatfs"/"diskio.c"
    source=diskio.read_text()
    source=source.replace("return sdmmc_storage_read(&sd_storage", "if (pdrv != 0) return RES_PARERR;\n\treturn sdmmc_storage_read(&sd_storage")
    source=source.replace("return sdmmc_storage_write(&sd_storage", "if (pdrv != 0) return RES_PARERR;\n\treturn sdmmc_storage_write(&sd_storage")
    diskio.write_text(source)
    sdmmc=STAGE/"bdk/storage/sdmmc.c"
    source=sdmmc.read_text()
    begin=source.index("static int _sdmmc_storage_handle_io_error(")
    end=source.index("static int _sdmmc_storage_readwrite(",begin)
    source=source[:begin]+'''/* Guardian: I/O recovery must never initialize the eMMC controller. */
static int _sdmmc_storage_handle_io_error(sdmmc_storage_t *storage, bool first_reinit)
{
 if (storage != &sd_storage || storage->sdmmc->id != SDMMC_1) return 1;
 u8 previous_cid[16]; memcpy(previous_cid, storage->raw_cid, sizeof(previous_cid));
 sd_error_count_increment(SD_ERROR_RW_FAIL);
 int res = first_reinit ? sd_initialize(true) : sd_init_retry(true);
 /* A removed/replaced card must not inherit an interrupted I/O operation. */
 if (!res && memcmp(previous_cid, storage->raw_cid, sizeof(previous_cid))) return 1;
 if (res) sd_error_count_increment(SD_ERROR_INIT_FAIL);
 return res;
}

'''+source[end:]
    needle="static int _sdmmc_storage_readwrite(sdmmc_storage_t *storage, u32 sector, u32 num_sectors, void *buf, u32 is_write)\n{"
    if needle not in source: raise RuntimeError("Pinned SDMMC read/write entry changed")
    source=source.replace(needle,needle+"\n\tif (storage != &sd_storage || storage->sdmmc->id != SDMMC_1) return 1;")
    sdmmc.write_text(source)
    (STAGE/"GUARDIAN_SOURCE.json").write_text(json.dumps({"upstream":URL,"commit":COMMIT,"license":"GPL-2.0-only","scope":"SD-only custom entry; not original Hekate product","hardware_tested":False},indent=2))
    shutil.copy2(ROOT/"rescue"/"README.md",STAGE/"GUARDIAN_README.md")
    (STAGE/"GUARDIAN_BUILD.md").write_text("""# Build this corresponding source\n\nInstall devkitPro devkitARM and GNU Make.\n\n```sh\nexport DEVKITARM=/path/to/devkitARM\nexport PATH=\"$DEVKITARM/bin:$PATH\"\nmake -j4\n```\n\nOutputs: output/ninmagic-guardian-rescue.bin and output/libsys_minerva.bso.\nThis Makefile already contains all Guardian modifications; no patch step is required.\nHardware has not been tested. See GUARDIAN_README.md and LICENSE.\n""")

def msys_path(p):
    text=str(p.resolve()).replace("\\","/")
    return "/"+text[0].lower()+text[2:] if len(text)>1 and text[1]==":" else text

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--devkitpro",default="C:/devkitPro" if os.name=="nt" else os.environ.get("DEVKITPRO","/opt/devkitpro"))
    ap.add_argument("--jobs",type=int,default=4)
    args=ap.parse_args()
    kit=Path(args.devkitpro)
    bash=kit/"msys2/usr/bin/bash.exe"
    if not bash.exists(): raise RuntimeError(f"MSYS2 missing: {bash}")
    archive=pinned_upstream(); extract(archive); patch()
    env=dict(os.environ)
    env["DEVKITPRO"]=msys_path(kit)
    env["DEVKITARM"]=msys_path(kit/"devkitARM")
    command=f"export PATH={shlex.quote(msys_path(kit/'devkitARM/bin'))}:$PATH; cd {shlex.quote(msys_path(STAGE))}; make -j{max(1,args.jobs)}"
    log=TMP/"guardian-rescue-build.log"
    with log.open("w",encoding="utf-8") as out:
        result=subprocess.run([str(bash),"-lc",command],env=env,stdout=out,stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(encoding="utf-8",errors="replace")[-14000:])
        raise SystemExit(result.returncode)
    unc=STAGE/"output/ninmagic-guardian-rescue_unc.bin"
    binary=STAGE/"output/ninmagic-guardian-rescue.bin"
    if unc.stat().st_size>140288 or binary.stat().st_size>126296:
        raise RuntimeError("Payload exceeds Hekate safe chainload size limit")
    symbols=subprocess.check_output([str(kit/"devkitARM/bin/arm-none-eabi-nm.exe"),"--defined-only",str(STAGE/"build/ninmagic-guardian-rescue/ninmagic-guardian-rescue.elf")],text=True)
    forbidden=("sdmmc_storage_init_mmc","sdmmc_storage_init_gc","emmc_initialize","emummc_storage_init_mmc","hos_launch","l4t_launch","f_mkfs","f_fdisk")
    linked={line.split()[-1] for line in symbols.splitlines() if line.split()}
    found=sorted(set(forbidden)&linked)
    if found: raise RuntimeError(f"Forbidden non-SD feature linked into rescue: {found}")
    addresses={line.split()[-1]:int(line.split()[0],16) for line in symbols.splitlines() if len(line.split())>=3 and re.fullmatch(r"[0-9a-fA-F]+",line.split()[0])}
    iram_bytes=addresses["__bss_end"]-addresses["__ipl_start"]
    if iram_bytes>140288: raise RuntimeError("Rescue including BSS exceeds safe IRAM budget")
    if addresses["__bss_end"]>=0x40031000: raise RuntimeError("Payload overlaps dedicated IRAM stack")
    audit_text=subprocess.check_output([sys.executable,str(ROOT/"rescue/check_entry.py"),"--elf",str(STAGE/"build/ninmagic-guardian-rescue/ninmagic-guardian-rescue.elf"),"--objdump",str(kit/"devkitARM/bin/arm-none-eabi-objdump.exe")],text=True)
    entry_audit=json.loads(audit_text)
    stack=[]
    for usage in (STAGE/"build/ninmagic-guardian-rescue").glob("*.su"):
        for line in usage.read_text().splitlines():
            fields=line.split("\t")
            if len(fields)==3:
                function=fields[0].rsplit(":",1)[-1]
                if function in linked:stack.append({"function":function,"frame_bytes":int(fields[1]),"kind":fields[2]})
    stack.sort(key=lambda x:x["frame_bytes"],reverse=True)
    DIST.mkdir(parents=True,exist_ok=True)
    (DIST/"stack-usage.json").write_text(json.dumps({"entry":entry_audit,"largest_linked_frames":stack},indent=2))
    for name in ("ninmagic-guardian-rescue.bin","libsys_minerva.bso"):
        shutil.copy2(STAGE/"output"/name,DIST/name)
    embedded=ROOT/"romfs/rescue"
    embedded.mkdir(parents=True,exist_ok=True)
    for name in ("ninmagic-guardian-rescue.bin","libsys_minerva.bso"):
        shutil.copy2(DIST/name,embedded/name)
    shutil.copy2(ROOT/"rescue/ninmagic-guardian.ini",embedded/"ninmagic-guardian.ini")
    shutil.copy2(STAGE/"LICENSE",ROOT/"rescue/GPL-2.0.txt")
    # Include the exact corresponding source and build files for every GPL payload.
    source_tar=ROOT/"dist/NinMagic-Guardian-Rescue-source.tar.gz"
    with tarfile.open(source_tar,"w:gz") as tar:
        for path in sorted(STAGE.rglob("*")):
            rel=path.relative_to(STAGE)
            if path.is_file() and not any(x in ("build","output") for x in rel.parts):
                tar.add(path,arcname=str(Path("NinMagic-Guardian-Rescue-source")/rel))
    info={"source_commit":COMMIT,"source_archive_sha256":hashlib.sha256(archive.read_bytes()).hexdigest(),"uncompressed_bytes":unc.stat().st_size,"iram_including_bss_bytes":iram_bytes,"entry_audit":entry_audit,"hardware_tested":False,"non_sd_symbols_absent":list(forbidden),"files":{p.name:{"bytes":p.stat().st_size,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()} for p in DIST.iterdir() if p.is_file() and p.name!="build-info.json"}}
    (DIST/"build-info.json").write_text(json.dumps(info,indent=2))
    print(json.dumps(info,indent=2))
    print("Source:",source_tar)
    print("Log:",log)

if __name__=="__main__": main()
