#include "web.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>

#ifdef LILYGO_T_ETH_ELITE
extern bool    isEthConnected();
extern String  getEthLocalIP();
extern String  getEthMAC();
extern String  getEthGateway();
extern int     getEthLinkSpeed();
extern bool    isEthFullDuplex();
extern uint8_t getMeshChannel();
extern void    setMeshChannel(uint8_t);
#endif

// --- Packet ring buffer ---
#ifdef BOARD_HAS_PSRAM
  #define LOG_SIZE 200
#else
  #define LOG_SIZE 10
#endif

struct PacketLog {
  uint8_t       type;
  char          src[18];
  uint8_t       appId;
  char          payload[65];
  unsigned long ts;
};

#ifdef BOARD_HAS_PSRAM
  static EXT_RAM_BSS_ATTR PacketLog _log[LOG_SIZE];
#else
  static PacketLog _log[LOG_SIZE];
#endif
static int       _logHead  = 0;
static int       _logCount = 0;

void logPacket(uint8_t type, uint8_t* srcMac, uint8_t appId, const uint8_t* payload, uint8_t payloadLen) {
  PacketLog& p = _log[_logHead];
  p.type  = type;
  p.appId = appId;
  p.ts    = millis();
  snprintf(p.src, sizeof(p.src), "%02X:%02X:%02X:%02X:%02X:%02X",
           srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
  uint8_t len = payloadLen < 64 ? payloadLen : 64;
  memcpy(p.payload, payload, len);
  p.payload[len] = '\0';
  _logHead = (_logHead + 1) % LOG_SIZE;
  if (_logCount < LOG_SIZE) _logCount++;
}

// --- Serial console log buffer (internal RAM, NOT PSRAM) ---
#define SERIAL_LOG_LINES    40
#define SERIAL_LOG_LINE_LEN 120
static char _serialLog[SERIAL_LOG_LINES][SERIAL_LOG_LINE_LEN];
static int  _serialLogHead  = 0;
static int  _serialLogCount = 0;

// --- Mutex protecting meshNodes[] and _log[] across tasks/cores ---
static SemaphoreHandle_t _dataMutex = nullptr;
void lockMeshData()   { if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY); }
void unlockMeshData() { if (_dataMutex) xSemaphoreGive(_dataMutex); }

void addSerialLog(const char* msg) {
  strncpy(_serialLog[_serialLogHead], msg, SERIAL_LOG_LINE_LEN - 1);
  _serialLog[_serialLogHead][SERIAL_LOG_LINE_LEN - 1] = '\0';
  _serialLogHead = (_serialLogHead + 1) % SERIAL_LOG_LINES;
  if (_serialLogCount < SERIAL_LOG_LINES) _serialLogCount++;
}

