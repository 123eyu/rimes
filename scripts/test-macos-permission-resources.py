#!/usr/bin/env python3
"""Exercise permission guides after relocating a built executable into an app.

Uses the existing fake-permission smoke only: no TCC requests, installation,
input-source registration, or changes to the source app/build directory.
"""

import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    resources = args.resources.resolve(strict=True)
    bundle_name = "RimeBuffer_RimeBuffer.bundle"
    if not (resources / bundle_name / "Permissions").is_dir():
        parser.error("the built product must contain its permission artwork")

    with tempfile.TemporaryDirectory(prefix="xsime-permission-resources-") as temporary:
        root = Path(temporary)
        app = root / "含空格目录" / "Permission Resource Probe.app"
        contents = app / "Contents"
        executable = contents / "MacOS" / "RIMES"
        executable.parent.mkdir(parents=True)
        shutil.copy2(binary, executable)
        packaged_resources = contents / "Resources"
        packaged_resources.mkdir()
        for bundle in resources.glob("*.bundle"):
            shutil.copytree(bundle, packaged_resources / bundle.name, symlinks=True)

        # Use a test-only application identity, without any IMK registration keys.
        with (contents / "Info.plist").open("wb") as output:
            plistlib.dump({
                "CFBundleIdentifier": "example.xsime.permission-resources." + uuid.uuid4().hex,
                "CFBundleName": "Permission Resource Probe",
                "CFBundleExecutable": "RIMES",
                "CFBundlePackageType": "APPL",
                "CFBundleVersion": "1",
                "LSUIElement": True,
                "NSPrincipalClass": "NSApplication",
            }, output)
        environment = os.environ.copy()
        for key, name in [
            ("RIMEBUFFER_USER_DIR", "profile"),
            ("RIMEBUFFER_LOCAL_DATA_ROOT", "local"),
            ("RIMEBUFFER_PLUGIN_ROOT", "plugins"),
        ]:
            directory = root / name
            directory.mkdir()
            environment[key] = str(directory)

        def run(label, *options):
            print("==> " + label, flush=True)
            subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", str(app)], check=True)
            subprocess.run([str(executable), "capture-permission-smoke", *options],
                           cwd=root, env=environment, check=True, timeout=180)

        run("permission guides in a relocated app")
        product_bundle = packaged_resources / bundle_name
        shutil.rmtree(product_bundle / "Permissions")
        run("permission guides without optional artwork", "--expect-missing-permission-resources")
        shutil.rmtree(product_bundle)
        run("permission guides without the product resource bundle", "--expect-missing-permission-resources")
        print("packaged permission resources: OK", flush=True)


if __name__ == "__main__":
    main()
