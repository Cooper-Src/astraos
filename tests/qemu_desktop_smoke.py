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
    """Minimal RFB 3.8 client used to send host-side pointer events to QEMU."""

    def __init__(self, host: str, port: int) -> None:
        self._socket = socket.create_connection((host, port), timeout=CONNECT_TIMEOUT)
        self._socket.settimeout(COMMAND_TIMEOUT)
        try:
            version = self._read_exact(12)
            if not version.startswith(b"RFB 003."):
                raise RuntimeError(f"unexpected VNC protocol banner: {version!r}")
            self._socket.sendall(b"RFB 003.008\n")

            security_count = self._read_exact(1)[0]
            if security_count == 0:
                reason_length = struct.unpack("!I", self._read_exact(4))[0]
                reason = self._read_exact(reason_length).decode("utf-8", errors="replace")
                raise RuntimeError(f"VNC server refused connection: {reason}")
            security_types = self._read_exact(security_count)
            if 1 not in security_types:
                raise RuntimeError(
                    f"VNC smoke test expects loopback 'None' security, got {list(security_types)}"
                )
            self._socket.sendall(b"\x01")
            security_result = struct.unpack("!I", self._read_exact(4))[0]
            if security_result != 0:
                raise RuntimeError(f"VNC security handshake failed: {security_result}")

            self._socket.sendall(b"\x01")  # ClientInit: shared session.
            server_init = self._read_exact(24)
            self.width, self.height = struct.unpack("!HH", server_init[:4])
            bits_per_pixel = server_init[4]
            if bits_per_pixel not in (8, 16, 32):
                raise RuntimeError(f"unsupported VNC pixel format: {bits_per_pixel} bits/pixel")
            name_length = struct.unpack("!I", server_init[20:24])[0]
            self._read_exact(name_length)
            if self.width < 1 or self.height < 1:
                raise RuntimeError(f"invalid VNC screen size: {self.width}x{self.height}")

            # Complete the basic RFB client setup and request the first framebuffer.
            # Raw is deliberately the only encoding requested, making updates bounded
            # and independent of optional VNC extensions.
            self._socket.sendall(b"\\x00\\x00\\x00\\x00" + server_init[4:20])
            self._socket.sendall(struct.pack("!BBH", 2, 0, 1) + struct.pack("!i", 0))
            self._socket.sendall(
                struct.pack("!BBHHHH", 3, 0, 0, 0, self.width, self.height)
            )
            update_header = self._read_exact(4)
            if update_header[0] != 0:
                raise RuntimeError(f"expected VNC framebuffer update, got type {update_header[0]}")
            rect_count = struct.unpack("!H", update_header[2:4])[0]
            pixel_bytes = bits_per_pixel // 8
            for _ in range(rect_count):
                rectangle = self._read_exact(12)
                rect_width, rect_height = struct.unpack("!HH", rectangle[4:8])
                encoding = struct.unpack("!i", rectangle[8:12])[0]
                if encoding == 0:  # Raw rectangle.
                    self._read_exact(rect_width * rect_height * pixel_bytes)
                elif encoding == 1:  # CopyRect.
                    self._read_exact(4)
                else:
                    raise RuntimeError(f"unexpected VNC encoding {encoding} in raw-only session")
        except Exception:
            self._socket.close()
            raise

    def _read_exact(self, size: int) -> bytes:
        data = bytearray()
        while len(data) < size:
            chunk = self._socket.recv(size - len(data))
            if not chunk:
                raise RuntimeError("VNC server closed the connection")
            data.extend(chunk)
        return bytes(data)

    def close(self) -> None:
        self._socket.close()

    def pointer(self, x: int, y: int, buttons: int = 0) -> None:
        x = max(0, min(x, self.width - 1))
        y = max(0, min(y, self.height - 1))
        self._socket.sendall(struct.pack("!BBHH", 5, buttons & 0xFF, x, y))


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


def wait_for_log_occurrence(
    path: Path, needle: str, occurrence: int, timeout: float
) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        content = read_log(path)
        if content.count(needle) >= occurrence:
            return content
        time.sleep(0.1)
    content = read_log(path)
    raise TimeoutError(
        f"serial output did not reach occurrence {occurrence} of {needle!r}. "
        f"Final output:\n{content[-6000:] if content else '(serial log is empty)'}"
    )


