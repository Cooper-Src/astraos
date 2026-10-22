#!/usr/bin/env python3
"""End-to-end QMP test for desktop rendering, keyboard input, and UEFI mouse support."""

from __future__ import annotations

import json
import re
import socket
import struct
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
        self._hmp_pointer_x: int | None = None
        self._hmp_pointer_y: int | None = None
        self.active_mouse_index: int | None = None
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

    def move_pointer_to(self, x: int, y: int, width: int, height: int) -> None:
        """Move the relative USB mouse with bounded, display-routed QMP events."""
        if width < 2 or height < 2:
            raise ValueError("framebuffer dimensions are too small for pointer input")
        x = max(0, min(x, width - 1))
        y = max(0, min(y, height - 1))
        if self._pointer_x is None or self._pointer_y is None:
            self._pointer_x = width // 2
            self._pointer_y = height // 2
        delta_x = int((x - self._pointer_x) / 2)
        delta_y = int((y - self._pointer_y) / 2)
        steps = max(1, (max(abs(delta_x), abs(delta_y)) + 39) // 40)
        for step in range(steps):
            next_x = int(delta_x * (step + 1) / steps)
            prev_x = int(delta_x * step / steps)
            next_y = int(delta_y * (step + 1) / steps)
            prev_y = int(delta_y * step / steps)
            relative_x = next_x - prev_x
            relative_y = next_y - prev_y
            if relative_x == 0 and relative_y == 0:
                continue
            self.execute(
                "input-send-event",
                {
                    "device": "video0",
                    "events": [
                        {"type": "rel", "data": {"axis": "x", "value": relative_x}},
                        {"type": "rel", "data": {"axis": "y", "value": relative_y}},
                    ],
                },
            )
            time.sleep(0.02)
        self._pointer_x = x
        self._pointer_y = y
        time.sleep(0.2)

    def mouse_button(self, down: bool) -> None:
        self.execute(
            "input-send-event",
            {
                "device": "video0",
                "events": [{"type": "btn", "data": {"button": "left", "down": down}}],
            },
        )
        time.sleep(0.15)

    def click(self, x: int, y: int, width: int, height: int) -> None:
        self.move_pointer_to(x, y, width, height)
        self.mouse_button(True)
        self.mouse_button(False)

    def hmp_click(self, x: int, y: int, width: int, height: int) -> None:
        """Use QEMU's active-mouse monitor API as a second independent input route."""
        if width < 2 or height < 2:
            raise ValueError("framebuffer dimensions are too small for pointer input")
        x = max(0, min(x, width - 1))
        y = max(0, min(y, height - 1))
        if self._hmp_pointer_x is None or self._hmp_pointer_y is None:
            self._hmp_pointer_x = width // 2
            self._hmp_pointer_y = height // 2
        delta_x = x - self._hmp_pointer_x
        delta_y = y - self._hmp_pointer_y
        self.execute(
            "human-monitor-command",
            {"command-line": f"mouse_set {self.active_mouse_index if self.active_mouse_index is not None else 0}"},
        )
        steps = max(1, (max(abs(delta_x), abs(delta_y)) + 39) // 40)
        for step in range(steps):
            next_x = int(delta_x * (step + 1) / steps)
            prev_x = int(delta_x * step / steps)
            next_y = int(delta_y * (step + 1) / steps)
            prev_y = int(delta_y * step / steps)
            move_x = int((next_x - prev_x) / 2)
            move_y = int((next_y - prev_y) / 2)
            if move_x == 0 and move_y == 0:
                continue
            self.execute(
                "human-monitor-command",
                {"command-line": f"mouse_move {move_x} {move_y}"},
            )
            time.sleep(0.02)
        self._hmp_pointer_x = x
        self._hmp_pointer_y = y
        time.sleep(0.2)
        self.execute(
            "human-monitor-command",
            {"command-line": "mouse_button 1"},
        )
        time.sleep(0.15)
        self.execute(
            "human-monitor-command",
            {"command-line": "mouse_button 0"},
        )
        time.sleep(0.15)

    def screenshot(self, path: Path) -> tuple[int, int, Counter[tuple[int, int, int]]]:
        self.execute("human-monitor-command", {"command-line": f"screendump {path}"})
        return read_ppm(path)



class HMPPointer:
    """Pointer adapter that sends all clicks through QEMU's selected mouse."""

    def __init__(self, client: QMPClient) -> None:
        self._client = client

    def click(self, x: int, y: int, width: int, height: int) -> None:
        self._client.hmp_click(x, y, width, height)


class VNCClient:
    """Tiny RFB client that sends absolute pointer events to QEMU's VNC backend."""

    def __init__(self, host: str = "127.0.0.1", port: int = 5901) -> None:
        self._socket = socket.create_connection((host, port), timeout=CONNECT_TIMEOUT)
        self._socket.settimeout(COMMAND_TIMEOUT)
        self.width = 0
        self.height = 0
        self._pointer_x = 0
        self._pointer_y = 0
        try:
            version = self._recv_exact(12)
            if not version.startswith(b"RFB 003."):
                raise RuntimeError(f"VNC server sent an invalid RFB version: {version!r}")
            self._socket.sendall(b"RFB 003.008\n")
            sec_count = self._recv_exact(1)[0]
            if sec_count == 0:
                length = int.from_bytes(self._recv_exact(4), "big")
                reason = self._recv_exact(length).decode("utf-8", errors="replace")
                raise RuntimeError(f"VNC server rejected the connection: {reason}")
            security_types = self._recv_exact(sec_count)
            if 1 not in security_types:
                raise RuntimeError(
                    f"VNC server offered unsupported authentication types: {security_types!r}"
                )
            self._socket.sendall(b"\x01")
            security_result = int.from_bytes(self._recv_exact(4), "big")
            if security_result != 0:
                raise RuntimeError("VNC server rejected None security")
            self._socket.sendall(b"\x01")  # ClientInit: shared desktop.
            server_init = self._recv_exact(24)
            self.width, self.height = struct.unpack("!HH", server_init[:4])
            name_length = int.from_bytes(server_init[20:24], "big")
            if name_length:
                self._recv_exact(name_length)

            pixel_format = struct.pack(
                "!BBBBHHHBBB3x",
                32, 24, 0, 1, 255, 255, 255, 16, 8, 0,
            )
            self._socket.sendall(b"\x00\x00\x00\x00" + pixel_format)
            self._socket.sendall(struct.pack("!BBHi", 2, 0, 1, 0))

            # QEMU's USB tablet is absolute, so RFB coordinates should match the
            # display pixel range and be scaled by the VNC backend into UEFI range.
            self._pointer_x = self.width // 2
            self._pointer_y = self.height // 2
            self._send_pointer(self._pointer_x, self._pointer_y, 0)
        except Exception:
            self._socket.close()
            raise

    def _recv_exact(self, size: int) -> bytes:
        payload = bytearray()
        while len(payload) < size:
            chunk = self._socket.recv(size - len(payload))
            if not chunk:
                raise RuntimeError("VNC server closed the connection during handshake")
            payload.extend(chunk)
        return bytes(payload)

    def close(self) -> None:
        self._socket.close()

    def _send_pointer(self, x: int, y: int, button_mask: int) -> None:
        x = max(0, min(x, self.width - 1))
        y = max(0, min(y, self.height - 1))
        self._socket.sendall(struct.pack("!BBHH", 5, button_mask, x, y))

    def move(self, x: int, y: int) -> None:
        self._pointer_x = max(0, min(x, self.width - 1))
        self._pointer_y = max(0, min(y, self.height - 1))
        self._send_pointer(self._pointer_x, self._pointer_y, 0)
        time.sleep(0.2)

    def button(self, left_down: bool) -> None:
        self._send_pointer(
            self._pointer_x, self._pointer_y, 1 if left_down else 0
        )
        time.sleep(0.15)

    def click(self, x: int, y: int, width: int, height: int) -> None:
        if width != self.width or height != self.height:
            raise RuntimeError(
                f"VNC/QMP size mismatch: VNC={self.width}x{self.height}, "
                f"QMP={width}x{height}"
            )
        self.move(x, y)
        self.button(True)
        self.button(False)




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
    mouse: VNCClient | None = None
    pointer: QMPClient | VNCClient | None = None
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
        mice = client.execute(
            "human-monitor-command",
            {"command-line": "info mice"},
        )
        print(f"QEMU input devices: {mice}")
        active_mouse = re.search(r"\*\s*Mouse #(\d+)", str(mice))
        if active_mouse is None:
            active_mouse = re.search(r"Mouse #(\d+)", str(mice))
        if active_mouse is not None:
            client.active_mouse_index = int(active_mouse.group(1))
        # First try QMP's documented guest-input event path, using the absolute
        # coordinate range expected by QEMU. If this display backend does not route
        # input to the USB tablet, fall back to the real VNC/RFB host-pointer path.
        pointer = client
        client.click(75, 20, width, height)
        try:
            wait_for_log(log_path, "desktop: pointer report", 1.5)
            wait_for_log(log_path, "desktop: applications menu opened", 1.5)
            print("Desktop pointer test used QMP absolute input events.")
        except TimeoutError:
            # QEMU's HMP mouse_set/mouse_move API explicitly selects its active
            # emulated mouse, which is useful when several input backends exist.
            client.hmp_click(75, 20, width, height)
            try:
                wait_for_log(log_path, "desktop: pointer report", EVENT_TIMEOUT)
                wait_for_log(log_path, "desktop: applications menu opened", EVENT_TIMEOUT)
                pointer = HMPPointer(client)
                print("Desktop pointer test used the QEMU active-mouse monitor API.")
            except TimeoutError:
                mouse = VNCClient()
                if mouse.width != width or mouse.height != height:
                    raise RuntimeError(
                        f"VNC/QMP size mismatch: VNC={mouse.width}x{mouse.height}, "
                        f"QMP={width}x{height}"
                    )
                pointer = mouse
                mouse.click(75, 20, width, height)
                wait_for_log(log_path, "desktop: pointer report", EVENT_TIMEOUT)
                wait_for_log(log_path, "desktop: applications menu opened", EVENT_TIMEOUT)
                print("Desktop pointer test used VNC/RFB absolute input events.")
        menu_screenshot = client.screenshot(screenshot_path)
        if menu_screenshot[2] == initial_screenshot[2]:
            raise RuntimeError("opening Applications did not change the captured desktop")
        pointer.click(65, 47 + 45, width, height)
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
        type_command(client, "write mouse.txt pointerok")
        wait_for_log(log_path, "desktop: terminal command write", EVENT_TIMEOUT)
        type_command(client, "exit")
        wait_for_log(log_path, "desktop: terminal window closed", EVENT_TIMEOUT)

        # Files sees the file Terminal just wrote; click the row twice to open it.
        pointer.click(75, 20, width, height)
        wait_for_log(log_path, "desktop: applications menu opened", EVENT_TIMEOUT)
        pointer.click(65, 47 + 70, width, height)
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
        pointer.click(file_x + 100, mouse_file_row_y, width, height)
        wait_for_log(log_path, "desktop: file entry selected by pointer", EVENT_TIMEOUT)
        pointer.click(file_x + 100, mouse_file_row_y, width, height)
        wait_for_log(log_path, "desktop: text viewer opened: mouse.txt", EVENT_TIMEOUT)
        client.send_key("b")
        wait_for_log(log_path, "desktop: text viewer closed", EVENT_TIMEOUT)

        # The taskbar can minimise, restore, and close the File Manager.
        pointer.click(file_x + file_width - 63, file_y + 16, width, height)
        wait_for_log(log_path, "desktop: window minimized", EVENT_TIMEOUT)
        pointer.click(280, 20, width, height)
        wait_for_log(log_path, "desktop: taskbar window focused", EVENT_TIMEOUT)
        pointer.click(file_x + file_width - 15, file_y + 16, width, height)
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
        if mouse is not None:
            mouse.close()
        if client is not None:
            client.close()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
