#include "webserver.h"
#include <Preferences.h>
#include <ctime>
#include <vector>
#include "../../battery/BATTERIES.h"
#include "../../battery/Battery.h"
#include "../../battery/Shunt.h"
#include "../../charger/CHARGERS.h"
#include "../../communication/can/comm_can.h"
#include "../../communication/contactorcontrol/comm_contactorcontrol.h"
#include "../../communication/equipmentstopbutton/comm_equipmentstopbutton.h"
#include "../../communication/nvm/comm_nvm.h"
#include "../../datalayer/datalayer.h"
#include "../../datalayer/datalayer_extended.h"
#include "../../devboard/safety/safety.h"
#include "../../inverter/INVERTERS.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../sdcard/sdcard.h"
#include "../utils/events.h"
#include "../utils/led_handler.h"
#include "../utils/timer.h"
#include "esp_task_wdt.h"
#include "html_escape.h"

#include <string>
extern std::string http_username;
extern std::string http_password;

bool webserver_auth = false;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Measure OTA progress
unsigned long ota_progress_millis = 0;

#include "api_json.h"

#include "advanced_battery_html.h"
#include "can_logging_html.h"
#include "can_replay_html.h"
#include "cellmonitor_html.h"
#include "debug_logging_html.h"
#include "events_html.h"
#include "index_html.h"
#include "settings_html.h"

MyTimer ota_timeout_timer = MyTimer(15000);
bool ota_active = false;

const char get_firmware_info_html[] = R"rawliteral(%X%)rawliteral";

