import socket
import subprocess
import tempfile
import time
from pathlib import Path

binary = Path('build/configd').resolve()

with tempfile.TemporaryDirectory(prefix='selyra-configd-') as tmp:
    data = Path(tmp) / 'data'
    runtime = Path(tmp) / 'run'

    data.mkdir(mode=0o700)
    runtime.mkdir(mode=0o700)

    endpoint = runtime / 'config.sock'
    command = [str(binary), str(data), str(runtime)]

    def start():
        p = subprocess.Popen(
            command, stdout=subprocess.DEVNULL
        )

        try:
            for _ in range(100):
                if p.poll() is not None:
                    raise RuntimeError('configd exited early')
                if endpoint.exists():
                    return p
                time.sleep(0.02)

            raise RuntimeError('configd socket missing')

        except BaseException:
            stop(p)
            raise

    def stop(p):
        if p.poll() is None:
            p.terminate()

            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
                raise

        assert p.returncode == 0
        assert not endpoint.exists()

    def request(message):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(4)
            s.connect(str(endpoint))
            s.sendall(message)

            response = b''

            while True:
                chunk = s.recv(4096)

                if not chunk:
                    return response

                response += chunk

    p = start()

    try:
        assert request(b'LIST\n') == (
            b'OK\nsystem.hostname=selyra\nlogging.max_kib=64\n'
        )

        assert request(b'SET system.hostname selyra-test\n') == b'OK\n'
        assert request(b'SET logging.max_kib 256\n') == b'OK\n'
        assert request(b'GET logging.max_kib\n') == b'OK 256\n'

        print('PASS: Configuration IPC read and write')

        assert request(b'SET logging.max_kib -1\n').startswith(b'ERR ')
        assert request(b'SET unknown.key 1\n').startswith(b'ERR ')
        assert request(b'GET logging.max_kib\x00').startswith(b'ERR ')
        assert request(b'GET logging.max_kib\n') == b'OK 256\n'

        print('PASS: Invalid requests rejected')

        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as slow:
            slow.settimeout(4)
            slow.connect(str(endpoint))
            slow.sendall(b'G')

            assert slow.recv(128) == b'ERR invalid request\n'

        assert request(b'GET system.hostname\n') == b'OK selyra-test\n'

        print('PASS: Incomplete request timed out')

        duplicate = subprocess.run(
            command, capture_output=True, timeout=5
        )

        assert duplicate.returncode != 0
        assert request(b'GET system.hostname\n') == b'OK selyra-test\n'

        print('PASS: Existing socket preserved')

    finally:
        stop(p)

    p = start()

    try:
        assert request(b'GET system.hostname\n') == b'OK selyra-test\n'
        assert request(b'GET logging.max_kib\n') == b'OK 256\n'
        assert request(b'RESET logging.max_kib\n') == b'OK\n'
        assert request(b'GET logging.max_kib\n') == b'OK 64\n'

        print('PASS: Configuration restored after service restart')

    finally:
        stop(p)

    assert (data / 'settings.conf').stat().st_mode & 0o777 == 0o600

    print('PASS: Private configuration file')

print('All Selyra configd tests passed.')
