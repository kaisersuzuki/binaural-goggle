/* Host-side stand-in for <xc.h>, used only by host_test.c.
   It models just enough of the dsPIC30F4013 registers to run the
   firmware logic on a PC. It is NOT used when building for the chip.
   Everything is static: it is included by exactly one source file. */
#ifndef MOCK_XC_H
#define MOCK_XC_H

#include <stdint.h>

#define __attribute__(x)
#define _FOSC(x)
#define _FWDT(x)
#define _FBORPOR(x)
#define _FGS(x)
#define Nop() ((void)0)
#define __builtin_mulss(a, b) ((int32_t)(int16_t)(a) * (int32_t)(int16_t)(b))

#define BITS16(n) struct { unsigned n##0:1, n##1:1, n##2:1, n##3:1, n##4:1, \
    n##5:1, n##6:1, n##7:1, n##8:1, n##9:1, n##10:1, n##11:1, n##12:1, \
    n##13:1, n##14:1, n##15:1; }
#define PORT(n) static union { uint16_t reg; BITS16(n) bits; } n##_u

PORT(LATA);  PORT(LATB);  PORT(LATC);  PORT(LATD);  PORT(LATF);
PORT(TRISA); PORT(TRISB); PORT(TRISC); PORT(TRISD); PORT(TRISF);

#define LATA LATA_u.reg
#define LATAbits LATA_u.bits
#define LATB LATB_u.reg
#define LATBbits LATB_u.bits
#define LATC LATC_u.reg
#define LATCbits LATC_u.bits
#define LATD LATD_u.reg
#define LATF LATF_u.reg
#define LATFbits LATF_u.bits
#define TRISA TRISA_u.reg
#define TRISAbits TRISA_u.bits
#define TRISB TRISB_u.reg
#define TRISC TRISC_u.reg
#define TRISCbits TRISC_u.bits
#define TRISD TRISD_u.reg
#define TRISF TRISF_u.reg

static uint16_t ADPCFG, T2CON, TMR2, PR2, T3CON, OC1R, OC1RS, OC2R, OC2RS,
                OC1CON, OC2CON, U1BRG, U1MODE, U1STA;
static struct { unsigned TON:1; } T2CONbits;
static struct { unsigned T2IF:1, U1RXIF:1; } IFS0bits;
static struct { unsigned T2IE:1, U1RXIE:1; } IEC0bits;
static struct { unsigned T2IP:3; } IPC1bits;
static struct { unsigned UTXEN:1, UTXBF:1, TRMT:1, URXDA:1, FERR:1,
                PERR:1, OERR:1; } U1STAbits = { 0, 0, 1, 0, 0, 0, 0 };

/* Timer3 advances on every read so the firmware's delay loops finish. */
static uint16_t mock_tmr3_v;
#define TMR3 (mock_tmr3_v += 97)

/* Reading the receive register returns the byte the test queued. */
static uint8_t mock_rx_byte;
#define U1RXREG (U1STAbits.URXDA = 0, mock_rx_byte)

/* Writes to the transmit register are captured, and counted as an
   error if they happen while the transmitter is disabled. */
static uint8_t  mock_tx[4096];
static unsigned mock_tx_n, mock_tx_while_disabled;
#define U1TXREG (*(mock_tx_while_disabled += !U1STAbits.UTXEN, \
                   &mock_tx[mock_tx_n++ & 4095]))

#endif
