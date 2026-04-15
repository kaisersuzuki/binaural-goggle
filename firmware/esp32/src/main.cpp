/*
 * Binaural Goggle Controller — ESP32 Firmware
 *
 * STATUS: Smoke test foundation. WiFi AP + minimal web UI verified working
 *         on ESP32 DevKitC. UART bridge to dsPIC stubbed (Serial logs only).
 *
 * NEXT: Replace Serial with Serial2 on GPIO 16/17 once final PCB arrives,
 *       add OTA updates, expand UI to cover full command set, implement
 *       STA mode fallback and SPIFFS-served full UI.
 *
 * Connect to "BinauralGoggle" WiFi (password: meditate123)
 * Browse to http://192.168.4.1
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

const char* AP_SSID = "BinauralGoggle";
const char* AP_PASS = "meditate123";

WebServer server(80);

// State (will eventually sync with dsPIC over UART)
int   carrier = 128;
float beat    = 4.0;
int   volume  = 32;

const char* INDEX_HTML = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Binaural Goggle</title>
  <style>
    body { font-family: monospace; background: #0d0f0e; color: #7ab87a;
           padding: 20px; max-width: 500px; margin: auto; }
    h1 { color: #c878c8; border-bottom: 1px solid #2a4a2a; padding-bottom: 8px; }
    label { display: block; margin-top: 16px; color: #6ab0c0; }
    input[type=range] { width: 100%; }
    .val { color: #c0a060; font-weight: bold; }
    button { background: #1a3a1a; color: #7ab87a; border: 1px solid #3a6a3a;
             padding: 10px 20px; margin-top: 20px; font-family: monospace;
             cursor: pointer; }
    button:hover { background: #2a4a2a; }
  </style>
</head>
<body>
  <h1>BINAURAL GOGGLE CTRL</h1>
  <label>Carrier: <span class="val" id="cv">128</span> Hz</label>
  <input type="range" id="c" min="32" max="512" step="32" value="128"
         oninput="cv.textContent=this.value">
  <label>Beat: <span class="val" id="bv">4.0</span> Hz</label>
  <input type="range" id="b" min="5" max="120" step="1" value="40"
         oninput="bv.textContent=(this.value/10).toFixed(1)">
  <label>Volume: <span class="val" id="vv">32</span>/63</label>
  <input type="range" id="v" min="0" max="63" step="1" value="32"
         oninput="vv.textContent=this.value">
  <button onclick="send()">APPLY</button>
  <pre id="log" style="color:#556655;margin-top:20px;"></pre>
  <script>
    function send() {
      const params = `carrier=${c.value}&beat=${b.value/10}&vol=${v.value}`;
      fetch('/set?' + params).then(r => r.text()).then(t => {
        log.textContent = t + '\n' + log.textContent;
      });
    }
  </script>
</body>
</html>
)HTML";

void handleRoot() { server.send(200, "text/html", INDEX_HTML); }

void handleSet() {
  if (server.hasArg("carrier")) carrier = server.arg("carrier").toInt();
  if (server.hasArg("beat"))    beat    = server.arg("beat").toFloat();
  if (server.hasArg("vol"))     volume  = server.arg("vol").toInt();

  // TODO: replace Serial with Serial2 (UART2 on GPIO 16/17) when on real board
  String cmd = "CARRIER:" + String(carrier) +
               " BEAT:"   + String(beat, 1) +
               " VOL:"    + String(volume);
  Serial.println("[UART stub] " + cmd);
  server.send(200, "text/plain", "OK " + cmd);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BinauralGoggle] starting AP...");

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.begin();
  Serial.println("web server up");
}

void loop() { server.handleClient(); }
