/*
 * ============================================================================
 * Binaural Beat + RGB LED Goggle Controller — dsPIC Firmware Rev 0.2
 * Target:   dsPIC30F4013 (PDIP-40)
 * Compiler: Microchip XC16 v2.x
 * Project:  https://github.com/<yourusername>/binaural-goggle
 * License:  CC BY-NC 4.0
 * ============================================================================
 *
 * HARDWARE PIN MAP
 *
 * Power:       VDD = +3.3V (MAX603 LDO)
 *              AVDD = +3.3V
 *              VSS/AVSS = GND
 *
 * Clock:       OSC1/OSC2 = 10 MHz crystal + 22pF caps + PLL×4 → Fcy = 20 MHz
 *
 * Audio out:   RD0 (OC1) → 2× RC LPF (10kΩ+10nF) → NTE941M unity buffer
 *                       → 10µF coupling cap → 3.5mm jack TIP  (Left)
 *              RD1 (OC2) → identical chain → RING (Right)
 *
 * SPI volume:  RF6 (SCK1)  → AD5220 CLK
 *              RF7 (SDO1)  → AD5220 DI
 *              RB2 (GPIO)  → AD5220 /CS  (active low)
 *              RB3 (GPIO)  → AD5220 U/D  (1 = up, 0 = down)
 *
 * UART1:       RF2 (U1RX)  ↔ ESP32 TX0 (GPIO 1)
 *              RF3 (U1TX)  ↔ ESP32 RX0 (GPIO 3)
 *              115200 8N1
 *
 * RGB LEDs:    RG2 → driver chain → Left  RED
 *              RG3 → driver chain → Left  GREEN
 *              RG4 → driver chain → Left  BLUE
 *              RG6 → driver chain → Right RED
 *              RG7 → driver chain → Right GREEN
 *              RG8 → driver chain → Right BLUE
 *              (each chain: 1kΩ → NTE74HC14×2 → 1kΩ → 2N3904 → 100Ω → LED → +5V)
 *
 * Battery:     AN0 = MAX8212 voltage divider tap for battery monitoring
 *
 * ICSP:        J1 header — MCLR / VDD / VSS / PGD / PGC for PICkit 4
 *
 * ============================================================================
 *
 * UART COMMAND PROTOCOL (ASCII, newline-terminated, sent from ESP32)
 *
 *   CARRIER:128         Carrier frequency Hz (32, 64, 128, 256, 512)
 *   BEAT:4.0            Beat frequency Hz (0.5–12.0)
 *   PATTERN:ON|OFF      Auto spike-dip sequencing
 *   NOISE:OFF|PINK|BROWN|BLUE|WHITE
 *   MIX:0–100           Tone/noise mix (100 = tone only, 0 = noise only)
 *   ISOCHRONIC:0–100    AM modulation depth
 *   VOL:0–63            AD5220 wiper position
 *   LED_MODE:OFF|RGB|FOLLOW
 *   LED_COLOR:r,g,b     Manual override 0–255 each
 *   LED_BRIGHT:0–255    Master LED brightness
 *   PRESET:<name>       Load named preset (THETA, ALPHA, SMR, MEDITATE, ...)
 *   STATUS?             Request current state — replies STATUS:<json>
 *
 * ============================================================================
 */

#include <xc.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ---- Configuration bits (10MHz xtal × PLL4 = 40 MHz osc, Fcy = 20 MIPS) ---- */
_FOSC(CSW_FSCM_OFF & XT_PLL4);
_FWDT(WDT_OFF);
_FBORPOR(PBOR_OFF & MCLR_EN);
_FGS(CODE_PROT_OFF);

/* ---- Constants ---- */
#define FCY                 20000000UL        /* Instruction clock 20 MHz     */
#define SAMPLE_RATE         8000U             /* DDS ISR rate                 */
#define PWM_PERIOD          (FCY / 256000)    /* ~256 kHz PWM carrier         */

#define DEFAULT_CARRIER_HZ  128
#define DEFAULT_BEAT_HZ     4.0f

/* ---- DDS state ---- */
static volatile uint32_t phase_acc_l = 0;
static volatile uint32_t phase_acc_r = 0;
static volatile uint32_t phase_inc_l = 0;
static volatile uint32_t phase_inc_r = 0;

/* 256-entry signed sin lookup, generated at startup */
static int8_t sin_lut[256];

/* ---- User parameters ---- */
static uint16_t carrier_hz   = DEFAULT_CARRIER_HZ;
static float    beat_hz      = DEFAULT_BEAT_HZ;
static uint8_t  volume_pos   = 32;     /* AD5220 wiper 0–63 */
static uint8_t  noise_mode   = 0;      /* 0=off 1=pink 2=brown 3=blue 4=white */
static uint8_t  mix_ratio    = 100;    /* 100 = tone only */
static uint8_t  iso_depth    = 0;      /* 0 = no AM */
static uint8_t  led_mode     = 1;      /* 0=off 1=rgb 2=follow */
static uint8_t  led_r=0, led_g=0, led_b=255;
static uint8_t  led_bright   = 128;
static uint8_t  pattern_on   = 0;

