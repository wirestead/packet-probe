import json
import os
import re
import shutil
from pathlib import Path
from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QLineEdit, QPushButton,
    QLabel, QTableView, QSplitter, QHeaderView, QMessageBox, QFileDialog,
    QPlainTextEdit, QGroupBox, QTabWidget, QComboBox, QRadioButton, QStackedWidget,
    QSpinBox, QCheckBox, QGridLayout, QDialog, QDialogButtonBox, QButtonGroup,
    QInputDialog, QFrame
)
from PySide6.QtCore import Qt, QItemSelection, QModelIndex, QTimer, QSettings
from PySide6.QtGui import QFont, QIntValidator
from .ipc_client import IpcClientWorker
from .event_model import PacketEvent
from .packet_table_model import PacketTableModel, PacketFilterProxyModel, DirectionChipDelegate
from .widgets.hex_view import HexView
from .widgets.event_detail import EventDetailView
from .jsonl_log_loader import load_packet_probe_jsonl
from .ipc_path import make_default_ipc_path, resolve_initial_socket_path
from .capture_process import CaptureProcess
from .capture_command import (
    build_capture_config, build_decoder_config, engine_mode_for_ui, find_packet_probe_binary, supports_send,
)
from .styles import DARK_THEME_QSS
from .viewer_settings import ViewerState, ViewerSettingsManager
from .ipc_connector import IpcConnector


def _chip_qss(bg: str, fg: str, border: str) -> str:
    # Shared pill/chip styling for the status indicators. Colors are the sRGB
    # equivalents of the "Packet Probe Viewer" design mock's oklch chips.
    return (
        f"background-color: {bg}; color: {fg}; border-radius: 6px; "
        f"padding: 4px 10px; font-weight: bold; border: 1px solid {border};"
    )


# Each transport maps to a distinct accent (base, hover) - the design's
# mode-adaptive accent, so the active capture mode reads through the primary
# buttons, focus rings, active pills and tab. sRGB of the mock's MODE_COLORS.
MODE_ACCENTS: dict[str, tuple[str, str]] = {
    "UDP": ("#2cb3b3", "#43c4c4"),
    "TCP Client": ("#4ba3f7", "#68b4f9"),
    "TCP Server": ("#7e8ef4", "#95a1f6"),
    "TCP Proxy": ("#ae7ee2", "#bd94e8"),
    "Serial": ("#eb883b", "#ef9a58"),
}


def _accent_overrides(accent: str, accent_hover: str) -> str:
    # Appended after DARK_THEME_QSS so the mode's accent wins for the widgets
    # that should recolor with the transport.
    return f"""
QLineEdit:focus, QComboBox:focus, QSpinBox:focus {{ border: 1px solid {accent}; background-color: #030509; }}
QPushButton#start_capture_btn, QPushButton#send_btn {{ background-color: {accent}; border: 1px solid {accent}; color: #090b0f; font-weight: 700; }}
QPushButton#start_capture_btn:hover, QPushButton#send_btn:hover {{ background-color: {accent_hover}; }}
QTabBar::tab:selected {{ background-color: #10141b; color: {accent}; font-weight: bold; }}
QCheckBox::indicator:checked {{ background-color: {accent}; border: 1px solid {accent}; }}
QRadioButton::indicator:checked {{ background-color: {accent}; border: 1px solid {accent}; }}
QComboBox QAbstractItemView {{ selection-background-color: {accent}; selection-color: #090b0f; }}
QMenu::item:selected {{ background-color: {accent}; color: #090b0f; }}
QPushButton:checked {{ background-color: {accent}; border: 1px solid {accent}; color: #090b0f; }}
"""


def _pill_qss(active: bool, accent: str) -> str:
    # Segmented-control button (Mode / Filter direction / Send format). Active
    # pill is filled with the accent and dark text; inactive is transparent.
    if active:
        return (
            f"QPushButton {{ background-color: {accent}; color: #090b0f; border: none; "
            f"border-radius: 6px; padding: 6px 12px; font-weight: 700; }}"
        )
    return (
        "QPushButton { background-color: transparent; color: #88909c; border: none; "
        "border-radius: 6px; padding: 6px 12px; font-weight: 600; } "
        "QPushButton:hover { color: #d4d8de; }"
    )


# Inset "track" behind a segmented pill group.
_PILL_TRACK_QSS = (
    "QWidget#pill_track { background-color: #05070d; border-radius: 8px; } "
)


def format_metadata_message(metadata: dict | None) -> str:
    if not metadata:
        return ""
    schema = metadata.get("schema", "")
    event_schema = metadata.get("event_schema", "")
    version = metadata.get("version", "")
    return f"Metadata: schema={schema}, event_schema={event_schema}, version={version}"


