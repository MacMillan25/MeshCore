"""Тег и релиз прошивки DarkTec на GitHub с восемью UF2.

Релиз кладёт ассеты с именами `<env>.uf2` — так их ищет DarkFlasher.
Тело релиза берётся из файла (--notes) или генерируется из git log.

Примеры:
    python tools/make_release.py --version 1.18.0d3 --notes RELEASE.md
    python tools/make_release.py --version 1.18.0d3 --notes RELEASE.md --dry-run
"""

import argparse
import io
import json
import os
import subprocess
import sys
import urllib.request

OWNER = "MacMillan25"
REPO = "MeshCore"
API = f"https://api.github.com/repos/{OWNER}/{REPO}"
UPLOADS = f"https://uploads.github.com/repos/{OWNER}/{REPO}"
GCM = r"C:\Program Files\Git\mingw64\bin\git-credential-manager.exe"

ENVS = [
    "Darktec_companion_radio_ble",
    "Darktec_companion_radio_usb",
    "Darktec_kiss_modem",
    "Darktec_repeater",
    "Darktec_repeater_bridge_rs232_serial1",
    "Darktec_room_server",
    "Darktec_sensor",
    "Darktec_terminal_chat",
]


def token():
    p = subprocess.run([GCM, "get"], input="protocol=https\nhost=github.com\n\n",
                       capture_output=True, text=True, timeout=60)
    for line in p.stdout.splitlines():
        if line.startswith("password="):
            return line[9:]
    raise SystemExit("GCM не отдал токен")


def api(url, payload=None, method=None, data=None, content_type="application/json"):
    body = data
    if payload is not None:
        body = json.dumps(payload).encode()
    req = urllib.request.Request(
        url, data=body, method=method or ("POST" if body is not None else "GET"),
        headers={"Authorization": "Bearer " + token(),
                 "Accept": "application/vnd.github+json",
                 "Content-Type": content_type,
                 "User-Agent": "dsh-agent"})
    with urllib.request.urlopen(req, timeout=180) as r:
        return json.load(r)


def run(cmd):
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)


def find_uf2(env, version):
    out = os.path.join("out", "uf2")
    for name in os.listdir(out):
        if name.startswith(env) and version in name and name.endswith(".uf2"):
            return os.path.join(out, name)
    raise SystemExit(f"Нет UF2 для {env} версии {version} — сначала собери скриптом build-darktec-uf2.ps1")


def find_zip(env, version):
    """DFU-пакет для обновления по воздуху (bootloader OTAFIX/Nordic DFU).

    PlatformIO кладёт его в .pio/build/<env>/firmware.zip при обычной сборке
    (не при -t create_uf2). Если в out/uf2 уже лежит готовый `<env>-...zip`,
    берём его.
    """
    out = os.path.join("out", "uf2")
    if os.path.isdir(out):
        for name in os.listdir(out):
            if name.startswith(env) and version in name and name.endswith(".zip"):
                return os.path.join(out, name)
    p = os.path.join(".pio", "build", env, "firmware.zip")
    if os.path.exists(p):
        return p
    raise SystemExit(f"Нет DFU-zip для {env} — собери `pio run -e {env}` (без -t create_uf2)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True, help="1.18.0d3")
    ap.add_argument("--notes", help="файл с текстом релиза")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    tag = f"darktec-v{args.version}"
    name = f"DarkTec {args.version}"
    body = ""
    if args.notes:
        body = io.open(args.notes, encoding="utf-8").read().strip()

    files = {env: find_uf2(env, args.version) for env in ENVS}
    for env, path in files.items():
        print(f"{env:40s} {path}  {os.path.getsize(path)} байт")

    if args.dry_run:
        return

    # Тег: локальный и на сервере.
    tags = subprocess.run(["git", "tag", "-l", tag], capture_output=True, text=True).stdout.strip()
    if not tags:
        run(["git", "tag", "-a", tag, "-m", f"DarkTec {args.version}"])
    else:
        print("тег уже есть:", tag)
    run(["git", "push", "origin", tag])

    # Релиз.
    try:
        rel = api(f"{API}/releases/tags/{tag}")
        print("релиз уже существует:", rel["html_url"])
        return
    except urllib.error.HTTPError as e:
        if e.code != 404:
            raise

    rel = api(f"{API}/releases", {"tag_name": tag, "name": name,
                                  "body": body, "draft": False,
                                  "prerelease": False})
    print("релиз создан:", rel["html_url"])

    for env in ENVS:
        path = files[env]
        with open(path, "rb") as f:
            data = f.read()
        asset = api(f"{UPLOADS}/releases/{rel['id']}/assets?name={env}.uf2",
                    data=data, content_type="application/octet-stream")
        print(f"  загружен {asset['name']} ({asset['size']} байт)")

    # DFU-пакеты для обновления по воздуху: их берёт приложение и bootloader
    # OTAFIX (Nordic DFU). Имя как у апстрима — <env>-v<версия>-<хеш>.zip.
    short = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                           capture_output=True, text=True).stdout.strip()
    for env in ENVS:
        zpath = find_zip(env, args.version)
        with open(zpath, "rb") as f:
            zdata = f.read()
        zname = f"{env}-v{args.version}-{short}.zip"
        asset = api(f"{UPLOADS}/releases/{rel['id']}/assets?name={zname}",
                    data=zdata, content_type="application/octet-stream")
        print(f"  загружен {asset['name']} ({asset['size']} байт)")

    print("готово:", rel["html_url"])


if __name__ == "__main__":
    sys.exit(main())
