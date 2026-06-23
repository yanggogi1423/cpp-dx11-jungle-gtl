import os
import subprocess
import sys


def build_clean_env():
    env = {}
    seen = set()
    path_value = os.environ.get("Path") or os.environ.get("PATH") or ""

    for key, value in os.environ.items():
        upper_key = key.upper()
        if upper_key == "PATH":
            continue
        if upper_key in seen:
            continue
        seen.add(upper_key)
        env[key] = value

    env["Path"] = path_value
    return env


def main():
    if len(sys.argv) < 2:
        print("Usage: RunProcessCleanEnv.py <command> [args...]")
        return 2

    completed = subprocess.run(sys.argv[1:], env=build_clean_env())
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
