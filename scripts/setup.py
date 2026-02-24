#!/usr/bin/env python3
"""
MoleHole project setup script.
- Checks system dependencies (Linux only, multiple package managers)
- Downloads and installs the Vulkan SDK into dependencies/vulkan-sdk/
- Generates a cmake_paths.cmake file so CMake can find paths without env vars
"""

import os
import sys
import platform
import subprocess
import shutil
import urllib.request
import json
import tarfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEPS_DIR = REPO_ROOT / "dependencies"
SCRIPTS_DIR = REPO_ROOT / "scripts"
VULKAN_SDK_DIR = DEPS_DIR / "vulkan-sdk"
CMAKE_PATHS_FILE = SCRIPTS_DIR / "cmake_paths.cmake"
LUNARG_VERSION_URL = "https://vulkan.lunarg.com/sdk/latest/linux.json"
LUNARG_SDK_URL_TEMPLATE = "https://sdk.lunarg.com/sdk/download/{version}/linux/vulkansdk-linux-x86_64-{version}.tar.xz?Human=true"

# ── Package definitions ────────────────────────────────────────────────────────

APT_PACKAGES = [
    "build-essential",
    "cmake",
    "ninja-build",
    "git",
    "python3",
    "pkg-config",
    # OpenGL / X11
    "libgl1-mesa-dev",
    "libx11-dev",
    "libxrandr-dev",
    "libxi-dev",
    "libxxf86vm-dev",
    "libxcursor-dev",
    "libxinerama-dev",
    # FFmpeg
    "libavcodec-dev",
    "libavformat-dev",
    "libavutil-dev",
    "libswscale-dev",
    # GTK3 for nativefiledialog-extended
    "libgtk-3-dev",
    # curl for downloads
    "curl",
    "xz-utils",
]

PACMAN_PACKAGES = [
    "base-devel",
    "cmake",
    "ninja",
    "git",
    "python",
    "pkgconf",
    # OpenGL / X11
    "mesa",
    "libx11",
    "libxrandr",
    "libxi",
    "libxxf86vm",
    "libxcursor",
    "libxinerama",
    # FFmpeg
    "ffmpeg",
    # GTK3
    "gtk3",
    # curl
    "curl",
    "xz",
    # Vulkan
    "vulkan-validation-layers",
]

DNF_PACKAGES = [
    "gcc-c++",
    "cmake",
    "ninja-build",
    "git",
    "python3",
    "pkgconfig",
    # OpenGL / X11
    "mesa-libGL-devel",
    "libX11-devel",
    "libXrandr-devel",
    "libXi-devel",
    "libXxf86vm-devel",
    "libXcursor-devel",
    "libXinerama-devel",
    # FFmpeg (requires RPM Fusion)
    "ffmpeg-devel",
    # GTK3
    "gtk3-devel",
    # curl
    "curl",
    "xz",
]

ZYPPER_PACKAGES = [
    "gcc-c++",
    "cmake",
    "ninja",
    "git",
    "python3",
    "pkg-config",
    # OpenGL / X11
    "Mesa-libGL-devel",
    "libX11-devel",
    "libXrandr-devel",
    "libXi-devel",
    "libXxf86vm-devel",
    "libXcursor-devel",
    "libXinerama-devel",
    # FFmpeg
    "ffmpeg-devel",
    # GTK3
    "gtk3-devel",
    # curl
    "curl",
    "xz",
]

# ── Helpers ────────────────────────────────────────────────────────────────────

def run(cmd: list[str], check=True, **kwargs) -> subprocess.CompletedProcess:
    print(f"  $ {' '.join(cmd)}")
    return subprocess.run(cmd, check=check, **kwargs)


def detect_package_manager() -> tuple[str, list[str]] | None:
    """Returns (pm_name, install_cmd_prefix) or None."""
    if shutil.which("apt-get"):
        return "apt", ["sudo", "apt-get", "install", "-y"]
    if shutil.which("pacman"):
        return "pacman", ["sudo", "pacman", "-S", "--needed", "--noconfirm"]
    if shutil.which("dnf"):
        return "dnf", ["sudo", "dnf", "install", "-y"]
    if shutil.which("zypper"):
        return "zypper", ["sudo", "zypper", "install", "-y"]
    return None


def get_package_list(pm_name: str) -> list[str]:
    return {
        "apt": APT_PACKAGES,
        "pacman": PACMAN_PACKAGES,
        "dnf": DNF_PACKAGES,
        "zypper": ZYPPER_PACKAGES,
    }[pm_name]


