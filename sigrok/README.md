# ARINC-429 Sigrok Decoder

A custom **Sigrok protocol decoder for ARINC 429**, designed to interpret bipolar-RZ ARINC signals captured by Sigrok logic analyzer.  
It reconstructs complete 32-bit ARINC words and displays all standard fields directly inside PulseView.

---

## ✨ Features

- **Full ARINC-429 decoding** (Label, SDI, Data, SSM, Parity)  
- Supports **high-speed (100 kHz)** and **low-speed (12.5 kHz)**  
- Reconstructs bits from **bipolar RZ** signal timing  
- Automatically detects **word boundaries**  
- Validates **odd parity**
---

## 🛠 How It Works

The decoder analyses signal transitions to turn the ARINC 429 bipolar-RZ waveform into logical 0/1 bits.  
When 32 bits are collected, the decoder formats the word according to the ARINC standard:

1. Extract **Label** (bits 1–8) → displayed in *octal*  
2. Decode **SDI** (bits 9–10)  
3. Parse **19-bit Data** → shown in hex  
4. Decode **SSM** (bits 30–31)  
5. Check **Odd Parity** (bit 32)  

PulseView then displays a clear annotation on the waveform, making it easy to verify the decoded ARINC fields.

---

## 💙 Support the Project

If you find this project useful, consider supporting it with a small donation.  
Your contribution helps cover development time, testing equipment, and future improvements.

[![Donate](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://www.paypal.com/donate/?hosted_button_id=3K2V8JSVAMUHJ)
