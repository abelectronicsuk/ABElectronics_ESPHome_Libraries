#pragma once

#include <vector>

#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace mcp3424 {

// One input channel of the MCP3424, published as a voltage sensor.
class MCP3424Sensor : public sensor::Sensor {
 public:
  void set_channel(uint8_t channel) { this->channel_ = channel; }
  void set_resolution_bits(uint8_t bits) { this->bits_ = bits; }
  void set_gain(uint8_t gain) { this->gain_ = gain; }

  uint8_t get_channel() const { return this->channel_; }
  uint8_t get_resolution_bits() const { return this->bits_; }
  uint8_t get_gain() const { return this->gain_; }

  // Config register byte for a one-shot conversion on this channel.
  uint8_t config_byte() const;
  // Time a conversion takes at this resolution, plus a small margin (ms).
  uint32_t conversion_time_ms() const;

 protected:
  uint8_t channel_{0};  // 0-3
  uint8_t bits_{18};    // 12, 14, 16 or 18
  uint8_t gain_{1};     // 1, 2, 4 or 8
};

// The MCP3424 chip. Each update it converts every registered channel in turn,
// using one-shot mode so a slow 18-bit channel never blocks the main loop.
class MCP3424Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void register_sensor(MCP3424Sensor *sensor) { this->sensors_.push_back(sensor); }

 protected:
  void start_conversion_(size_t index);
  void read_conversion_(size_t index, uint8_t attempt);

  std::vector<MCP3424Sensor *> sensors_;
  bool busy_{false};
};

}  // namespace mcp3424
}  // namespace esphome
