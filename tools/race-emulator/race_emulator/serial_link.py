"""USB serial connection that survives a device disappearing and reappearing."""

import threading
import time

import serial
from serial.tools import list_ports


def available_ports():
    return [{"device": p.device, "description": p.description,
             "serial_number": p.serial_number, "vid": p.vid, "pid": p.pid,
             "location": p.location} for p in list_ports.comports() if p.vid is not None]


class SerialLink:
    def __init__(self, name, callback, baud=115200):
        self.name = name
        self.callback = callback
        self.baud = baud
        self.wanted = None
        self.identity = None
        self.port = None
        self.connection = None
        self.lock = threading.RLock()
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._loop, daemon=True, name="serial-" + name)
        self.thread.start()

    def select(self, device):
        ports = available_ports()
        identity = next((p for p in ports if p["device"] == device), None)
        if identity is None:
            raise ValueError("USB port is not available")
        with self.lock:
            self._close()
            self.wanted = device
            self.identity = identity

    def deselect(self):
        with self.lock:
            self.wanted = None
            self.identity = None
            self._close()

    def connected(self):
        with self.lock:
            return self.connection is not None

    def send(self, command):
        if not isinstance(command, str) or "\n" in command or "\r" in command:
            raise ValueError("command must be one line")
        disconnected = None
        with self.lock:
            if self.connection is None:
                raise RuntimeError(self.name + " is disconnected")
            try:
                self.connection.write((command + "\n").encode("ascii"))
            except (serial.SerialException, OSError) as exc:
                self._close()
                disconnected = str(exc)
        if disconnected is not None:
            self.callback(self.name, "disconnect", disconnected)
            raise RuntimeError(self.name + " disconnected")
        self.callback(self.name, "command", command)

    def _find(self):
        ports = available_ports()
        if self.identity:
            identity = self.identity
            if identity["serial_number"]:
                for port in ports:
                    if (port["serial_number"] == identity["serial_number"]
                            and port["vid"] == identity["vid"]
                            and port["pid"] == identity["pid"]):
                        return port["device"]
            if identity["location"]:
                for port in ports:
                    if (port["location"] == identity["location"]
                            and port["vid"] == identity["vid"]
                            and port["pid"] == identity["pid"]):
                        return port["device"]
            if identity["serial_number"] or identity["location"]:
                return None
        return next((p["device"] for p in ports if p["device"] == self.wanted), None)

    def _close(self):
        if self.connection is not None:
            try:
                self.connection.close()
            except (serial.SerialException, OSError):
                pass
            self.connection = None
            self.port = None

    def _loop(self):
        pending = bytearray()
        while not self.stop_event.is_set():
            with self.lock:
                wanted = self.wanted
                connection = self.connection
            if not wanted:
                self.stop_event.wait(0.3)
                continue
            if connection is None:
                found = self._find()
                if found:
                    try:
                        opened = serial.Serial(found, self.baud, timeout=0.2, write_timeout=0.5)
                    except (serial.SerialException, OSError):
                        self.stop_event.wait(0.5)
                        continue
                    with self.lock:
                        if self.wanted != wanted:
                            opened.close()
                            continue
                        self.connection = opened
                        self.port = found
                    pending.clear()
                    self.callback(self.name, "connect", found)
                else:
                    self.stop_event.wait(0.5)
                continue
            try:
                chunk = connection.read(256)
                if not chunk and self._find() is None:
                    raise serial.SerialException("USB port disappeared")
            except (serial.SerialException, OSError) as exc:
                with self.lock:
                    if self.connection is connection:
                        self._close()
                if pending:
                    self.callback(self.name, "line", pending.decode("utf-8", "replace"))
                    pending.clear()
                self.callback(self.name, "disconnect", str(exc))
                continue
            pending.extend(chunk)
            while b"\n" in pending:
                line, _, remainder = pending.partition(b"\n")
                pending = bytearray(remainder)
                self.callback(self.name, "line", line.decode("utf-8", "replace").rstrip("\r"))
            if len(pending) > 8192:
                self.callback(self.name, "line", pending.decode("utf-8", "replace"))
                pending.clear()

    def close(self):
        self.stop_event.set()
        with self.lock:
            self.wanted = None
            self._close()
        self.thread.join(timeout=2)
