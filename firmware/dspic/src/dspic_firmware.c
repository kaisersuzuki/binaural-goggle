/*
 * dspic_firmware.c
 * Binaural Beat + RGB LED Goggle Controller
 * Target: dsPIC30F4013-20I/P, 40-pin PDIP, socketed
 * Fcy: 20MHz (10MHz crystal x PLL4)
 *
 * PIN ASSIGNMENTS (final PCB rev 1.1, Atan/Darlington PCB Design):
 *
 *   POWER:
 *     VDD  = pin 11, 21, 32, 40  (+3.3V)
 *     VSS  = pin 12, 20, 31, 39  (GND)
 *     AVDD = pin 40              (+3.3V analog)
 *     AVSS = pin 39              (GND analog)
 *
 *   CLOCK:
 *     OSC1 = pin 13  (10MHz crystal)
 *     OSC2 = pin 14  (10MHz crystal)
 *
 *   AUDIO PWM OUTPUT:
 *     OC1/RD0 = pin 34  (Left channel PWM)
 *     OC2/RD1 = pin 33  (Right channel PWM)
 *
 *   SPI1 -> AD5220 digital pot (volume):
 *     SCK1/RF6 = pin 24  (SPI clock)
 *     SDO1/RF3 = pin 25  (SPI data out -> AD5220 DI)
 *     RB2      = pin 4   (/CS  - chip select, active low)
 *     RB3      = pin 5   (U/D  - up/down direction)
 *
 *   UART2 -> ESP32 Serial2 (GPIO16/17):
 *     U2TX/RF5 = pin 27  (dsPIC TX -> ESP32 RX, GPIO16)
 *     U2RX/RF4 = pin 28  (dsPIC RX -> ESP32 TX, GPIO17)
 *
 *   RGB LED DRIVER (via 74HC14 + 2N3904):
 *     RB4 = pin 6   (Eye A - Red)
 *     RB5 = pin 7   (Eye A - Green)
 *     RB6 = pin 8   (Eye A - Blue)   NOTE: shared with PGC, LEDs flash during ICSP programming
 *     RB7 = pin 9   (Eye B - Red)    NOTE: shared with PGD, LEDs flash during ICSP programming
 *     RB8 = pin 10  (Eye B - Green)
 *     RB9 = pin 38  (Eye B - Blue)
 *
 *   TPA6132A2 HEADPHONE AMP ENABLE:
 *     RB10 = pin 37  (EN pin of TPA6132A2 - must be HIGH to enable amp)
 *
 *   MCLR:
 *     MCLR = pin 1   (10k pullup to 3.3V, ICSP reset)
 *
 * ARCHITECTURE:
 *   - Timer1 ISR @ 8kHz: DDS sine generation (L+R channels), beat sequencer,
 *     soft-PWM for RGB LEDs
 *   - OC1/OC2: PWM output ~256kHz, duty cycle set by DDS
 *   - SPI1: AD5220 volume control (write-only, 64 steps, pulse /CS with U/D set)
 *   - UART2: 9600 baud command parser (from ESP32)
 *
 * BEAT PATTERN (Tomás Cobos / published protocol):
 *   Baseline: 4Hz (theta) continuous
 *   Spikes:   8Hz for 40s every ~8min
 *   Dips:     2Hz for 90s every ~13min
 *   Transitions: 3s linear ramp
 *
 * COMPILE: MPLAB X + XC16
 * PROGRAM: PICkit 4 via ICSP header J1 (MCLR/VDD/VSS/PGD/PGC)
 */

#include <xc.h>
#include <stdint.h>
#include <string.h>

/* ── Configuration bits ──────────────────────────────────────────────── */
_FOSC(CSW_FSCM_OFF & XT_PLL4);   /* 10MHz crystal x PLL4 = 40MHz, Fcy=20MHz */
_FWDT(WDT_OFF);
_FBORPOR(PBOR_OFF & MCLR_EN);
_FGS(CODE_PROT_OFF);

/* ── Fcy and derived constants ───────────────────────────────────────── */
#define FCY         20000000UL
#define FSAMP       8000UL          /* DDS sample rate (Hz) */
#define PWM_PERIOD  78              /* OC period for ~256kHz PWM (FCY/256kHz) */
#define UART_BAUD   9600

