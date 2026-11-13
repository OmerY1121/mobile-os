import os
import socket
import time

SOCKET_PATH = os.environ["MOBILE_OS_SOCKET"]


def connect():
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(5)
    client.connect(SOCKET_PATH)
    return client


def check(actual, expected, name):
    if actual != expected:
        raise AssertionError(
            f"{name}: expected {expected!r}, got {actual!r}"
        )
    print(f"PASS: {name}")


# Test 1: Message arrives in separate pieces.
with connect() as client:
    client.sendall(b"STA")
    time.sleep(0.05)
    client.sendall(b"TUS")
    time.sleep(0.05)
    client.sendall(b"\n")
    reply = client.recv(256)

check(reply, b"statusd: running\n", "Partial message")


# Test 2: Unknown command.
with connect() as client:
    client.sendall(b"UNKNOWN\n")
    reply = client.recv(256)

check(reply, b"statusd: unknown command\n",
      "Unknown command")


# Test 3: Request exceeds the buffer.
with connect() as client:
    client.sendall(b"X" * 40 + b"\n")
    reply = client.recv(256)

check(reply, b"statusd: invalid request\n",
      "Oversized request")


# Test 4: Client leaves a request unfinished.
with connect() as client:
    client.sendall(b"STA")
    reply = client.recv(256)

check(reply, b"statusd: invalid request\n",
      "Request timeout")


# Test 5: Service should still accept connections.
with connect() as client:
    client.sendall(b"STATUS\n")
    reply = client.recv(256)

check(reply, b"statusd: running\n",
      "Recovery after timeout")

print("All IPC error tests passed.")