static const char dashboard_html[] = R"rawliteral(<!doctype html><html><head>
<meta charset="utf-8"><title>Battery Emulator</title>
<meta name="viewport" content="width=device-width">
<style>
html{font-family:Arial;display:inline-block;text-align:center}
h2{font-size:1.2em;margin:0.3em 0 0.5em 0}
h4{margin:0.6em 0;line-height:1.2}
body{max-width:800px;margin:0 auto;background:#000;color:#fff}
button{background:#505E67;color:#fff;border:none;padding:10px 20px;margin-bottom:20px;cursor:pointer;border-radius:10px}
button:hover{background:#3A4A52}
.block{padding:10px;margin-bottom:10px;border-radius:50px}
.info-block{background:#303E47}
.proto-block{background:#333}
.status-block{background:#333}
.charger-block{background:#FF6E00}
.bat-ok{background:#2D3F2F}
.bat-warn{background:#F5CC00}
.bat-error{background:#A70107}
.bat-updating{background:#2B35AF}
.bat-flex{display:flex;width:100%}
.bat-flex>.bat-block{flex:1}
.tooltip .tooltiptext{visibility:hidden;width:200px;background:#3A4A52;color:#fff;text-align:center;border-radius:6px;padding:8px;position:absolute;z-index:1;margin-left:-100px;opacity:0;transition:opacity 0.3s;font-size:0.9em;font-weight:normal;line-height:1.4}
.tooltip:hover .tooltiptext{visibility:visible;opacity:1}
.tooltip-icon{color:#505E67;cursor:help}
#bat2-block,#bat3-block{display:none}
</style>
</head><body>
<h2>Battery Emulator</h2>

<div class="block info-block">
 <h4 id="sw-hw"></h4>
 <h4 id="uptime"></h4>
 <h4 id="wifi-info"></h4>
 <h4 id="wifi-extra"></h4>
</div>

<div id="proto-block" class="block proto-block" style="display:none">
 <h4 id="proto-inverter" style="color:white;display:none"></h4>
 <h4 id="proto-battery" style="color:white;display:none"></h4>
 <h4 id="proto-shunt" style="color:white;display:none"></h4>
 <h4 id="proto-charger" style="color:white;display:none"></h4>
</div>

<div id="bat-container">
 <div id="bat1-block" class="block bat-ok" style="display:none">
  <h4 id="bat1-soc" style="color:white"></h4>
  <h4 id="bat1-soh" style="color:white"></h4>
  <h4 id="bat1-vi" style="color:white"></h4>
  <h4 id="bat1-power" style="color:white"></h4>
  <h4 id="bat1-totalcap" style="color:white"></h4>
  <h4 id="bat1-remcap" style="color:white"></h4>
  <h4 id="bat1-maxdischg" style="color:white"></h4>
  <h4 id="bat1-maxchg" style="color:white"></h4>
  <h4 id="bat1-maxdischg-i" style="color:white"></h4>
  <h4 id="bat1-maxchg-i" style="color:white"></h4>
  <h4 id="bat1-cells"></h4>
  <h4 id="bat1-delta"></h4>
  <h4 id="bat1-temps"></h4>
  <h4 id="bat1-status"></h4>
  <h4 id="bat1-real-bms" style="display:none"></h4>
  <h4 id="bat1-activity"></h4>
 </div>

 <div id="bat2-block" class="block bat-ok" style="display:none">
  <h4 id="bat2-soc" style="color:white"></h4>
  <h4 id="bat2-soh" style="color:white"></h4>
  <h4 id="bat2-vi" style="color:white"></h4>
  <h4 id="bat2-power" style="color:white"></h4>
  <h4 id="bat2-totalcap" style="color:white"></h4>
  <h4 id="bat2-remcap" style="color:white"></h4>
  <h4 id="bat2-maxdischg" style="color:white"></h4>
  <h4 id="bat2-maxchg" style="color:white"></h4>
  <h4 id="bat2-maxdischg-i" style="color:white"></h4>
  <h4 id="bat2-maxchg-i" style="color:white"></h4>
  <h4 id="bat2-cells"></h4>
  <h4 id="bat2-delta"></h4>
  <h4 id="bat2-temps"></h4>
  <h4 id="bat2-status"></h4>
  <h4 id="bat2-activity"></h4>
 </div>

 <div id="bat3-block" class="block bat-ok" style="display:none">
  <h4 id="bat3-soc" style="color:white"></h4>
  <h4 id="bat3-soh" style="color:white"></h4>
  <h4 id="bat3-vi" style="color:white"></h4>
  <h4 id="bat3-power" style="color:white"></h4>
  <h4 id="bat3-totalcap" style="color:white"></h4>
  <h4 id="bat3-remcap" style="color:white"></h4>
  <h4 id="bat3-maxdischg" style="color:white"></h4>
  <h4 id="bat3-maxchg" style="color:white"></h4>
  <h4 id="bat3-maxdischg-i" style="color:white"></h4>
  <h4 id="bat3-maxchg-i" style="color:white"></h4>
  <h4 id="bat3-cells"></h4>
  <h4 id="bat3-delta"></h4>
  <h4 id="bat3-temps"></h4>
  <h4 id="bat3-status"></h4>
  <h4 id="bat3-activity"></h4>
 </div>
</div>

<div class="block status-block">
 <h4 id="pause-status"></h4>
 <h4 id="contactor-info"></h4>
 <h4 id="bat2-join" style="display:none"></h4>
 <div id="contactor-detail"></div>
</div>

<div id="charger-block" class="block charger-block" style="display:none">
 <h4 id="chg-hv-en"></h4>
 <h4 id="chg-aux-en"></h4>
 <h4 id="chg-power" style="color:white"></h4>
 <h4 id="chg-eff" style="color:white;display:none"></h4>
 <h4 id="chg-hvdc-v" style="color:white"></h4>
 <h4 id="chg-hvdc-i" style="color:white"></h4>
 <h4 id="chg-lvdc-i" style="color:white"></h4>
 <h4 id="chg-lvdc-v" style="color:white"></h4>
 <h4 id="chg-ac-v" style="color:white"></h4>
 <h4 id="chg-ac-i" style="color:white"></h4>
</div>

<div id="buttons"></div>

<script>
var statusBMS=[,"INACTIVE","DARKSTART","OK","FAULT","UPDATING","STANDBY"];
statusBMS[0]="STANDBY";
var realBMS=["DISCONNECTED","STANDBY","OK","FAULT"];
var pauseNames=["Normal","Pausing...","Paused","Resuming..."];
var emulatorColors=["#2D3F2F","#F5CC00","#A70107","#2B35AF"];
var hasBat2=false,hasBat3=false,hasCharger=false;
var infoData=null,isPaused=false,equipStop=false;

function fmtPower(w,unit){
 unit=unit||"";
 if(w>=1000||w<=-1000) return (w/1000).toFixed(1)+" kW"+unit;
 return w.toFixed(0)+" W"+unit;
}

function uptimeStr(s){
 var d=Math.floor(s/86400);s%=86400;
 var h=Math.floor(s/3600);s%=3600;
 var m=Math.floor(s/60);s%=60;
 return d+" days, "+h+" hours, "+m+" minutes, "+s+" seconds";
}

function updateBatBlock(prefix,bat,eStatus,eqStop){
 var el=document.getElementById(prefix+"-block");
 if(!bat){el.style.display="none";return;}
 el.style.display="";
 var color=emulatorColors[eStatus]||"#2D3F2F";
 if(prefix!="bat1") color=(bat.bms_status==3)?"#2D3F2F":(bat.bms_status==4)?"#A70107":"#2D3F2F";
 el.style.backgroundColor=color;

 if(bat.soc_scaling_active)
  document.getElementById(prefix+"-soc").textContent="Scaled SOC: "+bat.soc_reported.toFixed(2)+"% (real: "+bat.soc_real.toFixed(2)+"%)";
 else
  document.getElementById(prefix+"-soc").textContent="SOC: "+bat.soc_real.toFixed(2)+"%";
 document.getElementById(prefix+"-soh").textContent="SOH: "+bat.soh.toFixed(2)+"%";
 document.getElementById(prefix+"-vi").textContent="Voltage: "+bat.voltage_V.toFixed(1)+" V \u00a0 Current: "+bat.current_A.toFixed(1)+" A";
 document.getElementById(prefix+"-power").textContent="Power: "+fmtPower(bat.power_W);

 if(bat.soc_scaling_active){
  document.getElementById(prefix+"-totalcap").textContent="Scaled total capacity: "+fmtPower(bat.reported_total_capacity_Wh,"h")+" (real: "+fmtPower(bat.total_capacity_Wh,"h")+")";
  document.getElementById(prefix+"-remcap").textContent="Scaled remaining capacity: "+fmtPower(bat.reported_remaining_capacity_Wh,"h")+" (real: "+fmtPower(bat.remaining_capacity_Wh,"h")+")";
 } else {
  document.getElementById(prefix+"-totalcap").textContent="Total capacity: "+fmtPower(bat.total_capacity_Wh,"h");
  document.getElementById(prefix+"-remcap").textContent="Remaining capacity: "+fmtPower(bat.remaining_capacity_Wh,"h");
 }

 var dpc=eqStop?"red":"white";
 var dp=document.getElementById(prefix+"-maxdischg");
 dp.textContent="Max discharge power: "+fmtPower(bat.max_discharge_power_W);dp.style.color=dpc;
 var cp=document.getElementById(prefix+"-maxchg");
 cp.textContent="Max charge power: "+fmtPower(bat.max_charge_power_W);cp.style.color=dpc;
 var di=document.getElementById(prefix+"-maxdischg-i");
 di.textContent="Max discharge current: "+bat.max_discharge_current_A.toFixed(1)+" A";di.style.color=dpc;
 var ci=document.getElementById(prefix+"-maxchg-i");
 ci.textContent="Max charge current: "+bat.max_charge_current_A.toFixed(1)+" A";ci.style.color=dpc;

 var delta=bat.cell_max_mV-bat.cell_min_mV;
 document.getElementById(prefix+"-cells").textContent="Cell min/max: "+bat.cell_min_mV+" mV / "+bat.cell_max_mV+" mV";
 var de=document.getElementById(prefix+"-delta");
 de.textContent="Cell delta: "+delta+" mV";de.style.color=(delta>500)?"red":"";
 document.getElementById(prefix+"-temps").textContent="Temperature min/max: "+bat.temp_min_C.toFixed(1)+" \u00b0C / "+bat.temp_max_C.toFixed(1)+" \u00b0C";
 document.getElementById(prefix+"-status").textContent="System status: "+(statusBMS[bat.bms_status]||"??");

 var act;
 if(bat.current_A==0) act="Battery idle";
 else if(bat.current_A<0) act="Battery discharging!";
 else act="Battery charging!";
 document.getElementById(prefix+"-activity").textContent=act;
}

function updateStatus(d){
 var sys=d.system;
 equipStop=sys.equipment_stop;
 isPaused=(sys.pause_status==2);
 document.getElementById("uptime").textContent="Uptime: "+uptimeStr(sys.uptime_s);

 updateBatBlock("bat1",d.battery,sys.emulator_status,equipStop);

 hasBat2=!!d.battery2;
 hasBat3=!!d.battery3;
 hasCharger=!!d.charger;

 if(hasBat2){
  document.getElementById("bat2-block").style.display="";
  updateBatBlock("bat2",d.battery2,sys.emulator_status,equipStop);
 }
 if(hasBat3){
  document.getElementById("bat3-block").style.display="";
  updateBatBlock("bat3",d.battery3,sys.emulator_status,equipStop);
 }

 // Multi-battery flex layout
 var container=document.getElementById("bat-container");
 if(hasBat2){
  container.style.display="flex";container.style.width="100%";
  var kids=container.children;for(var i=0;i<kids.length;i++)kids[i].style.flex="1";
 } else {
  container.style.display="";
  var kids=container.children;for(var i=0;i<kids.length;i++)kids[i].style.flex="";
 }

 // Pause status
 var pe=document.getElementById("pause-status");
 pe.textContent="Power status: "+pauseNames[sys.pause_status];
 pe.style.color=(sys.pause_status==0)?"":"red";

 // Contactor info
 var ci="Emulator allows contactor closing: ";
 ci+=(d.battery&&d.battery.bms_status==4)?"\u2717":"\u2713";
 ci+=" Inverter allows contactor closing: \u2713";
 document.getElementById("contactor-info").innerHTML=ci;

 if(hasBat2){
  var j2=document.getElementById("bat2-join");
  j2.style.display="";
  j2.textContent="Secondary battery allowed to join \u2713";
 }

 // Contactor detail
 var cd=document.getElementById("contactor-detail");
 cd.innerHTML="<h4>Contactors state: "+
  (sys.contactors_engaged==0?"<span style='color:red'>OFF (DISCONNECTED)</span>":
   sys.contactors_engaged==1?"<span style='color:green'>ON</span>":
   sys.contactors_engaged==2?"<span style='color:red'>OFF (FAULT)</span>":
   sys.contactors_engaged==3?"<span style='color:orange'>PRECHARGE</span>":"")+"</h4>";

 // Charger block
 if(d.charger){
  var cb=document.getElementById("charger-block");cb.style.display="";
  document.getElementById("chg-hv-en").innerHTML="Charger HV Enabled: "+(d.charger.HV_enabled?"\u2713":"<span style='color:red'>\u2717</span>");
  document.getElementById("chg-aux-en").innerHTML="Charger Aux12v Enabled: "+(d.charger.aux12V_enabled?"\u2713":"<span style='color:red'>\u2717</span>");
  document.getElementById("chg-power").textContent="Charger Output Power: "+fmtPower(d.charger.output_power_W);
  if(d.charger.efficiency_pct!=null){var ee=document.getElementById("chg-eff");ee.style.display="";ee.textContent="Charger Efficiency: "+d.charger.efficiency_pct.toFixed(0)+"%";}
  document.getElementById("chg-hvdc-v").textContent="Charger HVDC Output V: "+d.charger.HVDC_voltage_V.toFixed(2)+" V";
  document.getElementById("chg-hvdc-i").textContent="Charger HVDC Output I: "+d.charger.HVDC_current_A.toFixed(2)+" A";
  document.getElementById("chg-lvdc-i").textContent="Charger LVDC Output I: "+d.charger.LVDC_current_A.toFixed(2);
  document.getElementById("chg-lvdc-v").textContent="Charger LVDC Output V: "+d.charger.LVDC_voltage_V.toFixed(2);
  document.getElementById("chg-ac-v").textContent="Charger AC Input V: "+d.charger.AC_voltage_V.toFixed(2)+" VAC";
  document.getElementById("chg-ac-i").textContent="Charger AC Input I: "+d.charger.AC_current_A.toFixed(2)+" A";
 }

 // Buttons
 renderButtons();
}

function renderButtons(){
 var b="";
 if(isPaused)
  b+="<button onclick='PauseBattery(false)'>Resume charge/discharge</button> ";
 else
  b+="<button onclick=\"if(confirm('Are you sure you want to pause charging and discharging? This will set the maximum charge and discharge values to zero, preventing any further power flow.')) { PauseBattery(true); }\">Pause charge/discharge</button> ";
 b+="<button onclick='go(\"/update\")'>Perform OTA update</button> ";
 b+="<button onclick='go(\"/settings\")'>Change Settings</button> ";
 b+="<button onclick='go(\"/advanced\")'>More Battery Info</button> ";
 b+="<button onclick='go(\"/canlog\")'>CAN logger</button> ";
 b+="<button onclick='go(\"/canreplay\")'>CAN replay</button> ";
 b+="<button onclick='go(\"/cellmonitor\")'>Cellmonitor</button> ";
 b+="<button onclick='go(\"/events\")'>Events</button> ";
 b+="<button onclick='askReboot()'>Reboot Emulator</button>";
 if(!equipStop)
  b+="<br/><button style='background:red;color:white;cursor:pointer' onclick=\"if(confirm('This action will attempt to open contactors on the battery. Are you sure?')){estop(true)}\">Open Contactors</button><br/>";
 else
  b+="<br/><button style='background:green;color:white;cursor:pointer' onclick=\"if(confirm('This action will attempt to close contactors and enable power transfer. Are you sure?')){estop(false)}\">Close Contactors</button><br/>";
 document.getElementById("buttons").innerHTML=b;
}

function updateInfo(d){
 infoData=d;
 var t="Software: "+d.firmware+" Hardware: "+d.hardware+" @ "+d.cpu_temp_C.toFixed(1)+" \u00b0C";
 document.getElementById("sw-hw").textContent=t;

 var protoBlock=document.getElementById("proto-block");
 var hasProto=false;
 if(d.inverter_protocol){document.getElementById("proto-inverter").style.display="";document.getElementById("proto-inverter").textContent="Inverter protocol: "+d.inverter_protocol;hasProto=true;}
 if(d.battery_protocol){document.getElementById("proto-battery").style.display="";document.getElementById("proto-battery").textContent="Battery protocol: "+d.battery_protocol;hasProto=true;}
 if(d.shunt_protocol){document.getElementById("proto-shunt").style.display="";document.getElementById("proto-shunt").textContent="Shunt protocol: "+d.shunt_protocol;hasProto=true;}
 if(d.charger_protocol){document.getElementById("proto-charger").style.display="";document.getElementById("proto-charger").textContent="Charger protocol: "+d.charger_protocol;hasProto=true;}
 if(hasProto) protoBlock.style.display="";

 var wi="SSID: "+(d.wifi_ssid||"");
 if(d.wifi_rssi!=null) wi+=" RSSI:"+d.wifi_rssi+" dBm";
 document.getElementById("wifi-info").textContent=wi;
 if(d.wifi_ip){
  var ex="";
  if(d.wifi_hostname) ex+="Hostname: "+d.wifi_hostname+" ";
  ex+="IP: "+d.wifi_ip;
  document.getElementById("wifi-extra").textContent=ex;
 }
}

function go(url){window.location.href=url;}
function askReboot(){if(confirm('Are you sure you want to reboot the emulator? NOTE: If emulator is handling contactors, they will open during reboot!'))reboot();}
function reboot(){var x=new XMLHttpRequest();x.open('GET','/reboot',true);x.send();setTimeout(function(){window.location="/";},3000);}
function PauseBattery(p){var x=new XMLHttpRequest();x.onload=function(){fetchStatus();};x.open('GET','/pause?value='+p,true);x.send();}
function estop(s){var x=new XMLHttpRequest();x.onload=function(){fetchStatus();};x.open('GET','/equipmentStop?value='+s,true);x.send();}

function fetchStatus(){
 fetch('/api/status').then(function(r){return r.json();}).then(updateStatus).catch(function(e){console.log('status err',e);});
}
function fetchInfo(){
 fetch('/api/info').then(function(r){return r.json();}).then(updateInfo).catch(function(e){console.log('info err',e);});
}

fetchInfo();
fetchStatus();
setInterval(fetchStatus,5000);
setInterval(fetchInfo,60000);
</script>
</body></html>)rawliteral";

String importedLogs = "";      // Store the uploaded logfile contents in RAM
bool isReplayRunning = false;  // Global flag to track replay state

// True when user has updated settings that need a reboot to be effective.
bool settingsUpdated = false;

CAN_frame currentFrame = {.FD = true, .ext_ID = false, .DLC = 64, .ID = 0x12F, .data = {0}};

void handleFileUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len,
                      bool final) {
  if (!index) {
    importedLogs = "";  // Clear previous logs
    logging.printf("Receiving file: %s\n", filename.c_str());
  }

  // Append received data to the string (RAM storage)
  importedLogs += String((char*)data).substring(0, len);

  if (final) {
    logging.println("Upload Complete!");
    request->send(200, "text/plain", "File uploaded successfully");
  }
}

void canReplayTask(void* param) {
  std::vector<String> messages;
  messages.reserve(1000);  // Pre-allocate memory to reduce fragmentation

  if (!importedLogs.isEmpty()) {
    int lastIndex = 0;

    while (true) {
      int nextIndex = importedLogs.indexOf("\n", lastIndex);
      if (nextIndex == -1) {
        messages.push_back(importedLogs.substring(lastIndex));
        break;
      }
      messages.push_back(importedLogs.substring(lastIndex, nextIndex));
      lastIndex = nextIndex + 1;
    }

    do {
      float firstTimestamp = -1.0f;
      float lastTimestamp = 0.0f;
      bool firstMessageSent = false;  // Track first message

      for (size_t i = 0; i < messages.size(); i++) {
        String line = messages[i];
        line.trim();
        if (line.length() == 0)
          continue;

        int timeStart = line.indexOf("(") + 1;
        int timeEnd = line.indexOf(")");
        if (timeStart == 0 || timeEnd == -1)
          continue;

        float currentTimestamp = line.substring(timeStart, timeEnd).toFloat();

        if (firstTimestamp < 0) {
          firstTimestamp = currentTimestamp;
        }

        // Send first message immediately
        if (!firstMessageSent) {
          firstMessageSent = true;
          firstTimestamp = currentTimestamp;  // Adjust reference time
        } else {
          // Delay only if this isn't the first message
          float deltaT = (currentTimestamp - lastTimestamp) * 1000;
          vTaskDelay((int)deltaT / portTICK_PERIOD_MS);
        }

        lastTimestamp = currentTimestamp;

        int interfaceStart = timeEnd + 2;
        int interfaceEnd = line.indexOf(" ", interfaceStart);
        if (interfaceEnd == -1)
          continue;

        int idStart = interfaceEnd + 1;
        int idEnd = line.indexOf(" [", idStart);
        if (idStart == -1 || idEnd == -1)
          continue;

        String messageID = line.substring(idStart, idEnd);
        int dlcStart = idEnd + 2;
        int dlcEnd = line.indexOf("]", dlcStart);
        if (dlcEnd == -1)
          continue;

        String dlc = line.substring(dlcStart, dlcEnd);
        int dataStart = dlcEnd + 2;
        String dataBytes = line.substring(dataStart);

        currentFrame.ID = strtol(messageID.c_str(), NULL, 16);
        currentFrame.DLC = dlc.toInt();

        int byteIndex = 0;
        char* token = strtok((char*)dataBytes.c_str(), " ");
        while (token != NULL && byteIndex < currentFrame.DLC) {
          currentFrame.data.u8[byteIndex++] = strtol(token, NULL, 16);
          token = strtok(NULL, " ");
        }

        currentFrame.FD = (datalayer.system.info.can_replay_interface == CANFD_NATIVE) ||
                          (datalayer.system.info.can_replay_interface == CANFD_ADDON_MCP2518);
        currentFrame.ext_ID = (currentFrame.ID > 0x7F0);

        transmit_can_frame_to_interface(&currentFrame, (CAN_Interface)datalayer.system.info.can_replay_interface);
      }
    } while (datalayer.system.info.loop_playback);

    messages.clear();          // Free vector memory
    messages.shrink_to_fit();  // Release excess memory
  }

  isReplayRunning = false;  // Mark replay as stopped
  vTaskDelete(NULL);
}

void def_route_with_auth(const char* uri, AsyncWebServer& serv, WebRequestMethodComposite method,
                         std::function<void(AsyncWebServerRequest*)> handler) {
  serv.on(uri, method, [handler](AsyncWebServerRequest* request) {
    if (webserver_auth && !request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    handler(request);
  });
}

void init_webserver() {

  server.on("/logout", HTTP_GET, [](AsyncWebServerRequest* request) { request->send(401); });

  // Route for firmware info from ota update page
  def_route_with_auth("/GetFirmwareInfo", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json", get_firmware_info_html, get_firmware_info_processor);
  });

  // Route for root / web page - static dashboard with JS fetch from /api/*
  def_route_with_auth("/", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    // Clear OTA active flag as a safeguard in case onOTAEnd() wasn't called
    ota_active = false;
    request->send(200, "text/html", dashboard_html);
  });

  // Route for going to settings web page
  def_route_with_auth("/settings", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    // Using make_shared to ensure lifetime for the settings object during send() lambda execution
    auto settings = std::make_shared<BatteryEmulatorSettingsStore>(true);

    request->send(200, "text/html", settings_html,
                  [settings](const String& content) { return settings_processor(content, *settings); });
  });

  // Route for going to advanced battery info web page
  def_route_with_auth("/advanced", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", index_html, advanced_battery_processor);
  });

  // Route for going to CAN logging web page
  def_route_with_auth("/canlog", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(request->beginResponse(200, "text/html", can_logger_processor()));
  });

  // Route for going to CAN replay web page
  def_route_with_auth("/canreplay", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(request->beginResponse(200, "text/html", can_replay_processor()));
  });

  def_route_with_auth("/startReplay", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    // Prevent multiple replay tasks from being created
    if (isReplayRunning) {
      request->send(400, "text/plain", "Replay already running!");
      return;
    }

    datalayer.system.info.loop_playback = request->hasParam("loop") && request->getParam("loop")->value().toInt() == 1;
    isReplayRunning = true;  // Set flag before starting task

    xTaskCreatePinnedToCore(canReplayTask, "CAN_Replay", 8192, NULL, 1, NULL, 1);

    request->send(200, "text/plain", "CAN replay started!");
  });

  // Route for stopping the CAN replay
  def_route_with_auth("/stopReplay", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    datalayer.system.info.loop_playback = false;

    request->send(200, "text/plain", "CAN replay stopped!");
  });

  // Route to handle setting the CAN interface for CAN replay
  def_route_with_auth("/setCANInterface", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("interface")) {
      String canInterface = request->getParam("interface")->value();

      // Convert the received value to an integer
      int interfaceValue = canInterface.toInt();

      // Update the datalayer with the selected interface
      datalayer.system.info.can_replay_interface = interfaceValue;

      // Respond with success message
      request->send(200, "text/plain", "New interface selected");
    } else {
      request->send(400, "text/plain", "Error: updating interface failed");
    }
  });

  if (datalayer.system.info.web_logging_active || datalayer.system.info.SD_logging_active) {
    // Route for going to debug logging web page
    server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request) {
      AsyncWebServerResponse* response = request->beginResponse(200, "text/html", debug_logger_processor());
      request->send(response);
    });
  }

  // Define the handler to stop can logging
  server.on("/stop_can_logging", HTTP_GET, [](AsyncWebServerRequest* request) {
    datalayer.system.info.can_logging_active = false;
    request->send(200, "text/plain", "Logging stopped");
  });

  // Define the handler to import can log
  server.on(
      "/import_can_log", HTTP_POST,
      [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Ready to receive file.");  // Response when request is made
      },
      handleFileUpload);

  if (datalayer.system.info.CAN_SD_logging_active) {
    // Define the handler to export can log
    server.on("/export_can_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      pause_can_writing();
      request->send(SD_MMC, CAN_LOG_FILE, String(), true);
      resume_can_writing();
    });

    // Define the handler to delete can log
    server.on("/delete_can_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      delete_can_log();
      request->send(200, "text/plain", "Log file deleted");
    });
  } else {
    // Define the handler to export can log
    server.on("/export_can_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      String logs = String(datalayer.system.info.logged_can_messages);
      if (logs.length() == 0) {
        logs = "No logs available.";
      }

      // Get the current time
      time_t now = time(nullptr);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);

      // Ensure time retrieval was successful
      char filename[32];
      if (strftime(filename, sizeof(filename), "canlog_%H-%M-%S.txt", &timeinfo)) {
        // Valid filename created
      } else {
        // Fallback filename if automatic timestamping failed
        strcpy(filename, "battery_emulator_can_log.txt");
      }

      // Use request->send with dynamic headers
      AsyncWebServerResponse* response = request->beginResponse(200, "text/plain", logs);
      response->addHeader("Content-Disposition", String("attachment; filename=\"") + String(filename) + "\"");
      request->send(response);
    });
  }

  if (datalayer.system.info.SD_logging_active) {
    // Define the handler to delete log file
    server.on("/delete_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      delete_log();
      request->send(200, "text/plain", "Log file deleted");
    });

    // Define the handler to export debug log
    server.on("/export_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      pause_log_writing();
      request->send(SD_MMC, LOG_FILE, String(), true);
      resume_log_writing();
    });
  } else {
    // Define the handler to export debug log
    server.on("/export_log", HTTP_GET, [](AsyncWebServerRequest* request) {
      String logs = String(datalayer.system.info.logged_can_messages);
      if (logs.length() == 0) {
        logs = "No logs available.";
      }

      // Get the current time
      time_t now = time(nullptr);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);

      // Ensure time retrieval was successful
      char filename[32];
      if (strftime(filename, sizeof(filename), "log_%H-%M-%S.txt", &timeinfo)) {
        // Valid filename created
      } else {
        // Fallback filename if automatic timestamping failed
        strcpy(filename, "battery_emulator_log.txt");
      }

      // Use request->send with dynamic headers
      AsyncWebServerResponse* response = request->beginResponse(200, "text/plain", logs);
      response->addHeader("Content-Disposition", String("attachment; filename=\"") + String(filename) + "\"");
      request->send(response);
    });
  }

  // Route for going to cellmonitor web page
  def_route_with_auth("/cellmonitor", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", cellmonitor_html);
  });

  // Route for going to event log web page
  def_route_with_auth("/events", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", events_html);
  });

  // Route for clearing all events
  def_route_with_auth("/clearevents", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    reset_all_events();
    // Send back a response that includes an instant redirect to /events
    String response = "<html><body>";
    response += "<script>window.location.href = '/events';</script>";  // Instant redirect
    response += "</body></html>";
    request->send(200, "text/html", response);
  });

  def_route_with_auth("/factoryReset", server, HTTP_POST, [](AsyncWebServerRequest* request) {
    // Reset all settings to factory defaults
    BatteryEmulatorSettingsStore settings;
    settings.clearAll();

    request->send(200, "text/html", "OK");
  });

  const char* boolSettingNames[] = {
      "DBLBTR",        "CNTCTRL",      "CNTCTRLDBL",  "PWMCNTCTRL",   "PERBMSRESET",  "SDLOGENABLED", "STATICIP",
      "REMBMSRESET",   "EXTPRECHARGE", "USBENABLED",  "CANLOGUSB",    "WEBENABLED",   "CANFDASCAN",   "CANLOGSD",
      "WIFIAPENABLED", "MQTTENABLED",  "NOINVDISC",   "HADISC",       "MQTTTOPICS",   "MQTTCELLV",    "INVICNT",
      "GTWRHD",        "DIGITALHVIL",  "PERFPROFILE", "INTERLOCKREQ", "SOCESTIMATED", "PYLONOFFSET",  "PYLONORDER",
      "DEYEBYD",       "NCCONTACTOR",  "TRIBTR",      "CNTCTRLTRI",
  };

  const char* uintSettingNames[] = {
      "BATTCVMAX", "BATTCVMIN",  "MAXPRETIME", "MAXPREFREQ", "WIFICHANNEL", "DCHGPOWER", "CHGPOWER",
      "LOCALIP1",  "LOCALIP2",   "LOCALIP3",   "LOCALIP4",   "GATEWAY1",    "GATEWAY2",  "GATEWAY3",
      "GATEWAY4",  "SUBNET1",    "SUBNET2",    "SUBNET3",    "SUBNET4",     "MQTTPORT",  "MQTTTIMEOUT",
      "SOFAR_ID",  "PYLONSEND",  "INVCELLS",   "INVMODULES", "INVCELLSPER", "INVVLEVEL", "INVCAPACITY",
      "INVBTYPE",  "CANFREQ",    "CANFDFREQ",  "PRECHGMS",   "PWMFREQ",     "PWMHOLD",   "GTWCOUNTRY",
      "GTWMAPREG", "GTWCHASSIS", "GTWPACK",    "LEDMODE",    "GPIOOPT1",    "GPIOOPT2",  "GPIOOPT3",
  };

  const char* stringSettingNames[] = {"APNAME",       "APPASSWORD", "HOSTNAME",        "MQTTSERVER",     "MQTTUSER",
                                      "MQTTPASSWORD", "MQTTTOPIC",  "MQTTOBJIDPREFIX", "MQTTDEVICENAME", "HADEVICEID"};

  // Handles the form POST from UI to save settings of the common image
  server.on("/saveSettings", HTTP_POST,
            [boolSettingNames, stringSettingNames, uintSettingNames](AsyncWebServerRequest* request) {
              BatteryEmulatorSettingsStore settings;

              int numParams = request->params();
              for (int i = 0; i < numParams; i++) {
                auto p = request->getParam(i);
                if (p->name() == "inverter") {
                  auto type = static_cast<InverterProtocolType>(atoi(p->value().c_str()));
                  settings.saveUInt("INVTYPE", (int)type);
                } else if (p->name() == "INVCOMM") {
                  auto type = static_cast<comm_interface>(atoi(p->value().c_str()));
                  settings.saveUInt("INVCOMM", (int)type);
                } else if (p->name() == "battery") {
                  auto type = static_cast<BatteryType>(atoi(p->value().c_str()));
                  settings.saveUInt("BATTTYPE", (int)type);
                } else if (p->name() == "BATTCHEM") {
                  auto type = static_cast<battery_chemistry_enum>(atoi(p->value().c_str()));
                  settings.saveUInt("BATTCHEM", (int)type);
                } else if (p->name() == "BATTCOMM") {
                  auto type = static_cast<comm_interface>(atoi(p->value().c_str()));
                  settings.saveUInt("BATTCOMM", (int)type);
                } else if (p->name() == "BATTPVMAX") {
                  auto type = p->value().toFloat() * 10.0f;
                  settings.saveUInt("BATTPVMAX", (int)type);
                } else if (p->name() == "BATTPVMIN") {
                  auto type = p->value().toFloat() * 10.0f;
                  settings.saveUInt("BATTPVMIN", (int)type);
                } else if (p->name() == "charger") {
                  auto type = static_cast<ChargerType>(atoi(p->value().c_str()));
                  settings.saveUInt("CHGTYPE", (int)type);
                } else if (p->name() == "CHGCOMM") {
                  auto type = static_cast<comm_interface>(atoi(p->value().c_str()));
                  settings.saveUInt("CHGCOMM", (int)type);
                } else if (p->name() == "EQSTOP") {
                  auto type = static_cast<STOP_BUTTON_BEHAVIOR>(atoi(p->value().c_str()));
                  settings.saveUInt("EQSTOP", (int)type);
                } else if (p->name() == "BATT2COMM") {
                  auto type = static_cast<comm_interface>(atoi(p->value().c_str()));
                  settings.saveUInt("BATT2COMM", (int)type);
                } else if (p->name() == "BATT3COMM") {
                  auto type = static_cast<comm_interface>(atoi(p->value().c_str()));
                  settings.saveUInt("BATT3COMM", (int)type);
                } else if (p->name() == "shunt") {
                  auto type = static_cast<ShuntType>(atoi(p->value().c_str()));
                  settings.saveUInt("SHUNTTYPE", (int)type);
                } else if (p->name() == "SHUNTCOMM") {
                  auto type = static_cast<comm_interface>(atoi(p->value().c_str()));
                  settings.saveUInt("SHUNTCOMM", (int)type);
                } else if (p->name() == "SSID") {
                  settings.saveString("SSID", p->value().c_str());
                  ssid = settings.getString("SSID", "").c_str();
                } else if (p->name() == "PASSWORD") {
                  settings.saveString("PASSWORD", p->value().c_str());
                  password = settings.getString("PASSWORD", "").c_str();
                } else if (p->name() == "MQTTPUBLISHMS") {
                  auto interval = atoi(p->value().c_str()) * 1000;  // Convert seconds to milliseconds
                  settings.saveUInt("MQTTPUBLISHMS", interval);
                }

                for (auto& uintSetting : uintSettingNames) {
                  if (p->name() == uintSetting) {
                    auto value = atoi(p->value().c_str());
                    if (settings.getUInt(uintSetting, 0) != value) {
                      settings.saveUInt(uintSetting, value);
                    }
                  }
                }

                for (auto& stringSetting : stringSettingNames) {
                  if (p->name() == stringSetting) {
                    if (settings.getString(stringSetting) != p->value()) {
                      settings.saveString(stringSetting, p->value().c_str());
                    }
                  }
                }
              }

              for (auto& boolSetting : boolSettingNames) {
                auto p = request->getParam(boolSetting, true);
                const bool default_value = (std::string(boolSetting) == std::string("WIFIAPENABLED"));
                const bool value = p != nullptr && p->value() == "on";
                if (settings.getBool(boolSetting, default_value) != value) {
                  settings.saveBool(boolSetting, value);
                }
              }

              settingsUpdated = settings.were_settings_updated();
              request->redirect("/settings");
            });

  auto update_string = [](const char* route, std::function<void(String)> setter,
                          std::function<bool(String)> validator = nullptr) {
    def_route_with_auth(route, server, HTTP_GET, [=](AsyncWebServerRequest* request) {
      if (request->hasParam("value")) {
        String value = request->getParam("value")->value();

        if (validator && !validator(value)) {
          request->send(400, "text/plain", "Invalid value");
          return;
        }

        setter(value);
        request->send(200, "text/plain", "Updated successfully");
      } else {
        request->send(400, "text/plain", "Bad Request");
      }
    });
  };

  auto update_string_setting = [=](const char* route, std::function<void(String)> setter,
                                   std::function<bool(String)> validator = nullptr) {
    update_string(
        route,
        [setter](String value) {
          setter(value);
          store_settings();
        },
        validator);
  };

  auto update_int_setting = [=](const char* route, std::function<void(int)> setter) {
    update_string_setting(route, [setter](String value) { setter(value.toInt()); });
  };

  // Route for editing Wh
  update_int_setting("/updateBatterySize", [](int value) { datalayer.battery.info.total_capacity_Wh = value; });

  // Route for editing USE_SCALED_SOC
  update_int_setting("/updateUseScaledSOC", [](int value) { datalayer.battery.settings.soc_scaling_active = value; });

  // Route for enabling recovery mode charging
  update_int_setting("/enableRecoveryMode",
                     [](int value) { datalayer.battery.settings.user_requests_forced_charging_recovery_mode = value; });

  // Route for editing SOCMax
  update_string_setting("/updateSocMax", [](String value) {
    datalayer.battery.settings.max_percentage = static_cast<uint16_t>(value.toFloat() * 100);
  });

  // Route for editing CAN ID cutoff filter
  update_int_setting("/set_can_id_cutoff", [](int value) { user_selected_CAN_ID_cutoff_filter = value; });

  // Route for pause/resume Battery emulator
  update_string("/pause", [](String value) { setBatteryPause(value == "true" || value == "1", false); });

  // Route for equipment stop/resume
  update_string("/equipmentStop", [](String value) {
    if (value == "true" || value == "1") {
      setBatteryPause(true, false, true);  //Pause battery, do not pause CAN, equipment stop on (store to flash)
    } else {
      setBatteryPause(false, false, false);
    }
  });

  // Route for editing SOCMin
  update_string_setting("/updateSocMin", [](String value) {
    datalayer.battery.settings.min_percentage = static_cast<uint16_t>(value.toFloat() * 100);
  });

  // Route for editing MaxChargeA
  update_string_setting("/updateMaxChargeA", [](String value) {
    datalayer.battery.settings.max_user_set_charge_dA = static_cast<uint16_t>(value.toFloat() * 10);
  });

  // Route for editing MaxDischargeA
  update_string_setting("/updateMaxDischargeA", [](String value) {
    datalayer.battery.settings.max_user_set_discharge_dA = static_cast<uint16_t>(value.toFloat() * 10);
  });

  for (const auto& cmd : battery_commands) {
    auto route = String("/") + cmd.identifier;
    server.on(
        route.c_str(), HTTP_PUT,
        [cmd](AsyncWebServerRequest* request) {
          if (webserver_auth && !request->authenticate(http_username.c_str(), http_password.c_str())) {
            return request->requestAuthentication();
          }
        },
        nullptr,
        [cmd](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
          String battIndex = "";
          if (len > 0) {
            battIndex += (char)data[0];
          }
          Battery* batt = battery;
          if (battIndex == "1") {
            batt = battery2;
          }
          if (battIndex == "2") {
            batt = battery3;
          }
          if (batt) {
            cmd.action(batt);
          }
          request->send(200, "text/plain", "Command performed.");
        });
  }

  // Route for editing BATTERY_USE_VOLTAGE_LIMITS
  update_int_setting("/updateUseVoltageLimit",
                     [](int value) { datalayer.battery.settings.user_set_voltage_limits_active = value; });

  // Route for editing MaxChargeVoltage
  update_string_setting("/updateMaxChargeVoltage", [](String value) {
    datalayer.battery.settings.max_user_set_charge_voltage_dV = static_cast<uint16_t>(value.toFloat() * 10);
  });

  // Route for editing MaxDischargeVoltage
  update_string_setting("/updateMaxDischargeVoltage", [](String value) {
    datalayer.battery.settings.max_user_set_discharge_voltage_dV = static_cast<uint16_t>(value.toFloat() * 10);
  });

  // Route for editing BMSresetDuration
  update_string_setting("/updateBMSresetDuration", [](String value) {
    datalayer.battery.settings.user_set_bms_reset_duration_ms = static_cast<uint16_t>(value.toFloat() * 1000);
  });

  // Route for editing FakeBatteryVoltage
  update_string_setting("/updateFakeBatteryVoltage", [](String value) { battery->set_fake_voltage(value.toFloat()); });

  // Route for editing balancing enabled
  update_int_setting("/TeslaBalAct", [](int value) { datalayer.battery.settings.user_requests_balancing = value; });

  // Route for editing balancing max time
  update_string_setting("/BalTime", [](String value) {
    datalayer.battery.settings.balancing_max_time_ms = static_cast<uint32_t>(value.toFloat() * 60000);
  });

  // Route for editing balancing max power
  update_string_setting("/BalFloatPower", [](String value) {
    datalayer.battery.settings.balancing_float_power_W = static_cast<uint16_t>(value.toFloat());
  });

  // Route for editing balancing max pack voltage
  update_string_setting("/BalMaxPackV", [](String value) {
    datalayer.battery.settings.balancing_max_pack_voltage_dV = static_cast<uint16_t>(value.toFloat() * 10);
  });

  // Route for editing balancing max cell voltage
  update_string_setting("/BalMaxCellV", [](String value) {
    datalayer.battery.settings.balancing_max_cell_voltage_mV = static_cast<uint16_t>(value.toFloat());
  });

  // Route for editing balancing max cell voltage deviation
  update_string_setting("/BalMaxDevCellV", [](String value) {
    datalayer.battery.settings.balancing_max_deviation_cell_voltage_mV = static_cast<uint16_t>(value.toFloat());
  });

  if (charger) {
    // Route for editing ChargerTargetV
    update_string_setting(
        "/updateChargeSetpointV", [](String value) { datalayer.charger.charger_setpoint_HV_VDC = value.toFloat(); },
        [](String value) {
          float val = value.toFloat();
          return (val <= CHARGER_MAX_HV && val >= CHARGER_MIN_HV) &&
                 (val * datalayer.charger.charger_setpoint_HV_IDC <= CHARGER_MAX_POWER);
        });

    // Route for editing ChargerTargetA
    update_string_setting(
        "/updateChargeSetpointA", [](String value) { datalayer.charger.charger_setpoint_HV_IDC = value.toFloat(); },
        [](String value) {
          float val = value.toFloat();
          return (val <= CHARGER_MAX_A) && (val <= datalayer.battery.settings.max_user_set_charge_dA) &&
                 (val * datalayer.charger.charger_setpoint_HV_VDC <= CHARGER_MAX_POWER);
        });

    // Route for editing ChargerEndA
    update_string_setting("/updateChargeEndA",
                          [](String value) { datalayer.charger.charger_setpoint_HV_IDC_END = value.toFloat(); });

    // Route for enabling/disabling HV charger
    update_int_setting("/updateChargerHvEnabled",
                       [](int value) { datalayer.charger.charger_HV_enabled = (bool)value; });

    // Route for enabling/disabling aux12v charger
    update_int_setting("/updateChargerAux12vEnabled",
                       [](int value) { datalayer.charger.charger_aux12V_enabled = (bool)value; });
  }

  // Send a GET request to <ESP_IP>/update
  def_route_with_auth("/debug", server, HTTP_GET,
                      [](AsyncWebServerRequest* request) { request->send(200, "text/plain", "Debug: all OK."); });

  // Route to handle reboot command
  def_route_with_auth("/reboot", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", "Rebooting server...");

    //Equipment STOP without persisting the equipment state before restart
    // Max Charge/Discharge = 0; CAN = stop; contactors = open
    setBatteryPause(true, true, true, false);
    delay(1000);
    ESP.restart();
  });

  // Register JSON API routes
  register_api_routes(server);

  // Initialize ElegantOTA
  init_ElegantOTA();

  // Start server
  server.begin();
}

