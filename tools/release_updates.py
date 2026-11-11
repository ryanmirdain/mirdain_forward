"""Prepare Forward's Ed25519 keys, release signatures, and appcasts."""

import argparse
import base64
from datetime import datetime, timezone
from email.utils import format_datetime
import hashlib
import html
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "ryanmirdain/mirdain_forward"
FEED_BASE = "https://mirdainsystems.com/forward/updates"
SPARKLE = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", SPARKLE)
PLATFORMS = {
    "macos-arm64": ("MACOS", "macos", "12.0", "mirdain_forward-macos-arm64.dmg"),
    "macos-x86_64": ("MACOS", "macos", "12.0", "mirdain_forward-macos-x86_64.dmg"),
    "windows-x64": ("WINDOWS", "windows-x64", "10.0.17763", "mirdain_forward-windows-x64-setup.exe"),
}


def version_for_tag(tag):
    if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
        raise ValueError("Release tags must have the form v1.2.3")
    version = re.search(r"project\(mirdain_forward VERSION ([\d.]+)", (ROOT / "CMakeLists.txt").read_text())[1]
    if tag != f"v{version}":
        raise ValueError("The release tag must match CMakeLists.txt's project version")
    if not (ROOT / "packaging/updates/notes" / f"{version}.md").read_text().strip():
        raise ValueError("Release notes must not be empty")
    return version


def load_key(platform):
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    name = f"FORWARD_UPDATE_PRIVATE_KEY_{platform}"
    encoded = os.environ.get(name, "").strip()
    if not encoded:
        raise ValueError(f"Missing release secret: {name}")
    # Keys are raw 32-byte Ed25519 seeds, base64 encoded; no key bytes are logged.
    key = Ed25519PrivateKey.from_private_bytes(base64.b64decode(encoded, validate=True))
    public_name = f"FORWARD_UPDATE_PUBLIC_KEY_{platform}"
    public = os.environ.get(public_name, "").strip()
    if not public or key.public_key().public_bytes_raw() != base64.b64decode(public, validate=True):
        raise ValueError(f"{name} does not match {public_name}")
    return key


def key_paths(directory, platform):
    return directory / f"{platform.lower()}-private.key", directory / f"{platform.lower()}-public.key"


