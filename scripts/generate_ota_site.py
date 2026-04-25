#!/usr/bin/env python3
import argparse
import datetime as dt
import hashlib
import json
import shutil
from pathlib import Path


VARIANTS = {
    "esp32s3_lcd128": {
        "board_name": "ESP32-S3 Touch LCD 1.28",
        "chip": "esp32s3",
    },
    "esp32s3_amoled175": {
        "board_name": "ESP32-S3 Touch AMOLED 1.75",
        "chip": "esp32s3",
    },
    "esp32c6_amoled143": {
        "board_name": "ESP32-C6 Touch AMOLED 1.43",
        "chip": "esp32c6",
    },
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def resolve_binary_path(publish_root: Path, variant: str) -> Path:
    candidates = [
        publish_root / variant / "qnob-screen.bin",
        publish_root / f"binaries-{variant}" / "qnob-screen.bin",
        publish_root / f"binaries-{variant}" / variant / "qnob-screen.bin",
        publish_root / f"binaries-{variant}" / "Binaries" / variant / "qnob-screen.bin",
    ]
    for path in candidates:
        if path.is_file():
            return path

    recursive_matches = sorted(
        path for path in publish_root.rglob("qnob-screen.bin")
        if variant in {part.name for part in path.parents}
    )
    if recursive_matches:
        return recursive_matches[0]

    checked = "\n  - ".join(str(p) for p in candidates)
    discovered = "\n  - ".join(str(p) for p in recursive_matches[:10]) or "<none>"
    raise FileNotFoundError(
        f"Missing OTA binary for {variant}. Checked:\n  - {checked}\nDiscovered qnob-screen.bin files:\n  - {discovered}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate OTA site payload for GitHub Pages")
    parser.add_argument("--publish-root", required=True, help="Root containing Binaries/<variant>/qnob-screen.bin")
    parser.add_argument("--site-root", required=True, help="Output directory for static OTA site files")
    parser.add_argument("--release-tag", required=True, help="Git tag, expected format release-x.y.z.t")
    parser.add_argument("--base-url", required=True, help="Public base URL ending in /ota")
    args = parser.parse_args()

    publish_root = Path(args.publish_root)
    site_root = Path(args.site_root)
    release_tag = args.release_tag
    version = release_tag[len("release-"):] if release_tag.startswith("release-") else release_tag
    published_at = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    base_url = args.base_url.rstrip("/")

    latest_index = {
        "channel": "stable",
        "release_tag": release_tag,
        "version": version,
        "published_at": published_at,
        "variants": {},
    }

    for variant, meta in VARIANTS.items():
        binary_path = resolve_binary_path(publish_root, variant)

        release_binary_path = site_root / "ota" / "releases" / release_tag / variant / "qnob-screen.bin"
        release_binary_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(binary_path, release_binary_path)

        binary_sha256 = sha256_file(binary_path)
        binary_size = binary_path.stat().st_size
        binary_url = f"{base_url}/releases/{release_tag}/{variant}/qnob-screen.bin"

        manifest = {
            "channel": "stable",
            "release_tag": release_tag,
            "version": version,
            "variant": variant,
            "board_name": meta["board_name"],
            "chip": meta["chip"],
            "published_at": published_at,
            "binary_url": binary_url,
            "sha256": binary_sha256,
            "size_bytes": binary_size,
        }

        write_json(site_root / "ota" / "releases" / release_tag / f"{variant}.json", manifest)
        write_json(site_root / "ota" / "latest" / f"{variant}.json", manifest)

        latest_index["variants"][variant] = {
            "version": version,
            "manifest_url": f"{base_url}/latest/{variant}.json",
            "binary_url": binary_url,
            "sha256": binary_sha256,
            "size_bytes": binary_size,
        }

    write_json(site_root / "ota" / "latest" / "index.json", latest_index)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())