def check_submodules():
    print("\n── Checking git submodules ──────────────────────────────")
    result = run(
        ["git", "submodule", "status"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    uninitialized = [
        line for line in result.stdout.splitlines() if line.startswith("-")
    ]
    if uninitialized:
        print(f"  {len(uninitialized)} submodule(s) not initialized — running update …")
        run(["git", "submodule", "update", "--init", "--recursive"], cwd=REPO_ROOT)
    else:
        print("  All submodules OK.")


def fetch_latest_vulkan_version() -> str:
    print("  Fetching latest Vulkan SDK version from LunarG …")
    with urllib.request.urlopen(LUNARG_VERSION_URL, timeout=15) as resp:
        data = json.loads(resp.read())
    version = data["linux"]
    print(f"  Latest version: {version}")
    return version


def vulkan_sdk_installed(version: str) -> bool:
    marker = VULKAN_SDK_DIR / version / "x86_64" / "include" / "vulkan" / "vulkan.h"
    return marker.exists()


def download_vulkan_sdk(version: str):
    url = LUNARG_SDK_URL_TEMPLATE.format(version=version)
    archive_path = DEPS_DIR / f"vulkansdk-{version}.tar.xz"

    if not archive_path.exists():
        print(f"  Downloading Vulkan SDK {version} …")
        print(f"    URL: {url}")
        run([
            "curl", "-L", "--progress-bar",
            "-o", str(archive_path),
            url,
        ])
    else:
        print(f"  Archive already downloaded: {archive_path.name}")

    target_dir = VULKAN_SDK_DIR / version
    target_dir.mkdir(parents=True, exist_ok=True)

    print(f"  Extracting to {target_dir} …")
    with tarfile.open(archive_path, "r:xz") as tar:
        # The archive has a top-level directory named after the version
        members = tar.getmembers()
        strip = members[0].name.split("/")[0] if members else ""
        for member in members:
            if member.name.startswith(strip + "/"):
                member.name = member.name[len(strip) + 1:]
            if member.name:
                tar.extract(member, target_dir, filter="tar")

    print("  Extraction complete.")
    archive_path.unlink(missing_ok=True)


def write_cmake_paths(vulkan_version: str):
    """
    Write cmake_paths.cmake at repo root. CMakeLists.txt includes this file.
    It sets VULKAN_SDK to the local dependencies/ copy without any env vars.
    """
    sdk_root = VULKAN_SDK_DIR / vulkan_version / "x86_64"
    content = f"""# Auto-generated by scripts/setup.py — do not edit manually.
# This file provides local SDK paths to CMake without requiring environment variables.

set(VULKAN_SDK "{sdk_root}" CACHE PATH "Vulkan SDK root (set by setup.py)" FORCE)
set(VULKAN_INCLUDE_DIR "${{VULKAN_SDK}}/include" CACHE PATH "" FORCE)
set(VULKAN_LIBRARY "${{VULKAN_SDK}}/lib/libvulkan.so" CACHE PATH "" FORCE)
"""
    CMAKE_PATHS_FILE.write_text(content)
    print(f"  Written: scripts/cmake_paths.cmake")


# ── Stages ────────────────────────────────────────────────────────────────────

def install_system_packages():
    print("\n── Installing system packages ───────────────────────────")
    pm = detect_package_manager()
    if pm is None:
        print("  WARNING: No supported package manager found (apt/pacman/dnf/zypper).")
        print("  Please install required packages manually.")
        return
    pm_name, install_cmd = pm
    packages = get_package_list(pm_name)
    print(f"  Package manager: {pm_name}")
    sys.stdout.flush()
    if pm_name == "apt":
        run(["sudo", "apt-get", "update", "-qq"])
    run([*install_cmd, *packages])
    print("  System packages OK.")


def setup_vulkan_sdk():
    print("\n── Setting up Vulkan SDK ────────────────────────────────")
    version = fetch_latest_vulkan_version()
    if vulkan_sdk_installed(version):
        print(f"  Vulkan SDK {version} already present.")
    else:
        download_vulkan_sdk(version)
    write_cmake_paths(version)
    print(f"  Vulkan SDK ready at: dependencies/vulkan-sdk/{version}/x86_64")


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    print("═" * 60)
    print("  MoleHole Setup Script")
    print("═" * 60)
    sys.stdout.flush()

    if platform.system() != "Linux":
        print("ERROR: This script currently supports Linux only.")
        sys.exit(1)

    install_system_packages()
    check_submodules()
    setup_vulkan_sdk()

    print("\n═" * 60)
    print("  Setup complete!")
    print("  Next steps:")
    print("    cmake -B build -DCMAKE_BUILD_TYPE=Debug")
    print("    cmake --build build --config Debug")
    print("═" * 60)


if __name__ == "__main__":
    main()

