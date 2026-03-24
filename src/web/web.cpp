#include "web.h"
#include <Arduino.h>
#include <WiFi.h>

// --- Packet ring buffer ---
#define LOG_SIZE 10

struct PacketLog {
  uint8_t       type;
  char          src[18];
  uint8_t       appId;
  char          payload[65];
  unsigned long ts;
};

static PacketLog _log[LOG_SIZE];
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

// --- Dashboard HTML (stored in flash) ---
static const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>UniversalMesh</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:monospace;background:#0d1117;color:#c9d1d9;padding:16px}
    h1{color:#58a6ff;margin-bottom:16px;font-size:1.3em;display:flex;align-items:center;gap:10px}
    h2{color:#8b949e;font-size:0.78em;text-transform:uppercase;letter-spacing:.1em;margin-bottom:10px}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px;margin-bottom:12px}
    .card{background:#161b22;border:1px solid #30363d;border-radius:6px;padding:14px}
    .row{display:flex;justify-content:space-between;align-items:center;padding:5px 0;border-bottom:1px solid #21262d;font-size:0.83em}
    .row:last-child{border-bottom:none}
    .lbl{color:#8b949e}
    .val{color:#e6edf3;text-align:right}
    table{width:100%;border-collapse:collapse;font-size:0.8em}
    th{color:#8b949e;text-align:left;padding:5px 4px;border-bottom:1px solid #30363d;font-weight:normal}
    td{padding:5px 4px;border-bottom:1px solid #21262d;color:#e6edf3;word-break:break-all}
    tr:last-child td{border-bottom:none}
    .tag{font-size:0.75em;padding:1px 7px;border-radius:3px;background:#1f6feb;color:#e6edf3}
    .dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#3fb950;margin-right:6px}
    .sub{color:#484f58;font-size:0.78em;margin-top:6px}
    .empty{color:#484f58;font-size:0.82em;padding:8px 0}
  </style>
</head>
<body>
  <h1><span class="dot"></span>UniversalMesh Coordinator</h1>
  <div class="grid">
    <div class="card">
      <h2>ESP</h2>
      <div class="row"><span class="lbl">Chip</span><span class="val" id="chip">-</span></div>
      <div class="row"><span class="lbl">CPU</span><span class="val" id="cpu">-</span></div>
      <div class="row"><span class="lbl">Free Heap</span><span class="val" id="heap">-</span></div>
      <div class="row"><span class="lbl">Uptime</span><span class="val" id="uptime">-</span></div>
    </div>
    <div class="card">
      <h2>WiFi</h2>
      <div class="row"><span class="lbl">SSID</span><span class="val" id="ssid">-</span></div>
      <div class="row"><span class="lbl">IP</span><span class="val" id="ip">-</span></div>
      <div class="row"><span class="lbl">RSSI</span><span class="val" id="rssi">-</span></div>
      <div class="row"><span class="lbl">Channel</span><span class="val" id="ch">-</span></div>
    </div>
    <div class="card">
      <h2>Mesh Nodes</h2>
      <div id="nodes-empty" class="empty">No nodes discovered yet</div>
      <table id="nodes-table" style="display:none">
        <thead><tr><th>MAC</th><th>Last Seen</th></tr></thead>
        <tbody id="nodes-body"></tbody>
      </table>
    </div>
  </div>
  <div class="card">
    <h2>Live Packet Log</h2>
    <div id="log-empty" class="empty">No packets yet</div>
    <table id="log-table" style="display:none">
      <thead><tr><th>Type</th><th>From</th><th>App</th><th>Payload</th><th>Age</th></tr></thead>
      <tbody id="log-body"></tbody>
    </table>
    <div class="sub" id="tick"></div>
  </div>
  <script>
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
        set('chip', st.chip);
        set('cpu',  st.cpu_mhz+' MHz');
        set('heap', fmtBytes(st.free_heap));
        set('uptime', fmtUptime(st.uptime_ms));
        set('ssid', st.ssid);
        set('ip',   st.ip);
        set('rssi', st.rssi+' dBm');
        set('ch',   st.channel);

        const nb=document.getElementById('nodes-body');
        nb.innerHTML='';
        if(nd.nodes&&nd.nodes.length){
          document.getElementById('nodes-empty').style.display='none';
          document.getElementById('nodes-table').style.display='';
          nd.nodes.forEach(n=>{
            const dot='<span style="display:inline-block;width:8px;height:8px;border-radius:50%;background:'+(n.last_seen_seconds_ago<=120?'#3fb950':'#f85149')+';margin-right:6px"></span>';
            nb.innerHTML+='<tr><td>'+dot+n.mac+'</td><td>'+n.last_seen_seconds_ago+'s ago</td></tr>';
          });
        }else{
          document.getElementById('nodes-empty').style.display='';
          document.getElementById('nodes-table').style.display='none';
        }

        const lb=document.getElementById('log-body');
        lb.innerHTML='';
        if(lg.packets&&lg.packets.length){
          document.getElementById('log-empty').style.display='none';
          document.getElementById('log-table').style.display='';
          lg.packets.slice().reverse().forEach(p=>{
            lb.innerHTML+='<tr>'
              +'<td><span class="tag">0x'+p.type.toString(16).padStart(2,'0')+'</span></td>'
              +'<td>'+p.src+'</td>'
              +'<td>0x'+p.appId.toString(16).padStart(2,'0')+'</td>'
              +'<td>'+p.payload+'</td>'
              +'<td>'+p.age_s+'s</td>'
              +'</tr>';
          });
        }else{
          document.getElementById('log-empty').style.display='';
          document.getElementById('log-table').style.display='none';
        }
        set('tick','last update: '+new Date().toLocaleTimeString());
      }catch(e){}
    }
    refresh();
    setInterval(refresh,3000);
  </script>
</body>
</html>
)rawliteral";

// --- Endpoints ---
void initWebDashboard(AsyncWebServer& server) {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", HTML);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    String json = "{";
    json += "\"chip\":\""    + String(ESP.getChipModel())   + "\",";
    json += "\"cpu_mhz\":"   + String(ESP.getCpuFreqMHz())  + ",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap())    + ",";
    json += "\"uptime_ms\":" + String(millis())             + ",";
    json += "\"ssid\":\""    + WiFi.SSID()                  + "\",";
    json += "\"ip\":\""      + WiFi.localIP().toString()    + "\",";
    json += "\"rssi\":"      + String(WiFi.RSSI())          + ",";
    json += "\"channel\":"   + String(WiFi.channel());
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    String json = "{\"packets\":[";
    int start = _logCount < LOG_SIZE ? 0 : _logHead;
    for (int i = 0; i < _logCount; i++) {
      PacketLog& p = _log[(start + i) % LOG_SIZE];
      if (i > 0) json += ",";
      json += "{";
      json += "\"type\":"    + String(p.type)              + ",";
      json += "\"src\":\""   + String(p.src)               + "\",";
      json += "\"appId\":"   + String(p.appId)             + ",";
      json += "\"payload\":\"" + String(p.payload)         + "\",";
      json += "\"age_s\":"   + String((millis() - p.ts) / 1000);
      json += "}";
    }
    json += "]}";
    request->send(200, "application/json", json);
  });
}