// --- Dashboard HTML (stored in flash) ---
static const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html data-theme="dark">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>UniversalMesh</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    :root{
      --bg:#f6f8fa;--container-bg:#ffffff;--text:#24292f;--muted:#57606a;
      --border:#d0d7de;--card-bg:#ffffff;--row-border:#d0d7de;
      --sub:#8c959f;--empty:#8c959f;--tag-bg:#0969da;--tag:#fff;
      --btn-bg:#f6f8fa;--btn-border:#d0d7de;--btn-color:#57606a;
      --sel-bg:#fff;--sel-color:#24292f;--sel-border:#d0d7de;
      --th:#57606a;--th-border:#d0d7de;--td:#24292f;--td-border:#eaeef2;
      --dot-green:#1a7f37;--h1:#0969da;--h2:#57606a;
    }
    [data-theme=dark]{
      --bg:#0d1117;--container-bg:#161b22;--text:#c9d1d9;--muted:#8b949e;
      --border:#30363d;--card-bg:#161b22;--row-border:#21262d;
      --sub:#484f58;--empty:#484f58;--tag-bg:#1f6feb;--tag:#e6edf3;
      --btn-bg:#21262d;--btn-border:#30363d;--btn-color:#8b949e;
      --sel-bg:#21262d;--sel-color:#e6edf3;--sel-border:#30363d;
      --th:#8b949e;--th-border:#30363d;--td:#e6edf3;--td-border:#21262d;
      --dot-green:#3fb950;--h1:#58a6ff;--h2:#8b949e;
    }
    body{font-family:monospace;background:var(--bg);color:var(--text);transition:background .2s,color .2s}
    .container{max-width:1000px;margin:20px auto;padding:0 16px}
    h1{color:var(--h1);margin-bottom:16px;font-size:1.3em;display:flex;align-items:center;gap:10px}
    h2{color:var(--h2);font-size:0.78em;text-transform:uppercase;letter-spacing:.1em;margin-bottom:10px}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:15px;margin-bottom:15px}
    .card{background:var(--card-bg);border:1px solid var(--border);border-radius:6px;padding:14px}
    .row{display:flex;justify-content:space-between;align-items:center;padding:5px 0;border-bottom:1px solid var(--row-border);font-size:0.83em}
    .row:last-child{border-bottom:none}
    .lbl{color:var(--muted)}
    .val{color:var(--text);text-align:right}
    table{width:100%;border-collapse:collapse;font-size:0.8em}
    th{color:var(--th);text-align:left;padding:5px 4px;border-bottom:1px solid var(--th-border);font-weight:normal}
    td{padding:5px 4px;border-bottom:1px solid var(--td-border);color:var(--td);word-break:break-all}
    tr:last-child td{border-bottom:none}
    .tag{font-size:0.75em;padding:1px 7px;border-radius:3px;background:var(--tag-bg);color:var(--tag)}
    .dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--dot-green);margin-right:6px}
    .sub{color:var(--sub);font-size:0.78em;margin-top:6px}
    .empty{color:var(--empty);font-size:0.82em;padding:8px 0}
    .theme-btn{margin-left:auto;background:none;border:1px solid var(--border);border-radius:6px;padding:4px 8px;cursor:pointer;font-size:1em;line-height:1;color:var(--text)}
    .btn{font-family:monospace;font-size:0.75em;padding:2px 8px;border-radius:3px;border:1px solid var(--btn-border);background:var(--btn-bg);color:var(--btn-color);cursor:pointer}
    .btn-red{color:#f85149}
    .sel{font-family:monospace;font-size:0.83em;background:var(--sel-bg);color:var(--sel-color);border:1px solid var(--sel-border);border-radius:3px;padding:2px 4px}
    @media screen and (max-width:600px){
      .grid{grid-template-columns:1fr}
    }
    .console-out{font-family:monospace;background:#0d1117;color:#3fb950;height:220px;overflow-y:auto;white-space:pre-wrap;border-radius:4px;padding:10px;border:1px solid #30363d}
  </style>
  <script>
    (function(){
      var t=localStorage.getItem('theme')||'dark';
      document.documentElement.setAttribute('data-theme',t);
    })();
  </script>
</head>
<body>
  <div class="container">
  <h1><span class="dot"></span>UniversalMesh Coordinator<button class="theme-btn" onclick="toggleConsole()" id="console-btn" title="Toggle serial console" style="font-size:0.8em">&gt;_</button><button class="theme-btn" onclick="rebootDevice()" id="reboot-btn" title="Reboot coordinator" style="margin-left:6px;color:#f85149">↺</button><button class="theme-btn" onclick="toggleTheme()" id="theme-btn" title="Toggle dark/light mode" style="margin-left:6px">🌙</button></h1>
  <div class="grid">
    <div class="card">
      <h2>ESP</h2>
      <div class="row"><span class="lbl">Chip</span><span class="val" id="chip">-</span></div>
      <div class="row"><span class="lbl">Cores</span><span class="val" id="cores">-</span></div>
      <div class="row"><span class="lbl">CPU</span><span class="val" id="cpu">-</span></div>
      <div class="row"><span class="lbl">Flash</span><span class="val" id="flash">-</span></div>
      <div class="row"><span class="lbl">Free Heap</span><span class="val" id="heap">-</span></div>
      <div class="row"><span class="lbl">MAC</span><span class="val" id="esp-mac">-</span></div>
      <div class="row"><span class="lbl">Uptime</span><span class="val" id="uptime">-</span></div>
    </div>
    <div class="card" id="wifi-card">
      <h2>WiFi</h2>
      <div class="row"><span class="lbl">SSID</span><span class="val" id="ssid">-</span></div>
      <div class="row"><span class="lbl">IP</span><span class="val" id="ip">-</span></div>
      <div class="row"><span class="lbl">Gateway</span><span class="val" id="gw">-</span></div>
      <div class="row"><span class="lbl">RSSI</span><span class="val" id="rssi">-</span></div>
      <div class="row"><span class="lbl">Channel</span><span class="val" id="ch">-</span></div>
    </div>
    <div class="card" id="eth-card" style="display:none">
      <h2>&#x1F5A7; Ethernet</h2>
      <div class="row"><span class="lbl">Status</span><span class="val" id="eth-status">-</span></div>
      <div class="row"><span class="lbl">IP</span><span class="val" id="eth-ip">-</span></div>
      <div class="row"><span class="lbl">Gateway</span><span class="val" id="eth-gw">-</span></div>
      <div class="row"><span class="lbl">MAC</span><span class="val" id="eth-mac">-</span></div>
      <div class="row"><span class="lbl">Speed</span><span class="val" id="eth-speed">-</span></div>
    </div>
    <div class="card">
      <h2>Mesh Nodes <button id="ping-all-btn" onclick="pingAll()" class="btn" style="float:right">ping all</button></h2>
      <div id="nodes-empty" class="empty">No nodes discovered yet</div>
      <table id="nodes-table" style="display:none">
        <thead><tr><th>MAC</th><th>Last Seen</th></tr></thead>
        <tbody id="nodes-body"></tbody>
      </table>
    </div>
    <div class="card" id="mesh-card" style="display:none">
      <h2>Mesh</h2>
      <div class="row"><span class="lbl">Channel</span>
        <span class="val">
          <select id="mesh-ch-sel" class="sel">
            <option value="1">1</option><option value="2">2</option><option value="3">3</option>
            <option value="4">4</option><option value="5">5</option><option value="6">6</option>
            <option value="7">7</option><option value="8">8</option><option value="9">9</option>
            <option value="10">10</option><option value="11">11</option><option value="12">12</option>
            <option value="13">13</option>
          </select>
          <button onclick="saveMeshCh(false)" class="btn" style="margin-left:6px" id="mesh-ch-btn">save</button>
          <button onclick="saveMeshCh(true)" class="btn btn-red" style="margin-left:4px" id="mesh-ch-reboot-btn">save &amp; reboot</button>
        </span>
      </div>
    </div>
  </div>
  <div class="card">
    <h2>Live Packet Log <span id="log-pageinfo" style="float:right;font-size:0.8em;color:var(--sub)"></span></h2>
    <div id="log-empty" class="empty">No packets yet</div>
    <table id="log-table" style="display:none">
      <thead><tr><th>Type</th><th>From</th><th>App</th><th>Payload</th><th>Age</th></tr></thead>
      <tbody id="log-body"></tbody>
    </table>
    <div style="display:flex;justify-content:space-between;align-items:center;margin-top:8px">
      <div>
        <button id="log-prev" onclick="logPage(-1)" class="btn" style="margin-right:4px">&laquo; prev</button>
        <button id="log-next" onclick="logPage(1)"  class="btn">next &raquo;</button>
      </div>
      <div class="sub" id="tick"></div>
    </div>
  </div>
  <div id="console-panel" style="display:none;margin-top:15px">
    <div class="card">
      <h2>&gt;_ Serial Console</h2>
      <div id="console-out" class="console-out"></div>
    </div>
  </div>
  </div>
  <script>
    async function rebootDevice(){
      const btn=document.getElementById('reboot-btn');
      if(!confirm('Reboot the coordinator?')) return;
      btn.textContent='…';btn.disabled=true;
      try{ await fetch('/api/reboot',{method:'POST'}); }catch(e){}
      btn.textContent='↺';btn.disabled=false;
    }
    function toggleTheme(){
      const isDark=document.documentElement.getAttribute('data-theme')==='dark';
      const next=isDark?'light':'dark';
      document.documentElement.setAttribute('data-theme',next);
      document.getElementById('theme-btn').textContent=isDark?'☀️':'🌙';
      localStorage.setItem('theme',next);
    }
    (function(){
      const t=localStorage.getItem('theme')||'dark';
      document.getElementById('theme-btn').textContent=t==='dark'?'🌙':'☀️';
    })();
    const PAGE_SIZE=10;
    let logPage_=0;
    let logPackets_=[];
    function logPage(dir){
      logPage_=Math.max(0,Math.min(logPage_+dir,Math.ceil(logPackets_.length/PAGE_SIZE)-1));
      renderLog();
    }
    function renderLog(){
      const lb=document.getElementById('log-body');
      lb.innerHTML='';
      if(!logPackets_.length){
        document.getElementById('log-empty').style.display='';
        document.getElementById('log-table').style.display='none';
        document.getElementById('log-pageinfo').textContent='';
        document.getElementById('log-prev').style.visibility='hidden';
        document.getElementById('log-next').style.visibility='hidden';
        return;
      }
      const total=Math.ceil(logPackets_.length/PAGE_SIZE);
      const page=logPackets_.slice(logPage_*PAGE_SIZE,(logPage_+1)*PAGE_SIZE);
      document.getElementById('log-empty').style.display='none';
      document.getElementById('log-table').style.display='';
      document.getElementById('log-pageinfo').textContent=(logPage_+1)+' / '+total;
      document.getElementById('log-prev').style.visibility=logPage_>0?'visible':'hidden';
      document.getElementById('log-next').style.visibility=logPage_<total-1?'visible':'hidden';
      page.forEach(p=>{
        lb.innerHTML+='<tr>'
          +'<td><span class="tag">'+({0x12:'PING',0x13:'PONG',0x15:'DATA'}[p.type]||'0x'+p.type.toString(16).padStart(2,'0'))+'</span></td>'
          +'<td>'+p.src+'</td>'
          +'<td>0x'+p.appId.toString(16).padStart(2,'0')+'</td>'
          +'<td>'+(p.appId===0x05?'[heartbeat]':p.appId===0x00?({0x12:'[discovery ping]',0x13:'[discovery pong]'}[p.type]||'[discovery]'):p.payload)+'</td>'
          +'<td>'+p.age_s+'s</td>'
          +'</tr>';
      });
    }
    function pingAll(){
      const btn=document.getElementById('ping-all-btn');
      btn.textContent='...';btn.disabled=true;
      fetch('/api/discover').then(()=>{
        btn.textContent='sent!';
        setTimeout(()=>{btn.textContent='ping all';btn.disabled=false;},2000);
      }).catch(()=>{btn.textContent='ping all';btn.disabled=false;});
    }
    function fmtUptime(ms){
      let s=Math.floor(ms/1000),m=Math.floor(s/60),h=Math.floor(m/60),d=Math.floor(h/24);
      return d?d+'d '+h%24+'h':h?h+'h '+m%60+'m':m?m+'m '+s%60+'s':s+'s';
    }
    function fmtBytes(b){return b>1048576?(b/1048576).toFixed(1)+' MB':b>1024?(b/1024).toFixed(1)+' KB':b+' B'}
    function set(id,v){document.getElementById(id).textContent=v}
    async function refresh(){
      try{
        const [st,nd,lg]=await Promise.all([
          fetch('/api/status').then(r=>r.json()),
          fetch('/api/nodes').then(r=>r.json()),
          fetch('/api/log').then(r=>r.json())
        ]);
        set('chip',     st.chip);
        set('cores',    st.cores);
        set('cpu',      st.cpu_mhz+' MHz');
        set('flash',    fmtBytes(st.flash_size));
        set('heap',     fmtBytes(st.free_heap));
        set('esp-mac',  st.esp_mac);
        set('uptime',   fmtUptime(st.uptime_ms));
        set('ssid',     st.ssid);
        set('ip',       st.ip);
        set('gw',       st.gateway);
        set('rssi',     st.rssi+' dBm');
        set('ch',       st.channel);

        document.getElementById('wifi-card').style.display = st.wifi_connected ? '' : 'none';

        if(st.eth_present){
          document.getElementById('eth-card').style.display='';
          set('eth-status', st.eth_connected ? '🟢 Connected' : '🔴 Disconnected');
          set('eth-ip',     st.eth_ip);
          set('eth-gw',     st.eth_gateway);
          set('eth-mac',    st.eth_mac);
          set('eth-speed',  st.eth_connected ? st.eth_speed_mbps+'Mbps '+(st.eth_full_duplex?'Full':'Half')+'-Duplex' : '-');
          document.getElementById('mesh-card').style.display='';
          const sel=document.getElementById('mesh-ch-sel');
          if(sel && st.mesh_channel && !meshChDirty) sel.value=st.mesh_channel;
        }

        const nb=document.getElementById('nodes-body');
        nb.innerHTML='';
        if(nd.nodes&&nd.nodes.length){
          document.getElementById('nodes-empty').style.display='none';
          document.getElementById('nodes-table').style.display='';
          const blueDot='<span style="display:inline-block;width:8px;height:8px;border-radius:50%;background:#58a6ff;margin-right:6px"></span>';
          nb.innerHTML='<tr><td>'+blueDot+st.esp_mac+' <span style="font-size:0.75em;color:#58a6ff">[coordinator]</span></td><td>-</td></tr>';
          nd.nodes.filter(n=>n.mac.toUpperCase()!==st.esp_mac.toUpperCase()).forEach(n=>{
            const dot='<span style="display:inline-block;width:8px;height:8px;border-radius:50%;background:'+(n.last_seen_seconds_ago<=120?'#3fb950':'#f85149')+';margin-right:6px"></span>';
            nb.innerHTML+='<tr><td>'+dot+n.mac+'</td><td>'+n.last_seen_seconds_ago+'s ago</td></tr>';
          });
        }else{
          document.getElementById('nodes-empty').style.display='';
          document.getElementById('nodes-table').style.display='none';
        }

        logPackets_=lg.packets?lg.packets.slice().reverse():[];
        renderLog();
        set('tick','last update: '+new Date().toLocaleTimeString());
      }catch(e){}
    }
    let meshChDirty=false;
    document.addEventListener('change',e=>{if(e.target.id==='mesh-ch-sel') meshChDirty=true;});
    async function saveMeshCh(reboot){
      const ch=parseInt(document.getElementById('mesh-ch-sel').value);
      const btn=reboot?document.getElementById('mesh-ch-reboot-btn'):document.getElementById('mesh-ch-btn');
      btn.textContent='...';btn.disabled=true;
      try{
        await fetch('/api/mesh/channel',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({channel:ch,reboot:reboot})});
        btn.textContent=reboot?'rebooting…':'saved!';
        meshChDirty=false;
      }catch(e){btn.textContent='error';}
      if(!reboot) setTimeout(()=>{btn.textContent='save';btn.disabled=false;},2000);
    }
    let _consoleOpen=false,_consoleInterval=null;
    async function refreshConsole(){
      try{
        const d=await fetch('/api/serial').then(r=>r.json());
        const el=document.getElementById('console-out');
        el.textContent=(d.lines||[]).join('\n');
        el.scrollTop=el.scrollHeight;
      }catch(e){}
    }
    function toggleConsole(){
      _consoleOpen=!_consoleOpen;
      document.getElementById('console-panel').style.display=_consoleOpen?'':'none';
      document.getElementById('console-btn').style.opacity=_consoleOpen?'1':'0.5';
      if(_consoleOpen){refreshConsole();_consoleInterval=setInterval(refreshConsole,2000);}
      else{clearInterval(_consoleInterval);_consoleInterval=null;}
    }
    refresh();
    setInterval(refresh,3000);
  </script>
</body>
</html>
)rawliteral";

