/*
 * main.cpp — ESP32 Firmware for Binaural Beat Goggle Controller
 * Binaural Goggle Project — github.com/kaisersuzuki/binaural-goggle
 *
 * Hardware: ESP32-WROOM-32E (SMD, JLCPCB assembled)
 * PCB: Darlington PCB Design rev 1.1
 *
 * PIN ASSIGNMENTS:
 *   GPIO16 (RX2) — Serial2 RX from dsPIC U2TX (RF5)
 *   GPIO17 (TX2) — Serial2 TX to   dsPIC U2RX (RF4)
 *   GPIO0        — Boot mode / used on PCB for voltage divider (do not use as output)
 *
 * FEATURES:
 *   - WiFi AP mode: SSID "BinauralGoggle" / "meditate123"
 *   - Web UI at 192.168.4.1 (sliders: beat freq, carrier freq, volume, RGB)
 *   - OTA firmware update via ArduinoOTA
 *   - Serial2 command protocol to dsPIC (9600 baud)
 *   - STA fallback: if STA_SSID defined, tries STA first, falls back to AP
 *
 * COMMAND PROTOCOL TO dsPIC:
 *   BEAT:<hz>           set beat frequency
 *   CARRIER:<hz>        set carrier frequency
 *   VOL:<0-63>          set volume step
 *   RGB_A:<r>,<g>,<b>   set eye A color
 *   RGB_B:<r>,<g>,<b>   set eye B color
 *   RGB_AB:<r>,<g>,<b>  set both eyes
 *   STATUS              request status string
 *   RESET               reset sequencer
 *
 * BUILD: VS Code + PlatformIO, ESP32 Arduino framework
 * FLASH: Via J2 header (GND/3V3/EN/IO0/TX0/RX0) with FTDI/CP2102 adapter
 *        OR via OTA after first flash
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>

/* ── WiFi credentials ────────────────────────────────────────────────── */
#define AP_SSID     "BinauralGoggle"
#define AP_PASS     "meditate123"
#define AP_IP       "192.168.4.1"

/* Uncomment to enable STA fallback (connect to home network first) */
// #define STA_SSID "your_home_ssid"
// #define STA_PASS "your_home_password"

/* ── Serial2 to dsPIC ────────────────────────────────────────────────── */
#define DSPIC_RX_PIN  16    /* GPIO16 = Serial2 RX (from dsPIC TX RF5) */
#define DSPIC_TX_PIN  17    /* GPIO17 = Serial2 TX (to   dsPIC RX RF4) */
#define DSPIC_BAUD    9600

/* ── State ───────────────────────────────────────────────────────────── */
static float  g_beat_hz    = 4.0f;
static float  g_carrier_hz = 200.0f;
static uint8_t g_volume    = 32;
static uint8_t g_rgb_a[3]  = {255, 0, 0};  /* Eye A: red */
static uint8_t g_rgb_b[3]  = {255, 0, 0};  /* Eye B: red */
static String  g_dspic_status = "unknown";
static bool    g_dspic_ready  = false;

WebServer server(80);

/* ─────────────────────────────────────────────────────────────────────
   Send command to dsPIC via Serial2
   ───────────────────────────────────────────────────────────────────── */
static void dspic_send(const String &cmd) {
    Serial2.println(cmd);
    Serial.println("[->dsPIC] " + cmd);
}

/* ─────────────────────────────────────────────────────────────────────
   Web UI HTML
   Single-page app with sliders and color pickers
   ───────────────────────────────────────────────────────────────────── */
