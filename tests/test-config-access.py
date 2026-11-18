import os
import subprocess
import tempfile
import time
from pathlib import Path

with tempfile.TemporaryDirectory(
    prefix="selyra-config-access-"
) as tmp:

    data = Path(tmp) / "data"
    run = Path(tmp) / "run"

    data.mkdir(mode=0o700)
    run.mkdir(mode=0o700)

    endpoint = run / "config.sock"

    env = dict(os.environ,
               SELYRA_CONFIG_SOCKET=str(endpoint))

    fd = os.open(data, os.O_RDONLY | os.O_DIRECTORY)

    try:
        for mode in ("rw", "ro"):
            p = subprocess.Popen([
                "build/configd",
                f"--dir-fd={fd}",
                str(run),
                mode
            ], pass_fds=(fd,))

            def ctl(*args):
                return subprocess.run(
                    ["build/configctl", *args],
                    env=env, capture_output=True,
                    text=True, timeout=7
                )

            try:
                for _ in range(100):
                    if p.poll() is not None:
                        raise RuntimeError("configd exited early")

                    if endpoint.exists():
                        break

                    time.sleep(0.02)
                else:
                    raise RuntimeError("Socket unavailable")

                if mode == "rw":
                    assert ctl(
                        "set", "system.hostname", "selyra-persist"
                    ).returncode == 0

                    print("PASS: Inherited configuration directory")

                else:
                    result = ctl("get", "system.hostname")

                    assert result.returncode == 0
                    assert result.stdout == "selyra-persist\n"

                    before = (data / "settings.conf").read_bytes()

                    assert ctl(
                        "set", "system.hostname", "modified"
                    ).returncode != 0

                    assert ctl(
                        "reset", "system.hostname"
                    ).returncode != 0

                    assert (data / "settings.conf").read_bytes() == before

                    print("PASS: Persistent settings restored")
                    print("PASS: Read-only configuration preserved")

            finally:
                p.terminate()

                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
                    raise

    finally:
        os.close(fd)

print("All Selyra configuration access tests passed.")
