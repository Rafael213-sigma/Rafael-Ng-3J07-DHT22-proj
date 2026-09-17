// ============================================
// ESP32-S3 Environmental Monitor
// Board: ESP32S3 Dev Module
// Display: ST7789V2 320x240 SPI
// Sensor: DHT22
// ============================================

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <LovyanGFX.hpp>
#include <DHT.h>
#include <time.h>
#include <SPIFFS.h>

// ============================================
// PIN CONFIGURATION
// ============================================
#define DHTPIN        4
#define DHTTYPE       DHT22
#define BATTERY_PIN   1
#define TFT_SCK       12
#define TFT_MOSI      11
#define TFT_CS        10
#define TFT_DC        9
#define TFT_RST       7

// ============================================
// WIFI NETWORKS
// ============================================
struct WiFiNetwork { const char* ssid; const char* password; };
WiFiNetwork wifiList[] = {
  {"STEM", "stem28130360centre"},
  {"PC1 11C", "FsB3c74UgC"},
  {"1901", "Oxym0r0n"}
};
const int wifiCount = sizeof(wifiList) / sizeof(wifiList[0]);
int currentWifiIndex = 0;

// ============================================
// DISPLAY (LovyanGFX)
// ============================================
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;
public:
  LGFX(void) {
    auto cfg = _bus.config();
    cfg.spi_host = SPI2_HOST;
    cfg.freq_write = 80000000;
    cfg.dma_channel = 5;
    cfg.pin_sclk = TFT_SCK;
    cfg.pin_mosi = TFT_MOSI;
    cfg.pin_dc = TFT_DC;
    _bus.config(cfg);
    _panel.setBus(&_bus);
    auto pcfg = _panel.config();
    pcfg.pin_cs = TFT_CS;
    pcfg.pin_rst = TFT_RST;
    pcfg.panel_width = 240;
    pcfg.panel_height = 320;
    pcfg.invert = true;
    _panel.config(pcfg);
    setPanel(&_panel);
  }
};

LGFX tft;
LGFX_Sprite sprite(&tft);

// ============================================
// GLOBAL VARIABLES
// ============================================
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

float temperature = 0.0;
float humidity = 0.0;
float batteryVoltage = 0.0;
int batteryPercent = 0;

// History for graphs (last 60 readings = 1 minute)
#define HISTORY_SIZE 60
float tempHistory[HISTORY_SIZE];
float humHistory[HISTORY_SIZE];
int historyIndex = 0;
bool historyFull = false;

// Wallpaper
bool wallpaperEnabled = false;
#define WALLPAPER_FILE "/wallpaper.bin"

// Display settings
int dashTextSize = 2;    // Dashboard header text size
int cardTextSize = 4;    // Card value text size
int battTextSize = 5;    // Battery page text size
#define SETTINGS_FILE "/settings.txt"

// Display mode
enum DisplayMode { MODE_DASHBOARD, MODE_BATTERY };
DisplayMode displayMode = MODE_DASHBOARD;

// Button
#define BTN_PIN 14
bool lastBtnState = HIGH;
unsigned long lastBtnPress = 0;

// NTP
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 28800
#define DAYLIGHT_OFFSET_SEC 0

// ============================================
// BATTERY FUNCTIONS
// ============================================
float readBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  return (raw / 4095.0) * 3.3 * 2.0;
}

int getBatteryPercent(float v) {
  if (v >= 4.2) return 100;
  if (v <= 3.0) return 0;
  return (int)((v - 3.0) / 1.2 * 100);
}

// ============================================
// DISPLAY FUNCTIONS
// ============================================
void drawGraph(LGFX_Sprite &s, float *data, int count, int x, int y, int w, int h, uint16_t color, const char* label) {
  // Border
  s.drawRect(x, y, w, h, 0xFFFF);
  
  // Find min/max
  float minVal = data[0], maxVal = data[0];
  for (int i = 1; i < count; i++) {
    if (data[i] < minVal) minVal = data[i];
    if (data[i] > maxVal) maxVal = data[i];
  }
  float range = maxVal - minVal;
  if (range < 0.1) range = 0.1;
  
  // Draw bars
  int barW = max(1, (w - 2) / count);
  for (int i = 0; i < count; i++) {
    int barH = (int)((data[i] - minVal) / range * (h - 4));
    int px = x + 1 + (i * (w - 2) / count);
    int py = y + h - 2 - barH;
    s.fillRect(px, py, barW, barH, color);
  }
  
  // Label
  s.setTextSize(1);
  s.setTextColor(0xFFFF);
  s.setCursor(x + 2, y + 2);
  s.print(label);
  
  // Current value
  s.setCursor(x + w - 30, y + 2);
  s.print(data[count - 1], 1);
}

// Display wallpaper on sprite (supports JPEG and RGB565)
void displayWallpaper() {
  if (!wallpaperEnabled || !SPIFFS.exists(WALLPAPER_FILE)) return;
  
  File f = SPIFFS.open(WALLPAPER_FILE, "r");
  if (!f) return;
  
  size_t len = f.size();
  uint8_t* buf = (uint8_t*)malloc(len);
  if (!buf) { f.close(); return; }
  
  f.read(buf, len);
  f.close();
  
  // Try to decode as JPEG first
  // Check for JPEG magic bytes (FF D8 FF)
  if (len > 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
    // JPEG image - draw directly to sprite using LovyanGFX
    // We need to draw to the TFT first, then copy to sprite
    tft.drawJpg(buf, len, 0, 0);
    // Copy TFT to sprite
    sprite.fillScreen(0x0000);
    // Note: This is a limitation - we can't easily copy TFT to sprite
    // For now, we'll just draw to TFT directly when wallpaper is enabled
  } else {
    // Assume RGB565 raw format (320x240)
    uint16_t* pixels = (uint16_t*)buf;
    for (int y = 0; y < 240; y++) {
      for (int x = 0; x < 320; x++) {
        int idx = y * 320 + x;
        if (idx * 2 < len) {
          sprite.drawPixel(x, y, pixels[idx]);
        }
      }
    }
  }
  
  free(buf);
}

