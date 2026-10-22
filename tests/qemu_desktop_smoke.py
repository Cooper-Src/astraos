#!/usr/bin/env python3
"""QMP-driven smoke test for the AstraOS desktop and in-kernel display server."""

from __future__ import annotations

import json
import socket
import sys
import time
from pathlib import Path
from typing import Any

CONNECT_TIMEOUT = 12.0
COMMAND_TIMEOUT = 4.0
BOOT_TIMEOUT = 22.0
EVENT_TIMEOUT = 5.0


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

    def move_mouse_to(
        self, x: int, y: int, screen_width: int, screen_height: int
    ) -> None:
        if (x < 0 or x >= screen_width or y < 0 or y >= screen_height or
                screen_width <= 1 or screen_height <= 1):
            raise ValueError("mouse target falls outside the framebuffer")
        absolute_x = (x * 32767) // (screen_width - 1)
        absolute_y = (y * 32767) // (screen_height - 1)
        self.execute("input-send-event", {
            "events": [
                {"type": "abs", "data": {"axis": "x", "value": absolute_x}},
                {"type": "abs", "data": {"axis": "y", "value": absolute_y}},
            ]
        })
        time.sleep(0.15)

    def click_mouse(self, button: str = "left") -> None:
        self.execute("input-send-event", {
            "events": [{"type": "btn", "data": {"button": button, "down": True}}]
        })
        time.sleep(0.12)
        self.execute("input-send-event", {
            "events": [{"type": "btn", "data": {"button": button, "down": False}}]
        })
        time.sleep(0.12)

    def capture_screen(self, output_path: Path) -> None:
        self.execute("human-monitor-command", {
            "command-line": f"screendump {output_path.resolve()}"
        })