// --- Endpoints ---
void initWebDashboard(AsyncWebServer& server) {
  _dataMutex = xSemaphoreCreateMutex();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", HTML);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    String json = "{";
    json += "\"chip\":\""       + String(ESP.getChipModel())        + "\",";
    json += "\"cores\":"        + String(ESP.getChipCores())        + ",";
    json += "\"cpu_mhz\":"      + String(ESP.getCpuFreqMHz())       + ",";
    json += "\"flash_size\":"   + String(ESP.getFlashChipSize())    + ",";
    json += "\"free_heap\":"    + String(ESP.getFreeHeap())         + ",";
    json += "\"esp_mac\":\""    + String(WiFi.macAddress())         + "\",";
    json += "\"uptime_ms\":"    + String(millis())                  + ",";
    json += "\"ssid\":\""       + WiFi.SSID()                       + "\",";
    json += "\"ip\":\""         + WiFi.localIP().toString()         + "\",";
    json += "\"gateway\":\""    + WiFi.gatewayIP().toString()       + "\",";
    json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"rssi\":"         + String(WiFi.RSSI())               + ",";
    json += "\"channel\":"      + String(WiFi.channel())            + ",";
#ifdef LILYGO_T_ETH_ELITE
    bool ethOk = isEthConnected();
    json += "\"eth_present\":true,";
    json += "\"eth_connected\":"    + String(ethOk ? "true" : "false")      + ",";
    json += "\"eth_ip\":\""         + (ethOk ? getEthLocalIP() : "")        + "\",";
    json += "\"eth_mac\":\""        + getEthMAC()                           + "\",";
    json += "\"eth_gateway\":\""    + (ethOk ? getEthGateway() : "")        + "\",";
    json += "\"eth_speed_mbps\":"   + String(ethOk ? getEthLinkSpeed() : 0) + ",";
    json += "\"eth_full_duplex\":"  + String(ethOk && isEthFullDuplex() ? "true" : "false") + ",";
    json += "\"mesh_channel\":"     + String(getMeshChannel());
#else
    json += "\"eth_present\":false";
#endif
    json += "}";
    request->send(200, "application/json", json);
  });

