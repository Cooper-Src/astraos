#!/usr/bin/env python3
"""QMP-driven smoke test for the AstraOS desktop and in-kernel display server."""

from __future__ import annotations

import json
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

CONNECT_TIMEOUT = 12.0
COMMAND_TIMEOUT = 4.0
BOOT_TIMEOUT = 22.0
EVENT_TIMEOUT = 5.0
POINTER_SCALE = 3


class QMPClient:
    """Small bounded-timeout client for QEMU's QMP Unix socket."""

    def __init__(self, socket_path: str) -> None:
        self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._buffer = bytearray()
        self._next_id = 1
        try:
            self._socket.settimeout(CONNECT_TIMEOUT)
            self._socket.connect(socket_path)
            greeting = self._read_object(time.monotonic() + COMMAND_TIMEOUT)
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
            newline = self._buffer.find(b"\n")
            if newline >= 0:
                line = bytes(self._buffer[:newline])
                del self._buffer[:newline + 1]
                if not line:
                    continue
                message = json.loads(line)
                if not isinstance(message, dict):
                    raise RuntimeError("QMP sent a non-object message")
                return message
            remaining = deadline - time.monotonic()
            if remaining <= 0.0:
                raise TimeoutError("timed out waiting for QMP response")
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
        self._socket.sendall(
            json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\r\n"
        )
        deadline = time.monotonic() + COMMAND_TIMEOUT
        while True:
            response = self._read_object(deadline)
            if response.get("id") != command_id:
                continue
            if "error" in response:
                raise RuntimeError(f"QMP command failed: {response['error']}")
            if "return" not in response:
                raise RuntimeError("QMP command returned no result")
            return response["return"]

    def send_key(self, key: str) -> None:
        self.execute("human-monitor-command", {"command-line": f"sendkey {key}"})