/* ---- LFSR for noise generation ---- */
static uint32_t lfsr = 0xACE1u;
static inline uint8_t noise_byte(void) {
    uint32_t bit = ((lfsr >> 0) ^ (lfsr >> 10) ^ (lfsr >> 30) ^ (lfsr >> 31)) & 1u;
    lfsr = (lfsr >> 1) | (bit << 31);
    return (uint8_t)(lfsr & 0xFF);
}

/* ---- UART buffer ---- */
#define UART_BUF_SIZE 128
static char    uart_rx_buf[UART_BUF_SIZE];
static uint8_t uart_rx_idx = 0;

/* ============================================================================
 * INITIALIZATION
 * ========================================================================== */

static void build_sin_lut(void) {
    int i;
    for (i = 0; i < 256; i++) {
        /* Approximate sin via piecewise — full math.h sin works too, but slow */
        float angle = (i / 256.0f) * 6.28318530718f;
        float s = angle - (angle*angle*angle)/6.0f
                       + (angle*angle*angle*angle*angle)/120.0f;
        if (s > 1.0f)  s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        sin_lut[i] = (int8_t)(s * 127);
    }
}

static void update_phase_increments(void) {
    /* Left = carrier - beat/2,  Right = carrier + beat/2 */
    float left  = (float)carrier_hz - beat_hz / 2.0f;
    float right = (float)carrier_hz + beat_hz / 2.0f;

    /* phase_inc = freq × 2^32 / sample_rate */
    phase_inc_l = (uint32_t)((left  * 4294967296.0f) / SAMPLE_RATE);
    phase_inc_r = (uint32_t)((right * 4294967296.0f) / SAMPLE_RATE);
}

static void init_pwm(void) {
    OC1RS = PWM_PERIOD;
    OC2RS = PWM_PERIOD;
    OC1R  = PWM_PERIOD / 2;
    OC2R  = PWM_PERIOD / 2;
    OC1CON = 0x0006;   /* PWM mode, no fault */
    OC2CON = 0x0006;
}

static void init_timer1_sample_isr(void) {
    PR1 = (FCY / SAMPLE_RATE) - 1;
    T1CON = 0x8000;            /* Tcy clock, prescale 1:1, enable */
    IPC0bits.T1IP = 5;
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
}

static void init_uart1(void) {
    /* 115200 baud at Fcy 20MHz: U1BRG = (Fcy / (16 × baud)) - 1 = 10 */
    U1BRG  = 10;
    U1MODE = 0x8000;           /* Enable UART, 8-N-1 */
    U1STA  = 0x0400;           /* Enable TX */
    IPC2bits.U1RXIP = 4;
    IFS0bits.U1RXIF = 0;
    IEC0bits.U1RXIE = 1;
}

static void init_gpio(void) {
    /* RGB LED pins as outputs */
    TRISG &= ~((1<<2)|(1<<3)|(1<<4)|(1<<6)|(1<<7)|(1<<8));
    LATG  &= ~((1<<2)|(1<<3)|(1<<4)|(1<<6)|(1<<7)|(1<<8));

    /* AD5220 control pins */
    TRISBbits.TRISB2 = 0;     /* /CS */
    TRISBbits.TRISB3 = 0;     /* U/D */
    LATBbits.LATB2 = 1;       /* /CS idle high */
}

/* ============================================================================
 * AD5220 SPI VOLUME DRIVER
 * ========================================================================== */

static void ad5220_step(uint8_t up) {
    LATBbits.LATB3 = up ? 1 : 0;
    asm("nop"); asm("nop");
    LATBbits.LATB2 = 0;        /* /CS low */
    asm("nop"); asm("nop");
    LATBbits.LATB2 = 1;        /* /CS high — step occurs on rising edge */
}

static uint8_t ad5220_current = 63;
static void ad5220_set(uint8_t target) {
    if (target > 63) target = 63;
    while (ad5220_current < target) { ad5220_step(1); ad5220_current++; }
    while (ad5220_current > target) { ad5220_step(0); ad5220_current--; }
}

/* ============================================================================
 * UART COMMAND PARSER
 * ========================================================================== */

static void uart_send(const char *s) {
    while (*s) {
        while (U1STAbits.UTXBF);
        U1TXREG = *s++;
    }
}

static void send_status(void) {
    char buf[160];
    snprintf(buf, sizeof(buf),
        "STATUS:{\"carrier\":%u,\"beat\":%.1f,\"vol\":%u,\"noise\":%u,"
        "\"mix\":%u,\"iso\":%u,\"led\":%u,\"rgb\":[%u,%u,%u],\"bright\":%u}\r\n",
        carrier_hz, (double)beat_hz, volume_pos, noise_mode,
        mix_ratio, iso_depth, led_mode, led_r, led_g, led_b, led_bright);
    uart_send(buf);
}

