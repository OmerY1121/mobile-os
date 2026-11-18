import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path

with tempfile.TemporaryDirectory(prefix='selyra-ctl-') as tmp:
    data = Path(tmp) / 'data'
    run = Path(tmp) / 'run'

    data.mkdir(mode=0o700)
    run.mkdir(mode=0o700)

    endpoint = run / 'config.sock'

    env = dict(os.environ,
               SELYRA_CONFIG_SOCKET=str(endpoint),
               LC_ALL='C')

    def ctl(*args):
        return subprocess.run(
            ['build/configctl', *args],
            env=env,
            capture_output=True,
            text=True,
            timeout=7
        )

    assert ctl('list').returncode != 0

    p = subprocess.Popen(
        ['build/configd', str(data), str(run)]
    )

    try:
        for _ in range(100):
            if p.poll() is not None:
                raise RuntimeError('configd exited early')

            if endpoint.exists():
                break

            time.sleep(0.02)
        else:
            raise RuntimeError('configd socket missing')

        r = ctl('list')

        assert r.returncode == 0, r.stderr
        assert r.stdout == 'system.hostname=selyra\nlogging.max_kib=64\n'

        assert ctl('get', 'system.hostname').stdout == 'selyra\n'
        assert ctl('set', 'system.hostname', 'selyra-test').returncode == 0
        assert ctl('get', 'system.hostname').stdout == 'selyra-test\n'

        assert ctl('set', 'logging.max_kib', '256').returncode == 0
        assert ctl('reset', 'logging.max_kib').returncode == 0
        assert ctl('get', 'logging.max_kib').stdout == '64\n'

        print('PASS: configctl list, get, set and reset')

        for args in [
            ('get', 'unknown.key'),
            ('set', 'logging.max_kib', '-1'),
            ('set', 'system.hostname', 'x\nRESET logging.max_kib'),
            ('set', 'logging.max_kib'),
            ('list', 'extra')
        ]:
            r = ctl(*args)

            assert r.returncode != 0 and not r.stdout, (args, r)

        assert ctl('get', 'logging.max_kib').stdout == '64\n'

        print('PASS: Invalid commands return errors')

    finally:
        p.terminate()

        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            raise

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
        server.bind(str(endpoint))
        server.listen(1)
        server.settimeout(3)

        for payload in [
            b'OK\nsystem.hostname=selyra\n',
            b'OK\n\x00'
        ]:
            with subprocess.Popen(
                ['build/configctl', 'list'],
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            ) as client:

                with server.accept()[0] as conn:
                    conn.settimeout(3)
                    assert conn.recv(160) == b'LIST\n'
                    conn.sendall(payload)

                out, err = client.communicate(timeout=7)

                assert client.returncode != 0 and not out, (out, err)

        print('PASS: Invalid and incomplete server responses rejected')

        with subprocess.Popen(
            ['build/configctl', 'list'],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        ) as client:

            with server.accept()[0] as conn:
                conn.settimeout(3)
                assert conn.recv(160) == b'LIST\n'

                out, err = client.communicate(timeout=7)

                assert client.returncode != 0
                assert not out and b'timed out' in err

        print('PASS: Unresponsive server timed out')

print('All Selyra configctl tests passed.')
