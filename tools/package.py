"""Prepare Guardian's SD-root ZIP and corresponding source; never uploads.

The NRO updater replaces only its own NRO. Install the full ZIP to update the
independent Hekate rescue. The user's main Hekate configuration is not replaced.
"""
from pathlib import Path
import hashlib
import json
import zipfile
from publish_update import prepare
from verify_nro import verify, VERSION

ROOT = Path(__file__).resolve().parent.parent
BASE = "https://raw.githubusercontent.com/ANE-JV/ninmagic-guardian/main"


def require(value, message):
    if not value:
        raise ValueError(message)


def read_file(path):
    require(path.is_file() and not path.is_symlink(), "Missing regular package file: " + str(path))
    return path.read_bytes()


def immutable_zip(path, payload):
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(payload.items()):
                entry = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, data)
    require(path.is_file() and not path.is_symlink(), "Package must be a regular file")
    with zipfile.ZipFile(path) as archive:
        require(not archive.testzip() and len(archive.namelist()) == len(payload) and set(archive.namelist()) == set(payload), "ZIP inventory or CRC mismatch")
        for name, data in payload.items():
            require(archive.read(name) == data, "Immutable version differs; increment app version: " + name)


def main():
    dist = ROOT / "dist"
    binary = dist / "NinMagic-Guardian.nro"
    report = verify(binary)
    publication = dist / "publication"
    version_dir = publication / f"versions/v{VERSION}"
    rescue_source_name = f"NinMagic-Guardian-{VERSION}-rescue-source.tar.gz"
    rescue_source = read_file(dist / "NinMagic-Guardian-Rescue-source.tar.gz")
    source_name = f"NinMagic-Guardian-{VERSION}-source.zip"
    sources = {}
    for name in ("README.md", "CHANGELOG.md", "LICENSE", "Makefile", ".gitignore"):
        if name == ".gitignore" and not (ROOT / name).exists():
            continue
        sources["ninmagic-guardian/" + name] = read_file(ROOT / name)
    for folder in ("source", "include", "tools", "tests", "assets", "romfs", "docs", "rescue", "third_party"):
        root = ROOT / folder
        require(root.is_dir() and not root.is_symlink(), "Missing source directory: " + folder)
        for path in sorted(root.rglob("*")):
            if not path.is_file() or "__pycache__" in path.parts or path.suffix in (".pyc", ".o", ".exe"):
                continue
            sources["ninmagic-guardian/" + path.relative_to(ROOT).as_posix()] = read_file(path)
    source_notice = (
        f"NinMagic Guardian {VERSION} - fuentes correspondientes\n"
        f"NRO y herramientas: {BASE}/versions/v{VERSION}/{source_name}\n"
        f"Rescatador Hekate y nucleo compartido: {BASE}/versions/v{VERSION}/{rescue_source_name}\n"
        "La consulta de actualizaciones de la app actualiza solo el NRO.\n"
        "Para actualizar tambien el rescatador, extrae el ZIP completo en la raiz de la SD.\n"
    ).encode("utf-8")
    payload = {
        "switch/NinMagicGuardian/NinMagic-Guardian.nro": read_file(binary),
        "switch/NinMagicGuardian/LEEME.txt": read_file(ROOT / "README.md"),
        "switch/NinMagicGuardian/FUENTES.txt": source_notice,
        "switch/NinMagicGuardian/licencias/GPL-2.0.txt": read_file(ROOT / "LICENSE"),
        "switch/NinMagicGuardian/licencias/FONT-LICENSE.txt": read_file(ROOT / "romfs/FONT-LICENSE.txt"),
        "config/NinMagicGuardian/update-url.txt": (BASE + "/update.txt\n").encode("ascii"),
        "config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin": read_file(dist / "rescue/ninmagic-guardian-rescue.bin"),
        "config/NinMagicGuardian/rescue/libsys_minerva.bso": read_file(dist / "rescue/libsys_minerva.bso"),
        "config/NinMagicGuardian/rescue/LEEME.txt": read_file(ROOT / "rescue/README.md"),
        "config/NinMagicGuardian/rescue/GPL-2.0.txt": read_file(ROOT / "rescue/GPL-2.0.txt"),
        "bootloader/ini/ninmagic-guardian.ini": read_file(ROOT / "rescue/ninmagic-guardian.ini"),
    }
    for notice in sorted((ROOT / "third_party").glob("*.txt")):
        payload["switch/NinMagicGuardian/licencias/" + notice.name] = read_file(notice)
    for name, destination in (
        ("ninmagic-guardian-rescue.bin", "config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin"),
        ("libsys_minerva.bso", "config/NinMagicGuardian/rescue/libsys_minerva.bso"),
        ("ninmagic-guardian.ini", "bootloader/ini/ninmagic-guardian.ini"),
    ):
        require(payload[destination] == read_file(ROOT / "romfs/rescue" / name), "ZIP rescue differs from the resources embedded in the verified NRO: " + name)
    # Validate every required file and cross-check the embedded/external rescue
    # before freezing any versioned output.
    prepared = prepare(binary, BASE, publication)
    immutable_zip(version_dir / source_name, sources)
    rescue_destination = version_dir / rescue_source_name
    if not rescue_destination.exists():
        with rescue_destination.open("xb") as stream:
            stream.write(rescue_source)
    require(read_file(rescue_destination) == rescue_source, "Rescue source already differs in this immutable version")
    package = version_dir / f"NinMagic-Guardian-{VERSION}.zip"
    immutable_zip(package, payload)
    report.update({
        "package": package.name,
        "package_sha256": hashlib.sha256(read_file(package)).hexdigest(),
        "entries": sorted(payload),
        "source": source_name,
        "source_sha256": hashlib.sha256(read_file(version_dir / source_name)).hexdigest(),
        "rescue_source": rescue_source_name,
        "rescue_source_sha256": hashlib.sha256(rescue_source).hexdigest(),
        "rescue_sha256": hashlib.sha256(payload["config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin"]).hexdigest(),
        "manifest": prepared["manifest_url"],
        "updater_scope": "NRO only; use full ZIP for rescue updates",
        "modifies_main_hekate_configuration": False,
        "public_channel_verified": False,
        "console_installed": False,
    })
    (dist / "validation.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
