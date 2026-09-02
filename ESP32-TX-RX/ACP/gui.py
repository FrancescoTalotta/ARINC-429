#!/usr/bin/env python3
import socket
import tkinter as tk
from math import pi, sin, cos, log10
import struct

from typing import Optional, Callable
from typing import Dict, Any, Optional

# ----------------------------- UDP settings ------------------------------ #
UDP_HOST = "192.168.1.35"   # ESP32 W5500 IP
UDP_PORT = 5000             # ESP32 UDP_RX_PORT
UDP_CMD_MAGIC = 0xDEADBEEF

# Button -> payload mapping (strings will be sent as UTF-8)
PAYLOADS = {
    # top row
    "TOP_CALL_1": "CALL1",
    "TOP_CALL_2": "CALL2",
    "TOP_CALL_3": "CALL3",
    "TOP_CALL_4": "CALL4",
    "TOP_CALL_5": "CALL5",
    "TOP_MECH":   "MECH",
    "TOP_ATT":    "ATTN",
    # center block
    "ON_VOICE":   "ON_VOICE",
    "RESET":      "RESET",
    "CENTER_CALL":"CENTER_CALL",
}

CALL_BITS = {
    "TOP_CALL_1": 0,  # VHF1
    "TOP_CALL_2": 1,  # VHF2
    "TOP_CALL_3": 2,  # VHF3
    "TOP_CALL_4": 3,  # HF1
    "TOP_CALL_5": 4,  # HF2
    "TOP_MECH":   5,
    "TOP_ATT":    6,
}

ACP_KNOB_MAP = {
    (0o210, 1): ("top", 0),  # VHF1
    (0o210, 2): ("top", 1),  # VHF2
    (0o210, 3): ("top", 2),  # VHF3
    (0o211, 1): ("top", 3),  # HF1
    (0o211, 2): ("top", 4),  # HF2
    (0o215, 1): ("top", 5),  # INT
    (0o215, 2): ("top", 6),  # CAB
    (0o213, 1): ("bottom", 0),  # VOR1
    (0o213, 2): ("bottom", 1),  # VOR2
    (0o213, 3): ("bottom", 2),  # MKR
    (0o217, 0): ("bottom", 3),  # ILS
    (0o220, 0): ("bottom", 4),  # MLS
    (0o212, 1): ("bottom", 5),  # ADF1
    (0o212, 2): ("bottom", 6),  # ADF2
    (0o212, 3): ("center", 0),  # PA
}

# ---- decoding helpers (can be imported from your arinc429lib) ----
def reverse_8bits(x: int) -> int:
    x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4)
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2)
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1)
    return x

def reverse_2bits(x: int) -> int:
    return ((x & 0b01) << 1) | ((x & 0b10) >> 1)

def decode_arinc429(word: int) -> dict:
    label  = (word >> 24) & 0xFF
    sdi    = reverse_2bits((word >> 22) & 0x03)
    data   = (word >> 3) & 0x7FFFF
    ssm    = (word >> 1) & 0x03
    parity = word & 0x01
    parity_ok = (word.bit_count() & 1) == 1
    return {"label": label, "sdi": sdi, "data": data, "ssm": ssm, "parity": parity, "parity_ok": parity_ok}

def decode_acp_data(data: int) -> dict:
    return {
        "ER1":     data & 0x1,
        "ER0":     (data >> 1)  & 0x1,
        "RESET":   (data >> 2)  & 0x1,
        "VOICE":   (data >> 3)  & 0x1,
        "ON":      (data >> 4)  & 0x1,
        "CHANNEL": reverse_8bits((data >> 5) & 0xFF),
        "INT":     (data >> 13) & 0x1,
        "RAD":     (data >> 14) & 0x1,
    }

def decode_acp_knob_data(data: int) -> dict:
    return {
        "pulled": bool(data & 0x10),
        "volume": reverse_8bits((data >> 5) & 0xFF),
    }


class UdpWordReceiver:
    def __init__(self, host: str, port: int, callback):
        self.callback = callback
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.setblocking(False)

    def poll(self, max_packets: int = 50):
        for _ in range(max_packets):
            try:
                data, _ = self.sock.recvfrom(2048)
            except BlockingIOError:
                return
            if len(data) >= 4:
                (word,) = struct.unpack("!I", data[:4])
                self.callback(word)


