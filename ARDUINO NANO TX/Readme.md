# ARINC 429 Transmitter Using Arduino (AVR-C Implementation)

This project demonstrates how to build a **basic ARINC 429 transmitter** using an Arduino that generates a **SPI-like signal**.  
The transmitted waveform has correct rise and fall times and can drive an **ARINC 429 differential bus** through a simple hardware chain.

---

## 🧩 Overview

The transmission logic is implemented entirely in **pure AVR-C**, without relying on the Arduino framework.  
However, the program can be compiled and uploaded using the Arduino toolchain.

The signal chain is:

Arduino (USART in SPI mode)  
↓  
74HC00 NAND network  
↓   
HI-8586 line driver (Holt)   
↓   
ARINC 429 differential bus (A,B)


---

## ⚙️ Timing Control

ARINC 429 defines two standard data rates:
- **High-speed:** 100 kHz  
- **Low-speed:** 12.5 kHz  

The standard Arduino **SPI peripheral** can easily generate the **100 kHz** rate,  
but it cannot reach **12.5 kHz** because the 16 MHz system clock cannot be divided finely enough.

To solve this, the project uses the **USART in Synchronous (SPI) mode**, also known as **MSPIM**.  
In this configuration, the USART’s baud-rate generator acts as a flexible clock divider,  
allowing precise frequency control for both ARINC data rates.

The rising and falling times are managed by the HI-8586 SLP1.5 input pin. The HIGH or LOW states change the    
rising and falling times. In the `main.c` file the SLP1.5 is connected to PD5, and managed    
inside the `usart0_mspim_init` function.

---

## 🧰 Hardware Requirements

| Component | Purpose |
|------------|----------|
| **Arduino Nano** | Main controller (ATmega328P @ 16 MHz) |
| **74HC00** | Generates complementary signals for differential input |
| **Holt HI-8586** | ARINC 429 line driver |
| **1 nF capacitor (optional)** | Low-pass filtering or edge-shaping across differential lines |

---

## ⚡ USART Clock Pin (XCK)

The **XCKn** pin outputs the synchronous clock used by the MSPIM mode.  
Not all Arduino boards expose this pin:

| Board | XCK Pin | Available? |
|--------|----------|------------|
| **Arduino Uno** | D8 (PB0) | ✅ Yes |
| **Arduino Nano** | D8 (PB0) | ✅ Yes |
| **Arduino Micro** | D9 (PD5) | ✅ Yes |
| **Arduino Mega 2560** | PE2 / PD5 / PH2 / PJ2 | ❌ Not routed to headers |

---

## 🧠 Software Notes

- Written in **pure AVR-C** for maximum control over timing and registers.
- Uses the **USART in MSPIM mode** (`UMSELn1:0 = 11`) to generate a synchronous clock.
- Baud-rate generator (`UBRRn`) determines the ARINC bit rate:
  - `UBRR = 639` → 12.5 kHz
  - `UBRR = 79`  → 100 kHz
- The transmitted data follows ARINC 429 word structure with correct bit ordering.

---

## 🧪 Usage

1. Connect your Arduino Nano to the 74HC00 and HI-8586 as shown in the schematic found in the Le Labo de Michel YouTube Video https://www.youtube.com/watch?v=Smht2AQNsMY (min 13:47). 
2. For the HI-8586 dual rail power supply (-5V, +5V) a cheap small Aliexpress dual rail board will do the job just fine.  
3. In the `main.c` choose the transmission speed by changing the function `usart0_mspim_init(UBRR)`:
   - **Low speed (12.5 kHz):** set `UBRR = 639`
   - **High speed (100 kHz):** set `UBRR = 79`
4. In the `main.c` adjust the ARINC message using the `arinc429_send` function.
5. Observe the ARINC 429 signal on the HI-8586 output differential lines A, B.

---

## ⚠️ Safety Disclaimer   

This project is not designed, certified, or approved for use in real aircraft or aviation systems.    
It is intended solely for educational purposes, bench experiments, and non-professional flight simulation setups.    
Do not use this hardware or software in any operational avionics equipment.   

---

## 📄 License
This project is released under the MIT License.  
Feel free to use, modify, and adapt it to interface sims avionics or educational applications. 

---

## 💙 Support the Project

If you find this project useful, consider supporting it with a small donation.  
Your contribution helps cover development time, testing equipment, and future improvements.

[![Donate](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://www.paypal.com/donate/?hosted_button_id=3K2V8JSVAMUHJ)

## ✍️ Author
**Francesco Talotta** 
