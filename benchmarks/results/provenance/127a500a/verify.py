#!/usr/bin/env python3
"""Verify the recorded measurement inputs, substituting archived originals."""
import hashlib
import json
from pathlib import Path

archive = Path(__file__).resolve().parent
root = archive.parents[3]
manifest = json.loads((archive / "manifest.json").read_text())
digest = hashlib.sha256()
for entry in manifest["files"]:
    relative = entry["path"]
    override = manifest["overrides"].get(relative)
    path = archive / override if override else root / relative
    content = path.read_bytes()
    if hashlib.sha256(content).hexdigest() != entry["sha256"]:
        raise SystemExit(f"Changed measurement input: {relative}")
    digest.update(relative.encode("utf-8") + b"\0" + content + b"\0")
if digest.hexdigest() != manifest["source_sha256"]:
    raise SystemExit("Measurement manifest does not match the recorded source hash")
print("Verified measurement source:", digest.hexdigest())
