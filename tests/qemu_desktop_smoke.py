#!/usr/bin/env python3
"""End-to-end QMP test for desktop rendering, keyboard input, and UEFI mouse support."""

from __future__ import annotations

import json
import socket
import sys
import time
from collections import Counter
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
        self._pointer_x: int | None = None
        self._pointer_y: int | None = None
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
                raise RuntimeError(f"QMP command {command} failed: {response['error']}")
            if "return" not in response:
                raise RuntimeError("QMP command returned no result")
            return response["return"]

    def send_key(self, key: str) -> None:
        self.execute("human-monitor-command", {"command-line": f"sendkey {key}"})

    def select_mouse_device(self) -> str:
        """Select a non-absolute mouse when QEMU reports one."""
        mice = self.execute("query-mice")
        if not isinstance(mice, list) or not mice:
            raise RuntimeError(f"QEMU reports no guest mouse devices: {mice!r}")
        candidates = [
            mouse for mouse in mice
            if isinstance(mouse, dict) and not mouse.get("absolute", False)
        ]
        selected = candidates[0] if candidates else mice[0]
        if not isinstance(selected, dict) or not isinstance(selected.get("index"), int):
            raise RuntimeError(f"QEMU returned malformed mouse information: {mice!r}")
        self.execute(
            "human-monitor-command",
            {"command-line": f"mouse_set {selected['index']}"},
        )
        return str(selected.get("name", f"mouse #{selected['index']}"))

    def pointer_position(self, x: int, y: int, width: int, height: int) -> None:
        """Move the active relative mouse in small deltas through QEMU's HMP queue."""
        if width < 2 or height < 2:
            raise ValueError("framebuffer dimensions are too small for pointer injection")
        target_x = max(0, min(x, width - 1))
        target_y = max(0, min(y, height - 1))
        if self._pointer_x is None or self._pointer_y is None:
            # The AstraOS software cursor begins at the centre of the framebuffer.
            self._pointer_x = width // 2
            self._pointer_y = height // 2

        # HMP mouse_move injects REL deltas. AstraOS currently scales each by two pixels.
        remaining_x = (target_x - self._pointer_x) // 2
        remaining_y = (target_y - self._pointer_y) // 2
        step_limit = 48
        moved_x = 0
        moved_y = 0
        while remaining_x != 0 or remaining_y != 0:
            step_x = max(-step_limit, min(step_limit, remaining_x))
            step_y = max(-step_limit, min(step_limit, remaining_y))
            self.execute(
                "human-monitor-command",
                {"command-line": f"mouse_move {step_x} {step_y}"},
            )
            moved_x += step_x
            moved_y += step_y
            remaining_x -= step_x
            remaining_y -= step_y
            # Give the USB HID report and firmware polling timer time to run.
            time.sleep(0.04)

        self._pointer_x = max(
            0, min(self._pointer_x + moved_x * 2, width - 1)
        )
        self._pointer_y = max(
            0, min(self._pointer_y + moved_y * 2, height - 1)
        )

    def mouse_button(self, down: bool) -> None:
        # QEMU HMP's mouse_button argument is the full state bitmask: 1=left, 0=up.
        state = 1 if down else 0
        self.execute(
            "human-monitor-command",
            {"command-line": f"mouse_button {state}"},
        )

    def click(self, x: int, y: int, width: int, height: int) -> None:
        self.pointer_position(x, y, width, height)
        time.sleep(0.15)
        self.mouse_button(True)
        time.sleep(0.15)
        self.mouse_button(False)
        time.sleep(0.15)


    def screenshot(self, path: Path) -> tuple[int, int, Counter[tuple[int, int, int]]]:
        self.execute("human-monitor-command", {"command-line": f"screendump {path}"})
        return read_ppm(path)



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
        elif character == "/":
            key = "slash"
        elif character == ".":
            key = "dot"
        elif character == "-":
            key = "minus"
        else:
            raise ValueError(f"unsupported test-key character: {character!r}")
        client.send_key(key)
        time.sleep(0.12)
    client.send_key("ret")