class MainWindow(QMainWindow):
    def __init__(self, initial_socket_path: str = "/tmp/packet-probe.sock", settings_org: str = "UnilinkLab", settings_app: str = "PacketProbeViewer", parent=None):
        super().__init__(parent)
        self.setWindowTitle("Packet Probe Viewer")
        self.resize(1080, 840)

        # Mode-adaptive accent (see MODE_ACCENTS); defaults to UDP's cyan.
        self._accent, self._accent_hover = MODE_ACCENTS["UDP"]
        self.setStyleSheet(DARK_THEME_QSS + _accent_overrides(self._accent, self._accent_hover))

        # Session send macros (name/format/data/eol), like the design's macro row.
        self._macros: list[dict] = []

        self.generated_ipc_path = make_default_ipc_path()
        self.initial_socket_path = resolve_initial_socket_path(initial_socket_path)

        self.worker: IpcClientWorker | None = None
        self.capture_process = CaptureProcess(self)
        self._ipc_connector: IpcConnector | None = None
        self._ipc_connected = False
        self._active_capture_mode = ""

        # Engine control protocol v2 state (see docs/ipc-protocol.md). Once connected,
        # the viewer drives capture entirely through configure/start_capture/
        # stop_capture commands rather than process spawn/kill, so a config change no
        # longer requires restarting the packet-probe process.
        self._engine_state = "idle"
        # command id -> intent, so on_result_received can react per-command
        # ("configure", "start_capture", "stop_capture", "send").
        self._pending_commands: dict[str, str] = {}
        # True once the viewer itself has issued start_capture and is waiting for
        # engine confirmation; distinguishes "we're starting" from "someone else's
        # capture is already running" when a status broadcast arrives.
        self._starting_capture = False
        # Config queued by start_capture() while we wait for the engine
        # process/connection to come up, applied once on_status_changed("connected") fires.
        self._pending_config_to_start: dict | None = None

        self.is_paused = False
        self.pending_events: list[PacketEvent] = []
        self._event_count = 0
        self._error_count = 0

        self.setup_ui()
        self._settings_manager = ViewerSettingsManager(settings_org, settings_app)
        self.load_settings()
        self.set_mode("idle")
        self.update_status("Status: disconnected")
        self.update_port("-")
        self.update_conn_mode(self.mode_combo.currentText())

        # Connect capture process signals
        self.capture_process.started.connect(self.on_capture_started)
        self.capture_process.stopped.connect(self.on_capture_stopped)
        self.capture_process.error_occurred.connect(self.on_capture_error)
        self.capture_process.stdout_received.connect(self.on_capture_stdout)
        self.capture_process.stderr_received.connect(self.on_capture_stderr)

    def setup_ui(self):
        central_widget = QWidget(self)
        self.setCentralWidget(central_widget)

        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)

        # ── Top app bar (brand + status chips + socket/Attach/settings) ──────
        main_layout.addWidget(self._build_app_bar())

        # Top Control Widget
        top_control_widget = QWidget(self)
        top_control_layout = QVBoxLayout(top_control_widget)
        top_control_layout.setContentsMargins(12, 8, 12, 8)
        top_control_layout.setSpacing(8)

        # ── Capture Setup card: mode + mode-specific fields ─────────────────
        self.conn_group = QGroupBox("Capture Setup", self)
        conn_layout = QVBoxLayout(self.conn_group)

        mode_layout = QHBoxLayout()
        mode_caption = QLabel("MODE", self)
        mode_caption.setStyleSheet("color:#6b727e; font-size:10px; font-weight:600; letter-spacing:1px;")
        mode_layout.addWidget(mode_caption)
        # mode_combo stays as the backing model/logic (and for settings + tests);
        # it is hidden in favour of the segmented pill control below.
        self.mode_combo = QComboBox(self)
        self.mode_combo.addItems(["UDP", "TCP Client", "TCP Server", "TCP Proxy", "Serial"])
        self.mode_combo.hide()
        self.mode_combo.currentIndexChanged.connect(self.on_mode_changed)
        self.mode_pills = self._make_pill_bar(
            ["UDP", "TCP Client", "TCP Server", "TCP Proxy", "Serial"],
            lambda i: self.mode_combo.setCurrentIndex(i),
        )
        mode_layout.addWidget(self.mode_pills["widget"])
        mode_layout.addStretch()
        conn_layout.addLayout(mode_layout)

        self.mode_help_label = QLabel(self)
        self.mode_help_label.setWordWrap(True)
        self.mode_help_label.setStyleSheet("color: #79818d; font-style: italic;")
        conn_layout.addWidget(self.mode_help_label)

        # Parameter Stacked Widget (mode-specific fields)
        self.param_stack = QStackedWidget(self)
        conn_layout.addWidget(self.param_stack)

        # 1. UDP Panel
        udp_widget = QWidget(self)
        udp_grid = QGridLayout(udp_widget)
        udp_grid.setContentsMargins(0, 5, 0, 5)
        udp_grid.setSpacing(6)
        udp_grid.addWidget(QLabel("Bind Host:", self), 0, 0)
        self.udp_bind_host = QLineEdit("0.0.0.0", self)
        self.udp_bind_host.textChanged.connect(self.update_config_preview)
        udp_grid.addWidget(self.udp_bind_host, 0, 1)
        udp_grid.addWidget(QLabel("Bind Port:", self), 0, 2)
        self.udp_bind_port = QLineEdit("19000", self)
        self.udp_bind_port.setValidator(QIntValidator(1, 65535, self))
        self.udp_bind_port.textChanged.connect(self.update_config_preview)
        udp_grid.addWidget(self.udp_bind_port, 0, 3)
        udp_grid.addWidget(QLabel("Target Host:", self), 1, 0)
        self.udp_target_host = QLineEdit("127.0.0.1", self)
        self.udp_target_host.textChanged.connect(self.update_config_preview)
        udp_grid.addWidget(self.udp_target_host, 1, 1)
        udp_grid.addWidget(QLabel("Target Port:", self), 1, 2)
        self.udp_target_port = QLineEdit("19085", self)
        self.udp_target_port.setValidator(QIntValidator(1, 65535, self))
        self.udp_target_port.textChanged.connect(self.update_config_preview)
        udp_grid.addWidget(self.udp_target_port, 1, 3)
        self.param_stack.addWidget(udp_widget)

        # 2. TCP Client Panel
        tcp_client_widget = QWidget(self)
        tcp_client_grid = QGridLayout(tcp_client_widget)
        tcp_client_grid.setContentsMargins(0, 5, 0, 5)
        tcp_client_grid.setSpacing(6)
        tcp_client_grid.addWidget(QLabel("Remote Host:", self), 0, 0)
        self.tcp_cli_host = QLineEdit("127.0.0.1", self)
        self.tcp_cli_host.textChanged.connect(self.update_config_preview)
        tcp_client_grid.addWidget(self.tcp_cli_host, 0, 1)
        tcp_client_grid.addWidget(QLabel("Remote Port:", self), 0, 2)
        self.tcp_cli_port = QLineEdit("19085", self)
        self.tcp_cli_port.setValidator(QIntValidator(1, 65535, self))
        self.tcp_cli_port.textChanged.connect(self.update_config_preview)
        tcp_client_grid.addWidget(self.tcp_cli_port, 0, 3)
        self.param_stack.addWidget(tcp_client_widget)

        # 3. TCP Server Panel
        tcp_server_widget = QWidget(self)
        tcp_server_grid = QGridLayout(tcp_server_widget)
        tcp_server_grid.setContentsMargins(0, 5, 0, 5)
        tcp_server_grid.setSpacing(6)
        tcp_server_grid.addWidget(QLabel("Listen Host:", self), 0, 0)
        self.tcp_srv_host = QLineEdit("0.0.0.0", self)
        self.tcp_srv_host.textChanged.connect(self.update_config_preview)
        tcp_server_grid.addWidget(self.tcp_srv_host, 0, 1)
        tcp_server_grid.addWidget(QLabel("Listen Port:", self), 0, 2)
        self.tcp_srv_port = QLineEdit("19085", self)
        self.tcp_srv_port.setValidator(QIntValidator(1, 65535, self))
        self.tcp_srv_port.textChanged.connect(self.update_config_preview)
        tcp_server_grid.addWidget(self.tcp_srv_port, 0, 3)
        self.param_stack.addWidget(tcp_server_widget)

        # 4. TCP Proxy Panel
        tcp_proxy_widget = QWidget(self)
        tcp_proxy_grid = QGridLayout(tcp_proxy_widget)
        tcp_proxy_grid.setContentsMargins(0, 5, 0, 5)
        tcp_proxy_grid.setSpacing(6)
        tcp_proxy_grid.addWidget(QLabel("Listen Host:", self), 0, 0)
        self.tcp_prx_listen_host = QLineEdit("127.0.0.1", self)
        self.tcp_prx_listen_host.textChanged.connect(self.update_config_preview)
        tcp_proxy_grid.addWidget(self.tcp_prx_listen_host, 0, 1)
        tcp_proxy_grid.addWidget(QLabel("Listen Port:", self), 0, 2)
        self.tcp_prx_listen_port = QLineEdit("19000", self)
        self.tcp_prx_listen_port.setValidator(QIntValidator(1, 65535, self))
        self.tcp_prx_listen_port.textChanged.connect(self.update_config_preview)
        tcp_proxy_grid.addWidget(self.tcp_prx_listen_port, 0, 3)
        tcp_proxy_grid.addWidget(QLabel("Target Host:", self), 1, 0)
        self.tcp_prx_target_host = QLineEdit("127.0.0.1", self)
        self.tcp_prx_target_host.textChanged.connect(self.update_config_preview)
        tcp_proxy_grid.addWidget(self.tcp_prx_target_host, 1, 1)
        tcp_proxy_grid.addWidget(QLabel("Target Port:", self), 1, 2)
        self.tcp_prx_target_port = QLineEdit("19085", self)
        self.tcp_prx_target_port.setValidator(QIntValidator(1, 65535, self))
        self.tcp_prx_target_port.textChanged.connect(self.update_config_preview)
        tcp_proxy_grid.addWidget(self.tcp_prx_target_port, 1, 3)
        self.param_stack.addWidget(tcp_proxy_widget)

        # 5. Serial Panel
        serial_widget = QWidget(self)
        serial_grid = QGridLayout(serial_widget)
        serial_grid.setContentsMargins(0, 5, 0, 5)
        serial_grid.setSpacing(6)
        serial_grid.addWidget(QLabel("Port Path:", self), 0, 0)
        self.ser_port = QComboBox(self)
        self.ser_port.setEditable(True)
        self.ser_port.addItem("/dev/ttyUSB0")
        self.ser_port.setCurrentText("/dev/ttyUSB0")
        self.ser_port.currentIndexChanged.connect(self.update_config_preview)
        self.ser_port.editTextChanged.connect(self.update_config_preview)
        serial_grid.addWidget(self.ser_port, 0, 1)
        self.ser_port_refresh_btn = QPushButton("Refresh", self)
        self.ser_port_refresh_btn.setToolTip(
            "Ask the connected engine to list available serial ports (list_serial_ports command)."
        )
        self.ser_port_refresh_btn.clicked.connect(self.refresh_serial_ports)
        serial_grid.addWidget(self.ser_port_refresh_btn, 0, 4)
        serial_grid.addWidget(QLabel("Baud Rate:", self), 0, 2)
        self.ser_baud = QComboBox(self)
        self.ser_baud.setEditable(True)
        self.ser_baud.addItems([
            "1200", "2400", "4800", "9600", "19200", "38400",
            "57600", "115200", "230400", "460800", "921600"
        ])
        self.ser_baud.setCurrentText("115200")
        self.ser_baud.currentIndexChanged.connect(self.update_config_preview)
        self.ser_baud.editTextChanged.connect(self.update_config_preview)
        serial_grid.addWidget(self.ser_baud, 0, 3)
        self.param_stack.addWidget(serial_widget)

        top_control_layout.addWidget(self.conn_group)

        # Mode-specific field inputs use a monospace face (matches the mock).
        mono_field = QFont("JetBrains Mono")
        mono_field.setStyleHint(QFont.StyleHint.Monospace)
        for w in self.param_stack.findChildren(QLineEdit):
            w.setFont(mono_field)
        for w in self.param_stack.findChildren(QComboBox):
            w.setFont(mono_field)

        # ── Frame decoder widgets (rendered inline in the actions row) ──────
        self.decoder_combo = QComboBox(self)
        self.decoder_combo.addItems(["raw", "fixed", "delimiter", "length-prefix"])
        self.decoder_combo.currentIndexChanged.connect(self.on_decoder_changed)

        self.decoder_param_stack = QStackedWidget(self)

        # 1. Raw: no frame params (hidden when selected)
        self.decoder_param_stack.addWidget(QWidget(self))

        # 2. Fixed Size
        dec_fixed_widget = QWidget(self)
        dec_fixed_layout = QHBoxLayout(dec_fixed_widget)
        dec_fixed_layout.setContentsMargins(0, 0, 0, 0)
        dec_fixed_layout.addWidget(QLabel("Frame Size:", self))
        self.dec_fixed_size = QSpinBox(self)
        self.dec_fixed_size.setRange(1, 1000000)
        self.dec_fixed_size.setValue(16)
        self.dec_fixed_size.setFont(mono_field)
        self.dec_fixed_size.valueChanged.connect(self.update_config_preview)
        dec_fixed_layout.addWidget(self.dec_fixed_size)
        dec_fixed_layout.addStretch()
        self.decoder_param_stack.addWidget(dec_fixed_widget)

        # 3. Delimiter
        dec_delim_widget = QWidget(self)
        dec_delim_layout = QHBoxLayout(dec_delim_widget)
        dec_delim_layout.setContentsMargins(0, 0, 0, 0)
        dec_delim_layout.addWidget(QLabel("Delimiter (Hex):", self))
        self.dec_delim_edit = QLineEdit("0A", self)
        self.dec_delim_edit.setPlaceholderText("e.g. 0A or 0D0A")
        self.dec_delim_edit.setFont(mono_field)
        self.dec_delim_edit.setMaximumWidth(80)
        self.dec_delim_edit.textChanged.connect(self.update_config_preview)
        dec_delim_layout.addWidget(self.dec_delim_edit)
        self.dec_delim_inc_cb = QCheckBox("Incl.", self)
        self.dec_delim_inc_cb.setChecked(False)
        self.dec_delim_inc_cb.toggled.connect(self.update_config_preview)
        dec_delim_layout.addWidget(self.dec_delim_inc_cb)
        dec_delim_layout.addStretch()
        self.decoder_param_stack.addWidget(dec_delim_widget)

        # 4. Length-Prefix
        dec_len_widget = QWidget(self)
        dec_len_layout = QHBoxLayout(dec_len_widget)
        dec_len_layout.setContentsMargins(0, 0, 0, 0)
        dec_len_layout.addWidget(QLabel("Length:", self))
        self.dec_len_size_combo = QComboBox(self)
        self.dec_len_size_combo.addItems(["1", "2", "4"])
        self.dec_len_size_combo.setCurrentText("2")
        self.dec_len_size_combo.currentIndexChanged.connect(self.update_config_preview)
        dec_len_layout.addWidget(self.dec_len_size_combo)

        self.dec_len_endian_combo = QComboBox(self)
        self.dec_len_endian_combo.addItems(["big", "little"])
        self.dec_len_endian_combo.setCurrentText("big")
        self.dec_len_endian_combo.currentIndexChanged.connect(self.update_config_preview)
        dec_len_layout.addWidget(self.dec_len_endian_combo)

        self.dec_len_inc_hdr_cb = QCheckBox("Hdr", self)
        self.dec_len_inc_hdr_cb.setChecked(False)
        self.dec_len_inc_hdr_cb.toggled.connect(self.update_config_preview)
        dec_len_layout.addWidget(self.dec_len_inc_hdr_cb)
        dec_len_layout.addStretch()
        self.decoder_param_stack.addWidget(dec_len_widget)
        self.decoder_param_stack.setVisible(False)  # raw is the default: no params

        # ── Record-to-JSONL widgets (opt-in; rendered inline in the actions row) ──
        self.log_file_cb = QCheckBox("Record to JSONL", self)
        self.log_file_cb.setChecked(False)
        self.log_file_cb.toggled.connect(self._on_log_file_toggled)
        self.log_file_cb.toggled.connect(self.update_config_preview)
        self.log_file_edit = QLineEdit("capture.jsonl", self)
        self.log_file_edit.setEnabled(False)
        self.log_file_edit.setMaximumWidth(150)
        self.log_file_edit.textChanged.connect(self.update_config_preview)
        self.browse_log_btn = QPushButton("Browse", self)
        self.browse_log_btn.setEnabled(False)
        self.browse_log_btn.clicked.connect(self.browse_log_path)

        # ── Actions row: capture controls + decoder + record ────────────────
        action_btn_layout = QHBoxLayout()
        self.start_capture_btn = QPushButton("▶ Start Capture", self)
        self.start_capture_btn.setObjectName("start_capture_btn")
        self.start_capture_btn.clicked.connect(self.start_capture)
        action_btn_layout.addWidget(self.start_capture_btn)

        self.stop_capture_btn = QPushButton("■ Stop", self)
        self.stop_capture_btn.setObjectName("stop_capture_btn")
        self.stop_capture_btn.setEnabled(False)
        self.stop_capture_btn.clicked.connect(self.stop_capture)
        action_btn_layout.addWidget(self.stop_capture_btn)

        self.pause_btn = QPushButton("Pause", self)
        self.pause_btn.clicked.connect(self.toggle_pause)
        action_btn_layout.addWidget(self.pause_btn)

        self.clear_btn = QPushButton("Clear", self)
        self.clear_btn.clicked.connect(self.clear_all)
        action_btn_layout.addWidget(self.clear_btn)

        action_btn_layout.addWidget(self._vsep())
        dec_caption = QLabel("Decoder:", self)
        dec_caption.setStyleSheet("color:#88909c; font-weight:600;")
        action_btn_layout.addWidget(dec_caption)
        action_btn_layout.addWidget(self.decoder_combo)
        action_btn_layout.addWidget(self.decoder_param_stack)

        action_btn_layout.addStretch()
        action_btn_layout.addWidget(self.log_file_cb)
        action_btn_layout.addWidget(self.log_file_edit)
        action_btn_layout.addWidget(self.browse_log_btn)
        top_control_layout.addLayout(action_btn_layout)

        # Advanced/developer-facing fields (engine executable path, IPC socket path,
        # generated config preview) live in a separate Settings dialog rather than
        # always-on-screen - see open_settings_dialog(). The widgets themselves stay
        # attributes of MainWindow so the rest of the class can keep using them
        # unchanged; only where they're placed in the layout changes.
        self.cli_path_edit = QLineEdit(self)
        self.browse_cli_btn = QPushButton("Browse", self)
        self.browse_cli_btn.clicked.connect(self.browse_cli_path)
        self.socket_path_edit = QLineEdit(self.initial_socket_path, self)
        self.socket_path_edit.textChanged.connect(self.socket_path_label.setText)
        self.connect_btn = QPushButton("Attach", self)
        self.connect_btn.setToolTip("Attach to an already-running 'packet-probe engine' at this socket path.")
        self.connect_btn.clicked.connect(self.toggle_connection)
        self.config_preview_edit = QLineEdit(self)
        self.config_preview_edit.setReadOnly(True)
        self._build_settings_dialog()

        main_layout.addWidget(top_control_widget)

        # Filter Bar (direction / event type / text search over the live event table)
        filter_bar = QWidget(self)
        filter_bar.setObjectName("filter_bar")
        filter_bar.setStyleSheet(
            "QWidget#filter_bar { border-bottom: 1px solid #1e242e; }"
        )
        filter_layout = QHBoxLayout(filter_bar)
        filter_layout.setContentsMargins(14, 7, 14, 7)
        filter_layout.setSpacing(8)
        filter_caption = QLabel("FILTER", self)
        filter_caption.setStyleSheet("color:#6b727e; font-size:11px; font-weight:600; letter-spacing:1px;")
        filter_layout.addWidget(filter_caption)

        # Direction: hidden backing combo (data roles + tests) driven by pills.
        self.filter_direction_combo = QComboBox(self)
        self.filter_direction_combo.addItem("All Directions", "all")
        self.filter_direction_combo.addItem("App -> Device", "app_to_device")
        self.filter_direction_combo.addItem("Device -> App", "device_to_app")
        self.filter_direction_combo.hide()
        self.filter_direction_combo.currentIndexChanged.connect(self._on_filter_changed)
        self.filter_direction_combo.currentIndexChanged.connect(self._sync_dir_pills)
        self.dir_pills = self._make_pill_bar(
            ["All", "App → Dev", "Dev → App"],
            lambda i: self.filter_direction_combo.setCurrentIndex(i),
        )
        filter_layout.addWidget(self.dir_pills["widget"])

        self.filter_type_combo = QComboBox(self)
        self.filter_type_combo.addItem("All Types", "all")
        for event_type in ("raw_bytes", "frame", "latency", "error", "state_change"):
            self.filter_type_combo.addItem(event_type, event_type)
        self.filter_type_combo.currentIndexChanged.connect(self._on_filter_changed)
        filter_layout.addWidget(self.filter_type_combo)

        self.filter_text_edit = QLineEdit(self)
        self.filter_text_edit.setPlaceholderText("Search summary / hex…")
        self.filter_text_edit.setMaximumWidth(340)
        self.filter_text_edit.textChanged.connect(self._on_filter_changed)
        filter_layout.addWidget(self.filter_text_edit)
        filter_layout.addStretch()

        self.filter_count_label = QLabel("0 / 0 events", self)
        self.filter_count_label.setStyleSheet("color:#5d646f; font-size:11px;")
        filter_layout.addWidget(self.filter_count_label)
        main_layout.addWidget(filter_bar)

        # Main Splitter
        main_splitter = QSplitter(Qt.Orientation.Vertical, self)
        main_layout.addWidget(main_splitter)

        self.table_model = PacketTableModel(self)
        self.filter_proxy = PacketFilterProxyModel(self)
        self.filter_proxy.setSourceModel(self.table_model)
        self.table_view = QTableView(self)
        self.table_view.setModel(self.filter_proxy)
        self.table_view.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Interactive)
        self.table_view.horizontalHeader().setStretchLastSection(True)
        self.table_view.setSelectionBehavior(QTableView.SelectionBehavior.SelectRows)
        self.table_view.setSelectionMode(QTableView.SelectionMode.SingleSelection)
        self.table_view.selectionModel().selectionChanged.connect(self.on_selection_changed)
        # Direction column rendered as a filled chip badge (matches the mock).
        self.table_view.setItemDelegateForColumn(3, DirectionChipDelegate(self.table_view))
        main_splitter.addWidget(self.table_view)

        # Send Panel
        self.send_group = QGroupBox("SEND", self)
        self.send_group.setEnabled(False)
        send_v = QVBoxLayout(self.send_group)
        send_v.setSpacing(8)

        send_row = QHBoxLayout()
        # Hidden backing radios (settings + tests + send logic) driven by pills.
        self.text_radio = QRadioButton("Text", self)
        self.text_radio.setChecked(True)
        self.text_radio.hide()
        self.text_radio.toggled.connect(self.on_send_format_changed)
        self.hex_radio = QRadioButton("Hex", self)
        self.hex_radio.hide()
        self.fmt_pills = self._make_pill_bar(
            ["Text", "Hex"],
            lambda i: (self.hex_radio if i else self.text_radio).setChecked(True),
        )
        send_row.addWidget(self.fmt_pills["widget"])

        self.send_input = QLineEdit(self)
        self.send_input.setPlaceholderText("Type message to send…")
        self.send_input.setFont(mono_field)
        self.send_input.returnPressed.connect(self.send_data)
        send_row.addWidget(self.send_input)

        self.eol_combo = QComboBox(self)
        self.eol_combo.addItems(["EOL: None", "EOL: LF", "EOL: CR", "EOL: CRLF"])
        self.eol_combo.setCurrentIndex(0)
        send_row.addWidget(self.eol_combo)

        self.send_btn = QPushButton("Send", self)
        self.send_btn.setObjectName("send_btn")
        self.send_btn.clicked.connect(self.send_data)
        send_row.addWidget(self.send_btn)

        self.send_feedback_label = QLabel(self)
        send_row.addWidget(self.send_feedback_label)
        send_v.addLayout(send_row)

        # Macros row: quick-send buttons for saved payloads (session-scoped).
        self.macro_widget = QWidget(self)
        self._macro_layout = QHBoxLayout(self.macro_widget)
        self._macro_layout.setContentsMargins(0, 0, 0, 0)
        self._macro_layout.setSpacing(7)
        send_v.addWidget(self.macro_widget)
        self._rebuild_macros()

        # Bottom Detail Tabs
        self.detail_tabs = QTabWidget(self)

        self.hex_view = HexView(self)

        self.text_view = QPlainTextEdit(self)
        self.text_view.setReadOnly(True)
        text_font = QFont("Courier New", 10)
        text_font.setStyleHint(QFont.StyleHint.Monospace)
        self.text_view.setFont(text_font)
        self.text_view.setPlaceholderText("Text View")

        self.detail_view = EventDetailView(self)
        self.process_output = QPlainTextEdit(self)
        self.process_output.setReadOnly(True)

        self.detail_tabs.addTab(self.hex_view, "Hex")
        self.detail_tabs.addTab(self.text_view, "Text")
        self.detail_tabs.addTab(self.detail_view, "JSON")
        self.detail_tabs.addTab(self.process_output, "Process Log")

        main_splitter.addWidget(self.send_group)
        main_splitter.addWidget(self.detail_tabs)

        main_splitter.setSizes([450, 75, 275])

        # Bottom status bar: event/error counters (left) and the latest message
        # (right). The connection/state/mode/port chips live in the top app bar.
        status_bar = self.statusBar()
        self.event_count_label = QLabel("Events: 0", self)
        status_bar.addWidget(self.event_count_label)
        self.error_count_label = QLabel("Errors: 0", self)
        status_bar.addWidget(self.error_count_label)
        self.message_label = QLabel("", self)
        status_bar.addPermanentWidget(self.message_label)

    # ── Design chrome: app bar, segmented pills, theming, macros ───────────

    def _vsep(self) -> QFrame:
        sep = QFrame(self)
        sep.setFrameShape(QFrame.Shape.VLine)
        sep.setFixedHeight(22)
        sep.setStyleSheet("color:#1e242e; background-color:#1e242e; max-width:1px;")
        return sep

    def _build_app_bar(self) -> QWidget:
        bar = QWidget(self)
        bar.setObjectName("app_bar")
        bar.setFixedHeight(52)
        bar.setStyleSheet(
            "QWidget#app_bar { background-color:#090d14; border-bottom:1px solid #1e242e; }"
        )
        lay = QHBoxLayout(bar)
        lay.setContentsMargins(14, 0, 14, 0)
        lay.setSpacing(10)

        # Brand: accent logo tile + name + version badge
        self.brand_logo = QLabel(bar)
        self.brand_logo.setFixedSize(22, 22)
        self.brand_logo.setStyleSheet(f"background-color:{self._accent}; border-radius:6px;")
        lay.addWidget(self.brand_logo)
        title = QLabel("Packet Probe", bar)
        title.setStyleSheet("color:#e5e8ed; font-size:14px; font-weight:700;")
        lay.addWidget(title)
        version = QLabel("v0.4", bar)
        version.setStyleSheet(
            "background-color:#131921; color:#6b727e; font-family:'JetBrains Mono',monospace;"
            " font-size:10px; padding:2px 6px; border-radius:4px;"
        )
        lay.addWidget(version)

        lay.addWidget(self._vsep())

        # Connection / state / mode / port chips (styled by the update_* helpers)
        self.status_label = QLabel(bar)
        lay.addWidget(self.status_label)
        self.state_label = QLabel(bar)
        lay.addWidget(self.state_label)
        self.conn_mode_label = QLabel(bar)
        lay.addWidget(self.conn_mode_label)
        self.port_label = QLabel(bar)
        lay.addWidget(self.port_label)

        lay.addStretch()

        self.socket_path_label = QLabel(self.initial_socket_path, bar)
        self.socket_path_label.setStyleSheet(
            "color:#6b727e; font-family:'JetBrains Mono',monospace; font-size:11px;"
        )
        lay.addWidget(self.socket_path_label)

        self.attach_btn = QPushButton("Attach", bar)
        self.attach_btn.setToolTip(
            "Attach to / detach from an already-running 'packet-probe engine' at the configured socket path."
        )
        self.attach_btn.clicked.connect(self.toggle_connection)
        lay.addWidget(self.attach_btn)

        self.toggle_settings_btn = QPushButton("⚙", bar)
        self.toggle_settings_btn.setToolTip("Settings")
        self.toggle_settings_btn.setFixedWidth(34)
        self.toggle_settings_btn.clicked.connect(self.open_settings_dialog)
        lay.addWidget(self.toggle_settings_btn)
        return bar

    def _make_pill_bar(self, labels: list[str], on_click) -> dict:
        """Segmented pill control (Mode / Filter direction / Send format). The
        pills are the visible surface; a hidden combo/radio remains the source of
        truth for logic, settings, and tests."""
        track = QWidget(self)
        track.setObjectName("pill_track")
        track.setStyleSheet(_PILL_TRACK_QSS)
        row = QHBoxLayout(track)
        row.setContentsMargins(3, 3, 3, 3)
        row.setSpacing(3)
        group = QButtonGroup(track)
        group.setExclusive(True)
        buttons: list[QPushButton] = []
        for i, label in enumerate(labels):
            btn = QPushButton(label, track)
            btn.setCheckable(True)
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setStyleSheet(_pill_qss(i == 0, self._accent))
            group.addButton(btn, i)
            btn.clicked.connect(lambda _checked=False, idx=i: on_click(idx))
            row.addWidget(btn)
            buttons.append(btn)
        buttons[0].setChecked(True)
        return {"widget": track, "buttons": buttons, "group": group}

    def _restyle_pills(self, pills: dict, active_index: int) -> None:
        for i, btn in enumerate(pills["buttons"]):
            active = i == active_index
            btn.setChecked(active)
            btn.setStyleSheet(_pill_qss(active, self._accent))

    def _apply_theme(self) -> None:
        self.setStyleSheet(DARK_THEME_QSS + _accent_overrides(self._accent, self._accent_hover))

    def _sync_mode_visuals(self) -> None:
        # Called whenever the selected transport changes: swap the accent and
        # restyle every accent-dependent surface (pills, brand tile, theme).
        mode = self.mode_combo.currentText()
        self._accent, self._accent_hover = MODE_ACCENTS.get(mode, MODE_ACCENTS["UDP"])
        self._apply_theme()
        if hasattr(self, "brand_logo"):
            self.brand_logo.setStyleSheet(f"background-color:{self._accent}; border-radius:6px;")
        self._restyle_pills(self.mode_pills, self.mode_combo.currentIndex())
        self._restyle_pills(self.dir_pills, self.filter_direction_combo.currentIndex())
        self._restyle_pills(self.fmt_pills, 1 if self.hex_radio.isChecked() else 0)

    def _sync_dir_pills(self) -> None:
        self._restyle_pills(self.dir_pills, self.filter_direction_combo.currentIndex())

    def _update_filter_count(self) -> None:
        total = self.table_model.rowCount()
        shown = self.filter_proxy.rowCount()
        self.filter_count_label.setText(f"{shown} / {total} events")

    # ── Send macros (session-scoped quick-send buttons) ────────────────────

    def _rebuild_macros(self) -> None:
        while self._macro_layout.count():
            item = self._macro_layout.takeAt(0)
            w = item.widget()
            if w is not None:
                w.deleteLater()

        caption = QLabel("MACROS", self.macro_widget)
        caption.setStyleSheet("color:#5d646f; font-size:10px; font-weight:600; letter-spacing:1px;")
        self._macro_layout.addWidget(caption)

        for macro in self._macros:
            run = QPushButton(macro["name"], self.macro_widget)
            fmt = "HEX" if macro["is_hex"] else "TEXT"
            run.setToolTip(f"{fmt}: {macro['data']}")
            run.clicked.connect(lambda _c=False, m=macro: self._run_macro(m))
            self._macro_layout.addWidget(run)
            remove = QPushButton("×", self.macro_widget)
            remove.setFixedWidth(24)
            remove.setToolTip("Delete macro")
            remove.clicked.connect(lambda _c=False, m=macro: self._remove_macro(m))
            self._macro_layout.addWidget(remove)

        add = QPushButton("+ Save as macro", self.macro_widget)
        add.setStyleSheet(
            "QPushButton { background:transparent; border:1px dashed #3a424e; color:#88909c;"
            " border-radius:6px; padding:5px 11px; font-weight:600; }"
            " QPushButton:hover { color:#d4d8de; border-color:#5d646f; }"
        )
        add.clicked.connect(self._add_macro_from_input)
        self._macro_layout.addWidget(add)
        self._macro_layout.addStretch()

    def _run_macro(self, macro: dict) -> None:
        (self.hex_radio if macro["is_hex"] else self.text_radio).setChecked(True)
        self.eol_combo.setCurrentIndex(macro["eol_index"])
        self.send_input.setText(macro["data"])
        self.send_data()

    def _remove_macro(self, macro: dict) -> None:
        if macro in self._macros:
            self._macros.remove(macro)
        self._rebuild_macros()

    def _add_macro_from_input(self) -> None:
        data = self.send_input.text().strip()
        if not data:
            QMessageBox.information(
                self, "Save Macro", "Type a message in the Send box first, then save it as a macro."
            )
            return
        name, ok = QInputDialog.getText(self, "Save Macro", "Macro name:")
        if not ok or not name.strip():
            return
        self._macros.append({
            "name": name.strip(),
            "is_hex": self.hex_radio.isChecked(),
            "data": data,
            "eol_index": self.eol_combo.currentIndex(),
        })
        self._rebuild_macros()

    # ── Connection management ──────────────────────────────────────────────

    def toggle_connection(self):
        if self.worker and self.worker.isRunning():
            self.disconnect_socket()
        else:
            self.connect_socket()

    def connect_socket(self):
        socket_path = self.socket_path_edit.text().strip()
        if not socket_path:
            QMessageBox.warning(self, "Warning", "Socket path is empty.")
            return

        self.connect_btn.setEnabled(False)
        self.connect_btn.setText("Connecting...")
        self.socket_path_edit.setEnabled(False)
        self._set_message("")

        self.worker = IpcClientWorker(socket_path, self)
        self.worker.status_changed.connect(self.on_status_changed)
        self.worker.error_occurred.connect(self.on_error_occurred)
        self.worker.event_received.connect(self.on_event_received)
        self.worker.metadata_received.connect(self.on_metadata_received)
        self.worker.result_received.connect(self.on_result_received)
        self.worker.status_received.connect(self.on_status_received)
        self.worker.disconnected.connect(self.on_worker_finished)
        self.worker.wirestead_unavailable.connect(self.on_wirestead_unavailable)
        self.worker.start()

    def disconnect_socket(self):
        self._ipc_connected = False
        self._pending_commands.clear()
        if self._ipc_connector:
            self._ipc_connector.cancel()
            self._ipc_connector = None
        if self.worker:
            worker = self.worker
            self.worker = None
            worker.stop()

    def on_status_changed(self, status: str):
        if status == "connecting":
            self._ipc_connected = False
            self.connect_btn.setEnabled(False)
            self.connect_btn.setText("Connecting...")
            self._set_message("")
            self.update_status("Status: connecting")
        elif status == "connected":
            self._ipc_connected = True
            self.connect_btn.setEnabled(True)
            self.connect_btn.setText("Detach")
            self.socket_path_edit.setEnabled(False)
            self.clear_all()
            if self._starting_capture and self._pending_config_to_start is not None:
                # We connected as part of our own Start Capture request - chain
                # straight into configure/start_capture instead of settling on "live".
                config = self._pending_config_to_start
                self._pending_config_to_start = None
                self._set_message("Connected to engine, configuring...")
                self._configure_and_start(config)
            else:
                # A plain Attach (or a reconnect): sync UI with whatever the engine
                # is actually doing rather than assuming idle.
                self._set_message("Connected to engine")
                self._request_status_sync()
        elif status == "disconnected":
            self._ipc_connected = False
            self.connect_btn.setEnabled(True)
            self.connect_btn.setText("Attach")
            self.socket_path_edit.setEnabled(True)
            self._pending_commands.clear()
            if not self.capture_process.is_running():
                self._engine_state = "idle"
                self._starting_capture = False
                self._set_capture_controls_running(False)
                self.set_mode("idle")
                self.update_status("Status: disconnected")

        # Keep the app-bar Attach button mirrored with the settings-dialog one.
        self.attach_btn.setText(self.connect_btn.text().replace("...", "…"))
        self.attach_btn.setEnabled(self.connect_btn.isEnabled())

    def on_error_occurred(self, error_msg: str):
        self._set_message(f"Error: {error_msg}", is_error=True)
        self.update_status("Status: failed")
        self._bump_error_count()

    def _bump_error_count(self):
        self._error_count += 1
        self.error_count_label.setText(f"Errors: {self._error_count}")

    def on_wirestead_unavailable(self, message: str):
        # Unlike a transient IPC error, this never resolves on its own: the CLI process
        # (if any) keeps running, but the live event table will stay empty for the rest
        # of this session. Make that permanent, non-retryable state hard to miss.
        # (The status badge itself will settle back to the neutral "disconnected" style a
        # moment later via the worker's own status_changed/disconnected signals - the
        # persistent signal here is the message banner, dialog, and state label below.)
        self._set_message(f"Error: {message}", is_error=True)
        self.set_mode("no live view")
        QMessageBox.critical(
            self,
            "Live View Unavailable",
            "wirestead-python is not installed or failed to import, so the viewer cannot "
            "subscribe to the live IPC event stream:\n\n"
            f"{message}\n\n"
            "The CLI process (if running) is unaffected and will keep capturing/recording, "
            "but no events will appear in this table until wirestead-python is installed and "
            "the viewer reconnects."
        )

    def on_metadata_received(self, metadata: dict):
        self._set_message(format_metadata_message(metadata))

    def on_event_received(self, event_dict: dict):
        event = PacketEvent(event_dict)
        self._event_count += 1
        self.event_count_label.setText(f"Events: {self._event_count}")
        if event.type == "error":
            self._bump_error_count()
        if self.is_paused:
            self.pending_events.append(event)
        else:
            self.table_model.append_event(event)
            self.table_view.scrollToBottom()
        self._update_filter_count()

    def on_worker_finished(self):
        self.on_status_changed("disconnected")
        self.worker = None
        if not self.capture_process.is_running():
            self.set_mode("idle")

    # ── Status indicator helpers ───────────────────────────────────────────

    def _set_message(self, text: str, is_error: bool = False):
        self.message_label.setText(text)
        self.message_label.setStyleSheet("color: #ff645f; font-weight: 600;" if is_error else "")

    def update_status(self, text: str):
        self.status_label.setText(text)
        lower_text = text.lower()
        # "disconnected" and "failed"/"error" must be checked before the generic
        # "connected"/"running" match below, since "disconnected" contains "connected"
        # as a substring and would otherwise get the same "success" styling.
        if "disconnected" in lower_text:
            self.status_label.setStyleSheet(_chip_qss("#151b24", "#6b727e", "#232933"))
        elif "failed" in lower_text or "error" in lower_text:
            self.status_label.setStyleSheet(_chip_qss("#3a0f0e", "#ff645f", "#7a1f1c"))
        elif "connecting" in lower_text or "launching" in lower_text:
            self.status_label.setStyleSheet(_chip_qss("#3e2a00", "#eab532", "#6b4a00"))
        elif "offline" in lower_text:
            self.status_label.setStyleSheet(_chip_qss("#10141b", "#7e8ef4", "#2a3350"))
        elif "connected" in lower_text or "running" in lower_text or "capturing" in lower_text:
            self.status_label.setStyleSheet(_chip_qss("#063215", "#67d283", "#0d5a26"))
        else:
            self.status_label.setStyleSheet(_chip_qss("#151b24", "#6b727e", "#232933"))

    def set_mode(self, mode: str):
        self.state_label.setText(f"State: {mode}")
        lower_mode = mode.lower()
        if lower_mode == "live":
            self.state_label.setStyleSheet(_chip_qss("#063215", "#67d283", "#0d5a26"))
        elif lower_mode == "launcher":
            self.state_label.setStyleSheet(_chip_qss("#3e2a00", "#eab532", "#6b4a00"))
        elif lower_mode == "offline":
            self.state_label.setStyleSheet(_chip_qss("#10141b", "#7e8ef4", "#2a3350"))
        elif lower_mode == "no live view":
            self.state_label.setStyleSheet(_chip_qss("#3a0f0e", "#ff645f", "#7a1f1c"))
        else:  # "idle"
            self.state_label.setStyleSheet(_chip_qss("#151b24", "#6b727e", "#232933"))

    def update_port(self, port_str: str):
        self.port_label.setText(f"Port: {port_str}")
        if port_str == "-":
            self.port_label.setStyleSheet(_chip_qss("#030509", "#5d646f", "#10141b"))
        else:
            self.port_label.setStyleSheet(_chip_qss("#131921", "#979fab", "#232933"))

    def update_conn_mode(self, mode: str):
        self.conn_mode_label.setText(f"Mode: {mode}")
        lower_mode = mode.lower()
        # Design maps each transport to a distinct accent, shown as accent-colored
        # text on a neutral chip (see MODE_COLORS in the mock).
        if lower_mode == "udp":
            self.conn_mode_label.setStyleSheet(_chip_qss("#1a2029", "#2cb3b3", "#232933"))
        elif lower_mode == "tcp client":
            self.conn_mode_label.setStyleSheet(_chip_qss("#1a2029", "#4ba3f7", "#232933"))
        elif lower_mode == "tcp server":
            self.conn_mode_label.setStyleSheet(_chip_qss("#1a2029", "#7e8ef4", "#232933"))
        elif lower_mode == "tcp proxy":
            self.conn_mode_label.setStyleSheet(_chip_qss("#1a2029", "#ae7ee2", "#232933"))
        elif lower_mode == "serial":
            self.conn_mode_label.setStyleSheet(_chip_qss("#1a2029", "#eb883b", "#232933"))
        else:
            self.conn_mode_label.setStyleSheet(_chip_qss("#151b24", "#6b727e", "#232933"))

    # ── Capture lifecycle ──────────────────────────────────────────────────

    # Capture modes where the CLI accepts no send input at all (no stdin loop,
    # no IPC command handler) - see run_tcp_proxy.cpp.
    _SEND_UNSUPPORTED_MODES = ("TCP Proxy",)

    def _refresh_send_group_enabled(self, active: bool):
        send_capable = active and self._active_capture_mode not in self._SEND_UNSUPPORTED_MODES
        self.send_group.setEnabled(send_capable)
        if active and not send_capable:
            self.send_group.setToolTip(
                f"{self._active_capture_mode} mode forwards traffic between an existing client and "
                "target; it does not support sending messages from the viewer."
            )
        elif not active:
            self.send_group.setToolTip("Start a capture session before sending.")
        else:
            self.send_group.setToolTip("")
        self.send_feedback_label.clear()

    def _show_send_feedback(self, ok: bool, error: str):
        if ok:
            self.send_feedback_label.setText("✓ Sent")
            self.send_feedback_label.setStyleSheet("color: #5dc879; font-weight: 600;")
        else:
            self.send_feedback_label.setText(f"✗ {error or 'Send failed'}")
            self.send_feedback_label.setStyleSheet("color: #ff645f; font-weight: 600;")
        QTimer.singleShot(4000, self.send_feedback_label.clear)

    def _set_capture_controls_running(self, running: bool):
        self.start_capture_btn.setEnabled(not running and not self._starting_capture)
        self.stop_capture_btn.setEnabled(running)
        self.conn_group.setEnabled(not running)
        self._refresh_send_group_enabled(running)

    def _restore_capture_controls(self):
        self._starting_capture = False
        self._set_capture_controls_running(False)

    # ── Engine control protocol v2 (configure/start_capture/stop_capture) ──

    def _configure_and_start(self, config: dict):
        cmd_id = self.worker.send_command({"type": "command", "command": "configure", "config": config})
        self._pending_commands[cmd_id] = "configure"

    def _request_status_sync(self):
        if self.worker is None:
            return
        cmd_id = self.worker.send_command({"type": "command", "command": "get_status"})
        self._pending_commands[cmd_id] = "get_status"

    def _apply_engine_state(self, engine_state: str):
        self._engine_state = engine_state
        capturing = engine_state == "capturing"
        self._set_capture_controls_running(capturing)
        self.set_mode("live" if capturing else "idle")
        self.update_status("Status: capturing" if capturing else "Status: connected")

    def on_result_received(self, result: dict):
        cmd_id = result.get("id", "")
        intent = self._pending_commands.pop(cmd_id, None)
        ok = bool(result.get("ok", False))
        error = result.get("error", "")

        if intent == "configure":
            if ok:
                self._set_message("Configured, starting capture...")
                start_id = self.worker.send_command({"type": "command", "command": "start_capture"})
                self._pending_commands[start_id] = "start_capture"
            else:
                self._restore_capture_controls()
                self._set_message(f"Error: configure failed: {error}", is_error=True)
                QMessageBox.warning(self, "Configure Failed", error or "Unknown error")
        elif intent == "start_capture":
            self._starting_capture = False
            if ok:
                self._apply_engine_state("capturing")
                self._set_message("Capture started")
            else:
                self._set_capture_controls_running(False)
                self._set_message(f"Error: start_capture failed: {error}", is_error=True)
                QMessageBox.warning(self, "Start Capture Failed", error or "Unknown error")
        elif intent == "stop_capture":
            self._apply_engine_state("idle" if ok else self._engine_state)
            if ok:
                self._set_message("Capture stopped")
            else:
                self._set_message(f"Error: stop_capture failed: {error}", is_error=True)
        elif intent == "get_status":
            if ok:
                self._apply_engine_state(result.get("engine_state", "idle"))
            self._set_message("" if ok else f"Error: get_status failed: {error}", is_error=not ok)
        elif intent == "send":
            self._set_message("Sent" if ok else f"Error: send failed: {error}", is_error=not ok)
            self._show_send_feedback(ok, error)
        elif intent == "list_serial_ports":
            self.ser_port_refresh_btn.setEnabled(True)
            if ok:
                current = self.ser_port.currentText()
                ports = result.get("ports", [])
                self.ser_port.clear()
                self.ser_port.addItems(ports)
                if current:
                    self.ser_port.setCurrentText(current)
                self._set_message(f"Found {len(ports)} serial port(s)" if ports else "No serial ports found")
            else:
                self._set_message(f"Error: list_serial_ports failed: {error}", is_error=True)

    def refresh_serial_ports(self):
        if not (self._ipc_connected and self.worker is not None):
            QMessageBox.warning(
                self, "Warning", "Not connected to an engine yet - click Start Capture or Attach first."
            )
            return
        self.ser_port_refresh_btn.setEnabled(False)
        cmd_id = self.worker.send_command({"type": "command", "command": "list_serial_ports"})
        self._pending_commands[cmd_id] = "list_serial_ports"

    def on_status_received(self, status: dict):
        # Broadcast to all clients on every engine state change (see docs/ipc-protocol.md).
        # Ignore it while we have our own configure/start_capture in flight so we don't
        # show a stale state in between our own request and its result.
        if self._starting_capture:
            return
        self._apply_engine_state(status.get("engine_state", self._engine_state))

    # ── Capture lifecycle ────────────────────────────────────────────────

    def start_capture(self):
        try:
            config = self._collect_capture_config()
        except ValueError as exc:
            QMessageBox.warning(self, "Warning", str(exc))
            return

        self._active_capture_mode = self.mode_combo.currentText()
        self._starting_capture = True
        self._set_capture_controls_running(False)
        self.set_mode("launcher")
        self.update_status("Status: launching")

        if self._ipc_connected and self.worker is not None:
            self._configure_and_start(config)
            return

        self._pending_config_to_start = config

        if self.capture_process.is_running():
            # Engine process is already up but we're not connected - just (re)connect;
            # on_status_changed("connected") will pick up _pending_config_to_start.
            self.connect_socket()
            return

        executable = self.cli_path_edit.text().strip()
        if not executable:
            QMessageBox.warning(self, "Warning", "CLI Path is empty.")
            self._restore_capture_controls()
            return

        resolved_path = shutil.which(executable)
        if not resolved_path:
            QMessageBox.warning(self, "Warning", f"CLI executable not found or not executable:\n{executable}")
            self._restore_capture_controls()
            return

        self.generated_ipc_path = make_default_ipc_path()
        self.socket_path_edit.setText(self.generated_ipc_path)

        self.clear_all()
        self.process_output.clear()

        # Remove stale socket before launch
        try:
            Path(self.generated_ipc_path).unlink(missing_ok=True)
        except Exception:
            pass

        self.process_output.appendPlainText(f"[system] Starting engine: {executable} engine --ipc {self.generated_ipc_path}")

        try:
            self.capture_process.start(executable, ["engine", "--ipc", self.generated_ipc_path])
        except Exception as exc:
            self.process_output.appendPlainText(f"[system] Failed to start process: {exc}")
            self.on_capture_stopped(-1, "FailedToStart")

    def stop_capture(self):
        if self._ipc_connected and self.worker is not None:
            self.stop_capture_btn.setEnabled(False)
            cmd_id = self.worker.send_command({"type": "command", "command": "stop_capture"})
            self._pending_commands[cmd_id] = "stop_capture"
            return

        # No IPC connection to send stop_capture over (e.g. the engine hung before
        # ever accepting a connection) - fall back to killing the process outright.
        if self._ipc_connector:
            self._ipc_connector.cancel()
            self._ipc_connector = None
        self.process_output.appendPlainText("[system] Stopping engine process...")
        self.capture_process.stop()
        self._restore_capture_controls()

    def on_capture_started(self):
        self.process_output.appendPlainText("[system] Engine process started successfully.")
        self._ipc_connector = IpcConnector(
            self.generated_ipc_path,
            max_attempts=30,
            interval_ms=100,
            parent=self,
        )
        self._ipc_connector.ready.connect(self._on_ipc_socket_ready)
        self._ipc_connector.failed.connect(self._on_ipc_socket_failed)
        self._ipc_connector.start()

    def _on_ipc_socket_ready(self):
        self._ipc_connector = None
        self.connect_socket()

    def _on_ipc_socket_failed(self, reason: str):
        self._ipc_connector = None
        self._pending_config_to_start = None
        self._set_message(f"Error: {reason}", is_error=True)
        self.stop_capture()
        self._restore_capture_controls()
        self.set_mode("idle")
        self.update_status("Status: failed")

    def on_capture_stopped(self, exit_code, exit_status):
        if self._ipc_connector:
            self._ipc_connector.cancel()
            self._ipc_connector = None
        self._pending_config_to_start = None
        self.process_output.appendPlainText(f"[system] Engine process stopped. Exit code: {exit_code} ({exit_status})")
        self._restore_capture_controls()
        self.disconnect_socket()
        self.set_mode("idle")
        self.update_status("Status: disconnected")

    def on_capture_error(self, msg):
        self.process_output.appendPlainText(f"[stderr] Process error: {msg}")
        if not self.capture_process.is_running():
            if self._ipc_connector:
                self._ipc_connector.cancel()
                self._ipc_connector = None
            self._pending_config_to_start = None
            self._restore_capture_controls()
            self.disconnect_socket()
            self.set_mode("idle")
            self.update_status("Status: disconnected")

    def on_capture_stdout(self, data):
        text = data.rstrip()
        if text:
            self.process_output.appendPlainText(f"[stdout] {text}")

    def on_capture_stderr(self, data):
        text = data.rstrip()
        if text:
            self.process_output.appendPlainText(f"[stderr] {text}")

    # ── UI actions ────────────────────────────────────────────────────────

    def toggle_pause(self):
        if self.is_paused:
            self.is_paused = False
            self.pause_btn.setText("Pause")
            for event in self.pending_events:
                self.table_model.append_event(event)
            self.pending_events.clear()
            self.table_view.scrollToBottom()
        else:
            self.is_paused = True
            self.pause_btn.setText("Resume")

    def clear_all(self):
        self.table_model.clear()
        self.pending_events.clear()
        self.hex_view.clear()
        self.text_view.clear()
        self.detail_view.clear()
        self._event_count = 0
        self._error_count = 0
        self.event_count_label.setText("Events: 0")
        self.error_count_label.setText("Errors: 0")
        self._update_filter_count()

    def _on_filter_changed(self):
        self.filter_proxy.set_direction_filter(self.filter_direction_combo.currentData())
        self.filter_proxy.set_type_filter(self.filter_type_combo.currentData())
        self.filter_proxy.set_text_filter(self.filter_text_edit.text())
        self._update_filter_count()

    def on_selection_changed(self, selected: QItemSelection, deselected: QItemSelection):
        indexes = selected.indexes()
        if not indexes:
            self.hex_view.clear()
            self.text_view.clear()
            self.detail_view.clear()
            return

        source_index = self.filter_proxy.mapToSource(indexes[0])
        event = self.table_model.event_at(source_index.row())
        if event:
            self.hex_view.set_payload_hex(event.payload_hex)
            self.detail_view.set_event(event)

            if event.payload_hex:
                clean_hex = re.sub(r'[\s:\-]', '', event.payload_hex)
                try:
                    raw_bytes = bytes.fromhex(clean_hex)
                    chars = []
                    for b in raw_bytes:
                        if 32 <= b <= 126 or b == 10 or b == 13 or b == 9:
                            chars.append(chr(b))
                        else:
                            chars.append(".")
                    self.text_view.setPlainText("".join(chars))
                except Exception as e:
                    self.text_view.setPlainText(f"Failed to decode text: {e}")
            else:
                self.text_view.clear()

    def browse_cli_path(self):
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select packet-probe CLI executable",
            "",
            "Executables (*.exe *packet-probe*);;All Files (*)"
        )
        if path:
            self.cli_path_edit.setText(path)

    def open_log_file(self) -> None:
        if self.capture_process.is_running():
            reply = QMessageBox.question(
                self,
                "Capture Running",
                "Capture is running. Stop capture and open log?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No
            )
            if reply == QMessageBox.StandardButton.Yes:
                self.stop_capture()
            else:
                return

        path, _ = QFileDialog.getOpenFileName(
            self,
            "Open Packet Probe JSONL Log",
            "",
            "JSONL Files (*.jsonl);;All Files (*)",
        )
        if not path:
            return

        if self.worker and self.worker.isRunning():
            self.disconnect_socket()

        self.clear_all()

        try:
            result = load_packet_probe_jsonl(path)
        except Exception as exc:
            self._set_message(f"Error loading log: {exc}", is_error=True)
            return

        events = [PacketEvent(e) for e in result.events]
        self.table_model.set_events(events)
        self._update_filter_count()

        self.set_mode("offline")
        self.update_status("Status: offline log")

        meta_msg = format_metadata_message(result.metadata)
        filename = os.path.basename(path)
        count = len(result.events)
        malformed_count = len(result.malformed_lines)

        if malformed_count > 0:
            first_malformed_num = result.malformed_lines[0][0]
            msg = f"Loaded {count} events from {filename} with {malformed_count} malformed lines. First malformed line: {first_malformed_num}"
        else:
            msg = f"Loaded {count} events from {filename}"

        if meta_msg:
            self._set_message(f"{meta_msg}\n{msg}")
        else:
            self._set_message(msg)

    def on_send_format_changed(self):
        is_hex = self.hex_radio.isChecked()
        self.eol_combo.setEnabled(not is_hex)
        if is_hex:
            self.send_input.setPlaceholderText("Enter hex bytes (e.g. AA BB CC 00)…")
        else:
            self.send_input.setPlaceholderText("Type message to send…")
        if hasattr(self, "fmt_pills"):
            self._restyle_pills(self.fmt_pills, 1 if is_hex else 0)

    def _clean_hex_input(self, text: str) -> str | None:
        clean_hex = re.sub(r'(0x|0X|[\s:\-])', '', text).lower()
        if not clean_hex or not all(c in '0123456789abcdefABCDEF' for c in clean_hex) or len(clean_hex) % 2 != 0:
            QMessageBox.warning(
                self,
                "Invalid Hex",
                "Please enter a valid hex string (e.g. AA BB CC or AABBCC).\n"
                "Note: Each byte must have two hex digits."
            )
            return None
        return clean_hex

    def send_data(self):
        if not (self._ipc_connected and self.worker is not None):
            QMessageBox.warning(self, "Warning", "Cannot send data: not connected to the engine.")
            return
        if self._engine_state != "capturing":
            QMessageBox.warning(self, "Warning", "Cannot send data: capture is not running.")
            return

        text = self.send_input.text().strip()
        if not text:
            return

        if self.hex_radio.isChecked():
            clean_hex = self._clean_hex_input(text)
            if clean_hex is None:
                return
            payload_hex = clean_hex
        else:
            eol_idx = self.eol_combo.currentIndex()
            raw = text
            if eol_idx == 1:
                raw += "\n"
            elif eol_idx == 2:
                raw += "\r"
            elif eol_idx == 3:
                raw += "\r\n"
            try:
                payload_hex = raw.encode("utf-8").hex()
            except Exception as exc:
                QMessageBox.critical(self, "Error", f"Failed to encode text: {exc}")
                return

        cmd_id = self.worker.send_command({"type": "command", "command": "send", "payload_hex": payload_hex})
        self._pending_commands[cmd_id] = "send"
        self.send_input.clear()

    # ── Settings ──────────────────────────────────────────────────────────

    def load_settings(self):
        defaults = ViewerState(
            cli_path=find_packet_probe_binary(),
            socket_path=self.initial_socket_path,
        )
        state = self._settings_manager.load(defaults)

        self.cli_path_edit.setText(state.cli_path)
        self.socket_path_edit.setText(state.socket_path)
        if state.send_in_hex:
            self.hex_radio.setChecked(True)
        else:
            self.text_radio.setChecked(True)
        self.eol_combo.setCurrentIndex(state.eol_index)

        self.mode_combo.setCurrentIndex(state.mode_index)
        self.udp_bind_host.setText(state.udp_bind_host)
        self.udp_bind_port.setText(state.udp_bind_port)
        self.udp_target_host.setText(state.udp_target_host)
        self.udp_target_port.setText(state.udp_target_port)
        self.tcp_cli_host.setText(state.tcp_cli_host)
        self.tcp_cli_port.setText(state.tcp_cli_port)
        self.tcp_srv_host.setText(state.tcp_srv_host)
        self.tcp_srv_port.setText(state.tcp_srv_port)
        self.tcp_prx_listen_host.setText(state.tcp_prx_listen_host)
        self.tcp_prx_listen_port.setText(state.tcp_prx_listen_port)
        self.tcp_prx_target_host.setText(state.tcp_prx_target_host)
        self.tcp_prx_target_port.setText(state.tcp_prx_target_port)
        self.ser_port.setCurrentText(state.ser_port)
        self.ser_baud.setCurrentText(state.ser_baud)

        self.decoder_combo.setCurrentIndex(state.decoder_index)
        self.decoder_param_stack.setCurrentIndex(state.decoder_index)
        self.decoder_param_stack.setVisible(state.decoder_index != 0)
        self.dec_fixed_size.setValue(state.dec_fixed_size)
        self.dec_delim_edit.setText(state.dec_delim)
        self.dec_delim_inc_cb.setChecked(state.dec_delim_inc)
        self.dec_len_size_combo.setCurrentText(state.dec_len_size)
        self.dec_len_endian_combo.setCurrentText(state.dec_len_endian)
        self.dec_len_inc_hdr_cb.setChecked(state.dec_len_inc_hdr)
        self.log_file_edit.setText(state.log_file)
        self.log_file_cb.setChecked(state.log_enabled)

        # setCurrentIndex() above only fires on_mode_changed (and its help-text update)
        # if the index actually changed - call it explicitly so a saved/default mode of
        # 0 (UDP) still gets its help text populated.
        self.mode_help_label.setText(self._MODE_HELP_TEXT.get(self.mode_combo.currentText(), ""))

        self.update_config_preview()
        # Sync accent + segmented pills to the loaded mode / send format, even when
        # the index did not change (default UDP), so the pill selection is correct.
        self._sync_mode_visuals()

    def save_settings(self):
        state = ViewerState(
            cli_path=self.cli_path_edit.text().strip(),
            socket_path=self.socket_path_edit.text().strip(),
            send_in_hex=self.hex_radio.isChecked(),
            eol_index=self.eol_combo.currentIndex(),
            mode_index=self.mode_combo.currentIndex(),
            udp_bind_host=self.udp_bind_host.text().strip(),
            udp_bind_port=self.udp_bind_port.text().strip(),
            udp_target_host=self.udp_target_host.text().strip(),
            udp_target_port=self.udp_target_port.text().strip(),
            tcp_cli_host=self.tcp_cli_host.text().strip(),
            tcp_cli_port=self.tcp_cli_port.text().strip(),
            tcp_srv_host=self.tcp_srv_host.text().strip(),
            tcp_srv_port=self.tcp_srv_port.text().strip(),
            tcp_prx_listen_host=self.tcp_prx_listen_host.text().strip(),
            tcp_prx_listen_port=self.tcp_prx_listen_port.text().strip(),
            tcp_prx_target_host=self.tcp_prx_target_host.text().strip(),
            tcp_prx_target_port=self.tcp_prx_target_port.text().strip(),
            ser_port=self.ser_port.currentText().strip(),
            ser_baud=self.ser_baud.currentText().strip(),
            decoder_index=self.decoder_combo.currentIndex(),
            dec_fixed_size=self.dec_fixed_size.value(),
            dec_delim=self.dec_delim_edit.text().strip(),
            dec_delim_inc=self.dec_delim_inc_cb.isChecked(),
            dec_len_size=self.dec_len_size_combo.currentText(),
            dec_len_endian=self.dec_len_endian_combo.currentText(),
            dec_len_inc_hdr=self.dec_len_inc_hdr_cb.isChecked(),
            log_file=self.log_file_edit.text().strip(),
            log_enabled=self.log_file_cb.isChecked(),
        )
        self._settings_manager.save(state)

    # ── Mode / decoder combo handlers ────────────────────────────────────

    _MODE_HELP_TEXT = {
        "UDP": "Binds a local UDP socket and inspects datagrams to/from an optional target.",
        "TCP Client": "Connects directly to a TCP server device and inspects the traffic.",
        "TCP Server": "Listens for one incoming TCP client connection and inspects the traffic.",
        "TCP Proxy": "Sits between an existing client and a target device, forwarding traffic both ways (no Send).",
        "Serial": "Connects directly to a serial (COM/tty) target device.",
    }

    def on_mode_changed(self, index: int):
        self.param_stack.setCurrentIndex(index)
        mode = self.mode_combo.currentText()
        self.update_conn_mode(mode)
        self.mode_help_label.setText(self._MODE_HELP_TEXT.get(mode, ""))
        if mode == "UDP":
            self.log_file_edit.setText("udp.jsonl")
        elif mode == "TCP Client":
            self.log_file_edit.setText("tcp_client.jsonl")
        elif mode == "TCP Server":
            self.log_file_edit.setText("tcp_server.jsonl")
        elif mode == "TCP Proxy":
            self.log_file_edit.setText("tcp_proxy.jsonl")
        elif mode == "Serial":
            self.log_file_edit.setText("serial.jsonl")
        self.update_config_preview()
        self._sync_mode_visuals()

    def _collect_capture_config(self) -> dict:
        """Builds the "config" object for a "configure" command from the current
        form values. Raises ValueError (with a user-facing message) on invalid
        input, e.g. a non-numeric port."""
        mode = self.mode_combo.currentText()

        if mode == "UDP":
            fields = {
                "bind_host": self.udp_bind_host.text(),
                "bind_port": self.udp_bind_port.text(),
                "target_host": self.udp_target_host.text(),
                "target_port": self.udp_target_port.text(),
            }
        elif mode == "TCP Client":
            fields = {"host": self.tcp_cli_host.text(), "port": self.tcp_cli_port.text()}
        elif mode == "TCP Server":
            fields = {"listen_host": self.tcp_srv_host.text(), "listen_port": self.tcp_srv_port.text()}
        elif mode == "TCP Proxy":
            fields = {
                "listen_host": self.tcp_prx_listen_host.text(),
                "listen_port": self.tcp_prx_listen_port.text(),
                "target_host": self.tcp_prx_target_host.text(),
                "target_port": self.tcp_prx_target_port.text(),
            }
        elif mode == "Serial":
            fields = {"serial_port": self.ser_port.currentText(), "baudrate": self.ser_baud.currentText()}
        else:
            fields = {}

        decoder_kind = self.decoder_combo.currentText()
        decoder = build_decoder_config(
            decoder_kind,
            frame_size=self.dec_fixed_size.value(),
            delimiter_hex=self.dec_delim_edit.text().strip(),
            include_delimiter=self.dec_delim_inc_cb.isChecked(),
            length_size=int(self.dec_len_size_combo.currentText()),
            length_endian=self.dec_len_endian_combo.currentText(),
            length_includes_header=self.dec_len_inc_hdr_cb.isChecked(),
        )

        common = {
            "log_path": self.log_file_edit.text().strip() if self.log_file_cb.isChecked() else "",
            "hex_raw": False,
            "hex_frame": False,
            "latency": True,
        }

        return build_capture_config(mode, fields, decoder, common)

    def update_config_preview(self):
        mode = self.mode_combo.currentText()

        try:
            config = self._collect_capture_config()
        except ValueError as exc:
            self.config_preview_edit.setText(f"(invalid: {exc})")
        else:
            self.config_preview_edit.setText(json.dumps(config))

        # Update Port label
        active_port = "-"
        if mode == "UDP":
            bind_p = self.udp_bind_port.text().strip()
            target_p = self.udp_target_port.text().strip()
            active_port = f"Bind {bind_p}" if bind_p else "-"
            if target_p:
                active_port += f" (Target {target_p})"
        elif mode == "TCP Client":
            active_port = self.tcp_cli_port.text().strip()
        elif mode == "TCP Server":
            active_port = self.tcp_srv_port.text().strip()
        elif mode == "TCP Proxy":
            lp = self.tcp_prx_listen_port.text().strip()
            tp = self.tcp_prx_target_port.text().strip()
            active_port = f"Proxy {lp} -> {tp}"
        elif mode == "Serial":
            active_port = self.ser_port.currentText().strip()

        self.update_port(active_port or "-")

    def on_decoder_changed(self, index: int):
        self.decoder_param_stack.setCurrentIndex(index)
        # Raw (index 0) has no frame params - hide the inline param area.
        self.decoder_param_stack.setVisible(index != 0)
        self.update_config_preview()

    def _build_settings_dialog(self):
        dialog = QDialog(self)
        dialog.setWindowTitle("Settings")
        dialog.setMinimumWidth(480)
        layout = QVBoxLayout(dialog)

        paths_layout = QHBoxLayout()
        paths_layout.addWidget(QLabel("CLI Path:", dialog))
        paths_layout.addWidget(self.cli_path_edit)
        paths_layout.addWidget(self.browse_cli_btn)
        layout.addLayout(paths_layout)

        socket_layout = QHBoxLayout()
        socket_layout.addWidget(QLabel("Socket Path:", dialog))
        socket_layout.addWidget(self.socket_path_edit)
        socket_layout.addWidget(self.connect_btn)
        layout.addLayout(socket_layout)

        preview_layout = QHBoxLayout()
        preview_layout.addWidget(QLabel("Config Preview:", dialog))
        preview_layout.addWidget(self.config_preview_edit)
        layout.addLayout(preview_layout)

        # Open Log lives here now that the menu bar has been removed to match the mock.
        open_log_layout = QHBoxLayout()
        open_log_layout.addWidget(QLabel("Offline Log:", dialog))
        open_log_btn = QPushButton("Open Log File…", dialog)
        open_log_btn.clicked.connect(lambda: (dialog.close(), self.open_log_file()))
        open_log_layout.addWidget(open_log_btn)
        open_log_layout.addStretch()
        layout.addLayout(open_log_layout)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close, dialog)
        buttons.rejected.connect(dialog.close)
        buttons.accepted.connect(dialog.close)
        layout.addWidget(buttons)

        self.settings_dialog = dialog

    def open_settings_dialog(self):
        self.settings_dialog.show()
        self.settings_dialog.raise_()
        self.settings_dialog.activateWindow()

    def _on_log_file_toggled(self, checked: bool):
        self.log_file_edit.setEnabled(checked)
        self.browse_log_btn.setEnabled(checked)

    def browse_log_path(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Choose JSONL Log File", self.log_file_edit.text(), "JSONL Files (*.jsonl);;All Files (*)"
        )
        if path:
            self.log_file_edit.setText(path)

    def closeEvent(self, event):
        self.save_settings()
        self.disconnect_socket()
        # stop_capture() alone would only send a stop_capture IPC command, leaving an
        # engine process we spawned running in the background forever. Only kill the
        # process if we're the one who spawned it - an Attach-only session must leave
        # someone else's engine process alone.
        if self.capture_process.is_running():
            self.capture_process.stop()
        event.accept()
