/*
 * dspic_firmware.c  -  v3.0
 * Binaural beat + RGB LED goggle controller
 * Target: dsPIC30F4013-20E/P (PDIP-40, socketed), VDD = 3.3 V
 * Board:  AUDIO-CONTROLLER rev 1.1 (Darlington PCB Design), JLCPCB
 *
 * v3.0 is a rewrite against the netlist of the board as manufactured
 * (hardware/schematic/PCB_PCB_AUDIO-CONTROLLER_2_2026-04-19.json).
 * See docs/hardware_review_rev1.1.md for the findings behind each change.
 *
 * -- WHAT CHANGED vs v2.2, AND WHY ------------------------------------
 *  UART   The ESP32 link is on UART1 (RF2 pin 26 / RF3 pin 25), not
 *         UART2. Pins 27/28 are not connected on the board.
 *  UART   The board ties dsPIC U1TX to the ESP32 TXD0 pad and U1RX to the
 *         RXD0 pad. The ESP32-S3 swaps its pins in software. Because the
 *         ESP32 boot ROM prints on that pad at every reset, U1TX is kept
 *         high-impedance except while a reply is being sent, and the
 *         dsPIC never transmits unless it was sent a valid command.
 *  CLOCK  10 MIPS (XT x4 PLL). The -20E part is rated 10 MIPS maximum at
 *         3.0-3.6 V (datasheet table 23-1); v2.2 ran it at 20 MIPS.
 *  AUDIO  PWM and DDS both run at 31.25 kHz with 320 duty levels plus
 *         first-order error feedback, 1024-entry sine table. v2.2 used
 *         an 8 kHz sample rate with 78 duty levels, which put audible
 *         images and steps in the output.
 *  AUDIO  The AD5220 has 128 steps, not 64. Volume is now VOL:0-100 with
 *         a dB law: the pot sets the coarse level and the sine amplitude
 *         trims between pot steps. Volume changes ramp.
 *  AUDIO  The amplifier stays disabled until the PWM has settled and the
 *         pot is at zero, then the volume ramps up. AMP:ON|OFF added.
 *  LED    Flash phase is taken from the phase difference of the two
 *         audio oscillators, so light and beat cannot drift apart.
 *         Brightness PWM restarts at each flash so every flash is equal.
 *  SEQ    Spike and dip are relative to the BEAT setting (x2 and /2).
 *  CONFIG Brown-out reset on at 2.7 V, 64 ms power-up timer.
 *
 * -- PIN MAP (from the rev 1.1 netlist) -------------------------------
 *  OC1/RD0  p34  PWM audio L          OC2/RD1  p33  PWM audio R
 *  RF6      p24  AD5220 CLK (both)    RB2      p4   AD5220 /CS (both)
 *  RB3      p5   AD5220 U/D (both)    RB10     p37  TPA6132A2 EN
 *  U1RX/RF2 p26  <- ESP32 (net RX0)   U1TX/RF3 p25  -> ESP32 (net TX0)
 *  RB4  p6  A_R    RB5  p7  A_G    RB6/PGC p8  A_B
 *  RB7/PGD p9 B_R  RB8 p10  B_G    RB9    p38  B_B
 *  OSC1/OSC2 p13/p14  10 MHz crystal
 *  LED outputs are active high (two 74HC14 stages, then an NPN that
 *  sinks the LED cathode; the goggle LEDs must be common anode to +5 V).
 *
 * -- UART PROTOCOL (9600 8N1, newline-terminated ASCII) ---------------
 *  BEAT:<0.5-40>  CARRIER:<40-1000>  VOL:<0-100>  AMP:ON|OFF
 *  RGB_A:<r>,<g>,<b>  RGB_B:<r>,<g>,<b>  RGB_AB:<r>,<g>,<b>   (0-255)
 *  MODE:ALT|SYNC|SOLID   SEQ:ON|OFF   DUTY:<5-100>   STATUS   RESET
 *  Replies: OK:<CMD>, ERR:RANGE, ERR:PARSE, or the STATUS line.
 *  Lines that are not a known command get NO reply (see UART note above).
 *
 * BUILD: MPLAB X + XC16, optimisation -O1. Add this file to the project;
 *        dds_tables.h must sit next to it.  PROGRAM: PICkit 4 on H1.
 */