/* ── DDS sine table (256 entries, 8-bit, centered at 128) ────────────── */
static const uint8_t sine_table[256] = {
    128,131,134,137,140,143,146,149,152,155,158,161,164,167,170,173,
    176,179,182,184,187,190,192,195,198,200,203,205,208,210,212,215,
    217,219,221,223,225,227,229,231,233,234,236,238,239,240,242,243,
    244,245,246,247,248,249,250,250,251,252,252,253,253,253,254,254,
    254,254,254,254,254,253,253,253,252,252,251,250,250,249,248,247,
    246,245,244,243,242,240,239,238,236,234,233,231,229,227,225,223,
    221,219,217,215,212,210,208,205,203,200,198,195,192,190,187,184,
    182,179,176,173,170,167,164,161,158,155,152,149,146,143,140,137,
    134,131,128,124,121,118,115,112,109,106,103,100, 97, 94, 91, 88,
     85, 82, 79, 77, 74, 71, 69, 66, 63, 61, 58, 56, 53, 51, 49, 46,
     44, 42, 40, 38, 36, 34, 32, 30, 28, 27, 25, 23, 22, 21, 19, 18,
     17, 16, 15, 14, 13, 12, 11, 11, 10,  9,  9,  8,  8,  8,  7,  7,
      7,  7,  7,  7,  7,  8,  8,  8,  9,  9, 10, 11, 11, 12, 13, 14,
     15, 16, 17, 18, 19, 21, 22, 23, 25, 27, 28, 30, 32, 34, 36, 38,
     40, 42, 44, 46, 49, 51, 53, 56, 58, 61, 63, 66, 69, 71, 74, 77,
     79, 82, 85, 88, 91, 94, 97,100,103,106,109,112,115,118,121,124
};

/* ── DDS state ───────────────────────────────────────────────────────── */
static uint32_t phase_acc_L = 0;    /* 32-bit phase accumulator, left  */
static uint32_t phase_acc_R = 0;    /* 32-bit phase accumulator, right */
static uint32_t phase_inc_L = 0;    /* phase increment = freq_L * 2^32 / FSAMP */
static uint32_t phase_inc_R = 0;

/* carrier + beat: L = Fc, R = Fc + beat_freq */
static float carrier_hz   = 200.0f; /* carrier frequency (Hz) */
static float beat_hz      = 4.0f;   /* current beat frequency (Hz) */
static float target_beat  = 4.0f;   /* target beat for ramping */
static float ramp_step    = 0.0f;   /* per-sample ramp increment */

/* Beat sequencer timing (in samples at 8kHz) */
#define SAMPLES_PER_SEC     FSAMP
#define BASELINE_BEAT_HZ    4.0f
#define SPIKE_BEAT_HZ       8.0f
#define DIP_BEAT_HZ         2.0f
#define SPIKE_DURATION      (40UL  * SAMPLES_PER_SEC)
#define DIP_DURATION        (90UL  * SAMPLES_PER_SEC)
#define SPIKE_INTERVAL      (480UL * SAMPLES_PER_SEC)   /* ~8min  */
#define DIP_INTERVAL        (780UL * SAMPLES_PER_SEC)   /* ~13min */
#define RAMP_DURATION       (3UL   * SAMPLES_PER_SEC)

static uint32_t sequencer_tick  = 0;
static uint32_t next_spike      = SPIKE_INTERVAL;
static uint32_t next_dip        = DIP_INTERVAL;
static uint32_t event_end       = 0;
typedef enum { SEQ_BASELINE, SEQ_SPIKE, SEQ_DIP } seq_state_t;
static seq_state_t seq_state = SEQ_BASELINE;

/* ── RGB soft-PWM state ──────────────────────────────────────────────── */
/* 6 channels: [0]=A_R [1]=A_G [2]=A_B [3]=B_R [4]=B_G [5]=B_B */
static uint8_t  rgb_target[6]  = {255, 0, 0, 255, 0, 0}; /* default: red */
static uint8_t  rgb_current[6] = {0};
static uint8_t  pwm_counter    = 0;
static uint16_t led_tick        = 0;
#define LED_FADE_RATE   2   /* fade step per 8kHz tick interval */

/* ── Volume ──────────────────────────────────────────────────────────── */
static uint8_t volume_steps = 32;   /* 0-63, current position */

/* ── UART command buffer ─────────────────────────────────────────────── */
#define CMD_BUF_LEN 32
static char  cmd_buf[CMD_BUF_LEN];
static uint8_t cmd_idx = 0;
static volatile uint8_t cmd_ready = 0;