# ------------------------------- UDP helper ------------------------------ #
class UdpSender:
    def __init__(self, host: str, port: int):
        self.addr = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    def send(self, msg):
        data = msg if isinstance(msg, (bytes, bytearray)) else str(msg).encode("utf-8")
        try:
            self.sock.sendto(data, self.addr)
        except OSError as e:
            print(f"UDP send failed to {self.addr}: {e}")
            return False
        # (optional) print feedback to console
        print(f"UDP -> {self.addr}: {data!r}")
        return True

class UdpWordReceiver:
    """
    Non-blocking UDP receiver that calls on_word(word32) for each 4-byte word.
    Designed to be polled via Tkinter .after().
    """
    def __init__(self, bind_host: str, bind_port: int, on_word: Callable[[int], None]):
        self.on_word = on_word
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((bind_host, bind_port))
        self.sock.setblocking(False)

    def poll(self, max_packets: int = 50) -> None:
        for _ in range(max_packets):
            try:
                data, _addr = self.sock.recvfrom(2048)
            except BlockingIOError:
                return
            if len(data) >= 4:
                (word,) = struct.unpack("!I", data[:4])
                self.on_word(word)

# ------------------------- Annunciator PushButton ------------------------- #
class AnnunciatorButton(tk.Frame):
    """
    Pushbutton with a thin annunciator bar on top.
    - Lights are program-controlled ONLY via set_color('off'|'green'|'amber'|'red')
    - Clicking the button calls on_press(payload) to send UDP (does NOT change light)
    """
    PALETTE = {"off":"#3a4048", "green":"#69e36f", "amber":"#ffbf3f", "red":"#ff5a5f"}

    def __init__(self, master, text="", width=72, on_press=None, payload=None, **kw):
        super().__init__(master, bg=kw.pop("bg", "#7ba0bd"), highlightthickness=0)
        self.on_press = on_press
        self.payload = payload

        self.canvas = tk.Canvas(self, height=10, width=width, bd=0, highlightthickness=0,
                                bg=self["bg"])
        self.canvas.pack(fill="x", side="top", padx=2, pady=(2,0))
        self._bar = self.canvas.create_rectangle(2, 2, width-2, 8,
                                                 fill=self.PALETTE["off"], width=0)

        self.btn = tk.Button(self, text=text, relief="raised", bd=2, takefocus=False,
                             command=self._clicked, font=("Helvetica", 9, "bold"))
        self.btn.pack(fill="x", side="top", padx=2, pady=(2,4))

    def _clicked(self):
        if self.on_press:
            self.on_press(self.payload)

    def set_color(self, name: str):
        name = name.lower()
        if name not in self.PALETTE:
            raise ValueError(f"Unknown color '{name}'")
        self.canvas.itemconfigure(self._bar, fill=self.PALETTE[name])

# ------------------------------- Knob Widget ------------------------------ #
class Knob(tk.Frame):
    """Display-only rotary indicator (no mouse input)."""
    def __init__(self, master, label="", steps=11, radius=26, value=0, **kw):
        super().__init__(master, bg=kw.pop("bg", "#7ba0bd"), highlightthickness=0)
        self.steps = max(2, steps)
        self.value = max(0, min(self.steps-1, int(value)))
        self.readout_var = tk.StringVar(value="0")
        size = radius*2 + 8

        self.canvas = tk.Canvas(self, width=size, height=size, bd=0,
                                highlightthickness=0, bg=self["bg"])
        self.canvas.pack()
        self._cx = self._cy = size//2
        self._r = radius

        self.canvas.create_oval(self._cx-radius, self._cy-radius,
                                self._cx+radius, self._cy+radius,
                                fill="#15181d", outline="#0e1116", width=3)
        self._line = self.canvas.create_line(self._cx, self._cy,
                                             self._cx, self._cy - radius + 6,
                                             width=3, capstyle="round", fill="#ffffff")

        if label:
            tk.Label(self, text=label, bg=self["bg"], fg="#0f1a23",
                     font=("Helvetica", 9, "bold")).pack(pady=(2,6))
        tk.Label(self, textvariable=self.readout_var, bg=self["bg"], fg="#0f1a23",
                 font=("Helvetica", 8, "bold")).pack(pady=(0, 4))
        self._update_indicator()

    def _update_indicator(self):
        t = self.value / float(self.steps - 1)  # 0..1

        ang = -135 + t * 270.0
        rad = ang * pi / 180.0
        x = self._cx + (self._r - 6) * (-sin(rad))
        y = self._cy + (self._r - 6) * (-cos(rad))
        self.canvas.coords(self._line, self._cx, self._cy, x, y)

    def set(self, value):
        v = max(0, min(self.steps-1, int(value)))
        if v != self.value:
            self.value = v
            self._update_indicator()

    def set_from_percent(self, pct):
        pct = max(0.0, min(100.0, float(pct)))
        step = round((self.steps - 1) * pct / 100.0)
        self.set(step)

    def set_readout(self, value):
        self.readout_var.set(str(value))

