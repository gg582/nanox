#!/usr/bin/env python3
import os
import sys
import subprocess
import urllib.request
import stat
import venv
import shutil

# Use asciinema + agg for high quality terminal gifs
AGG_URL = "https://github.com/asciinema/agg/releases/download/v1.4.3/agg-x86_64-unknown-linux-gnu"
BIN_DIR = os.path.join(os.getcwd(), ".gifmaker_env")
AGG_BIN = os.path.join(BIN_DIR, "bin", "agg")
ASCIINEMA_BIN = os.path.join(BIN_DIR, "bin", "asciinema")

def run_cmd(cmd, env=None):
    print(f"Running: {' '.join(cmd)}")
    subprocess.run(cmd, check=True, env=env)

def init():
    print("Initializing GIF maker environment...")
    
    if os.path.exists(BIN_DIR):
        print("Cleaning up old environment...")
        shutil.rmtree(BIN_DIR)

    print("Creating virtual environment...")
    builder = venv.EnvBuilder(with_pip=True)
    builder.create(BIN_DIR)
    
    pip_bin = os.path.join(BIN_DIR, "bin", "pip")
    
    print("Installing asciinema...")
    run_cmd([pip_bin, "install", "asciinema"])
    
    print(f"Downloading agg from {AGG_URL}...")
    urllib.request.urlretrieve(AGG_URL, AGG_BIN)
    os.chmod(AGG_BIN, os.stat(AGG_BIN).st_mode | stat.S_IEXEC)
    
    print("Init complete. You can now run: python3 bootstrap_get_gifmaker.py capture")

def capture():
    if not os.path.exists(ASCIINEMA_BIN) or not os.path.exists(AGG_BIN):
        print("Please run init first: python3 bootstrap_get_gifmaker.py init")
        sys.exit(1)
        
    print("Starting capture... The editor will open.")
    print("Perform your actions, and when you quit the editor (F4), the GIF will be generated.")
    
    cast_file = "demo.cast"
    gif_file = "nanox.gif"
    
    if os.path.exists(cast_file):
        os.remove(cast_file)
        
    nx_bin = "/usr/local/bin/nx"
    if not os.path.exists(nx_bin):
        print("nx binary not found, building...")
        run_cmd(["make"])
        
    # Set environment variables for asciinema
    env = os.environ.copy()
    env["PATH"] = os.path.join(BIN_DIR, "bin") + os.pathsep + env.get("PATH", "")
    
    # Run asciinema to capture 'nx test.c'
    try:
        run_cmd([ASCIINEMA_BIN, "rec", "-c", f"{nx_bin} test.c", cast_file], env=env)
    except subprocess.CalledProcessError:
        print("Capture was interrupted or failed.")
        
    if os.path.exists(cast_file):
        print("Capture complete. Converting to GIF using agg...")
        # Customize agg settings for good presentation
        # Adjusting speed and font size for typical good looking terminal gifs
        try:
            run_cmd([AGG_BIN, "--speed", "1.5", "--font-size", "18", cast_file, gif_file])
            print(f"GIF successfully saved to {gif_file}!")
            print("To view it, open it with your favorite image viewer or a web browser.")
        except subprocess.CalledProcessError:
            print("Failed to convert cast to GIF.")
    else:
        print("Failed to capture cast. Ensure you exited the editor normally.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 bootstrap_get_gifmaker.py [init|capture]")
        sys.exit(1)
        
    cmd = sys.argv[1]
    if cmd == "init":
        init()
    elif cmd == "capture":
        capture()
    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)
