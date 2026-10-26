#include "mcp3424.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace mcp3424 {

static const char *const TAG = "mcp3424";

// Config register (datasheet section 5.2):
//  bit 7    RDY  - write 1 to start a one-shot conversion; reads 0 when new data is ready
//  bits 6-5 C1-C0 channel select
//  bit 4    O/C  - 0 = one-shot, 1 = continuous
//  bits 3-2 S1-S0 sample rate: 00 = 12-bit, 01 = 14-bit, 10 = 16-bit, 11 = 18-bit
//  bits 1-0 G1-G0 PGA gain: 00 = x1, 01 = x2, 10 = x4, 11 = x8
static const uint8_t CONFIG_START = 0x80;
static const uint8_t MAX_READ_ATTEMPTS = 5;
static const uint32_t RETRY_DELAY_MS = 10;

uint8_t MCP3424Sensor::config_byte() const {
  uint8_t rate;
  switch (this->bits_) {
    case 12: rate = 0; break;
    case 14: rate = 1; break;
    case 16: rate = 2; break;
    default: rate = 3; break;  // 18-bit
  }
  uint8_t gain;
  switch (this->gain_) {
    case 2: gain = 1; break;
    case 4: gain = 2; break;
    case 8: gain = 3; break;
    default: gain = 0; break;  // x1
  }
  return CONFIG_START | (this->channel_ << 5) | (rate << 2) | gain;  // O/C = 0, one-shot
}

uint32_t MCP3424Sensor::conversion_time_ms() const {
  // 240, 60, 15 and 3.75 samples per second, plus a small margin
  switch (this->bits_) {
    case 12: return 5;
    case 14: return 20;
    case 16: return 75;
    default: return 280;
  }
}

void MCP3424Component::setup() {
  // Check the chip answers on the bus. Writing a one-shot config with RDY = 0
  // sets the register without starting a conversion.
  uint8_t config = 0x00;
  if (this->write(&config, 1) != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "No MCP3424 found at address 0x%02X", this->address_);
    this->mark_failed();
  }
}

void MCP3424Component::dump_config() {
  ESP_LOGCONFIG(TAG, "MCP3424:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication with the MCP3424 failed");
  }
  LOG_UPDATE_INTERVAL(this);
  for (auto *s : this->sensors_) {
    LOG_SENSOR("  ", "Channel", s);
    ESP_LOGCONFIG(TAG, "    Channel: %u, Resolution: %u-bit, Gain: x%u", s->get_channel() + 1,
                  s->get_resolution_bits(), s->get_gain());
  }
}

void MCP3424Component::update() {
  if (this->sensors_.empty())
    return;
  if (this->busy_) {
    // The previous round has not finished; usually means update_interval is
    // shorter than the time all the channels take to convert.
    ESP_LOGW(TAG, "Previous conversions still running, skipping this update");
    return;
  }
  this->busy_ = true;
  this->start_conversion_(0);
}

void MCP3424Component::start_conversion_(size_t index) {
  if (index >= this->sensors_.size()) {
    this->busy_ = false;
    return;
  }
  auto *s = this->sensors_[index];
  uint8_t config = s->config_byte();
  if (this->write(&config, 1) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "Failed to start conversion on channel %u", s->get_channel() + 1);
    this->status_set_warning();
    s->publish_state(NAN);
    this->start_conversion_(index + 1);
    return;
  }
  this->set_timeout("mcp3424_read", s->conversion_time_ms(),
                    [this, index]() { this->read_conversion_(index, 0); });
}

void MCP3424Component::read_conversion_(size_t index, uint8_t attempt) {
  auto *s = this->sensors_[index];
  const uint8_t bits = s->get_resolution_bits();
  // 18-bit returns three data bytes, the others two, followed by the config byte
  const size_t data_len = (bits == 18) ? 3 : 2;
  uint8_t buf[4];

  if (this->read(buf, data_len + 1) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "Failed to read channel %u", s->get_channel() + 1);
    this->status_set_warning();
    s->publish_state(NAN);
    this->start_conversion_(index + 1);
    return;
  }

  // RDY is still 1 if the conversion has not finished yet
  if (buf[data_len] & 0x80) {
    if (attempt + 1 < MAX_READ_ATTEMPTS) {
      this->set_timeout("mcp3424_read", RETRY_DELAY_MS,
                        [this, index, attempt]() { this->read_conversion_(index, attempt + 1); });
      return;
    }
    ESP_LOGW(TAG, "Conversion timed out on channel %u", s->get_channel() + 1);
    this->status_set_warning();
    s->publish_state(NAN);
    this->start_conversion_(index + 1);
    return;
  }

  int32_t raw = 0;
  for (size_t i = 0; i < data_len; i++)
    raw = (raw << 8) | buf[i];

  // Keep only the result bits, then sign-extend (inputs are differential)
  const int32_t mask = (1L << bits) - 1;
  raw &= mask;
  if (raw & (1L << (bits - 1)))
    raw -= (1L << bits);

  // LSB = 2 x 2.048 V / 2^bits: 1 mV, 250 uV, 62.5 uV or 15.625 uV
  const float lsb = 4.096f / static_cast<float>(1L << bits);
  const float volts = static_cast<float>(raw) * lsb / static_cast<float>(s->get_gain());

  ESP_LOGD(TAG, "Channel %u: raw=%d, %.6f V", s->get_channel() + 1, static_cast<int>(raw), volts);
  this->status_clear_warning();
  s->publish_state(volts);
  this->start_conversion_(index + 1);
}

}  // namespace mcp3424
}  // namespace esphome
