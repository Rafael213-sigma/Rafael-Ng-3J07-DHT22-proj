#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// --- Wi-Fi Credentials (add as many as needed) ---
struct WiFiNetwork {
  const char* ssid;
  const char* password;
};

WiFiNetwork wifiList[] = {
  {"STEM", "stem28130360centre"},
  {"PC1 11C", "FsB3c74UgC"},
  // Add more networks here:
  // {"MyHomeWiFi", "password123"},
  // {"OfficeWiFi", "officepass"},
};

const int wifiCount = sizeof(wifiList) / sizeof(wifiList[0]);
int currentWifiIndex = 0;

// --- ESP32-C3 Pin Configuration ---
#define SDA_PIN 5
#define SCL_PIN 6
#define DHTPIN  4

// --- Button & Buzzer Pins ---
#define BTN_VOL_UP    0
#define BTN_VOL_DOWN  1
#define BTN_TEMP      2
#define BTN_HUM       3
#define BUZZER_PIN    7
#define BATTERY_PIN   8  // ADC pin for battery voltage

// --- Sensor & Display Setup ---
#define DHTTYPE DHT22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);

// Global variables for sensor data
float temperature = 0.0;
float humidity = 0.0;
bool oledOk = false;

// Battery monitoring
float batteryVoltage = 0.0;
int batteryPercent = 0;
#define BATTERY_DIVIDER_RATIO 1.5  // 100k + 200k divider = 1.5x
#define BATTERY_LOW_VOLTAGE 3.2    // Low battery threshold
#define BATTERY_MIN_VOLTAGE 3.0    // Empty battery
#define BATTERY_MAX_VOLTAGE 4.2    // Full battery
bool lowBatteryAlarmActive = false;
unsigned long lastLowBattBeep = 0;
#define LOW_BATT_BEEP_INTERVAL 5000  // Beep every 5 seconds for low battery

// Volume & Buzzer
int volume = 50;  // 0-100
bool buzzerActive = false;
unsigned long volumeShowTime = 0;  // When to show volume on screen
#define VOLUME_SHOW_DURATION 1500  // Show volume for 1.5 seconds
#define TEMP_ALARM_THRESHOLD 40.0  // Alarm when temp exceeds this
bool tempAlarmActive = false;
unsigned long lastAlarmBeep = 0;
#define ALARM_BEEP_INTERVAL 2000  // Beep every 2 seconds

// Display modes
enum DisplayMode { MODE_NORMAL, MODE_TEMP_GRAPH, MODE_HUM_GRAPH };
DisplayMode displayMode = MODE_NORMAL;

// History for OLED graphs (stores up to 1 hour of data at 1s intervals)
#define GRAPH_HISTORY 3600
float tempHistory[GRAPH_HISTORY];
float humHistory[GRAPH_HISTORY];
int historyIndex = 0;
bool historyFull = false;

// Button debounce
unsigned long lastBtnPress[4] = {0, 0, 0, 0};
#define DEBOUNCE_MS 250

// Non-blocking timer variables
unsigned long previousMillis = 0;
const long interval = 1000; // Update DHT22 and OLED every 1 second