static void parse_command(const char *cmd) {
    /* Simple ASCII command parser — KEY:VALUE format */
    if (strncmp(cmd, "CARRIER:", 8) == 0) {
        carrier_hz = (uint16_t)atoi(cmd + 8);
        update_phase_increments();
    }
    else if (strncmp(cmd, "BEAT:", 5) == 0) {
        beat_hz = (float)atof(cmd + 5);
        update_phase_increments();
    }
    else if (strncmp(cmd, "VOL:", 4) == 0) {
        volume_pos = (uint8_t)atoi(cmd + 4);
        ad5220_set(volume_pos);
    }
    else if (strncmp(cmd, "NOISE:", 6) == 0) {
        const char *n = cmd + 6;
        if      (strncmp(n, "OFF",   3) == 0) noise_mode = 0;
        else if (strncmp(n, "PINK",  4) == 0) noise_mode = 1;
        else if (strncmp(n, "BROWN", 5) == 0) noise_mode = 2;
        else if (strncmp(n, "BLUE",  4) == 0) noise_mode = 3;
        else if (strncmp(n, "WHITE", 5) == 0) noise_mode = 4;
    }
    else if (strncmp(cmd, "MIX:",        4) == 0) mix_ratio  = (uint8_t)atoi(cmd + 4);
    else if (strncmp(cmd, "ISOCHRONIC:", 11)== 0) iso_depth  = (uint8_t)atoi(cmd + 11);
    else if (strncmp(cmd, "LED_BRIGHT:", 11)== 0) led_bright = (uint8_t)atoi(cmd + 11);
    else if (strncmp(cmd, "PATTERN:",     8)== 0) pattern_on = (cmd[8] == 'O' && cmd[9] == 'N');
    else if (strncmp(cmd, "LED_COLOR:",  10) == 0) {
        sscanf(cmd + 10, "%hhu,%hhu,%hhu", &led_r, &led_g, &led_b);
    }
    else if (strncmp(cmd, "STATUS?", 7) == 0) {
        send_status();
        return;
    }

    uart_send("OK\r\n");
}

/* ============================================================================
 * INTERRUPTS
 * ========================================================================== */

void __attribute__((interrupt, no_auto_psv)) _T1Interrupt(void) {
    /* Sample-rate ISR: 8 kHz */
    phase_acc_l += phase_inc_l;
    phase_acc_r += phase_inc_r;

    int16_t sl = sin_lut[(phase_acc_l >> 24) & 0xFF];
    int16_t sr = sin_lut[(phase_acc_r >> 24) & 0xFF];

    /* Mix in noise if enabled */
    if (noise_mode != 0) {
        int16_t n = (int16_t)(int8_t)noise_byte();
        sl = (sl * mix_ratio + n * (100 - mix_ratio)) / 100;
        sr = (sr * mix_ratio + n * (100 - mix_ratio)) / 100;
    }

    /* Map signed -127..127 to PWM duty 0..PWM_PERIOD */
    OC1R = (uint16_t)(((int32_t)sl + 128) * PWM_PERIOD / 256);
    OC2R = (uint16_t)(((int32_t)sr + 128) * PWM_PERIOD / 256);

    IFS0bits.T1IF = 0;
}

void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void) {
    while (U1STAbits.URXDA) {
        char c = U1RXREG;
        if (c == '\r' || c == '\n') {
            if (uart_rx_idx > 0) {
                uart_rx_buf[uart_rx_idx] = 0;
                parse_command(uart_rx_buf);
                uart_rx_idx = 0;
            }
        } else if (uart_rx_idx < UART_BUF_SIZE - 1) {
            uart_rx_buf[uart_rx_idx++] = c;
        }
    }
    IFS0bits.U1RXIF = 0;
}

/* ============================================================================
 * MAIN
 * ========================================================================== */

int main(void) {
    build_sin_lut();
    init_gpio();
    init_pwm();
    init_uart1();
    update_phase_increments();
    ad5220_set(volume_pos);
    init_timer1_sample_isr();

    uart_send("READY\r\n");

    /* Main loop — soft-PWM for LEDs, slow housekeeping */
    uint8_t soft_pwm_count = 0;
    while (1) {
        /* Simple soft-PWM for RGB LEDs */
        if (led_mode != 0) {
            uint16_t r_thresh = ((uint16_t)led_r * led_bright) >> 8;
            uint16_t g_thresh = ((uint16_t)led_g * led_bright) >> 8;
            uint16_t b_thresh = ((uint16_t)led_b * led_bright) >> 8;

            uint8_t r_on = soft_pwm_count < r_thresh;
            uint8_t g_on = soft_pwm_count < g_thresh;
            uint8_t b_on = soft_pwm_count < b_thresh;

            if (r_on) LATG |=  (1<<2); else LATG &= ~(1<<2);
            if (g_on) LATG |=  (1<<3); else LATG &= ~(1<<3);
            if (b_on) LATG |=  (1<<4); else LATG &= ~(1<<4);
            if (r_on) LATG |=  (1<<6); else LATG &= ~(1<<6);
            if (g_on) LATG |=  (1<<7); else LATG &= ~(1<<7);
            if (b_on) LATG |=  (1<<8); else LATG &= ~(1<<8);

            soft_pwm_count++;
        }
    }

    return 0;
}
