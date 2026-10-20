"""Validate an Alpakit merged release and assemble a reviewable SMR publication kit."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "mod/BuildingSkills"
TARGETS = {
    "Windows": [
        "Binaries/Win64/FactoryGameSteam-BuildingSkills-Win64-Shipping.dll",
        "Binaries/Win64/FactoryGameEGS-BuildingSkills-Win64-Shipping.dll",
    ],
    "WindowsServer": ["Binaries/Win64/FactoryServer-BuildingSkills-Win64-Shipping.dll"],
    "LinuxServer": ["Binaries/Linux/libFactoryServer-BuildingSkills-Linux-Shipping.so"],
}
ASSETS = [
    "BuildingSkills.uasset", "RootGameInstance_BuildingSkills.uasset",
    "RootGameWorld_BuildingSkills.uasset", "MC_BuildingSkills.uasset",
    "MC_EditOperations.uasset", "IA_ToggleEditMode.uasset", "IA_TogglePlan.uasset",
    "MC_Plan.uasset", "IA_PlanSettings.uasset", "IA_PlanScrollUp.uasset",
    "IA_PlanScrollDown.uasset", "IA_PlanDelete.uasset", "IA_PlanUndo.uasset",
    "IA_PlanRedo.uasset",
]
NOTICES = [
    "LICENSE.txt", "ICU_64-1_License.txt", "SML_LICENSE.txt",
    "THIRD_PARTY_NOTICES.txt", "Icon128.png",
]
DEBUG_SUFFIXES = {".pdb", ".debug", ".sym"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def validate(archive, descriptor):
    files = archive.namelist()
    require(len(set(n.lower() for n in files)) == len(files), "Duplicate archive paths")
    for name in files:
        path = PurePosixPath(name)
        require(not path.is_absolute() and ".." not in path.parts and
                "\\" not in name and ":" not in name, f"Unsafe path: {name}")
        require(path.parts and path.parts[0] in TARGETS, f"Unexpected target: {name}")
    summaries = {}
    for platform, natives in TARGETS.items():
        prefix = platform + "/"
        require(prefix + "BuildingSkills.uplugin" in files, f"{platform}: missing descriptor")
        packaged = json.loads(archive.read(prefix + "BuildingSkills.uplugin"))
        for key, value in descriptor.items():
            require(packaged.get(key) == value, f"{platform}: descriptor differs for {key}")
        require(packaged.get("GameFeature") is True, f"{platform}: missing GameFeature marker")
        binaries = {}
        for name in natives:
            require(prefix + name in files, f"{platform}: missing packaged binary {name}")
            data = archive.read(prefix + name)
            local = PLUGIN / name
            require(local.is_file(), f"Missing compiled binary: {name}")
            require(sha(data) == sha(local.read_bytes()), f"{platform}: stale binary {name}")
            require(len(data) > 1024, f"{platform}: implausible binary size")
            if name.endswith(".dll"):
                manifest_name = name.replace("-BuildingSkills", "").replace(".dll", ".modules")
            else:
                manifest_name = "Binaries/Linux/FactoryServer-Linux-Shipping.modules"
            manifest = json.loads(archive.read(prefix + manifest_name))
            require(manifest.get("BuildId") == "SML", f"{platform}: invalid BuildId")
            require(manifest.get("Modules", {}).get("BuildingSkills") == Path(name).name,
                    f"{platform}: invalid module mapping")
            binaries[name] = sha(data)
        containers = [n for n in files if n.startswith(prefix) and n.endswith(".utoc")]
        require(len(containers) == 1, f"{platform}: expected one cooked container")
        toc = archive.read(containers[0])
        for asset in ASSETS:
            require(asset.encode() in toc, f"{platform}: missing cooked {asset}")
        for extension in (".pak", ".ucas"):
            payloads = [n for n in files if n.startswith(prefix) and n.endswith(extension)]
            require(len(payloads) == 1, f"{platform}: missing/ambiguous {extension}")
            require(archive.getinfo(payloads[0]).file_size > 0, f"{platform}: empty {extension}")
        for name in NOTICES:
            require(archive.read(prefix + "Resources/" + name) ==
                    (PLUGIN / "Resources" / name).read_bytes(),
                    f"{platform}: missing/stale notice or icon {name}")
        summaries[platform] = {"native_sha256": binaries, "required_cooked_assets": ASSETS}
    return summaries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path, help="Alpakit merged BuildingSkills.zip")
    args = parser.parse_args()
    descriptor = json.loads((PLUGIN / "BuildingSkills.uplugin").read_text(encoding="utf-8-sig"))
    version = descriptor["SemVersion"]
    require(version == descriptor["VersionName"], "Version fields differ")
    metadata = json.loads((ROOT / "docs/publishing/METADATA.json").read_text(encoding="utf-8"))
    require(metadata["version"] == version, "Publication metadata has stale version")
    require((ROOT / "LICENSE").read_bytes() == (PLUGIN / "Resources/LICENSE.txt").read_bytes(),
            "Packaged project license differs from root license")
    with zipfile.ZipFile(args.archive) as incoming:
        summaries = validate(incoming, descriptor)
        # Write nothing until the complete incoming archive passes validation.
        out = ROOT / "Saved/Publication" / version
        out.mkdir(parents=True, exist_ok=True)
        (out / "videos").mkdir(exist_ok=True)
        upload = out / "BuildingSkills.zip"
        require(args.archive.resolve() != upload.resolve(), "Input would be overwritten")
        excluded = []
        with zipfile.ZipFile(upload, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as result:
            for info in sorted(incoming.infolist(), key=lambda item: item.filename):
                if info.is_dir():
                    continue
                if PurePosixPath(info.filename).suffix.lower() in DEBUG_SUFFIXES:
                    excluded.append(info.filename)
                    continue
                result.writestr(info, incoming.read(info))
    with zipfile.ZipFile(upload) as final:
        require(final.testzip() is None, "Upload archive CRC check failed")
        validate(final, descriptor)
    for path in (ROOT / "docs/publishing").rglob("*"):
        if path.is_file():
            destination = out / path.relative_to(ROOT / "docs/publishing")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    # Include only tracked and explicitly non-ignored project files.
    source_files = sorted(set(git("ls-files", "-z", "--cached", "--others",
                                 "--exclude-standard").decode("utf-8").split("\0")) - {""})
    source = out / f"BuildingSkills-{version}-source.zip"
    with zipfile.ZipFile(source, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as result:
        for name in source_files:
            path = ROOT / name
            require(not path.is_symlink(), f"Refusing source symlink: {name}")
            require(not any(part in {"Saved", "Binaries", "Intermediate", ".git", ".aws"}
                            for part in PurePosixPath(name).parts),
                    f"Generated/private path in source listing: {name}")
            if path.is_file():
                result.write(path, "BuildingSkills/" + name)
    evidence_path = ROOT / f"Saved/Releases/BuildingSkills-{version}-verification.json"
    evidence = json.loads(evidence_path.read_text(encoding="utf-8-sig"))
    require(evidence["Version"] == version, "Build evidence has stale version")
    report = {
        "version": version,
        "prepared_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_base_commit": git("rev-parse", "HEAD").decode().strip(),
        "source_working_tree_changes": git("status", "--short").decode().splitlines(),
        "source_snapshot_sha256": sha(source.read_bytes()),
        "upload_sha256": sha(upload.read_bytes()),
        "alpakit_input_sha256": sha(args.archive.read_bytes()),
        "targets": summaries,
        "checks": [
            "Three target folders; Steam and Epic binaries both present",
            "Every packaged native binary matches its local compiled output",
            "Matching source descriptors and SML module manifests",
            "Required Edit and Plan cooked filenames present",
            "Cooked pak/ucas payloads present",
            "License notices and icon match source resources",
            "No duplicate/unsafe archive paths; CRC check passed",
        ],
        "excluded_debug_symbols": excluded,
        "prior_version_evidence": evidence,
        "gameplay_tests_run_during_publication_preparation": False,
        "published": False,
    }
    (out / "verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    checksums = "\n".join(
        f"{sha(p.read_bytes())}  {p.relative_to(out).as_posix()}"
        for p in sorted(out.rglob("*")) if p.is_file() and p.name != "SHA256SUMS.txt"
    )
    (out / "SHA256SUMS.txt").write_text(checksums + "\n", encoding="ascii")
    print(json.dumps({"output": str(out), "upload_bytes": upload.stat().st_size,
                      "targets": list(summaries), "sha256": sha(upload.read_bytes())}, indent=2))


if __name__ == "__main__":
    main()
