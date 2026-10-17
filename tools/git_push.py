#!/usr/bin/env python3
"""Push в origin без мучений с credential helper.

Зачем скрипт, а не просто `git push`:

* `git push`, запущенный из PowerShell, в некоторых окружениях падает с
  «could not read Username for 'https://github.com'» или зависает намертво:
  git не может договориться с credential helper'ом по каналу.
* Запуск git из Python работает: каналы не ломаются.

Токен берётся у `gh`, если он есть и авторизован, иначе — у Git Credential
Manager (Windows Credential Manager). Токен подставляется в URL ровно на один
`git push`, нигде не сохраняется и вычищается из вывода.

Использование:
    python tools/git_push.py                    # текущая ветка в origin
    python tools/git_push.py some-branch        # указанная ветка
    python tools/git_push.py --allow-empty      # не падать, если пушить нечего
"""

import argparse
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REMOTE = "https://github.com/MacMillan25/MeshCore.git"
GCM_NAMES = ("git-credential-manager.exe", "git-credential-manager-core.exe",
             "git-credential-manager", "git-credential-manager-core")


def _which(name: str) -> str:
    for p in os.environ.get("PATH", "").split(os.pathsep):
        exe = os.path.join(p, name)
        if os.path.isfile(exe):
            return exe
    return ""


def find_gh() -> str:
    """gh.exe: сначала в WinGet, потом в PATH."""
    root = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft",
                        "WinGet", "Packages")
    if os.path.isdir(root):
        for name in sorted(os.listdir(root)):
            if name.startswith("GitHub.cli_"):
                exe = os.path.join(root, name, "bin", "gh.exe")
                if os.path.isfile(exe):
                    return exe
    return _which("gh.exe") or _which("gh")


def find_gcm() -> str:
    """git-credential-manager: сначала PATH, потом стандартные места Git for Windows."""
    for name in GCM_NAMES:
        exe = _which(name)
        if exe:
            return exe
    candidates = [
        r"C:\Program Files\Git\mingw64\bin\git-credential-manager.exe",
        r"C:\Program Files\Git\mingw64\libexec\git-core\git-credential-manager.exe",
        os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "Git",
                     "mingw64", "bin", "git-credential-manager.exe"),
    ]
    for exe in candidates:
        if exe and os.path.isfile(exe):
            return exe
    return ""


def _password_from_lines(text: str) -> str:
    for line in text.splitlines():
        if line.startswith("password="):
            return line[len("password="):]
    return ""


def get_token_gh(gh: str) -> str:
    out = subprocess.run(
        [gh, "auth", "git-credential", "get"],
        input="protocol=https\nhost=github.com\n\n",
        capture_output=True, text=True, timeout=60,
    )
    return _password_from_lines(out.stdout)


def get_token_gcm(gcm: str) -> str:
    out = subprocess.run(
        [gcm, "get"],
        input="protocol=https\nhost=github.com\n\n",
        capture_output=True, text=True, timeout=60,
    )
    return _password_from_lines(out.stdout)


def get_token() -> str:
    gh = find_gh()
    if gh:
        token = get_token_gh(gh)
        if token:
            return token
    gcm = find_gcm()
    if gcm:
        token = get_token_gcm(gcm)
        if token:
            return token
    sys.exit("нет доступа к GitHub: ни gh, ни Git Credential Manager не отдали токен\n"
             "проверь: git-credential-manager get (protocol=https, host=github.com)")


def current_branch() -> str:
    return subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"],
                          cwd=REPO, capture_output=True, text=True,
                          timeout=30).stdout.strip()


def main() -> int:
    ap = argparse.ArgumentParser(description="Push в origin через токен gh/GCM")
    ap.add_argument("branch", nargs="?", default=None,
                    help="ветка (по умолчанию текущая)")
    ap.add_argument("--allow-empty", action="store_true",
                    help="не считать ошибкой «Everything up-to-date»")
    args = ap.parse_args()

    token = get_token()

    branch = args.branch or current_branch()
    if not branch or branch == "HEAD":
        sys.exit("не удалось определить текущую ветку — укажи её явно")

    env = dict(os.environ)
    env["GIT_TERMINAL_PROMPT"] = "0"

    url = REMOTE.replace("https://", f"https://x-access-token:{token}@")
    p = subprocess.run(
        ["git", "-c", "credential.helper=", "push", url, branch],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=600,
    )

    def scrub(s: str) -> str:
        return s.replace(token, "***") if s else ""

    text = scrub(p.stdout) + scrub(p.stderr)
    print(text.strip())

    if p.returncode != 0:
        if args.allow_empty and "up-to-date" in text:
            print("(пушить нечего — уже актуально)")
            return 0
        return p.returncode

    print(f"OK: ветка {branch} отправлена в origin")
    return 0


if __name__ == "__main__":
    sys.exit(main())
