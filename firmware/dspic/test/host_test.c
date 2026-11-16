/* host_test.c - runs the dsPIC firmware logic on a PC.
 *
 *   clang -std=c99 -Wall -Wextra -I. -o /tmp/host_test host_test.c -lm && /tmp/host_test
 *
 * Checks tone frequencies, PWM range, LED timing, the command parser,
 * the volume law and the sequencer. It cannot check interrupt timing or
 * anything electrical; that needs the real chip.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xc.h"

#define main fw_main
#include "../src/dspic_firmware.c"
#undef main

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
    printf("\n"); } } while (0)

/* one sample period, plus the control task when it is due */
static void sample(void)
{
    _T2Interrupt();
    if (ctrl_flag) { ctrl_flag = 0; control_tick(); }
}
static void run_seconds(double s)
{
    long n = (long)(s * FSAMP);
    while (n--) sample();
}

/* send a line the way the ESP32 does, return the reply (may be "") */
static const char *cmd(const char *line)
{
    static char reply[256];
    unsigned start = mock_tx_n, i, n = 0;
    char full[96];
    snprintf(full, sizeof full, "\n%s\n", line);
    for (i = 0; full[i]; i++) {
        mock_rx_byte = (uint8_t)full[i];
        U1STAbits.URXDA = 1;
        _U1RXInterrupt();
    }
    if (cmd_ready) { process_command(cmd_buf); cmd_ready = 0; }
    for (i = start; i < mock_tx_n && n < sizeof reply - 1; i++)
        if (mock_tx[i] != '\n') reply[n++] = (char)mock_tx[i];
    reply[n] = 0;
    return reply;
}
static void rx_raw(uint8_t b, int framing_error)
{
    mock_rx_byte = b; U1STAbits.URXDA = 1; U1STAbits.FERR = framing_error;
    _U1RXInterrupt();
    U1STAbits.FERR = 0;
}

/* frequency of a duty stream by quadrature correlation around f0 */
static double measure_hz(const uint16_t *d, long n, double f0)
{
    double best = f0, bestp = -1;
    for (double f = f0 - 0.5; f <= f0 + 0.5; f += 0.005) {
        double re = 0, im = 0;
        for (long i = 0; i < n; i++) {
            double w = 2 * M_PI * f * i / FSAMP, x = d[i] - 160.0;
            re += x * cos(w); im += x * sin(w);
        }
        if (re * re + im * im > bestp) { bestp = re * re + im * im; best = f; }
    }
    return best;
}