#include <xc.h>
#include <stdint.h>
#include <string.h>
#include "dds_tables.h"

/* -- Configuration bits ---------------------------------------------- */
_FOSC(CSW_FSCM_OFF & XT_PLL4);              /* 10 MHz x4 / 4 = 10 MIPS  */
_FWDT(WDT_OFF);
_FBORPOR(PBOR_ON & BORV_27 & PWRT_64 & MCLR_EN);
_FGS(CODE_PROT_OFF);

#define FW_VERSION      "3.0"

#define FCY             10000000UL
#define PWM_PERIOD      320U                /* 10 MHz / 320 = 31.25 kHz */
#define FSAMP           31250UL             /* one sample per PWM cycle */
#define BAUD            9600UL

/* Phase increment per hertz: 2^32 / FSAMP */
#define INC_PER_HZ      137438.953472f

/* Duty values are carried in Q6 (duty count * 64). */
#define DUTY_CENTER_Q6  ((PWM_PERIOD / 2U) * 64U)   /* 50 % duty        */
/* Full-scale sine swings +-136 counts around 160 (7.5 % .. 92.5 %).
   The ISR computes (sine_q15 * amp) >> 16, so amp = 2 * 136 * 64.     */
#define AMP_FULL        17408U

#define LED_DIV_MASK    3U                  /* LED update every 4th sample */
#define LED_PWM_MASK    31U                 /* 32 brightness steps, 244 Hz */
#define CTRL_DIV        1024U               /* control task: 30.52 Hz      */
#define CTRL_HZ         30.517578f
#define TICKS(sec)      ((uint32_t)((sec) * CTRL_HZ + 0.5f))

/* Session sequencer */
#define SPIKE_LEN_T     TICKS(40.0f)
#define DIP_LEN_T       TICKS(90.0f)
#define SPIKE_EVERY_T   TICKS(480.0f)
#define DIP_EVERY_T     TICKS(780.0f)
#define RAMP_TICKS      TICKS(3.0f)

#define BEAT_MIN        0.5f
#define BEAT_MAX        40.0f
#define CARRIER_MIN     40.0f
#define CARRIER_MAX     1000.0f

#define DEFAULT_BEAT    4.0f
#define DEFAULT_CARRIER 200.0f
#define DEFAULT_VOL     30U                 /* percent                    */
#define AMP_AT_BOOT     1                   /* 0: leave the TPA6132A2 shut
                                               down until AMP:ON (use this
                                               if U8 is bypassed)         */
#define VOL_RAMP_STEP   2U                  /* percent per control tick   */
#define FADE_STEP       8U                  /* colour units per tick      */

/* LATB bits */
#define LED_MASK        0x03F0U             /* RB4..RB9                   */
#define BIT_A_R         0x0010U
#define BIT_A_G         0x0020U
#define BIT_A_B         0x0040U
#define BIT_B_R         0x0080U
#define BIT_B_G         0x0100U
#define BIT_B_B         0x0200U

/* 32-bit accumulator with direct access to the high word (the dsPIC is
   little-endian); avoids 32-bit shifts in the interrupt. */
typedef union {
    uint32_t u32;
    int32_t  s32;
    struct { uint16_t lo; uint16_t hi; } w;
    struct { uint16_t lo; int16_t  hi; } sw;
} acc32_t;

typedef enum { MODE_ALT, MODE_SYNC, MODE_SOLID } led_mode_t;
typedef enum { SEQ_BASE, SEQ_SPIKE, SEQ_DIP } seq_state_t;

/* -- State shared with the sample interrupt -------------------------- */
static volatile acc32_t  ph_l, ph_r;            /* oscillator phases    */
static volatile uint32_t inc_l, inc_r;          /* phase increments     */
static volatile int16_t  amp_q = 0;             /* sine amplitude       */
static volatile uint8_t  led_level[6];          /* 0..32 per channel    */
static volatile uint8_t  led_mode = MODE_ALT;
static volatile uint16_t duty_half = 32768U;    /* ALT on-window        */
static volatile uint16_t duty_full = 65535U;    /* SYNC on-window       */
static volatile uint8_t  ctrl_flag = 0;

