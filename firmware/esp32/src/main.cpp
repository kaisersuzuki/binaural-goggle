/*
 * main.cpp - ESP32-S3 firmware v3.0, Binaural Goggle WiFi controller
 * Board: AUDIO-CONTROLLER rev 1.1, U3 = ESP32-S3-WROOM-1-N8R8
 *
 * Written against the netlist of the board as manufactured; see
 * docs/hardware_review_rev1.1.md for the findings behind it.
 *
 * -- The UART link is crossed on the PCB ------------------------------
 *   net TX0: module pad TXD0 (GPIO43) -- dsPIC pin 25 (U1TX) -- H3 pin 5
 *   net RX0: module pad RXD0 (GPIO44) -- dsPIC pin 26 (U1RX) -- H3 pin 6
 * Transmit is wired to transmit and receive to receive. The ESP32-S3 can
 * route any UART signal to any pin, so this firmware listens on GPIO43
 * and talks on GPIO44, using UART1 so that nothing the Arduino core
 * prints on UART0 can reach the dsPIC.
 *
 * The ESP32 boot ROM still prints on GPIO43 at every reset. The dsPIC
 * firmware (v3.0+) keeps its transmit pin high-impedance unless it is
 * answering a command, so the two never drive the net at the same time.
 *
 * -- GPIO0 --------------------------------------------------------------
 * GPIO0 is wired to the output of U5 (MAX8212), which pulls it LOW
 * whenever VIN is above about 4.6 V. With U5 fitted the module starts in
 * the ROM download mode on every power-up and this firmware never runs.
 * U5 must be removed (or its pin 4 lifted). GPIO0 is not an ADC pin on
 * the ESP32-S3 and nothing else senses VIN, so there is no VIN read-out.
 *
 * -- Flashing -----------------------------------------------------------
 * First flash: USB-UART adapter on H3 (GND, EN, IO0, adapter-RX -> pin 5
 * TX0, adapter-TX -> pin 6 RX0). See docs/programming.md.
 * Afterwards: http://192.168.4.1/update or `pio run -e goggle_ota -t upload`.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include "driver/gpio.h"

#define FW_VERSION     "3.0"

#define DSPIC_RX_PIN   43        /* module pad "TXD0": dsPIC transmits here */
#define DSPIC_TX_PIN   44        /* module pad "RXD0": dsPIC listens here   */
#define DSPIC_BAUD     9600
#define REPLY_TIMEOUT  400       /* ms; STATUS is about 75 ms at 9600 baud  */
#define CMD_MAX        38        /* dsPIC line buffer is 40                 */

#define AP_SSID   "BinauralGoggle"
#define AP_PASS   "meditate123"  /* WPA2 needs at least 8 characters        */
#define OTA_USER  "admin"
#define OTA_PASS  "goggle-ota"
/* Optional: also join a home network. The AP stays up either way.
#define STA_SSID "home_ssid"
#define STA_PASS "home_pass"
*/

static HardwareSerial   dspic(1);
static WebServer        server(80);
static HTTPUpdateServer updater;

static unsigned long lastReplyMs = 0;
static bool          everReplied = false;

/* -- debug ring buffer, served at /log -------------------------------- */
#define LOGN 32
static String  logbuf[LOGN];
static uint8_t loghead = 0;

static void weblog(const String &s) {
    logbuf[loghead] = String(millis() / 1000) + "s " + s;
    loghead = (loghead + 1) % LOGN;
}

/* -- dsPIC link ---------------------------------------------------------- */

/* Send one command and return the reply line, or "" on timeout. */
static String dspicCommand(const String &cmd) {
    while (dspic.available()) dspic.read();     /* drop anything stale     */

    /* The leading newline ends any half-line the dsPIC may be holding
       (for example noise received while this chip was resetting). */
    dspic.print('\n');
    dspic.print(cmd);
    dspic.print('\n');
    dspic.flush();
    weblog("> " + cmd);

    String line;
    unsigned long t0 = millis();
    while (millis() - t0 < REPLY_TIMEOUT) {
        while (dspic.available()) {
            char c = (char)dspic.read();
            if (c == '\n' || c == '\r') {
                if (line.length()) {
                    lastReplyMs = millis();
                    everReplied = true;
                    weblog("< " + line);
                    return line;
                }
            } else if (c >= 0x20 && c <= 0x7E && line.length() < 120) {
                line += c;
            }
        }
        delay(1);
    }
    weblog("< (no reply)");
    return "";
}