class KnobWithBar(tk.Frame):
    PALETTE = {"off":"#3a4048", "green":"#69e36f", "amber":"#ffbf3f", "red":"#ff5a5f"}

    def __init__(self, master, label="", steps=11, value=0, width=58, **kw):
        super().__init__(master, bg=kw.pop("bg", "#7ba0bd"), highlightthickness=0)
        self.canvas = tk.Canvas(self, height=10, width=width, bd=0, highlightthickness=0,
                                bg=self["bg"])
        self.canvas.pack(fill="x", side="top", padx=2, pady=(2, 0))
        self._bar = self.canvas.create_rectangle(2, 2, width-2, 8,
                                                 fill=self.PALETTE["off"], width=0)
        self.knob = Knob(self, label=label, steps=steps, value=value, bg=self["bg"])
        self.knob.pack(side="top")

    def set_color(self, name: str):
        name = name.lower()
        if name not in self.PALETTE:
            raise ValueError(f"Unknown color '{name}'")
        self.canvas.itemconfigure(self._bar, fill=self.PALETTE[name])

    def set(self, value):
        self.knob.set(value)

    def set_from_percent(self, pct):
        self.knob.set_from_percent(pct)

    def set_readout(self, value):
        self.knob.set_readout(value)

class ThreePositionVerticalSwitch(tk.Frame):
    """
    Airbus-style vertical 3-position switch:
        INT  (top label)
        (center unlabeled)
        RAD  (bottom label)

    Positions: "INT", "CENTER", "RAD"
    Adds coloring: INT -> green top, RAD -> green bottom, CENTER -> neutral.
    """
    def __init__(self, master, label="", initial="CENTER", on_change=None, **kw):
        super().__init__(master, bg=kw.pop("bg", "#7ba0bd"))
        self.on_change = on_change
        self._suppress = False

        # colors
        self._neutral_bg = "#94b2c8"
        self._selected_bg = "#c9d9e6"
        self._active_bg   = "#69e36f"  # "green" highlight for INT/RAD

        if label:
            tk.Label(self, text=label, bg=self["bg"],
                     font=("Helvetica", 9, "bold")).pack(pady=(0, 4))

        self.var = tk.StringVar(value=initial)

        tk.Label(self, text="INT", bg=self["bg"],
                 font=("Helvetica", 9, "bold")).pack()

        body = tk.Frame(self, bg=self._neutral_bg, bd=2, relief="ridge")
        body.pack(pady=2)

        self._buttons = {}
        for pos in ("INT", "CENTER", "RAD"):
            rb = tk.Radiobutton(
                body,
                variable=self.var,
                value=pos,
                indicatoron=False,
                width=6,
                height=1,
                text="" if pos == "CENTER" else " ",
                command=self._changed,
                bg=self._neutral_bg,
                activebackground=self._neutral_bg,
                selectcolor=self._selected_bg,
                relief="raised",
                bd=1
            )
            rb.pack(fill="x")
            self._buttons[pos] = rb

        tk.Label(self, text="RAD", bg=self["bg"],
                 font=("Helvetica", 9, "bold")).pack()

        # Apply initial visuals
        self._apply_visuals()

    def _apply_visuals(self):
        """Update segment colors according to current position."""
        v = self.var.get()

        # reset all to neutral
        for pos, rb in self._buttons.items():
            rb.configure(bg=self._neutral_bg, activebackground=self._neutral_bg)

        # highlight the selected end position
        if v == "INT":
            self._buttons["INT"].configure(bg=self._active_bg, activebackground=self._active_bg)
        elif v == "RAD":
            self._buttons["RAD"].configure(bg=self._active_bg, activebackground=self._active_bg)
        # CENTER => nothing highlighted

    def _changed(self):
        self._apply_visuals()
        if self._suppress:
            return
        if self.on_change:
            self.on_change(self.var.get())

    def set(self, value, trigger=False):
        """Programmatic update (e.g., from ARINC/UDP)."""
        self._suppress = not trigger
        self.var.set(value)
        self._apply_visuals()
        self._suppress = False

    def get(self):
        return self.var.get()


