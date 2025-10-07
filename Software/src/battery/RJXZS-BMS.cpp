#include "RJXZS-BMS.h"
#include "../battery/BATTERIES.h"
#include "../communication/can/comm_can.h"
#include "../datalayer/datalayer.h"
#include "../devboard/utils/events.h"
#include <limits>   // dla std::numeric_limits
#include <cstring>

// ── SOC Histeresis ─────────────────────
static bool top_full_latched = false;
static constexpr uint16_t SOC_TOP_LATCH_ON  = 9900; // 99.00%: lock „FULL”
static constexpr uint16_t SOC_TOP_LATCH_OFF = 9850; // 98.50%: unlock „FULL”

// ─── SOC from voltage lookup (Li-ion) + helpers ───
namespace {
  static const uint8_t kNumPts = 101;
  static const uint16_t kSoc_pptt[kNumPts] = {
    10000,9900,9800,9700,9600,9500,9400,9300,9200,9100,9000,8900,8800,8700,8600,
     8500,8400,8300,8200,8100,8000,7900,7800,7700,7600,7500,7400,7300,7200,7100,
     7000,6900,6800,6700,6600,6500,6400,6300,6200,6100,6000,5900,5800,5700,5600,
     5500,5400,5300,5200,5100,5000,4900,4800,4700,4600,4500,4400,4300,4200,4100,
     4000,3900,3800,3700,3600,3500,3400,3300,3200,3100,3000,2900,2800,2700,2600,
     2500,2400,2300,2200,2100,2000,1900,1800,1700,1600,1500,1400,1300,1200,1100,
     1000, 900, 800, 700, 600, 500, 400, 300, 200, 100, 0
  };
  static const uint16_t kCell_mV[kNumPts] = {F
    4200,4173,4148,4124,4102,4080,4060,4041,4023,4007,3993,3980,3969,3959,3953,
    3950,3941,3932,3924,3915,3907,3898,3890,3881,3872,3864,3855,3847,3838,3830,
    3821,3812,3804,3795,3787,3778,3770,3761,3752,3744,3735,3727,3718,3710,3701,
    3692,3684,3675,3667,3658,3650,3641,3632,3624,3615,3607,3598,3590,3581,3572,
    3564,3555,3547,3538,3530,3521,3512,3504,3495,3487,3478,3470,3461,3452,3444,
    3435,3427,3418,3410,3401,3392,3384,3375,3367,3358,3350,3338,3325,3313,3299,
    3285,3271,3255,3239,3221,3202,3180,3156,3127,3090,3000
  };

  // Pack resistance
  static const int PACK_INTERNAL_RESISTANCE_MOHM = 50;

  static uint16_t soc_from_cell_mV(uint16_t cell_mV) {
    if (cell_mV >= kCell_mV[0]) return kSoc_pptt[0];
    if (cell_mV <= kCell_mV[kNumPts-1]) return kSoc_pptt[kNumPts-1];
    for (uint8_t i=1;i<kNumPts;++i) if (cell_mV >= kCell_mV[i]) {
      float t = float(cell_mV - kCell_mV[i]) / float(kCell_mV[i-1] - kCell_mV[i]);
      uint16_t diff = kSoc_pptt[i-1] - kSoc_pptt[i];
      return uint16_t(kSoc_pptt[i] + t*diff);
    }
    return 0;
  }

  // SOC estimation based on pack voltage [dV], cell count and current [dA]
  static uint16_t soc_from_pack(uint16_t packVoltage_dV, uint16_t cellCount, int16_t current_dA) {
    if (cellCount == 0) return 0;
    int32_t pack_mV = int32_t(packVoltage_dV) * 100;
    // Uwaga: V_OCV ≈ V_meas − I·R (ten sam wzór działa dla obu znaków prądu)
    int32_t delta_mV = (int32_t(current_dA) * PACK_INTERNAL_RESISTANCE_MOHM) / 10; // dA→A
    int32_t compensated_mV = pack_mV - delta_mV;
    if (compensated_mV < 0) compensated_mV = 0;
    uint16_t avg_cell_mV = uint16_t(compensated_mV / cellCount);
    return soc_from_cell_mV(avg_cell_mV);
  }
} // namespace

