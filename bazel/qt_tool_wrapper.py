#!/usr/bin/env python3
import os
import sys
import subprocess
import shutil

def main():
    if len(sys.argv) < 3:
        print("Usage: qt_tool_wrapper.py <tool_name> <args...>")
        sys.exit(1)

    tool_name = sys.argv[1]
    args = sys.argv[2:]

    qt6_root = os.environ.get("QT6_ROOT", "")
    if not qt6_root:
        print("ERROR: QT6_ROOT is not set.")
        sys.exit(1)

    # Resolve tool binary
    tool_bin = ""
    candidates = [
        os.path.join(qt6_root, "bin", tool_name),
        os.path.join(qt6_root, "bin", tool_name + ".exe"),
        os.path.join(qt6_root, "libexec", tool_name),
        os.path.join(qt6_root, "libexec", tool_name + ".exe"),
    ]
    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            tool_bin = c
            break
    
    if not tool_bin:
        tool_bin = shutil.which(f"{tool_name}-qt6") or shutil.which(tool_name)
    
    if not tool_bin:
        print(f"ERROR: Qt tool '{tool_name}' not found under {qt6_root} or PATH.")
        sys.exit(1)

    cmd = [tool_bin] + args
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e:
        print(f"Qt tool '{tool_name}' failed with exit code {e.returncode}")
        sys.exit(e.returncode)

if __name__ == "__main__":
    main()