/* -- Main-loop state -------------------------------------------------- */
static float carrier_hz = DEFAULT_CARRIER;
static float base_beat  = DEFAULT_BEAT;         /* user setting         */
static float beat_hz    = DEFAULT_BEAT;         /* what is playing now  */
static float beat_goal  = DEFAULT_BEAT;         /* ramp target          */
static float ramp_step  = 0.0f;                 /* Hz per control tick  */

static uint8_t rgb_target[6]  = {255, 0, 0, 255, 0, 0};
static uint8_t rgb_current[6] = {0, 0, 0, 0, 0, 0};

static uint8_t     seq_enabled = 1;
static seq_state_t seq_state = SEQ_BASE;
static uint32_t    seq_t = 0, next_spike = 0, next_dip = 0, event_end = 0;

static uint8_t duty_pct = 100;
static uint8_t vol_goal = DEFAULT_VOL;          /* percent              */
static uint8_t vol_cur  = 0;                    /* percent, ramps       */
static uint8_t pot_pos  = 0;                    /* AD5220 step 0..127   */
static uint8_t amp_on   = 0;

/* -- UART receive ------------------------------------------------------ */
#define CMD_LEN 40
static char             rx_buf[CMD_LEN];
static uint8_t          rx_n = 0, rx_dirty = 0;
static char             cmd_buf[CMD_LEN];
static volatile uint8_t cmd_ready = 0;

/* ======================= small helpers ============================== */

/* Busy-wait on free-running Timer3 (1.25 MHz). Max about 50 ms. */
static void delay_us(uint16_t us)
{
    uint16_t counts = us + (us >> 2);           /* x 1.25               */
    uint16_t start = TMR3;
    while ((uint16_t)(TMR3 - start) < counts) { }
}

static void delay_ms(uint16_t ms)
{
    while (ms--) delay_us(1000);
}

/* "12.5" -> 12.5 ; returns -1 for anything that is not a plain number */
static float parse_float(const char *p)
{
    float whole = 0.0f, frac = 0.0f, div = 1.0f;
    uint8_t in_frac = 0, digits = 0;

    for (; *p; p++) {
        if (*p == '.') {
            if (in_frac) return -1.0f;
            in_frac = 1;
        } else if (*p >= '0' && *p <= '9') {
            if (++digits > 7) return -1.0f;
            if (in_frac) { frac = frac * 10.0f + (float)(*p - '0'); div *= 10.0f; }
            else         { whole = whole * 10.0f + (float)(*p - '0'); }
        } else {
            return -1.0f;
        }
    }
    return digits ? whole + frac / div : -1.0f;
}

/* "r,g,b" -> out[3]; returns 1 on success */
static uint8_t parse_rgb(const char *p, uint8_t *out)
{
    uint8_t tmp[3];
    uint16_t v = 0;
    uint8_t n = 0, seen = 0;

    for (;; p++) {
        if (*p >= '0' && *p <= '9') {
            v = v * 10U + (uint16_t)(*p - '0');
            if (v > 255U) return 0;
            seen = 1;
        } else if (*p == ',' || *p == '\0') {
            if (!seen || n >= 3) return 0;
            tmp[n++] = (uint8_t)v;
            v = 0; seen = 0;
            if (*p == '\0') break;
        } else {
            return 0;
        }
    }
    if (n != 3) return 0;
    out[0] = tmp[0]; out[1] = tmp[1]; out[2] = tmp[2];
    return 1;
}

static char *put_str(char *b, const char *s)
{
    while (*s) *b++ = *s++;
    return b;
}

static char *put_u16(char *b, uint16_t v)
{
    char tmp[6];
    uint8_t i = 0;
    do { tmp[i++] = (char)('0' + v % 10U); v /= 10U; } while (v);
    while (i) *b++ = tmp[--i];
    return b;
}