def save_and_check_screenshot(client: QMPClient, path: Path) -> tuple[int, int]:
    path.unlink(missing_ok=True)
    client.execute("screendump", {"filename": str(path), "format": "ppm"})
    data = path.read_bytes()
    position = 0

    def token() -> bytes:
        nonlocal position
        while position < len(data):
            if data[position] in b" \t\r\n":
                position += 1
            elif data[position] == ord("#"):
                while position < len(data) and data[position] not in b"\r\n":
                    position += 1
            else:
                break
        start = position
        while position < len(data) and data[position] not in b" \t\r\n#":
            position += 1
        if start == position:
            raise ValueError("invalid PPM screenshot header")
        return data[start:position]

    if token() != b"P6":
        raise ValueError("QEMU screenshot is not a binary RGB PPM")
    width = int(token())
    height = int(token())
    max_value = int(token())
    if width < 480 or height < 360 or max_value != 255:
        raise ValueError(f"unexpected desktop screenshot geometry/format: {width}x{height}, max={max_value}")
    if position < len(data) and data[position:position + 2] == b"\r\n":
        position += 2
    elif position < len(data) and data[position] in b" \t\r\n":
        position += 1
    pixels = data[position:]
    if len(pixels) != width * height * 3:
        raise ValueError(
            f"truncated screenshot: expected {width * height * 3} RGB bytes, got {len(pixels)}"
        )

    sample_step_x = max(1, width // 48)
    sample_step_y = max(1, height // 32)
    colors: set[tuple[int, int, int]] = set()
    for y in range(0, height, sample_step_y):
        for x in range(0, width, sample_step_x):
            offset = (y * width + x) * 3
            colors.add((pixels[offset], pixels[offset + 1], pixels[offset + 2]))
    if len(colors) < 12:
        raise ValueError(
            f"desktop screenshot looks blank/unrendered: only {len(colors)} sampled colors"
        )
    print(f"Desktop screenshot validated: {width}x{height}, {len(colors)} sampled colors ({path}).")
    return width, height


def click_at(
    client: QMPClient,
    pointer: VNCClient,
    log_path: Path,
    x: int,
    y: int,
    marker: str,
    occurrence: int,
) -> None:
    pointer.pointer(x, y, buttons=0)
    time.sleep(0.2)
    pointer.pointer(x, y, buttons=1)
    try:
        wait_for_log_occurrence(log_path, marker, occurrence, EVENT_TIMEOUT)
    finally:
        pointer.pointer(x, y, buttons=0)


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


def main(argv: list[str]) -> int:
    if len(argv) != 5:
        print(f"usage: {argv[0]} QMP_SOCKET SERIAL_LOG SCREENSHOT_PPM VNC_PORT", file=sys.stderr)
        return 2

    socket_path = argv[1]
    log_path = Path(argv[2])
    screenshot_path = Path(argv[3])
    try:
        vnc_port = int(argv[4])
    except ValueError:
        print("VNC_PORT must be an integer", file=sys.stderr)
        return 2
    client: QMPClient | None = None
    pointer: VNCClient | None = None
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
        wait_for_log(log_path, "ASTRAOS_POINTER_INPUT_READY", BOOT_TIMEOUT)
        screen_width, screen_height = save_and_check_screenshot(client, screenshot_path)

        vnc_deadline = time.monotonic() + CONNECT_TIMEOUT
        last_vnc_error: OSError | None = None
        while time.monotonic() < vnc_deadline:
            try:
                pointer = VNCClient("127.0.0.1", vnc_port)
                break
            except OSError as error:
                last_vnc_error = error
                time.sleep(0.1)
        if pointer is None:
            raise RuntimeError(
                f"could not connect to QEMU VNC input port {vnc_port}: {last_vnc_error}"
            )
        if (pointer.width, pointer.height) != (screen_width, screen_height):
            raise RuntimeError(
                f"VNC/QMP display sizes differ: {pointer.width}x{pointer.height} vs "
                f"{screen_width}x{screen_height}"
            )
        print(f"Connected to VNC pointer input: {pointer.width}x{pointer.height}.")
        client.send_key("t")
        wait_for_log(log_path, "desktop: terminal window opened", EVENT_TIMEOUT)
        type_command(client, "help")
        wait_for_log(log_path, "desktop: terminal command help", EVENT_TIMEOUT)
        type_command(client, "pwd")
        wait_for_log(log_path, "desktop: terminal command pwd", EVENT_TIMEOUT)
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

        start_marker = "desktop: mouse start menu opened"
        click_at(
            client, pointer, log_path, 90, 20, start_marker, 1
        )
        # Capture the actual start menu too, so CI verifies more than a boot marker.
        save_and_check_screenshot(client, screenshot_path)

        click_at(
            client, pointer, log_path, 90, 121, "desktop: mouse files opened", 1
        )
        # Taskbar buttons must minimise the active app and restore it on the next click.
        click_at(
            client, pointer, log_path, min(272, screen_width - 20), 20,
            "desktop: mouse task minimized", 1
        )
        click_at(
            client, pointer, log_path, min(272, screen_width - 20), 20,
            "desktop: mouse task restored", 1
        )

        click_at(
            client, pointer, log_path, 90, 20, start_marker, 2
        )
        click_at(
            client, pointer, log_path, 90, 94, "desktop: mouse terminal opened", 1
        )
        type_command(client, "help")
        wait_for_log_occurrence(
            log_path, "desktop: terminal command help", 2, EVENT_TIMEOUT
        )
        type_command(client, "exit")
        wait_for_log_occurrence(
            log_path, "desktop: terminal window closed", 2, EVENT_TIMEOUT
        )
        click_at(
            client, pointer, log_path, 90, 20, start_marker, 3
        )
        click_at(
            client, pointer, log_path, 90, 174, "desktop: mouse returned to shell", 1
        )
        wait_for_log(log_path, "desktop: returned to shell", EVENT_TIMEOUT)
        print("AstraOS desktop smoke test passed: framebuffer pixels, keyboard, terminal, Files, mouse clicks, start menu, taskbar minimise/restore and return-to-shell all worked.")
        return 0
    except (OSError, RuntimeError, TimeoutError, ValueError, json.JSONDecodeError) as error:
        print(f"desktop smoke test failed: {error}", file=sys.stderr)
        print("--- final serial output ---", file=sys.stderr)
        print(read_log(log_path)[-6000:], file=sys.stderr)
        return 1
    finally:
        if pointer is not None:
            pointer.close()
        if client is not None:
            client.close()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
