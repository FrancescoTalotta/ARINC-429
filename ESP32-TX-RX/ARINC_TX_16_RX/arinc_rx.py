"""Passive RX protocol, bounded UDP ingestion, and Tk-independent display model."""

from dataclasses import dataclass
from datetime import datetime
import queue
import re
import socket
import threading


DEFAULT_RX_PORT = 5004
RX_QUEUE_SIZE = 4096
_RECORD = re.compile(
    r"rx count=([0-9]{1,20}) raw=0x([0-9a-fA-F]{8}) "
    r"label=([0-3][0-7]{2}) sdi=([0-3]) "
    r"data=0x([0-7][0-9a-fA-F]{4}) ssm=([0-3]) parity=([01]) "
    r"dropped=([0-9]{1,20})"
)


@dataclass(frozen=True)
class RxRecord:
    count: int
    raw: int
    label: int
    sdi: int
    data: int
    ssm: int
    parity: int
    dropped: int

    @property
    def bits(self) -> str:
        # Firmware has already decoded data numerically; do not reverse it again.
        return f"{self.data:019b}"

    @property
    def key(self) -> tuple[int, int]:
        return self.label, self.sdi


def parse_record(text: str) -> RxRecord:
    match = _RECORD.fullmatch(text)
    if match is None:
        raise ValueError("Malformed RX record")
    bases = (10, 16, 8, 10, 16, 10, 10, 10)
    values = [int(value, base) for value, base in zip(match.groups(), bases)]
    if any(values[index] > 0xFFFFFFFFFFFFFFFF for index in (0, 7)):
        raise ValueError("RX counter exceeds uint64")
    return RxRecord(*values)


def parse_datagram(data: bytes) -> tuple[list[RxRecord], int]:
    """Return valid lines and malformed-line count, isolating bad ASCII lines."""
    records = []
    malformed = 0
    for line in data.split(b"\n"):
        if line.endswith(b"\r"):
            line = line[:-1]
        if not line:
            continue
        try:
            records.append(parse_record(line.decode("ascii")))
        except (UnicodeDecodeError, ValueError):
            malformed += 1
    return records, malformed


@dataclass(frozen=True)
class RxRow:
    record: RxRecord
    count: int
    last_seen: datetime

    @property
    def values(self) -> tuple:
        r = self.record
        return (f"{r.label:03o}", r.sdi, r.data, f"0x{r.data:05X}", r.bits,
                r.ssm, r.parity, self.count, self.last_seen.strftime("%H:%M:%S.%f")[:-3],
                f"0x{r.raw:08X}")


class RxModel:
    """Main-thread state, bounded to 256 labels x 4 SDIs.

    Counts are observed unique records, not inferred per-label firmware captures.
    Gaps are missing global sequence numbers and can include ISR/UDP/GUI losses.
    A subscription starts a new session; clear only resets rows, never diagnostics.
    Late/repeated sequence numbers are ignored, including after a firmware reboot:
    re-subscribe after reboot to establish a fresh sequence baseline.
    """

    def __init__(self) -> None:
        self.rows: dict[tuple[int, int], RxRow] = {}
        self.display_rows: dict[tuple[int, int], RxRow] = {}
        self.paused = False
        self.received = 0
        self.sequence_gaps = 0
        self.repeated_or_late = 0
        self.last_count: int | None = None
        self.firmware_dropped = 0

    def ingest(self, record: RxRecord, last_seen: datetime) -> None:
        if self.last_count is not None:
            if record.count <= self.last_count:
                self.repeated_or_late += 1
                return
            self.sequence_gaps += record.count - self.last_count - 1
        self.last_count = record.count
        self.firmware_dropped = record.dropped
        self.received += 1
        previous = self.rows.get(record.key)
        row = RxRow(record, previous.count + 1 if previous else 1, last_seen)
        self.rows[record.key] = row
        if not self.paused:
            self.display_rows[record.key] = row

    def set_paused(self, paused: bool) -> None:
        self.paused = paused
        if not paused:
            self.display_rows = self.rows.copy()

    def clear(self) -> None:
        self.rows.clear()
        self.display_rows.clear()

    def visible_rows(self, label: str = "", sdi: str = "") -> list[RxRow]:
        """Exact octal label / SDI filters; empty means all."""
        label = label.strip()
        sdi = sdi.strip()
        if label and re.fullmatch(r"[0-7]{1,3}", label) is None:
            raise ValueError("Label filter must be 000..377 octal, or blank")
        label_value = int(label, 8) if label else None
        if label_value is not None and label_value > 0o377:
            raise ValueError("Label filter must be 000..377 octal, or blank")
        if sdi and sdi not in ("0", "1", "2", "3"):
            raise ValueError("SDI filter must be 0..3, or blank")
        return [row for key, row in sorted(self.display_rows.items())
                if (label_value is None or key[0] == label_value)
                and (not sdi or key[1] == int(sdi))]


class RxListener:
    """One worker per socket. Never calls Tk; newest records drop on overflow."""

    def __init__(self, port: int, source_ip: str, capacity: int = RX_QUEUE_SIZE) -> None:
        if not 0 <= port <= 65535 or capacity < 1:
            raise ValueError("Invalid listener port or queue capacity")
        self.records: queue.Queue[tuple[RxRecord, datetime]] = queue.Queue(capacity)
        self.source_ip = source_ip
        self.stop_event = threading.Event()
        self.lock = threading.Lock()
        self.queue_drops = 0
        self.malformed = 0
        self.error = ""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.sock.settimeout(0.2)
            self.sock.bind(("0.0.0.0", port))
        except Exception:
            self.sock.close()
            raise
        self.port = self.sock.getsockname()[1]
        self.thread = threading.Thread(target=self._run, name="arinc-rx", daemon=True)

    def start(self) -> None:
        self.thread.start()

    def ingest_datagram(self, data: bytes) -> None:
        records, malformed = parse_datagram(data)
        drops = 0
        seen = datetime.now()
        for record in records:
            try:
                self.records.put_nowait((record, seen))
            except queue.Full:
                drops += 1
        with self.lock:
            self.queue_drops += drops
            self.malformed += malformed

    def stats(self) -> tuple[int, int, str]:
        with self.lock:
            return self.queue_drops, self.malformed, self.error

    def _run(self) -> None:
        try:
            while not self.stop_event.is_set():
                try:
                    data, address = self.sock.recvfrom(65535)
                except socket.timeout:
                    continue
                if address[0] == self.source_ip:
                    self.ingest_datagram(data)
        except OSError as exc:
            if not self.stop_event.is_set():
                with self.lock:
                    self.error = str(exc)
        finally:
            self.sock.close()

    def close(self) -> None:
        self.stop_event.set()
        self.sock.close()
        if self.thread.ident is not None:
            self.thread.join(timeout=1)
