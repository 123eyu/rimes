#!/usr/bin/env python3
"""Run the actual iOS archive importer tests on macOS, without a simulator.

This checks inspection/staging only; it does not replace deployment or device tests.
The generated harness and its dependency cache stay under ignored build/.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "build" / "input-schemes" / "importer-tests"
PACKAGE = '''// swift-tools-version:5.9
import PackageDescription
let package = Package(name: "SchemeImporterTests", platforms: [.macOS(.v13)],
    dependencies: [
        .package(url: "https://github.com/jpsim/Yams.git", exact: "5.1.3"),
        .package(url: "https://github.com/weichsel/ZIPFoundation.git", exact: "0.9.19"),
    ], targets: [
        .target(name: "RIMES", dependencies: [
            .product(name: "Yams", package: "Yams"),
            .product(name: "ZIPFoundation", package: "ZIPFoundation"),
        ]),
        .testTarget(name: "ImporterTests", dependencies: ["RIMES", "ZIPFoundation"]),
    ])
'''


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def main() -> int:
    write(HARNESS / "Package.swift", PACKAGE)
    write(HARNESS / "Sources/RIMES/ImportedRimeSchema.swift",
          "struct ImportedRimeSchema: Sendable { var id: String; var name: String }\n")
    write(HARNESS / "Sources/RIMES/RimeSchemeImportService.swift",
          (ROOT / "platforms/ios/App/RimeSchemeImportService.swift").read_text())
    write(HARNESS / "Tests/ImporterTests/RimeSchemeImportTests.swift",
          (ROOT / "platforms/ios/Tests/RimeSchemeImportTests.swift").read_text())
    return subprocess.call(["swift", "test", "--package-path", str(HARNESS)])


if __name__ == "__main__":
    raise SystemExit(main())
