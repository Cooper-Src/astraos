#!/usr/bin/env python3
"""QMP-driven smoke test for the AstraOS desktop and in-kernel display server."""

from __future__ import annotations

import json
import socket
import struct
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


class VNCClient:
    """Minimal RFB 3.8 client that forwards pointer events through QEMU's VNC frontend."""

    def __init__(self, host: str, port: int) -> None:
        self._socket = socket.create_connection((host, port), timeout=CONNECT_TIMEOUT)
        self._socket.settimeout(COMMAND_TIMEOUT)
        try:
            version = self._read_exact(12)
            if not version.startswith(b"RFB 003."):
                raise RuntimeError(f"unexpected VNC protocol banner: {version!r}")
            self._socket.sendall(version)

            security_count = self._read_exact(1)[0]
            if security_count == 0:
                reason_length = struct.unpack(">I", self._read_exact(4))[0]
                reason = self._read_exact(reason_length).decode("utf-8", errors="replace")
                raise RuntimeError(f"VNC server refused connection: {reason}")
            security_types = self._read_exact(security_count)
            if 1 not in security_types:
                raise RuntimeError(
                    f"VNC server has no unauthenticated security type: {security_types!r}"
                )
            self._socket.sendall(b"\x01")
            security_result = struct.unpack(">I", self._read_exact(4))[0]
            if security_result != 0:
                raise RuntimeError(f"VNC security handshake failed: {security_result}")

            self._socket.sendall(b"\x01")
            server_init = self._read_exact(24)
            self.width, self.height = struct.unpack(">HH", server_init[:4])
            name_length = struct.unpack(">I", server_init[20:24])[0]
            self.server_name = self._read_exact(name_length).decode("utf-8", errors="replace")
            if self.width < 480 or self.height < 360:
                raise RuntimeError(f"VNC framebuffer is too small: {self.width}x{self.height}")
        except Exception:
            self._socket.close()
            raise

    def _read_exact(self, length: int) -> bytes:
        if length < 0 or length > 65536:
            raise ValueError(f"invalid VNC read length: {length}")
        result = bytearray()
        while len(result) < length:
            chunk = self._socket.recv(length - len(result))
            if not chunk:
                raise RuntimeError("VNC connection closed during handshake")
            result.extend(chunk)
        return bytes(result)

    def pointer(self, x: int, y: int, buttons: int = 0) -> None:
        clamped_x = max(0, min(self.width - 1, x))
        clamped_y = max(0, min(self.height - 1, y))
        self._socket.sendall(
            struct.pack(">BBHH", 5, buttons & 0x07, clamped_x, clamped_y)
        )

    def close(self) -> None:
        self._socket.close()


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



def click_mouse(client: VNCClient, x: int, y: int) -> None:
    client.pointer(x, y, buttons=1)
    time.sleep(0.2)
    client.pointer(x, y, buttons=0)
    time.sleep(0.2)


def move_mouse_to(
    client: VNCClient,
    current_x: int,
    current_y: int,
    target_x: int,
    target_y: int,
    screen_width: int,
    screen_height: int,
) -> tuple[int, int]:
    del current_x, current_y, screen_width, screen_height
    client.pointer(target_x, target_y, buttons=0)
    time.sleep(0.2)
    return target_x, target_y


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
        vnc = VNCClient("127.0.0.1", 5905)
        if (vnc.width, vnc.height) != (screen_width, screen_height):
            raise RuntimeError(
                f"VNC/GOP dimensions differ: {vnc.width}x{vnc.height} vs "
                f"{screen_width}x{screen_height}"
            )
        mouse_x = screen_width // 2
        mouse_y = screen_height // 2

        mouse_x, mouse_y = move_mouse_to(
            vnc, mouse_x, mouse_y, 80, 20, screen_width, screen_height
        )
        click_mouse(vnc, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: applications menu opened by pointer", EVENT_TIMEOUT)
        mouse_x, mouse_y = move_mouse_to(
            vnc, mouse_x, mouse_y, 80, 95, screen_width, screen_height
        )
        click_mouse(vnc, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: terminal window opened by pointer", EVENT_TIMEOUT)

        type_command(client, "help")
        wait_for_log(log_path, "desktop: terminal command help", EVENT_TIMEOUT)
        type_command(client, "pwd")
        wait_for_log(log_path, "desktop: terminal command pwd", EVENT_TIMEOUT)
        type_command(client, "ls")
        wait_for_log(log_path, "desktop: terminal command ls", EVENT_TIMEOUT)

        mouse_x, mouse_y = move_mouse_to(
            vnc, mouse_x, mouse_y, 80, 20, screen_width, screen_height
        )
        click_mouse(vnc, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: applications menu opened by pointer", EVENT_TIMEOUT)
        mouse_x, mouse_y = move_mouse_to(
            vnc, mouse_x, mouse_y, 80, 119, screen_width, screen_height
        )
        click_mouse(vnc, mouse_x, mouse_y)
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
            vnc, mouse_x, mouse_y, file_x + 220, file_y + 76,
            screen_width, screen_height
        )
        click_mouse(vnc, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: file selected by pointer", EVENT_TIMEOUT)
        click_mouse(vnc, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: files directory opened", EVENT_TIMEOUT)

        click_mouse(vnc, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: text viewer opened", EVENT_TIMEOUT)

        mouse_x, mouse_y = move_mouse_to(
            vnc, mouse_x, mouse_y, 280, 20, screen_width, screen_height
        )
        click_mouse(vnc, mouse_x, mouse_y)
        wait_for_log(log_path, "desktop: task button focused by pointer", EVENT_TIMEOUT)
        type_command(client, "whoami")
        type_command(client, "exit")
        wait_for_log(log_path, "desktop: terminal window closed", EVENT_TIMEOUT)
        client.send_key("q")
        wait_for_log(log_path, "desktop: returned to shell", EVENT_TIMEOUT)
        vnc.close()
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
