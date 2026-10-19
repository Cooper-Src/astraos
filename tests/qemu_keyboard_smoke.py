#!/usr/bin/env python3
"""QMP-driven regression test for AstraOS firmware keyboard input."""

from __future__ import annotations

import json
import socket
import sys
import time
from pathlib import Path
from typing import Any


QMP_CONNECT_TIMEOUT = 12.0
QMP_COMMAND_TIMEOUT = 4.0
BOOT_TIMEOUT = 22.0
SHELL_TIMEOUT = 5.0


class QMPClient:
    """Minimal synchronous QMP client with bounded reads and command timeouts."""

    def __init__(self, socket_path: str) -> None:
        self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._buffer = bytearray()
        self._next_id = 1
        try:
            self._socket.settimeout(QMP_CONNECT_TIMEOUT)
            self._socket.connect(socket_path)
            greeting = self._read_object(time.monotonic() + QMP_COMMAND_TIMEOUT)
            if "QMP" not in greeting:
                raise RuntimeError("QEMU did not send a QMP greeting")
            self.execute("qmp_capabilities")
        except Exception:
            self._socket.close()
            raise

    def close(self) -> None:
        self._socket.close()

    def _read_object(self, deadline: float) -> dict[str, Any]:
        while True:
            newline = self._buffer.find(b"\\n")
            if newline >= 0:
                line = bytes(self._buffer[:newline])
                del self._buffer[: newline + 1]
                if not line:
                    continue
                value = json.loads(line)
                if not isinstance(value, dict):
                    raise RuntimeError("QMP sent a non-object message")
                return value

            remaining = deadline - time.monotonic()
            if remaining <= 0.0:
                raise TimeoutError("timed out waiting for a QMP response")
            self._socket.settimeout(remaining)
            chunk = self._socket.recv(4096)
            if not chunk:
                raise RuntimeError("QMP connection closed unexpectedly")
            self._buffer.extend(chunk)

    def execute(self, command: str, arguments: dict[str, Any] | None = None) -> Any:
        command_id = self._next_id
        self._next_id += 1
        request: dict[str, Any] = {"execute": command, "id": command_id}
        if arguments is not None:
            request["arguments"] = arguments
        payload = json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\\r\\n"
        self._socket.sendall(payload)
        deadline = time.monotonic() + QMP_COMMAND_TIMEOUT

        while True:
            response = self._read_object(deadline)
            if response.get("id") != command_id:
                continue
            if "error" in response:
                raise RuntimeError(f"QMP command {command!r} failed: {response['error']}")
            if "return" not in response:
                raise RuntimeError(f"QMP command {command!r} returned no result")
            return response["return"]

    def send_key(self, key: str) -> None:
        self.execute(
            "human-monitor-command",
            {"command-line": f"sendkey {key}"},
        )


def read_log(log_path: Path) -> str:
    try:
        return log_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def wait_for_log(log_path: Path, needle: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = read_log(log_path)
        if needle in text:
            return text
        time.sleep(0.1)
    text = read_log(log_path)
    if needle not in text:
        excerpt = text[-6000:] if text else "(serial log is empty)"
        raise TimeoutError(f"serial output did not contain {needle!r}. Last output:\\n{excerpt}")
    return text


def type_command(client: QMPClient, command: str) -> None:
    for character in command:
        if character == " ":
            client.send_key("spc")
        elif character.isascii() and character.isalpha():
            client.send_key(character.lower())
        elif character.isdigit():
            client.send_key(character)
        else:
            raise ValueError(f"unsupported test-key character: {character!r}")
        time.sleep(0.15)
    client.send_key("ret")


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"usage: {argv[0]} QMP_SOCKET SERIAL_LOG", file=sys.stderr)
        return 2

    socket_path = argv[1]
    log_path = Path(argv[2])
    client: QMPClient | None = None
    try:
        connect_deadline = time.monotonic() + QMP_CONNECT_TIMEOUT
        last_error: OSError | None = None
        while time.monotonic() < connect_deadline:
            try:
                client = QMPClient(socket_path)
                break
            except OSError as error:
                last_error = error
                time.sleep(0.1)
        if client is None:
            raise RuntimeError(f"could not connect to QEMU QMP socket: {last_error}")

        wait_for_log(log_path, "root@astra:/#", BOOT_TIMEOUT)
        type_command(client, "help")
        wait_for_log(log_path, "AstraOS kernel commands:", SHELL_TIMEOUT)
        type_command(client, "uname")
        wait_for_log(log_path, "AstraOS astra-aarch64 0.2 kernel", SHELL_TIMEOUT)
        print("UEFI keyboard smoke test passed: QEMU keyboard input reached the AstraOS shell.")
        return 0
    except (OSError, RuntimeError, TimeoutError, ValueError, json.JSONDecodeError) as error:
        print(f"keyboard smoke test failed: {error}", file=sys.stderr)
        print("--- final serial output ---", file=sys.stderr)
        print(read_log(log_path)[-6000:], file=sys.stderr)
        return 1
    finally:
        if client is not None:
            client.close()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