// --- Web Page HTML (Embedded) ---
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-C3 Dashboard</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:'Segoe UI',sans-serif;background:#000;color:#fff;min-height:100vh;padding:20px}
    .c{max-width:900px;margin:0 auto}
    header{text-align:center;padding:20px 0}
    header h1{font-size:2rem;color:#fff}
    header p{color:#fff;margin-top:5px;font-size:.9rem}
    .sr{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin:20px 0}
    .sc{background:rgba(255,255,255,.05);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,.1);border-radius:16px;padding:25px;text-align:center;transition:transform .3s}
    .sc:hover{transform:translateY(-5px);box-shadow:0 10px 30px rgba(0,0,0,.3)}
    .sc .l{font-size:.8rem;color:#fff;text-transform:uppercase;letter-spacing:2px}
    .sc .v{font-size:2.5rem;font-weight:700;margin:10px 0}
    .sc .u{font-size:1rem;color:#fff}
    .tv{color:#ff6b6b}.hv{color:#4ecdc4}.uv{color:#a29bfe;font-size:1.8rem!important}.bv{color:#ffd93d;font-size:1.8rem!important}
    .cc{background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.1);border-radius:16px;padding:20px;margin:15px 0}
    .cc h3{color:#fff;font-size:.85rem;text-transform:uppercase;letter-spacing:1px;margin-bottom:10px}
    .cc .info{display:flex;justify-content:space-between;color:#fff;font-size:.75rem;margin-bottom:10px}
    .cc .info span{color:#fff}
    canvas{width:100%!important;height:180px!important;display:block;border-radius:8px}
    footer{text-align:center;padding:30px 0;color:#fff;font-size:.8rem}
    .sd{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:5px}
    .on{background:#00ff88;box-shadow:0 0 10px #00ff88}.off{background:#ff4757;box-shadow:0 0 10px #ff4757}
  </style>
</head>
<body>
  <div class="c">
    <header>
      <h1>ESP32-C3 Live Dashboard</h1>
      <p>Environmental Monitoring System</p>
    </header>
    <div class="sr">
      <div class="sc">
        <div class="l">Temperature</div>
        <div class="v tv" id="temp">--</div>
        <div class="u">&deg;C</div>
      </div>
      <div class="sc">
        <div class="l">Humidity</div>
        <div class="v hv" id="hum">--</div>
        <div class="u">%</div>
      </div>
      <div class="sc">
        <div class="l">Uptime</div>
        <div class="v uv" id="uptime">00:00:00</div>
        <div class="u" id="ulabel">HH:MM:SS</div>
      </div>
      <div class="sc">
        <div class="l">Battery</div>
        <div class="v bv" id="batt">--</div>
        <div class="u" id="battinfo">--</div>
      </div>
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
    <footer>
      <span class="sd on" id="dot"></span>
      <span id="stxt">Connected</span> &bull; Last: <span id="lut">--</span>
    </footer>
  </div>
  <script>
    var TD=[],HD=[];
    function drawChart(cv,data,color,minId,avgId,maxId){
      var c=document.getElementById(cv),ctx=c.getContext('2d'),W,H,dpr;
      dpr=window.devicePixelRatio||1;
      W=c.offsetWidth;H=180;
      c.width=W*dpr;c.height=H*dpr;
      c.style.width=W+'px';c.style.height=H+'px';
      ctx.scale(dpr,dpr);
      ctx.clearRect(0,0,W,H);
      
      if(data.length<2){
        ctx.fillStyle='#555';ctx.font='14px sans-serif';ctx.textAlign='center';
        ctx.fillText('Waiting for data...',W/2,H/2);
        return;
      }
      
      var mn=Math.min.apply(null,data),mx=Math.max.apply(null,data);
      var avg=0;for(var i=0;i<data.length;i++)avg+=data[i];avg/=data.length;
      var pad=20,rng=mx-mn;if(rng<1)rng=1;
      mn-=rng*.1;mx+=rng*.1;rng=mx-mn;
      
      document.getElementById(minId).innerText=Math.min.apply(null,data).toFixed(1);
      document.getElementById(maxId).innerText=Math.max.apply(null,data).toFixed(1);
      document.getElementById(avgId).innerText=avg.toFixed(1);
      
      var gw=W-pad*2,gh=H-pad*2;
      
      // Grid
      ctx.strokeStyle='rgba(255,255,255,.06)';ctx.lineWidth=1;
      for(var i=0;i<=4;i++){
        var y=pad+gh*i/4;
        ctx.beginPath();ctx.moveTo(pad,y);ctx.lineTo(W-pad,y);ctx.stroke();
        ctx.fillStyle='#555';ctx.font='10px sans-serif';ctx.textAlign='right';
        ctx.fillText((mx-rng*i/4).toFixed(1),pad-4,y+3);
      }
      
      // Points
      var pts=[];
      for(var i=0;i<data.length;i++){
        pts.push({x:pad+gw*i/(data.length-1),y:pad+gh-(data[i]-mn)/rng*gh});
      }
      
      // Smooth line with bezier
      ctx.beginPath();ctx.strokeStyle=color;ctx.lineWidth=2.5;ctx.lineJoin='round';ctx.lineCap='round';
      ctx.moveTo(pts[0].x,pts[0].y);
      for(var i=1;i<pts.length;i++){
        var prev=pts[i-1],cur=pts[i];
        var cpx=(prev.x+cur.x)/2;
        ctx.bezierCurveTo(cpx,prev.y,cpx,cur.y,cur.x,cur.y);
      }
      ctx.stroke();
      
      // Glow
      ctx.save();ctx.filter='blur(6px)';ctx.globalAlpha=.3;
      ctx.beginPath();ctx.strokeStyle=color;ctx.lineWidth=4;
      ctx.moveTo(pts[0].x,pts[0].y);
      for(var i=1;i<pts.length;i++){
        var prev=pts[i-1],cur=pts[i];
        var cpx=(prev.x+cur.x)/2;
        ctx.bezierCurveTo(cpx,prev.y,cpx,cur.y,cur.x,cur.y);
      }
      ctx.stroke();ctx.restore();
      
      // Fill
      var grad=ctx.createLinearGradient(0,pad,0,H);
      grad.addColorStop(0,color.replace(')',',.25)').replace('#','rgba(').replace(/([a-f0-9]{2})([a-f0-9]{2})([a-f0-9]{2})/i,function(m,r,g,b){return parseInt(r,16)+','+parseInt(g,16)+','+parseInt(b,16)}));
      grad.addColorStop(1,'rgba(0,0,0,0)');
      ctx.beginPath();ctx.moveTo(pts[0].x,pts[0].y);
      for(var i=1;i<pts.length;i++){
        var prev=pts[i-1],cur=pts[i];
        var cpx=(prev.x+cur.x)/2;
        ctx.bezierCurveTo(cpx,prev.y,cpx,cur.y,cur.x,cur.y);
      }
      ctx.lineTo(pts[pts.length-1].x,H);ctx.lineTo(pts[0].x,H);ctx.closePath();
      ctx.fillStyle=grad;ctx.fill();
      
      // Dots on last few points
      for(var i=Math.max(0,pts.length-5);i<pts.length;i++){
        ctx.beginPath();ctx.arc(pts[i].x,pts[i].y,3,0,Math.PI*2);
        ctx.fillStyle=color;ctx.fill();
        ctx.strokeStyle='rgba(0,0,0,.3)';ctx.lineWidth=1;ctx.stroke();
      }
      
      // Current value label
      var last=pts[pts.length-1];
      ctx.fillStyle=color;ctx.font='bold 12px sans-serif';ctx.textAlign='left';
      ctx.fillText(data[data.length-1].toFixed(1),last.x+8,last.y+4);
    }
    
    function fmt(ms){
      var s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sc=s%60;
      if(h>99)return Math.floor(h/24)+'d '+h%24+'h';
      return(h<10?'0':'')+h+':'+(m<10?'0':'')+m+':'+(sc<10?'0':'')+sc;
    }
    
    var fails=0;
    function upd(){
      fetch('/data').then(function(r){return r.json();}).then(function(d){
        document.getElementById('temp').innerText=d.temperature.toFixed(1);
        document.getElementById('hum').innerText=d.humidity.toFixed(1);
        document.getElementById('uptime').innerText=fmt(d.uptime);
        document.getElementById('ulabel').innerText=d.uptime>86400000?'days':'HH:MM:SS';
        document.getElementById('batt').innerText=d.battery+'%';
        document.getElementById('battinfo').innerText=d.battV+'V';
        if(d.battery<20)document.getElementById('batt').style.color='#ff4757';
        else if(d.battery<50)document.getElementById('batt').style.color='#ffa502';
        else document.getElementById('batt').style.color='#ffd93d';
        document.getElementById('lut').innerText=new Date().toLocaleTimeString();
        document.getElementById('dot').className='sd on';
        document.getElementById('stxt').innerText='Connected';
        TD.push(d.temperature);HD.push(d.humidity);
        if(TD.length>40){TD.shift();HD.shift();}
        drawChart('tc',TD,'#ff6b6b','tmin','tavg','tmax');
        drawChart('hc',HD,'#4ecdc4','hmin','havg','hmax');
        fails=0;
      }).catch(function(){
        fails++;if(fails>3){document.getElementById('dot').className='sd off';document.getElementById('stxt').innerText='Disconnected';}
      });
    }
    setInterval(upd,1000);upd();
  </script>
</body>
</html>
)rawliteral";

// --- Web Server Request Handlers ---
void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

void handleData() {
  String json = "{\"temperature\":" + String(temperature, 2) + 
                ",\"humidity\":" + String(humidity, 2) + 
                ",\"uptime\":" + String(millis()) + 
                ",\"battery\":" + String(batteryPercent) +
                ",\"battV\":" + String(batteryVoltage, 2) + "}";
  server.send(200, "application/json", json);
}

void resetWiFi() {
  WiFi.disconnect(true);
  delay(500);
}

// --- Buzzer Functions ---
void buzzerBeep(int freq, int duration) {
  if (volume == 0) return;
  int vol = map(volume, 0, 100, 0, 255);
  ledcWrite(BUZZER_PIN, vol);
  delay(duration);
  ledcWrite(BUZZER_PIN, 0);
}

void buzzerTone(int freq, int duration) {
  if (volume == 0) return;
  ledcWriteTone(BUZZER_PIN, freq);
  delay(duration);
  ledcWrite(BUZZER_PIN, 0);
}

void buzzerClick() {
  buzzerTone(2000, 20);
}

void buzzerAlarm() {
  for (int i = 0; i < 3; i++) {
    buzzerTone(1000, 100);
    delay(50);
  }
}

// --- Battery Functions ---
float readBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  // ESP32-C3 ADC: 12-bit (0-4095), reference ~3.3V
  float voltage = (raw / 4095.0) * 3.3 * BATTERY_DIVIDER_RATIO;
  return voltage;
}

int getBatteryPercent(float voltage) {
  if (voltage >= BATTERY_MAX_VOLTAGE) return 100;
  if (voltage <= BATTERY_MIN_VOLTAGE) return 0;
  // Linear approximation for 18650
  int percent = (int)((voltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE) * 100);
  return constrain(percent, 0, 100);
}

// --- OLED Graph Drawing ---
void drawOLEDGraph(float *data, int len, bool full, float minVal, float maxVal, const char* label, uint16_t color, int count) {
  display.clearDisplay();
  
  // Title with current value
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(label);
  
  // Current value top right
  int latestIdx = (historyIndex - 1 + len) % len;
  display.setCursor(100, 0);
  display.print(data[latestIdx], 1);
  
  // Graph area
  int graphY = 12;
  int graphH = 50;
  int graphW = 126;
  
  // Border
  display.drawRect(0, graphY, graphW + 2, graphH + 2, SSD1306_WHITE);
  
  // Draw data
  if (count < 2) {
    display.display();
    return;
  }
  
  float range = maxVal - minVal;
  if (range < 0.1) range = 0.1;
  
  int startIdx = full ? (historyIndex - count + len) % len : 0;
  
  for (int i = 1; i < count && i < graphW; i++) {
    int idx1 = (startIdx + i - 1) % len;
    int idx2 = (startIdx + i) % len;
    
    int x1 = i;
    int x2 = i + 1;
    int y1 = graphY + graphH - (int)((data[idx1] - minVal) / range * (graphH - 2));
    int y2 = graphY + graphH - (int)((data[idx2] - minVal) / range * (graphH - 2));
    
    y1 = constrain(y1, graphY + 1, graphY + graphH - 1);
    y2 = constrain(y2, graphY + 1, graphY + graphH - 1);
    
    display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
  }
  
  // Min/Max labels
  display.setTextSize(1);
  display.setCursor(graphW + 4, graphY);
  display.print(maxVal, 0);
  display.setCursor(graphW + 4, graphY + graphH - 8);
  display.print(minVal, 0);
  
  display.display();
}

// --- Handle Button Presses ---
void handleButtons() {
  unsigned long now = millis();
  
  // Volume Up
  if (digitalRead(BTN_VOL_UP) == LOW && now - lastBtnPress[0] > DEBOUNCE_MS) {
    lastBtnPress[0] = now;
    volume = min(100, volume + 10);
    volumeShowTime = now;
    buzzerClick();
    Serial.print("Volume: "); Serial.println(volume);
  }
  
  // Volume Down
  if (digitalRead(BTN_VOL_DOWN) == LOW && now - lastBtnPress[1] > DEBOUNCE_MS) {
    lastBtnPress[1] = now;
    volume = max(0, volume - 10);
    volumeShowTime = now;
    buzzerClick();
    Serial.print("Volume: "); Serial.println(volume);
  }
  
  // Temp Graph Button - toggle graph
  if (digitalRead(BTN_TEMP) == LOW && now - lastBtnPress[2] > DEBOUNCE_MS) {
    lastBtnPress[2] = now;
    displayMode = (displayMode == MODE_TEMP_GRAPH) ? MODE_NORMAL : MODE_TEMP_GRAPH;
    buzzerClick();
  }
  
  // Humidity Graph Button - toggle graph
  if (digitalRead(BTN_HUM) == LOW && now - lastBtnPress[3] > DEBOUNCE_MS) {
    lastBtnPress[3] = now;
    displayMode = (displayMode == MODE_HUM_GRAPH) ? MODE_NORMAL : MODE_HUM_GRAPH;
    buzzerClick();
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize Buttons (internal pull-up)
  pinMode(BTN_VOL_UP, INPUT_PULLUP);
  pinMode(BTN_VOL_DOWN, INPUT_PULLUP);
  pinMode(BTN_TEMP, INPUT_PULLUP);
  pinMode(BTN_HUM, INPUT_PULLUP);
  
  // Initialize Battery ADC
  analogReadResolution(12);
  pinMode(BATTERY_PIN, INPUT);
  
  // Initialize Buzzer PWM
  ledcAttach(BUZZER_PIN, 5000, 8);  // 5kHz, 8-bit resolution
  
  // Init history arrays (3600 = 1 hour at 1s intervals)
  for (int i = 0; i < GRAPH_HISTORY; i++) {
    tempHistory[i] = 0;
    humHistory[i] = 0;
  }

  // Initialize Sensors & Displays
  dht.begin();
  Wire.begin(SDA_PIN, SCL_PIN);

  // I2C Scanner - check Serial for detected address
  Serial.println("\nI2C Scanner:");
  byte found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  Found device at 0x");
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) Serial.println("  No I2C devices found!");
  Serial.println();

  // Try both common addresses
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOk) {
    Serial.println("OLED at 0x3C failed, trying 0x3D...");
    oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  }
  if (!oledOk) {
    Serial.println(F("OLED initialization failed - check wiring!"));
  }

  // Show Initial Connecting Message on OLED
  if (oledOk) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 10);
    display.println("Connecting Wi-Fi...");
    display.display();
  }

  // Connect to Wi-Fi - try each network in the list
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setAutoReconnect(true);
  
  bool connected = false;
  for (int i = 0; i < wifiCount && !connected; i++) {
    currentWifiIndex = i;
    Serial.print("Trying: ");
    Serial.println(wifiList[i].ssid);
    WiFi.begin(wifiList[i].ssid, wifiList[i].password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      Serial.print("Connected to: ");
      Serial.println(wifiList[i].ssid);
    } else {
      Serial.print("Failed: ");
      Serial.println(wifiList[i].ssid);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi Failed! Continuing without connection.");
  }

  // Show connection result on OLED
  if (oledOk) {
    display.clearDisplay();
    display.setCursor(0, 0);
    if (WiFi.status() == WL_CONNECTED) {
      display.println("Wi-Fi Connected!");
      display.setCursor(0, 20);
      display.println("Web Dashboard IP:");
      display.setTextSize(1);
      display.setCursor(0, 40);
      display.println(WiFi.localIP());
    } else {
      display.println("Wi-Fi Failed!");
      display.setCursor(0, 20);
      display.println("Running standalone");
    }
    display.display();
  }
  delay(2000);

  // Web Server Routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("HTTP server started");
  Serial.print("Open: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Handle button presses
  handleButtons();
  
  // Auto-retry Wi-Fi if disconnected (cycle through networks)
  wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus == WL_DISCONNECTED || wifiStatus == WL_CONNECT_FAILED || 
      wifiStatus == WL_CONNECTION_LOST || wifiStatus == WL_NO_SSID_AVAIL) {
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 15000) {
      lastRetry = millis();
      currentWifiIndex = (currentWifiIndex + 1) % wifiCount;
      Serial.print("Retrying Wi-Fi: ");
      Serial.println(wifiList[currentWifiIndex].ssid);
      resetWiFi();
      WiFi.begin(wifiList[currentWifiIndex].ssid, wifiList[currentWifiIndex].password);
    }
  }

  // Listen for HTTP requests from computers on the network
  server.handleClient();

  // Timer logic: Read sensor and update OLED every 1000ms
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Read battery
    batteryVoltage = readBatteryVoltage();
    batteryPercent = getBatteryPercent(batteryVoltage);
    
    // Low battery alarm
    if (batteryVoltage < BATTERY_LOW_VOLTAGE && batteryVoltage > 2.5) {
      if (!lowBatteryAlarmActive || millis() - lastLowBattBeep > LOW_BATT_BEEP_INTERVAL) {
        lowBatteryAlarmActive = true;
        lastLowBattBeep = millis();
        buzzerTone(800, 100);
        delay(50);
        buzzerTone(600, 100);
        delay(50);
        buzzerTone(400, 150);
      }
    } else {
      lowBatteryAlarmActive = false;
    }

    // Read values
    float newTemp = dht.readTemperature();
    float newHum = dht.readHumidity();

    if (!isnan(newTemp) && !isnan(newHum)) {
      temperature = newTemp;
      humidity = newHum;
      
      // Store in history
      tempHistory[historyIndex] = temperature;
      humHistory[historyIndex] = humidity;
      historyIndex = (historyIndex + 1) % GRAPH_HISTORY;
      if (historyIndex == 0) historyFull = true;
      
      // Temperature alarm
      if (temperature > TEMP_ALARM_THRESHOLD) {
        if (!tempAlarmActive || millis() - lastAlarmBeep > ALARM_BEEP_INTERVAL) {
          tempAlarmActive = true;
          lastAlarmBeep = millis();
          buzzerTone(1500, 200);
          delay(50);
          buzzerTone(1500, 200);
        }
      } else {
        tempAlarmActive = false;
      }

      // Update OLED based on display mode
      if (oledOk) {
        switch (displayMode) {
          case MODE_NORMAL: {
            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(0, 0);
            display.print("IP: ");
            display.println(WiFi.localIP());
            
            // Battery indicator top right
            display.setCursor(100, 0);
            display.print(batteryPercent);
            display.print("%");
            
            display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
            
            display.setCursor(0, 16);
            display.print("Temp: ");
            display.setTextSize(2);
            display.print(temperature, 1);
            display.setTextSize(1);
            display.print("C");
            
            display.setCursor(0, 44);
            display.print("Hum:  ");
            display.setTextSize(2);
            display.print(humidity, 1);
            display.setTextSize(1);
            display.print("%");
            
            // Show volume overlay when button pressed
            if (millis() - volumeShowTime < VOLUME_SHOW_DURATION) {
              display.fillRoundRect(30, 20, 68, 24, 4, SSD1306_WHITE);
              display.setTextColor(SSD1306_BLACK);
              display.setTextSize(2);
              display.setCursor(38, 24);
              display.print("V:");
              display.print(volume);
              display.setTextColor(SSD1306_WHITE);
            }
            
            display.display();
            break;
          }
          case MODE_TEMP_GRAPH: {
            int count = historyFull ? GRAPH_HISTORY : historyIndex;
            if (count < 2) break;
            
            int startIdx = historyFull ? historyIndex : 0;
            float tMin = tempHistory[startIdx], tMax = tempHistory[startIdx];
            for (int i = 1; i < count; i++) {
              int idx = (startIdx + i) % GRAPH_HISTORY;
              if (tempHistory[idx] < tMin) tMin = tempHistory[idx];
              if (tempHistory[idx] > tMax) tMax = tempHistory[idx];
            }
            float margin = (tMax - tMin) * 0.1;
            if (margin < 1) margin = 1;
            drawOLEDGraph(tempHistory, GRAPH_HISTORY, historyFull, tMin - margin, tMax + margin, "TEMP GRAPH", SSD1306_WHITE, count);
            break;
          }
          case MODE_HUM_GRAPH: {
            int count = historyFull ? GRAPH_HISTORY : historyIndex;
            if (count < 2) break;
            
            int startIdx = historyFull ? historyIndex : 0;
            float hMin = humHistory[startIdx], hMax = humHistory[startIdx];
            for (int i = 1; i < count; i++) {
              int idx = (startIdx + i) % GRAPH_HISTORY;
              if (humHistory[idx] < hMin) hMin = humHistory[idx];
              if (humHistory[idx] > hMax) hMax = humHistory[idx];
            }
            float margin = (hMax - hMin) * 0.1;
            if (margin < 1) margin = 1;
            drawOLEDGraph(humHistory, GRAPH_HISTORY, historyFull, hMin - margin, hMax + margin, "HUM GRAPH", SSD1306_WHITE, count);
            break;
          }
        }
      }
    }
  }
}