/* value * 10 printed with one decimal */
static char *put_x10(char *b, uint16_t x10)
{
    b = put_u16(b, x10 / 10U);
    *b++ = '.';
    *b++ = (char)('0' + x10 % 10U);
    return b;
}

/* ======================= AD5220 volume pots ========================= */
/* Both pots share CLK, /CS and U/D, so left and right always match.
   The wiper moves one step on each falling CLK edge while /CS is low. */

#define POT_CS_LO()   (LATBbits.LATB2 = 0)
#define POT_CS_HI()   (LATBbits.LATB2 = 1)
#define POT_DIR_UP()  (LATBbits.LATB3 = 1)
#define POT_DIR_DN()  (LATBbits.LATB3 = 0)
#define POT_CLK_LO()  (LATFbits.LATF6 = 0)
#define POT_CLK_HI()  (LATFbits.LATF6 = 1)

static void pot_step(uint8_t up)
{
    if (up) POT_DIR_UP(); else POT_DIR_DN();
    Nop();
    POT_CS_LO();  Nop(); Nop();
    POT_CLK_HI(); Nop(); Nop();
    POT_CLK_LO(); Nop(); Nop();
    POT_CS_HI();  Nop();
}

static void pot_goto(uint8_t target)
{
    if (target > POT_STEPS - 1) target = POT_STEPS - 1;
    while (pot_pos < target) { pot_step(1); pot_pos++; }
    while (pot_pos > target) { pot_step(0); pot_pos--; }
}

/* The AD5220 powers up at mid-scale and has no read-back: drive it to
   the bottom stop so the tracked position is known. */
static void pot_home(void)
{
    uint8_t i;
    for (i = 0; i < POT_STEPS + 4; i++) pot_step(0);
    pot_pos = 0;
}

/* Set output level for a volume in percent. The pot gives the coarse
   level (and attenuates the DAC noise with it); the sine amplitude
   trims the remainder so every percent is a 0.48 dB step. */
static void apply_volume(uint8_t pct)
{
    uint16_t g, a;
    uint32_t trim;
    uint8_t n;

    if (pct == 0) {
        amp_q = 0;
        pot_goto(0);
        return;
    }
    if (pct > 100) pct = 100;

    g = vol_gain_q16[pct];
    for (n = 1; n < POT_STEPS - 1 && pot_gain_q16[n] < g; n++) { }

    trim = ((uint32_t)g << 15) / pot_gain_q16[n];        /* Q15, <= 1.0 */
    if (trim > 32768UL) trim = 32768UL;
    a = (uint16_t)(((uint32_t)AMP_FULL * trim) >> 15);

    /* Order the two changes so the level never overshoots in between. */
    if (n > pot_pos) { amp_q = (int16_t)a; pot_goto(n); }
    else             { pot_goto(n); amp_q = (int16_t)a; }
}

static void amp_enable(uint8_t on)
{
    /* constant bit writes compile to single BSET/BCLR instructions,
       which cannot be torn by the sample interrupt's LATB update */
    if (on) { amp_on = 1; LATBbits.LATB10 = 1; }
    else    { amp_on = 0; LATBbits.LATB10 = 0; }
}

/* ======================= UART1 ====================================== */

/* Send one line. The transmitter only drives the pin for the duration
   of the line; the rest of the time RF3 is a high-impedance input and
   the ESP32's pull-up holds the line idle. */
static void tx_line(const char *s)
{
    U1STAbits.UTXEN = 1;
    delay_us(400);                      /* idle-high lead-in            */
    while (*s) {
        while (U1STAbits.UTXBF) { }
        U1TXREG = (uint8_t)*s++;
    }
    while (U1STAbits.UTXBF) { }
    U1TXREG = '\n';
    delay_us(1500);                     /* let the last byte reach TSR  */
    while (!U1STAbits.TRMT) { }
    delay_us(400);
    U1STAbits.UTXEN = 0;
}

