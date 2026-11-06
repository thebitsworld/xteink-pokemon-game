#!/usr/bin/env python3
"""Package editable SD translations and the matching English template for a release."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

from gen_i18n import parse_yaml_file
from generate_language_template import fit_device_cache, generate, write_artifacts
from package_hyphenation import artifacts as hyphenation_artifacts

ROOT = Path(__file__).resolve().parents[1]


def write_bundle(output: Path, entries: dict) -> None:
    """The one ZIP the reader unpacks by itself (src/LanguageBundle.cpp), with its checksum beside it."""
    names = sorted(name for name in entries
                   if (name.startswith(".crossink/languages/") and name.endswith(".yaml"))
                   or (name.startswith(".crossink/hyphenation/") and name.endswith(".cphyph")))
    files = {name: entries[name] for name in names}
    files[".crossink/bundle-manifest.txt"] = ("\n".join(names) + "\n").encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    Path(f"{output}.sha256").write_text(f"{digest}  {output.name}\n")
    print(f"Packaged bundle of {len(names)} files, {output.stat().st_size} bytes: {output}")


def package(output: Path, version: str, benchmark: bool = False, template_output_dir=None,
            source_commit=None, status="local-fixture", bundle_output=None):
    artifacts = generate(version, source_commit, status)
    entries = dict(artifacts)
    entries.update(hyphenation_artifacts())
    # Keep the established archive spelling as an identical compatibility alias.
    entries["english-template.yaml"] = artifacts["english_template.yaml"]
    if template_output_dir is not None:
        write_artifacts(template_output_dir, artifacts)
    source = ROOT / "lib/I18n/translations"
    catalog = json.loads(artifacts["language-template.json"])["entries"]
    prefix = f"_firmware_version: {json.dumps(version, ensure_ascii=False)}\n"
    for path in sorted(source.glob("*.yaml")):
        if path.name == "english.yaml":
            continue
        destination = f".crossink/languages/{path.name}"
        # What does not fit the device's language slot stays out of the file, and shows in English.
        data = parse_yaml_file(str(path))
        kept = fit_device_cache(data, catalog, data.get("_language_code", path.stem))
        lines = [line for line in path.read_text(encoding="utf-8").splitlines(True)
                 if line.split(":", 1)[0] not in data or line.split(":", 1)[0] in kept]
        entries[destination] = (prefix + "".join(lines)).encode("utf-8")
    if benchmark:
        english = (source / "english.yaml").read_text(encoding="utf-8")
        english = english.replace('_language_code: "EN"', '_language_code: "BENCH"\n_keyboard: "EN"')
        english = english.replace('_language_name: "English"', '_language_name: "English benchmark"')
        entries[".crossink/languages/benchmark.yaml"] = (prefix + english).encode("utf-8")
    if bundle_output is not None:
        write_bundle(bundle_output, entries)
    entries["README.md"] = (ROOT / "docs/languages.md").read_bytes()
    entries["manifest.json"] = (json.dumps({
        "firmware": version,
        "format": "CrossInk flat YAML v1",
        "sha256": {name: hashlib.sha256(data).hexdigest() for name, data in sorted(entries.items())},
    }, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
    print(f"Packaged {len(entries)} files: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--benchmark", action="store_true", help="Include the optional English timing fixture")
    parser.add_argument("--template-output-dir", type=Path, help="Also write the atomic editor JSON and YAML pair")
    parser.add_argument("--source-commit", help="Full source snapshot Git SHA")
    parser.add_argument("--status", choices=["local-fixture", "release-build"], default="local-fixture")
    parser.add_argument("--bundle-output", type=Path, help="Also write the ZIP the reader unpacks by itself")
    args = parser.parse_args()
    package(args.output, args.version, args.benchmark, args.template_output_dir, args.source_commit, args.status, args.bundle_output)