def read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def wait_for_log(path: Path, needle: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        content = read_log(path)
        if needle in content:
            return content
        time.sleep(0.1)
    content = read_log(path)
    raise TimeoutError(
        f"serial output did not contain {needle!r}. Final output:\n"
        f"{content[-6000:] if content else '(serial log is empty)'}"
    )


def type_command(client: QMPClient, command: str) -> None:
    for character in command:
        if character == " ":
            key = "spc"
        elif character.isascii() and character.isalpha():
            key = character.lower()
        elif character.isdigit():
            key = character
        else:
            raise ValueError(f"unsupported test-key character: {character!r}")
        client.send_key(key)
        time.sleep(0.12)
    client.send_key("ret")



def find_qemu_window() -> str:
    deadline = time.monotonic() + CONNECT_TIMEOUT
    last_error = ""
    while time.monotonic() < deadline:
        result = subprocess.run(
            ["xdotool", "search", "--onlyvisible", "--name", "QEMU"],
            check=False,
            capture_output=True,
            text=True,
        )
        window_ids = result.stdout.split()
        for window_id in window_ids:
            geometry = subprocess.run(
                ["xdotool", "getwindowgeometry", "--shell", window_id],
                check=False,
                capture_output=True,
                text=True,
            )
            if geometry.returncode == 0:
                values: dict[str, int] = {}
                for line in geometry.stdout.splitlines():
                    if "=" in line:
                        name, value = line.split("=", 1)
                        if name in {"WIDTH", "HEIGHT"}:
                            try:
                                values[name] = int(value)
                            except ValueError:
                                pass
                if values.get("WIDTH", 0) >= 480 and values.get("HEIGHT", 0) >= 360:
                    return window_id
        last_error = result.stderr.strip()
        time.sleep(0.1)
    raise TimeoutError(f"could not find the visible QEMU GTK window: {last_error}")


def move_mouse_to(
    window_id: str,
    current_x: int,
    current_y: int,
    target_x: int,
    target_y: int,
    screen_width: int,
    screen_height: int,
) -> tuple[int, int]:
    if target_x < 0 or target_x >= screen_width or target_y < 0 or target_y >= screen_height:
        raise ValueError("mouse target falls outside the guest framebuffer")
    subprocess.run(
        ["xdotool", "mousemove", "--window", window_id, str(target_x), str(target_y)],
        check=True,
        capture_output=True,
        text=True,
    )
    time.sleep(0.2)
    return target_x, target_y


def click_mouse(window_id: str, x: int, y: int) -> None:
    subprocess.run(
        ["xdotool", "mousemove", "--window", window_id, str(x), str(y), "click", "1"],
        check=True,
        capture_output=True,
        text=True,
    )
    time.sleep(0.2)


def capture_and_verify_desktop(client: QMPClient, screenshot_path: Path) -> tuple[int, int]:
    response = client.execute(
        "human-monitor-command",
        {"command-line": f"screendump {screenshot_path}"},
    )
    if isinstance(response, str) and "error" in response.lower():
        raise RuntimeError(f"QEMU screendump failed: {response}")
    try:
        raw = screenshot_path.read_bytes()
    except OSError as error:
        raise RuntimeError(f"desktop screenshot was not written: {error}") from error

    position = 0

    def next_token() -> str:
        nonlocal position
        while position < len(raw):
            if raw[position] in b" \t\r\n":
                position += 1
                continue
            if raw[position] == ord("#"):
                while position < len(raw) and raw[position] not in b"\r\n":
                    position += 1
                continue
            break
        start = position
        while position < len(raw) and raw[position] not in b" \t\r\n#":
            position += 1
        if start == position:
            raise RuntimeError("invalid PPM screenshot header")
        token = raw[start:position].decode("ascii")
        if position < len(raw) and raw[position] in b" \t\r\n":
            if raw[position:position + 2] == b"\r\n":
                position += 2
            else:
                position += 1
        return token

    try:
        magic = next_token()
        width = int(next_token())
        height = int(next_token())
        maximum = int(next_token())
    except (ValueError, UnicodeDecodeError) as error:
        raise RuntimeError("could not parse QEMU screenshot header") from error
    if magic != "P6" or width < 480 or height < 360 or maximum != 255:
        raise RuntimeError(
            f"unexpected desktop screenshot format: {magic} {width}x{height} max={maximum}"
        )

    pixels = raw[position:]
    expected_bytes = width * height * 3
    if len(pixels) < expected_bytes:
        raise RuntimeError(
            f"desktop screenshot is truncated: have {len(pixels)} bytes, "
            f"expected {expected_bytes}"
        )

    pixel_count = width * height
    sample_step = max(1, pixel_count // 20000)
    colors: set[tuple[int, int, int]] = set()
    bright_samples = 0
    for pixel_index in range(0, pixel_count, sample_step):
        offset = pixel_index * 3
        red = pixels[offset]
        green = pixels[offset + 1]
        blue = pixels[offset + 2]
        colors.add((red, green, blue))
        if max(red, green, blue) > 180:
            bright_samples += 1

    if len(colors) < 12 or bright_samples < 8:
        raise RuntimeError(
            "desktop framebuffer looks blank or like a solid background: "
            f"{len(colors)} sampled colors, {bright_samples} bright samples"
        )
    print(
        f"Desktop screenshot verified: {width}x{height}, "
        f"{len(colors)} sampled colors, visible UI details present."
    )
    return width, height


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(f"usage: {argv[0]} QMP_SOCKET SERIAL_LOG SCREENSHOT_PPM", file=sys.stderr)
        return 2

    socket_path = argv[1]
    log_path = Path(argv[2])
    screenshot_path = Path(argv[3])
    client: QMPClient | None = None
    try:
        connect_deadline = time.monotonic() + CONNECT_TIMEOUT
        last_error: OSError | None = None
        while time.monotonic() < connect_deadline:
            try:
                client = QMPClient(socket_path)
                break
            except OSError as error:
                last_error = error
                time.sleep(0.1)
        if client is None:
            raise RuntimeError(f"could not connect to QMP socket: {last_error}")

        wait_for_log(log_path, "ASTRAOS_DISPLAY_SERVER_READY", BOOT_TIMEOUT)
        wait_for_log(log_path, "ASTRAOS_POINTER_INPUT_READY", EVENT_TIMEOUT)
        screen_width, screen_height = capture_and_verify_desktop(client, screenshot_path)
        qemu_window = find_qemu_window()
        mouse_x = screen_width // 2
        mouse_y = screen_height // 2

        mouse_x, mouse_y = move_mouse_to(
            qemu_window, mouse_x, mouse_y, 80, 20, screen_width, screen_height
        )
        click_mouse(qemu_window, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: applications menu opened by pointer", EVENT_TIMEOUT)
        mouse_x, mouse_y = move_mouse_to(
            qemu_window, mouse_x, mouse_y, 80, 95, screen_width, screen_height
        )
        click_mouse(qemu_window, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: terminal window opened by pointer", EVENT_TIMEOUT)

        type_command(client, "help")
        wait_for_log(log_path, "desktop: terminal command help", EVENT_TIMEOUT)
        type_command(client, "pwd")
        wait_for_log(log_path, "desktop: terminal command pwd", EVENT_TIMEOUT)
        type_command(client, "ls")
        wait_for_log(log_path, "desktop: terminal command ls", EVENT_TIMEOUT)

        mouse_x, mouse_y = move_mouse_to(
            qemu_window, mouse_x, mouse_y, 80, 20, screen_width, screen_height
        )
        click_mouse(qemu_window, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: applications menu opened by pointer", EVENT_TIMEOUT)
        mouse_x, mouse_y = move_mouse_to(
            qemu_window, mouse_x, mouse_y, 80, 119, screen_width, screen_height
        )
        click_mouse(qemu_window, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: file browser opened by pointer", EVENT_TIMEOUT)

        file_window_width = min(720, screen_width - 40)
        file_window_height = min(500, screen_height - 42 - 54 - 24)
        file_x = (screen_width - file_window_width) // 2 + 24
        file_y = 42 + 8 + (
            screen_height - 42 - 54 - file_window_height - 16
        ) // 2 + 20
        file_x = min(file_x, screen_width - file_window_width - 8)
        file_y = min(file_y, screen_height - 54 - file_window_height - 8)
        mouse_x, mouse_y = move_mouse_to(
            qemu_window, mouse_x, mouse_y, file_x + 220, file_y + 76,
            screen_width, screen_height
        )
        click_mouse(qemu_window, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: file selected by pointer", EVENT_TIMEOUT)
        click_mouse(qemu_window, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: files directory opened", EVENT_TIMEOUT)

        click_mouse(qemu_window, mouse_x, mouse_y)
        click_mouse(qemu_window, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: text viewer opened", EVENT_TIMEOUT)

        mouse_x, mouse_y = move_mouse_to(
            qemu_window, mouse_x, mouse_y, 280, 20, screen_width, screen_height
        )
        click_mouse(qemu_window, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: task button focused by pointer", EVENT_TIMEOUT)
        type_command(client, "whoami")
        type_command(client, "exit")
        wait_for_log(log_path, "desktop: terminal window closed", EVENT_TIMEOUT)
        client.send_key("q")
        wait_for_log(log_path, "desktop: returned to shell", EVENT_TIMEOUT)
        print("AstraOS desktop test passed: visible framebuffer, mouse, applications menu, taskbar, terminal and file manager.")
        return 0
    except (OSError, RuntimeError, TimeoutError, ValueError, json.JSONDecodeError) as error:
        print(f"desktop smoke test failed: {error}", file=sys.stderr)
        print("--- final serial output ---", file=sys.stderr)
        print(read_log(log_path)[-6000:], file=sys.stderr)
        return 1
    finally:
        if client is not None:
            client.close()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
