/*
 * main.cpp — ESP32 Firmware v2.2, Binaural Beat Goggle Controller
 * Board: Darlington PCB rev 1.1, ESP32-WROOM-32E (SMD)
 *
 * ── CRITICAL CHANGE vs v2.0 ─────────────────────────────────────────
 * The PCB routes the dsPIC's UART2 to the ESP32's **UART0**
 * (GPIO1/GPIO3 = the same TX0/RX0 nets as programming header H3).
 * There are no GPIO16/17 (RX2/TX2) nets on this board.
 * Therefore:
 *   - dsPIC comms use Serial (UART0) at 9600 — NOT Serial2
 *   - NO debug prints on Serial. Ever. The dsPIC would reply
 *     ERR:UNKNOWN to each one; echoing that back creates an infinite
 *     loop. Debug goes to an in-memory ring buffer served at /log.
 *   - FLASH THE ESP32 WITH THE dsPIC REMOVED FROM ITS SOCKET
 *     (they share the UART; the dsPIC's TX would fight the adapter).
 *
 * VERIFY BEFORE FIRST USE (board unpowered, dsPIC socketed):
 *   continuity H3 pin5 (TX0) <-> dsPIC socket pin 28 (U2RX/RF4)
 *   continuity H3 pin6 (RX0) <-> dsPIC socket pin 27 (U2TX/RF5)
 * Both beep  -> UART0 confirmed, leave DSPIC_ON_UART0 = 1.
 * Neither    -> set DSPIC_ON_UART0 0 (falls back to GPIO16/17).
 *
 * ── VIN sensing ─────────────────────────────────────────────────────
 * The 100k/33k divider feeds VIN to GPIO0 (ADC2_CH1). ADC2 is
 * unavailable while WiFi runs, so VIN is sampled once at boot and
 * reported in the UI. 9V in -> ~2.23V at the pin.
 *
 * FLASH: via H3 (GND/3V3/EN/IO0/TX0/RX0) using the ESP32 DevKitC as
 * the USB-UART bridge (DevKit EN held low so its own chip stays
 * quiet), dsPIC OUT of socket. After first flash: OTA.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>

#define DSPIC_ON_UART0  1        /* 1 = Serial/GPIO1+3 (per PCB)     */
#define DSPIC_BAUD      9600
#define VIN_ADC_PIN     0        /* GPIO0: 100k/33k divider from VIN */
#define VIN_SCALE       (3.3f / 4095.0f * (133.0f / 33.0f))

#define AP_SSID  "BinauralGoggle"
#define AP_PASS  "meditate123"
/* #define STA_SSID "home_ssid"
   #define STA_PASS "home_pass"  */

#if DSPIC_ON_UART0
  #define DSPIC Serial
#else
  #define DSPIC Serial2          /* fallback: GPIO16 RX / GPIO17 TX  */
#endif

WebServer server(80);
static float g_vin_boot = 0.0f;

/* ── debug ring buffer, served at /log (replaces Serial debug) ──── */
#define LOGN 24
static String logbuf[LOGN];
static uint8_t loghead = 0;
static void weblog(const String &s) {
    logbuf[loghead] = s;
    loghead = (loghead + 1) % LOGN;
}

/* ── dsPIC I/O ──────────────────────────────────────────────────── */
static void dspic_send(const String &cmd) {
    DSPIC.println(cmd);
    weblog("> " + cmd);
}

static String dspic_wait(unsigned long ms) {
    unsigned long t = millis();
    while (millis() - t < ms) {
        if (DSPIC.available()) {
            String r = DSPIC.readStringUntil('\n');
            r.trim();
            if (r.length()) { weblog("< " + r); return r; }
        }
    }
    return "";
}

