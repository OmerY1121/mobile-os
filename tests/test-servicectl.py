import os
import subprocess
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory(
    prefix="selyra-services-"
) as tmp:

    state = Path(tmp) / "services"

    env = dict(
        os.environ,
        SELYRA_SERVICE_STATE=str(state)
    )

    def ctl(*args):
        return subprocess.run(
            ["build/servicectl", *args],
            env=env,
            capture_output=True,
            text=True,
            timeout=3
        )

    logd = "logd=launched failures=0 last_exit=none\n"
    statusd = "statusd=stopped failures=0 last_exit=exit-0\n"

    assert ctl("list").returncode != 0

    state.write_text(logd + statusd)
    state.chmod(0o644)

    result = ctl("list")

    assert result.returncode == 0
    assert result.stdout == logd + statusd

    print("PASS: Service listing")

    assert ctl("status", "logd").stdout == logd
    assert ctl("status", "statusd").stdout == statusd

    print("PASS: Individual service status")

    assert ctl("status", "unknown").returncode != 0
    assert ctl("status", "../logd").returncode != 0
    assert ctl("list", "extra").returncode != 0

    print("PASS: Invalid service requests rejected")

    state.chmod(0o666)

    assert ctl("list").returncode != 0

    state.unlink()
    state.symlink_to("missing-target")

    assert ctl("list").returncode != 0

    state.unlink()
    os.mkfifo(state)

    assert ctl("list").returncode != 0

    print("PASS: Unsafe service state files rejected")

print("All Selyra servicectl tests passed.")
