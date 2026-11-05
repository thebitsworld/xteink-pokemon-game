#!/usr/bin/env python3
"""The bundle holds exactly what src/LanguageBundle.cpp installs, and a matching checksum."""
import hashlib
from pathlib import Path
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from package_languages import package  # noqa: E402

with tempfile.TemporaryDirectory() as tmp:
    bundle = Path(tmp) / "xteink-pokemon-languages.zip"
    package(Path(tmp) / "sources.zip", "0.0", bundle_output=bundle)
    digest = Path(f"{bundle}.sha256").read_text().split()[0]
    assert digest == hashlib.sha256(bundle.read_bytes()).hexdigest()
    with zipfile.ZipFile(bundle) as archive:
        names = set(archive.namelist())
        manifest = archive.read(".crossink/bundle-manifest.txt").decode().split()
    assert set(manifest) | {".crossink/bundle-manifest.txt"} == names
    assert all((n.startswith(".crossink/languages/") and n.endswith(".yaml") and n.count("/") == 2)
               or (n.startswith(".crossink/hyphenation/") and n.endswith(".cphyph") and n.count("/") == 2)
               for n in manifest)
    assert sum(n.endswith(".cphyph") for n in manifest) == 9
    assert ".crossink/languages/english.yaml" not in names
print("language bundle ok")
