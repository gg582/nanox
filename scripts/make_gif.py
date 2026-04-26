import os
import subprocess
import shutil
import urllib.request
import platform

def check_or_install_tools():
    # 1. Check asciinema
    asciinema_cmd = shutil.which("asciinema")
    if not asciinema_cmd:
        print("[*] asciinema not found. Installing via pip...")
        subprocess.run(["python3", "-m", "pip", "install", "--user", "asciinema"], check=True)
        # Try to find it again in ~/.local/bin
        local_bin = os.path.expanduser("~/.local/bin")
        if os.path.exists(os.path.join(local_bin, "asciinema")):
            os.environ["PATH"] += os.pathsep + local_bin
        asciinema_cmd = shutil.which("asciinema")
        if not asciinema_cmd:
            print("[!] Failed to find asciinema after installation. Please check your PATH.")
            exit(1)
            
    # 2. Check agg
    agg_cmd = shutil.which("agg")
    if not agg_cmd:
        # Check if we have agg locally downloaded
        if os.path.exists("./agg"):
            agg_cmd = "./agg"
        else:
            print("[*] agg (Asciinema GIF Generator) not found.")
            arch = platform.machine().lower()
            if arch in ["aarch64", "arm64"]:
                agg_url = "https://github.com/asciinema/agg/releases/download/v1.4.3/agg-aarch64-unknown-linux-gnu"
            elif arch in ["x86_64", "amd64"]:
                agg_url = "https://github.com/asciinema/agg/releases/download/v1.4.3/agg-x86_64-unknown-linux-gnu"
            else:
                print(f"[!] Unsupported architecture for automatic agg download: {arch}")
                print("Please install agg manually: https://github.com/asciinema/agg")
                exit(1)
                
            print(f"[*] Downloading agg for {arch}...")
            urllib.request.urlretrieve(agg_url, "agg")
            os.chmod("agg", 0o755)
            agg_cmd = "./agg"

    return asciinema_cmd, agg_cmd

def main():
    print("[*] Checking dependencies...")
    asciinema_cmd, agg_cmd = check_or_install_tools()

    cast_file = "output.cast"
    gif_file = "output.gif"
    test_file = "test.c"

    # Create empty test.c if it doesn't exist
    if not os.path.exists(test_file):
        with open(test_file, "w") as f:
            f.write("")

    print("[*] Starting nanox recording. The recording will stop when you exit nanox.")
    
    # Run asciinema to record the session
    try:
        subprocess.run([asciinema_cmd, "rec", "-c", "/usr/local/bin/nanox test.c", cast_file], check=True)
    except subprocess.CalledProcessError:
        print("[!] Recording failed or was interrupted.")
        if os.path.exists(test_file):
            os.remove(test_file)
        exit(1)

    print("[*] Recording finished. Converting to GIF...")
    
    # Convert cast to gif using agg
    try:
        subprocess.run([agg_cmd, cast_file, gif_file], check=True)
        print(f"[*] Successfully created GIF at {gif_file}")
    except subprocess.CalledProcessError:
        print("[!] Failed to convert recording to GIF.")
    finally:
        # Clean up files
        print("[*] Cleaning up temporary files...")
        if os.path.exists(cast_file):
            os.remove(cast_file)
        if os.path.exists(test_file):
            os.remove(test_file)
            
    print("[*] Done!")

if __name__ == "__main__":
    main()
