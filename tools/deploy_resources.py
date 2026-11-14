#!/usr/bin/env python3
"""Deploy runtime skills and verify their SHA-256 manifest without formatting storage."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
RUNTIME_ROOT = "/littlefs/res"


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            value.update(block)
    return value.hexdigest()


def atomic_write(path, data):
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def manifest_entries(target):
    entries = []
    for path in sorted(target.glob("skills/*.md")):
        data = path.read_bytes()
        entries.append({
            "path": path.relative_to(target).as_posix(),
            "runtime_path": RUNTIME_ROOT + "/" + path.relative_to(target).as_posix(),
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
    return entries


def deploy(source, target, manifest):
    source = source.resolve()
    target = target.resolve()
    if not source.is_dir():
        raise RuntimeError("skill source is missing: %s" % source)
    if target == source or source in target.parents:
        raise RuntimeError("target must not be the source directory or its child")
    skills = sorted(source.glob("*.md"))
    if not skills or not (source / "smart_home_scenes.md").is_file():
        raise RuntimeError("skills and smart_home_scenes.md are required")
    (target / "skills").mkdir(parents=True, exist_ok=True)
    for skill in skills:
        atomic_write(target / "skills" / skill.name, skill.read_bytes())
    document = {
        "version": 1,
        "runtime_root": RUNTIME_ROOT,
        "files": manifest_entries(target),
    }
    manifest.parent.mkdir(parents=True, exist_ok=True)
    atomic_write(manifest, (json.dumps(document, indent=2, sort_keys=True) + "\n").encode())
    print("deployed %d skill/catalog files to %s" % (len(skills), target))
    print("manifest: %s" % manifest)


def verify(target, manifest):
    document = json.loads(manifest.read_text(encoding="utf-8"))
    if document.get("version") != 1 or not document.get("files"):
        raise RuntimeError("invalid or empty resource manifest")
    for entry in document["files"]:
        path = target / entry["path"]
        if not path.is_file():
            raise RuntimeError("missing resource: %s" % path)
        if path.stat().st_size != entry["size"] or digest(path) != entry["sha256"]:
            raise RuntimeError("SHA-256 mismatch: %s" % path)
    print("verified %d resources with SHA-256" % len(document["files"]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=ROOT / "resources" / "skills")
    parser.add_argument("--target", type=Path, default=ROOT / "build" / "resources" / "res")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    manifest = args.manifest or args.target.parent / "resource-manifest.json"
    try:
        if args.verify:
            verify(args.target, manifest)
        else:
            deploy(args.source, args.target, manifest)
            verify(args.target, manifest)
    except (OSError, ValueError, RuntimeError) as error:
        print("resource deployment failed: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
