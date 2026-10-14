#!/usr/bin/env python3
"""Push в origin без мучений с credential helper.

Зачем скрипт, а не просто `git push`:

* Штатный Git Credential Manager (`credential.helper = manager`) в этом
  окружении учётных данных для github.com не имеет — он умеет только спросить
  их интерактивно, а это в неинтерактивной сессии падает с
  «Cannot prompt because user interactivity has been disabled».
* `gh` в роли helper'а и любой другой helper, которого git запускает как
  отдельную программу, здесь **зависает**: git ждёт ответа по каналу и не
  дожидается. То же самое происходит, если запускать git из PowerShell
  с перенаправлением вывода.
* А вот запуск git из Python работает: каналы не ломаются.

Поэтому: берём токен у `gh` одной командой, подставляем его в URL ровно на
один `git push`, токен нигде не сохраняем.

Использование:
    python tools/git_push.py                    # текущая ветка в origin
    python tools/git_push.py some-branch        # указанная ветка
    python tools/git_push.py --allow-empty      # не падать, если пушить нечего

Токен в вывод не попадает: он вычищается из stdout/stderr перед печатью.
"""

import argparse
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REMOTE = "https://github.com/MacMillan25/MeshCore.git"


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
    for p in os.environ.get("PATH", "").split(os.pathsep):
        exe = os.path.join(p, "gh.exe")
        if os.path.isfile(exe):
            return exe
    sys.exit("gh.exe не найден — без него не получить доступ к GitHub")


def get_token(gh: str) -> str:
    out = subprocess.run(
        [gh, "auth", "git-credential", "get"],
        input="protocol=https\nhost=github.com\n\n",
        capture_output=True, text=True, timeout=60,
    )
    for line in out.stdout.splitlines():
        if line.startswith("password="):
            return line[len("password="):]
    sys.exit("gh не отдал токен. Войди заново: gh auth login --web\n"
             + out.stdout[:400] + out.stderr[:400])


def main() -> int:
    ap = argparse.ArgumentParser(description="Push в origin через токен gh")
    ap.add_argument("branch", nargs="?", default=None,
                    help="ветка (по умолчанию текущая)")
    ap.add_argument("--allow-empty", action="store_true",
                    help="не считать ошибкой «Everything up-to-date»")
    args = ap.parse_args()

    gh = find_gh()
    token = get_token(gh)

    if args.branch:
        branch = args.branch
    else:
        branch = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"],
                                cwd=REPO, capture_output=True, text=True,
                                timeout=30).stdout.strip()
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