String getConnectResultString(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED:
      return "Connected";
    case WL_NO_SHIELD:
      return "No shield";
    case WL_IDLE_STATUS:
      return "Idle status";
    case WL_NO_SSID_AVAIL:
      return "No SSID available";
    case WL_SCAN_COMPLETED:
      return "Scan completed";
    case WL_CONNECT_FAILED:
      return "Connect failed";
    case WL_CONNECTION_LOST:
      return "Connection lost";
    case WL_DISCONNECTED:
      return "Disconnected";
    default:
      return "Unknown";
  }
}

void ota_monitor() {
  if (ota_active && ota_timeout_timer.elapsed()) {
    // OTA timeout, try to restore can and clear the update event
    set_event(EVENT_OTA_UPDATE_TIMEOUT, 0);
    onOTAEnd(false);
  }
}

// Function to initialize ElegantOTA
void init_ElegantOTA() {
  ElegantOTA.begin(&server);  // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
}

String get_firmware_info_processor(const String& var) {
  if (var == "X") {
    String content = "";
    static JsonDocument doc;

    doc["hardware"] = esp32hal->name();
    doc["firmware"] = String(version_number);
    serializeJson(doc, content);
    return content;
  }
  return String();
}

String get_uptime() {
  uint64_t milliseconds;
  uint32_t remaining_seconds_in_day;
  uint32_t remaining_seconds;
  uint32_t remaining_minutes;
  uint32_t remaining_hours;
  uint16_t total_days;

  milliseconds = millis64();

  //convert passed millis to days, hours, minutes, seconds
  total_days = milliseconds / (1000 * 60 * 60 * 24);
  remaining_seconds_in_day = (milliseconds / 1000) % (60 * 60 * 24);
  remaining_hours = remaining_seconds_in_day / (60 * 60);
  remaining_minutes = (remaining_seconds_in_day % (60 * 60)) / 60;
  remaining_seconds = remaining_seconds_in_day % 60;

  return (String)total_days + " days, " + (String)remaining_hours + " hours, " + (String)remaining_minutes +
         " minutes, " + (String)remaining_seconds + " seconds";
}