void RjxzsBms::update_values() {

  // ── SOC MODE  ───────────────────────────────────────────────
  uint16_t soc_pptt = 0;

  // (1) SOC reported by BMS – UNCOMMENT to use:
  // soc_pptt = uint16_t(battery_capacity_percentage) * 100;  // 0..10000

  //(2) SOC from Cell Voltage – DEFAULT ENABLED:
  {
    //Determine the current BEFORE calculating SOC (as you write it to the datalayer below)
    int16_t current_for_soc_dA = total_current;
    if (discharging_active)      current_for_soc_dA = -total_current; // rozładowanie ujemne
    else /* charging/unknown */  current_for_soc_dA =  total_current; // ładowanie dodatnie

    // Count the current number of "live" cells (without waiting for the update of the populated_cellvoltages below)
    uint16_t live_cells = 0;
    for (int i = 0; i < MAX_AMOUNT_CELLS; ++i) if (cellvoltages[i] > 0) live_cells++;

    uint16_t cellCount =
        (datalayer.battery.info.number_of_cells > 0) ? datalayer.battery.info.number_of_cells
                                                     : live_cells;
    if (cellCount == 0) cellCount = 192; // default HV

    soc_pptt = soc_from_pack(total_voltage, cellCount, current_for_soc_dA);
  }

  datalayer.battery.status.real_soc = soc_pptt;

  // ── Histeresis
  if (!top_full_latched && datalayer.battery.status.real_soc >= SOC_TOP_LATCH_ON) {
    top_full_latched = true;
  } else if (top_full_latched && datalayer.battery.status.real_soc <= SOC_TOP_LATCH_OFF) {
    top_full_latched = false;
  }

  // ──────────────────────────────────────────────────────────────────

  datalayer.battery.status.remaining_capacity_Wh = static_cast<uint32_t>(
      (static_cast<double>(datalayer.battery.status.real_soc) / 10000) * datalayer.battery.info.total_capacity_Wh);

  datalayer.battery.status.soh_pptt;  // This BMS does not have a SOH% formula

  datalayer.battery.status.voltage_dV = total_voltage;

  if (charging_active) {
    datalayer.battery.status.current_dA = total_current;
  } else if (discharging_active) {
    datalayer.battery.status.current_dA = -total_current;
  } else {  // No direction data. Better assume charging than nothing
    datalayer.battery.status.current_dA = total_current;
  }

  // ---Charging limits with hysteresis "full pack"
  if (top_full_latched) {
    // when "full" – stop or severely limit charging:
    datalayer.battery.status.max_charge_power_W = MAX_CHARGE_POWER_WHEN_TOPBALANCING_W; // daj 0, jeśli chcesz total off
  } else if (datalayer.battery.status.real_soc > RAMPDOWN_SOC) {
    datalayer.battery.status.max_charge_power_W =
        datalayer.battery.status.override_charge_power_W *
        (1 - (datalayer.battery.status.real_soc - RAMPDOWN_SOC) / (10000.0 - RAMPDOWN_SOC));
  } else {
    datalayer.battery.status.max_charge_power_W = datalayer.battery.status.override_charge_power_W;
  }

  // Discharge power is manually set
  datalayer.battery.status.max_discharge_power_W = datalayer.battery.status.override_discharge_power_W;

  uint16_t temperatures[] = {
      module_1_temperature,  module_2_temperature,  module_3_temperature,  module_4_temperature,
      module_5_temperature,  module_6_temperature,  module_7_temperature,  module_8_temperature,
      module_9_temperature,  module_10_temperature, module_11_temperature, module_12_temperature,
      module_13_temperature, module_14_temperature, module_15_temperature, module_16_temperature};

  uint16_t min_temp = std::numeric_limits<uint16_t>::max();
  uint16_t max_temp = 0;

  // Loop through the array to find min and max temperatures, ignoring 0 values
  for (int i = 0; i < 16; i++) {
    if (temperatures[i] != 0) {  // Ignore unavailable temperatures
      if (temperatures[i] < min_temp) {
        min_temp = temperatures[i];
      }
      if (temperatures[i] > max_temp) {
        max_temp = temperatures[i];
      }
    }
  }

  datalayer.battery.status.temperature_min_dC = min_temp;

  datalayer.battery.status.temperature_max_dC = max_temp;

  //Map all cell voltages to the global array
  memcpy(datalayer.battery.status.cell_voltages_mV, cellvoltages, MAX_AMOUNT_CELLS * sizeof(uint16_t));

  datalayer.battery.info.number_of_cells = populated_cellvoltages;  // 1-192S

  datalayer.battery.info.max_design_voltage_dV;  // Set according to cells?

  datalayer.battery.info.min_design_voltage_dV;  // Set according to cells?

  datalayer.battery.status.cell_max_voltage_mV = maximum_cell_voltage;

  datalayer.battery.status.cell_min_voltage_mV = minimum_cell_voltage;
}