#ifdef LILYGO_T_ETH_ELITE
  server.on("/api/mesh/channel", HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
      JsonDocument doc;
      deserializeJson(doc, data, len);
      uint8_t ch = doc["channel"] | 1;
      bool reboot = doc["reboot"] | false;
      setMeshChannel(ch);
      request->send(200, "application/json", "{\"channel\":" + String(ch) + ",\"reboot\":" + String(reboot?"true":"false") + "}");
      if (reboot) { delay(200); ESP.restart(); }
    }
  );
#endif

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"rebooting\":true}");
    delay(200);
    ESP.restart();
  });

  server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    // Snapshot indices under lock only — keep lock time in microseconds, not milliseconds
    lockMeshData();
    int head  = _logHead;
    int count = _logCount;
    unlockMeshData();

    String json = "{\"packets\":[";
    int start = count < LOG_SIZE ? 0 : head;
    for (int i = 0; i < count; i++) {
      const PacketLog& p = _log[(start + i) % LOG_SIZE];
      if (i > 0) json += ",";
      json += "{";
      json += "\"type\":"  + String(p.type)  + ",";
      json += "\"src\":\""  + String(p.src)  + "\",";
      json += "\"appId\":" + String(p.appId) + ",";
      // Escape payload — raw bytes (e.g. 0x01) must not appear unescaped in JSON
      json += "\"payload\":\"";
      for (const char* c = p.payload; *c; c++) {
        uint8_t b = (uint8_t)*c;
        if      (b == '"')  json += "\\\"";
        else if (b == '\\') json += "\\\\";
        else if (b < 0x20)  { char esc[7]; snprintf(esc, sizeof(esc), "\\u%04x", b); json += esc; }
        else                json += *c;
      }
      json += "\",";
      json += "\"age_s\":" + String((millis() - p.ts) / 1000);
      json += "}";
    }
    json += "]}";
    request->send(200, "application/json", json);
  });

  server.on("/api/serial", HTTP_GET, [](AsyncWebServerRequest* request) {
    String json = "{\"lines\":[";
    int start = _serialLogCount < SERIAL_LOG_LINES ? 0 : _serialLogHead;
    int count = _serialLogCount;
    for (int i = 0; i < count; i++) {
      const char* line = _serialLog[(start + i) % SERIAL_LOG_LINES];
      if (i > 0) json += ",";
      json += "\"";
      for (const char* c = line; *c; c++) {
        uint8_t b = (uint8_t)*c;
        if      (b == '"')  json += "\\\"";
        else if (b == '\\') json += "\\\\";
        else if (b < 0x20)  { char esc[7]; snprintf(esc, sizeof(esc), "\\u%04x", b); json += esc; }
        else                json += *c;
      }
      json += "\"";
    }
    json += "]}";
    request->send(200, "application/json", json);
  });
}
