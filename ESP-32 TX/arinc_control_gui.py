#!/usr/bin/env python3

import socket
import random
import subprocess
import threading
import tkinter as tk
from tkinter import ttk, messagebox

from arinc_label_database import ArincLabelEntry, lookup_label, normalize_label, search_labels


DEFAULT_PORT = 5002
DEFAULT_SCAN_STATUS_PORT = 5003
SLOT_COUNT = 16


class ArincControlGui(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("ARINC 429 TX Scheduler 16+1")
        self.geometry("1450x900")
        self.minsize(1100, 650)

        self.ip_var = tk.StringVar(value="192.168.1.32")
        self.port_var = tk.StringVar(value=str(DEFAULT_PORT))
        self.speed_var = tk.StringVar(value="high")
        self.status_var = tk.StringVar(value="Ready")
        self.packet_var = tk.StringVar(value="")
        self.bit_slot_var = tk.StringVar(value="0")
        self.bit_hex_var = tk.StringVar(value="0x00000")
        self.bit_dec_var = tk.StringVar(value="0")
        self.bcd_freq_var = tk.StringVar(value="112.50")
        self.bcd_layout_var = tk.StringVar(value="Right aligned ABC.DE -> 0xABCDE")
        self.bnr_value_var = tk.StringVar(value="0")
        self.bnr_resolution_var = tk.StringVar(value="1")
        self.bnr_shift_var = tk.StringVar(value="0")
        self.bnr_layout_var = tk.StringVar(value="Right aligned")
        self.bnr_signed_var = tk.BooleanVar(value=False)
        self.bnr_reverse_var = tk.BooleanVar(value=False)
        self.decode_var = tk.StringVar(value="BCD: 100.00   BNR: 0")
        self.db_search_var = tk.StringVar(value="")
        self.db_details_var = tk.StringVar(value="Select a label from the database")
        self.scan_enabled_var = tk.BooleanVar(value=False)
        self.scan_start_var = tk.StringVar(value="000")
        self.scan_end_var = tk.StringVar(value="377")
        self.scan_sdi_var = tk.StringVar(value="0")
        self.scan_period_var = tk.StringVar(value="50")
        self.scan_dwell_var = tk.StringVar(value="500")
        self.scan_status_port_var = tk.StringVar(value=str(DEFAULT_SCAN_STATUS_PORT))
        self.scan_last_var = tk.StringVar(value="No scan status received")
        self.scan_function_var = tk.StringVar(value="")
        self.scheduler_active = False
        self.speed_update_after_id: str | None = None
        self.bcd_update_after_id: str | None = None
        self.bnr_update_after_id: str | None = None
        self.slot_update_after_ids: list[str | None] = [None] * SLOT_COUNT
        self.loading_bits = False

        self.enabled_vars: list[tk.BooleanVar] = []
        self.label_vars: list[tk.StringVar] = []
        self.sdi_vars: list[tk.StringVar] = []
        self.data_vars: list[tk.StringVar] = []
        self.ssm_vars: list[tk.StringVar] = []
        self.period_vars: list[tk.StringVar] = []
        self.offset_vars: list[tk.StringVar] = []
        self.mode_vars: list[tk.StringVar] = []
        self.bit_vars: list[tk.BooleanVar] = []
        self.db_tree: ttk.Treeview | None = None
        self.scan_status_stop = threading.Event()
        self.scan_status_sock: socket.socket | None = None
        self.colors = self._theme_colors(self._detect_dark_mode())

        self._init_slots()
        self._init_bit_vars()
        self._configure_style()
        self._build_ui()
        self._bind_live_updates()
        self._load_bits_from_slot()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.start_scan_status_listener()

    def _configure_style(self) -> None:
        colors = self.colors
        self.configure(bg=colors["app_bg"])
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        style.configure("App.TFrame", background=colors["panel_bg"])
        style.configure("Panel.TLabelframe", background=colors["panel_bg"], bordercolor=colors["border"], relief="solid")
        style.configure("Panel.TLabelframe.Label", background=colors["accent"], foreground=colors["accent_fg"], padding=(8, 3), font=("TkDefaultFont", 11, "bold"))
        style.configure("Title.TLabel", background=colors["panel_bg"], foreground=colors["title_fg"], font=("TkDefaultFont", 18, "bold"))
        style.configure("Header.TLabel", background=colors["header_bg"], foreground=colors["header_fg"], padding=(6, 4), font=("TkDefaultFont", 10, "bold"))
        style.configure("TLabel", background=colors["panel_bg"], foreground=colors["text_fg"])
        style.configure("TEntry", fieldbackground=colors["field_bg"], foreground=colors["field_fg"], insertcolor=colors["field_fg"])
        style.configure("TCombobox", fieldbackground=colors["field_bg"], foreground=colors["field_fg"])
        style.configure("Primary.TButton", background=colors["primary_bg"], foreground=colors["primary_fg"], padding=(10, 5), font=("TkDefaultFont", 10, "bold"))
        style.configure("Action.TButton", background=colors["action_bg"], foreground=colors["action_fg"], padding=(8, 4))
        style.configure("Danger.TButton", background=colors["danger_bg"], foreground=colors["danger_fg"], padding=(10, 5), font=("TkDefaultFont", 10, "bold"))
        style.configure("Status.TLabel", background=colors["status_bg"], foreground=colors["status_fg"], padding=(8, 5), font=("TkDefaultFont", 10, "bold"))
        style.configure("Command.TEntry", fieldbackground=colors["command_bg"], foreground=colors["command_fg"], insertcolor=colors["command_fg"])
        style.configure("Treeview", background=colors["field_bg"], fieldbackground=colors["field_bg"], foreground=colors["field_fg"], rowheight=22)
        style.configure("Treeview.Heading", background=colors["header_bg"], foreground=colors["header_fg"], font=("TkDefaultFont", 10, "bold"))
        style.map("Treeview", background=[("selected", colors["primary_bg"])], foreground=[("selected", colors["primary_fg"])])

        style.map("Primary.TButton", background=[("active", colors["primary_active"])])
        style.map("Action.TButton", background=[("active", colors["action_active"])])
        style.map("Danger.TButton", background=[("active", colors["danger_active"])])

    def _detect_dark_mode(self) -> bool:
        try:
            result = subprocess.run(
                ["defaults", "read", "-g", "AppleInterfaceStyle"],
                capture_output=True,
                text=True,
                timeout=1,
                check=False,
            )
        except Exception:
            return False
        return result.stdout.strip().lower() == "dark"

    def _theme_colors(self, dark_mode: bool) -> dict[str, str]:
        if dark_mode:
            return {
                "app_bg": "#020617",
                "panel_bg": "#0f172a",
                "border": "#334155",
                "title_fg": "#e2e8f0",
                "text_fg": "#cbd5e1",
                "header_bg": "#1e293b",
                "header_fg": "#f8fafc",
                "field_bg": "#111827",
                "field_fg": "#f8fafc",
                "accent": "#1d4ed8",
                "accent_fg": "#ffffff",
                "primary_bg": "#2563eb",
                "primary_active": "#1d4ed8",
                "primary_fg": "#ffffff",
                "action_bg": "#1e3a8a",
                "action_active": "#2563eb",
                "action_fg": "#dbeafe",
                "danger_bg": "#7f1d1d",
                "danger_active": "#991b1b",
                "danger_fg": "#fee2e2",
                "status_bg": "#064e3b",
                "status_fg": "#d1fae5",
                "command_bg": "#020617",
                "command_fg": "#e2e8f0",
                "check_select": "#1e293b",
            }
        return {
            "app_bg": "#0f172a",
            "panel_bg": "#e8eef8",
            "border": "#93a4bd",
            "title_fg": "#0f172a",
            "text_fg": "#1e293b",
            "header_bg": "#bfdbfe",
            "header_fg": "#0f172a",
            "field_bg": "#f8fafc",
            "field_fg": "#0f172a",
            "accent": "#1d4ed8",
            "accent_fg": "#ffffff",
            "primary_bg": "#2563eb",
            "primary_active": "#1d4ed8",
            "primary_fg": "#ffffff",
            "action_bg": "#dbeafe",
            "action_active": "#bfdbfe",
            "action_fg": "#1e3a8a",
            "danger_bg": "#fee2e2",
            "danger_active": "#fecaca",
            "danger_fg": "#991b1b",
            "status_bg": "#d1fae5",
            "status_fg": "#064e3b",
            "command_bg": "#0f172a",
            "command_fg": "#e2e8f0",
            "check_select": "#ffffff",
        }

    def _checkbutton(self, parent: tk.Widget, variable: tk.BooleanVar, text: str = "") -> tk.Checkbutton:
        colors = self.colors
        return tk.Checkbutton(
            parent,
            text=text,
            variable=variable,
            bg=colors["panel_bg"],
            activebackground=colors["panel_bg"],
            fg=colors["text_fg"],
            activeforeground=colors["text_fg"],
            selectcolor=colors["check_select"],
            font=("TkDefaultFont", 12),
            padx=4,
            pady=2,
            indicatoron=True,
            highlightthickness=0,
            borderwidth=0,
        )

    def _init_slots(self) -> None:
        defaults = [
            (True, "320", "0", "90", "0", "50", "0"),
            (False, "034", "0", "0", "0", "100", "10"),
            (False, "024", "0", "0", "0", "100", "20"),
        ]

        for slot in range(SLOT_COUNT):
            enabled, label, sdi, data, ssm, period, offset = defaults[slot] if slot < len(defaults) else (False, "000", "0", "0", "0", "100", str(slot * 10))
            self.enabled_vars.append(tk.BooleanVar(value=enabled))
            self.label_vars.append(tk.StringVar(value=label))
            self.sdi_vars.append(tk.StringVar(value=sdi))
            self.data_vars.append(tk.StringVar(value=data))
            self.ssm_vars.append(tk.StringVar(value=ssm))
            self.period_vars.append(tk.StringVar(value=period))
            self.offset_vars.append(tk.StringVar(value=offset))
            self.mode_vars.append(tk.StringVar(value="Raw"))

    def _init_bit_vars(self) -> None:
        for _ in range(19):
            self.bit_vars.append(tk.BooleanVar(value=False))

    def _build_ui(self) -> None:
        self.columnconfigure(0, weight=1)
        self.rowconfigure(0, weight=1)

        canvas = tk.Canvas(self, highlightthickness=0, background=self.colors["app_bg"])
        vscroll = ttk.Scrollbar(self, orient="vertical", command=canvas.yview)
        hscroll = ttk.Scrollbar(self, orient="horizontal", command=canvas.xview)
        canvas.configure(yscrollcommand=vscroll.set, xscrollcommand=hscroll.set)

        canvas.grid(row=0, column=0, sticky="nsew")
        vscroll.grid(row=0, column=1, sticky="ns")
        hscroll.grid(row=1, column=0, sticky="ew")

        root = ttk.Frame(canvas, padding=14, style="App.TFrame")
        window = canvas.create_window((0, 0), window=root, anchor="nw")

        def configure_scroll_region(_event: tk.Event) -> None:
            canvas.configure(scrollregion=canvas.bbox("all"))

        def configure_canvas_width(event: tk.Event) -> None:
            canvas.itemconfigure(window, width=max(event.width, root.winfo_reqwidth()))

        root.bind("<Configure>", configure_scroll_region)
        canvas.bind("<Configure>", configure_canvas_width)
        root.columnconfigure(5, weight=1)
        root.columnconfigure(9, weight=1)

        title = ttk.Label(root, text="ARINC 429 TX Scheduler 16+1", style="Title.TLabel")
        title.grid(row=0, column=0, columnspan=10, sticky="w", pady=(0, 12))

        ttk.Label(root, text="ESP32 IP").grid(row=1, column=0, sticky="w")
        ttk.Entry(root, textvariable=self.ip_var, width=17).grid(row=1, column=1, sticky="w")
        ttk.Label(root, text="UDP Port").grid(row=1, column=2, sticky="w")
        ttk.Entry(root, textvariable=self.port_var, width=8).grid(row=1, column=3, sticky="w")
        ttk.Label(root, text="Global Speed").grid(row=1, column=4, sticky="w")
        ttk.Combobox(root, textvariable=self.speed_var, values=["high", "low"], width=8, state="readonly").grid(row=1, column=5, sticky="w")

        headers = ["Slot", "Enable", "Label", "SDI", "Mode", "Data19", "SSM", "Period ms", "Offset ms", "Actions"]
        for col, header in enumerate(headers):
            ttk.Label(root, text=header, style="Header.TLabel").grid(row=2, column=col, sticky="ew", pady=(14, 4))

        for slot in range(SLOT_COUNT):
            row = slot + 3
            ttk.Label(root, text=str(slot)).grid(row=row, column=0, sticky="w")
            self._checkbutton(root, self.enabled_vars[slot]).grid(row=row, column=1, sticky="w")
            ttk.Entry(root, textvariable=self.label_vars[slot], width=8).grid(row=row, column=2, sticky="w")
            ttk.Combobox(root, textvariable=self.sdi_vars[slot], values=["0", "1", "2", "3"], width=4, state="readonly").grid(row=row, column=3, sticky="w")
            ttk.Combobox(root, textvariable=self.mode_vars[slot], values=["Raw", "BCD", "BNR"], width=6, state="readonly").grid(row=row, column=4, sticky="w")
            ttk.Entry(root, textvariable=self.data_vars[slot], width=10).grid(row=row, column=5, sticky="w")
            ttk.Combobox(root, textvariable=self.ssm_vars[slot], values=["0", "1", "2", "3"], width=4, state="readonly").grid(row=row, column=6, sticky="w")
            ttk.Entry(root, textvariable=self.period_vars[slot], width=8).grid(row=row, column=7, sticky="w")
            ttk.Entry(root, textvariable=self.offset_vars[slot], width=8).grid(row=row, column=8, sticky="w")
            actions = ttk.Frame(root, style="App.TFrame")
            actions.grid(row=row, column=9, sticky="w")
            ttk.Button(actions, text="Apply", style="Action.TButton", command=lambda s=slot: self.apply_row(s)).grid(row=0, column=0, padx=(0, 4))
            ttk.Button(actions, text="Once", style="Action.TButton", command=lambda s=slot: self.send_once(s)).grid(row=0, column=1)

        panel_row = SLOT_COUNT + 3

        buttons = ttk.Frame(root, style="App.TFrame")
        buttons.grid(row=panel_row, column=0, columnspan=8, sticky="w", pady=(14, 8))
        ttk.Button(buttons, text="Start Scheduler", style="Primary.TButton", command=self.start_scheduler).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(buttons, text="Apply All", style="Action.TButton", command=self.apply_all).grid(row=0, column=1, padx=(0, 8))
        ttk.Button(buttons, text="Stop All", style="Danger.TButton", command=self.stop_all).grid(row=0, column=2, padx=(0, 8))
        ttk.Button(buttons, text="Clear ESP32 Slots", style="Danger.TButton", command=self.clear_slots).grid(row=0, column=3)

        bits = ttk.LabelFrame(root, text="Data19 Bit Editor", padding=8, style="Panel.TLabelframe")
        bits.grid(row=panel_row + 1, column=0, columnspan=10, sticky="ew", pady=(12, 4))
        ttk.Label(bits, text="Slot").grid(row=0, column=0, sticky="w")
        ttk.Combobox(bits, textvariable=self.bit_slot_var, values=[str(i) for i in range(SLOT_COUNT)], width=4, state="readonly").grid(row=0, column=1, sticky="w")
        ttk.Label(bits, text="Hex").grid(row=0, column=2, sticky="w")
        ttk.Label(bits, textvariable=self.bit_hex_var, width=10).grid(row=0, column=3, sticky="w")
        ttk.Label(bits, text="Dec").grid(row=0, column=4, sticky="w")
        ttk.Label(bits, textvariable=self.bit_dec_var, width=8).grid(row=0, column=5, sticky="w")
        ttk.Button(bits, text="Clear Bits", style="Action.TButton", command=self.clear_bits).grid(row=0, column=6, padx=(8, 4))
        ttk.Button(bits, text="Random", style="Action.TButton", command=self.randomize_data19).grid(row=0, column=7, padx=(0, 4))

        for index, var in enumerate(self.bit_vars):
            bit_number = index + 1
            row = 1 + index // 10
            col = index % 10
            self._checkbutton(bits, var, text=str(bit_number)).grid(row=row, column=col, sticky="w", padx=3, pady=3)

        convert = ttk.LabelFrame(root, text="Selected Slot Data Helper", padding=8, style="Panel.TLabelframe")
        convert.grid(row=panel_row + 2, column=0, columnspan=10, sticky="ew", pady=(8, 4))
        ttk.Label(convert, textvariable=self.decode_var).grid(row=0, column=0, columnspan=8, sticky="w")
        ttk.Label(convert, text="BCD Frequency").grid(row=1, column=0, sticky="w")
        ttk.Entry(convert, textvariable=self.bcd_freq_var, width=10).grid(row=1, column=1, sticky="w")
        ttk.Combobox(convert, textvariable=self.bcd_layout_var, values=["Shift left 1 nibble 1AB.CD -> 0xABCD0", "Right aligned ABC.DE -> 0xABCDE", "Legacy 0x0ABCD -> 1AB.CD"], width=39, state="readonly").grid(row=1, column=2, sticky="w")
        ttk.Button(convert, text="Apply BCD", style="Action.TButton", command=self.apply_bcd).grid(row=1, column=3, padx=(8, 16))
        ttk.Label(convert, text="BNR Value").grid(row=2, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(convert, textvariable=self.bnr_value_var, width=10).grid(row=2, column=1, sticky="w", pady=(8, 0))
        ttk.Label(convert, text="Resolution").grid(row=2, column=2, sticky="w", pady=(8, 0))
        ttk.Entry(convert, textvariable=self.bnr_resolution_var, width=10).grid(row=2, column=3, sticky="w", pady=(8, 0))
        ttk.Label(convert, text="Shift").grid(row=2, column=4, sticky="w", pady=(8, 0))
        ttk.Entry(convert, textvariable=self.bnr_shift_var, width=5).grid(row=2, column=5, sticky="w", pady=(8, 0))
        ttk.Combobox(convert, textvariable=self.bnr_layout_var, values=["Right aligned", "Shift left N bits"], width=16, state="readonly").grid(row=2, column=6, sticky="w", pady=(8, 0))
        self._checkbutton(convert, self.bnr_signed_var, text="Signed").grid(row=2, column=7, sticky="w", pady=(8, 0))
        self._checkbutton(convert, self.bnr_reverse_var, text="Reverse bits").grid(row=2, column=8, sticky="w", pady=(8, 0))
        ttk.Button(convert, text="Apply BNR", style="Action.TButton", command=self.apply_bnr).grid(row=2, column=9, padx=(8, 0), pady=(8, 0))

        database = ttk.LabelFrame(root, text="ARINC Label Database", padding=8, style="Panel.TLabelframe")
        database.grid(row=panel_row + 3, column=0, columnspan=10, sticky="ew", pady=(8, 4))
        database.columnconfigure(3, weight=1)
        ttk.Label(database, text="Search").grid(row=0, column=0, sticky="w")
        ttk.Entry(database, textvariable=self.db_search_var, width=28).grid(row=0, column=1, sticky="w")
        ttk.Button(database, text="Search", style="Action.TButton", command=self.refresh_label_database).grid(row=0, column=2, sticky="w")
        ttk.Button(database, text="Apply to Selected Slot", style="Action.TButton", command=self.apply_database_label_to_slot).grid(row=0, column=3, sticky="w")
        ttk.Label(database, textvariable=self.db_details_var).grid(row=1, column=0, columnspan=4, sticky="w")
        columns = ("label", "encoding", "name", "resolution", "range")
        self.db_tree = ttk.Treeview(database, columns=columns, show="headings", height=7)
        headings = {
            "label": "Label",
            "encoding": "Mode",
            "name": "Function",
            "resolution": "Resolution",
            "range": "Range",
        }
        widths = {"label": 70, "encoding": 70, "name": 430, "resolution": 120, "range": 150}
        for column in columns:
            self.db_tree.heading(column, text=headings[column])
            self.db_tree.column(column, width=widths[column], anchor="w")
        self.db_tree.grid(row=2, column=0, columnspan=4, sticky="ew", pady=(4, 0))
        self.db_tree.bind("<<TreeviewSelect>>", self._on_database_selection)

        scan = ttk.LabelFrame(root, text="Persistent Scan Channel", padding=8, style="Panel.TLabelframe")
        scan.grid(row=panel_row + 4, column=0, columnspan=10, sticky="ew", pady=(8, 4))
        self._checkbutton(scan, self.scan_enabled_var, text="Enable").grid(row=0, column=0, sticky="w")
        ttk.Label(scan, text="Start label").grid(row=0, column=1, sticky="w")
        ttk.Entry(scan, textvariable=self.scan_start_var, width=8).grid(row=0, column=2, sticky="w")
        ttk.Label(scan, text="End label").grid(row=0, column=3, sticky="w")
        ttk.Entry(scan, textvariable=self.scan_end_var, width=8).grid(row=0, column=4, sticky="w")
        ttk.Label(scan, text="SDI").grid(row=0, column=5, sticky="w")
        ttk.Combobox(scan, textvariable=self.scan_sdi_var, values=["0", "1", "2", "3"], width=4, state="readonly").grid(row=0, column=6, sticky="w")
        ttk.Label(scan, text="Word period ms").grid(row=0, column=7, sticky="w")
        ttk.Entry(scan, textvariable=self.scan_period_var, width=8).grid(row=0, column=8, sticky="w")
        ttk.Label(scan, text="Dwell ms/label").grid(row=0, column=9, sticky="w")
        ttk.Entry(scan, textvariable=self.scan_dwell_var, width=8).grid(row=0, column=10, sticky="w")
        ttk.Button(scan, text="Apply Scan", style="Primary.TButton", command=self.apply_scan_channel).grid(row=0, column=11, sticky="w")
        ttk.Button(scan, text="Stop Scan", style="Danger.TButton", command=self.stop_scan_channel).grid(row=0, column=12, sticky="w")
        ttk.Label(scan, text="Status UDP").grid(row=1, column=0, sticky="w")
        ttk.Entry(scan, textvariable=self.scan_status_port_var, width=8).grid(row=1, column=1, sticky="w")
        ttk.Button(scan, text="Restart Listener", style="Action.TButton", command=self.restart_scan_status_listener).grid(row=1, column=2, sticky="w")
        ttk.Label(scan, textvariable=self.scan_last_var).grid(row=1, column=3, columnspan=10, sticky="w")
        ttk.Label(scan, textvariable=self.scan_function_var).grid(row=2, column=0, columnspan=13, sticky="w")

        ttk.Label(root, text="Last command").grid(row=panel_row + 5, column=0, columnspan=8, sticky="w", pady=(8, 2))
        ttk.Entry(root, textvariable=self.packet_var, width=140, state="readonly", style="Command.TEntry").grid(row=panel_row + 6, column=0, columnspan=10, sticky="ew")
        ttk.Label(root, textvariable=self.status_var, style="Status.TLabel").grid(row=panel_row + 7, column=0, columnspan=10, sticky="ew", pady=(8, 0))

        for child in root.winfo_children():
            child.grid_configure(padx=4, pady=2)
        self.refresh_label_database()

    def _bind_live_updates(self) -> None:
        self.speed_var.trace_add("write", self._schedule_speed_update)
        self.bit_slot_var.trace_add("write", self._on_bit_slot_changed)
        for slot in range(SLOT_COUNT):
            for var in (
                self.enabled_vars[slot],
                self.label_vars[slot],
                self.sdi_vars[slot],
                self.data_vars[slot],
                self.ssm_vars[slot],
                self.period_vars[slot],
                self.offset_vars[slot],
                self.mode_vars[slot],
            ):
                var.trace_add("write", lambda *_args, s=slot: self._schedule_slot_update(s))
            self.data_vars[slot].trace_add("write", self._on_data_field_changed)

        for var in self.bit_vars:
            var.trace_add("write", self._on_bit_changed)

        for var in (self.bcd_freq_var, self.bcd_layout_var):
            var.trace_add("write", self._schedule_bcd_update)

        for var in (
            self.bnr_value_var,
            self.bnr_resolution_var,
            self.bnr_shift_var,
            self.bnr_layout_var,
            self.bnr_signed_var,
            self.bnr_reverse_var,
        ):
            var.trace_add("write", self._on_bnr_helper_changed)

        self.db_search_var.trace_add("write", lambda *_args: self.refresh_label_database())

    def refresh_label_database(self) -> None:
        if self.db_tree is None:
            return
        for item in self.db_tree.get_children():
            self.db_tree.delete(item)
        for entry in search_labels(self.db_search_var.get())[:250]:
            self.db_tree.insert(
                "",
                "end",
                iid=entry.label,
                values=(entry.label, entry.encoding, entry.name, entry.resolution, entry.range_text),
            )

    def _selected_database_entry(self) -> ArincLabelEntry | None:
        if self.db_tree is None:
            return None
        selection = self.db_tree.selection()
        if not selection:
            return None
        return lookup_label(selection[0])

    def _on_database_selection(self, _event: tk.Event) -> None:
        entry = self._selected_database_entry()
        if entry is None:
            self.db_details_var.set("Select a label from the database")
            return
        details = f"{entry.label}: {entry.name} | {entry.encoding} | default data19=0x{entry.default_data19:05X} ssm={entry.default_ssm}"
        if entry.units:
            details += f" | units={entry.units}"
        if entry.notes:
            details += f" | {entry.notes}"
        self.db_details_var.set(details)

    def apply_database_label_to_slot(self) -> None:
        entry = self._selected_database_entry()
        if entry is None:
            messagebox.showerror("ARINC Label Database", "Select a database label first")
            return
        slot = self._selected_bit_slot()
        self.label_vars[slot].set(entry.label)
        self.mode_vars[slot].set(entry.encoding if entry.encoding in ("BCD", "BNR") else "Raw")
        self.data_vars[slot].set(f"0x{entry.default_data19:05X}")
        self.ssm_vars[slot].set(str(entry.default_ssm))
        self._load_bits_from_slot()
        self._send_slot_update(slot)

    def _selected_bit_slot(self) -> int:
        try:
            slot = int(self.bit_slot_var.get())
        except ValueError:
            return 0
        return max(0, min(SLOT_COUNT - 1, slot))

    def _slot_data_value(self, slot: int) -> int:
        try:
            return int(self.data_vars[slot].get().strip(), 0) & 0x7FFFF
        except ValueError:
            return 0

    def _set_bit_display(self, value: int) -> None:
        self.bit_hex_var.set(f"0x{value & 0x7FFFF:05X}")
        self.bit_dec_var.set(str(value & 0x7FFFF))

    def _load_bits_from_slot(self) -> None:
        slot = self._selected_bit_slot()
        value = self._slot_data_value(slot)
        self.loading_bits = True
        for index, var in enumerate(self.bit_vars):
            var.set(bool(value & (1 << index)))
        self.loading_bits = False
        self._set_bit_display(value)
        self._set_decode_display(value)

    def _bits_to_value(self) -> int:
        value = 0
        for index, var in enumerate(self.bit_vars):
            if var.get():
                value |= 1 << index
        return value & 0x7FFFF

    def _on_bit_slot_changed(self, *_args: object) -> None:
        self._load_bits_from_slot()

    def _on_data_field_changed(self, *_args: object) -> None:
        if self.loading_bits:
            return
        self._load_bits_from_slot()

    def _on_bit_changed(self, *_args: object) -> None:
        if self.loading_bits:
            return
        slot = self._selected_bit_slot()
        value = self._bits_to_value()
        self.loading_bits = True
        self.data_vars[slot].set(f"0x{value:05X}")
        self.loading_bits = False
        self._set_bit_display(value)
        self._set_decode_display(value)
        self._schedule_slot_update(slot)

    def _on_bnr_helper_changed(self, *_args: object) -> None:
        self._set_decode_display(self._slot_data_value(self._selected_bit_slot()))
        self._schedule_bnr_update()

    def clear_bits(self) -> None:
        self.loading_bits = True
        for var in self.bit_vars:
            var.set(False)
        self.loading_bits = False
        self._on_bit_changed()

    def _set_selected_slot_data(self, value: int) -> None:
        slot = self._selected_bit_slot()
        value &= 0x7FFFF
        self.data_vars[slot].set(f"0x{value:05X}")
        self._load_bits_from_slot()

    def _decode_bcd_display(self, value: int) -> str:
        layout = self.bcd_layout_var.get()
        if layout == "Shift left 1 nibble 1AB.CD -> 0xABCD0":
            digits = f"{(value >> 4) & 0x0FFFF:04X}"
            return f"1{digits[:2]}.{digits[2:]}"
        if layout == "Right aligned ABC.DE -> 0xABCDE":
            digits = f"{value & 0x7FFFF:05X}"
            return f"{digits[:3]}.{digits[3:]}"
        digits = f"{value & 0x0FFFF:04X}"
        return f"1{digits[:2]}.{digits[2:]}"

    def _decode_bnr_display(self, value: int) -> str:
        try:
            resolution = float(self.bnr_resolution_var.get())
            shift = int(self.bnr_shift_var.get())
        except ValueError:
            return "invalid"
        if resolution <= 0:
            return "invalid"
        if self.bnr_layout_var.get() == "Right aligned":
            shift = 0
        if not 0 <= shift <= 18:
            return "invalid"
        bits = 19 - shift

        source = self._reverse_bits(value & 0x7FFFF, 19) if self.bnr_reverse_var.get() else value
        mask = (1 << bits) - 1
        raw = (source >> shift) & mask
        if self.bnr_signed_var.get() and (raw & (1 << (bits - 1))):
            raw -= 1 << bits
        return f"{raw * resolution:g}"

    def _reverse_bits(self, value: int, width: int) -> int:
        result = 0
        for _ in range(width):
            result = (result << 1) | (value & 1)
            value >>= 1
        return result

    def _set_decode_display(self, value: int) -> None:
        self.decode_var.set(f"BCD: {self._decode_bcd_display(value)}   BNR: {self._decode_bnr_display(value)}")

    def apply_bcd(self, live: bool = False) -> None:
        try:
            text = self.bcd_freq_var.get().strip()
            digits = "".join(ch for ch in text if ch.isdigit())
            layout = self.bcd_layout_var.get()
            if layout == "Shift left 1 nibble 1AB.CD -> 0xABCD0":
                if len(digits) < 5:
                    digits = digits.ljust(5, "0")
                if not digits.startswith("1"):
                    raise ValueError("1AB.CD shifted layout expects frequency starting with 1xx.xx")
                value = int(digits[1:5], 16) << 4
            elif layout == "Right aligned ABC.DE -> 0xABCDE":
                if len(digits) != 5:
                    raise ValueError("ABC.DE layout needs 5 digits, e.g. 11250")
                value = int(digits, 16)
            else:
                if len(digits) < 5:
                    digits = digits.ljust(5, "0")
                if not digits.startswith("1"):
                    raise ValueError("1AB.CD layout expects frequency starting with 1xx.xx")
                value = int(digits[1:5], 16)
        except Exception as exc:
            if not live:
                messagebox.showerror("BCD", str(exc))
            self.status_var.set(f"Invalid BCD, not sent: {exc}")
            return

        slot = self._selected_bit_slot()
        self.mode_vars[slot].set("BCD")
        self._set_selected_slot_data(value)
        self._send_slot_update(slot, live=live)

    def apply_bnr(self, live: bool = False) -> None:
        try:
            value = float(self.bnr_value_var.get())
            resolution = float(self.bnr_resolution_var.get())
            shift = int(self.bnr_shift_var.get())
            if resolution <= 0:
                raise ValueError("Resolution must be greater than zero")
            if self.bnr_layout_var.get() == "Right aligned":
                shift = 0
                if self.bnr_shift_var.get() != "0":
                    self.bnr_shift_var.set("0")
            if not 0 <= shift <= 18:
                raise ValueError("BNR shift must be 0..18")
            bits = 19 - shift

            raw = int(round(value / resolution))
            if self.bnr_signed_var.get():
                raw_min = -(1 << (bits - 1))
                raw_max = (1 << (bits - 1)) - 1
            else:
                raw_min = 0
                raw_max = (1 << bits) - 1
            if not raw_min <= raw <= raw_max:
                value_min = raw_min * resolution
                value_max = raw_max * resolution
                raise ValueError(f"BNR value must be {value_min:g}..{value_max:g} with shift {shift} and resolution {resolution:g}")
            if raw < 0:
                raw = (1 << bits) + raw
            encoded = (raw << shift) & 0x7FFFF
            if self.bnr_reverse_var.get():
                encoded = self._reverse_bits(encoded, 19)
        except Exception as exc:
            if not live:
                messagebox.showerror("BNR", str(exc))
            self.status_var.set(f"Invalid BNR, not sent: {exc}")
            return

        slot = self._selected_bit_slot()
        self.mode_vars[slot].set("BNR")
        self._set_selected_slot_data(encoded)
        self._send_slot_update(slot, live=live)

    def _schedule_bcd_update(self, *_args: object) -> None:
        if self.bcd_update_after_id is not None:
            self.after_cancel(self.bcd_update_after_id)
        self.bcd_update_after_id = self.after(300, self._send_bcd_update)

    def _send_bcd_update(self) -> None:
        self.bcd_update_after_id = None
        self.apply_bcd(live=True)

    def _schedule_bnr_update(self, *_args: object) -> None:
        if self.bnr_update_after_id is not None:
            self.after_cancel(self.bnr_update_after_id)
        self.bnr_update_after_id = self.after(300, self._send_bnr_update)

    def _send_bnr_update(self) -> None:
        self.bnr_update_after_id = None
        self.apply_bnr(live=True)

    def _schedule_speed_update(self, *_args: object) -> None:
        if self.speed_update_after_id is not None:
            self.after_cancel(self.speed_update_after_id)
        self.speed_update_after_id = self.after(300, self._send_speed_update)

    def _send_speed_update(self) -> None:
        self.speed_update_after_id = None
        try:
            command = self._speed_command()
        except Exception as exc:
            self.status_var.set(f"Invalid speed: {exc}")
            return
        if self._send_command(command, live=True) and self.scheduler_active:
            self._send_command("start=1", live=True)

    def _schedule_slot_update(self, slot: int, *_args: object) -> None:
        if self.slot_update_after_ids[slot] is not None:
            self.after_cancel(self.slot_update_after_ids[slot])
        self.slot_update_after_ids[slot] = self.after(300, lambda s=slot: self._send_slot_update(s, live=True))

    def _send_slot_update(self, slot: int, live: bool = False) -> bool:
        if self.slot_update_after_ids[slot] is not None:
            self.after_cancel(self.slot_update_after_ids[slot])
            self.slot_update_after_ids[slot] = None
        ok = self.apply_row(slot, live=live)
        if ok and self.scheduler_active:
            ok = self._send_command("start=1", live=live) and ok
        if ok:
            self.status_var.set(f"Slot {slot} updated and sent")
        return ok

    def _cancel_pending_updates(self) -> None:
        if self.speed_update_after_id is not None:
            self.after_cancel(self.speed_update_after_id)
            self.speed_update_after_id = None
        if self.bcd_update_after_id is not None:
            self.after_cancel(self.bcd_update_after_id)
            self.bcd_update_after_id = None
        if self.bnr_update_after_id is not None:
            self.after_cancel(self.bnr_update_after_id)
            self.bnr_update_after_id = None
        for slot, after_id in enumerate(self.slot_update_after_ids):
            if after_id is not None:
                self.after_cancel(after_id)
                self.slot_update_after_ids[slot] = None

    def _destination(self) -> tuple[str, int]:
        return self.ip_var.get().strip(), int(self.port_var.get().strip())

    def _send_command(self, command: str, live: bool = False) -> bool:
        try:
            destination = self._destination()
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.sendto(command.encode("ascii"), destination)
        except Exception as exc:
            self.packet_var.set(command)
            if not live:
                messagebox.showerror("ARINC Scheduler", str(exc))
            self.status_var.set(f"Command failed: {exc}")
            return False

        self.packet_var.set(command)
        self.status_var.set(f"Sent to {destination[0]}:{destination[1]}")
        return True

    def start_scan_status_listener(self) -> None:
        self.scan_status_stop.clear()
        thread = threading.Thread(target=self._scan_status_listener, daemon=True)
        thread.start()

    def restart_scan_status_listener(self) -> None:
        self.scan_status_stop.set()
        if self.scan_status_sock is not None:
            self.scan_status_sock.close()
            self.scan_status_sock = None
        self.scan_last_var.set("Restarting scan status listener...")
        self.start_scan_status_listener()

    def _scan_status_listener(self) -> None:
        try:
            port = int(self.scan_status_port_var.get().strip())
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(0.5)
            sock.bind(("0.0.0.0", port))
            self.scan_status_sock = sock
        except Exception as exc:
            self.after(0, lambda: self.scan_last_var.set(f"Scan status listener failed: {exc}"))
            return

        self.after(0, lambda: self.scan_last_var.set(f"Listening for scan status on UDP {port}"))
        while not self.scan_status_stop.is_set():
            try:
                data, addr = sock.recvfrom(512)
            except TimeoutError:
                continue
            except OSError:
                break
            try:
                text = data.decode("ascii", errors="replace")
            except Exception:
                continue
            self.after(0, lambda t=text, a=addr: self._set_scan_status(t, a))

    def _set_scan_status(self, text: str, addr: tuple[str, int]) -> None:
        values: dict[str, str] = {}
        for token in text.split():
            if "=" in token:
                key, value = token.split("=", 1)
                values[key] = value

        label = values.get("label", "---")
        data = values.get("data", "")
        sdi = values.get("sdi", "")
        ssm = values.get("ssm", "")
        count = values.get("count", "")
        known = values.get("known", "")
        self.scan_last_var.set(
            f"#{count} from {addr[0]} label={label} sdi={sdi} data19={data} ssm={ssm} known={known}"
        )
        try:
            entry = lookup_label(label)
        except Exception:
            entry = None
        if entry is None:
            self.scan_function_var.set("Unknown label: random data19, SSM cycles 0/1/2/3")
        else:
            self.scan_function_var.set(f"{entry.label}: {entry.name} | {entry.encoding} | {entry.units} {entry.resolution} {entry.range_text}".strip())

    def _on_close(self) -> None:
        self.scan_status_stop.set()
        if self.scan_status_sock is not None:
            self.scan_status_sock.close()
        self.destroy()

    def _validate_slot(self, slot: int) -> tuple[str, int, int, int, int, int]:
        label = self.label_vars[slot].get().strip()
        sdi = int(self.sdi_vars[slot].get().strip())
        data = int(self.data_vars[slot].get().strip(), 0)
        ssm = int(self.ssm_vars[slot].get().strip())
        period = int(self.period_vars[slot].get().strip())
        offset = int(self.offset_vars[slot].get().strip())

        int(label, 8)
        if not 0 <= sdi <= 3:
            raise ValueError(f"Slot {slot}: SDI must be 0..3")
        if not 0 <= data <= 0x7FFFF:
            raise ValueError(f"Slot {slot}: Data19 must be 0..0x7FFFF")
        if not 0 <= ssm <= 3:
            raise ValueError(f"Slot {slot}: SSM must be 0..3")
        if not 1 <= period <= 5000:
            raise ValueError(f"Slot {slot}: period must be 1..5000 ms")
        if not 0 <= offset <= 5000:
            raise ValueError(f"Slot {slot}: offset must be 0..5000 ms")

        return label, sdi, data, ssm, period, offset

    def _slot_command(self, slot: int, once: bool = False) -> str:
        label, sdi, data, ssm, period, offset = self._validate_slot(slot)
        enabled = 1 if self.enabled_vars[slot].get() else 0
        return (
            f"slot={slot} label={label} sdi={sdi} data={data} ssm={ssm} "
            f"period={period} offset={offset} enabled={enabled} once={1 if once else 0}"
        )

    def _speed_command(self) -> str:
        speed = self.speed_var.get().strip()
        if speed not in ("high", "low"):
            raise ValueError("Global speed must be high or low")
        return f"speed={speed}"

    def _scan_command(self, enabled: bool | None = None) -> str:
        scan_enabled = self.scan_enabled_var.get() if enabled is None else enabled
        start_label = normalize_label(self.scan_start_var.get())
        end_label = normalize_label(self.scan_end_var.get())
        sdi = int(self.scan_sdi_var.get().strip())
        period = int(self.scan_period_var.get().strip())
        dwell = int(self.scan_dwell_var.get().strip())
        if not 0 <= sdi <= 3:
            raise ValueError("Scan SDI must be 0..3")
        if not 1 <= period <= 5000:
            raise ValueError("Scan word period must be 1..5000 ms")
        if not period <= dwell <= 60000:
            raise ValueError("Scan dwell must be at least word period and no more than 60000 ms")
        return (
            f"scan_enabled={1 if scan_enabled else 0} scan_start={start_label} "
            f"scan_end={end_label} scan_sdi={sdi} scan_period={period} scan_dwell={dwell}"
        )

    def apply_row(self, slot: int, live: bool = False) -> bool:
        try:
            command = self._slot_command(slot)
        except Exception as exc:
            if not live:
                messagebox.showerror("ARINC Scheduler", str(exc))
            self.status_var.set(f"Invalid slot {slot}: {exc}")
            return False
        return self._send_command(command, live=live)

    def send_once(self, slot: int) -> None:
        try:
            command = self._slot_command(slot, once=True)
        except Exception as exc:
            messagebox.showerror("ARINC Scheduler", str(exc))
            self.status_var.set(f"Invalid slot {slot}: {exc}")
            return
        self._send_command(command)

    def randomize_data19(self) -> None:
        slot = self._selected_bit_slot()
        value = random.randint(0, 0x7FFFF)
        self.data_vars[slot].set(f"0x{value:05X}")
        self._load_bits_from_slot()
        self._send_slot_update(slot)

    def apply_all(self, live: bool = False) -> bool:
        try:
            commands = [self._speed_command()]
            commands.extend(self._slot_command(slot) for slot in range(SLOT_COUNT))
        except Exception as exc:
            if not live:
                messagebox.showerror("ARINC Scheduler", str(exc))
            self.status_var.set(f"Invalid value, not sent: {exc}")
            return False

        ok = True
        for command in commands:
            ok = self._send_command(command, live=live) and ok
        if self.scheduler_active:
            ok = self._send_command("start=1", live=live) and ok
        if ok:
            self.status_var.set("Live scheduler update sent" if live else "All slots applied")
        return ok

    def start_scheduler(self) -> None:
        if self.apply_all():
            self.scheduler_active = True
            self._send_command("start=1")
            self.status_var.set("Scheduler active: changes are sent live")

    def stop_all(self) -> None:
        self._cancel_pending_updates()
        self.scheduler_active = False
        self._send_command("all_enabled=0")
        self.status_var.set("Scheduler stopped")

    def clear_slots(self) -> None:
        self._cancel_pending_updates()
        self.scheduler_active = False
        self._send_command("clear=1")
        self.status_var.set("ESP32 scheduler slots cleared")

    def apply_scan_channel(self) -> None:
        try:
            command = self._scan_command()
        except Exception as exc:
            messagebox.showerror("ARINC Scan Channel", str(exc))
            self.status_var.set(f"Invalid scan channel: {exc}")
            return
        if self._send_command(command):
            self.status_var.set("Persistent scan channel updated")

    def stop_scan_channel(self) -> None:
        self.scan_enabled_var.set(False)
        try:
            command = self._scan_command(enabled=False)
        except Exception as exc:
            messagebox.showerror("ARINC Scan Channel", str(exc))
            self.status_var.set(f"Invalid scan channel: {exc}")
            return
        if self._send_command(command):
            self.status_var.set("Persistent scan channel stopped")


if __name__ == "__main__":
    ArincControlGui().mainloop()
