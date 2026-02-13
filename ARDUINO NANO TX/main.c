#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>

/* USART0 pins on ATmega2560:
   XCK0 = PD4 (clock out)
   TXD0 = PE1 (data out)
   RXD0 = PE0 (unused) */


static void usart0_mspim_init(uint16_t ubrr)
{
    // XCK0, TXD0 outputs; RXD0 input
    DDRD |= _BV(PD4);
    DDRD |= _BV(PD5);

    UCSR0B = 0;  // disable TX/RX while configuring

    // UMSEL01:0 = 11 (MSPIM), UCPOL0=1 (idle HIGH), 8-bit (UCSZ01:0=11)
    UCSR0C = (1<<UMSEL01) | (1<<UMSEL00)   // MSPIM mode
           | (0<<UCPOL0)                   // CPOL=0  -> clock idles LOW
           | (1<<UCSZ01)  | (1<<UCSZ00);   // UCSZ1=1 -> MBS first, UCSZ0 -> leading or tailing


    UCSR0B = (1<<TXEN0);          // enable transmitter (RX optional)

    UBRR0L = ubrr;                //set the baud, have to split it into the two registers
    UBRR0H = (ubrr >> 8);         //high end of baud register

    PORTD &= ~(1<<PD5);      //HIGH  Speed ARINC
    //PORTD |=  (1<<PD5);        //LOW Speed ARINC 

}


static void mspim_tx_bytes(const uint8_t *p, uint8_t n)
{

    // Clear TXC before starting a new frame train
    UCSR0A = (1<<TXC0);

    // Prime first byte
    while (!(UCSR0A & (1<<UDRE0))) { }
    UDR0 = *p++;

    // Feed the rest as soon as the data register empties
    while (--n) {
        while (!(UCSR0A & (1<<UDRE0))) { }
        UDR0 = *p++;
    }

    // Wait for the last byte to fully shift out (no gap before next call)
    while (!(UCSR0A & (1<<TXC0))) { }
}


/* ---------- ARINC 429 helpers ---------- */
// Same helper used in ESP32 RMT_ARINC.c
static inline uint32_t reverse_bits(uint32_t x, uint8_t n, uint32_t pad_right)
{
    uint32_t r = 0;
    for (uint8_t i = 0; i < n; i++) {
        r <<= 1;
        r |= (x >> i) & 1U;
    }
    return (r >> pad_right);
}

// Same parity function used in ESP32 RMT_ARINC.c
static uint8_t ARINC429_ComputeMSBParity(uint32_t X)
{
    uint8_t parity = 0;
    uint32_t mask = 1U;

    for (uint8_t i = 0; i < 32; i++, mask <<= 1) {
        if (X & mask) {
            parity++;
        }
    }
    return (uint8_t)((parity + 1U) & 1U);
}

static uint8_t ARINC429_Permute8Bits(uint8_t X)
{
    uint8_t result = 0;

    if (X & 0x80) result |= 0x01;
    if (X & 0x40) result |= 0x02;
    if (X & 0x20) result |= 0x04;
    if (X & 0x10) result |= 0x08;
    if (X & 0x08) result |= 0x10;
    if (X & 0x04) result |= 0x20;
    if (X & 0x02) result |= 0x40;
    if (X & 0x01) result |= 0x80;

    return result;
}

// Same word builder used in ESP32 RMT_ARINC.c
static uint32_t arinc429_send(uint8_t label, uint8_t sdi, uint32_t data19, uint8_t ssm)
{
    uint8_t lbl = ARINC429_Permute8Bits(label);

    uint32_t w31 = ((uint32_t)lbl << 24)
                 | ((uint32_t)sdi   << 22)
                 | ((uint32_t)data19 << 3)
                 | ((uint32_t)ssm   << 1);

    return (w31 & 0xFFFFFFFEUL) | ((uint32_t)ARINC429_ComputeMSBParity(w31));
}

static void send_altimeter_0204(uint16_t alt_ft)
{
    uint32_t data19 = reverse_bits((uint32_t)alt_ft, 19, 1);
    uint32_t word = arinc429_send(0204, 0, data19, 3);

    uint8_t buf[4] = {
        (uint8_t)(word >> 24),
        ARINC429_Permute8Bits((uint8_t)(word >> 16)),
        ARINC429_Permute8Bits((uint8_t)(word >> 8)),
        ARINC429_Permute8Bits((uint8_t)word)
    };

    mspim_tx_bytes(buf, 4);
}



int main(void)
{
    // Exact SCK = 12.5 kHz (UBRR=639)
    //usart0_mspim_init(639);
  
    // Switch to exact 100 kHz (UBRR=79)
    usart0_mspim_init(79);



    const uint8_t repeats_per_step = 100; // 100 * 50ms = 5.0s per altitude step

    for(;;) {
        for (uint16_t alt_ft = 0; alt_ft <= 5000; alt_ft += 500) {
            for (uint8_t i = 0; i < repeats_per_step; i++) {
                send_altimeter_0204(alt_ft);
                _delay_ms(50);
            }
        }

        for (int16_t alt_ft = 4500; alt_ft >= 0; alt_ft -= 500) {
            for (uint8_t i = 0; i < repeats_per_step; i++) {
                send_altimeter_0204((uint16_t)alt_ft);
                _delay_ms(50);
            }
        }
    }
}
