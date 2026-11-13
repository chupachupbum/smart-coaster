/**
 * @file wifi_portal.cpp
 * @brief Embedded WebServer, captive portal, and REST API controller for Wi-Fi management.
 */

#include "wifi_portal.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef ARDUINO
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart Cup Stand</title>
<style>
:root{--bg:#121212;--card:#1e1e1e;--border:#333;--text:#e0e0e0;--accent:#3b82f6;--green:#10b981;--red:#ef4444;--yellow:#f59e0b}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;padding:12px;max-width:480px;margin:0 auto;line-height:1.4}
header{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid var(--border);margin-bottom:12px}
h1{font-size:1.15rem;font-weight:600}
.badge{padding:3px 8px;border-radius:12px;font-size:0.75rem;font-weight:600}
.badge-green{background:#064e3b;color:#6ee7b7}
.badge-yellow{background:#78350f;color:#fcd34d}
.badge-red{background:#7f1d1d;color:#fca5a5}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:14px;margin-bottom:12px}
.card h2{font-size:0.95rem;margin-bottom:10px;display:flex;justify-content:space-between;align-items:center}
.btn{background:var(--accent);color:#fff;border:none;border-radius:6px;padding:7px 12px;font-size:0.85rem;cursor:pointer;font-weight:500}
.btn:hover{opacity:0.9}
.btn-sm{padding:4px 8px;font-size:0.75rem}
.btn-danger{background:var(--red)}
.btn-secondary{background:#374151}
.stat-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:10px}
.stat-box{background:#262626;padding:8px 10px;border-radius:6px}
.stat-val{font-size:1.1rem;font-weight:700;color:#fff}
.stat-lbl{font-size:0.7rem;color:#9ca3af;text-transform:uppercase}
.progress-bg{background:#374151;border-radius:8px;height:12px;overflow:hidden;margin:8px 0}
.progress-fill{background:var(--green);height:100%;width:0%;transition:width 0.3s}
.btn-group{display:flex;gap:6px;margin-top:10px;flex-wrap:wrap}
.net-item{display:flex;justify-content:space-between;align-items:center;padding:8px;background:#262626;border-radius:6px;margin-bottom:6px;font-size:0.85rem}
.net-info{display:flex;align-items:center;gap:6px}
.rssi-bars{font-family:monospace;font-size:0.75rem;color:var(--accent)}
.form-group{margin-bottom:8px}
.form-group label{display:block;font-size:0.75rem;color:#9ca3af;margin-bottom:3px}
.form-group input{width:100%;padding:7px 10px;background:#262626;border:1px solid var(--border);border-radius:6px;color:#fff;font-size:0.85rem}
.banner{background:#7f1d1d;color:#fecaca;padding:10px;border-radius:8px;margin-bottom:12px;font-size:0.8rem;border:1px solid #b91c1c}
</style>
</head>
<body>
<header>
  <div>
    <h1>Smart Coaster</h1>
    <div style="font-size:0.75rem;color:#9ca3af">smart-coaster.local</div>
  </div>
  <div style="text-align:right">
    <span id="b-wifi" class="badge badge-yellow">AP Setup Mode</span>
    <div style="margin-top:4px"><span id="b-srv" class="badge badge-yellow" style="font-size:0.7rem">ntfy.sh: UNKNOWN</span></div>
  </div>
</header>

<div id="rollback-banner" class="banner" style="display:none"></div>

<div class="card">
  <h2><span>Wi-Fi Networks</span>
    <div>
      <button class="btn btn-sm" onclick="scanWifi()">Rescan</button>
      <button class="btn btn-sm btn-danger" onclick="forgetWifi()">Forget</button>
    </div>
  </h2>
  <div id="net-list" style="margin-top:8px"><div style="color:#9ca3af;font-size:0.8rem">Scanning networks...</div></div>
</div>

<div class="card">
  <h2><span>Live Coaster</span><span id="cup-badge" class="badge badge-green">EMPTY</span></h2>
  <div class="stat-grid">
    <div class="stat-box"><div class="stat-lbl">Weight</div><div class="stat-val"><span id="w-net">0.0</span> <span style="font-size:0.8rem">g</span></div></div>
    <div class="stat-box"><div class="stat-lbl">Reminder In</div><div class="stat-val"><span id="countdown">--</span><span style="font-size:0.8rem">s</span></div></div>
  </div>
  <div style="display:flex;justify-content:space-between;font-size:0.8rem;color:#9ca3af">
    <span>Intake Progress</span>
    <span><span id="consumed">0</span> / <span id="goal">2000</span> ml (<span id="pct">0</span>%)</span>
  </div>
  <div class="progress-bg"><div id="progress-bar" class="progress-fill"></div></div>
  <div class="btn-group">
    <button class="btn btn-secondary" onclick="action('tare')">Tare</button>
    <button class="btn btn-secondary" onclick="action('reset')">Reset Intake</button>
    <button id="btn-demo" class="btn btn-secondary" onclick="toggleDemo()">Demo Mode: OFF</button>
  </div>
</div>

<div class="card">
  <h2><span>Notifications & Goal</span></h2>
  <div class="form-group">
    <label>Daily Goal (ml)</label>
    <input type="number" id="cfg-goal" min="100" max="10000" step="50" value="2000">
  </div>
  <div class="form-group">
    <label>ntfy.sh Server URL</label>
    <input type="text" id="cfg-srv" value="http://ntfy.sh">
  </div>
  <div class="form-group">
    <label>ntfy.sh Topic</label>
    <input type="text" id="cfg-topic" value="smart-cup-stand">
  </div>
  <div class="btn-group">
    <button class="btn" onclick="saveConfig()">Save Settings</button>
    <button class="btn btn-secondary" onclick="action('test_push')">Send Test Alert</button>
  </div>
</div>

<script>
let isDemo = false;
async function poll(){
  try{
    const r=await fetch('/api/status');
    if(r.ok){
      const d=await r.json();
      document.getElementById('w-net').textContent=d.net_weight_g.toFixed(1);
      document.getElementById('cup-badge').textContent=d.cup_state;
      document.getElementById('cup-badge').className='badge '+(d.cup_state==='EMPTY'?'badge-yellow':'badge-green');
      document.getElementById('countdown').textContent=d.countdown_s;
      document.getElementById('consumed').textContent=d.consumed_ml.toFixed(0);
      document.getElementById('goal').textContent=d.daily_goal_ml.toFixed(0);
      document.getElementById('pct').textContent=d.progress_pct.toFixed(0);
      document.getElementById('progress-bar').style.width=Math.min(100,d.progress_pct)+'%';
      isDemo=d.demo_mode;
      document.getElementById('btn-demo').textContent='Demo Mode: '+(isDemo?'ON':'OFF');
      const wb=document.getElementById('b-wifi');
      if(d.wifi_state==='CONNECTED'){
        wb.textContent='Connected ('+d.ssid+')';
        wb.className='badge badge-green';
      }else if(d.ap_active){
        wb.textContent='AP Setup Mode';
        wb.className='badge badge-yellow';
      }else{
        wb.textContent=d.wifi_state;
        wb.className='badge badge-red';
      }
      const sb=document.getElementById('b-srv');
      sb.textContent='ntfy.sh: '+d.server_status;
      sb.className='badge '+(d.server_status==='REACHABLE'?'badge-green':(d.server_status==='DEGRADED'?'badge-yellow':'badge-red'));
    }
  }catch(e){}
}
async function checkWifiStatus(){
  try{
    const r=await fetch('/api/wifi/status');
    if(r.ok){
      const d=await r.json();
      const rb=document.getElementById('rollback-banner');
      if(d.rollback_occurred){
        rb.style.display='block';
        rb.textContent='⚠️ Connection failed to "'+d.failed_ssid+'" ('+d.last_error+'). Restored previous connection.';
      }else{
        rb.style.display='none';
      }
    }
  }catch(e){}
}
async function scanWifi(){
  const nl=document.getElementById('net-list');
  nl.innerHTML='<div style="color:#9ca3af;font-size:0.8rem">Scanning for 2.4 GHz networks...</div>';
  try{
    await fetch('/api/wifi/scan',{method:'POST'});
    for(let i=0;i<10;i++){
      await new Promise(r=>setTimeout(r,800));
      const r=await fetch('/api/wifi/scan');
      if(r.ok){
        const d=await r.json();
        if(!d.scanning){
          renderNetworks(d.networks);
          return;
        }
      }
    }
  }catch(e){nl.innerHTML='<div style="color:#ef4444;font-size:0.8rem">Scan failed</div>';}
}
function renderNetworks(nets){
  const nl=document.getElementById('net-list');
  if(!nets||nets.length===0){
    nl.innerHTML='<div style="color:#9ca3af;font-size:0.8rem">No networks discovered</div>';
    return;
  }
  let h='';
  nets.forEach(n=>{
    const sig=n.rssi>-60?'▂▄▆█':(n.rssi>-70?'▂▄▆_':(n.rssi>-80?'▂▄__':(n.rssi>-90?'▂___':'____')));
    const lock=n.open?'🔓':'🔒';
    const s=n.ssid.replace(/"/g,'&quot;');
    h+='<div class="net-item"><div class="net-info"><span>'+lock+'</span><strong>'+s+'</strong><span class="rssi-bars">'+sig+' ('+n.rssi+'dBm)</span></div>';
    h+='<button class="btn btn-sm '+(n.open?'btn-secondary':'')+'" onclick="connectTo(\''+s+'\','+n.open+')">Connect</button></div>';
  });
  nl.innerHTML=h;
}
async function connectTo(ssid,isOpen){
  let pass='';
  if(!isOpen){
    pass=prompt('Enter Wi-Fi password for "'+ssid+'":');
    if(pass===null)return;
  }
  try{
    const r=await fetch('/api/wifi/connect',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({ssid:ssid,password:pass})
    });
    const d=await r.json();
    if(r.ok){
      alert('Connecting to "'+ssid+'"... Coaster will verify connection.');
      setTimeout(checkWifiStatus,4000);
    }else{
      alert('Error: '+(d.msg||'Failed to connect'));
    }
  }catch(e){alert('Request failed');}
}
async function forgetWifi(){
  if(!confirm('Forget stored Wi-Fi credentials and switch to AP Setup Mode?'))return;
  try{
    await fetch('/api/wifi/forget',{method:'POST'});
    alert('Wi-Fi credentials cleared. In AP Setup Mode.');
    poll();
  }catch(e){}
}
async function action(act){
  try{
    await fetch('/api/action',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({action:act})
    });
    poll();
  }catch(e){}
}
function toggleDemo(){
  action(isDemo?'demo_off':'demo_on');
}
async function saveConfig(){
  const g=parseFloat(document.getElementById('cfg-goal').value);
  const srv=document.getElementById('cfg-srv').value.trim();
  const top=document.getElementById('cfg-topic').value.trim();
  try{
    const r=await fetch('/api/config',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({daily_goal_ml:g,ntfy_server_url:srv,ntfy_topic:top})
    });
    if(r.ok)alert('Settings saved successfully!');
    else{
      const d=await r.json();
      alert('Error: '+(d.msg||'Failed'));
    }
  }catch(e){alert('Failed to save settings');}
}
poll();
checkWifiStatus();
scanWifi();
setInterval(poll,2000);
setInterval(checkWifiStatus,5000);
</script>
</body>
</html>
)rawliteral";
#else
static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart Cup Stand</title>
<style>
:root{--bg:#121212;--card:#1e1e1e;--border:#333;--text:#e0e0e0;--accent:#3b82f6;--green:#10b981;--red:#ef4444;--yellow:#f59e0b}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;padding:12px;max-width:480px;margin:0 auto;line-height:1.4}
header{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid var(--border);margin-bottom:12px}
h1{font-size:1.15rem;font-weight:600}
.badge{padding:3px 8px;border-radius:12px;font-size:0.75rem;font-weight:600}
.badge-green{background:#064e3b;color:#6ee7b7}
.badge-yellow{background:#78350f;color:#fcd34d}
.badge-red{background:#7f1d1d;color:#fca5a5}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:14px;margin-bottom:12px}
.card h2{font-size:0.95rem;margin-bottom:10px;display:flex;justify-content:space-between;align-items:center}
.btn{background:var(--accent);color:#fff;border:none;border-radius:6px;padding:7px 12px;font-size:0.85rem;cursor:pointer;font-weight:500}
.btn:hover{opacity:0.9}
.btn-sm{padding:4px 8px;font-size:0.75rem}
.btn-danger{background:var(--red)}
.btn-secondary{background:#374151}
.stat-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:10px}
.stat-box{background:#262626;padding:8px 10px;border-radius:6px}
.stat-val{font-size:1.1rem;font-weight:700;color:#fff}
.stat-lbl{font-size:0.7rem;color:#9ca3af;text-transform:uppercase}
.progress-bg{background:#374151;border-radius:8px;height:12px;overflow:hidden;margin:8px 0}
.progress-fill{background:var(--green);height:100%;width:0%;transition:width 0.3s}
.btn-group{display:flex;gap:6px;margin-top:10px;flex-wrap:wrap}
.net-item{display:flex;justify-content:space-between;align-items:center;padding:8px;background:#262626;border-radius:6px;margin-bottom:6px;font-size:0.85rem}
.net-info{display:flex;align-items:center;gap:6px}
.rssi-bars{font-family:monospace;font-size:0.75rem;color:var(--accent)}
.form-group{margin-bottom:8px}
.form-group label{display:block;font-size:0.75rem;color:#9ca3af;margin-bottom:3px}
.form-group input{width:100%;padding:7px 10px;background:#262626;border:1px solid var(--border);border-radius:6px;color:#fff;font-size:0.85rem}
.banner{background:#7f1d1d;color:#fecaca;padding:10px;border-radius:8px;margin-bottom:12px;font-size:0.8rem;border:1px solid #b91c1c}
</style>
</head>
<body>
<header>
  <div>
    <h1>Smart Coaster</h1>
    <div style="font-size:0.75rem;color:#9ca3af">smart-coaster.local</div>
  </div>
  <div style="text-align:right">
    <span id="b-wifi" class="badge badge-yellow">AP Setup Mode</span>
    <div style="margin-top:4px"><span id="b-srv" class="badge badge-yellow" style="font-size:0.7rem">ntfy.sh: UNKNOWN</span></div>
  </div>
</header>

<div id="rollback-banner" class="banner" style="display:none"></div>

<div class="card">
  <h2><span>Wi-Fi Networks</span>
    <div>
      <button class="btn btn-sm" onclick="scanWifi()">Rescan</button>
      <button class="btn btn-sm btn-danger" onclick="forgetWifi()">Forget</button>
    </div>
  </h2>
  <div id="net-list" style="margin-top:8px"><div style="color:#9ca3af;font-size:0.8rem">Scanning networks...</div></div>
</div>

<div class="card">
  <h2><span>Live Coaster</span><span id="cup-badge" class="badge badge-green">EMPTY</span></h2>
  <div class="stat-grid">
    <div class="stat-box"><div class="stat-lbl">Weight</div><div class="stat-val"><span id="w-net">0.0</span> <span style="font-size:0.8rem">g</span></div></div>
    <div class="stat-box"><div class="stat-lbl">Reminder In</div><div class="stat-val"><span id="countdown">--</span><span style="font-size:0.8rem">s</span></div></div>
  </div>
  <div style="display:flex;justify-content:space-between;font-size:0.8rem;color:#9ca3af">
    <span>Intake Progress</span>
    <span><span id="consumed">0</span> / <span id="goal">2000</span> ml (<span id="pct">0</span>%)</span>
  </div>
  <div class="progress-bg"><div id="progress-bar" class="progress-fill"></div></div>
  <div class="btn-group">
    <button class="btn btn-secondary" onclick="action('tare')">Tare</button>
    <button class="btn btn-secondary" onclick="action('reset')">Reset Intake</button>
    <button id="btn-demo" class="btn btn-secondary" onclick="toggleDemo()">Demo Mode: OFF</button>
  </div>
</div>

<div class="card">
  <h2><span>Notifications & Goal</span></h2>
  <div class="form-group">
    <label>Daily Goal (ml)</label>
    <input type="number" id="cfg-goal" min="100" max="10000" step="50" value="2000">
  </div>
  <div class="form-group">
    <label>ntfy.sh Server URL</label>
    <input type="text" id="cfg-srv" value="http://ntfy.sh">
  </div>
  <div class="form-group">
    <label>ntfy.sh Topic</label>
    <input type="text" id="cfg-topic" value="smart-cup-stand">
  </div>
  <div class="btn-group">
    <button class="btn" onclick="saveConfig()">Save Settings</button>
    <button class="btn btn-secondary" onclick="action('test_push')">Send Test Alert</button>
  </div>
</div>

<script>
let isDemo = false;
async function poll(){
  try{
    const r=await fetch('/api/status');
    if(r.ok){
      const d=await r.json();
      document.getElementById('w-net').textContent=d.net_weight_g.toFixed(1);
      document.getElementById('cup-badge').textContent=d.cup_state;
      document.getElementById('cup-badge').className='badge '+(d.cup_state==='EMPTY'?'badge-yellow':'badge-green');
      document.getElementById('countdown').textContent=d.countdown_s;
      document.getElementById('consumed').textContent=d.consumed_ml.toFixed(0);
      document.getElementById('goal').textContent=d.daily_goal_ml.toFixed(0);
      document.getElementById('pct').textContent=d.progress_pct.toFixed(0);
      document.getElementById('progress-bar').style.width=Math.min(100,d.progress_pct)+'%';
      isDemo=d.demo_mode;
      document.getElementById('btn-demo').textContent='Demo Mode: '+(isDemo?'ON':'OFF');
      const wb=document.getElementById('b-wifi');
      if(d.wifi_state==='CONNECTED'){
        wb.textContent='Connected ('+d.ssid+')';
        wb.className='badge badge-green';
      }else if(d.ap_active){
        wb.textContent='AP Setup Mode';
        wb.className='badge badge-yellow';
      }else{
        wb.textContent=d.wifi_state;
        wb.className='badge badge-red';
      }
      const sb=document.getElementById('b-srv');
      sb.textContent='ntfy.sh: '+d.server_status;
      sb.className='badge '+(d.server_status==='REACHABLE'?'badge-green':(d.server_status==='DEGRADED'?'badge-yellow':'badge-red'));
    }
  }catch(e){}
}
async function checkWifiStatus(){
  try{
    const r=await fetch('/api/wifi/status');
    if(r.ok){
      const d=await r.json();
      const rb=document.getElementById('rollback-banner');
      if(d.rollback_occurred){
        rb.style.display='block';
        rb.textContent='⚠️ Connection failed to "'+d.failed_ssid+'" ('+d.last_error+'). Restored previous connection.';
      }else{
        rb.style.display='none';
      }
    }
  }catch(e){}
}
async function scanWifi(){
  const nl=document.getElementById('net-list');
  nl.innerHTML='<div style="color:#9ca3af;font-size:0.8rem">Scanning for 2.4 GHz networks...</div>';
  try{
    await fetch('/api/wifi/scan',{method:'POST'});
    for(let i=0;i<10;i++){
      await new Promise(r=>setTimeout(r,800));
      const r=await fetch('/api/wifi/scan');
      if(r.ok){
        const d=await r.json();
        if(!d.scanning){
          renderNetworks(d.networks);
          return;
        }
      }
    }
  }catch(e){nl.innerHTML='<div style="color:#ef4444;font-size:0.8rem">Scan failed</div>';}
}
function renderNetworks(nets){
  const nl=document.getElementById('net-list');
  if(!nets||nets.length===0){
    nl.innerHTML='<div style="color:#9ca3af;font-size:0.8rem">No networks discovered</div>';
    return;
  }
  let h='';
  nets.forEach(n=>{
    const sig=n.rssi>-60?'▂▄▆█':(n.rssi>-70?'▂▄▆_':(n.rssi>-80?'▂▄__':(n.rssi>-90?'▂___':'____')));
    const lock=n.open?'🔓':'🔒';
    const s=n.ssid.replace(/"/g,'&quot;');
    h+='<div class="net-item"><div class="net-info"><span>'+lock+'</span><strong>'+s+'</strong><span class="rssi-bars">'+sig+' ('+n.rssi+'dBm)</span></div>';
    h+='<button class="btn btn-sm '+(n.open?'btn-secondary':'')+'" onclick="connectTo(\''+s+'\','+n.open+')">Connect</button></div>';
  });
  nl.innerHTML=h;
}
async function connectTo(ssid,isOpen){
  let pass='';
  if(!isOpen){
    pass=prompt('Enter Wi-Fi password for "'+ssid+'":');
    if(pass===null)return;
  }
  try{
    const r=await fetch('/api/wifi/connect',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({ssid:ssid,password:pass})
    });
    const d=await r.json();
    if(r.ok){
      alert('Connecting to "'+ssid+'"... Coaster will verify connection.');
      setTimeout(checkWifiStatus,4000);
    }else{
      alert('Error: '+(d.msg||'Failed to connect'));
    }
  }catch(e){alert('Request failed');}
}
async function forgetWifi(){
  if(!confirm('Forget stored Wi-Fi credentials and switch to AP Setup Mode?'))return;
  try{
    await fetch('/api/wifi/forget',{method:'POST'});
    alert('Wi-Fi credentials cleared. In AP Setup Mode.');
    poll();
  }catch(e){}
}
async function action(act){
  try{
    await fetch('/api/action',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({action:act})
    });
    poll();
  }catch(e){}
}
function toggleDemo(){
  action(isDemo?'demo_off':'demo_on');
}
async function saveConfig(){
  const g=parseFloat(document.getElementById('cfg-goal').value);
  const srv=document.getElementById('cfg-srv').value.trim();
  const top=document.getElementById('cfg-topic').value.trim();
  try{
    const r=await fetch('/api/config',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({daily_goal_ml:g,ntfy_server_url:srv,ntfy_topic:top})
    });
    if(r.ok)alert('Settings saved successfully!');
    else{
      const d=await r.json();
      alert('Error: '+(d.msg||'Failed'));
    }
  }catch(e){alert('Failed to save settings');}
}
poll();
checkWifiStatus();
scanWifi();
setInterval(poll,2000);
setInterval(checkWifiStatus,5000);
</script>
</body>
</html>
)rawliteral";
#endif

// Helper functions for lightweight bounded parsing and escaping
static void escapeJsonString(const char* in, char* out, size_t max_out) {
    if (!in || !out || max_out == 0) return;
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 2 < max_out; ++i) {
        char c = in[i];
        if (c == '"') {
            if (o + 2 < max_out) { out[o++] = '\\'; out[o++] = '"'; }
        } else if (c == '\\') {
            if (o + 2 < max_out) { out[o++] = '\\'; out[o++] = '\\'; }
        } else if (c == '\r') {
            if (o + 2 < max_out) { out[o++] = '\\'; out[o++] = 'r'; }
        } else if (c == '\n') {
            if (o + 2 < max_out) { out[o++] = '\\'; out[o++] = 'n'; }
        } else if (c == '\t') {
            if (o + 2 < max_out) { out[o++] = '\\'; out[o++] = 't'; }
        } else if (static_cast<unsigned char>(c) >= 32) {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

static bool parseJsonString(const char* json, const char* key, char* out, size_t max_out, bool* out_overflow = nullptr) {
    if (!json || !key || !out || max_out == 0) return false;
    out[0] = '\0';
    if (out_overflow) *out_overflow = false;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return false;
    p++;
    size_t o = 0;
    bool overflow = false;
    while (*p != '\0' && *p != '"') {
        char c;
        if (*p == '\\' && p[1] != '\0') {
            p++;
            if (*p == '"') c = '"';
            else if (*p == '\\') c = '\\';
            else if (*p == 'n') c = '\n';
            else if (*p == 'r') c = '\r';
            else if (*p == 't') c = '\t';
            else c = *p;
            p++;
        } else {
            c = *p++;
        }
        if (o + 1 < max_out) {
            out[o++] = c;
        } else {
            overflow = true;
        }
    }
    out[o] = '\0';
    if (out_overflow) *out_overflow = overflow;
    return true;
}

static bool parseJsonFloat(const char* json, const char* key, float* out_val) {
    if (!json || !key || !out_val) return false;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    char* endptr = nullptr;
    float val = strtof(p, &endptr);
    if (endptr == p) return false;
    *out_val = val;
    return true;
}

static bool parseJsonBool(const char* json, const char* key, bool* out_val) {
    if (!json || !key || !out_val) return false;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (strncmp(p, "true", 4) == 0) {
        *out_val = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out_val = false;
        return true;
    }
    return false;
}

static bool parseUrlEncoded(const char* query, const char* key, char* out, size_t max_out, bool* out_overflow = nullptr) {
    if (!query || !key || !out || max_out == 0) return false;
    out[0] = '\0';
    if (out_overflow) *out_overflow = false;
    size_t klen = strlen(key);
    const char* p = query;
    while (*p != '\0') {
        if ((p == query || *(p - 1) == '&') && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            size_t o = 0;
            bool overflow = false;
            while (*p != '\0' && *p != '&') {
                char c = *p;
                if (*p == '+') {
                    c = ' ';
                    p++;
                } else if (*p == '%' && p[1] != '\0' && p[2] != '\0') {
                    auto hexVal = [](char h) -> int {
                        if (h >= '0' && h <= '9') return h - '0';
                        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                        return -1;
                    };
                    int h1 = hexVal(p[1]);
                    int h2 = hexVal(p[2]);
                    if (h1 >= 0 && h2 >= 0) {
                        c = static_cast<char>((h1 << 4) | h2);
                        p += 3;
                    } else {
                        c = *p++;
                    }
                } else {
                    p++;
                }
                if (o + 1 < max_out) {
                    out[o++] = c;
                } else {
                    overflow = true;
                }
            }
            out[o] = '\0';
            if (out_overflow) *out_overflow = overflow;
            return true;
        }
        p++;
    }
    return false;
}

#ifdef ARDUINO
WifiPortal::WifiPortal(CoasterDomain& domain, NetworkPush& network, LedRing& led_ring)
    : domain_(domain),
      network_(network),
      led_ring_(led_ring),
      server_(WIFI_WEB_PORT),
      dns_started_(false),
      mdns_started_(false),
      last_connected_ip_(0) {}
#else
WifiPortal::WifiPortal(CoasterDomain& domain, NetworkPush& network, LedRing& led_ring)
    : domain_(domain),
      network_(network),
      led_ring_(led_ring) {}
#endif

void WifiPortal::begin() {
#ifdef ARDUINO
    auto sendCors = [this]() {
        server_.sendHeader("Access-Control-Allow-Origin", "*");
        server_.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server_.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    };

    auto captiveRedirect = [this, sendCors]() {
        sendCors();
        server_.sendHeader("Location", "http://192.168.4.1/", true);
        server_.send(302, "text/plain", "");
    };

    server_.on("/", HTTP_GET, [this, sendCors]() {
        sendCors();
        server_.send_P(200, "text/html", INDEX_HTML);
    });

    server_.on("/generate_204", HTTP_GET, captiveRedirect);
    server_.on("/hotspot-detect.html", HTTP_GET, captiveRedirect);
    server_.on("/connecttest.txt", HTTP_GET, captiveRedirect);
    server_.on("/ncsi.txt", HTTP_GET, captiveRedirect);
    server_.on("/redirect", HTTP_GET, captiveRedirect);

    auto handleApiRoute = [this, sendCors]() {
        sendCors();
        const char* method = (server_.method() == HTTP_GET) ? "GET" :
                             ((server_.method() == HTTP_POST) ? "POST" : "OPTIONS");
        String uri = server_.uri();
        String body = server_.arg("plain");
        if (body.length() == 0 && server_.args() > 0) {
            for (int i = 0; i < server_.args(); i++) {
                if (i > 0) body += "&";
                body += server_.argName(i) + "=" + server_.arg(i);
            }
        }
        char out_buf[2048];
        const char* content_type = "application/json";
        int code = handleRequest(method, uri.c_str(), body.c_str(), out_buf, sizeof(out_buf), millis(), &content_type);
        server_.send(code, content_type, out_buf);
    };

    server_.on("/api/status", HTTP_GET, handleApiRoute);
    server_.on("/api/wifi/status", HTTP_GET, handleApiRoute);
    server_.on("/api/wifi/scan", HTTP_ANY, handleApiRoute);
    server_.on("/api/wifi/connect", HTTP_POST, handleApiRoute);
    server_.on("/api/wifi/ap", HTTP_POST, handleApiRoute);
    server_.on("/api/wifi/forget", HTTP_POST, handleApiRoute);
    server_.on("/api/config", HTTP_POST, handleApiRoute);
    server_.on("/api/action", HTTP_POST, handleApiRoute);

    server_.onNotFound([this, sendCors, captiveRedirect, handleApiRoute]() {
        if (server_.method() == HTTP_OPTIONS) {
            sendCors();
            server_.send(204);
            return;
        }
        if (network_.isApActive()) {
            captiveRedirect();
        } else {
            sendCors();
            server_.send(404, "application/json", "{\"status\":\"ERROR\",\"msg\":\"Not Found\"}");
        }
    });

    server_.begin();
#endif
}

void WifiPortal::update(uint32_t now_ms) {
    (void)now_ms;
#ifdef ARDUINO
    // 1. DNSServer management
    bool ap_active = network_.isApActive();
    if (ap_active && !dns_started_) {
        dns_started_ = dns_server_.start(WIFI_DNS_PORT, "*", WiFi.softAPIP());
    } else if (!ap_active && dns_started_) {
        dns_server_.stop();
        dns_started_ = false;
    }
    if (dns_started_) {
        dns_server_.processNextRequest();
    }

    // 2. ESPmDNS management
    bool connected = network_.isConnected();
    uint32_t current_ip = static_cast<uint32_t>(WiFi.localIP());
    if (connected && (!mdns_started_ || current_ip != last_connected_ip_)) {
        if (mdns_started_) {
            MDNS.end();
        }
        if (MDNS.begin(WIFI_MDNS_HOSTNAME)) {
            MDNS.addService("http", "tcp", WIFI_WEB_PORT);
            mdns_started_ = true;
            last_connected_ip_ = current_ip;
        }
    } else if (!connected && mdns_started_) {
        MDNS.end();
        mdns_started_ = false;
        last_connected_ip_ = 0;
    }

    // 3. WebServer client processing
    server_.handleClient();
#endif
}

const char* WifiPortal::getIndexHtml() {
    return INDEX_HTML;
}

int WifiPortal::handleRequest(const char* method, const char* path, const char* body,
                             char* out_buf, size_t max_out_len, uint32_t now_ms,
                             const char** out_content_type) {
    if (!method || !path || !out_buf || max_out_len == 0) return 400;

    if (strcmp(method, "OPTIONS") == 0) {
        if (out_content_type) *out_content_type = "text/plain";
        out_buf[0] = '\0';
        return 200;
    }

    bool is_get = (strcmp(method, "GET") == 0);
    bool is_post = (strcmp(method, "POST") == 0);

    // 1. Root route: GET /
    if (strcmp(path, "/") == 0) {
        if (!is_get) return 405;
        if (out_content_type) *out_content_type = "text/html";
        strncpy(out_buf, INDEX_HTML, max_out_len - 1);
        out_buf[max_out_len - 1] = '\0';
        return 200;
    }

    // Captive Portal Probe Redirections
    if (strcmp(path, "/generate_204") == 0 ||
        strcmp(path, "/hotspot-detect.html") == 0 ||
        strcmp(path, "/connecttest.txt") == 0 ||
        strcmp(path, "/ncsi.txt") == 0 ||
        strcmp(path, "/redirect") == 0) {
        if (!is_get) return 405;
        if (out_content_type) *out_content_type = "text/html";
        snprintf(out_buf, max_out_len, "<meta http-equiv=\"refresh\" content=\"0; url=http://192.168.4.1/\">");
        return 302;
    }

    // All /api/* routes default to application/json
    if (out_content_type) *out_content_type = "application/json";

    // 2. GET /api/status
    if (strcmp(path, "/api/status") == 0) {
        if (!is_get) return 405;
        const CoasterStatus& st = domain_.get_status();
        char ip_buf[20];
        network_.getIpString(ip_buf, sizeof(ip_buf));
        snprintf(out_buf, max_out_len,
            "{\"cup_state\":\"%s\",\"alert_state\":\"%s\",\"net_weight_g\":%.1f,\"consumed_ml\":%.1f,"
            "\"daily_goal_ml\":%.1f,\"progress_pct\":%.1f,\"countdown_s\":%lu,\"demo_mode\":%s,"
            "\"led_mode\":\"%s\",\"wifi_state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%ld,"
            "\"mdns\":\"%s.local\",\"ap_active\":%s,\"server_status\":\"%s\",\"queue_size\":%lu}",
            CoasterDomain::cup_state_to_string(st.cup_state),
            CoasterDomain::alert_state_to_string(st.alert_state),
            st.net_weight_g,
            st.consumed_today_ml,
            st.daily_goal_ml,
            st.progress_pct,
            (unsigned long)st.countdown_s,
            st.demo_mode ? "true" : "false",
            led_ring_.getModeString(),
            network_.getWifiStateStr(),
            network_.getSsid(),
            ip_buf,
            (long)network_.getRssi(),
            WIFI_MDNS_HOSTNAME,
            network_.isApActive() ? "true" : "false",
            network_.getServerReachabilityStr(),
            (unsigned long)network_.getQueueSize());
        return 200;
    }

    // 3. GET /api/wifi/status
    if (strcmp(path, "/api/wifi/status") == 0) {
        if (!is_get) return 405;
        char ip_buf[20];
        char mac_buf[20];
        network_.getIpString(ip_buf, sizeof(ip_buf));
        network_.getMacString(mac_buf, sizeof(mac_buf));
        snprintf(out_buf, max_out_len,
            "{\"wifi_state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%ld,\"mac\":\"%s\","
            "\"mdns_host\":\"%s\",\"ap_active\":%s,\"ap_ssid\":\"%s\",\"ap_ip\":\"192.168.4.1\","
            "\"ap_clients\":%u,\"switching\":%s,\"rollback_occurred\":%s,\"failed_ssid\":\"%s\","
            "\"last_error\":\"%s\",\"server_status\":\"%s\",\"post_ok\":%lu,\"post_fail\":%lu,"
            "\"post_retries_exhausted\":%lu}",
            network_.getWifiStateStr(),
            network_.getSsid(),
            ip_buf,
            (long)network_.getRssi(),
            mac_buf,
            WIFI_MDNS_HOSTNAME,
            network_.isApActive() ? "true" : "false",
            WIFI_AP_SSID,
            (unsigned int)network_.getApClientCount(),
            network_.isSwitching() ? "true" : "false",
            network_.hasRollbackOccurred() ? "true" : "false",
            network_.getFailedSsid(),
            network_.getLastErrorStr(),
            network_.getServerReachabilityStr(),
            (unsigned long)network_.getSuccessfulPostCount(),
            (unsigned long)network_.getFailedPostCount(),
            (unsigned long)network_.getExhaustedRetryCount());
        return 200;
    }

    // 4. GET & POST /api/wifi/scan
    if (strncmp(path, "/api/wifi/scan", 14) == 0 && (path[14] == '\0' || path[14] == '?')) {
        if (!is_get && !is_post) return 405;
        if (is_post || strstr(path, "refresh=1") != nullptr) {
            network_.startAsyncScan(now_ms);
        }
        bool scanning = network_.isScanning();
        size_t count = network_.getScanCount();
        const WifiScanEntry* entries = network_.getScanResults();

        int written = snprintf(out_buf, max_out_len, "{\"scanning\":%s,\"count\":%u,\"networks\":[",
                               scanning ? "true" : "false", (unsigned int)count);
        if (written > 0 && (size_t)written < max_out_len) {
            size_t offset = (size_t)written;
            for (size_t i = 0; i < count; ++i) {
                char escaped_ssid[68];
                escapeJsonString(entries[i].ssid, escaped_ssid, sizeof(escaped_ssid));
                char entry_buf[128];
                int entry_len = snprintf(entry_buf, sizeof(entry_buf),
                    "%s{\"ssid\":\"%s\",\"rssi\":%ld,\"channel\":%u,\"open\":%s}",
                    (i > 0 ? "," : ""),
                    escaped_ssid,
                    (long)entries[i].rssi,
                    (unsigned int)entries[i].channel,
                    entries[i].auth_open ? "true" : "false");
                if (entry_len > 0 && offset + (size_t)entry_len + 3 < max_out_len) {
                    memcpy(out_buf + offset, entry_buf, entry_len);
                    offset += entry_len;
                } else {
                    break;
                }
            }
            if (offset + 2 < max_out_len) {
                out_buf[offset++] = ']';
                out_buf[offset++] = '}';
                out_buf[offset] = '\0';
            }
        }
        return 200;
    }

    // 5. POST /api/wifi/connect
    if (strcmp(path, "/api/wifi/connect") == 0) {
        if (!is_post) return 405;
        char ssid[33] = {0};
        char pass[65] = {0};
        bool parsed_ok = false;
        bool s_overflow = false;
        bool p_overflow = false;

        if (body && body[0] != '\0') {
            if (strchr(body, '{')) {
                parsed_ok = parseJsonString(body, "ssid", ssid, sizeof(ssid), &s_overflow);
                parseJsonString(body, "password", pass, sizeof(pass), &p_overflow);
            } else {
                parsed_ok = parseUrlEncoded(body, "ssid", ssid, sizeof(ssid), &s_overflow);
                parseUrlEncoded(body, "password", pass, sizeof(pass), &p_overflow);
            }
        }

        size_t s_len = strlen(ssid);
        size_t p_len = strlen(pass);
        if (!parsed_ok || s_overflow || p_overflow || s_len < 1 || s_len > 32 || p_len > 64) {
            snprintf(out_buf, max_out_len, "{\"status\":\"ERROR\",\"msg\":\"Invalid SSID or password\"}");
            return 400;
        }

        network_.requestWifiSwitch(ssid, pass, now_ms);
        char escaped_ssid[68];
        escapeJsonString(ssid, escaped_ssid, sizeof(escaped_ssid));
        snprintf(out_buf, max_out_len, "{\"status\":\"CONNECTING\",\"ssid\":\"%s\"}", escaped_ssid);
        return 200;
    }

    // 6. POST /api/wifi/ap
    if (strcmp(path, "/api/wifi/ap") == 0) {
        if (!is_post) return 405;
        network_.startApFallback(now_ms);
        snprintf(out_buf, max_out_len,
            "{\"status\":\"AP_FALLBACK\",\"ap_ssid\":\"%s\",\"ap_ip\":\"192.168.4.1\"}",
            WIFI_AP_SSID);
        return 200;
    }

    // 7. POST /api/wifi/forget
    if (strcmp(path, "/api/wifi/forget") == 0) {
        if (!is_post) return 405;
        network_.forgetCredentials(now_ms);
        snprintf(out_buf, max_out_len, "{\"status\":\"FORGOTTEN\",\"ap_active\":true}");
        return 200;
    }

    // 8. POST /api/config
    if (strcmp(path, "/api/config") == 0) {
        if (!is_post) return 405;
        float new_goal = 0.0f;
        if (parseJsonFloat(body, "daily_goal_ml", &new_goal)) {
            if (new_goal < 100.0f || new_goal > 10000.0f) {
                snprintf(out_buf, max_out_len,
                    "{\"status\":\"ERROR\",\"msg\":\"daily_goal_ml must be between 100 and 10000\"}");
                return 400;
            }
            domain_.set_daily_goal(new_goal);
        }

        bool new_demo = false;
        if (parseJsonBool(body, "demo_mode", &new_demo)) {
            domain_.set_demo_mode(new_demo);
            led_ring_.setDemoMode(new_demo);
        }

        char new_topic[64] = {0};
        if (parseJsonString(body, "ntfy_topic", new_topic, sizeof(new_topic)) && new_topic[0] != '\0') {
            network_.setTopic(new_topic);
        }

        char new_srv[64] = {0};
        if (parseJsonString(body, "ntfy_server_url", new_srv, sizeof(new_srv)) && new_srv[0] != '\0') {
            network_.setServerUrl(new_srv);
        }

        network_.saveConfigToNvs();
        const CoasterStatus& st = domain_.get_status();
        snprintf(out_buf, max_out_len,
            "{\"status\":\"OK\",\"daily_goal_ml\":%.1f,\"demo_mode\":%s,\"ntfy_topic\":\"%s\",\"ntfy_server_url\":\"%s\"}",
            st.daily_goal_ml,
            st.demo_mode ? "true" : "false",
            network_.getTopic(),
            network_.getServerUrl());
        return 200;
    }

    // 9. POST /api/action
    if (strcmp(path, "/api/action") == 0) {
        if (!is_post) return 405;
        char action[32] = {0};
        if (body && body[0] != '\0') {
            if (strchr(body, '{')) {
                parseJsonString(body, "action", action, sizeof(action));
            } else {
                parseUrlEncoded(body, "action", action, sizeof(action));
            }
        }

        if (strcmp(action, "tare") == 0) {
            domain_.manual_tare();
            snprintf(out_buf, max_out_len, "{\"status\":\"OK\",\"action\":\"tare\"}");
            return 200;
        }
        if (strcmp(action, "reset") == 0) {
            domain_.reset();
            snprintf(out_buf, max_out_len, "{\"status\":\"OK\",\"action\":\"reset\"}");
            return 200;
        }
        if (strcmp(action, "test_push") == 0) {
            network_.queueNotification("Smart Coaster Test", "Push notifications are working!", 3, "bell");
            snprintf(out_buf, max_out_len, "{\"status\":\"OK\",\"action\":\"test_push\",\"queued\":true}");
            return 200;
        }
        if (strcmp(action, "demo_on") == 0) {
            domain_.set_demo_mode(true);
            led_ring_.setDemoMode(true);
            snprintf(out_buf, max_out_len, "{\"status\":\"OK\",\"action\":\"demo_on\",\"demo\":true}");
            return 200;
        }
        if (strcmp(action, "demo_off") == 0) {
            domain_.set_demo_mode(false);
            led_ring_.setDemoMode(false);
            snprintf(out_buf, max_out_len, "{\"status\":\"OK\",\"action\":\"demo_off\",\"demo\":false}");
            return 200;
        }

        snprintf(out_buf, max_out_len, "{\"status\":\"ERROR\",\"msg\":\"Unknown action\"}");
        return 400;
    }

    snprintf(out_buf, max_out_len, "{\"status\":\"ERROR\",\"msg\":\"Not Found\"}");
    return 404;
}