// Alternative: Direct TFT display for JPEG
void displayWallpaperDirect() {
  if (!SPIFFS.exists(WALLPAPER_FILE)) return;
  
  File f = SPIFFS.open(WALLPAPER_FILE, "r");
  if (!f) return;
  
  size_t len = f.size();
  uint8_t* buf = (uint8_t*)malloc(len);
  if (!buf) { f.close(); return; }
  
  f.read(buf, len);
  f.close();
  
  tft.drawJpg(buf, len, 0, 0);
  free(buf);
}

// Previous values for change detection
float prevTemp = -999;
float prevHum = -999;
int prevBatt = -1;
DisplayMode prevMode = MODE_DASHBOARD;

void updateDisplay() {
  // Check if values changed significantly (threshold: 0.1)
  bool changed = (abs(temperature - prevTemp) > 0.1) || 
                 (abs(humidity - prevHum) > 0.1) || 
                 (batteryPercent != prevBatt) ||
                 (displayMode != prevMode);
  
  // If nothing changed, skip update
  if (!changed) return;
  
  // Update previous values
  prevTemp = temperature;
  prevHum = humidity;
  prevBatt = batteryPercent;
  prevMode = displayMode;
  
  // If wallpaper is enabled, draw directly to TFT
  if (wallpaperEnabled) {
    displayWallpaperDirect();
    
    // Draw text directly on TFT
    struct tm t;
    tft.setTextColor(0xFFFF);
    
    switch (displayMode) {
      case MODE_DASHBOARD: {
        tft.setTextSize(2);
        tft.setCursor(5, 3);
        tft.print("DASHBOARD");
        
        if (getLocalTime(&t, 100)) {
          tft.setCursor(240, 3);
          int h = t.tm_hour;
          const char* ap = "AM";
          if (h == 0) h = 12;
          else if (h == 12) ap = "PM";
          else if (h > 12) { h -= 12; ap = "PM"; }
          if (h < 10) tft.print(" ");
          tft.print(h);
          tft.print(":");
          if (t.tm_min < 10) tft.print("0");
          tft.print(t.tm_min);
          tft.setTextSize(1);
          tft.setCursor(290, 3);
          tft.print(ap);
        }
        tft.drawFastHLine(0, 22, 320, 0xFFFF);
        
        tft.setTextSize(1);
        tft.setCursor(10, 30);
        tft.print("TEMPERATURE");
        tft.setTextSize(4);
        tft.setCursor(20, 50);
        tft.print(temperature, 1);
        tft.setTextSize(2);
        tft.print(" C");
        
        tft.setTextSize(1);
        tft.setCursor(10, 100);
        tft.print("HUMIDITY");
        tft.setTextSize(4);
        tft.setCursor(20, 120);
        tft.print(humidity, 1);
        tft.setTextSize(2);
        tft.print(" %");
        
        tft.drawFastHLine(0, 165, 320, 0xFFFF);
        if (getLocalTime(&t, 100)) {
          tft.setTextSize(2);
          tft.setCursor(5, 175);
          const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
          tft.print(days[t.tm_wday]);
          tft.print(" ");
          tft.print(t.tm_mon + 1);
          tft.print("/");
          tft.print(t.tm_mday);
          tft.print("/");
          tft.print(t.tm_year + 1900);
          
          tft.setCursor(200, 175);
          int h = t.tm_hour;
          const char* ap = "AM";
          if (h == 0) h = 12;
          else if (h == 12) ap = "PM";
          else if (h > 12) { h -= 12; ap = "PM"; }
          if (h < 10) tft.print(" ");
          tft.print(h);
          tft.print(":");
          if (t.tm_min < 10) tft.print("0");
          tft.print(t.tm_min);
          tft.print(ap);
        }
        
        tft.setTextSize(2);
        tft.setCursor(5, 200);
        tft.print("IP: ");
        tft.print(WiFi.localIP());
        break;
      }
      case MODE_BATTERY: {
        tft.setTextSize(2);
        tft.setCursor(5, 3);
        tft.print("BATTERY");
        tft.drawFastHLine(0, 22, 320, 0xFFFF);
        
        // Battery icon
        tft.drawRoundRect(10, 30, 140, 50, 8, 0xFFFF);
        tft.fillRect(150, 45, 8, 20, 0xFFFF);
        int fillW = (batteryPercent * 136) / 100;
        if (fillW > 0) tft.fillRoundRect(12, 32, fillW, 46, 6, 0xFFFF);
        
        // Percentage
        tft.setTextSize(5);
        tft.setCursor(170, 35);
        tft.print(batteryPercent);
        tft.setTextSize(2);
        tft.print("%");
        
        // Voltage with VDC
        tft.setCursor(170, 95);
        tft.print(batteryVoltage, 2);
        tft.print(" VDC");
        
        // Status
        tft.setCursor(170, 125);
        if (batteryPercent > 50) tft.print("GOOD");
        else if (batteryPercent > 20) tft.print("LOW");
        else tft.print("CRITICAL");
        break;
      }
    }
    return;
  }
  
  // Normal sprite-based display (no wallpaper)
  sprite.fillScreen(0x0000);
  
  struct tm t;
  
  switch (displayMode) {
    case MODE_DASHBOARD: {
      // Header
      sprite.setTextSize(2);
      sprite.setTextColor(0xFFFF);
      sprite.setCursor(5, 3);
      sprite.print("DASHBOARD");
      
      // Time
      if (getLocalTime(&t, 100)) {
        sprite.setCursor(240, 3);
        int h = t.tm_hour;
        const char* ap = "AM";
        if (h == 0) h = 12;
        else if (h == 12) ap = "PM";
        else if (h > 12) { h -= 12; ap = "PM"; }
        if (h < 10) sprite.print(" ");
        sprite.print(h);
        sprite.print(":");
        if (t.tm_min < 10) sprite.print("0");
        sprite.print(t.tm_min);
        sprite.setTextSize(1);
        sprite.setCursor(290, 3);
        sprite.print(ap);
      }
      sprite.drawFastHLine(0, 22, 320, 0xFFFF);
      
      // Temperature
      sprite.setTextSize(1);
      sprite.setCursor(10, 30);
      sprite.print("TEMPERATURE");
      sprite.setTextSize(4);
      sprite.setCursor(20, 50);
      sprite.print(temperature, 1);
      sprite.setTextSize(2);
      sprite.print(" C");
      
      // Humidity
      sprite.setTextSize(1);
      sprite.setCursor(10, 100);
      sprite.print("HUMIDITY");
      sprite.setTextSize(4);
      sprite.setCursor(20, 120);
      sprite.print(humidity, 1);
      sprite.setTextSize(2);
      sprite.print(" %");
      
      // Date
      sprite.drawFastHLine(0, 165, 320, 0xFFFF);
      if (getLocalTime(&t, 100)) {
        sprite.setTextSize(2);
        sprite.setCursor(5, 175);
        const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        sprite.print(days[t.tm_wday]);
        sprite.print(" ");
        sprite.print(t.tm_mon + 1);
        sprite.print("/");
        sprite.print(t.tm_mday);
        sprite.print("/");
        sprite.print(t.tm_year + 1900);
        
        sprite.setCursor(200, 175);
        int h = t.tm_hour;
        const char* ap = "AM";
        if (h == 0) h = 12;
        else if (h == 12) ap = "PM";
        else if (h > 12) { h -= 12; ap = "PM"; }
        if (h < 10) sprite.print(" ");
        sprite.print(h);
        sprite.print(":");
        if (t.tm_min < 10) sprite.print("0");
        sprite.print(t.tm_min);
        sprite.print(ap);
      }
      
      // IP
      sprite.setCursor(5, 195);
      sprite.print("IP: ");
      sprite.print(WiFi.localIP());
      
      // Footer
      sprite.drawFastHLine(0, 215, 320, 0xFFFF);
      sprite.setCursor(5, 225);
      sprite.print("Press button for battery");
      break;
    }
    
    case MODE_BATTERY: {
      // Header
      sprite.setTextSize(2);
      sprite.setTextColor(0xFFFF);
      sprite.setCursor(5, 3);
      sprite.print("BATTERY");
      sprite.drawFastHLine(0, 22, 320, 0xFFFF);
      
      // Battery icon
      sprite.drawRoundRect(10, 30, 140, 50, 8, 0xFFFF);
      sprite.fillRect(150, 45, 8, 20, 0xFFFF);
      int fillW = (batteryPercent * 136) / 100;
      if (fillW > 0) sprite.fillRoundRect(12, 32, fillW, 46, 6, 0xFFFF);
      
      // Percentage
      sprite.setTextSize(5);
      sprite.setTextColor(0xFFFF);
      sprite.setCursor(170, 35);
      sprite.print(batteryPercent);
      sprite.setTextSize(2);
      sprite.print("%");
      
      // Voltage
      sprite.setCursor(170, 95);
      sprite.print(batteryVoltage, 2);
      sprite.print(" VDC");
      
      // Status
      sprite.setCursor(170, 125);
      if (batteryPercent > 50) sprite.print("GOOD");
      else if (batteryPercent > 20) sprite.print("LOW");
      else sprite.print("CRITICAL");
      
      // Info
      sprite.drawFastHLine(0, 155, 320, 0xFFFF);
      sprite.setTextSize(2);
      sprite.setCursor(5, 165);
      sprite.print("ADC: ");
      sprite.print(analogRead(BATTERY_PIN));
      sprite.setCursor(5, 190);
      sprite.print("Voltage: ");
      sprite.print(batteryVoltage, 3);
      sprite.print("V");
      
      // Footer
      sprite.drawFastHLine(0, 210, 320, 0xFFFF);
      sprite.setCursor(5, 220);
      sprite.print("Press button to return");
      break;
    }
  }
  
  sprite.pushSprite(0, 0);
}