void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void)
{
    IFS0bits.U1RXIF = 0;

    while (U1STAbits.URXDA) {
        uint8_t bad = (uint8_t)(U1STAbits.FERR | U1STAbits.PERR);
        char c = (char)U1RXREG;

        if (!bad && (c == '\n' || c == '\r')) {
            if (rx_n && !rx_dirty && !cmd_ready) {
                rx_buf[rx_n] = '\0';
                memcpy(cmd_buf, rx_buf, (size_t)rx_n + 1U);
                cmd_ready = 1;
            }
            rx_n = 0;
            rx_dirty = 0;
        } else if (bad || c < 0x20 || c > 0x7E) {
            rx_dirty = 1;               /* noise: drop the whole line   */
        } else if (rx_n < CMD_LEN - 1) {
            rx_buf[rx_n++] = c;
        } else {
            rx_dirty = 1;               /* too long                     */
        }
    }
    if (U1STAbits.OERR) {
        U1STAbits.OERR = 0;
        rx_dirty = 1;
    }
}

/* ============ Sample interrupt: 31.25 kHz, integer only ============= */

void __attribute__((interrupt, auto_psv)) _T2Interrupt(void)
{
    static uint16_t err_l = 0, err_r = 0;       /* quantiser residue    */
    static uint16_t ctrl_ct = CTRL_DIV;
    static uint8_t  sub = 0, pwm_ct = 0, was_a = 0, was_b = 0;
    acc32_t p;
    uint16_t s;

    IFS0bits.T2IF = 0;

    /* --- two sine oscillators -> PWM duty (latched next period) ----- */
    ph_l.u32 += inc_l;
    ph_r.u32 += inc_r;

    p.s32 = __builtin_mulss(sine_tab[ph_l.w.hi >> 6], amp_q);
    s = (uint16_t)(DUTY_CENTER_Q6 + p.sw.hi) + err_l;
    OC1RS = s >> 6;
    err_l = s & 63U;

    p.s32 = __builtin_mulss(sine_tab[ph_r.w.hi >> 6], amp_q);
    s = (uint16_t)(DUTY_CENTER_Q6 + p.sw.hi) + err_r;
    OC2RS = s >> 6;
    err_r = s & 63U;

    /* --- LEDs, every 4th sample (7.8 kHz) ---------------------------- */
    if ((++sub & LED_DIV_MASK) == 0) {
        uint16_t beat, m = 0;
        uint8_t a, b;

        /* Beat phase = phase lead of the right oscillator over the left */
        p.u32 = ph_r.u32 - ph_l.u32;
        beat = p.w.hi;

        if (led_mode == MODE_SOLID) {
            a = 1; b = 1;
        } else if (led_mode == MODE_SYNC) {
            a = (beat < duty_full);
            b = a;
        } else {                                /* MODE_ALT             */
            a = (beat < duty_half);
            b = (beat >= 32768U) && ((uint16_t)(beat - 32768U) < duty_half);
        }

        /* Restart the brightness PWM at each flash so flashes match. */
        if ((a && !was_a) || (b && !was_b)) pwm_ct = 0;
        else pwm_ct = (pwm_ct + 1U) & LED_PWM_MASK;
        was_a = a; was_b = b;

        if (a) {
            if (pwm_ct < led_level[0]) m |= BIT_A_R;
            if (pwm_ct < led_level[1]) m |= BIT_A_G;
            if (pwm_ct < led_level[2]) m |= BIT_A_B;
        }
        if (b) {
            if (pwm_ct < led_level[3]) m |= BIT_B_R;
            if (pwm_ct < led_level[4]) m |= BIT_B_G;
            if (pwm_ct < led_level[5]) m |= BIT_B_B;
        }
        LATB = (LATB & ~LED_MASK) | m;
    }

    if (--ctrl_ct == 0) { ctrl_ct = CTRL_DIV; ctrl_flag = 1; }
}

/* ============ Control task: 30.5 Hz, runs in the main loop ========== */

static void set_beat_goal(float hz)
{
    float d = hz > beat_hz ? hz - beat_hz : beat_hz - hz;
    beat_goal = hz;
    ramp_step = d / (float)RAMP_TICKS;
}

static void seq_rearm(void)
{
    seq_state  = SEQ_BASE;
    next_spike = seq_t + SPIKE_EVERY_T;
    next_dip   = seq_t + DIP_EVERY_T;
}

