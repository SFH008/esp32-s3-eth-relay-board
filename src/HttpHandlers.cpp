#include "HttpHandlers.h"
#include "Config.h"
#include "Logger.h"
#include "BoardPins.h"
#include "RelayControl.h"
#include "MqttManager.h"
#include "SignalKClient.h"
#include "N2kManager.h"
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
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Relay Board</title>
  <style>
    :root{
      --gap:12px;--radius:12px;--card-w:1020px;
      --bg:#f0f2f5;--card:#fff;--border:#e4e6ea;--shadow:0 2px 8px rgba(0,0,0,.07);
      --text:#111;--muted:#6b7280;--brand:#2563eb;--brand-bg:#eff6ff;
      --ok:#16a34a;--ok-bg:#dcfce7;--err:#dc2626;--err-bg:#fee2e2;
      --warn:#d97706;--warn-bg:#fef3c7;
    }
    *{box-sizing:border-box;margin:0;padding:0;}
    body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);
         color:var(--text);padding:20px;display:flex;justify-content:center;}
    .page{width:100%;max-width:var(--card-w);}

    /* ── Cards ── */
    .card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);
          box-shadow:var(--shadow);padding:16px;margin-bottom:14px;}
    .card-title{font-weight:700;font-size:.95rem;color:#374151;
                border-bottom:1px solid var(--border);padding-bottom:8px;margin-bottom:12px;
                display:flex;align-items:center;justify-content:space-between;}
    .subsect{font-size:.78rem;font-weight:700;text-transform:uppercase;letter-spacing:.06em;
             color:var(--muted);margin:14px 0 8px;}
    .subsect:first-child{margin-top:0;}

    /* ── Status header ── */
    .status-bar{display:flex;flex-wrap:wrap;gap:8px 20px;font-size:.88rem;}
    .stat{display:flex;align-items:center;gap:6px;}
    .stat-label{color:var(--muted);font-weight:500;}
    .badge{display:inline-flex;align-items:center;gap:4px;font-size:.78rem;font-weight:600;
           padding:2px 8px;border-radius:999px;white-space:nowrap;}
    .badge-ok  {background:var(--ok-bg); color:var(--ok);}
    .badge-err {background:var(--err-bg);color:var(--err);}
    .badge-warn{background:var(--warn-bg);color:var(--warn);}
    .badge-muted{background:#f3f4f6;color:var(--muted);}
    .dot-led{width:8px;height:8px;border-radius:50%;background:currentColor;flex-shrink:0;}

    /* ── Digital inputs ── */
    .di-grid{display:grid;grid-template-columns:repeat(8,1fr);gap:8px;}
    @media(max-width:600px){.di-grid{grid-template-columns:repeat(4,1fr);}}
    .di-chip{border:1px solid var(--border);border-radius:8px;padding:10px 6px;
             text-align:center;font-size:.8rem;font-weight:600;color:var(--muted);
             transition:all .15s;}
    .di-chip.hi{border-color:var(--ok);background:var(--ok-bg);color:var(--ok);}
    .di-num{font-size:.7rem;font-weight:500;opacity:.7;margin-bottom:3px;}
    .di-status{font-size:.78rem;}

    /* ── Relays ── */
    .relay-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:var(--gap);}
    @media(max-width:600px){.relay-grid{grid-template-columns:repeat(2,1fr);}}
    .relay-btn{display:flex;flex-direction:column;align-items:center;gap:4px;
               border:2px solid var(--border);border-radius:10px;padding:14px 8px;
               background:#fafafa;cursor:pointer;transition:all .12s;
               font-family:inherit;width:100%;}
    .relay-btn:hover{border-color:#94a3b8;background:#f1f5f9;}
    .relay-btn:active{transform:translateY(1px);}
    .relay-btn.on{border-color:var(--brand);background:var(--brand-bg);}
    .relay-btn .r-label{font-weight:700;font-size:.92rem;color:#374151;}
    .relay-btn.on .r-label{color:var(--brand);}
    .relay-btn .r-state{font-size:.72rem;font-weight:600;
                        color:var(--muted);text-transform:uppercase;letter-spacing:.05em;}
    .relay-btn.on .r-state{color:var(--brand);}
    .relay-indicator{width:10px;height:10px;border-radius:50%;
                     background:#cbd5e1;border:1px solid #94a3b8;flex-shrink:0;}
    .relay-btn.on .relay-indicator{background:var(--brand);border-color:#1d4ed8;
                                   box-shadow:0 0 0 3px rgba(37,99,235,.18);}

    /* ── Toolbar ── */
    .toolbar{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px;}
    .btn{border:1px solid var(--border);background:#fff;color:#374151;border-radius:8px;
         padding:8px 14px;cursor:pointer;font-size:.85rem;font-weight:600;
         font-family:inherit;transition:background .15s,border-color .15s;}
    .btn:hover{background:#f3f4f6;border-color:#94a3b8;}
    .btn:active{transform:translateY(1px);}
    .btn-primary{background:var(--brand);color:#fff;border-color:var(--brand);}
    .btn-primary:hover{background:#1d4ed8;border-color:#1d4ed8;}
    .btn-danger{color:var(--err);border-color:#fca5a5;}
    .btn-danger:hover{background:var(--err-bg);}

    /* ── Config form ── */
    .cfg-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
    @media(max-width:580px){.cfg-grid{grid-template-columns:1fr;}}
    .cfg-grid-3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;}
    @media(max-width:700px){.cfg-grid-3{grid-template-columns:1fr 1fr;}}
    @media(max-width:480px){.cfg-grid-3{grid-template-columns:1fr;}}
    .field{display:flex;flex-direction:column;gap:3px;}
    .field label{font-size:.78rem;font-weight:600;color:var(--muted);}
    input[type=text],input[type=password],input[type=number]{
      border:1px solid var(--border);border-radius:7px;padding:7px 9px;
      font-size:.88rem;font-family:inherit;color:var(--text);width:100%;
      transition:border-color .15s,box-shadow .15s;}
    input:focus{outline:none;border-color:var(--brand);
                box-shadow:0 0 0 3px rgba(37,99,235,.12);}
    /* relay-label grid: 4 cols */
    .label-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;}
    @media(max-width:560px){.label-grid{grid-template-columns:repeat(2,1fr);}}

    /* ── Log ── */
    .log-box{font-family:'Menlo','Consolas',monospace;font-size:12px;line-height:1.6;
             height:150px;overflow-y:auto;background:#f8fafc;
             border:1px solid var(--border);border-radius:8px;padding:10px;}
    .log-ts{color:var(--muted);}

    /* ── Misc ── */
    h1{font-size:1.25rem;font-weight:800;margin-bottom:2px;}
    .page-sub{font-size:.82rem;color:var(--muted);margin-bottom:14px;}
  </style>
</head>
<body>
<div class="page">

  <h1>ESP32-S3 Relay Board</h1>
  <p class="page-sub" id="page-sub">Loading…</p>

  <!-- ── Status ── -->
  <div class="card">
    <div class="status-bar" id="status-bar">
      <div class="stat"><span class="stat-label">Interface</span><span id="s-iface" class="badge badge-muted">—</span></div>
      <div class="stat"><span class="stat-label">IP</span><span id="s-ip">—</span></div>
      <div class="stat"><span class="stat-label">Relay mask</span><code id="s-mask">0x00</code></div>
      <div class="stat"><span class="stat-label">MQTT</span><span id="s-mqtt" class="badge">—</span></div>
      <div class="stat"><span class="stat-label">SignalK</span><span id="s-sk" class="badge">—</span></div>
      <div class="stat"><span class="stat-label">NMEA 2000</span><span id="s-n2k" class="badge">—</span></div>
    </div>
  </div>

  <!-- ── Digital Inputs ── -->
  <div class="card">
    <div class="card-title">Digital Inputs</div>
    <div class="di-grid" id="di-grid"></div>
  </div>

  <!-- ── Relays ── -->
  <div class="card">
    <div class="card-title">
      <span>Relays</span>
      <div style="display:flex;gap:6px;">
        <button class="btn" onclick="allOff()">All OFF</button>
        <button class="btn btn-primary" onclick="allOn()">All ON</button>
      </div>
    </div>
    <div class="relay-grid" id="relay-grid"></div>
  </div>

  <!-- ── Configuration ── -->
  <div class="card">
    <div class="card-title">Configuration</div>

    <div class="subsect">Ethernet</div>
    <div class="cfg-grid">
      <div class="field"><label>Static IP</label><input id="c-eth-ip" type="text" placeholder="192.168.1.100"></div>
      <div class="field"><label>Gateway</label><input id="c-eth-gw" type="text" placeholder="192.168.1.1"></div>
      <div class="field"><label>Subnet mask</label><input id="c-eth-sn" type="text" placeholder="255.255.255.0"></div>
      <div class="field"><label>DNS</label><input id="c-eth-dns" type="text" placeholder="192.168.1.1"></div>
    </div>

    <div class="subsect">WiFi</div>
    <div class="cfg-grid">
      <div class="field"><label>SSID</label><input id="c-ssid" type="text"></div>
      <div class="field"><label>Password</label><input id="c-wpass" type="password"></div>
      <div class="field"><label>Hostname</label><input id="c-host" type="text"></div>
    </div>

    <div class="subsect">MQTT</div>
    <div class="cfg-grid">
      <div class="field"><label>Broker IP</label><input id="c-mbroker" type="text"></div>
      <div class="field"><label>Port</label><input id="c-mport" type="number" min="1" max="65535"></div>
      <div class="field"><label>Username</label><input id="c-muser" type="text"></div>
      <div class="field"><label>Password</label><input id="c-mpass" type="password"></div>
      <div class="field"><label>Client ID</label><input id="c-mcid" type="text"></div>
      <div class="field"><label>Base topic</label><input id="c-mtopic" type="text"></div>
    </div>

    <div class="subsect">SignalK</div>
    <div class="cfg-grid">
      <div class="field"><label>Server IP / hostname</label><input id="c-skhost" type="text"></div>
      <div class="field"><label>Port</label><input id="c-skport" type="number" min="1" max="65535"></div>
      <div class="field"><label>Username</label><input id="c-skuser" type="text" placeholder="Required to obtain token"></div>
      <div class="field"><label>Password</label><input id="c-skpass" type="password" placeholder="Required to obtain token"></div>
      <div class="field"><label>Bearer token</label><input id="c-sktoken" type="password" placeholder="Auto-filled after login"></div>
      <div class="field"><label>Vessel / source name</label><input id="c-skvessel" type="text"></div>
    </div>
    <div class="toolbar">
      <button class="btn" id="btn-sk-access" onclick="skRequestAccess()">Request SK access now</button>
      <button class="btn btn-danger" id="btn-sk-clear" style="display:none" onclick="skClearDenied()">Clear denied &amp; re-pair</button>
    </div>

    <div class="subsect">NMEA 2000</div>
    <div class="cfg-grid">
      <div class="field"><label>Device instance (relay bank, 0–252)</label><input id="c-n2k-inst" type="number" min="0" max="252"></div>
      <div class="field"><label>Preferred source address (0–251)</label><input id="c-n2k-addr" type="number" min="0" max="251"></div>
    </div>

    <div class="subsect">Relay labels</div>
    <div class="label-grid" id="label-grid"></div>

    <div class="toolbar" style="margin-top:16px;border-top:1px solid var(--border);padding-top:14px;">
      <button class="btn btn-primary" onclick="saveCfg()">Save &amp; Reboot</button>
      <button class="btn btn-danger" onclick="resetCfg()">Reset to defaults</button>
    </div>
  </div>

  <!-- ── Log ── -->
  <div class="card">
    <div class="card-title">
      <span>Log</span>
      <button class="btn" onclick="clearLog()">Clear</button>
    </div>
    <div class="log-box" id="log-box"></div>
  </div>

  <!-- ── Actions ── -->
  <div class="card toolbar">
    <button class="btn btn-danger" onclick="reboot()">Reboot</button>
  </div>

</div><!-- .page -->
<script>
// ── State ─────────────────────────────────────────────────────────────────────
var S = {mask:0, di:[], count:8, labels:[], net:{iface:'',ip:'',ssid:''},
         mqtt_connected:false, sk_connected:false, n2k_running:false};
var prevDI = [];
var relaysDrawn = false;
var diDrawn = false;

// ── Utilities ─────────────────────────────────────────────────────────────────
function fmtMs(ms){
  var s=Math.floor(ms/1000);
  return [Math.floor(s/3600)%24, Math.floor(s%3600/60), s%60]
    .map(v=>String(v).padStart(2,'0')).join(':');
}
function badge(cls, dot, text){
  return '<span class="badge '+cls+'"><span class="dot-led"></span>'+text+'</span>';
}

// ── Log ──────────────────────────────────────────────────────────────────────
function appendLog(txt){
  var b=document.getElementById('log-box');
  if(!b)return;
  var line=document.createElement('div');
  line.innerHTML='<span class="log-ts">['+fmtMs(performance.now())+']</span> '+txt;
  b.appendChild(line);
  b.scrollTop=b.scrollHeight;
}
function clearLog(){
  fetch('/api/log/clear',{method:'POST'});
  var b=document.getElementById('log-box'); if(b)b.innerHTML='';
}
async function fetchLog(){
  try{
    var r=await fetch('/api/log'); var d=await r.json();
    var b=document.getElementById('log-box'); if(!b)return;
    b.innerHTML=d.map(o=>'<div><span class="log-ts">['+fmtMs(o.t)+']</span> '+o.m+'</div>').join('');
    b.scrollTop=b.scrollHeight;
  }catch(e){}
}

// ── Status header ─────────────────────────────────────────────────────────────
function paintStatus(){
  var set=(id,h)=>{var e=document.getElementById(id);if(e)e.innerHTML=h;};
  var iface=S.net.iface||'—';
  var icls = iface==='Ethernet'||iface.startsWith('WiFi') ? 'badge-ok' : 'badge-muted';
  set('s-iface','<span class="badge '+icls+'">'+iface+'</span>');
  set('s-ip', S.net.ip||'—');
  set('s-mask','0x'+(S.mask>>>0).toString(16).toUpperCase().padStart(2,'0'));
  set('s-mqtt', badge(S.mqtt_connected?'badge-ok':'badge-err','',S.mqtt_connected?'Connected':'Disconnected'));
  set('s-sk',   badge(S.sk_connected  ?'badge-ok':'badge-err','',S.sk_connected  ?'Connected':'Disconnected'));
  set('s-n2k',  badge(S.n2k_running   ?'badge-ok':'badge-warn','',S.n2k_running  ?'Running':'Offline'));
  var sub='';
  if(S.net.ssid)sub+=S.net.ssid;
  if(S.net.rssi)sub+=' ('+S.net.rssi+' dBm)';
  var sp=document.getElementById('page-sub'); if(sp)sp.textContent=sub||'ESP32-S3 8-Relay Board';
}

// ── Digital inputs ────────────────────────────────────────────────────────────
function drawDI(){
  var g=document.getElementById('di-grid'); if(!g)return;
  var h='';
  for(var i=0;i<S.di.length;i++){
    var hi=S.di[i]===1;
    h+='<div class="di-chip'+(hi?' hi':'')+'" id="di'+i+'">'
      +'<div class="di-num">DI'+(i+1)+'</div>'
      +'<div class="di-status">'+(hi?'HIGH':'LOW')+'</div>'
      +'</div>';
  }
  g.innerHTML=h; diDrawn=true;
}
function paintDI(){
  for(var i=0;i<S.di.length;i++){
    var el=document.getElementById('di'+i); if(!el)return;
    var hi=S.di[i]===1;
    el.classList.toggle('hi',hi);
    el.querySelector('.di-status').textContent=hi?'HIGH':'LOW';
  }
}

// ── Relays ────────────────────────────────────────────────────────────────────
function labelFor(i){ return (S.labels&&S.labels[i])||('R'+(i+1)); }

function drawRelays(){
  var g=document.getElementById('relay-grid'); if(!g)return;
  var h='';
  for(var i=0;i<S.count;i++){
    var on=((S.mask>>i)&1)===1;
    h+='<button class="relay-btn'+(on?' on':'')+'" id="rb'+i+'" onclick="toggleRelay('+i+')">'
      +'<div class="relay-indicator"></div>'
      +'<div class="r-label">'+labelFor(i)+'</div>'
      +'<div class="r-state">'+(on?'ON':'OFF')+'</div>'
      +'</button>';
  }
  g.innerHTML=h; relaysDrawn=true;
}
function paintRelays(){
  for(var i=0;i<S.count;i++){
    var b=document.getElementById('rb'+i); if(!b)continue;
    var on=((S.mask>>i)&1)===1;
    b.classList.toggle('on',on);
    b.querySelector('.r-label').textContent=labelFor(i);
    b.querySelector('.r-state').textContent=on?'ON':'OFF';
  }
}

function toggleRelay(i){
  var was=((S.mask>>i)&1)===1, next=!was;
  if(next)S.mask|=(1<<i); else S.mask&=~(1<<i);
  paintRelays(); paintStatus();
  appendLog(labelFor(i)+' → '+(next?'ON':'OFF'));
  fetch('/api/relay',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams({index:String(i),state:next?'1':'0'})})
  .catch(e=>{
    if(next)S.mask&=~(1<<i); else S.mask|=(1<<i);
    paintRelays(); paintStatus();
    appendLog('Error toggling relay: '+e.message);
  });
}

function allOn() { applyMask(0xFF); }
function allOff(){ applyMask(0x00); }
function applyMask(v){
  fetch('/api/mask',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams({value:String(v)})})
  .then(r=>r.json()).then(j=>{S=j;paintRelays();paintStatus();})
  .catch(e=>appendLog('Mask error: '+e.message));
}

// ── Poll ──────────────────────────────────────────────────────────────────────
function refresh(){
  fetch('/api/state').then(r=>r.json()).then(j=>{
    var labelsChanged=JSON.stringify(j.labels)!==JSON.stringify(S.labels);
    S=j;
    paintStatus();
    if(!diDrawn)drawDI(); else paintDI();
    if(!relaysDrawn||labelsChanged)drawRelays(); else paintRelays();
    // DI change events
    if(prevDI.length===S.di.length)
      for(var i=0;i<S.di.length;i++)
        if(S.di[i]!==prevDI[i])
          appendLog('DI'+(i+1)+' → '+(S.di[i]===1?'HIGH':'LOW'));
    prevDI=S.di.slice();
  }).catch(e=>appendLog('State error: '+e.message));
}

// ── Config load / save ────────────────────────────────────────────────────────
var LABEL_INPUTS = [];

async function loadCfg(){
  try{
    var r=await fetch('/api/config'); var d=await r.json();
    var f=(id,v)=>{var e=document.getElementById(id);if(e&&v!=null)e.value=v;};
    f('c-eth-ip', d.eth_ip); f('c-eth-gw',d.eth_gw);
    f('c-eth-sn', d.eth_sn); f('c-eth-dns',d.eth_dns);
    f('c-ssid',   d.ssid);   f('c-wpass',''); f('c-host',d.host);
    f('c-mbroker',d.mbroker);f('c-mport',d.mport);
    f('c-muser',  d.muser);  f('c-mpass','');
    f('c-mcid',   d.mcid);   f('c-mtopic',d.mtopic);
    f('c-skhost', d.skhost); f('c-skport',d.skport);
    f('c-skuser', d.skuser); f('c-skpass',''); f('c-sktoken','');
    f('c-skvessel',d.skvessel);
    f('c-n2k-inst',d.n2k_inst); f('c-n2k-addr',d.n2k_addr);

    // Token status placeholder
    var t=document.getElementById('c-sktoken');
    if(t)t.placeholder=d.sk_has_token?'Stored in NVS (leave blank to keep)':'No token stored';
    var cb=document.getElementById('btn-sk-clear');
    if(cb)cb.style.display=d.sk_denied?'':'none';

    // Relay label inputs
    var lg=document.getElementById('label-grid');
    if(lg){
      lg.innerHTML='';
      LABEL_INPUTS=[];
      for(var i=0;i<8;i++){
        var lbl=(d.labels&&d.labels[i])||('R'+(i+1));
        var div=document.createElement('div');
        div.className='field';
        div.innerHTML='<label>Relay '+(i+1)+'</label>'
          +'<input type="text" maxlength="16" value="'+lbl+'" id="rl'+i+'">';
        lg.appendChild(div);
        LABEL_INPUTS.push('rl'+i);
      }
    }
  }catch(e){appendLog('Config load error: '+e.message);}
}

function saveCfg(){
  var g=(id)=>{var e=document.getElementById(id);return e?e.value:'';};
  var p=new URLSearchParams({
    eth_ip:g('c-eth-ip'), eth_gw:g('c-eth-gw'),
    eth_sn:g('c-eth-sn'), eth_dns:g('c-eth-dns'),
    ssid:g('c-ssid'), wpass:g('c-wpass'), host:g('c-host'),
    mbroker:g('c-mbroker'), mport:g('c-mport'),
    muser:g('c-muser'),  mpass:g('c-mpass'),
    mcid:g('c-mcid'),   mtopic:g('c-mtopic'),
    skhost:g('c-skhost'),skport:g('c-skport'),
    skuser:g('c-skuser'),skpass:g('c-skpass'),
    sktoken:g('c-sktoken'),skvessel:g('c-skvessel'),
    n2k_inst:g('c-n2k-inst'), n2k_addr:g('c-n2k-addr')
  });
  for(var i=0;i<8;i++) p.append('rl'+i, g('rl'+i)||('R'+(i+1)));
  fetch('/api/config',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p})
  .then(()=>appendLog('Saved — rebooting…'))
  .catch(e=>appendLog('Save error: '+e.message));
}

function resetCfg(){
  if(!confirm('Reset all settings to defaults and reboot?'))return;
  fetch('/api/config/reset',{method:'POST'})
    .then(()=>{appendLog('Reset — rebooting…');setTimeout(loadCfg,3000);})
    .catch(e=>appendLog('Reset error: '+e.message));
}

function reboot(){
  if(!confirm('Reboot now?'))return;
  fetch('/reboot').then(()=>appendLog('Reboot sent.'));
}

// ── SignalK pairing ───────────────────────────────────────────────────────────
function skRequestAccess(){
  var btn=document.getElementById('btn-sk-access');
  if(btn){btn.textContent='Requesting… (approve in SK admin)';btn.disabled=true;}
  appendLog('Submitting SK access request — approve in SignalK admin within 120 s…');
  fetch('/api/sk/request-access',{method:'POST'})
    .then(r=>r.json())
    .then(d=>{
      if(d.ok)appendLog('Access approved — SK reconnecting.');
      else if(d.denied){
        appendLog('Access DENIED. Click "Clear denied & re-pair" to retry.');
        var c=document.getElementById('btn-sk-clear');if(c)c.style.display='';
      } else appendLog('Access request failed: '+(d.error||'check log'));
    })
    .catch(e=>appendLog('SK access error: '+e.message))
    .finally(()=>{if(btn){btn.textContent='Request SK access now';btn.disabled=false;}});
}
function skClearDenied(){
  fetch('/api/sk/clear-denied',{method:'POST'})
    .then(()=>{appendLog('Denied flag cleared — rebooting…');
               var c=document.getElementById('btn-sk-clear');if(c)c.style.display='none';})
    .catch(e=>appendLog('Error: '+e.message));
}

// ── Boot ─────────────────────────────────────────────────────────────────────
window.addEventListener('load',()=>{
  refresh(); loadCfg(); fetchLog();
  setInterval(refresh, 500);
  setInterval(fetchLog, 3000);
});
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
  doc["n2k_running"]    = n2kIsRunning();

  uint8_t di = readDI_mask();
  doc["di_mask"] = di;
  JsonArray diArr = doc["di"].to<JsonArray>();
  for (uint8_t i = 0; i < BoardPins::DI_COUNT; ++i) diArr.add((di >> i) & 1);
  doc["count"] = BoardPins::RELAY_COUNT;

  // Relay labels
  JsonArray lblArr = doc["labels"].to<JsonArray>();
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; ++i) lblArr.add(g_cfg.relayLabels[i]);

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

  String out; serializeJson(doc, out); return out;
}

