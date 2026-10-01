"""Builds the "config" object for the engine's "configure" IPC command.

Mirrors packet_probe::cli::engine_config_to_json/from_json
(apps/packet-probe-cli/engine_config.cpp) field-for-field so the viewer and the
C++ engine agree on the config shape without either side guessing the other's.
See docs/ipc-protocol.md, "Control Protocol v2", for the wire schema.
"""

import os
from pathlib import Path


_MODE_UI_TO_ENGINE = {
    "UDP": "udp",
    "TCP Client": "tcp-client",
    "TCP Server": "tcp-server",
    "TCP Proxy": "tcp-proxy",
    "Serial": "serial",
}

# tcp-proxy forwards traffic between an existing client and a target; it has no
# send() (see docs/ipc-protocol.md and TcpProxyCaptureSession).
_SEND_UNSUPPORTED_ENGINE_MODES = ("tcp-proxy",)


def engine_mode_for_ui(ui_mode: str) -> str:
    """Maps a mode-combo display string (e.g. "TCP Client") to the engine's
    mode identifier (e.g. "tcp-client")."""
    try:
        return _MODE_UI_TO_ENGINE[ui_mode]
    except KeyError:
        raise ValueError(f"unknown capture mode: {ui_mode}")


def supports_send(engine_mode: str) -> bool:
    return engine_mode not in _SEND_UNSUPPORTED_ENGINE_MODES


def _parse_port(value: str, field_name: str) -> int:
    value = value.strip()
    if not value:
        return 0
    try:
        port = int(value)
    except ValueError:
        raise ValueError(f"{field_name} must be a number: {value!r}")
    if port < 0 or port > 65535:
        raise ValueError(f"{field_name} must be between 0 and 65535: {port}")
    return port


def build_decoder_config(
    decoder: str,
    frame_size: int = 0,
    delimiter_hex: str = "",
    include_delimiter: bool = True,
    length_size: int = 2,
    length_endian: str = "big",
    length_includes_header: bool = False,
) -> dict:
    return {
        "decoder": decoder,
        "frame_size": frame_size,
        "delimiter_hex": delimiter_hex,
        "include_delimiter": include_delimiter,
        "length_size": length_size,
        "length_endian": length_endian,
        "length_includes_header": length_includes_header,
    }


def build_capture_config(ui_mode: str, fields: dict, decoder: dict, common: dict) -> dict:
    """Builds the full "config" object for a "configure" command.

    `ui_mode` is the mode-combo display string (e.g. "TCP Client"). `fields` holds
    the mode-specific values as raw strings, keyed by the same names main_window's
    widgets use: bind_host/bind_port/target_host/target_port (UDP), host/port
    (TCP Client), listen_host/listen_port (TCP Server), listen_host/listen_port/
    target_host/target_port (TCP Proxy), serial_port/baudrate (Serial). `decoder`
    is the dict from build_decoder_config(). `common` holds log_path/hex_raw/
    hex_frame/latency.

    Raises ValueError with a user-facing message on invalid input (e.g. a
    non-numeric port), so callers can show it directly in a warning dialog.
    """
    mode = engine_mode_for_ui(ui_mode)

    config = {
        "mode": mode,
        "host": "",
        "port": 0,
        "serial_port": "",
        "baudrate": 115200,
        "listen_host": "",
        "listen_port": 0,
        "bind_host": "0.0.0.0",
        "bind_port": 0,
        "target_host": "",
        "target_port": 0,
        "log_path": common.get("log_path", ""),
        "hex_raw": common.get("hex_raw", False),
        "hex_frame": common.get("hex_frame", False),
        "latency": common.get("latency", True),
        "decoder": decoder,
    }

    if mode == "udp":
        config["bind_host"] = fields.get("bind_host", "0.0.0.0").strip() or "0.0.0.0"
        config["bind_port"] = _parse_port(fields.get("bind_port", ""), "Bind Port")
        config["target_host"] = fields.get("target_host", "").strip()
        config["target_port"] = _parse_port(fields.get("target_port", ""), "Target Port")
    elif mode == "tcp-client":
        config["host"] = fields.get("host", "").strip()
        config["port"] = _parse_port(fields.get("port", ""), "Remote Port")
    elif mode == "tcp-server":
        config["listen_host"] = fields.get("listen_host", "").strip()
        config["listen_port"] = _parse_port(fields.get("listen_port", ""), "Listen Port")
    elif mode == "tcp-proxy":
        config["listen_host"] = fields.get("listen_host", "").strip()
        config["listen_port"] = _parse_port(fields.get("listen_port", ""), "Listen Port")
        config["target_host"] = fields.get("target_host", "").strip()
        config["target_port"] = _parse_port(fields.get("target_port", ""), "Target Port")
    elif mode == "serial":
        config["serial_port"] = fields.get("serial_port", "").strip()
        baudrate = fields.get("baudrate", "").strip()
        if baudrate:
            try:
                config["baudrate"] = int(baudrate)
            except ValueError:
                raise ValueError(f"Baud Rate must be a number: {baudrate!r}")

    return config


def find_packet_probe_binary() -> str:
    # 1. Check environment variable
    env_path = os.environ.get("PACKET_PROBE_CLI")
    if env_path:
        return env_path

    # 2. Check workspace build directory relative to this file
    try:
        current_dir = Path(__file__).resolve().parent
        workspace_root = current_dir.parents[1]
        build_root = workspace_root / "build"
        exe_name = "packet-probe.exe" if os.name == "nt" else "packet-probe"

        candidates = [
            build_root / "packet-probe",
            build_root / "apps" / "packet-probe-cli" / "packet-probe",
            build_root / exe_name,
            build_root / "apps" / "packet-probe-cli" / exe_name,
        ]
        # Multi-config generators (MSVC/Ninja Multi-Config) place binaries
        # under a per-configuration subdirectory instead of build/ directly.
        for config in ("Debug", "Release", "RelWithDebInfo", "MinSizeRel"):
            candidates.append(build_root / config / exe_name)
            candidates.append(build_root / "apps" / "packet-probe-cli" / config / exe_name)

        for candidate in candidates:
            if candidate.exists() and os.access(candidate, os.X_OK):
                return str(candidate)
    except Exception:
        pass

    return "packet-probe"
