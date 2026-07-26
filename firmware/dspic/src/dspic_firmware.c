/*
 * dspic_firmware.c  —  v2.2
 * Binaural Beat + RGB LED Goggle Controller
 * Target: dsPIC30F4013 (PDIP-40, socketed)
 * Board:  Darlington PCB Design rev 1.1 (Atan), JLCPCB assembled
 *
 * ── CHANGES vs v2.1 ─────────────────────────────────────────────────
 *  FIX: LED beat gate moved from the 31.25Hz control task into the
 *       8kHz ISR. At 31.25Hz any flicker above ~4Hz aliased badly
 *       (20Hz got 1.5 updates/cycle). Photic-driving rates 8-20Hz
 *       now get 400-1000 samples/cycle and are clean.
 *  NEW: SEQ:ON|OFF  — disable the auto spike/dip sequencer so a
 *       manually set BEAT stays put (required for photic work).
 *  NEW: DUTY:<5-100> — flash duty cycle percent. Low duty (20-30)
 *       drives phosphenes far harder than a 50% square wave.
 *
 * ── CHANGES vs v2.0 ─────────────────────────────────────────────────
 *  FIX: _FOSC uses XT_PLL8 (10MHz x 8 / 4 = 20 MIPS). PLL4 gave 10 MIPS
 *       and made every clock-derived rate run at half speed.
 *  FIX: ISR is now integer-only. All float math (beat ramp, phase-inc
 *       recompute) runs in a 31.25Hz control task in the main loop.
 *  NEW: LED beat synchronisation. Modes: ALT (default — eyes flash on
 *       opposite half-cycles), SYNC (together), SOLID (no pulse).
 *  FIX: LED soft-PWM now 64 levels @ 8kHz = 125Hz (was 31Hz = flicker).
 *  FIX: no sscanf/sprintf/atoi — hand-rolled parsers (XC16-safe).
 *  NEW: AD5220 wiper sync at boot (64 steps down, then up to default)
 *       so tracked position is correct regardless of power-up state.
 *
 * ── PIN MAP (verified against rev 1.1 netlist) ──────────────────────
 *  OC1/RD0  p34  PWM audio L          OC2/RD1  p33  PWM audio R
 *  SCK1/RF6 p24  AD5220 CLK (both)    RB2 p4   AD5220 /CS (both)
 *  RB3      p5   AD5220 U/D (both)
 *  U2TX/RF5 p27  -> ESP32 RX0         U2RX/RF4 p28  <- ESP32 TX0
 *  RB4  p6  A_R    RB5  p7  A_G    RB6/PGC p8  A_B
 *  RB7/PGD p9 B_R  RB8 p10  B_G    RB9    p38  B_B
 *  RB10    p37  TPA6132A2 EN (drive HIGH at boot or no audio)
 *  OSC1/OSC2 p13/p14  10MHz crystal
 *
 * NOTE: RB6/RB7 double as PGC/PGD — LEDs flicker during ICSP. Harmless.
 * NOTE: The two AD5220s share CLK//CS/UD and step together (L+R).
 *
 * ── UART PROTOCOL (9600 8N1, newline-terminated ASCII) ──────────────
 *  BEAT:<hz>  CARRIER:<hz>  VOL:<0-63>  RGB_A/RGB_B/RGB_AB:<r>,<g>,<b>
 *  MODE:ALT|SYNC|SOLID   SEQ:ON|OFF   DUTY:<5-100>   STATUS   RESET
 *
 * BUILD: MPLAB X + XC16.  PROGRAM: PICkit 4 on H1.
 */

#include <xc.h>
#include <stdint.h>
#include <string.h>

/* ── Configuration bits ─────────────────────────────────────────── */
_FOSC(CSW_FSCM_OFF & XT_PLL8);   /* 10MHz x8 PLL -> Fcy = 20 MIPS */
_FWDT(WDT_OFF);
_FBORPOR(PBOR_OFF & MCLR_EN);
_FGS(CODE_PROT_OFF);