void onOTAStart() {
  //try to Pause the battery
  setBatteryPause(true, true);

  // Log when OTA has started
  set_event(EVENT_OTA_UPDATE, 0);

  // If already set, make a new attempt
  clear_event(EVENT_OTA_UPDATE_TIMEOUT);
  ota_active = true;

  ota_timeout_timer.reset();
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    logging.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
    // Reset the "watchdog"
    ota_timeout_timer.reset();
  }
}

void onOTAEnd(bool success) {

  ota_active = false;
  clear_event(EVENT_OTA_UPDATE);

  // Log when OTA has finished
  if (success) {
    //Equipment STOP without persisting the equipment state before restart
    // Max Charge/Discharge = 0; CAN = stop; contactors = open
    setBatteryPause(true, true, true, false);
    // a reboot will be done by the OTA library. no need to do anything here
    logging.println("OTA update finished successfully!");
  } else {
    logging.println("There was an error during OTA update!");
    //try to Resume the battery pause and CAN communication
    setBatteryPause(false, false);
  }
}

template <typename T>  // This function makes power values appear as W when under 1000, and kW when over
String formatPowerValue(String label, T value, String unit, int precision, String color) {
  String result = "<h4 style='color: " + color + ";'>" + label + ": ";
  result += formatPowerValue(value, unit, precision);
  result += "</h4>";
  return result;
}
template <typename T>  // This function makes power values appear as W when under 1000, and kW when over
String formatPowerValue(T value, String unit, int precision) {
  String result = "";

  if (std::is_same<T, float>::value || std::is_same<T, uint16_t>::value || std::is_same<T, uint32_t>::value) {
    float convertedValue = static_cast<float>(value);

    if (convertedValue >= 1000.0f || convertedValue <= -1000.0f) {
      result += String(convertedValue / 1000.0f, precision) + " kW";
    } else {
      result += String(convertedValue, 0) + " W";
    }
  }

  result += unit;
  return result;
}