static void publish_oscillators(void)
{
    uint32_t il = (uint32_t)(carrier_hz * INC_PER_HZ);
    uint32_t ir = il + (uint32_t)(beat_hz * INC_PER_HZ);

    IEC0bits.T2IE = 0;
    inc_l = il;
    inc_r = ir;
    IEC0bits.T2IE = 1;
}

static void publish_duty(void)
{
    uint32_t dh = (32768UL * duty_pct) / 100UL;
    uint32_t df = (65536UL * duty_pct) / 100UL;
    if (df > 65535UL) df = 65535UL;

    IEC0bits.T2IE = 0;
    duty_half = (uint16_t)dh;
    duty_full = (uint16_t)df;
    IEC0bits.T2IE = 1;
}

static void control_tick(void)
{
    uint8_t i;

    /* --- beat ramp --- */
    if (beat_hz != beat_goal) {
        if (beat_hz < beat_goal) {
            beat_hz += ramp_step;
            if (beat_hz > beat_goal) beat_hz = beat_goal;
        } else {
            beat_hz -= ramp_step;
            if (beat_hz < beat_goal) beat_hz = beat_goal;
        }
        if (ramp_step <= 0.0f) beat_hz = beat_goal;
    }
    publish_oscillators();

    /* --- session sequencer: spike to 2x, dip to 1/2 of the set beat --- */
    seq_t++;
    if (seq_enabled) {
        if (seq_state == SEQ_BASE) {
            if (seq_t >= next_spike) {
                float hz = base_beat * 2.0f;
                set_beat_goal(hz > BEAT_MAX ? BEAT_MAX : hz);
                event_end  = seq_t + SPIKE_LEN_T;
                next_spike = seq_t + SPIKE_EVERY_T;
                seq_state  = SEQ_SPIKE;
            } else if (seq_t >= next_dip) {
                float hz = base_beat * 0.5f;
                set_beat_goal(hz < BEAT_MIN ? BEAT_MIN : hz);
                event_end = seq_t + DIP_LEN_T;
                next_dip  = seq_t + DIP_EVERY_T;
                seq_state = SEQ_DIP;
            }
        } else if (seq_t >= event_end) {
            set_beat_goal(base_beat);
            seq_state = SEQ_BASE;
        }
    }

    /* --- colour fade toward targets, then publish 0..32 levels --- */
    for (i = 0; i < 6; i++) {
        uint8_t c = rgb_current[i], t = rgb_target[i];
        if (c < t)      c = (uint8_t)((t - c > FADE_STEP) ? c + FADE_STEP : t);
        else if (c > t) c = (uint8_t)((c - t > FADE_STEP) ? c - FADE_STEP : t);
        rgb_current[i] = c;
        led_level[i] = (uint8_t)(((uint16_t)c + 4U) >> 3);
    }

    /* --- volume ramp --- */
    if (vol_cur != vol_goal) {
        if (vol_cur < vol_goal)
            vol_cur = (uint8_t)((vol_goal - vol_cur > VOL_RAMP_STEP)
                                ? vol_cur + VOL_RAMP_STEP : vol_goal);
        else
            vol_cur = (uint8_t)((vol_cur - vol_goal > VOL_RAMP_STEP)
                                ? vol_cur - VOL_RAMP_STEP : vol_goal);
        apply_volume(vol_cur);
    }
}

/* ======================= command parser ============================= */

static void send_status(void)
{
    char out[96], *b = out;

    b = put_str(b, "BEAT:");   b = put_x10(b, (uint16_t)(beat_hz * 10.0f + 0.5f));
    b = put_str(b, " CAR:");   b = put_x10(b, (uint16_t)(carrier_hz * 10.0f + 0.5f));
    b = put_str(b, " VOL:");   b = put_u16(b, vol_goal);
    b = put_str(b, " MODE:");
    b = put_str(b, led_mode == MODE_ALT ? "ALT" :
                   led_mode == MODE_SYNC ? "SYNC" : "SOLID");
    b = put_str(b, " SEQ:");   b = put_str(b, seq_enabled ? "ON" : "OFF");
    b = put_str(b, " DUTY:");  b = put_u16(b, duty_pct);
    b = put_str(b, " AMP:");   b = put_str(b, amp_on ? "ON" : "OFF");
    b = put_str(b, " FW:" FW_VERSION);
    *b = '\0';
    tx_line(out);
}

