#include "api_json.h"
#include <WiFi.h>
#include <algorithm>
#include <vector>
#include "../../battery/BATTERIES.h"
#include "../../battery/Battery.h"
#include "../../battery/Shunt.h"
#include "../../charger/CHARGERS.h"
#include "../../communication/contactorcontrol/comm_contactorcontrol.h"
#include "../../communication/equipmentstopbutton/comm_equipmentstopbutton.h"
#include "../../datalayer/datalayer.h"
#include "../../inverter/INVERTERS.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../hal/hal.h"
#include "../safety/safety.h"
#include "../utils/events.h"

#include <string>
extern const char* version_number;
extern std::string ssid;
extern std::string http_username;
extern std::string http_password;
extern bool webserver_auth;

extern void def_route_with_auth(const char* uri, AsyncWebServer& serv, WebRequestMethodComposite method,
                                std::function<void(AsyncWebServerRequest*)> handler);

static void serialize_battery_status(JsonObject& obj, DATALAYER_BATTERY_STATUS_TYPE& status,
                                     DATALAYER_BATTERY_INFO_TYPE& info,
                                     DATALAYER_BATTERY_SETTINGS_TYPE& settings) {
  obj["soc_real"] = static_cast<float>(status.real_soc) / 100.0f;
  obj["soc_reported"] = static_cast<float>(status.reported_soc) / 100.0f;
  obj["soh"] = static_cast<float>(status.soh_pptt) / 100.0f;
  obj["voltage_V"] = static_cast<float>(status.voltage_dV) / 10.0f;
  obj["current_A"] = static_cast<float>(status.current_dA) / 10.0f;
  obj["power_W"] = status.active_power_W;
  obj["temp_min_C"] = static_cast<float>(status.temperature_min_dC) / 10.0f;
  obj["temp_max_C"] = static_cast<float>(status.temperature_max_dC) / 10.0f;
  obj["max_discharge_power_W"] = status.max_discharge_power_W;
  obj["max_charge_power_W"] = status.max_charge_power_W;
  obj["max_discharge_current_A"] = static_cast<float>(status.max_discharge_current_dA) / 10.0f;
  obj["max_charge_current_A"] = static_cast<float>(status.max_charge_current_dA) / 10.0f;
  obj["cell_max_mV"] = status.cell_max_voltage_mV;
  obj["cell_min_mV"] = status.cell_min_voltage_mV;
  obj["remaining_capacity_Wh"] = status.remaining_capacity_Wh;
  obj["reported_remaining_capacity_Wh"] = status.reported_remaining_capacity_Wh;
  obj["total_capacity_Wh"] = info.total_capacity_Wh;
  obj["reported_total_capacity_Wh"] = info.reported_total_capacity_Wh;
  obj["soc_scaling_active"] = settings.soc_scaling_active;
  obj["bms_status"] = static_cast<int>(status.bms_status);
  obj["real_bms_status"] = static_cast<int>(status.real_bms_status);
}

static void serialize_battery_cells(JsonObject& obj, DATALAYER_BATTERY_STATUS_TYPE& status,
                                    DATALAYER_BATTERY_INFO_TYPE& info) {
  uint8_t num_cells = info.number_of_cells;
  obj["num_cells"] = num_cells;
  obj["balancing_status"] = static_cast<int>(status.balancing_status);
  JsonArray voltages = obj["voltages_mV"].to<JsonArray>();
  JsonArray balancing = obj["balancing"].to<JsonArray>();
  for (uint8_t i = 0; i < num_cells; i++) {
    voltages.add(status.cell_voltages_mV[i]);
    balancing.add(status.cell_balancing_status[i]);
  }
}

