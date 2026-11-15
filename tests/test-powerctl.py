#!/usr/bin/env python3
import os
import socket
import subprocess
import tempfile
import threading
from pathlib import Path

CLIENT = Path("build/test-powerctl").resolve()

def run(action, reply, expected_request, expected_code):
    with tempfile.TemporaryDirectory(
        prefix="selyra-power-test-"
    ) as tmp:
        path = str(Path(tmp) / "power.sock")
        observed = []

        with socket.socket(
            socket.AF_UNIX, socket.SOCK_STREAM
        ) as server:
            server.bind(path)
            server.listen(1)
            server.settimeout(5)

            def serve():
                try:
                    conn, _ = server.accept()
                    with conn:
                        conn.settimeout(5)
                        received = b""

                        while not received.endswith(b"\n"):
                            part = conn.recv(64)
                            if not part:
                                break
                            received += part

                        observed.append(received)
                        conn.sendall(reply.encode())

                except Exception as exc:
                    observed.append(exc)

            thread = threading.Thread(target=serve)
            thread.start()

            env = {
                **os.environ,
                "SELYRA_POWER_SOCKET": path
            }

            try:
                result = subprocess.run(
                    [str(CLIENT), action],
                    env=env,
                    capture_output=True,
                    text=True,
                    timeout=5
                )
            finally:
                thread.join(timeout=6)

        assert observed == [expected_request.encode()], observed
        assert result.returncode == expected_code, result
        assert result.stdout == reply, result.stdout


for action, request in [
    ("poweroff", "POWEROFF\n"),
    ("reboot", "REBOOT\n")
]:
    run(
        action,
        "power: accepted; shutdown in progress\n",
        request,
        0
    )

run(
    "reboot",
    "power: permission denied\n",
    "REBOOT\n",
    2
)

run("poweroff", "power: shutdown already in progress\n",
    "POWEROFF\n", 2)

for args in [[], ["invalid"], ["reboot", "extra"]]:
    result = subprocess.run(
        [str(CLIENT), *args],
        capture_output=True,
        timeout=5
    )
    assert result.returncode != 0

print("PASS: powerctl request mapping")
print("PASS: accepted, denied and busy responses")
print("PASS: invalid arguments rejected")
print("PASS: no system shutdown performed")