/* returns 1 for "ON", 0 for "OFF", 2 otherwise */
static uint8_t parse_onoff(const char *p)
{
    if (!strcmp(p, "ON"))  return 1;
    if (!strcmp(p, "OFF")) return 0;
    return 2;
}

static void process_command(const char *cmd)
{
    if (!strncmp(cmd, "BEAT:", 5)) {
        float hz = parse_float(cmd + 5);
        if (hz >= BEAT_MIN && hz <= BEAT_MAX) {
            base_beat = hz;
            seq_state = SEQ_BASE;           /* a manual beat ends an event */
            set_beat_goal(hz);
            tx_line("OK:BEAT");
        } else tx_line(hz < 0.0f ? "ERR:PARSE" : "ERR:RANGE");

    } else if (!strncmp(cmd, "CARRIER:", 8)) {
        float hz = parse_float(cmd + 8);
        if (hz >= CARRIER_MIN && hz <= CARRIER_MAX) {
            carrier_hz = hz;
            tx_line("OK:CARRIER");
        } else tx_line(hz < 0.0f ? "ERR:PARSE" : "ERR:RANGE");

    } else if (!strncmp(cmd, "VOL:", 4)) {
        float v = parse_float(cmd + 4);
        if (v >= 0.0f && v <= 100.0f) {
            vol_goal = (uint8_t)(v + 0.5f);
            tx_line("OK:VOL");
        } else tx_line(v < 0.0f ? "ERR:PARSE" : "ERR:RANGE");

    } else if (!strncmp(cmd, "AMP:", 4)) {
        uint8_t on = parse_onoff(cmd + 4);
        if (on == 2) tx_line("ERR:PARSE");
        else {
            if (on && !amp_on) {            /* come up from silence     */
                vol_cur = 0;
                apply_volume(0);
            }
            amp_enable(on);
            tx_line("OK:AMP");
        }

    } else if (!strncmp(cmd, "RGB_AB:", 7)) {
        if (parse_rgb(cmd + 7, rgb_target)) {
            rgb_target[3] = rgb_target[0];
            rgb_target[4] = rgb_target[1];
            rgb_target[5] = rgb_target[2];
            tx_line("OK:RGB_AB");
        } else tx_line("ERR:PARSE");

    } else if (!strncmp(cmd, "RGB_A:", 6)) {
        tx_line(parse_rgb(cmd + 6, rgb_target) ? "OK:RGB_A" : "ERR:PARSE");

    } else if (!strncmp(cmd, "RGB_B:", 6)) {
        tx_line(parse_rgb(cmd + 6, rgb_target + 3) ? "OK:RGB_B" : "ERR:PARSE");

    } else if (!strncmp(cmd, "MODE:", 5)) {
        if      (!strcmp(cmd + 5, "ALT"))   { led_mode = MODE_ALT;   tx_line("OK:MODE"); }
        else if (!strcmp(cmd + 5, "SYNC"))  { led_mode = MODE_SYNC;  tx_line("OK:MODE"); }
        else if (!strcmp(cmd + 5, "SOLID")) { led_mode = MODE_SOLID; tx_line("OK:MODE"); }
        else tx_line("ERR:PARSE");

    } else if (!strncmp(cmd, "SEQ:", 4)) {
        uint8_t on = parse_onoff(cmd + 4);
        if (on == 2) tx_line("ERR:PARSE");
        else {
            if (seq_state != SEQ_BASE) set_beat_goal(base_beat);
            seq_enabled = on;
            seq_rearm();
            tx_line("OK:SEQ");
        }

    } else if (!strncmp(cmd, "DUTY:", 5)) {
        float d = parse_float(cmd + 5);
        if (d >= 5.0f && d <= 100.0f) {
            duty_pct = (uint8_t)(d + 0.5f);
            publish_duty();
            tx_line("OK:DUTY");
        } else tx_line(d < 0.0f ? "ERR:PARSE" : "ERR:RANGE");

    } else if (!strcmp(cmd, "STATUS")) {
        send_status();

    } else if (!strcmp(cmd, "RESET")) {
        base_beat   = DEFAULT_BEAT;
        carrier_hz  = DEFAULT_CARRIER;
        set_beat_goal(DEFAULT_BEAT);
        seq_enabled = 1;
        seq_rearm();
        duty_pct = 100;
        publish_duty();
        led_mode = MODE_ALT;
        tx_line("OK:RESET");
    }
    /* Anything else: stay silent. Noise from the shared programming
       header must never make this chip drive the line. */
}