def validate_desktop_screenshot(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    position = 0

    def token() -> bytes:
        nonlocal position
        while position < len(data):
            if data[position] in b" \t\r\n":
                position += 1
            elif data[position] == ord("#"):
                newline = data.find(b"\n", position)
                if newline < 0:
                    raise RuntimeError("screendump has a truncated PPM comment")
                position = newline + 1
            else:
                break
        start = position
        while position < len(data) and data[position] not in b" \t\r\n#":
            position += 1
        if position == start:
            raise RuntimeError("screendump has an invalid PPM header")
        return data[start:position]

    if token() != b"P6":
        raise RuntimeError("QEMU desktop screendump is not a binary PPM image")
    width = int(token())
    height = int(token())
    maximum = int(token())
    if width < 480 or height < 360 or maximum != 255:
        raise RuntimeError(f"unexpected desktop screenshot dimensions/format: {width}x{height}, max={maximum}")

    # Consume the header delimiter only; a pixel byte is allowed to be whitespace.
    if position >= len(data) or data[position] not in b" \t\r\n":
        raise RuntimeError("screendump is missing its PPM data delimiter")
    delimiter = data[position]
    position += 1
    if delimiter == ord("\r") and position < len(data) and data[position] == ord("\n"):
        position += 1

    pixels = data[position:]
    expected_bytes = width * height * 3
    if len(pixels) < expected_bytes:
        raise RuntimeError(
            f"screendump pixel data is truncated: expected {expected_bytes}, got {len(pixels)}"
        )

    colors: set[tuple[int, int, int]] = set()
    x_step = max(1, width // 53)
    y_step = max(1, height // 37)
    for y in range(0, height, y_step):
        for x in range(0, width, x_step):
            offset = (y * width + x) * 3
            colors.add((pixels[offset], pixels[offset + 1], pixels[offset + 2]))
    if len(colors) < 12:
        raise RuntimeError(
            f"desktop framebuffer looks blank or monochrome: sampled {len(colors)} colours"
        )
    return width, height, len(colors)


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
    punctuation = {".": "dot", "/": "slash", "-": "minus"}
    for character in command:
        if character == " ":
            key = "spc"
        elif character.isascii() and character.isalpha():
            key = character.lower()
        elif character.isdigit():
            key = character
        elif character in punctuation:
            key = punctuation[character]
        else:
            raise ValueError(f"unsupported test-key character: {character!r}")
        client.send_key(key)
        time.sleep(0.12)
    client.send_key("ret")


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"usage: {argv[0]} QMP_SOCKET SERIAL_LOG", file=sys.stderr)
        return 2

    socket_path = argv[1]
    log_path = Path(argv[2])
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
        wait_for_log(log_path, "ASTRAOS_POINTER_READY", EVENT_TIMEOUT)
        screenshot_path = log_path.parent / "desktop-smoke.ppm"
        client.capture_screen(screenshot_path)
        screenshot_width, screenshot_height, sampled_colors = validate_desktop_screenshot(
            screenshot_path
        )
        print(
            f"Desktop framebuffer rendered {screenshot_width}x{screenshot_height} "
            f"with {sampled_colors} sampled colours."
        )

        # QMP absolute input is delivered to the emulated USB tablet and translated
        # by UEFI into absolute coordinates; the first movement produces the initial event.
        client.move_mouse_to(80, 20, screenshot_width, screenshot_height)
        wait_for_log(log_path, "desktop: pointer event received", EVENT_TIMEOUT)
        client.click_mouse()
        wait_for_log(log_path, "desktop: applications menu opened by pointer", EVENT_TIMEOUT)
        client.move_mouse_to(32, 95, screenshot_width, screenshot_height)
        client.click_mouse()
        wait_for_log(log_path, "desktop: terminal window opened by pointer", EVENT_TIMEOUT)

        type_command(client, "pwd")
        wait_for_log(log_path, "desktop: terminal command pwd", EVENT_TIMEOUT)
        type_command(client, "touch /tmp/mouse-test.txt")
        wait_for_log(log_path, "desktop: terminal filesystem create", EVENT_TIMEOUT)
        type_command(client, "write /tmp/mouse-test.txt pointer-ok")
        wait_for_log(log_path, "desktop: terminal command write", EVENT_TIMEOUT)

        # Open Files from the same menu, navigate to /tmp, and open the new file.
        client.move_mouse_to(80, 20, screenshot_width, screenshot_height)
        client.click_mouse()
        wait_for_log(log_path, "desktop: applications menu opened by pointer", EVENT_TIMEOUT)
        client.move_mouse_to(32, 119, screenshot_width, screenshot_height)
        client.click_mouse()
        wait_for_log(log_path, "desktop: file browser opened by pointer", EVENT_TIMEOUT)
        client.move_mouse_to(92, 181, screenshot_width, screenshot_height)
        client.click_mouse()
        wait_for_log(log_path, "desktop: file entry selected by pointer", EVENT_TIMEOUT)
        client.click_mouse()
        wait_for_log(log_path, "desktop: files directory opened", EVENT_TIMEOUT)
        client.move_mouse_to(112, 149, screenshot_width, screenshot_height)
        client.click_mouse()
        wait_for_log(log_path, "desktop: file entry selected by pointer", EVENT_TIMEOUT)
        client.click_mouse()
        wait_for_log(log_path, "desktop: text viewer opened", EVENT_TIMEOUT)

        # The task button for Terminal is the second entry on the panel.
        client.move_mouse_to(280, 20, screenshot_width, screenshot_height)
        client.click_mouse()
        wait_for_log(log_path, "desktop: taskbar window focused by pointer", EVENT_TIMEOUT)
        type_command(client, "help")
        wait_for_log(log_path, "desktop: terminal command help", EVENT_TIMEOUT)
        type_command(client, "ls")
        wait_for_log(log_path, "desktop: terminal command ls", EVENT_TIMEOUT)
        type_command(client, "exit")
        wait_for_log(log_path, "desktop: terminal window closed", EVENT_TIMEOUT)
        client.send_key("a")
        wait_for_log(log_path, "desktop: about window focused", EVENT_TIMEOUT)
        client.send_key("m")
        wait_for_log(log_path, "desktop: applications menu opened", EVENT_TIMEOUT)
        client.send_key("f")
        wait_for_log(log_path, "desktop: file browser opened", EVENT_TIMEOUT)
        client.send_key("r")
        wait_for_log(log_path, "desktop: files refreshed", EVENT_TIMEOUT)
        client.send_key("c")
        wait_for_log(log_path, "desktop: window closed", EVENT_TIMEOUT)
        client.send_key("n")
        wait_for_log(log_path, "desktop: window minimized", EVENT_TIMEOUT)
        client.send_key("q")
        wait_for_log(log_path, "desktop: returned to shell", EVENT_TIMEOUT)
        print("AstraOS desktop smoke test passed: framebuffer desktop, UEFI mouse, start menu, taskbar, terminal, VFS-backed file manager and window controls worked.")
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
