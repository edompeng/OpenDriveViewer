#!/usr/bin/env python3
import os
import sys
import subprocess
import glob

def main():
    if len(sys.argv) != 3:
        print("Usage: moc_wrapper.py <input-header> <output-cpp>")
        sys.exit(1)

    in_header = sys.argv[1]
    out_cpp = sys.argv[2]

    qt6_root = os.environ.get("QT6_ROOT", "")
    if not qt6_root:
        print("ERROR: QT6_ROOT is not set.")
        sys.exit(1)

    # Resolve moc binary
    moc_bin = ""
    candidates = [
        os.path.join(qt6_root, "libexec", "moc"),
        os.path.join(qt6_root, "libexec", "moc.exe"),
        os.path.join(qt6_root, "bin", "moc"),
        os.path.join(qt6_root, "bin", "moc.exe"),
    ]
    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            moc_bin = c
            break
    
    if not moc_bin:
        # Fallback to PATH
        import shutil
        moc_bin = shutil.which("moc-qt6") or shutil.which("moc")
    
    if not moc_bin:
        print(f"ERROR: Qt moc executable not found under {qt6_root} or PATH.")
        sys.exit(1)

    # Build include paths
    moc_args = ["-I."]
    
    # Platform specific includes
    if sys.platform == "darwin":
        if os.path.isdir(os.path.join(qt6_root, "lib")):
            moc_args.append("-F" + os.path.join(qt6_root, "lib"))
        if os.path.isdir(os.path.join(qt6_root, "include")):
            moc_args.append("-I" + os.path.join(qt6_root, "include"))
        
        for fw in glob.glob(os.path.join(qt6_root, "lib", "Qt*.framework/Headers")):
            moc_args.append("-I" + fw)
            
    elif sys.platform == "win32":
        inc_dir = os.path.join(qt6_root, "include")
        if os.path.isdir(inc_dir):
            moc_args.append("-I" + inc_dir)
            for d in os.listdir(inc_dir):
                full_d = os.path.join(inc_dir, d)
                if os.path.isdir(full_d) and d.startswith("Qt"):
                    moc_args.append("-I" + full_d)
    else:
        # Linux / Other
        inc_dir = os.path.join(qt6_root, "include")
        if os.path.isdir(inc_dir):
            moc_args.append("-I" + inc_dir)
            for d in os.listdir(inc_dir):
                full_d = os.path.join(inc_dir, d)
                if os.path.isdir(full_d) and d.startswith("Qt"):
                    moc_args.append("-I" + full_d)
        
        # System fallbacks
        for system_inc in ["/usr/include/qt6", "/usr/include/x86_64-linux-gnu/qt6"]:
            if os.path.isdir(system_inc):
                moc_args.append("-I" + system_inc)
                for d in os.listdir(system_inc):
                    full_d = os.path.join(system_inc, d)
                    if os.path.isdir(full_d) and d.startswith("Qt"):
                        moc_args.append("-I" + full_d)

    cmd = [moc_bin] + moc_args + [in_header, "-o", out_cpp]
    # print(f"Running: {' '.join(cmd)}") # Debug
    
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e:
        print(f"MOC failed with exit code {e.returncode}")
        sys.exit(e.returncode)

if __name__ == "__main__":
    main()