String buildLogJson() {
  std::vector<UILogLine> v;
  UI_LOG.collectTail(200, v);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (auto& l : v) { JsonObject o = arr.add<JsonObject>(); o["t"]=l.ms; o["m"]=l.txt; }
  String out; serializeJson(doc, out); return out;
}

// ── Handlers ──────────────────────────────────────────────────────────────────
static void handleIndex()  { server.send(200, "text/html",        HTML_INDEX); }
static void handleState()  { server.send(200, "application/json", buildStateJson()); }

static void handleRelay() {
  String idx_s = server.hasArg("index") ? server.arg("index") : server.arg("idx");
  String on_s  = server.hasArg("state") ? server.arg("state") : server.arg("on");
  if (!idx_s.length() || !on_s.length()) { server.send(400,"text/plain","Missing params"); return; }
  uint8_t idx = (uint8_t)idx_s.toInt();
  if (idx >= BoardPins::RELAY_COUNT) { server.send(400,"text/plain","Index out of range"); return; }
  relaySet(idx, on_s=="1"||on_s=="true"||on_s=="on");
  handleState();
}

static void handleMask() {
  if (!server.hasArg("value")) { server.send(400,"text/plain","Missing value"); return; }
  relayApplyMask((uint8_t)server.arg("value").toInt());
  handleState();
}