int main(void)
{
    /* ---- boot, as main() does ---- */
    init_gpio(); init_timer3(); pot_home();
    publish_oscillators(); publish_duty(); seq_rearm();
    init_uart1(); init_pwm();
    amp_enable(1);
    CHECK(PR2 == 319, "PR2=%u", PR2);
    CHECK(U1BRG == 64, "U1BRG=%u", U1BRG);
    CHECK((TRISF & 0x0008) != 0, "U1TX pin must start as an input");
    CHECK(OC1RS == 160 && OC2RS == 160, "PWM must start at 50%%");
    CHECK(amp_q == 0 && pot_pos == 0, "must boot silent");

    run_seconds(2.0);
    CHECK(vol_cur == DEFAULT_VOL, "volume ramp ended at %u", vol_cur);

    /* ---- tones: 200 Hz left, 204 Hz right ---- */
    cmd("SEQ:OFF"); cmd("VOL:100");
    run_seconds(3.0);
    {
        long n = 4 * FSAMP, i, lo = 999, hi = 0; double mean = 0;
        uint16_t *l = malloc(n * sizeof *l), *r = malloc(n * sizeof *r);
        for (i = 0; i < n; i++) {
            sample(); l[i] = OC1RS; r[i] = OC2RS;
            if (l[i] < lo) lo = l[i];
            if (l[i] > hi) hi = l[i];
            mean += l[i];
        }
        double fl = measure_hz(l, n, 200.0), fr = measure_hz(r, n, 204.0);
        printf("tones: L=%.3f Hz R=%.3f Hz, duty %ld..%ld of 320, mean %.2f\n",
               fl, fr, lo, hi, mean / n);
        CHECK(fabs(fl - 200.0) < 0.02, "left %.3f", fl);
        CHECK(fabs(fr - 204.0) < 0.02, "right %.3f", fr);
        CHECK(lo >= 8 && hi <= 312, "duty out of range %ld..%ld", lo, hi);
        CHECK(fabs(mean / n - 160.0) < 0.5, "mean duty %.2f", mean / n);
        free(l); free(r);
    }

    /* ---- LED gate follows the 4 Hz beat, eyes alternate ---- */
    cmd("RGB_AB:255,0,0");
    run_seconds(2.0);
    {
        long n = 5 * FSAMP, i, rises = 0, both = 0, on_a = 0;
        int prev = 0;
        for (i = 0; i < n; i++) {
            sample();
            int a = (LATB & BIT_A_R) != 0, b = (LATB & BIT_B_R) != 0;
            if (a && !prev) rises++;
            if (a && b) both++;
            on_a += a; prev = a;
        }
        printf("LED ALT: %ld flashes in 5 s, eye A on %.1f %%, overlap %ld\n",
               rises, 100.0 * on_a / n, both);
        CHECK(rises >= 19 && rises <= 21, "flash count %ld", rises);
        CHECK(both == 0, "eyes overlapped in ALT mode");
        CHECK(fabs(100.0 * on_a / n - 50.0) < 2.0, "ALT duty %.1f", 100.0 * on_a / n);
        CHECK((LATB & (BIT_A_G | BIT_A_B | BIT_B_G | BIT_B_B)) == 0, "stray colour");
    }
    cmd("MODE:SYNC"); cmd("DUTY:25"); cmd("BEAT:10.0");
    run_seconds(4.0);
    {
        long n = 5 * FSAMP, i, rises = 0, diff = 0, on_a = 0;
        int prev = 0;
        for (i = 0; i < n; i++) {
            sample();
            int a = (LATB & BIT_A_R) != 0, b = (LATB & BIT_B_R) != 0;
            if (a && !prev) rises++;
            if (a != b) diff++;
            on_a += a; prev = a;
        }
        printf("LED SYNC 10 Hz 25%%: %ld flashes in 5 s, on %.1f %%\n",
               rises, 100.0 * on_a / n);
        CHECK(rises >= 49 && rises <= 51, "flash count %ld", rises);
        CHECK(diff == 0, "eyes differ in SYNC mode");
        CHECK(fabs(100.0 * on_a / n - 25.0) < 1.5, "SYNC duty %.1f", 100.0 * on_a / n);
    }
    /* half brightness -> about half the on-time inside each flash */
    cmd("MODE:SOLID"); cmd("RGB_AB:128,0,0");
    run_seconds(1.0);
    {
        long n = FSAMP, i, on = 0;
        for (i = 0; i < n; i++) { sample(); on += (LATB & BIT_A_R) != 0; }
        CHECK(fabs(100.0 * on / n - 50.0) < 4.0, "50%% brightness gave %.1f", 100.0 * on / n);
    }

    /* ---- command parser ---- */
    CHECK(!strcmp(cmd("BEAT:4.0"), "OK:BEAT"), "BEAT");
    CHECK(!strcmp(cmd("BEAT:99"), "ERR:RANGE"), "BEAT range");
    CHECK(!strcmp(cmd("BEAT:abc"), "ERR:PARSE"), "BEAT parse");
    CHECK(!strcmp(cmd("CARRIER:432.5"), "OK:CARRIER"), "CARRIER");
    CHECK(!strcmp(cmd("RGB_A:1,2"), "ERR:PARSE"), "RGB short");
    CHECK(!strcmp(cmd("RGB_A:1,2,300"), "ERR:PARSE"), "RGB big");
    CHECK(!strcmp(cmd("RGB_B:0,0,255"), "OK:RGB_B"), "RGB_B");
    CHECK(!strcmp(cmd("MODE:ALT"), "OK:MODE"), "MODE");
    CHECK(!strcmp(cmd("VOL:101"), "ERR:RANGE"), "VOL range");
    CHECK(!strcmp(cmd("VOL:30"), "OK:VOL"), "VOL");
    CHECK(!strcmp(cmd("DUTY:100"), "OK:DUTY"), "DUTY");
    CHECK(!strcmp(cmd("HELLO"), ""), "unknown command must get no reply");
    CHECK(!strcmp(cmd("ERR:UNKNOWN"), ""), "echo must get no reply");
    run_seconds(4.0);
    printf("status: %s\n", cmd("STATUS"));
    CHECK(!strcmp(cmd("STATUS"),
          "BEAT:4.0 CAR:432.5 VOL:30 MODE:ALT SEQ:OFF DUTY:100 AMP:ON FW:3.0"),
          "STATUS was '%s'", cmd("STATUS"));
    /* line noise, then a framing error inside a valid-looking command */
    { unsigned before = mock_tx_n; int i;
      for (i = 0; i < 2000; i++) rx_raw((uint8_t)(rand() & 0xFF), rand() & 1);
      rx_raw('\n', 0);
      rx_raw('S', 0); rx_raw('T', 1); rx_raw('A', 0); rx_raw('\n', 0);
      if (cmd_ready) { process_command(cmd_buf); cmd_ready = 0; }
      CHECK(mock_tx_n == before, "noise produced %u bytes of reply", mock_tx_n - before); }
    CHECK(!strcmp(cmd("AMP:OFF"), "OK:AMP") && !LATBbits.LATB10, "AMP:OFF");
    CHECK(!strcmp(cmd("AMP:ON"), "OK:AMP") && LATBbits.LATB10 && amp_q == 0, "AMP:ON");
    CHECK(mock_tx_while_disabled == 0, "wrote U1TXREG with transmitter off");
    CHECK(U1STAbits.UTXEN == 0, "transmitter left enabled");

    /* ---- volume law: every percent lands within 2 % of its target ---- */
    {
        int p; double prev = 0, worst = 0;
        for (p = 1; p <= 100; p++) {
            apply_volume((uint8_t)p);
            double want = pow(10, (-12.0 - 0.48 * (100 - p)) / 20);
            double got = pot_gain_q16[pot_pos] / 65536.0 * amp_q / AMP_FULL;
            double e = fabs(got / want - 1);
            if (e > worst) worst = e;
            CHECK(got > prev, "volume not monotonic at %d", p);
            CHECK(amp_q > 0 && amp_q <= (int)AMP_FULL, "amp_q %d at %d", amp_q, p);
            prev = got;
        }
        printf("volume law: worst error %.2f %% over VOL 1..100\n", worst * 100);
        CHECK(worst < 0.03, "volume error %.3f", worst);
        apply_volume(0);
        CHECK(amp_q == 0 && pot_pos == 0, "VOL 0 must mute");
    }

    /* ---- sequencer: spike to 8 Hz at 8 min, back to 4, dip to 2 at 13 ---- */
    cmd("RESET");
    {
        long t; double at_500 = 0, at_560 = 0, at_800 = 0, at_900 = 0;
        for (t = 0; t < (long)(900 * CTRL_HZ); t++) {
            control_tick();
            if (t == (long)(500 * CTRL_HZ)) at_500 = beat_hz;
            if (t == (long)(560 * CTRL_HZ)) at_560 = beat_hz;
            if (t == (long)(800 * CTRL_HZ)) at_800 = beat_hz;
        }
        at_900 = beat_hz;
        printf("sequencer: 500 s %.1f Hz, 560 s %.1f Hz, 800 s %.1f Hz, 900 s %.1f Hz\n",
               at_500, at_560, at_800, at_900);
        CHECK(at_500 == 8.0, "spike"); CHECK(at_560 == 4.0, "after spike");
        CHECK(at_800 == 2.0, "dip");   CHECK(at_900 == 4.0, "after dip");
    }

    printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
    return failures != 0;
}
