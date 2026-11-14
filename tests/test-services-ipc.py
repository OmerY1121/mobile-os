#!/usr/bin/env python3
import os
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path.cwd()
STATUSD = (ROOT / "build/statusd").resolve()
STATUSCTL = (ROOT / "build/statusctl").resolve()

def run_test():
    with tempfile.TemporaryDirectory(
        prefix="mo-services-"
    ) as directory:
        base = Path(directory)
        state = base / "services"
        sock = base / "status.sock"

        env = os.environ.copy()
        env["MOBILE_OS_SOCKET"] = str(sock)
        env["MOBILE_OS_STATE_FILE"] = str(state)

        server = subprocess.Popen(
            [str(STATUSD)],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE
        )

        try:
            deadline = time.monotonic() + 5

            while not sock.exists():
                if server.poll() is not None:
                    raise RuntimeError(
                        "statusd exited during startup"
                    )
                if time.monotonic() > deadline:
                    raise RuntimeError(
                        "statusd startup timed out"
                    )
                time.sleep(0.05)

            def check(content, expected):
                state.write_text(content)
                state.chmod(0o644)

                result = subprocess.run(
                    [str(STATUSCTL), "services"],
                    env=env,
                    text=True,
                    capture_output=True,
                    timeout=4
                )

                if (result.returncode != 0 or
                        result.stdout.strip() != expected):
                    raise RuntimeError(
                        f"Unexpected response: "
                        f"{result.stdout!r}, "
                        f"rc={result.returncode}, "
                        f"stderr={result.stderr!r}"
                    )

            check(
                "logd=launched failures=0 last_exit=none\n"
                "statusd=launched failures=0 last_exit=none\n",
                "services: logd=launched failures=0 "
                "last_exit=none; statusd=launched failures=0 "
                "last_exit=none"
            )
            print("PASS: running services reported")

            check(
                "logd=failed failures=3 last_exit=signal-9\n"
                "statusd=launched failures=0 last_exit=none\n",
                "services: logd=failed failures=3 "
                "last_exit=signal-9; "
                "statusd=launched failures=0 last_exit=none"
            )
            print("PASS: service failure and exit signal reported")

            state.chmod(0o666)

            result = subprocess.run(
                [str(STATUSCTL), "services"],
                env=env,
                text=True,
                capture_output=True,
                timeout=4
            )

            if result.stdout.strip() != \
                    "statusd: state unavailable":
                raise RuntimeError(
                    "Unsafe state file was accepted"
                )

            print("PASS: unsafe state file rejected")
        finally:
            if server.poll() is None:
                server.terminate()

            try:
                server.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
                server.communicate()

    print("All service IPC integration tests passed.")

if __name__ == "__main__":
    run_test()