/* ── Web UI ─────────────────────────────────────────────────────── */
static const char WEB_UI[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Binaural Goggle</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0d1117;color:#c9d1d9;font-family:'Courier New',monospace;
padding:20px;max-width:480px;margin:0 auto}
h1{color:#7ab87a;font-size:14px;letter-spacing:3px;text-transform:uppercase}
.sub{color:#3d5a3d;font-size:10px;margin-bottom:20px}
.card{background:#161b22;border:1px solid #21262d;border-radius:4px;
padding:16px;margin-bottom:12px}
.card h2{font-size:10px;color:#7ab87a;letter-spacing:2px;
text-transform:uppercase;margin-bottom:12px}
label{font-size:11px;color:#8b949e;display:flex;
justify-content:space-between;margin-bottom:6px}
input[type=range]{width:100%;height:4px;margin-bottom:14px;accent-color:#7ab87a}
.row{display:flex;gap:8px}
input[type=color]{width:48px;height:36px;border:none;background:none;cursor:pointer}
.btn{background:#1e3a1e;border:1px solid #3d7a3d;color:#7ab87a;padding:10px;
font-family:inherit;font-size:10px;letter-spacing:1px;cursor:pointer;
border-radius:2px;width:100%;text-transform:uppercase}
.btn:hover{background:#2a4a2a}
.btn.sel{background:#3d7a3d;color:#0d1117}
.btn.danger{border-color:#7a3d3d;color:#c07070;background:#2a1e1e}
.status{font-size:10px;color:#3d5a3d;padding:8px;background:#0d1117;
border-radius:2px;margin-top:8px;min-height:32px;word-break:break-all;
white-space:pre-wrap}
</style></head><body>
<h1>Binaural Goggle</h1>
<div class="sub">192.168.4.1 · v2.2 · VIN at boot: <span id="vin">?</span>V</div>

<div class="card"><h2>Beat</h2>
<label>Beat <span id="beat_v">4.0</span> Hz</label>
<input type="range" id="beat" min="0.5" max="40" step="0.5" value="4"
 oninput="beat_v.textContent=parseFloat(this.value).toFixed(1)">
<label>Carrier <span id="car_v">200</span> Hz</label>
<input type="range" id="car" min="50" max="500" step="5" value="200"
 oninput="car_v.textContent=this.value">
<div class="row">
<button class="btn" onclick="pre(2)">2 Delta</button>
<button class="btn" onclick="pre(4)">4 Theta</button>
<button class="btn" onclick="pre(8)">8 Alpha</button>
<button class="btn" onclick="pre(10)">10 Alpha</button></div></div>

<div class="card"><h2>LED Mode</h2>
<div class="row">
<button class="btn sel" id="m_ALT"  onclick="mode('ALT')">Alternate</button>
<button class="btn" id="m_SYNC" onclick="mode('SYNC')">Sync</button>
<button class="btn" id="m_SOLID" onclick="mode('SOLID')">Solid</button></div></div>

<div class="card"><h2>Photic Driving</h2>
<label>Duty <span id="duty_v">100</span> % <span style="color:#4a5">lower = sharper flash</span></label>
<input type="range" id="duty" min="5" max="100" step="5" value="100"
 oninput="duty_v.textContent=this.value">
<div class="row">
<button class="btn" id="seqb" onclick="toggleSeq()">Auto-Seq: ON</button>
<button class="btn" onclick="dream()">Dreamachine</button></div>
<div style="font-size:9px;color:#6a5a3a;margin-top:8px;line-height:1.5">
Dreamachine preset: 10Hz · SYNC · 25% duty · auto-seq off · red.
Sweep 8-20Hz with eyes CLOSED. Stop at once if you feel unwell.</div></div>

<div class="card"><h2>Volume</h2>
<label>Level <span id="vol_v">32</span> / 63</label>
<input type="range" id="vol" min="0" max="63" value="32"
 oninput="vol_v.textContent=this.value"></div>

<div class="card"><h2>Color</h2>
<label>Eye A</label><input type="color" id="ca" value="#ff0000"><br><br>
<label>Eye B</label><input type="color" id="cb" value="#ff0000"><br>
<button class="btn" onclick="cb.value=ca.value">Sync A → B</button></div>

<div class="card">
<button class="btn" onclick="applyAll()">⬆ Apply All</button><br><br>
<div class="row">
<button class="btn" onclick="cmd('STATUS')">Status</button>
<button class="btn" onclick="showLog()">Log</button>
<button class="btn danger" onclick="cmd('RESET')">Reset</button></div>
<div class="status" id="sb">ready</div></div>

<script>
const $=id=>document.getElementById(id);
function hex2rgb(h){return[parseInt(h.slice(1,3),16),parseInt(h.slice(3,5),16),
parseInt(h.slice(5,7),16)]}
function pre(hz){$('beat').value=hz;$('beat_v').textContent=hz.toFixed(1)}
async function cmd(c){try{const r=await fetch('/cmd?c='+encodeURIComponent(c));
$('sb').textContent=await r.text()}catch(e){$('sb').textContent='err '+e}}
async function mode(m){await cmd('MODE:'+m);
['ALT','SYNC','SOLID'].forEach(x=>$('m_'+x).classList.toggle('sel',x==m))}
async function showLog(){const r=await fetch('/log');
$('sb').textContent=await r.text()}
let seqOn=true;
async function toggleSeq(){seqOn=!seqOn;await cmd('SEQ:'+(seqOn?'ON':'OFF'));
$('seqb').textContent='Auto-Seq: '+(seqOn?'ON':'OFF');
$('seqb').classList.toggle('sel',!seqOn)}
async function dream(){
seqOn=false;$('seqb').textContent='Auto-Seq: OFF';$('seqb').classList.add('sel');
await cmd('SEQ:OFF');await mode('SYNC');
$('duty').value=25;$('duty_v').textContent='25';await cmd('DUTY:25');
$('beat').value=10;$('beat_v').textContent='10.0';await cmd('BEAT:10.0');
$('ca').value='#ff0000';$('cb').value='#ff0000';
await cmd('RGB_AB:255,0,0');
$('sb').textContent='Dreamachine armed: 10Hz SYNC 25% red. Eyes closed.'}
async function applyAll(){
await cmd('BEAT:'+parseFloat($('beat').value).toFixed(1));
await cmd('CARRIER:'+$('car').value+'.0');
await cmd('VOL:'+$('vol').value);
await cmd('DUTY:'+$('duty').value);
const a=hex2rgb($('ca').value),b=hex2rgb($('cb').value);
await cmd('RGB_A:'+a.join(','));
await cmd('RGB_B:'+b.join(','));
$('sb').textContent='all settings sent'}
fetch('/vin').then(r=>r.text()).then(t=>$('vin').textContent=t);
setInterval(()=>cmd('STATUS'),10000);
</script></body></html>
)rawliteral";

/* ── handlers ───────────────────────────────────────────────────── */
void handle_root() { server.send(200, "text/html", WEB_UI); }

void handle_cmd() {
    if (!server.hasArg("c")) { server.send(400, "text/plain", "missing"); return; }
    dspic_send(server.arg("c"));
    String r = dspic_wait(250);
    server.send(200, "text/plain", r.length() ? r : "timeout");
}

void handle_log() {
    String out;
    for (uint8_t i = 0; i < LOGN; i++) {
        const String &s = logbuf[(loghead + i) % LOGN];
        if (s.length()) { out += s; out += '\n'; }
    }
    server.send(200, "text/plain", out.length() ? out : "(empty)");
}

void handle_vin() {
    server.send(200, "text/plain", String(g_vin_boot, 2));
}

/* ── setup / loop ───────────────────────────────────────────────── */
void setup() {
    /* VIN sample BEFORE WiFi (GPIO0 is ADC2 — dead once WiFi runs) */
    analogReadResolution(12);
    uint32_t acc = 0;
    for (int i = 0; i < 8; i++) { acc += analogRead(VIN_ADC_PIN); delay(2); }
    g_vin_boot = (acc / 8) * VIN_SCALE;

    /* dsPIC UART — shared with H3 on this PCB */
#if DSPIC_ON_UART0
    DSPIC.begin(DSPIC_BAUD);                 /* UART0: GPIO1/GPIO3   */
#else
    DSPIC.begin(DSPIC_BAUD, SERIAL_8N1, 16, 17);
#endif
    DSPIC.setTimeout(50);

#ifdef STA_SSID
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(STA_SSID, STA_PASS);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(250);
#endif
    WiFi.softAP(AP_SSID, AP_PASS);
    WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1),
                      IPAddress(255,255,255,0));

    server.on("/",    handle_root);
    server.on("/cmd", handle_cmd);
    server.on("/log", handle_log);
    server.on("/vin", handle_vin);
    server.onNotFound([](){ server.send(404, "text/plain", "nope"); });
    server.begin();

    ArduinoOTA.setHostname("binaural-goggle");
    ArduinoOTA.setPassword("goggle-ota");
    ArduinoOTA.begin();

    weblog("boot: VIN=" + String(g_vin_boot, 2) + "V");

    /* listen for the dsPIC's READY banner (it may already be up) */
    String r = dspic_wait(2000);
    if (r.startsWith("READY:")) weblog("dsPIC online: " + r);
    else                        weblog("dsPIC silent at boot (ok if it spoke first)");
}

void loop() {
    server.handleClient();
    ArduinoOTA.handle();

    /* drain unsolicited dsPIC lines into the web log — NEVER echo
       anything back onto the UART here (loop hazard) */
    while (DSPIC.available()) {
        String line = DSPIC.readStringUntil('\n');
        line.trim();
        if (line.length()) weblog("< " + line);
    }
}
