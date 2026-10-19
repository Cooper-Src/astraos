#!/usr/bin/env python3
"""QMP-driven smoke test for AstraOS desktop startup and keyboard window management."""

from __future__ import annotations

import sys
import time
from pathlib import Path

from qemu_keyboard_smoke import QMPClient, read_log, type_command, wait_for_log

BOOT_TIMEOUT = 22.0
ACTION_TIMEOUT = 6.0


def wait_for_occurrences(log_path: Path, needle: str, count: int, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = read_log(log_path)
        if text.count(needle) >= count:
            return text
        time.sleep(0.1)
    text = read_log(log_path)
    raise TimeoutError(
        f"serial output did not contain {needle!r} {count} times. Last output:\n"
        f"{text[-6000:] if text else '(serial log is empty)'}"
    )


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"usage: {argv[0]} QMP_SOCKET SERIAL_LOG", file=sys.stderr)
        return 2

    socket_path = argv[1]
    log_path = Path(argv[2])
    client: QMPClient | None = None
    try:
        deadline = time.monotonic() + 12.0
        last_error: OSError | None = None
        while time.monotonic() < deadline:
            try:
                client = QMPClient(socket_path)
                break
            except OSError as error:
                last_error = error
                time.sleep(0.1)
        if client is None:
            raise RuntimeError(f"could not connect to QEMU QMP socket: {last_error}")

        wait_for_log(log_path, "AstraOS desktop session started.", BOOT_TIMEOUT)
        client.send_key("2")
        wait_for_log(log_path, "[desktop] focused System Information window.", ACTION_TIMEOUT)
        client.send_key("1")
        wait_for_log(log_path, "[desktop] focused Welcome window.", ACTION_TIMEOUT)
        client.send_key("t")
        wait_for_log(log_path, "[desktop] switching to kernel terminal.", ACTION_TIMEOUT)
        wait_for_log(log_path, "root@astra:/#", ACTION_TIMEOUT)

        type_command(client, "desktop")
        wait_for_occurrences(log_path, "AstraOS desktop session started.", 2, ACTION_TIMEOUT)
        client.send_key("q")
        wait_for_log(log_path, "[desktop] closing desktop and returning to kernel shell.", ACTION_TIMEOUT)
        print("Desktop smoke test passed: startup, window focus, terminal handoff and exit all worked.")
        return 0
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"desktop smoke test failed: {error}", file=sys.stderr)
        print("--- final serial output ---", file=sys.stderr)
        print(read_log(log_path)[-6000:], file=sys.stderr)
        return 1
    finally:
        if client is not None:
            client.close()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