#define FCY          20000000UL
#define FSAMP        8000UL
#define PWM_PERIOD   78            /* 20MHz/78 ~= 256kHz PWM carrier   */
#define CTRL_DIV     256           /* control task every 256 samples   */
                                   /* -> 31.25Hz control rate          */

/* ── Sine table: 256 entries, 0..255 centred at 128 ─────────────── */
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

/* ── ISR-shared state (integer only) ────────────────────────────── */
static volatile uint32_t phase_acc_L = 0, phase_acc_R = 0;
static volatile uint32_t phase_inc_L = 0, phase_inc_R = 0;
static volatile uint8_t  pwm6 = 0;                 /* 0..63 soft-PWM  */
static volatile uint32_t led_phase = 0;            /* beat phase acc  */
static volatile uint32_t led_phase_inc = 0;        /* per-sample step */
static volatile uint16_t duty_half = 32768;        /* ALT threshold   */
static volatile uint16_t duty_full = 65535;        /* SYNC threshold  */
static volatile uint16_t ctrl_downct = CTRL_DIV;   /* ISR->task div   */
static volatile uint8_t  ctrl_flag = 0;            /* run control task*/

/* ── Control-task state (floats live here, NOT in ISR) ──────────── */
static float carrier_hz  = 200.0f;
static float beat_hz     = 4.0f;
static float target_beat = 4.0f;
static float ramp_per_ctrl = 0.05f;   /* Hz per control tick during ramp */

static uint8_t  rgb_target[6]  = {255,0,0, 255,0,0};
static volatile uint8_t rgb_current[6] = {0};
#define FADE_STEP 8                    /* per control tick (31Hz)     */

typedef enum { MODE_ALT, MODE_SYNC, MODE_SOLID } led_mode_t;
static volatile led_mode_t led_mode = MODE_ALT;
static uint8_t  seq_enabled = 1;       /* auto spike/dip sequencer     */
static uint8_t  duty_pct    = 100;     /* flash duty, percent          */

/* Sequencer (ticks are control ticks @ 31.25Hz) */
#define CTRL_HZ            31          /* integer approx is fine      */
#define BASELINE_BEAT      4.0f
#define SPIKE_BEAT         8.0f
#define DIP_BEAT           2.0f
#define SPIKE_LEN_T        (40UL  * CTRL_HZ)
#define DIP_LEN_T          (90UL  * CTRL_HZ)
#define SPIKE_EVERY_T      (480UL * CTRL_HZ)
#define DIP_EVERY_T        (780UL * CTRL_HZ)
#define RAMP_TICKS         (3UL   * CTRL_HZ)     /* 3s transitions   */

static uint32_t seq_t = 0, next_spike = SPIKE_EVERY_T,
                next_dip = DIP_EVERY_T, event_end = 0;
typedef enum { SEQ_BASE, SEQ_SPIKE, SEQ_DIP } seq_t_e;
static seq_t_e seq_state = SEQ_BASE;

static uint8_t volume_steps = 32;

/* ── UART command buffer ────────────────────────────────────────── */
#define CMD_LEN 32
static char cmd_buf[CMD_LEN];
static volatile uint8_t cmd_idx = 0, cmd_ready = 0;

/* ═════════════════════════ helpers ═════════════════════════════ */

static uint32_t freq_to_inc(float hz) {
    return (uint32_t)(hz * (4294967296.0f / (float)FSAMP));
}

/* parse "12.5" -> float; returns -1 on garbage */
static float parse_float(const char *p) {
    float whole = 0, frac = 0, div = 1;
    uint8_t in_frac = 0, seen = 0;
    while (*p) {
        if (*p == '.') { if (in_frac) return -1; in_frac = 1; }
        else if (*p >= '0' && *p <= '9') {
            seen = 1;
            if (in_frac) { frac = frac*10 + (*p-'0'); div *= 10; }
            else           whole = whole*10 + (*p-'0');
        } else if (*p != ' ') return -1;
        p++;
    }
    return seen ? whole + frac/div : -1;
}