static const char WEB_UI[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Binaural Goggle</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: #0d1117;
    color: #c9d1d9;
    font-family: 'Courier New', monospace;
    padding: 20px;
    max-width: 480px;
    margin: 0 auto;
  }
  h1 { color: #7ab87a; font-size: 14px; letter-spacing: 3px;
       text-transform: uppercase; margin-bottom: 4px; }
  .subtitle { color: #3d5a3d; font-size: 10px; margin-bottom: 24px; }
  .card {
    background: #161b22;
    border: 1px solid #21262d;
    border-radius: 4px;
    padding: 16px;
    margin-bottom: 12px;
  }
  .card h2 { font-size: 10px; color: #7ab87a; letter-spacing: 2px;
             text-transform: uppercase; margin-bottom: 12px; }
  label { font-size: 11px; color: #8b949e; display: flex;
          justify-content: space-between; margin-bottom: 6px; }
  input[type=range] {
    width: 100%; height: 4px; margin-bottom: 14px;
    accent-color: #7ab87a;
  }
  .color-row { display: flex; gap: 12px; align-items: center; }
  input[type=color] { width: 48px; height: 36px; border: none;
                      background: none; cursor: pointer; border-radius: 4px; }
  .btn {
    background: #1e3a1e;
    border: 1px solid #3d7a3d;
    color: #7ab87a;
    padding: 10px 20px;
    font-family: 'Courier New', monospace;
    font-size: 11px;
    letter-spacing: 1px;
    cursor: pointer;
    border-radius: 2px;
    width: 100%;
    text-transform: uppercase;
  }
  .btn:hover { background: #2a4a2a; }
  .btn.danger { border-color: #7a3d3d; color: #c07070; background: #2a1e1e; }
  .status { font-size: 10px; color: #3d5a3d; padding: 8px;
            background: #0d1117; border-radius: 2px; margin-top: 8px;
            min-height: 32px; word-break: break-all; }
  .preset-row { display: flex; gap: 8px; }
  .preset-row .btn { font-size: 9px; padding: 8px; }
</style>
</head>
<body>
<h1>Binaural Goggle</h1>
<div class="subtitle">192.168.4.1 &nbsp;·&nbsp; dsPIC30F4013 + ESP32</div>

<div class="card">
  <h2>Beat Frequency</h2>
  <label>Beat <span id="beat_val">4.0</span> Hz</label>
  <input type="range" id="beat" min="0.5" max="40" step="0.5" value="4.0"
    oninput="document.getElementById('beat_val').textContent=parseFloat(this.value).toFixed(1)">

  <label>Carrier <span id="carrier_val">200</span> Hz</label>
  <input type="range" id="carrier" min="50" max="500" step="5" value="200"
    oninput="document.getElementById('carrier_val').textContent=this.value">

  <div class="preset-row">
    <button class="btn" onclick="setPreset(2,'Delta (2Hz)')">2Hz Delta</button>
    <button class="btn" onclick="setPreset(4,'Theta (4Hz)')">4Hz Theta</button>
    <button class="btn" onclick="setPreset(8,'Alpha (8Hz)')">8Hz Alpha</button>
    <button class="btn" onclick="setPreset(10,'Alpha (10Hz)')">10Hz Alpha</button>
  </div>
</div>

<div class="card">
  <h2>Volume</h2>
  <label>Level <span id="vol_val">32</span> / 63</label>
  <input type="range" id="volume" min="0" max="63" step="1" value="32"
    oninput="document.getElementById('vol_val').textContent=this.value">
</div>

<div class="card">
  <h2>LED Color</h2>
  <label>Eye A</label>
  <div class="color-row">
    <input type="color" id="color_a" value="#ff0000" oninput="previewColor('a',this.value)">
    <span style="font-size:10px;color:#556655">R:<span id="a_r">255</span>
    G:<span id="a_g">0</span> B:<span id="a_b">0</span></span>
  </div>
  <br>
  <label>Eye B</label>
  <div class="color-row">
    <input type="color" id="color_b" value="#ff0000" oninput="previewColor('b',this.value)">
    <span style="font-size:10px;color:#556655">R:<span id="b_r">255</span>
    G:<span id="b_g">0</span> B:<span id="b_b">0</span></span>
  </div>
  <br>
  <button class="btn" onclick="syncColors()">Sync A = B</button>
</div>

<div class="card">
  <button class="btn" onclick="sendAll()">⬆ Apply All Settings</button>
  <br><br>
  <button class="btn" onclick="sendCmd('STATUS')">Request Status</button>
  <br><br>
  <button class="btn danger" onclick="sendCmd('RESET')">Reset Sequencer</button>
  <div class="status" id="status_box">waiting...</div>
</div>

<script>
function hexToRgb(hex) {
  const r = parseInt(hex.slice(1,3),16);
  const g = parseInt(hex.slice(3,5),16);
  const b = parseInt(hex.slice(5,7),16);
  return [r,g,b];
}

function previewColor(eye, hex) {
  const [r,g,b] = hexToRgb(hex);
  document.getElementById(eye+'_r').textContent = r;
  document.getElementById(eye+'_g').textContent = g;
  document.getElementById(eye+'_b').textContent = b;
}

function syncColors() {
  const hex = document.getElementById('color_a').value;
  document.getElementById('color_b').value = hex;
  previewColor('b', hex);
}

function setPreset(hz, label) {
  document.getElementById('beat').value = hz;
  document.getElementById('beat_val').textContent = hz.toFixed(1);
  setStatus('Preset: ' + label);
}

function setStatus(msg) {
  document.getElementById('status_box').textContent = msg;
}

async function sendCmd(cmd) {
  try {
    const resp = await fetch('/cmd?c=' + encodeURIComponent(cmd));
    const text = await resp.text();
    setStatus(text);
  } catch(e) { setStatus('error: ' + e); }
}

async function sendAll() {
  const beat    = parseFloat(document.getElementById('beat').value).toFixed(1);
  const carrier = document.getElementById('carrier').value;
  const vol     = document.getElementById('volume').value;
  const [ar,ag,ab] = hexToRgb(document.getElementById('color_a').value);
  const [br,bg,bb] = hexToRgb(document.getElementById('color_b').value);

  await sendCmd('BEAT:' + beat);
  await sendCmd('CARRIER:' + carrier + '.0');
  await sendCmd('VOL:' + vol);
  await sendCmd('RGB_A:' + ar + ',' + ag + ',' + ab);
  await sendCmd('RGB_B:' + br + ',' + bg + ',' + bb);
  setStatus('All settings sent.');
}

// Poll status every 5s
setInterval(() => sendCmd('STATUS'), 5000);
</script>
</body>
</html>
)rawliteral";

/* ─────────────────────────────────────────────────────────────────────
   Web server handlers
   ───────────────────────────────────────────────────────────────────── */
void handle_root() {
    server.send(200, "text/html", WEB_UI);
}

void handle_cmd() {
    if (!server.hasArg("c")) {
        server.send(400, "text/plain", "missing param");
        return;
    }
    String cmd = server.arg("c");
    dspic_send(cmd);

    /* Wait up to 200ms for dsPIC response */
    unsigned long t = millis();
    String resp = "";
    while (millis() - t < 200) {
        if (Serial2.available()) {
            resp = Serial2.readStringUntil('\n');
            resp.trim();
            break;
        }
    }
    if (resp.length() == 0) resp = "timeout";
    server.send(200, "text/plain", resp);
}

void handle_status() {
    dspic_send("STATUS");
    unsigned long t = millis();
    String resp = "";
    while (millis() - t < 300) {
        if (Serial2.available()) {
            resp = Serial2.readStringUntil('\n');
            resp.trim();
            break;
        }
    }
    server.send(200, "text/plain", resp.length() ? resp : "no response");
}

void handle_not_found() {
    server.send(404, "text/plain", "not found");
}

/* ─────────────────────────────────────────────────────────────────────
   OTA setup
   ───────────────────────────────────────────────────────────────────── */
void setup_ota() {
    ArduinoOTA.setHostname("binaural-goggle");
    ArduinoOTA.setPassword("goggle-ota");
    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("[OTA] End");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]\n", error);
    });
    ArduinoOTA.begin();
}