/* ─────────────────────────────────────────────────────────────────────
   UTILITY: compute DDS phase increment
   phase_inc = freq * 2^32 / FSAMP
   ───────────────────────────────────────────────────────────────────── */
static uint32_t freq_to_inc(float hz) {
    return (uint32_t)(hz * (4294967296.0f / (float)FSAMP));
}

/* ─────────────────────────────────────────────────────────────────────
   SPI1 / AD5220 volume control
   AD5220 is write-only (no MISO). To step wiper:
     1. Set U/D pin (RB3) for direction
     2. Assert /CS low (RB2)
     3. Pulse CLK (handled by SPI peripheral or bit-bang)
     4. Deassert /CS high
   We use a simple bit-bang approach for reliability.
   ───────────────────────────────────────────────────────────────────── */
#define AD5220_CS_LOW()   LATBbits.LATB2 = 0
#define AD5220_CS_HIGH()  LATBbits.LATB2 = 1
#define AD5220_UD_UP()    LATBbits.LATB3 = 1
#define AD5220_UD_DOWN()  LATBbits.LATB3 = 0
#define AD5220_CLK_LOW()  LATFbits.LATF6 = 0
#define AD5220_CLK_HIGH() LATFbits.LATF6 = 1

static void ad5220_step(uint8_t up) {
    if (up) AD5220_UD_UP(); else AD5220_UD_DOWN();
    AD5220_CS_LOW();
    __asm__("nop"); __asm__("nop");
    AD5220_CLK_HIGH();
    __asm__("nop"); __asm__("nop");
    AD5220_CLK_LOW();
    __asm__("nop"); __asm__("nop");
    AD5220_CS_HIGH();
}

/* Set absolute volume (0-63). AD5220 has no read-back so we track position. */
static void ad5220_set_volume(uint8_t target) {
    if (target > 63) target = 63;
    while (volume_steps < target) { ad5220_step(1); volume_steps++; }
    while (volume_steps > target) { ad5220_step(0); volume_steps--; }
}

/* ─────────────────────────────────────────────────────────────────────
   RGB LED output (soft-PWM via PORTB)
   Channels mapped to PORTB bits:
     RB4 = A_Red, RB5 = A_Green, RB6 = A_Blue
     RB7 = B_Red, RB8 = B_Green, RB9 = B_Blue
   ───────────────────────────────────────────────────────────────────── */
static void rgb_update_outputs(void) {
    /* soft-PWM: compare pwm_counter against each channel's current value */
    LATBbits.LATB4 = (pwm_counter < rgb_current[0]) ? 1 : 0; /* A_R */
    LATBbits.LATB5 = (pwm_counter < rgb_current[1]) ? 1 : 0; /* A_G */
    LATBbits.LATB6 = (pwm_counter < rgb_current[2]) ? 1 : 0; /* A_B */
    LATBbits.LATB7 = (pwm_counter < rgb_current[3]) ? 1 : 0; /* B_R */
    LATBbits.LATB8 = (pwm_counter < rgb_current[4]) ? 1 : 0; /* B_G */
    LATBbits.LATB9 = (pwm_counter < rgb_current[5]) ? 1 : 0; /* B_B */
    pwm_counter++;
}

