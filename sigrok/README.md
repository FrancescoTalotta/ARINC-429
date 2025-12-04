# ARINC-429 Sigrok Decoder

A custom **Sigrok protocol decoder for ARINC 429**, designed to interpret bipolar-RZ ARINC signals captured by any Sigrok-compatible logic analyzer.  
It reconstructs complete 32-bit ARINC words and displays all standard fields directly inside PulseView.

---

## ✨ Features

- **Full ARINC-429 decoding** (Label, SDI, Data, SSM, Parity)  
- Supports **high-speed (100 kHz)** and **low-speed (12.5 kHz)**  
- Reconstructs bits from **bipolar RZ** signal timing  
- Automatically detects **word boundaries**  
- Validates **odd parity** and flags errors  
- Provides annotations in **octal, hex, and binary**  
- Works with any Sigrok-compatible logic analyzer (Saleae, FX2, DSLogic, Logic Pirate, etc.)

---

## 🛠 How It Works

The decoder analyses signal transitions to turn the ARINC 429 bipolar-RZ waveform into logical 0/1 bits.  
When 32 bits are collected, the decoder formats the word according to the ARINC standard:

1. Extract **Label** (bits 1–8) → displayed in *octal*  
2. Decode **SDI** (bits 9–10)  
3. Parse **19-bit Data** → shown in hex  
4. Interpret **SSM** (bits 30–31)  
5. Check **Odd Parity** (bit 32)  

PulseView then displays a clear annotation on the waveform, making it easy to verify the decoded ARINC fields.

---