/* ─────────────────────────────────────────────────────────────────────
   setup
   ───────────────────────────────────────────────────────────────────── */
void setup() {
    Serial.begin(115200);
    Serial.println("\n[BOOT] Binaural Goggle ESP32 v2.0");

    /* Serial2 to dsPIC */
    Serial2.begin(DSPIC_BAUD, SERIAL_8N1, DSPIC_RX_PIN, DSPIC_TX_PIN);
    Serial.println("[UART2] dsPIC serial on GPIO16/17 @ 9600 baud");

    /* WiFi — try STA first if configured, fall back to AP */
#ifdef STA_SSID
    Serial.printf("[WiFi] Trying STA: %s\n", STA_SSID);
    WiFi.begin(STA_SSID, STA_PASS);
    unsigned long sta_start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - sta_start < 8000) {
        delay(250);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] STA connected: %s\n", WiFi.localIP().toString().c_str());
        goto wifi_done;
    }
    Serial.println("\n[WiFi] STA failed, starting AP");
#endif

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    WiFi.softAPConfig(
        IPAddress(192,168,4,1),
        IPAddress(192,168,4,1),
        IPAddress(255,255,255,0)
    );
    Serial.printf("[WiFi] AP started: %s  IP: %s\n", AP_SSID, AP_IP);

#ifdef STA_SSID
wifi_done:
#endif

    /* Web server routes */
    server.on("/", handle_root);
    server.on("/cmd", handle_cmd);
    server.on("/status", handle_status);
    server.onNotFound(handle_not_found);
    server.begin();
    Serial.println("[HTTP] Server started at 192.168.4.1");

    /* OTA */
    setup_ota();
    Serial.println("[OTA] Ready (hostname: binaural-goggle, pass: goggle-ota)");

    /* Wait for dsPIC READY message */
    Serial.println("[UART2] Waiting for dsPIC...");
    unsigned long boot_wait = millis();
    while (millis() - boot_wait < 3000) {
        if (Serial2.available()) {
            String msg = Serial2.readStringUntil('\n');
            msg.trim();
            Serial.println("[dsPIC] " + msg);
            if (msg.startsWith("READY:")) {
                g_dspic_ready = true;
                break;
            }
        }
    }
    if (!g_dspic_ready) {
        Serial.println("[WARN] dsPIC not responding — continuing anyway");
    }
}

/* ─────────────────────────────────────────────────────────────────────
   loop
   ───────────────────────────────────────────────────────────────────── */
void loop() {
    server.handleClient();
    ArduinoOTA.handle();

    /* Forward any unsolicited dsPIC messages to USB serial for debug */
    while (Serial2.available()) {
        String line = Serial2.readStringUntil('\n');
        line.trim();
        if (line.length()) {
            Serial.println("[dsPIC] " + line);
        }
    }
}
