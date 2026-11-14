#!/usr/bin/env python3
import os
import socket
import subprocess
import tempfile
import threading
import time
from pathlib import Path

STATUSD = Path("build/statusd").resolve()
STATUSCTL = Path("build/statusctl").resolve()

if not STATUSD.is_file() or not STATUSCTL.is_file():
    raise SystemExit("Run make first.")

with tempfile.TemporaryDirectory(
    prefix="mo-health-"
) as directory:
    root = Path(directory)
    status_path = str(root / "status.sock")
    log_path = str(root / "log.sock")

    env = os.environ.copy()
    env["MOBILE_OS_SOCKET"] = status_path
    env["MOBILE_OS_LOG_SOCKET"] = log_path

    with open(root / "statusd.log", "w") as output:
        server = subprocess.Popen(
            [str(STATUSD)],
            env=env,
            stdout=output,
            stderr=subprocess.STDOUT
        )

        try:
            deadline = time.monotonic() + 5

            while not Path(status_path).exists():
                if server.poll() is not None:
                    raise RuntimeError(
                        "statusd terminated during startup"
                    )
                if time.monotonic() >= deadline:
                    raise RuntimeError(
                        "statusd socket not created"
                    )
                time.sleep(0.05)

            def check(expected, code):
                result = subprocess.run(
                    [str(STATUSCTL), "health"],
                    env=env,
                    text=True,
                    capture_output=True,
                    timeout=5
                )

                actual = result.stdout.strip()

                if actual != expected or result.returncode != code:
                    raise RuntimeError(
                        f"Expected {expected!r}, rc={code}; "
                        f"got {actual!r}, rc={result.returncode}; "
                        f"stderr={result.stderr!r}"
                    )

            check(
                "health: statusd=ok logd=unavailable",
                2
            )
            print("PASS: missing logd detected")

            fake = socket.socket(
                socket.AF_UNIX,
                socket.SOCK_STREAM
            )
            fake.bind(log_path)
            fake.listen(5)
            fake.settimeout(0.2)

            mode = ["healthy"]
            stop = threading.Event()

            def fake_logd():
                while not stop.is_set():
                    try:
                        conn, _ = fake.accept()
                    except socket.timeout:
                        continue
                    except OSError:
                        break

                    with conn:
                        conn.settimeout(2)
                        try:
                            data = conn.recv(256)

                            if not data.startswith(b"WRITE "):
                                continue

                            if mode[0] == "healthy":
                                conn.sendall(b"OK\n")
                            else:
                                time.sleep(3)
                        except OSError:
                            pass

            worker = threading.Thread(
                target=fake_logd,
                daemon=True
            )
            worker.start()

            try:
                check(
                    "health: statusd=ok logd=ok",
                    0
                )
                print("PASS: healthy logd accepted")

                mode[0] = "stalled"
                start = time.monotonic()

                check(
                    "health: statusd=ok logd=unavailable",
                    2
                )

                elapsed = time.monotonic() - start

                if elapsed > 4:
                    raise RuntimeError(
                        "Health check exceeded time limit"
                    )

                print("PASS: unresponsive logd detected")
                print(f"Health timeout: {elapsed:.2f}s")
            finally:
                stop.set()
                fake.close()
                worker.join(timeout=4)

        finally:
            server.terminate()

            try:
                server.wait(timeout=2)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()

print("All health failure tests passed.")