void handleButton() {
  bool btn = digitalRead(BTN_PIN);
  if (btn == LOW && lastBtnState == HIGH && millis() - lastBtnPress > 250) {
    lastBtnPress = millis();
    displayMode = (displayMode == MODE_DASHBOARD) ? MODE_BATTERY : MODE_DASHBOARD;
  }
  lastBtnState = btn;
}

// ============================================
// WEB SERVER - MAIN PAGE
// ============================================
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Monitor</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',sans-serif;background:#111;color:#fff;padding:20px}
.c{max-width:900px;margin:0 auto}
h1{text-align:center;color:#4ecdc4;margin-bottom:5px}
p.sub{text-align:center;color:#888;margin-bottom:20px}
.time{text-align:center;color:#4ecdc4;font-size:1.2rem;margin-bottom:20px}
.sr{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:15px;margin:20px 0}
.sc{background:#1e1e1e;padding:20px;border-radius:12px;text-align:center}
.sc .l{font-size:0.8rem;color:#aaa;text-transform:uppercase;letter-spacing:2px}
.sc .v{font-size:2.2rem;font-weight:700;margin:10px 0}
.sc .u{font-size:0.9rem;color:#888}
.tv{color:#ff6b6b}.hv{color:#4ecdc4}.bv{color:#ffd93d}.uv{color:#a29bfe}
.cc{background:#1e1e1e;border-radius:12px;padding:15px;margin:15px 0}
.cc h3{color:#aaa;font-size:0.85rem;text-transform:uppercase;margin-bottom:10px}
canvas{width:100%!important;height:150px!important;display:block;border-radius:8px}
.info{display:flex;justify-content:space-between;color:#888;font-size:0.8rem;margin-bottom:8px}
button{padding:8px 20px;background:#4ecdc4;color:#000;border:none;border-radius:6px;cursor:pointer;font-size:14px}
button:hover{background:#45b7af}
input{padding:8px;background:#222;color:#fff;border:1px solid #444;border-radius:6px;width:80px;text-align:center}
footer{text-align:center;padding:20px 0;color:#555;font-size:0.8rem}
</style>
</head>
<body>
<div class="c">
<h1>ESP32-S3 Monitor</h1>
<p class="sub">Environmental Monitoring System</p>
<div class="time">
<span id="day">---</span>, <span id="date">---</span> &bull; <span id="time">--:--:--</span>
</div>
<div class="sr">
<div class="sc"><div class="l">Temperature</div><div class="v tv" id="temp">--</div><div class="u">&deg;C</div></div>
<div class="sc"><div class="l">Humidity</div><div class="v hv" id="hum">--</div><div class="u">%</div></div>
<div class="sc"><div class="l">Battery</div><div class="v bv" id="batt">--</div><div class="u" id="battinfo">--</div></div>
<div class="sc"><div class="l">Uptime</div><div class="v uv" id="uptime">00:00:00</div><div class="u">HH:MM:SS</div></div>
</div>
<div class="cc">
<h3>Temperature History</h3>
<div class="info"><span>Min: <b id="tmin">--</b></span><span>Avg: <b id="tavg">--</b></span><span>Max: <b id="tmax">--</b></span></div>
<canvas id="tc"></canvas>
</div>
<div class="cc">
<h3>Humidity History</h3>
<div class="info"><span>Min: <b id="hmin">--</b></span><span>Avg: <b id="havg">--</b></span><span>Max: <b id="hmax">--</b></span></div>
<canvas id="hc"></canvas>
</div>
<div class="cc">
<h3>Wallpaper</h3>
<div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap">
<a href="/wallpaper" style="color:#4ecdc4">Upload Wallpaper</a>
<button onclick="toggleWallpaper()">Toggle Wallpaper</button>
</div>
</div>
<div class="cc">
<h3>Display Settings</h3>
<div style="display:flex;flex-direction:column;gap:15px">
<div>
<label style="color:#aaa;font-size:0.85rem">Dashboard Text Size: <span id="dashSizeVal">2</span></label>
<input type="range" id="dashSize" min="1" max="4" value="2" style="width:100%">
</div>
<div>
<label style="color:#aaa;font-size:0.85rem">Card Value Size: <span id="cardSizeVal">4</span></label>
<input type="range" id="cardSize" min="2" max="6" value="4" style="width:100%">
</div>
<div>
<label style="color:#aaa;font-size:0.85rem">Battery Page Size: <span id="battSizeVal">5</span></label>
<input type="range" id="battSize" min="3" max="7" value="5" style="width:100%">
</div>
<button onclick="saveDisplaySettings()">Save Settings</button>
<span id="settingsStatus" style="color:#888;font-size:0.8rem"></span>
</div>
</div>
<footer>
<span id="stxt">Connected</span> &bull; Last: <span id="lut">--</span>
</footer>
</div>
<script>
var TD=[],HD=[];
function drawChart(cv,data,color,minId,avgId,maxId){
  var c=document.getElementById(cv),ctx=c.getContext('2d'),W,H,dpr;
  dpr=window.devicePixelRatio||1;W=c.offsetWidth;H=150;
  c.width=W*dpr;c.height=H*dpr;c.style.width=W+'px';c.style.height=H+'px';
  ctx.scale(dpr,dpr);ctx.clearRect(0,0,W,H);
  if(data.length<2){ctx.fillStyle='#555';ctx.font='14px sans-serif';ctx.textAlign='center';ctx.fillText('Waiting...',W/2,H/2);return;}
  var mn=Math.min.apply(null,data),mx=Math.max.apply(null,data),avg=0;
  for(var i=0;i<data.length;i++)avg+=data[i];avg/=data.length;
  var pad=20,rng=mx-mn;if(rng<1)rng=1;mn-=rng*.1;mx+=rng*.1;rng=mx-mn;
  document.getElementById(minId).innerText=Math.min.apply(null,data).toFixed(1);
  document.getElementById(maxId).innerText=Math.max.apply(null,data).toFixed(1);
  document.getElementById(avgId).innerText=avg.toFixed(1);
  var gw=W-pad*2,gh=H-pad*2;
  ctx.strokeStyle='rgba(255,255,255,.06)';ctx.lineWidth=1;
  for(var i=0;i<=4;i++){var y=pad+gh*i/4;ctx.beginPath();ctx.moveTo(pad,y);ctx.lineTo(W-pad,y);ctx.stroke();}
  var pts=[];
  for(var i=0;i<data.length;i++)pts.push({x:pad+gw*i/(data.length-1),y:pad+gh-(data[i]-mn)/rng*gh});
  ctx.beginPath();ctx.strokeStyle=color;ctx.lineWidth=2.5;ctx.lineJoin='round';ctx.lineCap='round';
  ctx.moveTo(pts[0].x,pts[0].y);
  for(var i=1;i<pts.length;i++){var p=pts[i-1],c2=pts[i],cx=(p.x+c2.x)/2;ctx.bezierCurveTo(cx,p.y,cx,c2.y,c2.x,c2.y);}
  ctx.stroke();
  var grad=ctx.createLinearGradient(0,pad,0,H);
  grad.addColorStop(0,color.replace(')',',.25)').replace('#','rgba(').replace(/([a-f0-9]{2})([a-f0-9]{2})([a-f0-9]{2})/i,function(m,r,g,b){return parseInt(r,16)+','+parseInt(g,16)+','+parseInt(b,16)}));
  grad.addColorStop(1,'rgba(0,0,0,0)');
  ctx.beginPath();ctx.moveTo(pts[0].x,pts[0].y);
  for(var i=1;i<pts.length;i++){var p=pts[i-1],c2=pts[i],cx=(p.x+c2.x)/2;ctx.bezierCurveTo(cx,p.y,cx,c2.y,c2.x,c2.y);}
  ctx.lineTo(pts[pts.length-1].x,H);ctx.lineTo(pts[0].x,H);ctx.closePath();ctx.fillStyle=grad;ctx.fill();
  var last=pts[pts.length-1];ctx.fillStyle=color;ctx.font='bold 12px sans-serif';ctx.textAlign='left';
  ctx.fillText(data[data.length-1].toFixed(1),last.x+8,last.y+4);
}
function fmt(ms){var s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sc=s%60;return(h<10?'0':'')+h+':'+(m<10?'0':'')+m+':'+(sc<10?'0':'')+sc;}
function upd(){
  fetch('/data').then(function(r){return r.json();}).then(function(d){
    document.getElementById('temp').innerText=d.temperature.toFixed(1);
    document.getElementById('hum').innerText=d.humidity.toFixed(1);
    document.getElementById('batt').innerText=d.battery+'%';
    document.getElementById('battinfo').innerText=d.battV+'V';
    document.getElementById('uptime').innerText=fmt(d.uptime);
    document.getElementById('time').innerText=d.time;
    document.getElementById('date').innerText=d.date;
    document.getElementById('day').innerText=d.day;
    document.getElementById('lut').innerText=new Date().toLocaleTimeString();
    document.getElementById('stxt').innerText='Connected';
    TD.push(d.temperature);HD.push(d.humidity);
    if(TD.length>60){TD.shift();HD.shift();}
    drawChart('tc',TD,'#ff6b6b','tmin','tavg','tmax');
    drawChart('hc',HD,'#4ecdc4','hmin','havg','hmax');
  }).catch(function(){document.getElementById('stxt').innerText='Disconnected';});
}
function toggleWallpaper(){fetch('/wallpaper/toggle');}

// Display settings
function saveDisplaySettings(){
  var dashSize=document.getElementById('dashSize').value;
  var cardSize=document.getElementById('cardSize').value;
  var battSize=document.getElementById('battSize').value;
  fetch('/display/settings?dash='+dashSize+'&card='+cardSize+'&batt='+battSize)
    .then(function(r){return r.text();})
    .then(function(t){
      document.getElementById('settingsStatus').innerText='Saved!';
      setTimeout(function(){document.getElementById('settingsStatus').innerText='';},2000);
    });
}

// Load settings on page load
function loadDisplaySettings(){
  fetch('/display/settings').then(function(r){return r.json();}).then(function(d){
    document.getElementById('dashSize').value=d.dash;
    document.getElementById('cardSize').value=d.card;
    document.getElementById('battSize').value=d.batt;
    document.getElementById('dashSizeVal').innerText=d.dash;
    document.getElementById('cardSizeVal').innerText=d.card;
    document.getElementById('battSizeVal').innerText=d.batt;
  });
}

// Update slider values display
document.getElementById('dashSize').addEventListener('input',function(e){
  document.getElementById('dashSizeVal').innerText=e.target.value;
});
document.getElementById('cardSize').addEventListener('input',function(e){
  document.getElementById('cardSizeVal').innerText=e.target.value;
});
document.getElementById('battSize').addEventListener('input',function(e){
  document.getElementById('battSizeVal').innerText=e.target.value;
});

loadDisplaySettings();
setInterval(upd,1000);upd();
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

// ============================================
// WEB SERVER - DATA ENDPOINT
// ============================================
void handleData() {
  struct tm t;
  char timeStr[20] = "--:--";
  char dateStr[30] = "--/--/----";
  char dayStr[10] = "---";
  
  if (getLocalTime(&t, 100)) {
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int h = t.tm_hour;
    const char* ap = "AM";
    if (h == 0) { h = 12; ap = "AM"; }
    else if (h == 12) { ap = "PM"; }
    else if (h > 12) { h -= 12; ap = "PM"; }
    sprintf(timeStr, "%d:%02d:%02d %s", h, t.tm_min, t.tm_sec, ap);
    sprintf(dateStr, "%s %d, %d", months[t.tm_mon], t.tm_mday, t.tm_year + 1900);
    strcpy(dayStr, days[t.tm_wday]);
  }
  
  String json = "{\"temperature\":" + String(temperature, 2) + 
                ",\"humidity\":" + String(humidity, 2) + 
                ",\"uptime\":" + String(millis()) + 
                ",\"battery\":" + String(batteryPercent) +
                ",\"battV\":" + String(batteryVoltage, 2) +
                ",\"time\":\"" + timeStr + "\"" +
                ",\"date\":\"" + dateStr + "\"" +
                ",\"day\":\"" + dayStr + "\"}";
  server.send(200, "application/json", json);
}

// ============================================
// WEB SERVER - WALLPAPER
// ============================================
void handleWallpaperPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wallpaper Editor</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',sans-serif;background:#111;color:#fff;padding:15px}
h1{text-align:center;color:#4ecdc4;margin-bottom:10px}
.info{text-align:center;color:#888;font-size:0.9rem;margin-bottom:15px}
.editor{display:flex;gap:15px;flex-wrap:wrap;justify-content:center}
.canvas-wrap{position:relative;background:#000;border:2px solid #333;border-radius:8px;overflow:hidden}
canvas{display:block}
.controls{display:flex;flex-direction:column;gap:10px;min-width:200px}
.btn-group{display:flex;gap:5px;flex-wrap:wrap}
button{padding:8px 15px;background:#4ecdc4;color:#000;border:none;border-radius:6px;cursor:pointer;font-size:13px;flex:1;min-width:80px}
button:hover{background:#45b7af}
button.active{background:#ff6b6b;color:#fff}
label{color:#aaa;font-size:0.85rem}
input[type=range]{width:100%;margin:5px 0}
.preview-wrap{margin-top:15px;text-align:center}
.preview-wrap canvas{border:2px solid #4ecdc4;border-radius:8px;margin:0 auto}
.upload-btn{display:block;width:100%;padding:12px;background:#4ecdc4;color:#000;border:none;border-radius:8px;font-size:16px;cursor:pointer;margin-top:15px}
.upload-btn:hover{background:#45b7af}
#status{margin:10px 0;padding:10px;border-radius:6px;display:none;text-align:center}
.ok{background:#00ff8833;color:#00ff88}
.err{background:#ff6b6b33;color:#ff6b6b}
.upload-area{border:2px dashed #4ecdc4;border-radius:10px;padding:40px;text-align:center;cursor:pointer;margin-bottom:15px}
.upload-area:hover{background:rgba(78,205,196,0.1)}
input[type=file]{display:none}
</style>
</head>
<body>
<h1>Wallpaper Editor</h1>
<p class="info">Upload, crop, zoom, rotate, and position your wallpaper</p>

<div class="upload-area" id="uploadArea" onclick="document.getElementById('fileInput').click()">
<p>Click or drag image here to upload</p>
<p class="info">Supports JPG, PNG, GIF</p>
</div>
<input type="file" id="fileInput" accept="image/*">

<div class="editor" id="editor" style="display:none">
<div class="canvas-wrap">
  <canvas id="canvas" width="400" height="300"></canvas>
</div>
<div class="controls">
  <div>
    <label>Zoom: <span id="zoomVal">100%</span></label>
    <input type="range" id="zoom" min="10" max="300" value="100">
  </div>
  <div>
    <label>Rotation: <span id="rotVal">0°</span></label>
    <input type="range" id="rotation" min="0" max="360" value="0">
  </div>
  <div class="btn-group">
    <button onclick="rotate(-90)">-90°</button>
    <button onclick="rotate(90)">+90°</button>
    <button onclick="resetTransform()">Reset</button>
  </div>
  <div class="btn-group">
    <button onclick="fitImage()">Fit</button>
    <button onclick="fillImage()">Fill</button>
    <button onclick="centerImage()">Center</button>
  </div>
  <div>
    <label>Brightness: <span id="brightVal">100%</span></label>
    <input type="range" id="brightness" min="20" max="200" value="100">
  </div>
  <div>
    <label>Contrast: <span id="contrastVal">100%</span></label>
    <input type="range" id="contrast" min="20" max="200" value="100">
  </div>
</div>
</div>

<div class="preview-wrap" id="previewWrap" style="display:none">
<h3 style="color:#4ecdc4;margin-bottom:10px">Preview (320x240)</h3>
<canvas id="preview" width="320" height="240"></canvas>
</div>

<button class="upload-btn" id="uploadBtn" style="display:none" onclick="uploadWallpaper()">Upload Wallpaper</button>
<div id="status"></div>
<a href="/" style="display:block;text-align:center;color:#4ecdc4;margin-top:15px">Back to Dashboard</a>

<script>
var img = new Image();
var canvas = document.getElementById('canvas');
var ctx = canvas.getContext('2d');
var previewCanvas = document.getElementById('preview');
var previewCtx = previewCanvas.getContext('2d');

var imgX = 0, imgY = 0;
var imgScale = 1;
var imgRotation = 0;
var brightness = 100;
var contrast = 100;
var isDragging = false;
var dragStartX, dragStartY;
var imgStartX, imgStartY;

// File input
document.getElementById('fileInput').addEventListener('change', function(e) {
  if (e.target.files[0]) loadImage(e.target.files[0]);
});

// Drag and drop
var uploadArea = document.getElementById('uploadArea');
uploadArea.addEventListener('dragover', function(e) { e.preventDefault(); this.style.background = 'rgba(78,205,196,0.2)'; });
uploadArea.addEventListener('dragleave', function(e) { this.style.background = ''; });
uploadArea.addEventListener('drop', function(e) {
  e.preventDefault();
  this.style.background = '';
  if (e.dataTransfer.files[0]) loadImage(e.dataTransfer.files[0]);
});

function loadImage(file) {
  var reader = new FileReader();
  reader.onload = function(e) {
    img.onload = function() {
      document.getElementById('editor').style.display = 'flex';
      document.getElementById('previewWrap').style.display = 'block';
      document.getElementById('uploadBtn').style.display = 'block';
      document.getElementById('uploadArea').style.display = 'none';
      fitImage();
      draw();
    };
    img.src = e.target.result;
  };
  reader.readAsDataURL(file);
}

function draw() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.save();
  
  // Apply transforms
  ctx.translate(canvas.width / 2, canvas.height / 2);
  ctx.rotate(imgRotation * Math.PI / 180);
  ctx.scale(imgScale, imgScale);
  ctx.translate(imgX - canvas.width / 2, imgY - canvas.height / 2);
  
  // Apply brightness/contrast
  ctx.filter = 'brightness(' + brightness + '%) contrast(' + contrast + '%)';
  
  ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
  ctx.restore();
  
  // Draw crop frame
  ctx.strokeStyle = '#4ecdc4';
  ctx.lineWidth = 2;
  ctx.setLineDash([5, 5]);
  var cropX = (canvas.width - 320) / 2;
  var cropY = (canvas.height - 240) / 2;
  ctx.strokeRect(cropX, cropY, 320, 240);
  ctx.setLineDash([]);
  
  // Draw crosshair
  ctx.strokeStyle = 'rgba(78,205,196,0.3)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(canvas.width / 2, 0);
  ctx.lineTo(canvas.width / 2, canvas.height);
  ctx.moveTo(0, canvas.height / 2);
  ctx.lineTo(canvas.width, canvas.height / 2);
  ctx.stroke();
  
  // Update preview
  updatePreview();
}

function updatePreview() {
  previewCtx.clearRect(0, 0, 320, 240);
  previewCtx.save();
  previewCtx.translate(160, 120);
  previewCtx.rotate(imgRotation * Math.PI / 180);
  previewCtx.scale(imgScale * (320 / canvas.width), imgScale * (240 / canvas.height));
  previewCtx.translate(imgX - canvas.width / 2, imgY - canvas.height / 2);
  previewCtx.filter = 'brightness(' + brightness + '%) contrast(' + contrast + '%)';
  previewCtx.drawImage(img, 0, 0, canvas.width, canvas.height);
  previewCtx.restore();
}

// Mouse/touch controls
canvas.addEventListener('mousedown', function(e) {
  isDragging = true;
  dragStartX = e.clientX;
  dragStartY = e.clientY;
  imgStartX = imgX;
  imgStartY = imgY;
});

canvas.addEventListener('mousemove', function(e) {
  if (!isDragging) return;
  imgX = imgStartX + (e.clientX - dragStartX) / imgScale;
  imgY = imgStartY + (e.clientY - dragStartY) / imgScale;
  draw();
});

canvas.addEventListener('mouseup', function() { isDragging = false; });
canvas.addEventListener('mouseleave', function() { isDragging = false; });

// Touch support
canvas.addEventListener('touchstart', function(e) {
  e.preventDefault();
  isDragging = true;
  dragStartX = e.touches[0].clientX;
  dragStartY = e.touches[0].clientY;
  imgStartX = imgX;
  imgStartY = imgY;
});

canvas.addEventListener('touchmove', function(e) {
  e.preventDefault();
  if (!isDragging) return;
  imgX = imgStartX + (e.touches[0].clientX - dragStartX) / imgScale;
  imgY = imgStartY + (e.touches[0].clientY - dragStartY) / imgScale;
  draw();
});

canvas.addEventListener('touchend', function() { isDragging = false; });

// Zoom with mouse wheel
canvas.addEventListener('wheel', function(e) {
  e.preventDefault();
  var zoomSlider = document.getElementById('zoom');
  var val = parseInt(zoomSlider.value) + (e.deltaY > 0 ? -5 : 5);
  val = Math.max(10, Math.min(300, val));
  zoomSlider.value = val;
  imgScale = val / 100;
  document.getElementById('zoomVal').innerText = val + '%';
  draw();
});

// Slider controls
document.getElementById('zoom').addEventListener('input', function(e) {
  imgScale = e.target.value / 100;
  document.getElementById('zoomVal').innerText = e.target.value + '%';
  draw();
});

document.getElementById('rotation').addEventListener('input', function(e) {
  imgRotation = parseInt(e.target.value);
  document.getElementById('rotVal').innerText = e.target.value + '°';
  draw();
});

document.getElementById('brightness').addEventListener('input', function(e) {
  brightness = parseInt(e.target.value);
  document.getElementById('brightVal').innerText = e.target.value + '%';
  draw();
});

document.getElementById('contrast').addEventListener('input', function(e) {
  contrast = parseInt(e.target.value);
  document.getElementById('contrastVal').innerText = e.target.value + '%';
  draw();
});

// Helper functions
function rotate(deg) {
  imgRotation = (imgRotation + deg + 360) % 360;
  document.getElementById('rotation').value = imgRotation;
  document.getElementById('rotVal').innerText = imgRotation + '°';
  draw();
}

function resetTransform() {
  imgX = 0; imgY = 0;
  imgScale = 1; imgRotation = 0;
  brightness = 100; contrast = 100;
  document.getElementById('zoom').value = 100;
  document.getElementById('rotation').value = 0;
  document.getElementById('brightness').value = 100;
  document.getElementById('contrast').value = 100;
  document.getElementById('zoomVal').innerText = '100%';
  document.getElementById('rotVal').innerText = '0°';
  document.getElementById('brightVal').innerText = '100%';
  document.getElementById('contrastVal').innerText = '100%';
  fitImage();
  draw();
}

function fitImage() {
  var scaleW = canvas.width / img.width;
  var scaleH = canvas.height / img.height;
  imgScale = Math.min(scaleW, scaleH);
  imgX = canvas.width / 2;
  imgY = canvas.height / 2;
  document.getElementById('zoom').value = Math.round(imgScale * 100);
  document.getElementById('zoomVal').innerText = Math.round(imgScale * 100) + '%';
  draw();
}

function fillImage() {
  var scaleW = canvas.width / img.width;
  var scaleH = canvas.height / img.height;
  imgScale = Math.max(scaleW, scaleH);
  imgX = canvas.width / 2;
  imgY = canvas.height / 2;
  document.getElementById('zoom').value = Math.round(imgScale * 100);
  document.getElementById('zoomVal').innerText = Math.round(imgScale * 100) + '%';
  draw();
}

function centerImage() {
  imgX = canvas.width / 2;
  imgY = canvas.height / 2;
  draw();
}

function uploadWallpaper() {
  var btn = document.getElementById('uploadBtn');
  btn.disabled = true;
  btn.innerText = 'Processing...';
  
  // Create output canvas at 320x240
  var outCanvas = document.createElement('canvas');
  outCanvas.width = 320;
  outCanvas.height = 240;
  var outCtx = outCanvas.getContext('2d');
  
  // Apply transforms to output
  outCtx.translate(160, 120);
  outCtx.rotate(imgRotation * Math.PI / 180);
  outCtx.scale(imgScale * (320 / canvas.width), imgScale * (240 / canvas.height));
  outCtx.translate(imgX - canvas.width / 2, imgY - canvas.height / 2);
  outCtx.filter = 'brightness(' + brightness + '%) contrast(' + contrast + '%)';
  outCtx.drawImage(img, 0, 0, canvas.width, canvas.height);
  
  // Convert to blob and upload
  outCanvas.toBlob(function(blob) {
    var formData = new FormData();
    formData.append('image', blob, 'wallpaper.jpg');
    
    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/wallpaper/upload', true);
    
    xhr.upload.onprogress = function(e) {
      if (e.lengthComputable) {
        var percent = Math.round(e.loaded / e.total * 100);
        btn.innerText = 'Uploading... ' + percent + '%';
      }
    };
    
    xhr.onload = function() {
      btn.disabled = false;
      btn.innerText = 'Upload Wallpaper';
      var status = document.getElementById('status');
      status.style.display = 'block';
      if (xhr.status === 200) {
        status.className = 'ok';
        status.innerText = 'Wallpaper uploaded successfully!';
      } else {
        status.className = 'err';
        status.innerText = 'Upload failed: ' + xhr.responseText;
      }
      setTimeout(function() { status.style.display = 'none'; }, 3000);
    };
    
    xhr.onerror = function() {
      btn.disabled = false;
      btn.innerText = 'Upload Wallpaper';
      var status = document.getElementById('status');
      status.style.display = 'block';
      status.className = 'err';
      status.innerText = 'Upload failed - connection error';
    };
    
    xhr.send(formData);
  }, 'image/jpeg', 0.85);
}
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleWallpaperUpload() {
  HTTPUpload& upload = server.upload();
  static File f;
  static bool isJpeg = false;
  
  if (upload.status == UPLOAD_FILE_START) {
    // Check if JPEG by filename
    String filename = upload.filename;
    isJpeg = filename.endsWith(".jpg") || filename.endsWith(".jpeg");
    f = SPIFFS.open(WALLPAPER_FILE, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (f) f.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (f) { 
      f.close(); 
      wallpaperEnabled = true;
      displayWallpaperDirect();
    }
  }
}

void handleWallpaperToggle() {
  wallpaperEnabled = !wallpaperEnabled;
  server.send(200, "text/plain", wallpaperEnabled ? "ON" : "OFF");
}

void handleWallpaperUploadDone() {
  server.send(200, "text/plain", "Upload complete");
  displayWallpaperDirect();
}

void handleDisplaySettings() {
  if (server.hasArg("dash") && server.hasArg("card") && server.hasArg("batt")) {
    dashTextSize = server.arg("dash").toInt();
    cardTextSize = server.arg("card").toInt();
    battTextSize = server.arg("batt").toInt();
    
    // Save to SPIFFS
    File f = SPIFFS.open(SETTINGS_FILE, "w");
    if (f) {
      f.printf("%d,%d,%d", dashTextSize, cardTextSize, battTextSize);
      f.close();
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    // Return current settings
    String json = "{\"dash\":" + String(dashTextSize) + 
                  ",\"card\":" + String(cardTextSize) + 
                  ",\"batt\":" + String(battTextSize) + "}";
    server.send(200, "application/json", json);
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  
  // Init hardware
  analogReadResolution(12);
  dht.begin();
  pinMode(BTN_PIN, INPUT_PULLUP);
  
  // Init display
  tft.init();
  tft.setRotation(1);  // Landscape mode (320x240)
  tft.fillScreen(0x0000);
  tft.setTextColor(0xFFFF);
  tft.setTextSize(2);
  tft.setCursor(50, 100);
  tft.print("Starting...");
  
  // Init sprite
  sprite.createSprite(320, 240);
  
  // Init SPIFFS
  if (SPIFFS.begin(true)) {
    if (SPIFFS.exists(WALLPAPER_FILE)) wallpaperEnabled = true;
    
    // Load display settings
    if (SPIFFS.exists(SETTINGS_FILE)) {
      File f = SPIFFS.open(SETTINGS_FILE, "r");
      if (f) {
        String content = f.readString();
        f.close();
        int comma1 = content.indexOf(',');
        int comma2 = content.indexOf(',', comma1 + 1);
        if (comma1 > 0 && comma2 > 0) {
          dashTextSize = content.substring(0, comma1).toInt();
          cardTextSize = content.substring(comma1 + 1, comma2).toInt();
          battTextSize = content.substring(comma2 + 1).toInt();
        }
      }
    }
  }
  
  // WiFi Selection Menu
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(500);
  
  int selectedWiFi = 0;
  bool wifiSelected = false;
  
  // Draw WiFi menu
  sprite.fillScreen(0x0000);
  sprite.setTextSize(2);
  sprite.setTextColor(0xFFFF);
  sprite.setCursor(10, 5);
  sprite.print("SELECT WIFI");
  sprite.drawFastHLine(0, 28, 320, 0xFFFF);
  
  // Draw WiFi list
  for (int i = 0; i < wifiCount; i++) {
    sprite.setCursor(20, 40 + i * 35);
    sprite.setTextSize(2);
    sprite.setTextColor(i == selectedWiFi ? 0x07E0 : 0xFFFF);
    sprite.print("> ");
    sprite.print(wifiList[i].ssid);
  }
  
  sprite.setTextSize(1);
  sprite.setTextColor(0xFFFF);
  sprite.setCursor(10, 220);
  sprite.print("PRESS = CYCLE   HOLD = CONNECT");
  sprite.pushSprite(0, 0);
  
  // Wait for WiFi selection
  while (!wifiSelected) {
    bool btn = digitalRead(BTN_PIN);
    if (btn == LOW) {
      unsigned long pressStart = millis();
      delay(200);
      
      // Check for long press (connect)
      while (digitalRead(BTN_PIN) == LOW) {
        if (millis() - pressStart > 1000) {
          wifiSelected = true;
          break;
        }
        delay(10);
      }
      
      // Short press = cycle
      if (!wifiSelected) {
        // Clear old selection
        sprite.fillRect(20, 40 + selectedWiFi * 35, 280, 25, 0x0000);
        sprite.setCursor(20, 40 + selectedWiFi * 35);
        sprite.setTextSize(2);
        sprite.setTextColor(0xFFFF);
        sprite.print("> ");
        sprite.print(wifiList[selectedWiFi].ssid);
        
        // Move to next
        selectedWiFi = (selectedWiFi + 1) % wifiCount;
        
        // Draw new selection
        sprite.fillRect(20, 40 + selectedWiFi * 35, 280, 25, 0x0000);
        sprite.setCursor(20, 40 + selectedWiFi * 35);
        sprite.setTextColor(0x07E0);
        sprite.print("> ");
        sprite.print(wifiList[selectedWiFi].ssid);
        sprite.pushSprite(0, 0);
      }
    }
    delay(50);
  }
  
  // Connect to selected WiFi
  currentWifiIndex = selectedWiFi;
  
  // Show connecting screen
  sprite.fillScreen(0x0000);
  sprite.setTextSize(2);
  sprite.setTextColor(0x07E0);
  sprite.setCursor(10, 80);
  sprite.print("CONNECTING TO:");
  sprite.setTextSize(3);
  sprite.setCursor(10, 120);
  sprite.print(wifiList[selectedWiFi].ssid);
  sprite.pushSprite(0, 0);
  
  WiFi.begin(wifiList[selectedWiFi].ssid, wifiList[selectedWiFi].password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    sprite.fillScreen(0x0000);
    sprite.setTextSize(2);
    sprite.setTextColor(0x07E0);
    sprite.setCursor(10, 80);
    sprite.print("CONNECTED!");
    sprite.setTextSize(2);
    sprite.setCursor(10, 120);
    sprite.print("IP: ");
    sprite.print(WiFi.localIP());
    sprite.pushSprite(0, 0);
    delay(2000);
  } else {
    sprite.fillScreen(0x0000);
    sprite.setTextSize(2);
    sprite.setTextColor(0xF800);
    sprite.setCursor(10, 100);
    sprite.print("CONNECTION FAILED");
    sprite.pushSprite(0, 0);
    delay(2000);
  }
  
  // Web server
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/wallpaper", handleWallpaperPage);
  server.on("/wallpaper/upload", HTTP_POST, handleWallpaperUploadDone, handleWallpaperUpload);
  server.on("/wallpaper/toggle", handleWallpaperToggle);
  server.on("/display/settings", handleDisplaySettings);
  server.begin();
  
  // NTP
  if (WiFi.status() == WL_CONNECTED) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  }
}

// ============================================
// LOOP
// ============================================
void loop() {
  handleButton();
  server.handleClient();
  
  // WiFi retry - try next network if disconnected
  static unsigned long lastWifi = 0;
  if (millis() - lastWifi > 15000) {
    lastWifi = millis();
    if (WiFi.status() != WL_CONNECTED) {
      currentWifiIndex = (currentWifiIndex + 1) % wifiCount;
      Serial.print("Trying WiFi: ");
      Serial.println(wifiList[currentWifiIndex].ssid);
      WiFi.disconnect(false);
      WiFi.begin(wifiList[currentWifiIndex].ssid, wifiList[currentWifiIndex].password);
    }
  }
  
  // Read sensors every second
  static unsigned long lastSensor = 0;
  if (millis() - lastSensor > 1000) {
    lastSensor = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) temperature = t;
    if (!isnan(h)) humidity = h;
    
    batteryVoltage = readBatteryVoltage();
    batteryPercent = getBatteryPercent(batteryVoltage);
    
    // Store history
    tempHistory[historyIndex] = temperature;
    humHistory[historyIndex] = humidity;
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    if (historyIndex == 0) historyFull = true;
  }
  
  // Update display at 24 FPS
  static unsigned long lastDisplay = 0;
  if (millis() - lastDisplay > 100) {  // Update every 100ms
    lastDisplay = millis();
    updateDisplay();
  }
}