void register_api_routes(AsyncWebServer& server) {

  // GET /api/status - Primary polling endpoint
  def_route_with_auth("/api/status", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    static JsonDocument doc;
    doc.clear();

    if (battery) {
      JsonObject bat = doc["battery"].to<JsonObject>();
      serialize_battery_status(bat, datalayer.battery.status, datalayer.battery.info, datalayer.battery.settings);
    } else {
      doc["battery"] = nullptr;
    }

    if (battery2) {
      JsonObject bat2 = doc["battery2"].to<JsonObject>();
      serialize_battery_status(bat2, datalayer.battery2.status, datalayer.battery2.info, datalayer.battery2.settings);
    } else {
      doc["battery2"] = nullptr;
    }

    if (battery3) {
      JsonObject bat3 = doc["battery3"].to<JsonObject>();
      serialize_battery_status(bat3, datalayer.battery3.status, datalayer.battery3.info, datalayer.battery3.settings);
    } else {
      doc["battery3"] = nullptr;
    }

    JsonObject sys = doc["system"].to<JsonObject>();
    sys["emulator_status"] = static_cast<int>(get_emulator_status());
    sys["pause_status"] = static_cast<int>(emulator_pause_status);
    sys["equipment_stop"] = datalayer.system.info.equipment_stop_active;
    sys["contactors_engaged"] = datalayer.system.status.contactors_engaged;
    sys["uptime_s"] = millis() / 1000;

    if (charger) {
      JsonObject chg = doc["charger"].to<JsonObject>();
      chg["HV_enabled"] = datalayer.charger.charger_HV_enabled;
      chg["aux12V_enabled"] = datalayer.charger.charger_aux12V_enabled;
      chg["output_power_W"] = charger->outputPowerDC();
      chg["HVDC_voltage_V"] = charger->HVDC_output_voltage();
      chg["HVDC_current_A"] = charger->HVDC_output_current();
      chg["LVDC_voltage_V"] = charger->LVDC_output_voltage();
      chg["LVDC_current_A"] = charger->LVDC_output_current();
      chg["AC_voltage_V"] = charger->AC_input_voltage();
      chg["AC_current_A"] = charger->AC_input_current();
      if (charger->efficiencySupported()) {
        chg["efficiency_pct"] = charger->efficiency();
      }
    } else {
      doc["charger"] = nullptr;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });

  // GET /api/cells - Cell-level data
  def_route_with_auth("/api/cells", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    static JsonDocument doc;
    doc.clear();

    if (battery) {
      JsonObject bat = doc["battery"].to<JsonObject>();
      serialize_battery_cells(bat, datalayer.battery.status, datalayer.battery.info);
    } else {
      doc["battery"] = nullptr;
    }

    if (battery2) {
      JsonObject bat2 = doc["battery2"].to<JsonObject>();
      serialize_battery_cells(bat2, datalayer.battery2.status, datalayer.battery2.info);
    } else {
      doc["battery2"] = nullptr;
    }

    if (battery3) {
      JsonObject bat3 = doc["battery3"].to<JsonObject>();
      serialize_battery_cells(bat3, datalayer.battery3.status, datalayer.battery3.info);
    } else {
      doc["battery3"] = nullptr;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });

  // GET /api/events - Event log data
  def_route_with_auth("/api/events", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    static JsonDocument doc;
    doc.clear();

    static std::vector<EventData> order_events;
    order_events.clear();

    const EVENTS_STRUCT_TYPE* event_pointer;
    for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
      event_pointer = get_event_pointer((EVENTS_ENUM_TYPE)i);
      if (event_pointer->occurences > 0) {
        order_events.push_back({static_cast<EVENTS_ENUM_TYPE>(i), event_pointer});
      }
    }
    std::sort(order_events.begin(), order_events.end(), compareEventsByTimestampDesc);

    uint64_t current_timestamp = millis64();
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& event : order_events) {
      JsonObject obj = arr.add<JsonObject>();
      obj["type"] = get_event_enum_string(event.event_handle);
      obj["level"] = get_event_level_string(event.event_handle);
      obj["ms_ago"] = current_timestamp - event.event_pointer->timestamp;
      obj["occurrences"] = event.event_pointer->occurences;
      obj["data"] = event.event_pointer->data;
      obj["message"] = get_event_message_string(event.event_handle);
    }
    order_events.clear();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });

  // GET /api/info - Slow-changing system info
  def_route_with_auth("/api/info", server, HTTP_GET, [](AsyncWebServerRequest* request) {
    static JsonDocument doc;
    doc.clear();

    doc["firmware"] = version_number;
    doc["hardware"] = esp32hal->name();

    if (battery) {
      doc["battery_protocol"] = datalayer.system.info.battery_protocol;
    }
    if (inverter) {
      doc["inverter_protocol"] = inverter->name();
    }
    if (charger) {
      doc["charger_protocol"] = charger->name();
    }
    if (user_selected_shunt_type != ShuntType::None) {
      doc["shunt_protocol"] = datalayer.system.info.shunt_protocol;
    }

    wl_status_t status = WiFi.status();
    doc["wifi_ssid"] = ssid.c_str();
    if (status == WL_CONNECTED) {
      doc["wifi_rssi"] = WiFi.RSSI();
      doc["wifi_ip"] = WiFi.localIP().toString();
      doc["wifi_hostname"] = WiFi.getHostname();
    }

    doc["cpu_temp_C"] = datalayer.system.info.CPU_temperature;
    doc["free_heap"] = ESP.getFreeHeap();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
}
