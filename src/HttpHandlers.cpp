#include "HttpHandlers.h"
#include "Config.h"
#include "Logger.h"
#include "BoardPins.h"
#include "RelayControl.h"
#include "MqttManager.h"
#include "SignalKClient.h"
#include <ETH.h>
#include <WiFi.h>
#include <ArduinoJson.h>

WebServer server(80);

extern uint8_t g_mode;
enum NetMode : uint8_t { MODE_NONE, MODE_AP, MODE_WIFI, MODE_ETH };
extern uint8_t readDI_mask();

// ── HTML ──────────────────────────────────────────────────────────────────────
static const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32-S3 Relay Board</title>
  <style>
    :root{--card-w:980px;--gap:12px;--radius:14px;--shadow:0 4px 14px rgba(0,0,0,.08);
          --bg:#f6f7fb;--card:#fff;--text:#111;--muted:#6b7280;--border:#e5e7eb;
          --brand:#2563eb;--brand-weak:#dbeafe;--ok:#10b981;--ok-weak:#d1fae5;}
    *{box-sizing:border-box;}
    body{margin:0;padding:24px;font-family:system-ui,-apple-system,sans-serif;
         color:var(--text);background:var(--bg);display:flex;justify-content:center;}
    .container{width:100%;max-width:var(--card-w);}
    .card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);
          box-shadow:var(--shadow);padding:16px 16px 18px;margin-bottom:16px;}
    h1{font-size:1.4rem;margin:0 0 6px 0;}
    .sub{font-size:.95rem;color:var(--muted);display:flex;gap:10px;flex-wrap:wrap;}
    .label{font-weight:600;color:#374151;}
    .section-title{font-weight:700;font-size:1.02rem;margin:2px 0 12px 0;color:#374151;
                   border-bottom:1px solid var(--border);padding-bottom:6px;}
    .subsection{font-weight:600;font-size:.9rem;color:var(--muted);
                margin:14px 0 8px 0;text-transform:uppercase;letter-spacing:.05em;}
    .inputs-grid{display:grid;grid-template-columns:repeat(8,1fr);gap:var(--gap);}
    @media(max-width:680px){.inputs-grid{grid-template-columns:repeat(4,1fr);}}
    .di-pill{border:1px solid var(--border);border-radius:999px;padding:8px 10px;
             display:flex;align-items:center;gap:10px;background:#fff;}
    .dot{width:14px;height:14px;border-radius:50%;background:#cbd5e1;border:1px solid #94a3b8;}
    .dot.high{background:var(--ok);border-color:#059669;box-shadow:0 0 0 3px var(--ok-weak);}
    .relay-grid{display:grid;gap:var(--gap);grid-template-columns:repeat(4,minmax(140px,1fr));}
    @media(max-width:680px){.relay-grid{grid-template-columns:repeat(2,1fr);}}
    button.relay{appearance:none;outline:none;border:1px solid var(--border);background:#fff;
                 border-radius:12px;padding:12px 10px;font-weight:600;color:#374151;cursor:pointer;
                 transition:transform .05s,border-color .15s,box-shadow .15s;}
    button.relay:hover{border-color:#c7cad0;}
    button.relay:active{transform:translateY(1px);}
    button.relay.active{border-color:#1f2937;box-shadow:inset 0 0 0 2px var(--brand-weak);
                        background:#f8fafc;color:#111827;}
    .toolbar{display:flex;gap:10px;flex-wrap:wrap;}
    .btn{appearance:none;border:1px solid var(--border);background:#fff;color:#111;
         border-radius:10px;padding:10px 12px;cursor:pointer;transition:background .2s;}
    .btn:hover{background:#f3f4f6;}
    .cfg-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;}
    @media(max-width:600px){.cfg-grid{grid-template-columns:1fr;}}
    .cfg-label{font-size:.85rem;color:var(--muted);margin-bottom:2px;}
    input[type=text],input[type=password],input[type=number]{
      border:1px solid var(--border);border-radius:8px;padding:8px 10px;
      font-size:.9rem;width:100%;margin-bottom:0;}
  </style>
</head>
<body>
<div class="container">

  <!-- Status header -->
  <div class="card">
    <h1>ESP32-S3 8-Relay + RGB</h1>
    <div class="sub">
      <div><span class="label">Interface:</span> <span id="iface">—</span></div>
      <div><span class="label">IP:</span> <span id="ip">—</span></div>
      <div><span class="label">SSID:</span> <span id="ssid"></span></div>
      <div><span class="label">Mask:</span> <span id="mask">0x00</span></div>
      <div><span class="label">MQTT:</span> <span id="mqtt">—</span></div>
      <div><span class="label">SignalK:</span> <span id="sk">—</span></div>
    </div>
  </div>

  <!-- Digital inputs -->
  <div class="card">
    <div class="section-title">Digital Inputs</div>
    <div class="inputs-grid" id="inputs"></div>
  </div>

  <!-- Relays -->
  <div class="card">
    <div class="section-title">Relays</div>
    <div id="buttons" class="relay-grid"></div>
  </div>

  <!-- Configuration -->
  <div class="card">
    <div class="section-title">Configuration</div>

    <div class="subsection">Network</div>
    <div class="cfg-grid">
      <div><div class="cfg-label">WiFi SSID</div><input id="cfg-ssid" type="text"></div>
      <div><div class="cfg-label">WiFi password</div><input id="cfg-wpass" type="password"></div>
      <div><div class="cfg-label">Hostname</div><input id="cfg-host" type="text"></div>
    </div>

    <div class="subsection">MQTT</div>
    <div class="cfg-grid">
      <div><div class="cfg-label">Broker IP</div><input id="cfg-mbroker" type="text"></div>
      <div><div class="cfg-label">Port</div><input id="cfg-mport" type="number"></div>
      <div><div class="cfg-label">Username</div><input id="cfg-muser" type="text"></div>
      <div><div class="cfg-label">Password</div><input id="cfg-mpass" type="password"></div>
      <div><div class="cfg-label">Client ID</div><input id="cfg-mcid" type="text"></div>
      <div><div class="cfg-label">Base topic</div><input id="cfg-mtopic" type="text"></div>
    </div>

    <div class="subsection">SignalK</div>
    <div class="cfg-grid">
      <div><div class="cfg-label">Server IP / hostname</div><input id="cfg-skhost" type="text"></div>
      <div><div class="cfg-label">Port</div><input id="cfg-skport" type="number"></div>
      <div><div class="cfg-label">Username</div><input id="cfg-skuser" type="text"
           placeholder="Required to obtain token"></div>
      <div><div class="cfg-label">Password</div><input id="cfg-skpass" type="password"
           placeholder="Required to obtain token"></div>
      <div><div class="cfg-label">Bearer token</div><input id="cfg-sktoken" type="password"
           placeholder="Auto-filled after login — paste manually if needed"></div>
      <div><div class="cfg-label">Vessel / source name</div><input id="cfg-skvessel" type="text"></div>
    </div>
    <div class="toolbar" style="margin-top:8px;">
      <button class="btn" onclick="skRequestAccess()" id="skAccessBtn">Request access now</button>
      <button class="btn" onclick="skClearDenied()" id="skClearBtn" style="display:none">Clear denied &amp; re-pair</button>
    </div>

    <div class="toolbar" style="margin-top:14px;">
      <button class="btn" onclick="saveCfg()">Save &amp; Reboot</button>
      <button class="btn" onclick="resetCfg()">Reset to defaults</button>
    </div>
  </div>

  <!-- Log -->
  <div class="card">
    <div class="section-title" style="display:flex;align-items:center;justify-content:space-between;">
      <span>Messages</span>
      <button class="btn" onclick="clearLog()">Clear</button>
    </div>
    <div style="padding:10px;border:1px solid var(--border);border-radius:8px;
         background:#f9fafb;font-family:monospace;font-size:13px;height:140px;overflow-y:auto;">
      <span id="msgContent">Ready.</span>
    </div>
  </div>

  <!-- Actions -->
  <div class="card toolbar">
    <button id="rebootBtn" class="btn">Reboot</button>
  </div>

</div>
<script>
var S={mask:0,di:[],count:8,net:{iface:'',ip:'',ssid:''},mqtt_connected:false,sk_connected:false};
var prevDI=[];

function fmt(ms){const s=Math.floor(ms/1000);
  return[Math.floor(s/3600)%24,Math.floor((s%3600)/60),s%60].map(v=>String(v).padStart(2,'0')).join(':');}
function msg(t){const el=document.getElementById('msgContent');
  if(el){el.innerHTML+='<br>'+t;el.scrollTop=el.scrollHeight;}}
function clearLog(){fetch('/api/log/clear',{method:'POST'})
  .then(()=>{const el=document.getElementById('msgContent');if(el)el.innerHTML='';});}
async function fetchLog(){try{const r=await fetch('/api/log');const d=await r.json();
  const el=document.getElementById('msgContent');if(!el)return;
  el.innerHTML=(d||[]).map(o=>`[${fmt(o.t)}] ${o.m}`).join('<br>');el.scrollTop=el.scrollHeight;}catch(e){}}

function drawRelays(){var w=document.getElementById('buttons'),h='';
  for(var i=0;i<S.count;i++){var on=((S.mask>>i)&1)===1;
    h+='<button class="relay '+(on?'active':'')+'" id="r'+i+'" data-s="'+(on?1:0)+'">R'+(i+1)+': '+(on?'ON':'OFF')+'</button>';}
  w.innerHTML=h;
  for(let i=0;i<S.count;i++){let b=document.getElementById('r'+i);
    b.addEventListener('click',function(){
      var cur=b.getAttribute('data-s')==='1',next=cur?0:1;
      b.setAttribute('data-s',String(next));b.classList.toggle('active',!!next);
      b.textContent='R'+(i+1)+': '+(next?'ON':'OFF');
      if(next)S.mask|=(1<<i);else S.mask&=~(1<<i);paintHdr();
      msg('R'+(i+1)+' '+(next?'ON':'OFF'));
      fetch('/api/relay',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:new URLSearchParams({index:String(i),state:String(next)})})
        .catch(e=>{b.setAttribute('data-s',String(cur?1:0));b.classList.toggle('active',!!cur);
          b.textContent='R'+(i+1)+': '+(cur?'ON':'OFF');
          if(cur)S.mask|=(1<<i);else S.mask&=~(1<<i);paintHdr();msg('Error: '+e.message);});});}}

function drawInputs(){var w=document.getElementById('inputs'),h='';
  for(var i=0;i<S.di.length;i++)
    h+='<div class="di-pill"><div class="dot '+(S.di[i]===0?'high':'')+'" id="d'+i+'"></div><div>DI'+(i+1)+'</div></div>';
  w.innerHTML=h;}
function paintInputs(){for(var i=0;i<S.di.length;i++){
  var d=document.getElementById('d'+i);if(d)d.classList.toggle('high',S.di[i]===0);}}

function paintHdr(){
  var set=(id,v)=>{var e=document.getElementById(id);if(e)e.textContent=v;};
  set('iface',S.net.iface);set('ip',S.net.ip);set('ssid',S.net.ssid||'');
  set('mask','0x'+S.mask.toString(16).toUpperCase().padStart(2,'0'));
  function badge(id,on){var e=document.getElementById(id);if(!e)return;
    e.textContent=on?'Connected':'Disconnected';e.style.color=on?'var(--ok)':'#ef4444';}
  badge('mqtt',S.mqtt_connected);
  badge('sk',S.sk_connected);}

function refresh(){fetch('/api/state').then(r=>r.json()).then(j=>{S=j;paintHdr();
  if(!document.getElementById('r0'))drawRelays();
  if(!document.getElementById('d0'))drawInputs();paintInputs();
  if(prevDI.length===S.di.length)for(let i=0;i<S.di.length;i++)
    if(S.di[i]!==prevDI[i])msg('DI'+(i+1)+' → '+(S.di[i]===0?'ACTIVE':'INACTIVE'));
  prevDI=S.di.slice();}).catch(e=>msg('State error: '+e.message));}

async function loadCfg(){try{
  const r=await fetch('/api/config');const d=await r.json();
  var f=(id,v)=>{var e=document.getElementById(id);if(e)e.value=v||'';};
  f('cfg-ssid',d.ssid);f('cfg-wpass','');f('cfg-host',d.host);
  f('cfg-mbroker',d.mbroker);f('cfg-mport',d.mport);
  f('cfg-muser',d.muser);f('cfg-mpass','');
  f('cfg-mcid',d.mcid);f('cfg-mtopic',d.mtopic);
  f('cfg-skhost',d.skhost);f('cfg-skport',d.skport);
  f('cfg-skuser',d.skuser);
  f('cfg-skpass','');   // never echoed
  f('cfg-sktoken','');  // never echoed
  f('cfg-skvessel',d.skvessel);
  // Show token status without revealing value
  var t=document.getElementById('cfg-sktoken');
  if(t)t.placeholder=d.sk_has_token?'Token stored in NVS (leave blank to keep)':'No token stored';
  var c=document.getElementById('skClearBtn');
  if(c)c.style.display=d.sk_denied?'':'none';
}catch(e){msg('Config load error: '+e.message);}}

function saveCfg(){
  var g=(id)=>document.getElementById(id)?.value||'';
  var body=new URLSearchParams({
    ssid:g('cfg-ssid'),wpass:g('cfg-wpass'),host:g('cfg-host'),
    mbroker:g('cfg-mbroker'),mport:g('cfg-mport'),
    muser:g('cfg-muser'),mpass:g('cfg-mpass'),
    mcid:g('cfg-mcid'),mtopic:g('cfg-mtopic'),
    skhost:g('cfg-skhost'),skport:g('cfg-skport'),
    skuser:g('cfg-skuser'),skpass:g('cfg-skpass'),
    sktoken:g('cfg-sktoken'),skvessel:g('cfg-skvessel')});
  fetch('/api/config',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},body})
    .then(()=>msg('Config saved — rebooting...'))
    .catch(e=>msg('Save error: '+e.message));}

function skRequestAccess(){
  var btn=document.getElementById('skAccessBtn');
  if(btn){btn.textContent='Requesting... (approve in SK admin UI)';btn.disabled=true;}
  msg('Submitting device access request — approve in SignalK admin UI within 120 s...');
  fetch('/api/sk/request-access',{method:'POST'})
    .then(r=>r.json())
    .then(d=>{
      if(d.ok){msg('Access approved — SK reconnecting with new token.');}
      else if(d.denied){
        msg('Access DENIED by server. Click "Clear denied & re-pair" to try again.');
        var c=document.getElementById('skClearBtn');if(c)c.style.display='';
      } else {
        msg('Access request failed: '+(d.error||'unknown — check log'));}
    })
    .catch(e=>msg('Access request error: '+e.message))
    .finally(()=>{if(btn){btn.textContent='Request access now';btn.disabled=false;}});}

function skClearDenied(){
  fetch('/api/sk/clear-denied',{method:'POST'})
    .then(()=>{
      msg('Denied flag cleared — rebooting to retry access request...');
      var c=document.getElementById('skClearBtn');if(c)c.style.display='none';})
    .catch(e=>msg('Error: '+e.message));}

function resetCfg(){if(!confirm('Reset all settings to defaults?'))return;
  fetch('/api/config/reset',{method:'POST'})
    .then(()=>{msg('Reset — rebooting...');loadCfg();})
    .catch(e=>msg('Reset error: '+e.message));}

document.getElementById('rebootBtn').addEventListener('click',
  ()=>fetch('/reboot').then(()=>msg('Reboot sent.')));

setInterval(refresh,500);
window.addEventListener('load',()=>{refresh();loadCfg();fetchLog();setInterval(fetchLog,2000);});
</script>
</body>
</html>
)rawliteral";

// ── JSON helpers ──────────────────────────────────────────────────────────────
String buildStateJson() {
  JsonDocument doc;
  doc["mask"]           = relayGetMask();
  doc["mqtt_connected"] = mqttIsConnected();
  doc["sk_connected"]   = skIsConnected();

  uint8_t di = readDI_mask();
  doc["di_mask"] = di;
  JsonArray arr = doc["di"].to<JsonArray>();
  for (uint8_t i = 0; i < BoardPins::DI_COUNT; ++i) arr.add((di >> i) & 1);
  doc["count"] = BoardPins::RELAY_COUNT;

  JsonObject net = doc["net"].to<JsonObject>();
  if (g_mode == MODE_ETH) {
    net["iface"] = "Ethernet"; net["ip"] = ETH.localIP().toString();
    net["ssid"]  = "";         net["rssi"] = 0;
  } else if (g_mode == MODE_WIFI) {
    net["iface"] = "WiFi";     net["ip"] = WiFi.localIP().toString();
    net["ssid"]  = WiFi.SSID(); net["rssi"] = WiFi.RSSI();
  } else if (g_mode == MODE_AP) {
    net["iface"] = "WiFi AP";  net["ip"] = WiFi.softAPIP().toString();
    net["ssid"]  = g_cfg.hostname + "-AP"; net["rssi"] = 0;
  } else {
    net["iface"] = "None";     net["ip"] = "0.0.0.0";
    net["ssid"]  = "";         net["rssi"] = 0;
  }

  String out; serializeJson(doc, out);
  return out;
}

String buildLogJson() {
  std::vector<UILogLine> v;
  UI_LOG.collectTail(200, v);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto& l : v) {
    JsonObject o = arr.add<JsonObject>();
    o["t"] = l.ms; o["m"] = l.txt;
  }
  String out; serializeJson(doc, out);
  return out;
}

// ── Handlers ──────────────────────────────────────────────────────────────────
static void handleIndex()  { server.send(200, "text/html",        HTML_INDEX); }
static void handleState()  { server.send(200, "application/json", buildStateJson()); }

static void handleRelay() {
  String idx_s = server.hasArg("index") ? server.arg("index") : server.arg("idx");
  String on_s  = server.hasArg("state") ? server.arg("state") : server.arg("on");
  if (!idx_s.length() || !on_s.length()) { server.send(400, "text/plain", "Missing params"); return; }
  uint8_t idx = (uint8_t)idx_s.toInt();
  if (idx >= BoardPins::RELAY_COUNT) { server.send(400, "text/plain", "Index out of range"); return; }
  relaySet(idx, (on_s == "1" || on_s == "true" || on_s == "on"));
  handleState();
}

static void handleMask() {
  if (!server.hasArg("value")) { server.send(400, "text/plain", "Missing value"); return; }
  relayApplyMask((uint8_t)server.arg("value").toInt());
  handleState();
}

static void handleGetConfig() {
  JsonDocument doc;
  doc["ssid"]        = g_cfg.wifiSsid;
  doc["host"]        = g_cfg.hostname;
  doc["mbroker"]     = g_cfg.mqttBroker;
  doc["mport"]       = g_cfg.mqttPort;
  doc["muser"]       = g_cfg.mqttUser;
  doc["mcid"]        = g_cfg.mqttClientId;
  doc["mtopic"]      = g_cfg.mqttBaseTopic;
  doc["skhost"]      = g_cfg.skServer;
  doc["skport"]      = g_cfg.skPort;
  doc["skuser"]      = g_cfg.skUser;
  doc["skvessel"]    = g_cfg.skVesselName;
  doc["sk_has_token"]= (g_cfg.skToken.length() > 0);
  doc["sk_denied"]   = skAccessDenied();
  // wpass, mpass, skpass, sktoken never sent to browser
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handlePostConfig() {
  auto arg = [&](const char* k, String& dest) {
    if (server.hasArg(k) && server.arg(k).length() > 0) dest = server.arg(k);
  };
  arg("ssid",     g_cfg.wifiSsid);
  arg("wpass",    g_cfg.wifiPass);
  arg("host",     g_cfg.hostname);
  arg("mbroker",  g_cfg.mqttBroker);
  if (server.hasArg("mport") && server.arg("mport").length() > 0)
    g_cfg.mqttPort = (uint16_t)server.arg("mport").toInt();
  arg("muser",    g_cfg.mqttUser);
  arg("mpass",    g_cfg.mqttPass);
  arg("mcid",     g_cfg.mqttClientId);
  arg("mtopic",   g_cfg.mqttBaseTopic);
  arg("skhost",   g_cfg.skServer);
  if (server.hasArg("skport") && server.arg("skport").length() > 0)
    g_cfg.skPort = (uint16_t)server.arg("skport").toInt();
  arg("skuser",   g_cfg.skUser);
  arg("skpass",   g_cfg.skPass);
  arg("sktoken",  g_cfg.skToken);
  arg("skvessel", g_cfg.skVesselName);
  configSave();
  server.send(200, "text/plain", "saved");
  delay(500);
  ESP.restart();
}

static void handleConfigReset() {
  configReset();
  server.send(200, "text/plain", "reset");
  delay(500);
  ESP.restart();
}

static void handleDiag() {
  JsonDocument doc;
  doc["heap"]      = ESP.getFreeHeap();
  doc["uptime_ms"] = millis();
  doc["mqtt"]      = mqttIsConnected();
  doc["sk"]        = skIsConnected();
  doc["core"]      = xPortGetCoreID();
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ── httpStart ─────────────────────────────────────────────────────────────────
void httpStart() {
  server.on("/",                   HTTP_GET,  handleIndex);
  server.on("/api/state",          HTTP_GET,  handleState);
  server.on("/api/relay",          HTTP_GET,  handleRelay);
  server.on("/api/relay",          HTTP_POST, handleRelay);
  server.on("/api/mask",           HTTP_POST, handleMask);
  server.on("/api/log",            HTTP_GET,  []{ server.send(200, "application/json", buildLogJson()); });
  server.on("/api/log/clear",      HTTP_POST, []{ UI_LOG.clear(); server.send(200, "text/plain", "cleared"); });
  server.on("/api/config",         HTTP_GET,  handleGetConfig);
  server.on("/api/config",         HTTP_POST, handlePostConfig);
  server.on("/api/config/reset",   HTTP_POST, handleConfigReset);
  server.on("/api/diag",           HTTP_GET,  handleDiag);
  server.on("/api/sk/request-access", HTTP_POST, []{
    // Runs skRequestAccess() on core 1 — may block up to 120 s while polling
    // Response will be slow if admin hasn't approved yet; JS sets no timeout
    bool ok = skRequestAccess();
    JsonDocument doc;
    doc["ok"]     = ok;
    doc["denied"] = skAccessDenied();
    if (!ok && !skAccessDenied()) doc["error"] = "Access request failed or timed out";
    String out; serializeJson(doc, out);
    server.send(ok ? 200 : 202, "application/json", out);
  });
  server.on("/api/sk/clear-denied", HTTP_POST, []{
    skClearDenied();
    server.send(200, "text/plain", "cleared");
    delay(500); ESP.restart();
  });
  server.on("/reboot",             HTTP_GET,  []{
    server.send(200, "text/plain", "rebooting"); delay(200); ESP.restart();
  });
  server.begin();
  LOG("[HTTP] Server started on :80");
}