void RjxzsBms::handle_incoming_can_frame(CAN_frame rx_frame) {

  /*
  // All CAN messages recieved will be logged via serial
  logging.print(millis());  // Example printout, time, ID, length, data: 7553  1DB  8  FF C0 B9 EA 0 0 2 5D
  logging.print("  ");
  logging.print(rx_frame.ID, HEX);
  logging.print("  ");
  logging.print(rx_frame.DLC);
  logging.print("  ");
  for (int i = 0; i < rx_frame.DLC; ++i) {
    logging.print(rx_frame.data.u8[i], HEX);
    logging.print(" ");
  }
  logging.println("");
  */
  switch (rx_frame.ID) {
    case 0xF5:                 // This is the only message is sent from BMS
      setup_completed = true;  // Let the function know we no longer need to send startup messages
      datalayer.battery.status.CAN_battery_still_alive = CAN_STILL_ALIVE;
      mux = rx_frame.data.u8[0];

      if (mux == 0x01) {  // discharge protection voltage, protective current, battery pack capacity
        discharge_protection_voltage = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        protective_current = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        battery_pack_capacity = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x02) {  // Number of strings, charge protection voltage, protection temperature
        number_of_battery_strings = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        charging_protection_voltage = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        protection_temperature = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x03) {  // Total voltage/current/power
        total_voltage = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        total_current = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        total_power = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x04) {  // Capacity, percentages
        battery_usage_capacity = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        battery_capacity_percentage = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        charging_capacity = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x05) {  //Recovery / capacity
        charging_recovery_voltage = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        discharging_recovery_voltage = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        remaining_capacity = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x06) {  // temperature, equalization
        host_temperature = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        status_accounting = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        equalization_starting_voltage = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
        if ((rx_frame.data.u8[4] & 0x40) >> 6) {
          charging_active = true;
          discharging_active = false;
        } else {
          charging_active = false;
          discharging_active = true;
        }
      } else if (mux >= 0x07 && mux <= 0x46) {
        // Cell voltages 1-192 (3 per message, 0x07=1-3, 0x08=4-6, ..., 0x46=190-192)
        int cell_index = (mux - 0x07) * 3;
        for (int i = 0; i < 3; i++) {
          if (cell_index + i >= MAX_AMOUNT_CELLS) {
            break;
          }
          cellvoltages[cell_index + i] = (rx_frame.data.u8[1 + i * 2] << 8) | rx_frame.data.u8[2 + i * 2];
        }
        if (cell_index + 2 >= populated_cellvoltages) {
          populated_cellvoltages = cell_index + 2 + 1;
        }
      } else if (mux == 0x47) {
        temperature_below_zero_mod1_4 = rx_frame.data.u8[2];
        module_1_temperature = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
      } else if (mux == 0x48) {
        module_2_temperature = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        module_3_temperature = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x49) {
        module_4_temperature = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
      } else if (mux == 0x4A) {
        temperature_below_zero_mod5_8 = rx_frame.data.u8[2];
        module_5_temperature = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
      } else if (mux == 0x4B) {
        module_6_temperature = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        module_7_temperature = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x4C) {
        module_8_temperature = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
      } else if (mux == 0x4D) {
        temperature_below_zero_mod9_12 = rx_frame.data.u8[2];
        module_9_temperature = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        module_10_temperature = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x4E) {
        module_11_temperature = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        module_12_temperature = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        module_13_temperature = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x4F) {
        module_14_temperature = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        module_15_temperature = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        module_16_temperature = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x50) {
        low_voltage_power_outage_protection = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        low_voltage_power_outage_delayed = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        num_of_triggering_protection_cells = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x51) {
        balanced_reference_voltage = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        minimum_cell_voltage = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        maximum_cell_voltage = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
      } else if (mux == 0x52) {
        accumulated_total_capacity_high = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        accumulated_total_capacity_low = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        pre_charge_delay_time = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
        LCD_status = rx_frame.data.u8[7];
      } else if (mux == 0x53) {
        differential_pressure_setting_value = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        use_capacity_to_automatically_reset = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        low_temperature_protection_setting_value = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
        protecting_historical_logs = rx_frame.data.u8[7];

        if ((protecting_historical_logs & 0x0F) > 0) {
          set_event(EVENT_RJXZS_LOG, 0);
        } else {
          clear_event(EVENT_RJXZS_LOG);
        }

        if (protecting_historical_logs == 0x01) {
          // Overcurrent protection
          set_event(EVENT_DISCHARGE_LIMIT_EXCEEDED, 0);  // could also be EVENT_CHARGE_LIMIT_EXCEEDED
        } else if (protecting_historical_logs == 0x02) {
          // over discharge protection
          set_event(EVENT_BATTERY_UNDERVOLTAGE, 0);
        } else if (protecting_historical_logs == 0x03) {
          // overcharge protection
          set_event(EVENT_BATTERY_OVERVOLTAGE, 0);
        } else if (protecting_historical_logs == 0x04) {
          // Over temperature protection
          set_event(EVENT_BATTERY_OVERHEAT, 0);
        } else if (protecting_historical_logs == 0x05) {
          // Battery string error protection
          set_event(EVENT_BATTERY_CAUTION, 0);
        } else if (protecting_historical_logs == 0x06) {
          // Damaged charging relay
          set_event(EVENT_BATTERY_CHG_STOP_REQ, 0);
        } else if (protecting_historical_logs == 0x07) {
          // Damaged discharge relay
          set_event(EVENT_BATTERY_DISCHG_STOP_REQ, 0);
        } else if (protecting_historical_logs == 0x08) {
          // Low voltage power outage protection
          set_event(EVENT_12V_LOW, 0);
        } else if (protecting_historical_logs == 0x09) {
          // Voltage difference protection
          set_event(EVENT_VOLTAGE_DIFFERENCE, differential_pressure_setting_value);
        } else if (protecting_historical_logs == 0x0A) {
          // Low temperature protection
          set_event(EVENT_BATTERY_FROZEN, low_temperature_protection_setting_value);
        }
      } else if (mux == 0x54) {
        hall_sensor_type = (rx_frame.data.u8[1] << 8) | rx_frame.data.u8[2];
        fan_start_setting_value = (rx_frame.data.u8[3] << 8) | rx_frame.data.u8[4];
        ptc_heating_start_setting_value = (rx_frame.data.u8[5] << 8) | rx_frame.data.u8[6];
        default_channel_state = rx_frame.data.u8[7];
      }
      break;
    default:
      break;
  }
}

void RjxzsBms::transmit_can(unsigned long currentMillis) {
  // Send 10s CAN Message
  if (currentMillis - previousMillis10s >= INTERVAL_10_S) {
    previousMillis10s = currentMillis;

    if (datalayer.battery.status.bms_status == FAULT) {
      // Incase we loose BMS comms, resend CAN start
      setup_completed = false;
    }

    if (!setup_completed) {
      transmit_can_frame(&RJXZS_10);  // Communication connected flag
      transmit_can_frame(&RJXZS_1C);  // CAN OK
    }
  }
}

void RjxzsBms::setup(void) {  // Performs one time setup at startup
  strncpy(datalayer.system.info.battery_protocol, Name, 63);
  datalayer.system.info.battery_protocol[63] = '\0';
  datalayer.battery.info.max_design_voltage_dV = user_selected_max_pack_voltage_dV;
  datalayer.battery.info.min_design_voltage_dV = user_selected_min_pack_voltage_dV;
  datalayer.battery.info.max_cell_voltage_mV = user_selected_max_cell_voltage_mV;
  datalayer.battery.info.min_cell_voltage_mV = user_selected_min_cell_voltage_mV;
  datalayer.system.status.battery_allows_contactor_closing = true;
}
