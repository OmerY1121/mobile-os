import os
import subprocess
import tempfile
import time
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="selyra-recovery-") as tmp:
    data = Path(tmp) / "data"
    run = Path(tmp) / "run"

    data.mkdir(mode=0o700)
    run.mkdir(mode=0o700)

    settings = data / "settings.conf"
    endpoint = run / "config.sock"

    damaged = b"system.hostname=broken name\nlogging.max_kib=-1\n"

    settings.write_bytes(damaged)
    settings.chmod(0o600)

    env = dict(os.environ, SELYRA_CONFIG_SOCKET=str(endpoint))
    command = ["build/configd", str(data), str(run)]

    def ctl(*args):
        return subprocess.run(
            ["build/configctl", *args],
            env=env, capture_output=True,
            text=True, timeout=7
        )

    def start():
        p = subprocess.Popen(command)

        try:
            for _ in range(100):
                if p.poll() is not None:
                    raise RuntimeError("configd exited early")

                if endpoint.exists():
                    return p

                time.sleep(0.02)

            raise RuntimeError("Missing config socket")
        except BaseException:
            if p.poll() is None:
                p.kill()
                p.wait()
            raise

    def stop(p):
        p.terminate()

        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            raise

        assert p.returncode == 0

    p = start()

    try:
        assert ctl("recovery").stdout == "required\n"
        assert ctl("get", "system.hostname").stdout == "selyra\n"
        assert ctl("get", "logging.max_kib").stdout == "64\n"

        print("PASS: Defaults available in recovery mode")

        assert ctl("set", "logging.max_kib", "128").returncode != 0
        assert ctl("reset", "system.hostname").returncode != 0
        assert settings.read_bytes() == damaged

        print("PASS: Damaged configuration preserved")

        assert ctl("recover").returncode == 0
        assert ctl("recovery").stdout == "not-required\n"

        assert settings.read_text() == (
            "system.hostname=selyra\nlogging.max_kib=64\n"
        )

        assert ctl("recover").returncode != 0

        print("PASS: Explicit recovery successful")

        assert ctl("set", "logging.max_kib", "256").returncode == 0

    finally:
        stop(p)

    p = start()

    try:
        assert ctl("recovery").stdout == "not-required\n"
        assert ctl("get", "logging.max_kib").stdout == "256\n"

        print("PASS: Recovered settings survive restart")

    finally:
        stop(p)

    settings.chmod(0o644)

    result = subprocess.run(
        command, capture_output=True, timeout=5
    )

    assert result.returncode != 0
    assert not endpoint.exists()

    print("PASS: Unsafe configuration rejected")

print("All Selyra configuration recovery tests passed.")
