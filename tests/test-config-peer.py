import os
import subprocess
import sys
import tempfile
from pathlib import Path

assert os.geteuid() == 0, "Run this test as root"

with tempfile.TemporaryDirectory(prefix="selyra-peer-") as tmp:
    base = Path(tmp)
    base.chmod(0o755)

    run = base / "run"
    run.mkdir(mode=0o700)
    os.chown(run, 65534, 65534)

    path = run / "config.sock"

    code = '''
import socket
import sys

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
    s.bind(sys.argv[1])
    s.listen(1)
    s.settimeout(5)

    print("ready", flush=True)

    with s.accept()[0] as c:
        c.settimeout(5)
        assert c.recv(160) == b""
'''

    def drop():
        os.setgroups([])
        os.setgid(65534)
        os.setuid(65534)

    p = subprocess.Popen(
        [sys.executable, "-c", code, str(path)],
        stdout=subprocess.PIPE,
        text=True,
        preexec_fn=drop
    )

    try:
        assert p.stdout.readline().strip() == "ready"

        result = subprocess.run(
            ["build/configctl", "get", "system.hostname"],
            env=dict(os.environ,
                     SELYRA_CONFIG_SOCKET=str(path),
                     LC_ALL="C"),
            capture_output=True,
            text=True,
            timeout=7
        )

        assert result.returncode == 1
        assert not result.stdout
        assert "Operation not permitted" in result.stderr
        assert p.wait(timeout=7) == 0

    finally:
        if p.poll() is None:
            p.kill()
            p.wait()

print("PASS: Wrong server UID rejected before request transmission")