/* Fade current RGB values toward targets */
static void rgb_fade_step(void) {
    uint8_t i;
    for (i = 0; i < 6; i++) {
        if (rgb_current[i] < rgb_target[i]) {
            uint8_t delta = rgb_target[i] - rgb_current[i];
            rgb_current[i] += (delta > LED_FADE_RATE) ? LED_FADE_RATE : delta;
        } else if (rgb_current[i] > rgb_target[i]) {
            uint8_t delta = rgb_current[i] - rgb_target[i];
            rgb_current[i] -= (delta > LED_FADE_RATE) ? LED_FADE_RATE : delta;
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────
   Beat sequencer — called every sample from Timer1 ISR
   ───────────────────────────────────────────────────────────────────── */
static void sequencer_tick_fn(void) {
    sequencer_tick++;

    /* ramp beat_hz toward target_beat */
    if (beat_hz < target_beat) {
        beat_hz += ramp_step;
        if (beat_hz > target_beat) beat_hz = target_beat;
    } else if (beat_hz > target_beat) {
        beat_hz -= ramp_step;
        if (beat_hz < target_beat) beat_hz = target_beat;
    }

    /* update phase increments whenever beat changes */
    phase_inc_L = freq_to_inc(carrier_hz);
    phase_inc_R = freq_to_inc(carrier_hz + beat_hz);

    /* state machine */
    switch (seq_state) {
        case SEQ_BASELINE:
            if (sequencer_tick >= next_spike) {
                target_beat = SPIKE_BEAT_HZ;
                ramp_step = (SPIKE_BEAT_HZ - beat_hz) / (float)RAMP_DURATION;
                if (ramp_step < 0) ramp_step = -ramp_step;
                event_end = sequencer_tick + SPIKE_DURATION;
                next_spike = sequencer_tick + SPIKE_INTERVAL;
                seq_state = SEQ_SPIKE;
            } else if (sequencer_tick >= next_dip) {
                target_beat = DIP_BEAT_HZ;
                ramp_step = (beat_hz - DIP_BEAT_HZ) / (float)RAMP_DURATION;
                if (ramp_step < 0) ramp_step = -ramp_step;
                event_end = sequencer_tick + DIP_DURATION;
                next_dip = sequencer_tick + DIP_INTERVAL;
                seq_state = SEQ_DIP;
            }
            break;

        case SEQ_SPIKE:
        case SEQ_DIP:
            if (sequencer_tick >= event_end) {
                target_beat = BASELINE_BEAT_HZ;
                ramp_step = 0.0006f; /* ~3s ramp back at 8kHz */
                seq_state = SEQ_BASELINE;
            }
            break;
    }
}

/* ─────────────────────────────────────────────────────────────────────
   Timer1 ISR — 8kHz sample rate
   Generates DDS samples, updates PWM duty cycles, drives LED soft-PWM
   ───────────────────────────────────────────────────────────────────── */
void __attribute__((__interrupt__, no_auto_psv)) _T1Interrupt(void) {
    /* DDS: advance phase accumulators */
    phase_acc_L += phase_inc_L;
    phase_acc_R += phase_inc_R;

    /* Read sine table (top 8 bits of 32-bit accumulator = table index) */
    uint8_t samp_L = sine_table[phase_acc_L >> 24];
    uint8_t samp_R = sine_table[phase_acc_R >> 24];

    /* Update OC1/OC2 duty cycle (PWM output) */
    OC1RS = (uint16_t)samp_L * PWM_PERIOD / 255;
    OC2RS = (uint16_t)samp_R * PWM_PERIOD / 255;

    /* LED soft-PWM (run every sample for ~31kHz PWM rate) */
    rgb_update_outputs();

    /* LED fade (run at ~31Hz: every 256 samples) */
    led_tick++;
    if (led_tick >= 256) {
        led_tick = 0;
        rgb_fade_step();
    }

    /* Beat sequencer */
    sequencer_tick_fn();

    IFS0bits.T1IF = 0; /* clear interrupt flag */
}

/* ─────────────────────────────────────────────────────────────────────
   UART2 RX ISR — collect command bytes into buffer
   Commands are newline-terminated ASCII strings from ESP32
   ───────────────────────────────────────────────────────────────────── */
void __attribute__((__interrupt__, no_auto_psv)) _U2RXInterrupt(void) {
    char c = U2RXREG;
    IFS1bits.U2RXIF = 0;

    if (c == '\n' || c == '\r') {
        if (cmd_idx > 0) {
            cmd_buf[cmd_idx] = '\0';
            cmd_ready = 1;
            cmd_idx = 0;
        }
    } else if (cmd_idx < CMD_BUF_LEN - 1) {
        cmd_buf[cmd_idx++] = c;
    }
}

/* ─────────────────────────────────────────────────────────────────────
   UART2 transmit helper
   ───────────────────────────────────────────────────────────────────── */
static void uart2_putch(char c) {
    while (!U2STAbits.TRMT);
    U2TXREG = c;
}

static void uart2_puts(const char *s) {
    while (*s) uart2_putch(*s++);
    uart2_putch('\n');
}

/* ─────────────────────────────────────────────────────────────────────
   Command parser
   Protocol (from ESP32):
     BEAT:<hz>        set beat frequency (e.g. BEAT:4.0)
     CARRIER:<hz>     set carrier frequency (e.g. CARRIER:200.0)
     VOL:<0-63>       set volume step
     RGB_A:<r>,<g>,<b>  set eye A color (0-255 each)
     RGB_B:<r>,<g>,<b>  set eye B color
     RGB_AB:<r>,<g>,<b> set both eyes same color
     STATUS           reply with current state
     RESET            reset sequencer to baseline
   ───────────────────────────────────────────────────────────────────── */
static void process_command(char *cmd) {
    if (strncmp(cmd, "BEAT:", 5) == 0) {
        float hz = 0;
        /* simple float parse */
        char *p = cmd + 5;
        float whole = 0, frac = 0, fdiv = 1;
        uint8_t in_frac = 0;
        while (*p) {
            if (*p == '.') { in_frac = 1; }
            else if (*p >= '0' && *p <= '9') {
                if (in_frac) { frac = frac * 10 + (*p - '0'); fdiv *= 10; }
                else whole = whole * 10 + (*p - '0');
            }
            p++;
        }
        hz = whole + frac / fdiv;
        if (hz > 0.5f && hz <= 40.0f) {
            target_beat = hz;
            ramp_step = 0.0006f;
        }
        uart2_puts("OK:BEAT");

    } else if (strncmp(cmd, "CARRIER:", 8) == 0) {
        /* same float parse as above */
        char *p = cmd + 8;
        float whole = 0, frac = 0, fdiv = 1;
        uint8_t in_frac = 0;
        while (*p) {
            if (*p == '.') { in_frac = 1; }
            else if (*p >= '0' && *p <= '9') {
                if (in_frac) { frac = frac * 10 + (*p - '0'); fdiv *= 10; }
                else whole = whole * 10 + (*p - '0');
            }
            p++;
        }
        float hz = whole + frac / fdiv;
        if (hz >= 50.0f && hz <= 1500.0f) carrier_hz = hz;
        uart2_puts("OK:CARRIER");

    } else if (strncmp(cmd, "VOL:", 4) == 0) {
        uint8_t v = (uint8_t)atoi(cmd + 4);
        if (v > 63) v = 63;
        ad5220_set_volume(v);
        uart2_puts("OK:VOL");

    } else if (strncmp(cmd, "RGB_A:", 6) == 0) {
        uint8_t r = 0, g = 0, b = 0;
        sscanf(cmd + 6, "%hhu,%hhu,%hhu", &r, &g, &b);
        rgb_target[0] = r; rgb_target[1] = g; rgb_target[2] = b;
        uart2_puts("OK:RGB_A");

    } else if (strncmp(cmd, "RGB_B:", 6) == 0) {
        uint8_t r = 0, g = 0, b = 0;
        sscanf(cmd + 6, "%hhu,%hhu,%hhu", &r, &g, &b);
        rgb_target[3] = r; rgb_target[4] = g; rgb_target[5] = b;
        uart2_puts("OK:RGB_B");

    } else if (strncmp(cmd, "RGB_AB:", 7) == 0) {
        uint8_t r = 0, g = 0, b = 0;
        sscanf(cmd + 7, "%hhu,%hhu,%hhu", &r, &g, &b);
        rgb_target[0] = r; rgb_target[1] = g; rgb_target[2] = b;
        rgb_target[3] = r; rgb_target[4] = g; rgb_target[5] = b;
        uart2_puts("OK:RGB_AB");

    } else if (strcmp(cmd, "STATUS") == 0) {
        char buf[64];
        /* send beat freq as integer*10 to avoid float formatting */
        uint16_t beat_x10 = (uint16_t)(beat_hz * 10.0f);
        uint16_t carr_x10 = (uint16_t)(carrier_hz * 10.0f);
        sprintf(buf, "BEAT:%u.%u CARRIER:%u.%u VOL:%u",
            beat_x10 / 10, beat_x10 % 10,
            carr_x10 / 10, carr_x10 % 10,
            volume_steps);
        uart2_puts(buf);

    } else if (strcmp(cmd, "RESET") == 0) {
        target_beat = BASELINE_BEAT_HZ;
        ramp_step = 0.0006f;
        seq_state = SEQ_BASELINE;
        sequencer_tick = 0;
        next_spike = SPIKE_INTERVAL;
        next_dip = DIP_INTERVAL;
        uart2_puts("OK:RESET");

    } else {
        uart2_puts("ERR:UNKNOWN");
    }
}

/* ─────────────────────────────────────────────────────────────────────
   Hardware init
   ───────────────────────────────────────────────────────────────────── */
static void init_gpio(void) {
    /* Set PORTB pins as digital outputs:
       RB2 (/CS), RB3 (U/D), RB4-RB9 (RGB LEDs), RB10 (TPA6132A2 EN) */
    ADPCFG = 0xFFFF;                /* all ANx pins as digital */
    TRISBbits.TRISB2  = 0;         /* /CS  out */
    TRISBbits.TRISB3  = 0;         /* U/D  out */
    TRISBbits.TRISB4  = 0;         /* A_R  out */
    TRISBbits.TRISB5  = 0;         /* A_G  out */
    TRISBbits.TRISB6  = 0;         /* A_B  out */
    TRISBbits.TRISB7  = 0;         /* B_R  out */
    TRISBbits.TRISB8  = 0;         /* B_G  out */
    TRISBbits.TRISB9  = 0;         /* B_B  out */
    TRISBbits.TRISB10 = 0;         /* TPA EN out */

    /* PORTF: RF3=SDO1 out, RF4=RX2 in, RF5=TX2 out, RF6=SCK1 out */
    TRISFbits.TRISF3 = 0;          /* SDO1 out */
    TRISFbits.TRISF4 = 1;          /* U2RX in  */
    TRISFbits.TRISF5 = 0;          /* U2TX out */
    TRISFbits.TRISF6 = 0;          /* SCK1 out */

    /* PORTD: RD0=OC1 out, RD1=OC2 out */
    TRISDbits.TRISD0 = 0;
    TRISDbits.TRISD1 = 0;

    /* Initial states */
    LATBbits.LATB2  = 1;           /* /CS deasserted */
    LATBbits.LATB3  = 1;           /* U/D default up */
    LATBbits.LATB10 = 1;           /* TPA6132A2 EN HIGH — amp enabled */
    LATB &= ~0x03F0;               /* LEDs off (RB4-RB9 = 0) */
    LATFbits.LATF6  = 0;           /* SCK idle low */
}

static void init_pwm(void) {
    /* OC1 and OC2: PWM mode, period ~256kHz */
    OC1CON = 0x0006;               /* PWM mode, no fault */
    OC2CON = 0x0006;
    OC1R  = 0;                     /* initial duty = 0 */
    OC1RS = 0;
    OC2R  = 0;
    OC2RS = 0;
    /* Timer2 as PWM timebase */
    T2CONbits.TCKPS = 0b00;        /* 1:1 prescaler */
    PR2 = PWM_PERIOD;              /* period register */
    T2CONbits.TON = 1;
    OC1CONbits.OCM = 0b110;        /* PWM mode, fault disabled */
    OC2CONbits.OCM = 0b110;
}

static void init_timer1(void) {
    /* Timer1: 8kHz ISR for DDS */
    T1CON = 0;
    T1CONbits.TCKPS = 0b00;        /* 1:1 prescaler */
    PR1 = (uint16_t)(FCY / FSAMP - 1);  /* = 2499 for 8kHz at 20MHz */
    IPC0bits.T1IP = 6;             /* priority 6 */
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
    T1CONbits.TON = 1;
}

static void init_uart2(void) {
    /* UART2 at 9600 baud for ESP32 communication */
    U2BRG  = (uint16_t)(FCY / (16UL * UART_BAUD) - 1);  /* = 129 */
    U2MODE = 0x8000;               /* UART2 enabled, 8N1 */
    U2STA  = 0x0400;               /* TX enabled */
    IFS1bits.U2RXIF = 0;
    IEC1bits.U2RXIE = 1;           /* enable RX interrupt */
}

/* ─────────────────────────────────────────────────────────────────────
   main
   ───────────────────────────────────────────────────────────────────── */
int main(void) {
    /* Initial phase increments: 200Hz carrier, 4Hz beat */
    phase_inc_L = freq_to_inc(carrier_hz);
    phase_inc_R = freq_to_inc(carrier_hz + beat_hz);

    init_gpio();
    init_pwm();
    init_uart2();
    init_timer1();      /* start last — ISR will begin firing */

    /* Set initial volume to mid-range (step 32 of 63) */
    /* AD5220 resets to end-stop on power-up, step up to 32 */
    ad5220_set_volume(32);

    /* Send boot message to ESP32 */
    uart2_puts("READY:BINAURAL_GOGGLE_V2");

    /* Main loop: process UART commands */
    while (1) {
        if (cmd_ready) {
            cmd_ready = 0;
            process_command(cmd_buf);
        }
        /* IDLE — all real work happens in Timer1 ISR */
        Idle();
    }

    return 0;
}
