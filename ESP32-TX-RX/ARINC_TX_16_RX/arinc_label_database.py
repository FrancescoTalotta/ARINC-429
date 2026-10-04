from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class ArincLabelEntry:
    label: str
    name: str
    encoding: str
    units: str = ""
    resolution: str = ""
    range_text: str = ""
    default_data19: int = 0
    default_ssm: int = 0
    notes: str = ""


def _entry(
    label: str,
    name: str,
    encoding: str,
    units: str = "",
    resolution: str = "",
    range_text: str = "",
    default_data19: int = 0,
    default_ssm: int | None = None,
    notes: str = "",
) -> ArincLabelEntry:
    encoding = encoding.upper()
    if default_ssm is None:
        if encoding == "BNR":
            default_ssm = 3
        elif encoding in ("BCD", "DSC"):
            default_ssm = 0
        else:
            default_ssm = 0
    return ArincLabelEntry(
        label=label,
        name=name,
        encoding=encoding,
        units=units,
        resolution=resolution,
        range_text=range_text,
        default_data19=default_data19 & 0x7FFFF,
        default_ssm=default_ssm & 0x03,
        notes=notes,
    )


# Sources: GAMA Publication No. 11 v6.0 and the luktronics aviologic-binaries
# ARINC standard labels wiki. Duplicate labels can exist for different LRUs; this
# database keeps the most generally useful simulator default for each octal label.
ARINC_LABELS: tuple[ArincLabelEntry, ...] = (
    _entry("001", "Distance to go", "BCD", "NM", "0.1", "+/-3999.9"),
    _entry("002", "Time to go", "BCD", "min", "0.1", "0-399.9"),
    _entry("010", "Latitude", "BCD", "deg", "1", "0-90"),
    _entry("011", "Longitude", "BCD", "deg", "1", "0-180"),
    _entry("012", "Ground speed", "BCD", "kt", "0.1", "0-7999.9"),
    _entry("013", "True track angle", "BCD", "deg", "1", "0-359"),
    _entry("014", "Magnetic heading", "BCD", "deg", "1", "0-359"),
    _entry("015", "Wind speed", "BCD", "kt", "1", "0-799"),
    _entry("016", "Wind direction", "BCD", "deg", "1", "0-359"),
    _entry("017", "Selected runway heading", "BCD", "deg", "0.1", "0-359.9"),
    _entry("024", "Selected course 1", "BCD", "deg", "1", "0-359", notes="GAMA label 024G; bit 11 can be discrete"),
    _entry("027", "Selected course 2", "BCD", "deg", "1", "0-359"),
    _entry("030", "VHF COM frequency", "BCD", "MHz", "0.025", "118-135.975", notes="GAMA label 030G"),
    _entry("031", "Beacon transponder code", "BCD", "discrete", "", "", notes="GAMA label 031G"),
    _entry("032", "ADF frequency", "BCD", "kHz", "0.5", "190-1750"),
    _entry("033", "ILS frequency", "BCD", "MHz", "0.05", "108-111.95"),
    _entry("034", "VOR/ILS frequency", "BCD", "MHz", "0.05", "108-117.95", notes="Also used by ADC for baro correction in other contexts"),
    _entry("035", "DME frequency", "BCD", "MHz", "0.05", "108-135.95"),
    _entry("040", "UHF COM frequency", "BCD", "MHz", "0.025", "225-399.975"),
    _entry("041", "Set position latitude", "BCD", "deg:min", "0.1", "180N-180S"),
    _entry("042", "Set position longitude", "BCD", "deg:min", "0.1", "180E-180W"),
    _entry("043", "Set magnetic heading", "BCD", "deg", "1", "0-359"),
    _entry("044", "True heading", "BCD", "deg", "1", "0-359"),
    _entry("060", "Omega data select", "BNR", "discrete"),
    _entry("061", "Covariance data", "BNR"),
    _entry("074", "Data record header", "DSC", "discrete"),
    _entry("075", "Active waypoint from/to data", "DSC", "discrete"),
    _entry("100", "Selected course 1", "BNR", "deg", "0.05", "+/-180"),
    _entry("101", "Selected heading", "BNR", "deg", "0.05", "+/-180"),
    _entry("102", "Selected altitude", "BNR", "ft", "1", "0-65536"),
    _entry("103", "Selected airspeed", "BNR", "kt", "0.25", "60-400"),
    _entry("104", "Selected vertical speed", "BNR", "ft/min", "16", "+/-6000"),
    _entry("105", "Selected runway heading", "BNR", "deg", "0.1", "+/-180"),
    _entry("106", "Selected Mach", "BNR", "Mach", "0.001", "0.10-0.92"),
    _entry("110", "Selected course 2", "BNR", "deg", "0.05", "+/-180"),
    _entry("111", "Test word A", "BNR"),
    _entry("113", "Message checksum", "BNR"),
    _entry("114", "Desired track true", "BNR", "deg", "0.05", "+/-180"),
    _entry("115", "Waypoint bearing true", "BNR", "deg", "0.05", "+/-180"),
    _entry("116", "Cross track distance", "BNR", "NM", "0.004", "+/-128"),
    _entry("117", "Vertical deviation", "BNR", "ft", "1", "+/-16384"),
    _entry("121", "Horizontal command to autopilot", "BNR", "deg", "0.01", "+/-180"),
    _entry("122", "Vertical command to autopilot", "BNR", "deg", "0.05", "+/-180"),
    _entry("123", "Throttle command", "BNR", "deg/sec", "0.001", "+/-2.56"),
    _entry("125", "Greenwich mean time", "BCD", "hr:min", "0.1 min", "0-23:59.9"),
    _entry("137", "Flap angle", "BNR"),
    _entry("147", "Magnetic variation", "BNR", "deg", "0.05", "+/-180"),
    _entry("150", "Greenwich mean time", "BNR", "hr:min:sec", "1 sec", "0-23:59:59"),
    _entry("157", "Normalized angle of attack", "BNR", "stall ratio", "0.0005", "+/-2"),
    _entry("162", "ADF bearing", "BNR", "deg", "0.05", "+/-180"),
    _entry("163", "Wind on nose", "BNR", "kt", "0.5", "+/-256"),
    _entry("164", "Radio altitude", "BNR", "ft", "0.1245", "0-2500"),
    _entry("165", "Radio altitude", "BCD", "ft", "0.1", "0-7999.9"),
    _entry("173", "Localizer deviation", "BNR", "DDM", "0.0001", "+/-0.4"),
    _entry("174", "Glideslope deviation", "BNR", "DDM", "0.0002", "+/-0.8"),
    _entry("201", "DME distance display", "BCD", "NM", "0.01", "0-799.99"),
    _entry("202", "DME distance", "BNR", "NM", "0.00391", "0-512"),
    _entry("203", "Pressure altitude QNE", "BNR", "ft", "1", "+/-131072"),
    _entry("204", "Baro corrected altitude 1", "BNR", "ft", "1", "+/-131072"),
    _entry("205", "Mach number", "BNR", "Mach", "0.0625", "0-0.99 typical"),
    _entry("206", "Calibrated airspeed", "BNR", "kt", "0.0625", "50-450"),
    _entry("207", "Maximum operating speed", "BNR", "kt", "0.25", "250-380"),
    _entry("210", "True airspeed", "BNR", "kt", "0.0625", "0-700"),
    _entry("211", "Total air temperature", "BNR", "deg C", "0.25", "-75 to +60"),
    _entry("212", "Altitude rate", "BNR", "ft/min", "16", "+/-6000"),
    _entry("213", "Static air temperature", "BNR", "deg C", "0.25", "-75 to +35"),
    _entry("215", "Impact pressure Qc", "BNR"),
    _entry("220", "Baro corrected altitude 2", "BNR", "ft", "1", "+/-131072"),
    _entry("221", "Angle of attack indicated", "BNR", "deg", "0.0439", "+/-180"),
    _entry("222", "VOR omnibearing / AoA probe 1", "BNR", "deg", "0.044", "+/-180"),
    _entry("223", "AoA probe 2", "BNR", "deg", "0.0439", "+/-180"),
    _entry("230", "True airspeed", "BCD", "kt", "1", "0-799"),
    _entry("231", "Total air temperature", "BCD", "deg C", "1", "+/-799"),
    _entry("233", "Static air temperature", "BCD", "deg C", "1", "+/-799"),
    _entry("234", "Baro correction source 1", "BCD", "mb", "1", "0-79999"),
    _entry("236", "Baro correction source 2", "BCD", "mb", "1", "0-79999"),
    _entry("241", "AoA corrected / normalized AoA", "BNR", "deg", "0.0439", "+/-180"),
    _entry("242", "Total pressure", "BNR", "mb", "0.03125", "0-2048"),
    _entry("246", "Static pressure corrected", "BNR", "mb", "0.03125", "0-2048"),
    _entry("251", "Distance to go / baro altitude 3", "BNR", "NM", "0.125", "0-4096"),
    _entry("252", "Time to go / baro altitude 4", "BNR", "min", "1", "0-512"),
    _entry("260", "Date", "BCD", "day", "1"),
    _entry("261", "GPS discrete word 1", "DSC", "discrete"),
    _entry("270", "General purpose discrete word 1", "DSC", "discrete"),
    _entry("271", "General purpose discrete word 2", "DSC", "discrete"),
    _entry("272", "General purpose discrete word 3", "DSC", "discrete"),
    _entry("273", "General purpose discrete word 4", "DSC", "discrete"),
    _entry("274", "General purpose discrete word 5", "DSC", "discrete"),
    _entry("275", "LRN status word", "DSC", "discrete"),
    _entry("276", "General purpose discrete word 7", "DSC", "discrete"),
    _entry("277", "Cabin display control discrete", "DSC", "discrete"),
    _entry("300", "Station declination, type and class", "BNR", "discrete"),
    _entry("301", "Message characters 7-9", "BNR", "discrete"),
    _entry("302", "Message characters 10-12", "BNR", "discrete"),
    _entry("303", "Message length/type/number", "BNR", "discrete"),
    _entry("304", "Message characters 1-3", "BNR", "discrete"),
    _entry("305", "Message characters 4-6", "BNR", "discrete"),
    _entry("306", "Nav/waypoint/airport latitude", "BNR", "deg", "0.000172", "180N-180S"),
    _entry("307", "Nav/waypoint/airport longitude", "BNR", "deg", "0.000172", "180E-180W"),
    _entry("310", "Present position latitude", "BNR", "deg", "0.000172", "180N-180S"),
    _entry("311", "Present position longitude", "BNR", "deg", "0.000172", "180E-180W"),
    _entry("312", "Ground speed", "BNR", "kt", "0.125", "0-4096"),
    _entry("313", "Track angle true", "BNR", "deg", "0.05", "+/-180"),
    _entry("314", "True heading", "BNR", "deg", "0.0055", "+/-180"),
    _entry("315", "Wind speed", "BNR", "kt", "1", "0-256"),
    _entry("316", "Wind angle true", "BNR", "deg", "0.7", "+/-180"),
    _entry("320", "Magnetic heading", "BNR", "deg", "0.0055", "+/-180"),
    _entry("321", "Drift angle", "BNR", "deg", "0.05", "+/-180"),
    _entry("324", "Pitch attitude", "BNR"),
    _entry("325", "Roll attitude / DME arc radius", "BNR"),
    _entry("326", "Angular rate / lateral scale factor", "BNR", "NM", "0.0039", "+/-128"),
    _entry("327", "Angular rate / vertical scale factor", "BNR", "ft", "0.0625", "+/-2048"),
    _entry("330", "Conic arc inbound course", "BNR", "deg", "0.0055", "+/-180"),
    _entry("331", "Conic arc radius", "BNR", "NM", "0.0078", "0-256"),
    _entry("332", "Conic arc course change angle", "BNR", "deg", "0.0055", "+/-180"),
    _entry("333", "Airport runway azimuth / body normal accel", "BNR"),
    _entry("334", "Airport runway length", "BNR", "ft", "1", "0-32768"),
    _entry("335", "Holding pattern azimuth", "BNR", "deg", "0.0055", "+/-180"),
    _entry("340", "Procedure turn azimuth", "BNR", "deg", "0.0055", "+/-180"),
    _entry("351", "Distance to destination", "BNR", "NM", "0.125", "0-32768"),
    _entry("352", "Estimated time to destination", "BNR", "min", "1", "0-4096"),
    _entry("353", "Destination local time offset", "BCD", "hr:min", "0.1 min", "23:59"),
    _entry("360", "Inertial vertical data", "BNR"),
    _entry("361", "Inertial vertical data", "BNR"),
    _entry("362", "Navigation-axis acceleration/velocity", "BNR"),
    _entry("363", "Navigation-axis acceleration/velocity", "BNR"),
    _entry("364", "Navigation-axis acceleration/velocity", "BNR"),
    _entry("365", "Inertial vertical speed", "BNR"),
    _entry("366", "Navigation-axis velocity", "BNR"),
    _entry("367", "Navigation-axis velocity", "BNR"),
    _entry("371", "General aviation equipment ID", "DSC", "discrete"),
    _entry("377", "Equipment identifier word", "DSC", "discrete"),
)

LABEL_BY_OCTAL = {entry.label: entry for entry in ARINC_LABELS}


def normalize_label(text: str) -> str:
    label = text.strip().upper().removesuffix("G").removesuffix("P")
    value = int(label, 8)
    if not 0 <= value <= 0o377:
        raise ValueError("label must be 000..377 octal")
    return f"{value:03o}"


def lookup_label(text: str) -> ArincLabelEntry | None:
    return LABEL_BY_OCTAL.get(normalize_label(text))


def search_labels(query: str) -> list[ArincLabelEntry]:
    query = query.strip().lower()
    if not query:
        return list(ARINC_LABELS)
    matches: list[ArincLabelEntry] = []
    for entry in ARINC_LABELS:
        haystack = " ".join(
            (
                entry.label,
                entry.name,
                entry.encoding,
                entry.units,
                entry.resolution,
                entry.range_text,
                entry.notes,
            )
        ).lower()
        if query in haystack:
            matches.append(entry)
    return matches