static void handleGetConfig() {
  JsonDocument doc;
  // Ethernet
  doc["eth_ip"]  = g_cfg.ethIp;
  doc["eth_gw"]  = g_cfg.ethGateway;
  doc["eth_sn"]  = g_cfg.ethSubnet;
  doc["eth_dns"] = g_cfg.ethDns;
  // WiFi / Network
  doc["ssid"]    = g_cfg.wifiSsid;
  doc["host"]    = g_cfg.hostname;
  // MQTT
  doc["mbroker"] = g_cfg.mqttBroker;
  doc["mport"]   = g_cfg.mqttPort;
  doc["muser"]   = g_cfg.mqttUser;
  doc["mcid"]    = g_cfg.mqttClientId;
  doc["mtopic"]  = g_cfg.mqttBaseTopic;
  // SignalK
  doc["skhost"]      = g_cfg.skServer;
  doc["skport"]      = g_cfg.skPort;
  doc["skuser"]      = g_cfg.skUser;
  doc["skvessel"]    = g_cfg.skVesselName;
  doc["sk_has_token"]= g_cfg.skToken.length() > 0;
  doc["sk_denied"]   = skAccessDenied();
  // N2K
  doc["n2k_inst"] = g_cfg.n2kDeviceInstance;
  doc["n2k_addr"] = g_cfg.n2kSourceAddress;
  // Relay labels
  JsonArray lblArr = doc["labels"].to<JsonArray>();
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; ++i) lblArr.add(g_cfg.relayLabels[i]);
  // passwords never sent
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handlePostConfig() {
  auto arg = [&](const char* k, String& dest) {
    if (server.hasArg(k) && server.arg(k).length() > 0) dest = server.arg(k);
  };
  // Ethernet
  arg("eth_ip",  g_cfg.ethIp);
  arg("eth_gw",  g_cfg.ethGateway);
  arg("eth_sn",  g_cfg.ethSubnet);
  arg("eth_dns", g_cfg.ethDns);
  // WiFi
  arg("ssid",    g_cfg.wifiSsid);
  arg("wpass",   g_cfg.wifiPass);
  arg("host",    g_cfg.hostname);
  // MQTT
  arg("mbroker", g_cfg.mqttBroker);
  if (server.hasArg("mport") && server.arg("mport").length())
    g_cfg.mqttPort = (uint16_t)server.arg("mport").toInt();
  arg("muser",   g_cfg.mqttUser);
  arg("mpass",   g_cfg.mqttPass);
  arg("mcid",    g_cfg.mqttClientId);
  arg("mtopic",  g_cfg.mqttBaseTopic);
  // SignalK
  arg("skhost",  g_cfg.skServer);
  if (server.hasArg("skport") && server.arg("skport").length())
    g_cfg.skPort = (uint16_t)server.arg("skport").toInt();
  arg("skuser",  g_cfg.skUser);
  arg("skpass",  g_cfg.skPass);
  arg("sktoken", g_cfg.skToken);
  arg("skvessel",g_cfg.skVesselName);
  // N2K
  if (server.hasArg("n2k_inst") && server.arg("n2k_inst").length())
    g_cfg.n2kDeviceInstance = (uint8_t)constrain(server.arg("n2k_inst").toInt(), 0, 252);
  if (server.hasArg("n2k_addr") && server.arg("n2k_addr").length())
    g_cfg.n2kSourceAddress  = (uint8_t)constrain(server.arg("n2k_addr").toInt(), 0, 251);
  // Relay labels
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; ++i) {
    char key[4]; snprintf(key, sizeof(key), "rl%d", i);
    if (server.hasArg(key)) {
      String lbl = server.arg(key);
      lbl.trim();
      if (lbl.length() == 0) { char def[4]; snprintf(def,sizeof(def),"R%d",i+1); lbl=def; }
      if (lbl.length() > 16) lbl = lbl.substring(0,16);
      g_cfg.relayLabels[i] = lbl;
    }
  }

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
  doc["n2k"]       = n2kIsRunning();
  doc["core"]      = xPortGetCoreID();
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ── httpStart ─────────────────────────────────────────────────────────────────
void httpStart() {
  server.on("/",                      HTTP_GET,  handleIndex);
  server.on("/api/state",             HTTP_GET,  handleState);
  server.on("/api/relay",             HTTP_GET,  handleRelay);
  server.on("/api/relay",             HTTP_POST, handleRelay);
  server.on("/api/mask",              HTTP_POST, handleMask);
  server.on("/api/log",               HTTP_GET,  []{ server.send(200,"application/json",buildLogJson()); });
  server.on("/api/log/clear",         HTTP_POST, []{ UI_LOG.clear(); server.send(200,"text/plain","cleared"); });
  server.on("/api/config",            HTTP_GET,  handleGetConfig);
  server.on("/api/config",            HTTP_POST, handlePostConfig);
  server.on("/api/config/reset",      HTTP_POST, handleConfigReset);
  server.on("/api/diag",              HTTP_GET,  handleDiag);
  server.on("/api/sk/request-access", HTTP_POST, []{
    bool ok = skRequestAccess();
    JsonDocument doc;
    doc["ok"]     = ok;
    doc["denied"] = skAccessDenied();
    if (!ok && !skAccessDenied()) doc["error"] = "Access request failed or timed out";
    String out; serializeJson(doc, out);
    server.send(ok ? 200 : 202, "application/json", out);
  });
  server.on("/api/sk/clear-denied",   HTTP_POST, []{
    skClearDenied();
    server.send(200,"text/plain","cleared");
    delay(500); ESP.restart();
  });
  server.on("/reboot", HTTP_GET, []{
    server.send(200,"text/plain","rebooting"); delay(200); ESP.restart();
  });
  server.begin();
  LOG("[HTTP] Server started on :80");
}