def init_keys(args):
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    # This command is only run by the operator, never automatically in CI.
    args.directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    paths = [path for platform in ("MACOS", "WINDOWS") for path in key_paths(args.directory, platform)]
    if any(path.exists() for path in paths):
        raise ValueError("Key files already exist; refusing to replace an existing release identity")
    for platform in ("MACOS", "WINDOWS"):
        private_path, public_path = key_paths(args.directory, platform)
        key = Ed25519PrivateKey.generate()
        with os.fdopen(os.open(private_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "w") as file:
            file.write(base64.b64encode(key.private_bytes_raw()).decode() + "\n")
        public_path.write_text(base64.b64encode(key.public_key().public_bytes_raw()).decode() + "\n")
        print(f"Created {private_path} and {public_path}; back up this directory securely.")


def configure_github(args):
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    # gh reads each private value through stdin; it never appears in argv/stdout.
    for platform in ("MACOS", "WINDOWS"):
        private_path, public_path = key_paths(args.directory, platform)
        key = Ed25519PrivateKey.from_private_bytes(base64.b64decode(private_path.read_text().strip(), validate=True))
        public = public_path.read_text().strip()
        if key.public_key().public_bytes_raw() != base64.b64decode(public, validate=True):
            raise ValueError(f"Public/private key mismatch for {platform}")
        subprocess.run(["gh", "secret", "set", f"FORWARD_UPDATE_PRIVATE_KEY_{platform}",
                        "--repo", REPOSITORY, "--env", "release"], input=private_path.read_bytes(), check=True)
        subprocess.run(["gh", "variable", "set", f"FORWARD_UPDATE_PUBLIC_KEY_{platform}",
                        "--repo", REPOSITORY, "--body", public], check=True)
    for platform in PLATFORMS:
        subprocess.run(["gh", "variable", "set", f"FORWARD_UPDATE_FEED_{platform.upper().replace('-', '_')}",
                        "--repo", REPOSITORY, "--body", f"{FEED_BASE}/{platform}.xml"], check=True)
    print("Configured GitHub public variables and the release environment's private signing secrets.")


def preflight(args):
    version = version_for_tag(args.tag)
    for platform in ("MACOS", "WINDOWS"):
        load_key(platform)
    for platform in PLATFORMS:
        name = f"FORWARD_UPDATE_FEED_{platform.upper().replace('-', '_')}"
        if os.environ.get(name) != f"{FEED_BASE}/{platform}.xml":
            raise ValueError(f"Set {name} to {FEED_BASE}/{platform}.xml")
    if not os.environ.get("APERTURE_PUBLISH_TOKEN"):
        raise ValueError("Missing release secret: APERTURE_PUBLISH_TOKEN")
    print(f"Release configuration ready for {version}")


def build_metadata(args):
    version = version_for_tag(args.tag)
    platform, _, minimum, filename = PLATFORMS[args.platform]
    configuration = (args.build / "generated/UpdaterConfig.h").read_text()
    public = os.environ[f"FORWARD_UPDATE_PUBLIC_KEY_{platform}"].strip()
    feed = f"{FEED_BASE}/{args.platform}.xml"
    if "ForwardUpdatesConfigured = true" not in configuration or public not in configuration or feed not in configuration:
        raise ValueError("Release binary must be built with the expected feed and public key")
    args.output.write_text(json.dumps({"version": version, "platform": args.platform,
        "public_key": public, "feed": feed, "minimum_os": minimum, "filename": filename,
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()}, indent=2) + "\n")


def sign(args):
    version = version_for_tag(args.tag)
    notes = (ROOT / "packaging/updates/notes" / f"{version}.md").read_text().strip()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    args.output.mkdir(parents=True, exist_ok=True)
    for platform, (key_platform, os_name, minimum, filename) in PLATFORMS.items():
        metadata = json.loads((args.assets / f"{platform}.json").read_text())
        expected = {"version": version, "platform": platform, "public_key": os.environ[f"FORWARD_UPDATE_PUBLIC_KEY_{key_platform}"].strip(),
                    "feed": f"{FEED_BASE}/{platform}.xml", "minimum_os": minimum, "filename": filename, "commit": commit}
        if metadata != expected:
            raise ValueError(f"Release build metadata mismatch for {platform}")
        key = load_key(key_platform)
        package = (args.assets / filename).read_bytes()
        signature = key.sign(package)
        key.public_key().verify(signature, package)
        rss = ET.Element("rss", {"version": "2.0"})
        channel = ET.SubElement(rss, "channel")
        ET.SubElement(channel, "title").text = f"Mirdain Forward - {platform}"
        item = ET.SubElement(channel, "item")
        ET.SubElement(item, "title").text = f"Mirdain Forward {version}"
        ET.SubElement(item, "pubDate").text = format_datetime(datetime.now(timezone.utc), usegmt=True)
        ET.SubElement(item, "link").text = f"https://github.com/{REPOSITORY}/releases/tag/{args.tag}"
        ET.SubElement(item, "description").text = "".join(f"<p>{html.escape(paragraph)}</p>" for paragraph in notes.split("\n\n"))
        ET.SubElement(item, f"{{{SPARKLE}}}minimumSystemVersion").text = minimum
        attributes = {"url": f"https://github.com/{REPOSITORY}/releases/download/{args.tag}/{filename}",
                      "length": str(len(package)), "type": "application/octet-stream",
                      f"{{{SPARKLE}}}os": os_name, f"{{{SPARKLE}}}version": version,
                      f"{{{SPARKLE}}}shortVersionString": version,
                      f"{{{SPARKLE}}}edSignature": base64.b64encode(signature).decode()}
        if key_platform == "WINDOWS":
            attributes[f"{{{SPARKLE}}}installerArguments"] = "/SILENT /SP- /NOICONS"
        ET.SubElement(item, "enclosure", attributes)
        ET.indent(rss, space="  ")
        ET.ElementTree(rss).write(args.output / f"{platform}.xml", encoding="utf-8", xml_declaration=True)
    # Checksums include the portable ZIP for people downloading it manually.
    packages = sorted(path for path in args.assets.iterdir() if path.suffix in (".dmg", ".exe", ".zip"))
    (args.output / "SHA256SUMS").write_text("".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in packages))
    print(f"Signed release packages and generated three feeds for {version}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("init-keys", "configure-github"):
        command = commands.add_parser(name)
        command.add_argument("--directory", type=Path, default=ROOT / ".release-keys")
        command.set_defaults(run=init_keys if name == "init-keys" else configure_github)
    command = commands.add_parser("preflight")
    command.add_argument("--tag", required=True)
    command.set_defaults(run=preflight)
    command = commands.add_parser("build-metadata")
    command.add_argument("--tag", required=True)
    command.add_argument("--platform", choices=PLATFORMS, required=True)
    command.add_argument("--build", type=Path, default=ROOT / "build")
    command.add_argument("--output", type=Path, required=True)
    command.set_defaults(run=build_metadata)
    command = commands.add_parser("sign")
    command.add_argument("--tag", required=True)
    command.add_argument("--assets", type=Path, required=True)
    command.add_argument("--output", type=Path, required=True)
    command.set_defaults(run=sign)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