# ------------------------------ Panel Layout ------------------------------ #
class ACPDemo(tk.Tk):
    PANEL_BG = "#7ba0bd"

    def __init__(self, udp: UdpSender):
        super().__init__()
        self.udp = udp
        self.calls_state = 0
        self.switches = {}
        self.title("Audio Control Panel — UDP buttons, program-driven lights/knobs")
        self.configure(bg=self.PANEL_BG)

        outer = tk.Frame(self, bg=self.PANEL_BG, bd=8)
        outer.pack(padx=8, pady=8)

        # Row 0: CALL/MECH/ATT buttons (send UDP only)
        top = tk.Frame(outer, bg=self.PANEL_BG); top.grid(row=0, column=0, sticky="ew")
        self.calls = []
        names = [("CALL", "TOP_CALL_1"), ("CALL", "TOP_CALL_2"), ("CALL", "TOP_CALL_3"),
                 ("CALL", "TOP_CALL_4"), ("CALL", "TOP_CALL_5"),
                 ("MECH", "TOP_MECH"), ("ATT", "TOP_ATT")]
        for col, (label, key) in enumerate(names):
            ab = AnnunciatorButton(
                top, text=label, width=64,
                on_press=lambda _payload=None, bit=CALL_BITS[key]: self.toggle_call_bit(bit),
                payload=None  # unused; lambda closes over key
            )
            ab.grid(row=0, column=col, padx=6, pady=4)
            self.calls.append(ab)

        # Row 1: upper knobs (display-only)
        row1 = tk.Frame(outer, bg=self.PANEL_BG); row1.grid(row=1, column=0, pady=(2, 0))
        labels1 = ["VHF1","VHF2","VHF3","HF1","HF2","INT","CAB"]
        self.knobs1 = []
        for col, lab in enumerate(labels1):
            k = Knob(row1, label=lab, steps=12, value=0)
            k.grid(row=0, column=col, padx=10)
            self.knobs1.append(k)

        # Row 2: center block (three UDP buttons + one knob)
        center = tk.Frame(outer, bg=self.PANEL_BG); center.grid(row=2, column=0, pady=(0, 2))

        self.switches["MODE"] = ThreePositionVerticalSwitch(
            center,
            label="MODE",
            initial="CENTER",
            on_change=lambda v: self.udp.send(f"switch MODE {v}")
        )
        self.switches["MODE"].grid(row=0, column=0, padx=(0, 0))

        self.center_buttons = {
            "ON VOICE": AnnunciatorButton(center, text="ON\nVOICE", width=68,
                                          on_press=lambda _payload=None: self.udp.send(PAYLOADS["ON_VOICE"])),
            "RESET":    AnnunciatorButton(center, text="RESET",    width=68,
                                          on_press=lambda _payload=None: self.clear_call_bits()),
            "CALL":     AnnunciatorButton(center, text="CALL",     width=68,
                                          on_press=lambda _payload=None: self.udp.send(PAYLOADS["CENTER_CALL"])),
        }
        self.center_buttons["ON VOICE"].grid(row=0, column=1, padx=8)
        self.center_buttons["RESET"].grid(row=0, column=2, padx=8)
        self.center_buttons["CALL"].grid(row=0, column=3, padx=8)

        self.center_knob = KnobWithBar(center, label="PA", steps=12, value=0, width=58)
        self.center_knob.grid(row=0, column=4, padx=(16, 0))


        # Row 3: lower knobs (display-only)
        row3 = tk.Frame(outer, bg=self.PANEL_BG); row3.grid(row=3, column=0, pady=(6, 0))
        labels2 = ["VOR1","VOR2","MKR","ILS","MLS","ADF1","ADF2"]
        self.knobs2 = []
        for col, lab in enumerate(labels2):
            k = KnobWithBar(row3, label=lab, steps=12, value=0)
            k.grid(row=0, column=col, padx=10)
            self.knobs2.append(k)

        # Example: program-driven updates for lights/knobs (remove/replace as needed)
        #self.after(400, self.demo_updates)
        self.rx = UdpWordReceiver("0.0.0.0", 5001, self.on_arinc_word)
        self.after(10, self._poll_rx)

    def _poll_rx(self):
        self.rx.poll()
        self.after(10, self._poll_rx)

    def send_call_bits(self):
        self.udp.send(struct.pack("!IB", UDP_CMD_MAGIC, self.calls_state & 0x7F))

    def toggle_call_bit(self, bit: int):
        self.calls_state ^= 1 << bit
        self.calls[bit].set_color("green" if self.calls_state & (1 << bit) else "off")
        self.send_call_bits()

    def clear_call_bits(self):
        self.calls_state = 0
        for call in self.calls:
            call.set_color("off")
        self.send_call_bits()

    def on_arinc_word(self, word: int):
        decoded = decode_arinc429(word)

        if not decoded["parity_ok"]:
            return

        key = (decoded["label"], decoded["sdi"])
        if key in ACP_KNOB_MAP:
            self.apply_acp_knob_state(key, decoded["data"])
            return

        if decoded["label"] != 0o210:
            return

        acp = decode_acp_data(decoded["data"])
        self.apply_acp_state(acp)

    def apply_acp_knob_state(self, key: tuple[int, int], data: int):
        row, idx = ACP_KNOB_MAP[key]
        knob = decode_acp_knob_data(data)
        if row == "top":
            self.calls[idx].set_color("green" if knob["pulled"] else "off")
            self.knobs1[idx].set_from_percent((255 - knob["volume"]) / 255.0 * 100.0)
            self.knobs1[idx].set_readout(knob["volume"])
        elif row == "bottom":
            self.knobs2[idx].set_color("green" if knob["pulled"] else "off")
            self.knobs2[idx].set_from_percent((255 - knob["volume"]) / 255.0 * 100.0)
            self.knobs2[idx].set_readout(knob["volume"])
        elif row == "center":
            self.center_knob.set_color("green" if knob["pulled"] else "off")
            self.center_knob.set_from_percent((255 - knob["volume"]) / 255.0 * 100.0)
            self.center_knob.set_readout(knob["volume"])

    def apply_acp_state(self, acp: dict):
        # VOICE annunciator
        self.center_buttons["ON VOICE"].set_color(
            "red" if acp["VOICE"] else "off"
        )

        # RESET annunciator
        self.center_buttons["RESET"].set_color(
            "red" if acp["RESET"] else "off"
        )

        # Update MODE switch from state (no UDP echo back)
        if acp["INT"]:
            self.switches["MODE"].set("INT", trigger=False)
        elif acp["RAD"]:
            self.switches["MODE"].set("RAD", trigger=False)
        else:
            self.switches["MODE"].set("CENTER", trigger=False)

        # INT / RAD → top row indicators (example mapping)
        #self.calls[5].set_color("green" if acp["INT"] else "off")  # MECH
        #self.calls[6].set_color("green" if acp["RAD"] else "off")  # ATT

    # --------- program-controlled indicators (example; replace/remove) --------- #
    def demo_updates(self):
        # Set top lights
        self.calls[0].set_color("green")
        self.calls[5].set_color("amber")
        # Center button lights
        self.center_buttons["ON VOICE"].set_color("green")
        self.center_buttons["RESET"].set_color("off")
        self.center_buttons["CALL"].set_color("off")
        # Knobs
        for k, pct in zip(self.knobs1, [0, 20, 35, 55, 70, 85, 100]): k.set_from_percent(pct)
        for k, step in zip(self.knobs2, [0, 2, 4, 6, 8, 10]): k.set(step)
        self.center_knob.set_from_percent(60)
        # Reschedule demo if you want continuous animation; otherwise remove:
        self.after(1500, self.demo_updates)

# --------------------------------- main ----------------------------------- #
if __name__ == "__main__":
    udp = UdpSender(UDP_HOST, UDP_PORT)
    app = ACPDemo(udp)
    app.mainloop()
