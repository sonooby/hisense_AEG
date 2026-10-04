#include "air_conditioner.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <cmath>
namespace esphome {
namespace hisense {
namespace ac {
static const char *const TAG = "climate.air_conditioner";

void AirConditioner::setup() {
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
  }

  // LOWER/HIGHER have no native ESPHome enum, so keep only those as
  // custom fan modes. AUTO/QUIET/LOW/MEDIUM/HIGH are native climate fan modes.
  this->set_supported_custom_fan_modes({"LOWER", "HIGHER"});
  this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);

  this->last_status_change = millis();
  this->status = Status::standby;
}
void AirConditioner::loop() {
  if (millis() - this->last_extra_log_print > 3000) {
    print_extra_log_in_this_loop = true;
    this->last_extra_log_print = millis();
  } else {
    print_extra_log_in_this_loop = false;
  }

  if (millis() - this->last_recived_ > 10000 &&
      millis() - this->last_status_change > 10000) {
    ESP_LOGD(TAG, "Stuck reset error");
    this->rx_buffer_.clear();
    change_status(Status::after_fail);
    return;
  }

  if (status == Status::after_fail) {
    this->rx_buffer_.clear();
    uint8_t byte;
    while (available() && read_byte(&byte)) {
      flush();
    }

    auto before_next_try = millis() - last_status_change > 8000;

    if (print_extra_log_in_this_loop)
      ESP_LOGD(TAG,
               "Waiting %d before asking for status after communitation fail.",
               before_next_try);

    if (before_next_try) {
      ESP_LOGD(TAG, "Reset status.");
      change_status(Status::standby);
    }

    return;
  }

  // Never transmit a control frame while a status request/response is in flight.
  // Otherwise a user command can be appended immediately after the poll frame
  // (F4 FB F4 F5...), and this AC may acknowledge the poll but ignore the command.
  if (this->next_hvac_settings_ && status == Status::standby) {
    send_status();
    change_status(Status::waiting_for_change_confirm);
    return;
  }

  if (millis() - this->last_recived_ > 2000 && status == Status::standby) {
    ESP_LOGD(TAG, "Requsting status.");
    std::vector<uint8_t> requst_status{0xF4, 0xF5, 0x00, 0x40, 0x0C, 0x00,
                                       0x00, 0x01, 0x01, 0xFE, 0x01, 0x00,
                                       0x00, 0x66, 0x00, 0x00, 0x00};
    this->send_raw(requst_status);
    // F4 F5 00 40 0C 00 00 01 01 FE 01 00 00 66 00 00 00 01 B3 F4 FB
    change_status(Status::waiting_for_status_response);

    return;
  }

  int available_to_read = 0;
  while ((available_to_read = this->available())) {
    std::vector<uint8_t> rec_buffor{};
    rec_buffor.resize(available_to_read, 0);

    this->read_array(&rec_buffor[0], available_to_read);
    this->rx_buffer_.insert(this->rx_buffer_.end(), rec_buffor.begin(),
                            rec_buffor.end());

    const auto parse_status = this->parse_ac_message_byte_();
    this->last_recived_ = millis();
    switch (parse_status) {
    case ParseStatus::PARSE_ERROR:
      ESP_LOGD(TAG, "Parse error");
      this->rx_buffer_.clear();
      change_status(Status::after_fail);
      return;
    case ParseStatus::PARSE_OK:
      this->rx_buffer_.clear();
      change_status(Status::standby);
      return;
    case ParseStatus::NOT_FULL:
      change_status(Status::waiting_for_more);
      break;
    }
  }
}

float AirConditioner::get_setup_priority() const {
  // After UART bus
  return setup_priority::BUS - 1.0f;
}

climate::ClimateTraits AirConditioner::traits() {
  auto traits = climate::ClimateTraits();

  traits.set_supported_modes(
      {climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_COOL,
       climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY,
       climate::CLIMATE_MODE_DRY, climate::CLIMATE_MODE_AUTO});

  // Home Assistant treats AUTO/QUIET/LOW/MEDIUM/HIGH as native climate
  // fan modes even if names with the same spelling are advertised as custom.
  // Advertise those natively; LOWER and HIGHER remain custom modes.
  traits.set_supported_fan_modes(
      {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_QUIET,
       climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM,
       climate::CLIMATE_FAN_HIGH});

  traits.set_supported_swing_modes(
      {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_BOTH,
       climate::CLIMATE_SWING_VERTICAL, climate::CLIMATE_SWING_HORIZONTAL});

  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(16.0f);
  traits.set_visual_max_temperature(30.0f);
  traits.set_visual_target_temperature_step(1.0f);
  traits.set_visual_current_temperature_step(1.0f);

  return traits;
}
void AirConditioner::send_status() {
  if (this->next_hvac_settings_ == nullopt) {
    ESP_LOGD(TAG, "send_status nullopt");
    return;
  }
  auto next_hvac_settings = next_hvac_settings_.value();

  std::vector<uint8_t> status{
      0xF4,
      0xF5, // 1
      0x00, // 2
      0x40, // 3
      0x29, // 4
      0x00, // 5
      0x00, // 6
      0x01, // 7
      0x01, // 8
      0xFE, // 9
      0x01, // 10
      0x00, // 11
      0x00, // 12
      0x65, // 13
      0x00, // 14
      0x00, // 15
      0x00, // 16
      0x00, // 17
      0x00, // 18
      0x00, // 19
      0x00, // 20
      0x00, // 21
      0x00, // 22
      0x04, // 23
      0x00, // 24
      0x00, // 25
      0x00, // 26
      0x00, // 27
      0x00, // 28
      0x00, // 29
      0x00, // 30
      0x00, // 31
      0x00, // 32
      0x00, // 33
      0x00, // 34
      0x00, // 35
      0x00, // 36
      0x00, // 37
      0x00, // 38
      0x00, // 39
      0x00, // 40
      0x00, // 41
      0x00, // 42
      0x00, // 43
      0x00, // 44
      0x00  // 45
  };

  if (next_hvac_settings.mode != nullopt) {
    int mode = encode_climateMode((next_hvac_settings.mode.value()));

    if (next_hvac_settings.mode.value() == climate::CLIMATE_MODE_OFF) {
      status[18] = 0b00000100;

    } else {
      uint8_t m_mode = mode << 1;
      m_mode |= (1ul << 0);
      m_mode = m_mode << 4;
      m_mode |= 0b00001100; // To on

      status[18] = m_mode;
      next_hvac_settings.target_temperature = this->target_temperature;
    }
  }

  if (next_hvac_settings.target_temperature != nullopt) {
    auto my_tmp = static_cast<uint8_t>(next_hvac_settings.target_temperature.value());
    if (my_tmp < 16ul || my_tmp > 30ul) {
      ESP_LOGW(TAG, "Invalid target temperature: %u", my_tmp);
      return;
    }

    uint8_t c = my_tmp;
    uint8_t tempX = 0;
    tempX = (c << 1);
    tempX |= (1ul << 0);
    status[19] = tempX;
  }
  
  // Sleep profile command: wire[17]. 0=OFF, 1..4=Sleep profiles.
  if (next_hvac_settings.sleep_profile != nullopt) {
    const uint8_t profile = next_hvac_settings.sleep_profile.value();
    status[17] = (profile == 0) ? 0x01 : static_cast<uint8_t>(profile * 2 + 1);
  }

  // Eco and Turbo share command wire[33], but each command uses its own
  // validity/value nibble. Setters queue them one at a time.
  if (next_hvac_settings.economy != nullopt) {
    status[33] = next_hvac_settings.economy.value() ? 0x30 : 0x10;
  } else if (next_hvac_settings.turbo != nullopt) {
    status[33] = next_hvac_settings.turbo.value() ? 0x0C : 0x04;
  }

  // Display/DIMMER must only be touched by an explicit Display command.
  // Sending 0x40/0xC0 with every unrelated climate command causes the
  // indoor-unit display to be changed as a side effect (for example when
  // switching OFF -> AUTO). 0x00 means "do not change display state".
  if (next_hvac_settings.display != nullopt) {
    status[36] = next_hvac_settings.display.value() ? 0b11000000 : 0b01000000;
    ESP_LOGD(TAG, "Display command: %s",
             next_hvac_settings.display.value() ? "ON" : "OFF");
  }

  if (next_hvac_settings.swing_mode != nullopt) {
    // TODO: Combine with decoding
    auto next_swing_setting = next_hvac_settings.swing_mode;
    if (next_swing_setting == climate::ClimateSwingMode::CLIMATE_SWING_BOTH) {
      status[32] = 0b11110000;
    } else if (next_swing_setting == climate::ClimateSwingMode::CLIMATE_SWING_OFF) {
      status[32] = 0b01010000;
    } else if (next_swing_setting == climate::ClimateSwingMode::CLIMATE_SWING_VERTICAL) {
      status[32] = 0b11010000;
    } else if (next_swing_setting == climate::ClimateSwingMode::CLIMATE_SWING_HORIZONTAL) {
      status[32] = 0b01110000;
    }
  }

  if (next_hvac_settings.raw_fan_command != nullopt) {
    status[16] = next_hvac_settings.raw_fan_command.value();
    // Turbo restore never intends to toggle QUIET implicitly.
    status[35] = (status[16] == 0x03) ? 0x30 : 0x10;
  } else if (next_hvac_settings.custom_fan_mode != nullopt) {
    const auto &fan = next_hvac_settings.custom_fan_mode.value();
    status[16] = encode_custom_fan_mode(fan);

    // QUIET also has a dedicated mute flag in the command frame.
    // Explicitly clear it when another fan mode is selected.
    status[35] = (fan == "QUIET") ? 0x30 : 0x10;
  } else if (next_hvac_settings.fan_mode != nullopt) {
    // Compatibility path for service calls that still use standard fan enums.
    status[16] = encode_fan_mode(next_hvac_settings.fan_mode.value());
    status[35] =
        (next_hvac_settings.fan_mode.value() == climate::CLIMATE_FAN_QUIET)
            ? 0x30
            : 0x10;
  }

  
  send_raw(status);
  this->next_hvac_settings_ = nullopt;
  ESP_LOGD(TAG, "send_status");
}

void AirConditioner::control(const climate::ClimateCall &call) {
  auto next_hvac_settings = HvacSettings();
  bool has_change = false;

  if (call.get_mode().has_value()) {
    next_hvac_settings.mode = call.get_mode();
    has_change = true;
  }
  if (call.has_custom_fan_mode()) {
    auto custom_fan = call.get_custom_fan_mode();
    next_hvac_settings.custom_fan_mode =
        std::string(custom_fan.c_str(), custom_fan.size());
    has_change = true;
  } else if (call.get_fan_mode().has_value()) {
    next_hvac_settings.fan_mode = call.get_fan_mode();
    has_change = true;
  }
  if (call.get_swing_mode().has_value()) {
    next_hvac_settings.swing_mode = call.get_swing_mode();
    has_change = true;
  }
  if (call.get_target_temperature().has_value()) {
    next_hvac_settings.target_temperature = call.get_target_temperature();
    has_change = true;
  }
  if (call.get_preset().has_value()) {
    next_hvac_settings.preset = call.get_preset();
    has_change = true;
  }

  if (has_change) {
    this->next_hvac_settings_ = std::move(next_hvac_settings);
  }
}

ParseStatus AirConditioner::parse_ac_message_byte_() {
  size_t at = this->rx_buffer_.size() - 1;

  if (at < 6) {
    return ParseStatus::NOT_FULL;
  }

  if (this->rx_buffer_[0] != 0xF4 || this->rx_buffer_[1] != 0xF5) {
    ESP_LOGD(TAG, "Wrong magic start sequence 0x%x 0x%x.", this->rx_buffer_[0],
             this->rx_buffer_[1]);
    return ParseStatus::PARSE_ERROR;
  }

  std::vector<uint8_t> payload(rx_buffer_.begin() + 2, rx_buffer_.end() - 4);
  if (at > 8 && this->rx_buffer_[at] == 0xFB &&
      this->rx_buffer_[at - 1] == 0xF4) {

    std::vector<uint8_t> payload_for_crc(rx_buffer_.begin(),
                                         rx_buffer_.end() - 4);

    auto const checksum = this->checksum(std::move(payload_for_crc));

    uint8_t cr1 = (checksum >> 8) & 0xFF;
    uint8_t cr2 = checksum & 0xFF;
    size_t len = rx_buffer_.size();

    uint8_t provided_cr1 = rx_buffer_[at - 3];
    uint8_t provided_cr2 = rx_buffer_[at - 2];

    if (cr1 != provided_cr1 || cr2 != provided_cr2) {
      ESP_LOGD(TAG, "Not valid checksum, expected: 0x%x 0x%x, got 0x%x 0x%x.",
               cr1, cr2, provided_cr1, provided_cr2);
      return ParseStatus::PARSE_ERROR;
    }
  } else {
    ESP_LOGV(TAG, "Not found end sequence yet at: %d.", at);
    return ParseStatus::NOT_FULL;
  }

  ESP_LOGD(TAG, "Parsing message that seem to be OK: %s",
           format_hex_pretty(payload).c_str());
  decode_message(payload);

  if (waiting_for_response) {
    waiting_for_response = 0;
  } else {
    ESP_LOGD(TAG, "Not expected");
  }

  return ParseStatus::PARSE_OK;
}

void AirConditioner::dump_config() {
  ESP_LOGCONFIG(TAG, "Hisense:");
  LOG_PIN("  Flow Control Pin: ", this->flow_control_pin_);
}

void AirConditioner::send_raw(const std::vector<uint8_t> &payload) {
  // Add F4 F5 at start

  if (payload.empty()) {
    return;
  }
  auto const checksum = this->checksum(payload);
  uint8_t cr1 = (checksum & 0x0000ff00) >> 8;
  uint8_t cr2 = (checksum & 0x000000ff);

  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(true);

  this->write_array(payload);
  this->write_byte(cr1);
  this->write_byte(cr2);
  this->write_byte(0xF4);
  this->write_byte(0xFB);
  this->flush();

  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(false);
  waiting_for_response = 1;

  last_send_ = millis();
}

short int AirConditioner::checksum(const std::vector<uint8_t> &payload) {
  short int csum = 0;
  int arrlen = payload.size();
  for (int i = 2; i < arrlen; i++) {
    csum = csum + payload[i];
  }
  return csum;
}

void AirConditioner::decode_message(std::vector<uint8_t> payload) {
  ESP_LOGD(TAG, "decode_message");

  // Status frame offsets below are payload[] offsets, i.e. wire offset - 2.
  // We need at least wire byte 45 (payload[43]).
  if (payload.size() <= 43) {
    ESP_LOGW(TAG, "Status payload too short: %u bytes", (unsigned) payload.size());
    return;
  }

  const uint8_t fan_raw = payload[14];        // wire[16]
  this->last_fan_status_raw_ = fan_raw;
  const uint8_t mode_raw = payload[16];       // wire[18]
  const uint8_t mode_nibble = mode_raw >> 4;
  const uint8_t compressor_hz = payload[40];  // wire[42]

  // Power + HVAC mode.
  const bool is_on = (mode_raw & 0x08) != 0;
  if (is_on) {
    this->mode = decode_climateMode(mode_nibble);
    ESP_LOGD(TAG, "mode raw=0x%02X -> %d", mode_raw, this->mode);
  } else {
    this->mode = climate::CLIMATE_MODE_OFF;
    ESP_LOGD(TAG, "AC is OFF");
  }

  // Setpoint and current room temperature.
  this->target_temperature = static_cast<float>(payload[17]);  // wire[19]
  this->current_temperature = static_cast<float>(payload[18]); // wire[20]

  // Exact fan speed mapping from captures:
  // status 01=AUTO, 02=QUIET, 0A=LOWER, 0C=LOW, 0E=MEDIUM,
  //        10=HIGH, 12=HIGHER.
  // Native ESPHome modes are used where possible so Home Assistant does not
  // reinterpret a custom mode called "LOW"/"HIGH"/etc. as a native mode.
  switch (fan_raw) {
    case 0x00:
    case 0x01:
      this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
      ESP_LOGD(TAG, "fan raw=0x%02X -> AUTO", fan_raw);
      break;
    case 0x02:
      this->set_fan_mode_(climate::CLIMATE_FAN_QUIET);
      ESP_LOGD(TAG, "fan raw=0x%02X -> QUIET", fan_raw);
      break;
    case 0x0A:
      this->set_custom_fan_mode_("LOWER");
      ESP_LOGD(TAG, "fan raw=0x%02X -> LOWER", fan_raw);
      break;
    case 0x0C:
      this->set_fan_mode_(climate::CLIMATE_FAN_LOW);
      ESP_LOGD(TAG, "fan raw=0x%02X -> LOW", fan_raw);
      break;
    case 0x0E:
      this->set_fan_mode_(climate::CLIMATE_FAN_MEDIUM);
      ESP_LOGD(TAG, "fan raw=0x%02X -> MEDIUM", fan_raw);
      break;
    case 0x10:
      this->set_fan_mode_(climate::CLIMATE_FAN_HIGH);
      ESP_LOGD(TAG, "fan raw=0x%02X -> HIGH", fan_raw);
      break;
    case 0x12:
      this->set_custom_fan_mode_("HIGHER");
      ESP_LOGD(TAG, "fan raw=0x%02X -> HIGHER", fan_raw);
      break;
    default:
      ESP_LOGW(TAG, "Unknown fan status code: 0x%02X", fan_raw);
      break;
  }

  // Swing status: wire[35] / payload[33].
  // 0x80 = vertical (up/down), 0x40 = horizontal (left/right).
  const bool vertical = (payload[33] & 0x80) != 0;
  const bool horizontal = (payload[33] & 0x40) != 0;

  if (vertical && horizontal) {
    this->swing_mode = climate::CLIMATE_SWING_BOTH;
  } else if (vertical) {
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  } else if (horizontal) {
    this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
  } else {
    this->swing_mode = climate::CLIMATE_SWING_OFF;
  }

  // Extra remote functions. Status wire[17]/[35]/[37].
  const uint8_t sleep_raw = payload[15];
  if (sleep_raw <= 0x08 && (sleep_raw % 2) == 0) {
    this->sleep_profile_ = sleep_raw / 2;
  }
  this->economy_enable_ = (payload[33] & 0x04) != 0;
  this->turbo_enable_ = (payload[33] & 0x02) != 0;
  // Status frame byte 37 bit7 follows the *actual panel illumination*.
  // When DIMMER is OFF, any ordinary remote/climate command lights the panel
  // for about 9-10 seconds and then it goes dark again.  Publishing that raw
  // bit directly makes the HA Display switch chatter ON/OFF after every command.
  // Treat it as the persistent DIMMER state only after it has stayed unchanged
  // for 12 seconds.  Explicit HA Display commands still update immediately.
  const bool raw_display = (payload[35] & 0x80) != 0;
  if (!this->display_raw_initialized_) {
    this->display_raw_candidate_ = raw_display;
    this->display_raw_changed_at_ = millis();
    this->display_raw_initialized_ = true;
  } else if (raw_display != this->display_raw_candidate_) {
    this->display_raw_candidate_ = raw_display;
    this->display_raw_changed_at_ = millis();
  } else if (millis() - this->display_raw_changed_at_ >= 12000 &&
             this->display_enable != this->display_raw_candidate_) {
    this->display_enable = this->display_raw_candidate_;
    ESP_LOGD(TAG, "Display stable state: %s",
             this->display_enable ? "ON" : "OFF");
  }

  // Current action. Compressor frequency is a reliable indicator for
  // compressor-driven modes; FAN_ONLY is active even at 0 Hz.
  if (!is_on) {
    this->action = climate::CLIMATE_ACTION_OFF;
  } else if (mode_nibble == 0) {
    this->action = climate::CLIMATE_ACTION_FAN;
  } else if (compressor_hz == 0) {
    this->action = climate::CLIMATE_ACTION_IDLE;
  } else {
    switch (mode_nibble) {
      case 1:
      case 5:
        this->action = climate::CLIMATE_ACTION_HEATING;
        break;
      case 2:
      case 6:
        this->action = climate::CLIMATE_ACTION_COOLING;
        break;
      case 3:
      case 7:
        this->action = climate::CLIMATE_ACTION_DRYING;
        break;
      default:
        this->action = climate::CLIMATE_ACTION_IDLE;
        break;
    }
  }

  this->publish_state();

#ifdef USE_SENSOR
  update_sub_sensor_(SubSensorType::INDOOR_TEMPERATURE,
                     static_cast<float>(payload[18]));
  update_sub_sensor_(SubSensorType::INDOOR_COIL_TEMPERATURE,
                     static_cast<float>(payload[19]));

  // 0x80 means "sensor/value unavailable" on units without a humidity sensor.
  if (payload[21] == 0x80) {
    update_sub_sensor_(SubSensorType::INDOOR_HUMIDITY, NAN);
  } else {
    update_sub_sensor_(SubSensorType::INDOOR_HUMIDITY,
                       static_cast<float>(payload[21]));
  }

  // Real outdoor sensors are wire[44]/wire[45] = payload[42]/payload[43].
  // They are signed int8 values, so negative outdoor temperatures work too.
  update_sub_sensor_(SubSensorType::OUTDOOR_TEMPERATURE,
                     static_cast<float>(static_cast<int8_t>(payload[42])));
  update_sub_sensor_(SubSensorType::OUTDOOR_COIL_TEMPERATURE,
                     static_cast<float>(static_cast<int8_t>(payload[43])));

  // Existing enum already contains COMPRESSOR_FREQUENCY.
  // If it is exposed by sensor/__init__.py later, this publishes the real value.
  update_sub_sensor_(SubSensorType::COMPRESSOR_FREQUENCY,
                     static_cast<float>(compressor_hz));
#endif
}

#ifdef USE_SENSOR
void AirConditioner::set_sub_sensor(SubSensorType type, sensor::Sensor *sens) {
  std::size_t index = static_cast<std::size_t>(type);
  if (index < sub_sensors_.size()) {
    sub_sensors_[index] = sens;
  } else {
    ESP_LOGE(TAG, "Not valid sensor index: %d", index);
  }
};
void AirConditioner::update_sub_sensor_(SubSensorType type, float value) {
  std::size_t index = static_cast<std::size_t>(type);
  if (index >= sub_sensors_.size()) {
    ESP_LOGE(TAG, "Invalid sensor index: %u", (unsigned) index);
    return;
  }

  // A valid but unconfigured optional sensor is normal.
  if (sub_sensors_[index] != nullptr) {
    sub_sensors_[index]->publish_state(value);
  }
};
#endif

void AirConditioner::set_display_switch(bool state) {
  auto next_hvac_settings = HvacSettings();
  next_hvac_settings.display = state;
  this->next_hvac_settings_ = std::move(next_hvac_settings);
  this->display_enable = state;
  this->display_raw_candidate_ = state;
  this->display_raw_changed_at_ = millis();
  this->display_raw_initialized_ = true;
}

void AirConditioner::set_economy_switch(bool state) {
  auto next_hvac_settings = HvacSettings();
  next_hvac_settings.economy = state;
  this->next_hvac_settings_ = std::move(next_hvac_settings);
}

void AirConditioner::set_turbo_switch(bool state) {
  auto next_hvac_settings = HvacSettings();

  if (state) {
    // The real remote remembers the pre-SUPER setpoint/fan locally.
    // The indoor unit itself does not restore them when the Turbo bit is cleared.
    if (!this->turbo_enable_) {
      this->turbo_restore_temperature_ = this->target_temperature;
      this->turbo_restore_fan_status_raw_ = this->last_fan_status_raw_;
      this->turbo_restore_valid_ = true;
      ESP_LOGD(TAG, "SUPER save: temp=%.0f fan_status=0x%02X",
               this->turbo_restore_temperature_,
               this->turbo_restore_fan_status_raw_);
    }
    next_hvac_settings.turbo = true;
  } else {
    next_hvac_settings.turbo = false;

    if (this->turbo_restore_valid_) {
      next_hvac_settings.target_temperature = this->turbo_restore_temperature_;

      uint8_t cmd = 0x01;
      switch (this->turbo_restore_fan_status_raw_) {
        case 0x00:
        case 0x01: cmd = 0x01; break; // AUTO
        case 0x02: cmd = 0x03; break; // QUIET
        case 0x0A: cmd = 0x0B; break; // LOWER
        case 0x0C: cmd = 0x0D; break; // LOW
        case 0x0E: cmd = 0x0F; break; // MEDIUM
        case 0x10: cmd = 0x11; break; // HIGH
        case 0x12: cmd = 0x13; break; // HIGHER
        default:
          ESP_LOGW(TAG, "Unknown pre-SUPER fan status 0x%02X; restoring AUTO",
                   this->turbo_restore_fan_status_raw_);
          cmd = 0x01;
          break;
      }
      next_hvac_settings.raw_fan_command = cmd;
      ESP_LOGD(TAG, "SUPER restore: temp=%.0f fan_cmd=0x%02X",
               this->turbo_restore_temperature_, cmd);
      this->turbo_restore_valid_ = false;
    }
  }

  this->next_hvac_settings_ = std::move(next_hvac_settings);
}

void AirConditioner::set_sleep_profile(uint8_t profile) {
  if (profile > 4) {
    ESP_LOGW(TAG, "Invalid sleep profile: %u", profile);
    return;
  }
  auto next_hvac_settings = HvacSettings();
  next_hvac_settings.sleep_profile = profile;
  this->next_hvac_settings_ = std::move(next_hvac_settings);
}

void AirConditioner::change_status(Status new_status) {
  this->status = new_status;
  last_status_change = millis();
}

} // namespace ac
} // namespace hisense
} // namespace esphome