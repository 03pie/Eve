"""Assemble a Windows x64 Cordis platform-only snapshot from a built Release host."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import zipfile

import package_preview as common
from preview_pe import file_version, inspect_pe


DEPLOYMENT_SHA = "41cd2aa93b23f0780bd1f7fb6a934a82933556bf49303b43e360997ca640702f"
DEPLOYMENT_MANIFEST_SHA = "fdff0203a649922a43b3668574b30c0eec09313f5d038065467515072e45b13b"
DEPLOYMENT_PREFIX = "Cordis-0.1.5-preview.3-windows-x64/"
IMAGEFORMATS_SHA = "bf8285c7ce04284527ab823ddc7cf48a1bb79131db3a7127342167f4814253d7"
IMAGEFORMATS_PREFIX = "qtimageformats-everywhere-src-5.15.2/src/3rdparty/libwebp/"
IMAGEFORMATS_URL = ("https://download.qt.io/archive/qt/5.15/5.15.2/submodules/"
                    "qtimageformats-everywhere-src-5.15.2.tar.xz")
HOST_FILES = ("cordis_plugin_workbench.exe", "cordis_log_query.exe", "cordis_core.dll",
              "cordis_qt.dll", "cordis_ui.dll", "ElaWidgetTools.dll")
QT_DLLS = ("Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll")
WEBP_PLUGIN = "plugins/imageformats/qwebp.dll"
SYSTEM_DLLS = common.SYSTEM_DLLS | {"combase", "runtimeobject", "shcore"}


def local_input(source, name, role):
    source = common.regular(source)
    identity = common.identity(source)
    return {"path": name, "bytes": identity["bytes"], "sha256": common.sha(source),
            "role": role, "source": {"path": str(source), "identity": identity}}


def archive_input(source, name, member, digest, role, target):
    destination = target / common.relative_path(name)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as writer:
        shutil.copyfileobj(source, writer)
    return {"path": name, "bytes": destination.stat().st_size, "sha256": common.sha(destination),
            "role": role, "source": {"archiveSha256": digest, "entry": member}}


def deployment_files(archive, payload, qt):
    common.require(common.sha(archive) == DEPLOYMENT_SHA, "P6 runtime source SHA-256 differs")
    with zipfile.ZipFile(archive) as source:
        manifest = source.read(DEPLOYMENT_PREFIX + "build-manifest.json")
        common.require(hashlib.sha256(manifest).hexdigest() == DEPLOYMENT_MANIFEST_SHA,
                       "P6 runtime source manifest differs")
        inventory = {row["path"]: row for row in json.loads(manifest)["files"]}
        selected = {*QT_DLLS, *common.CRT_FILES, "qt.conf", *("plugins/" + p for p in common.QT_PLUGINS)}
        selected.update(name for name in inventory if name.startswith("licenses/"))
        common.require(selected <= inventory.keys() and len(selected) == len({p.casefold() for p in selected}),
                       "P6 runtime dependency set differs")
        for name in QT_DLLS:
            current = common.regular(qt / "bin" / name)
            common.require(common.sha(current) == inventory[name]["sha256"],
                           "WebP Qt kit differs from the fixed deployment: " + name)
        records = []
        for name in sorted(selected):
            common.relative_path(name)
            member = DEPLOYMENT_PREFIX + name
            with source.open(member) as content:
                record = archive_input(content, name, member, DEPLOYMENT_SHA, "fixed-runtime-or-license", payload)
            facts = inventory[name]
            common.require(record["bytes"] == facts["bytes"] and record["sha256"] == facts["sha256"],
                           "P6 runtime member differs: " + name)
            records.append(record)
        return records


def webp_notices(archive, payload):
    common.require(common.sha(archive) == IMAGEFORMATS_SHA, "Qt Image Formats source SHA-256 differs")
    records = []
    with tarfile.open(archive, "r:xz") as source:
        for name in ("COPYING", "PATENTS", "qt_attribution.json"):
            member = source.getmember(IMAGEFORMATS_PREFIX + name)
            common.require(member.isfile(), "WebP notice is not a regular member")
            with source.extractfile(member) as content:
                records.append(archive_input(content, "licenses-qtimageformats/" + name, member.name,
                                             IMAGEFORMATS_SHA, "webp-notice", payload))
    return records


def validate_pe(payload, records):
    available = {row["path"].casefold() for row in records}
    system = set()
    for row in records:
        if not row["path"].lower().endswith((".exe", ".dll")):
            continue
        path = payload / row["path"]
        row["pe"] = inspect_pe(path)
        row["pe"]["fileVersion"] = file_version(path)
        if row["path"] in QT_DLLS or row["path"].startswith("plugins/"):
            common.require(row["pe"]["fileVersion"] == "5.15.2.0", "Qt build version differs: " + row["path"])
        for name in row["pe"]["imports"] + row["pe"]["delayImports"]:
            adjacent = (Path(row["path"]).parent / name).as_posix().casefold()
            if name in available or adjacent in available:
                continue
            common.require(name[:-4] in SYSTEM_DLLS or re.fullmatch(r"(?:api|ext)-ms-win-[a-z0-9-]+\.dll", name),
                           "Unresolved Windows dependency: " + name)
            system.add(name)
    return sorted(system)


def pack_and_extract(payload, archive, extracted, sums):
    common.verify_payload(payload, sums)
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as packed:
        for name in sorted(sums):
            packed.write(payload / name, payload.name + "/" + name)
    with zipfile.ZipFile(archive) as packed:
        members = {payload.name + "/" + name for name in sums}
        common.require(len(packed.infolist()) == len(members) and set(packed.namelist()) == members,
                       "Platform ZIP file set differs")
        extracted.mkdir()
        for name in sorted(sums):
            destination = extracted / common.relative_path(name)
            destination.parent.mkdir(parents=True, exist_ok=True)
            with packed.open(payload.name + "/" + name) as source, destination.open("xb") as target:
                shutil.copyfileobj(source, target)
            common.require(destination.stat().st_size == (payload / name).stat().st_size,
                           "Platform ZIP member length differs: " + name)
    common.verify_payload(extracted, sums)
    common.verify_payload(payload, sums)


def package(args):
    common.require(re.fullmatch(r"(\d+\.\d+\.\d+)-platform-preview\.[1-9]\d*", args.delivery_version),
                   "Invalid platform delivery version")
    common.require(args.output.is_absolute(), "Output must be an absolute path")
    host = common.reject_reparse(args.host_root)
    qt = common.reject_reparse(args.qt_root)
    deployment = common.regular(args.deployment_archive)
    imageformats = common.regular(args.imageformats_source)
    output = common.reject_reparse(args.output)
    common.require(not output.exists() and all(not item.is_relative_to(output)
                                              for item in (host, qt, deployment, imageformats, common.ROOT)),
                   "Output must be a new directory disjoint from selected inputs")
    common.require(not any(output.is_relative_to(item) for item in (host, qt, deployment.parent, imageformats.parent)),
                   "Output cannot be nested in an input directory")
    local = [local_input(host / name, name, "platform-release") for name in HOST_FILES]
    local.append(local_input(qt / WEBP_PLUGIN, WEBP_PLUGIN, "webp-runtime"))
    local.append(local_input(common.ASSETS / "platform" / "README.txt", "README.txt", "operator-guide"))
    tool_digests = {name: common.sha(common.regular(common.ROOT / "tools" / name)) for name in
                    ("package_platform.py", "package_preview.py", "preview_pe.py")}
    name = "Cordis-" + args.delivery_version + "-windows-x64"
    output.parent.mkdir(parents=True, exist_ok=True)
    common.reject_reparse(output.parent)
    output.mkdir()
    incomplete = output / "INCOMPLETE.txt"
    common.write_exclusive(incomplete, b"Packaging incomplete.\n")
    payload = output / name
    payload.mkdir()
    common.copy_inputs(local, payload)
    records = local + deployment_files(deployment, payload, qt) + webp_notices(imageformats, payload)
    system = validate_pe(payload, records)
    common.require(len(records) == len({row["path"].casefold() for row in records}), "Duplicate payload file")
    manifest = {"schema": "cordis.platform.package/1", "platform": "windows-x64",
                "deliveryVersion": args.delivery_version, "scope": "internal-technical-preview",
                "entryPoints": list(HOST_FILES[:2]), "businessModules": [],
                "files": sorted(records, key=lambda row: row["path"]), "systemDependencies": system,
                "deploymentArchiveSha256": DEPLOYMENT_SHA,
                "webpSource": {"url": IMAGEFORMATS_URL, "sha256": IMAGEFORMATS_SHA},
                "producerSha256": tool_digests,
                "source": {"commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=common.ROOT,
                                                             text=True).strip(),
                           "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=common.ROOT))}}
    manifest_bytes = common.canonical(manifest)
    common.write_exclusive(payload / "build-manifest.json", manifest_bytes)
    manifest_sha = hashlib.sha256(manifest_bytes).hexdigest()
    sums = {row["path"]: row["sha256"] for row in records}
    sums["build-manifest.json"] = manifest_sha
    sum_bytes = "".join(f"{digest}  {path}\n" for path, digest in sorted(sums.items())).encode("utf-8")
    common.write_exclusive(payload / "SHA256SUMS.txt", sum_bytes)
    sums["SHA256SUMS.txt"] = hashlib.sha256(sum_bytes).hexdigest()
    partial = output / (name + ".zip.partial")
    extracted = output / "verification-extracted"
    pack_and_extract(payload, partial, extracted, sums)
    for row in local:
        source = common.regular(row["source"]["path"])
        common.require(common.identity(source) == row["source"]["identity"] and common.sha(source) == row["sha256"],
                       "Local input changed before package closure: " + str(source))
    common.require(common.sha(deployment) == DEPLOYMENT_SHA and common.sha(imageformats) == IMAGEFORMATS_SHA,
                   "Frozen deployment input changed before package closure")
    common.require(all(common.sha(common.ROOT / "tools" / path) == digest for path, digest in tool_digests.items()),
                   "Packaging tools changed before package closure")
    archive = partial.rename(output / (name + ".zip"))
    archive_sha = common.sha(archive)
    result = {"schema": "cordis.platform.archive-verification/1", "passed": True,
              "archive": str(archive), "archiveSha256": archive_sha, "archiveBytes": archive.stat().st_size,
              "manifestSha256": manifest_sha, "files": len(sums), "extraction": str(extracted),
              "fullExtractionLengthAndSha256Verified": True, "payloadExecuted": False}
    common.write_exclusive(output / (name + ".verification.json"), common.canonical(result))
    common.write_exclusive(archive.with_suffix(".zip.sha256"), f"{archive_sha}  {archive.name}\n".encode("utf-8"))
    incomplete.unlink()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--host-root", type=Path, required=True)
    cli.add_argument("--qt-root", type=Path, required=True)
    cli.add_argument("--deployment-archive", type=Path, required=True)
    cli.add_argument("--imageformats-source", type=Path, required=True)
    cli.add_argument("--output", type=Path, required=True)
    cli.add_argument("--delivery-version", default="0.1.5-platform-preview.1")
    package(cli.parse_args())