/* parse "r,g,b" into out[3]; returns 1 on success */
static uint8_t parse_rgb(const char *p, uint8_t *out) {
    uint16_t v = 0; uint8_t n = 0, seen = 0;
    while (1) {
        if (*p >= '0' && *p <= '9') {
            v = v*10 + (*p-'0'); if (v > 255) v = 255; seen = 1;
        } else if (*p == ',' || *p == '\0') {
            if (!seen || n >= 3) return 0;
            out[n++] = (uint8_t)v; v = 0; seen = 0;
            if (*p == '\0') break;
        } else return 0;
        p++;
    }
    return (n == 3);
}

/* append unsigned int as decimal to buf, return new end ptr */
static char *put_u16(char *b, uint16_t v) {
    char tmp[6]; uint8_t i = 0;
    do { tmp[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) *b++ = tmp[--i];
    return b;
}

/* ── AD5220 (both units in parallel): pulse CLK with /CS low ───── */
#define POT_CS_LO()  (LATBbits.LATB2 = 0)
#define POT_CS_HI()  (LATBbits.LATB2 = 1)
#define POT_UP()     (LATBbits.LATB3 = 1)
#define POT_DN()     (LATBbits.LATB3 = 0)
#define POT_CLK_LO() (LATFbits.LATF6 = 0)
#define POT_CLK_HI() (LATFbits.LATF6 = 1)

static void pot_step(uint8_t up) {
    if (up) POT_UP(); else POT_DN();
    POT_CS_LO();  __builtin_nop(); __builtin_nop();
    POT_CLK_HI(); __builtin_nop(); __builtin_nop();
    POT_CLK_LO(); __builtin_nop(); __builtin_nop();
    POT_CS_HI();
}

static void pot_set(uint8_t target) {
    if (target > 63) target = 63;
    while (volume_steps < target) { pot_step(1); volume_steps++; }
    while (volume_steps > target) { pot_step(0); volume_steps--; }
}

/* force wiper to a known position regardless of power-up state */
static void pot_sync_boot(uint8_t initial) {
    uint8_t i;
    for (i = 0; i < 64; i++) pot_step(0);   /* slam to bottom */
    volume_steps = 0;
    pot_set(initial);
}

/* ── UART2 ──────────────────────────────────────────────────────── */
static void uart2_putch(char c) { while (!U2STAbits.TRMT); U2TXREG = c; }
static void uart2_puts(const char *s) { while (*s) uart2_putch(*s++); uart2_putch('\n'); }

void __attribute__((__interrupt__, no_auto_psv)) _U2RXInterrupt(void) {
    char c = U2RXREG;
    IFS1bits.U2RXIF = 0;
    if (U2STAbits.OERR) U2STAbits.OERR = 0;      /* clear overrun    */
    if (c == '\n' || c == '\r') {
        if (cmd_idx) { cmd_buf[cmd_idx] = 0; cmd_ready = 1; cmd_idx = 0; }
    } else if (cmd_idx < CMD_LEN-1 && !cmd_ready) {
        cmd_buf[cmd_idx++] = c;
    }
}

/* ═══════════════ Timer1 ISR @ 8kHz — INTEGER ONLY ══════════════ */
void __attribute__((__interrupt__, no_auto_psv)) _T1Interrupt(void) {
    /* DDS */
    phase_acc_L += phase_inc_L;
    phase_acc_R += phase_inc_R;
    OC1RS = (uint16_t)((sine_table[phase_acc_L >> 24] * PWM_PERIOD) >> 8);
    OC2RS = (uint16_t)((sine_table[phase_acc_R >> 24] * PWM_PERIOD) >> 8);

    /* --- LED beat gate, sampled at 8kHz so 8-20Hz strobe is clean --- */
    led_phase += led_phase_inc;
    {
        uint16_t ph = (uint16_t)(led_phase >> 16);
        uint8_t eyeA, eyeB;
        switch (led_mode) {
        case MODE_SYNC:
            eyeA = eyeB = (ph < duty_full);
            break;
        case MODE_SOLID:
            eyeA = eyeB = 1;
            break;
        default: /* MODE_ALT: eye A on 1st half, eye B on 2nd half */
            eyeA = (ph < duty_half);
            eyeB = (ph >= 32768) && ((uint16_t)(ph - 32768) < duty_half);
            break;
        }

        /* RGB soft-PWM: 64 levels -> 125Hz repeat */
        pwm6 = (pwm6 + 1) & 0x3F;
        LATBbits.LATB4 = eyeA && (pwm6 < (rgb_current[0] >> 2));
        LATBbits.LATB5 = eyeA && (pwm6 < (rgb_current[1] >> 2));
        LATBbits.LATB6 = eyeA && (pwm6 < (rgb_current[2] >> 2));
        LATBbits.LATB7 = eyeB && (pwm6 < (rgb_current[3] >> 2));
        LATBbits.LATB8 = eyeB && (pwm6 < (rgb_current[4] >> 2));
        LATBbits.LATB9 = eyeB && (pwm6 < (rgb_current[5] >> 2));
    }

    /* wake control task at 31.25Hz */
    if (--ctrl_downct == 0) { ctrl_downct = CTRL_DIV; ctrl_flag = 1; }

    IFS0bits.T1IF = 0;
}

/* ═══════════════ Control task @ 31.25Hz (main loop) ════════════ */
static void control_tick(void) {
    uint8_t i;

    /* --- beat ramp --- */
    if (beat_hz < target_beat) {
        beat_hz += ramp_per_ctrl;
        if (beat_hz > target_beat) beat_hz = target_beat;
    } else if (beat_hz > target_beat) {
        beat_hz -= ramp_per_ctrl;
        if (beat_hz < target_beat) beat_hz = target_beat;
    }

    /* --- recompute DDS increments (only place this happens) --- */
    {
        uint32_t iL = freq_to_inc(carrier_hz);
        uint32_t iR = freq_to_inc(carrier_hz + beat_hz);
        IEC0bits.T1IE = 0;          /* brief atomic window */
        phase_inc_L = iL;
        phase_inc_R = iR;
        IEC0bits.T1IE = 1;
    }

    /* --- sequencer (skipped entirely when SEQ:OFF) --- */
    if (seq_enabled) {
    seq_t++;
    switch (seq_state) {
    case SEQ_BASE:
        if (seq_t >= next_spike) {
            target_beat = SPIKE_BEAT;
            ramp_per_ctrl = (SPIKE_BEAT - BASELINE_BEAT) / (float)RAMP_TICKS;
            event_end = seq_t + SPIKE_LEN_T;
            next_spike = seq_t + SPIKE_EVERY_T;
            seq_state = SEQ_SPIKE;
        } else if (seq_t >= next_dip) {
            target_beat = DIP_BEAT;
            ramp_per_ctrl = (BASELINE_BEAT - DIP_BEAT) / (float)RAMP_TICKS;
            event_end = seq_t + DIP_LEN_T;
            next_dip = seq_t + DIP_EVERY_T;
            seq_state = SEQ_DIP;
        }
        break;
    case SEQ_SPIKE:
    case SEQ_DIP:
        if (seq_t >= event_end) {
            ramp_per_ctrl = (target_beat > BASELINE_BEAT ?
                             target_beat - BASELINE_BEAT :
                             BASELINE_BEAT - target_beat) / (float)RAMP_TICKS;
            target_beat = BASELINE_BEAT;
            seq_state = SEQ_BASE;
        }
        break;
    }
    }   /* end if (seq_enabled) */
    if (ramp_per_ctrl < 0.001f) ramp_per_ctrl = 0.05f;

    /* --- colour fade toward targets --- */
    for (i = 0; i < 6; i++) {
        if (rgb_current[i] < rgb_target[i]) {
            uint8_t d = rgb_target[i] - rgb_current[i];
            rgb_current[i] += (d > FADE_STEP) ? FADE_STEP : d;
        } else if (rgb_current[i] > rgb_target[i]) {
            uint8_t d = rgb_current[i] - rgb_target[i];
            rgb_current[i] -= (d > FADE_STEP) ? FADE_STEP : d;
        }
    }

    /* --- publish LED phase increment + duty thresholds for the ISR ---
       full cycle = 2^32 of phase; per sample: beat_hz * 2^32 / 8000
       = beat_hz * 536870.912                                        */
    {
        uint32_t inc = (uint32_t)(beat_hz * 536870.912f);
        uint32_t dh  = ((uint32_t)32768 * duty_pct) / 100;
        uint32_t df  = ((uint32_t)65536 * duty_pct) / 100;
        if (df > 65535) df = 65535;
        IEC0bits.T1IE = 0;
        led_phase_inc = inc;
        duty_half = (uint16_t)dh;
        duty_full = (uint16_t)df;
        IEC0bits.T1IE = 1;
    }
}

/* ═══════════════ command parser ════════════════════════════════ */
static void process_command(char *cmd) {
    if (!strncmp(cmd, "BEAT:", 5)) {
        float hz = parse_float(cmd+5);
        if (hz >= 0.5f && hz <= 40.0f) {
            ramp_per_ctrl = (hz > beat_hz ? hz - beat_hz : beat_hz - hz)
                            / (float)RAMP_TICKS;
            if (ramp_per_ctrl < 0.001f) ramp_per_ctrl = 0.05f;
            target_beat = hz;
            uart2_puts("OK:BEAT");
        } else uart2_puts("ERR:RANGE");

    } else if (!strncmp(cmd, "CARRIER:", 8)) {
        float hz = parse_float(cmd+8);
        if (hz >= 50.0f && hz <= 1500.0f) {
            carrier_hz = hz;
            uart2_puts("OK:CARRIER");
        } else uart2_puts("ERR:RANGE");

    } else if (!strncmp(cmd, "VOL:", 4)) {
        float v = parse_float(cmd+4);
        if (v >= 0 && v <= 63) { pot_set((uint8_t)v); uart2_puts("OK:VOL"); }
        else uart2_puts("ERR:RANGE");

    } else if (!strncmp(cmd, "RGB_AB:", 7)) {
        uint8_t c[3];
        if (parse_rgb(cmd+7, c)) {
            memcpy(rgb_target,   c, 3);
            memcpy(rgb_target+3, c, 3);
            uart2_puts("OK:RGB_AB");
        } else uart2_puts("ERR:PARSE");

    } else if (!strncmp(cmd, "RGB_A:", 6)) {
        if (parse_rgb(cmd+6, rgb_target)) uart2_puts("OK:RGB_A");
        else uart2_puts("ERR:PARSE");

    } else if (!strncmp(cmd, "RGB_B:", 6)) {
        if (parse_rgb(cmd+6, rgb_target+3)) uart2_puts("OK:RGB_B");
        else uart2_puts("ERR:PARSE");

    } else if (!strncmp(cmd, "SEQ:", 4)) {
        if      (!strcmp(cmd+4, "ON"))  { seq_enabled = 1; uart2_puts("OK:SEQ"); }
        else if (!strcmp(cmd+4, "OFF")) { seq_enabled = 0; uart2_puts("OK:SEQ"); }
        else uart2_puts("ERR:PARSE");

    } else if (!strncmp(cmd, "DUTY:", 5)) {
        float d = parse_float(cmd+5);
        if (d >= 5.0f && d <= 100.0f) { duty_pct = (uint8_t)d; uart2_puts("OK:DUTY"); }
        else uart2_puts("ERR:RANGE");

    } else if (!strncmp(cmd, "MODE:", 5)) {
        if      (!strcmp(cmd+5, "ALT"))   { led_mode = MODE_ALT;   uart2_puts("OK:MODE"); }
        else if (!strcmp(cmd+5, "SYNC"))  { led_mode = MODE_SYNC;  uart2_puts("OK:MODE"); }
        else if (!strcmp(cmd+5, "SOLID")) { led_mode = MODE_SOLID; uart2_puts("OK:MODE"); }
        else uart2_puts("ERR:PARSE");

    } else if (!strcmp(cmd, "STATUS")) {
        char out[72], *b = out;
        uint16_t bx10 = (uint16_t)(beat_hz    * 10.0f + 0.5f);
        uint16_t cx10 = (uint16_t)(carrier_hz * 10.0f + 0.5f);
        memcpy(b, "BEAT:", 5); b += 5;
        b = put_u16(b, bx10/10); *b++ = '.'; *b++ = '0'+bx10%10;
        memcpy(b, " CAR:", 5); b += 5;
        b = put_u16(b, cx10/10); *b++ = '.'; *b++ = '0'+cx10%10;
        memcpy(b, " VOL:", 5); b += 5;
        b = put_u16(b, volume_steps);
        memcpy(b, " MODE:", 6); b += 6;
        {
            const char *m = (led_mode==MODE_ALT) ? "ALT" :
                            (led_mode==MODE_SYNC)? "SYNC" : "SOLID";
            while (*m) *b++ = *m++;
        }
        memcpy(b, " SEQ:", 5); b += 5;
        { const char *q = seq_enabled ? "ON" : "OFF"; while (*q) *b++ = *q++; }
        memcpy(b, " DUTY:", 6); b += 6;
        b = put_u16(b, duty_pct);
        *b = 0;
        uart2_puts(out);

    } else if (!strcmp(cmd, "RESET")) {
        target_beat = BASELINE_BEAT;
        ramp_per_ctrl = 0.05f;
        seq_state = SEQ_BASE;
        seq_t = 0; next_spike = SPIKE_EVERY_T; next_dip = DIP_EVERY_T;
        seq_enabled = 1; duty_pct = 100; led_mode = MODE_ALT;
        uart2_puts("OK:RESET");

    } else uart2_puts("ERR:UNKNOWN");
}

/* ═══════════════ init ══════════════════════════════════════════ */
static void init_gpio(void) {
    ADPCFG = 0xFFFF;                       /* all ANx digital        */
    TRISBbits.TRISB2 = 0;  TRISBbits.TRISB3 = 0;
    TRISBbits.TRISB4 = 0;  TRISBbits.TRISB5 = 0;
    TRISBbits.TRISB6 = 0;  TRISBbits.TRISB7 = 0;
    TRISBbits.TRISB8 = 0;  TRISBbits.TRISB9 = 0;
    TRISBbits.TRISB10 = 0;
    TRISFbits.TRISF4 = 1;  TRISFbits.TRISF5 = 0;   /* U2RX in, U2TX out */
    TRISFbits.TRISF6 = 0;                          /* pot CLK           */
    TRISDbits.TRISD0 = 0;  TRISDbits.TRISD1 = 0;   /* OC1/OC2           */

    POT_CS_HI(); POT_UP(); POT_CLK_LO();
    LATB &= ~0x03F0;                       /* LEDs off               */
    LATBbits.LATB10 = 1;                   /* TPA6132A2 EN — amp ON  */
}

static void init_pwm(void) {
    OC1CON = 0; OC2CON = 0;
    OC1R = 0; OC1RS = 0; OC2R = 0; OC2RS = 0;
    PR2 = PWM_PERIOD;
    T2CONbits.TCKPS = 0;
    OC1CONbits.OCM = 0b110;                /* PWM, fault off         */
    OC2CONbits.OCM = 0b110;
    T2CONbits.TON = 1;
}

static void init_timer1(void) {
    T1CON = 0;
    PR1 = (uint16_t)(FCY / FSAMP - 1);     /* 2499 @ 20 MIPS         */
    IPC0bits.T1IP = 6;
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
    T1CONbits.TON = 1;
}

static void init_uart2(void) {
    U2BRG = (uint16_t)(FCY / (16UL * 9600UL) - 1);   /* 129 */
    U2MODE = 0x8000;
    U2STA  = 0x0400;
    IFS1bits.U2RXIF = 0;
    IEC1bits.U2RXIE = 1;
}

/* ═══════════════ main ══════════════════════════════════════════ */
int main(void) {
    phase_inc_L = freq_to_inc(carrier_hz);
    phase_inc_R = freq_to_inc(carrier_hz + beat_hz);

    init_gpio();
    init_pwm();
    init_uart2();
    pot_sync_boot(32);          /* known volume before audio starts  */
    init_timer1();              /* ISR live from here                */

    uart2_puts("READY:BINAURAL_GOGGLE_V2.2");

    while (1) {
        if (ctrl_flag) { ctrl_flag = 0; control_tick(); }
        if (cmd_ready) { cmd_ready = 0; process_command(cmd_buf); }
    }
    return 0;
}