def _ppm_token(data: bytes, position: int) -> tuple[bytes, int]:
    while position < len(data):
        if data[position] in b" \t\r\n":
            position += 1
            continue
        if data[position] == ord("#"):
            newline = data.find(b"\n", position)
            if newline < 0:
                raise RuntimeError("invalid PPM comment at end of file")
            position = newline + 1
            continue
        break
    start = position
    while position < len(data) and data[position] not in b" \t\r\n#":
        position += 1
    if start == position:
        raise RuntimeError("missing token in PPM screenshot")
    return data[start:position], position


def read_ppm(path: Path) -> tuple[int, int, Counter[tuple[int, int, int]]]:
    """Read a bounded raw P6 screenshot and count RGB values to verify visible UI."""
    data = path.read_bytes()
    position = 0
    magic, position = _ppm_token(data, position)
    width_token, position = _ppm_token(data, position)
    height_token, position = _ppm_token(data, position)
    max_token, position = _ppm_token(data, position)
    if magic != b"P6" or max_token != b"255":
        raise RuntimeError("QEMU screenshot is not an 8-bit binary PPM image")
    width = int(width_token)
    height = int(height_token)
    if width < 480 or height < 360 or width > 8192 or height > 8192:
        raise RuntimeError(f"unexpected screenshot dimensions: {width}x{height}")

    if position >= len(data) or data[position] not in b" \t\r\n":
        raise RuntimeError("PPM header is missing its pixel separator")
    if data[position:position + 2] == b"\r\n":
        position += 2
    else:
        position += 1
    pixels = data[position:]
    expected_size = width * height * 3
    if len(pixels) != expected_size:
        raise RuntimeError(
            f"PPM pixel byte count mismatch: expected {expected_size}, got {len(pixels)}"
        )
    counts: Counter[tuple[int, int, int]] = Counter()
    for offset in range(0, len(pixels), 3):
        counts[(pixels[offset], pixels[offset + 1], pixels[offset + 2])] += 1
    return width, height, counts


def assert_desktop_is_visible(
    screenshot: tuple[int, int, Counter[tuple[int, int, int]]]
) -> None:
    width, height, colors = screenshot
    required_colors = {
        "panel": ((0x11, 0x1B, 0x2B), 100),
        "active window title bar": ((0x2C, 0x52, 0x6A), 100),
        "window body": ((0x1A, 0x26, 0x38), 1000),
        "wallpaper accent": ((0x20, 0x36, 0x51), 100),
        "visible text": ((0xEA, 0xF1, 0xFA), 30),
    }
    missing = [
        name for name, (rgb, minimum_count) in required_colors.items()
        if colors[rgb] < minimum_count
    ]
    if missing:
        raise RuntimeError(
            "desktop rendered without expected UI pixels: "
            + ", ".join(missing)
            + f"; screenshot={width}x{height}, distinct_colors={len(colors)}"
        )