/* Only printable ASCII, bounded length: nothing else goes on the wire. */
static bool cleanCommand(String &c) {
    c.trim();
    c.toUpperCase();
    if (c.length() == 0 || c.length() > CMD_MAX) return false;
    for (size_t i = 0; i < c.length(); i++) {
        char ch = c[i];
        if (ch < 0x20 || ch > 0x7E) return false;
    }
    return true;
}

/* -- Web UI ---------------------------------------------------------------- */
static const char WEB_UI[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Binaural Goggle</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0d1117;color:#c9d1d9;font-family:'Courier New',monospace;
padding:20px;max-width:480px;margin:0 auto}
h1{color:#7ab87a;font-size:14px;letter-spacing:3px;text-transform:uppercase}
.sub{color:#5d8a5d;font-size:10px;margin-bottom:20px}
.sub.off{color:#c07070}
.card{background:#161b22;border:1px solid #21262d;border-radius:4px;
padding:16px;margin-bottom:12px}
.card h2{font-size:10px;color:#7ab87a;letter-spacing:2px;
text-transform:uppercase;margin-bottom:12px}
label{font-size:11px;color:#8b949e;display:flex;
justify-content:space-between;margin-bottom:6px}
input[type=range]{width:100%;height:4px;margin-bottom:14px;accent-color:#7ab87a}
.row{display:flex;gap:8px}
.row+.row{margin-top:8px}
input[type=color]{width:100%;height:36px;border:none;background:none;cursor:pointer}
.btn{background:#1e3a1e;border:1px solid #3d7a3d;color:#7ab87a;padding:10px 4px;
font-family:inherit;font-size:10px;letter-spacing:1px;cursor:pointer;
border-radius:2px;width:100%;text-transform:uppercase}
.btn:hover{background:#2a4a2a}
.btn.sel{background:#3d7a3d;color:#0d1117}
.btn.danger{border-color:#7a3d3d;color:#c07070;background:#2a1e1e}
.note{font-size:9px;color:#9a8a5a;margin-top:10px;line-height:1.5}
.status{font-size:10px;color:#5d8a5d;padding:8px;background:#0d1117;
border-radius:2px;margin-top:8px;min-height:32px;word-break:break-all;
white-space:pre-wrap}
</style></head><body>
<h1>Binaural Goggle</h1>
<div class="sub off" id="link">connecting…</div>

<div class="card"><h2>Beat</h2>
<label>Beat <span><span id="beat_v">4.0</span> Hz</span></label>
<input type="range" id="beat" min="0.5" max="40" step="0.5" value="4">
<label>Carrier <span><span id="car_v">200</span> Hz</span></label>
<input type="range" id="car" min="50" max="500" step="5" value="200">
<div class="row">
<button class="btn" onclick="preset(2)">2 Delta</button>
<button class="btn" onclick="preset(4)">4 Theta</button>
<button class="btn" onclick="preset(8)">8 Alpha</button>
<button class="btn" onclick="preset(10)">10 Alpha</button></div>
<div class="row">
<button class="btn" id="seqb" onclick="toggleSeq()">Auto-Seq: ON</button></div>
<div class="note">Auto-Seq: every 8 min the beat doubles for 40 s, every
13 min it halves for 90 s. Turn it off to hold one rate.</div></div>

<div class="card"><h2>Volume</h2>
<label>Level <span><span id="vol_v">30</span> %</span></label>
<input type="range" id="vol" min="0" max="100" step="1" value="30">
<div class="row">
<button class="btn sel" id="ampb" onclick="toggleAmp()">Amp: ON</button></div></div>

<div class="card"><h2>Lights</h2>
<div class="row">
<button class="btn sel" id="m_ALT" onclick="mode('ALT')">Alternate</button>
<button class="btn" id="m_SYNC" onclick="mode('SYNC')">Sync</button>
<button class="btn" id="m_SOLID" onclick="mode('SOLID')">Solid</button></div>
<div class="row" style="margin-bottom:12px">
<button class="btn" onclick="dream()">Dreamachine</button>
<button class="btn" onclick="lightsOff()">Lights off</button></div>
<label>Flash duty <span><span id="duty_v">100</span> %</span></label>
<input type="range" id="duty" min="5" max="100" step="5" value="100">
<label>Eye A / Eye B</label>
<div class="row"><input type="color" id="ca" value="#ff0000">
<input type="color" id="cb" value="#ff0000"></div>
<div class="row"><button class="btn" onclick="copyAB()">Copy A → B</button></div>
<div class="note">Flashing light can trigger seizures in people with
photosensitive epilepsy, most strongly between 15 and 25 Hz. Do not use
the lights if that could apply to you. Keep your eyes closed, and stop
at once if you feel unwell. Dreamachine: 10 Hz, both eyes, 25 % duty, red.</div></div>

<div class="card">
<div class="row">
<button class="btn" onclick="refresh(true)">Status</button>
<button class="btn" onclick="showLog()">Log</button>
<button class="btn" onclick="location.href='/update'">Update</button>
<button class="btn danger" onclick="resetAll()">Reset</button></div>
<div class="status" id="sb">ready</div></div>

<script>
const $=id=>document.getElementById(id);
let chain=Promise.resolve(),busy=0,seqOn=true,ampOn=true;
function send(c){busy++;const p=chain.then(()=>fetch('/cmd?c='+encodeURIComponent(c))
.then(r=>r.text()).catch(e=>'ERR:HTTP '+e));chain=p.then(()=>{busy--});return p}
async function cmd(c){const r=await send(c);$('sb').textContent=c+' → '+r;return r}
function hex2rgb(h){return[1,3,5].map(i=>parseInt(h.slice(i,i+2),16))}
function rgb2hex(a){return'#'+a.map(v=>(+v).toString(16).padStart(2,'0')).join('')}
function setSeq(on){seqOn=on;$('seqb').textContent='Auto-Seq: '+(on?'ON':'OFF');
$('seqb').classList.toggle('sel',on)}
function setAmp(on){ampOn=on;$('ampb').textContent='Amp: '+(on?'ON':'OFF');
$('ampb').classList.toggle('sel',on)}
function setMode(m){['ALT','SYNC','SOLID'].forEach(x=>$('m_'+x).classList.toggle('sel',x==m))}
function setSlider(id,v){$(id).value=v;showVal(id)}
function showVal(id){const v=parseFloat($(id).value);
$(id+'_v').textContent=id=='beat'?v.toFixed(1):v}
const sliderCmd={beat:v=>'BEAT:'+parseFloat(v).toFixed(1),car:v=>'CARRIER:'+v+'.0',
vol:v=>'VOL:'+v,duty:v=>'DUTY:'+v};
for(const id in sliderCmd){$(id).addEventListener('input',()=>showVal(id));
$(id).addEventListener('change',()=>cmd(sliderCmd[id]($(id).value)))}
$('ca').addEventListener('change',()=>cmd('RGB_A:'+hex2rgb($('ca').value).join(',')));
$('cb').addEventListener('change',()=>cmd('RGB_B:'+hex2rgb($('cb').value).join(',')));
function preset(hz){setSlider('beat',hz);cmd('BEAT:'+hz.toFixed(1))}
function mode(m){setMode(m);cmd('MODE:'+m)}
function toggleSeq(){setSeq(!seqOn);cmd('SEQ:'+(seqOn?'ON':'OFF'))}
function toggleAmp(){setAmp(!ampOn);cmd('AMP:'+(ampOn?'ON':'OFF'))}
function copyAB(){$('cb').value=$('ca').value;
cmd('RGB_AB:'+hex2rgb($('ca').value).join(','))}
function lightsOff(){$('ca').value=$('cb').value='#000000';cmd('RGB_AB:0,0,0')}
async function dream(){setSeq(false);setMode('SYNC');setSlider('duty',25);
setSlider('beat',10);$('ca').value=$('cb').value='#ff0000';
await cmd('SEQ:OFF');await cmd('MODE:SYNC');await cmd('DUTY:25');
await cmd('BEAT:10.0');await cmd('RGB_AB:255,0,0');
$('sb').textContent='Dreamachine: 10 Hz, both eyes, 25 % duty, red. Eyes closed.'}
async function resetAll(){await cmd('RESET');refresh(true)}
async function showLog(){const r=await fetch('/log');$('sb').textContent=await r.text()}
async function refresh(show){if(busy)return;const s=await send('STATUS');
const ok=s.startsWith('BEAT:');
$('link').textContent=ok?'192.168.4.1 · dsPIC online · firmware '+(/FW:(\S+)/.exec(s)||[])[1]
:'192.168.4.1 · dsPIC NOT RESPONDING';
$('link').classList.toggle('off',!ok);
if(show)$('sb').textContent=ok?s:'no reply from dsPIC';
if(!ok||busy)return;
const f={};s.split(' ').forEach(p=>{const i=p.indexOf(':');f[p.slice(0,i)]=p.slice(i+1)});
if(document.activeElement.type!='range'){setSlider('vol',f.VOL);setSlider('duty',f.DUTY);
if(f.SEQ=='OFF')setSlider('beat',f.BEAT);setSlider('car',Math.round(f.CAR))}
setMode(f.MODE);setSeq(f.SEQ=='ON');setAmp(f.AMP=='ON')}
refresh(true);setInterval(refresh,5000);
</script></body></html>
)rawliteral";

/* -- HTTP handlers ------------------------------------------------------- */
static void handleRoot() {
    server.send_P(200, "text/html", WEB_UI);
}

static void handleCmd() {
    String c = server.arg("c");
    if (!cleanCommand(c)) {
        server.send(400, "text/plain", "ERR:BADCMD");
        return;
    }
    String r = dspicCommand(c);
    server.send(200, "text/plain", r.length() ? r : "NO REPLY");
}

static void handleLog() {
    String out = "esp32 fw " FW_VERSION ", dsPIC ";
    out += everReplied
        ? "last answered " + String((millis() - lastReplyMs) / 1000) + " s ago\n"
        : "has never answered\n";
    for (uint8_t i = 0; i < LOGN; i++) {
        const String &s = logbuf[(loghead + i) % LOGN];
        if (s.length()) { out += s; out += '\n'; }
    }
    server.send(200, "text/plain", out);
}

/* -- setup / loop ---------------------------------------------------------- */
void setup() {
    /* UART1 on the swapped pins. This also turns GPIO43 into an input,
       which disconnects the UART0 transmitter the boot ROM left on it. */
    dspic.begin(DSPIC_BAUD, SERIAL_8N1, DSPIC_RX_PIN, DSPIC_TX_PIN);
    gpio_pullup_en((gpio_num_t)DSPIC_RX_PIN);   /* idle level while the
                                                   dsPIC is not driving */

    WiFi.persistent(false);
#ifdef STA_SSID
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(STA_SSID, STA_PASS);
#else
    WiFi.mode(WIFI_AP);
#endif
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASS);

    server.on("/", handleRoot);
    server.on("/cmd", handleCmd);
    server.on("/log", handleLog);
    server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
    updater.setup(&server, "/update", OTA_USER, OTA_PASS);
    server.begin();

    ArduinoOTA.setHostname("binaural-goggle");
    ArduinoOTA.setPassword(OTA_PASS);
    ArduinoOTA.begin();

    weblog("boot, esp32 fw " FW_VERSION);

    /* The dsPIC needs about 0.4 s after power-up before it listens. */
    for (int i = 0; i < 5; i++) {
        if (dspicCommand("STATUS").startsWith("BEAT:")) break;
        delay(300);
    }
    if (!everReplied) weblog("dsPIC did not answer at boot");
}

void loop() {
    server.handleClient();
    ArduinoOTA.handle();
    delay(2);
}