/* ======================= initialisation ============================= */

static void init_gpio(void)
{
    ADPCFG = 0xFFFF;                    /* all AN pins digital          */

    /* Port B: RB2 pot /CS (idle high), RB3 U/D, RB4-9 LEDs (off),
       RB10 amp EN (off). RB0, RB1, RB11, RB12 are unconnected: drive
       them low rather than leave them floating. */
    LATB  = 0x0004;
    TRISB = 0x0000;

    /* Port F: RF2 = U1RX in, RF3 = U1TX (input until a reply is sent),
       RF6 = pot CLK. RF0, RF1, RF4, RF5 unconnected -> outputs low. */
    LATF  = 0x0000;
    TRISF = 0x000C;

    /* Port D: RD0/RD1 are OC1/OC2. RD2, RD3, RD8, RD9 unconnected. */
    LATD  = 0x0000;
    TRISD = 0x0000;

    /* Unconnected RC13, RC14, RA11 -> outputs low. */
    LATCbits.LATC13 = 0;  LATCbits.LATC14 = 0;
    TRISCbits.TRISC13 = 0; TRISCbits.TRISC14 = 0;
    LATAbits.LATA11 = 0;
    TRISAbits.TRISA11 = 0;
}

static void init_timer3(void)
{
    T3CON = 0x8010;                     /* on, 1:8 -> 1.25 MHz, no IRQ  */
}

static void init_uart1(void)
{
    U1BRG  = (uint16_t)(FCY / (16UL * BAUD) - 1UL);     /* 64 -> 9615   */
    U1MODE = 0x8000;                    /* enable, 8N1, main pins       */
    U1STA  = 0x0000;                    /* transmitter off for now      */
    IFS0bits.U1RXIF = 0;
    IEC0bits.U1RXIE = 1;
}

static void init_pwm(void)
{
    T2CON = 0;                          /* 1:1, internal clock          */
    TMR2  = 0;
    PR2   = PWM_PERIOD - 1U;
    OC1R  = PWM_PERIOD / 2U;  OC1RS = PWM_PERIOD / 2U;
    OC2R  = PWM_PERIOD / 2U;  OC2RS = PWM_PERIOD / 2U;
    OC1CON = 0x0006;                    /* PWM on Timer2, no fault pin  */
    OC2CON = 0x0006;
    IPC1bits.T2IP = 6;                  /* above the UART (4)           */
    IFS0bits.T2IF = 0;
    IEC0bits.T2IE = 1;
    T2CONbits.TON = 1;
}

/* ======================= main ======================================= */

int main(void)
{
    init_gpio();                        /* amp off, LEDs off            */
    init_timer3();
    pot_home();                         /* wiper to zero                */

    publish_oscillators();
    publish_duty();
    seq_rearm();

    init_uart1();
    init_pwm();                         /* 50 % duty, silent            */

    delay_ms(300);                      /* let the RC filter settle     */
    amp_enable(AMP_AT_BOOT);
    delay_ms(20);                       /* amplifier start-up           */
    /* vol_cur is 0; control_tick() now ramps up to DEFAULT_VOL. */

    for (;;) {
        if (ctrl_flag) {
            ctrl_flag = 0;
            control_tick();
        }
        if (cmd_ready) {
            process_command(cmd_buf);
            cmd_ready = 0;
        }
    }
    return 0;
}