def assert_terminal_is_visible(
    screenshot: tuple[int, int, Counter[tuple[int, int, int]]]
) -> None:
    width, height, colors = screenshot
    required_colors = {
        "terminal surface": ((0x0A, 0x11, 0x1B), 1000),
        "terminal text": ((0xC3, 0xF5, 0xD5), 30),
        "active title bar": ((0x2C, 0x52, 0x6A), 100),
    }
    missing = [
        name for name, (rgb, minimum_count) in required_colors.items()
        if colors[rgb] < minimum_count
    ]
    if missing:
        raise RuntimeError(
            "Terminal window is not visibly rendered: "
            + ", ".join(missing)
            + f"; screenshot={width}x{height}, distinct_colors={len(colors)}"
        )


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(f"usage: {argv[0]} QMP_SOCKET SERIAL_LOG", file=sys.stderr)
        return 2

    socket_path = argv[1]
    log_path = Path(argv[2])
    screenshot_path = log_path.with_name("desktop-screendump.ppm")
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
        wait_for_log(log_path, "ASTRAOS_DESKTOP_RENDERED", EVENT_TIMEOUT)
        wait_for_log(log_path, "Desktop pointer: UEFI mouse input available.", EVENT_TIMEOUT)

        initial_screenshot = client.screenshot(screenshot_path)
        assert_desktop_is_visible(initial_screenshot)
        width, height, _ = initial_screenshot
        selected_mouse = client.select_mouse_device()
        print(f"QEMU mouse input selected: {selected_mouse}")
        # HMP targets the currently selected emulated mouse, unlike display-routed
        # QMP events which may bypass the firmware's USB HID protocol.
        client.pointer_position(75, 20, width, height)
        time.sleep(0.25)
        wait_for_log(log_path, "desktop: pointer report", EVENT_TIMEOUT)
        client.click(75, 20, width, height)
        wait_for_log(log_path, "desktop: applications menu opened", EVENT_TIMEOUT)
        menu_screenshot = client.screenshot(screenshot_path)
        if menu_screenshot[2] == initial_screenshot[2]:
            raise RuntimeError("opening Applications did not change the captured desktop")
        client.click(65, 47 + 45, width, height)
        wait_for_log(log_path, "desktop: terminal window opened", EVENT_TIMEOUT)
        assert_terminal_is_visible(client.screenshot(screenshot_path))

        type_command(client, "help")
        wait_for_log(log_path, "desktop: terminal command help", EVENT_TIMEOUT)
        type_command(client, "pwd")
        wait_for_log(log_path, "desktop: terminal command pwd", EVENT_TIMEOUT)
        type_command(client, "ls")
        wait_for_log(log_path, "desktop: terminal command ls", EVENT_TIMEOUT)
        type_command(client, "touch mouse.txt")
        wait_for_log(log_path, "desktop: terminal filesystem create", EVENT_TIMEOUT)
        type_command(client, "write mouse.txt pointer-ok")
        wait_for_log(log_path, "desktop: terminal command write", EVENT_TIMEOUT)
        type_command(client, "exit")
        wait_for_log(log_path, "desktop: terminal window closed", EVENT_TIMEOUT)

        # Files sees the file Terminal just wrote; click the row twice to open it.
        client.click(75, 20, width, height)
        wait_for_log(log_path, "desktop: applications menu opened", EVENT_TIMEOUT)
        client.click(65, 47 + 70, width, height)
        wait_for_log(log_path, "desktop: file browser opened", EVENT_TIMEOUT)

        file_width = min(720, width - 40)
        file_height = min(500, height - 42 - 54 - 24)
        file_x = (width - file_width) // 2 + 24
        file_y = 42 + 8 + (height - 42 - 54 - file_height - 16) // 2 + 20
        if file_x > width - file_width - 8:
            file_x = width - file_width - 8
        if file_y > height - 54 - file_height - 8:
            file_y = height - 54 - file_height - 8
        first_row_y = file_y + 30 + 18 + 23
        mouse_file_row_y = first_row_y + 4 * 18 + 9
        client.click(file_x + 100, mouse_file_row_y, width, height)
        wait_for_log(log_path, "desktop: file entry selected by pointer", EVENT_TIMEOUT)
        client.click(file_x + 100, mouse_file_row_y, width, height)
        wait_for_log(log_path, "desktop: text viewer opened: mouse.txt", EVENT_TIMEOUT)
        client.send_key("b")
        wait_for_log(log_path, "desktop: text viewer closed", EVENT_TIMEOUT)

        # The taskbar can minimise, restore, and close the File Manager.
        client.click(file_x + file_width - 63, file_y + 16, width, height)
        wait_for_log(log_path, "desktop: window minimized", EVENT_TIMEOUT)
        client.click(280, 20, width, height)
        wait_for_log(log_path, "desktop: taskbar window focused", EVENT_TIMEOUT)
        client.click(file_x + file_width - 15, file_y + 16, width, height)
        wait_for_log(log_path, "desktop: window closed", EVENT_TIMEOUT)
        client.send_key("q")
        wait_for_log(log_path, "desktop: returned to shell", EVENT_TIMEOUT)

        print(
            "AstraOS desktop test passed: visible framebuffer UI, UEFI mouse input, "
            "Applications menu, Terminal/VFS, File Manager, taskbar restore and window controls."
        )
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
