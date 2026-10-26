# RTC Pi with ESPHome and the ESP32-P4

## Keeping time with a real-time clock and ESPHome

**You will need this board:** [RTC Pi](https://www.abelectronics.co.uk/p/70/rtc-pi) - Real time clock for the Raspberry Pi and Single-Board Computers

This tutorial will use an RTC Pi with an ESP32-P4 development board running ESPHome. When the ESP32-P4 starts, it will load the time from the DS1307 real-time clock on the RTC Pi. Once it connects to Wi-Fi, it will get the correct time from the internet and save it to the DS1307. Finally, it will read the time from the DS1307 every second and log it to the console. You will need a Waveshare ESP32-P4-Module-DEV-KIT, an RTC Pi and a CR2032 battery.

A real-time clock keeps track of the date and time when the main power is switched off. This is useful for projects that need to know the time even when they cannot reach the internet, such as a data logger in a shed or a timer that must keep running after a power cut.

We will use the ESPHome Device Builder to write the configuration and install it on the ESP32-P4. ESPHome Device Builder runs as a Home Assistant add-on or as a standalone app on your computer; see the [ESPHome getting started guide](https://esphome.io/guides/getting_started_hassio/) if you have not installed it yet.

We are using the Waveshare ESP32-P4-Module-DEV-KIT for this tutorial because it has a 40-pin GPIO header with the same layout as the Raspberry Pi, so the RTC Pi plugs straight onto it. The ESP32-P4 does not have Wi-Fi built in; the DEV-KIT includes an ESP32-C6 coprocessor that provides Wi-Fi, and ESPHome talks to it using the esp32\_hosted component.

## Stage 1 – Setting up the ESP32-P4

First, fit the CR2032 battery into the battery holder on the RTC Pi. Do not connect the RTC Pi to a powered board without a battery installed, as this can damage the DS1307 chip.

Make sure the ESP32-P4-Module-DEV-KIT is disconnected from power, then line up pin 1 on the RTC Pi with pin 1 on the 40-pin header of the DEV-KIT and press the RTC Pi down firmly onto the pins.

The DS1307 needs a 4.5V to 5.5V supply, so the RTC Pi takes its power from the 5V pin on the header. The RTC Pi includes an I2C logic level converter, so the I2C bus on the header side stays at the 3.3V level the ESP32-P4 needs. On the DEV-KIT, the I2C bus uses GPIO7 for SDA on header pin 3 and GPIO8 for SCL on header pin 5.

Open ESPHome Device Builder and click **New Device**. Give your device the name **p4-ds1307-rtc**, choose **ESP32-P4** as the device type and click **Skip** to finish the wizard without installing. Then click **Edit** on the new device card to open the configuration file. Keep the api encryption key and the ota password that the wizard created and replace the rest of the file with the sections below.

The first part of the configuration is the same as in our other ESP32-P4 tutorials. It sets the device name, the ESP32-P4 chip, the ESP32-C6 Wi-Fi coprocessor, the logger, Wi-Fi and the web server.

```yaml
substitutions:
  name: "p4-ds1307-rtc"
  friendly_name: "P4 DS1307 RTC Demo"

esphome:
  name: ${name}
  friendly_name: ${friendly_name}

esp32:
  board: esp32-p4-evboard
  flash_size: 16MB
  framework:
    type: esp-idf

esp32_hosted:
  variant: ESP32C6
  active_high: true
  reset_pin: GPIO54
  cmd_pin: GPIO19
  clk_pin: GPIO18
  d0_pin: GPIO14
  d1_pin: GPIO15
  d2_pin: GPIO16
  d3_pin: GPIO17

logger:
  level: DEBUG
  hardware_uart: UART0

api:

ota:
  - platform: esphome

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

web_server:
  port: 80
  version: 3
  log: true
```

Paste the encryption key and password that the wizard created back into the api and ota sections, and check that wifi\_ssid and wifi\_password are set in **Secrets**.

Next, we set up the I2C bus.

```yaml
i2c:
  sda: GPIO7
  scl: GPIO8
  frequency: 100kHz
  scan: true
```

The DS1307 only supports the 100kHz I2C speed, so we set the frequency to 100kHz rather than the 400kHz we used for the IO Pi Plus. If you share the bus with other devices, the whole bus runs at 100kHz.

## Stage 2 – Adding the DS1307

The RTC Pi uses the DS1307 real-time clock from Maxim, which keeps time using a 32.768kHz crystal and runs from the CR2032 battery when the main power is off. The DS1307 uses I2C address 0x68, which cannot be changed.

In ESPHome, a clock is added as a time source. We add the DS1307 like this:

```yaml
time:
  - platform: ds1307
    id: rtc_time
    timezone: Europe/London
    update_interval: never
```

id gives the clock a name we can use later in the configuration. The DS1307 stores the time in UTC, and timezone tells ESPHome how to convert it to local time. Europe/London means the time switches between GMT and BST automatically; change it to your own time zone if you are outside the UK.

update\_interval controls how often ESPHome reads the DS1307 on its own. We set it to never because we will read the chip ourselves every second in Stage 4.

When the ESP32-P4 starts, its internal clock does not know the time. We want it to load the time from the DS1307 straight away, before Wi-Fi has connected. We do this with an on\_boot trigger in the esphome section at the top of the file.

```yaml
esphome:
  name: ${name}
  friendly_name: ${friendly_name}
  on_boot:
    priority: 600
    then:
      - ds1307.read_time:
          id: rtc_time
      - logger.log:
          format: "Boot: system clock loaded from DS1307"
          level: INFO
```

on\_boot runs a list of actions when the ESP32-P4 starts. priority: 600 makes it run early, after the I2C bus is ready but before the Wi-Fi connection starts. ds1307.read\_time reads the date and time from the DS1307 and sets the ESP32-P4's internal clock, and logger.log writes a message to the console so we can see that it happened.

## Stage 3 – Setting the time from the internet

A new DS1307 does not know the time, and even a set clock slowly drifts by a few seconds a month. We will correct it from the internet each time the ESP32-P4 connects to Wi-Fi, using SNTP, the Simple Network Time Protocol.

Add a second time source below the DS1307 in the time section:

```yaml
  - platform: sntp
    id: sntp_time
    timezone: Europe/London
    servers:
      - 0.pool.ntp.org
      - 1.pool.ntp.org
      - 2.pool.ntp.org
```

The servers list is a set of public time servers from the NTP Pool Project. ESPHome tries the first server and moves on to the next if it does not answer.

When the SNTP time source receives the time, it runs an on\_time\_sync trigger. We use this to write the new time into the DS1307.

```yaml
    on_time_sync:
      then:
        - ds1307.write_time:
            id: rtc_time
        - logger.log:
            format: "NTP sync complete - DS1307 updated from network time"
            level: INFO
```

ds1307.write\_time takes the current time from the ESP32-P4's internal clock, which SNTP has just set, and writes it to the DS1307. The next time the ESP32-P4 starts without an internet connection, the on\_boot trigger from Stage 2 will load this time from the DS1307.

## Stage 4 – Logging the time every second

Now we have a clock that is set from the internet and loaded at startup. Let us read it back from the DS1307 every second and write it to the console.

ESPHome runs actions on a timer with the interval component.

```yaml
interval:
  - interval: 1s
    then:
      - ds1307.read_time:
          id: rtc_time
```

Every second, ds1307.read\_time reads the time over the I2C bus from the DS1307 and copies it into the ESP32-P4's internal clock. Because the internal clock is set from the chip each second, the time we log next is the time the DS1307 is keeping.

To format the time and write it to the console, we use a lambda. A lambda is a short piece of C++ code inside the configuration, which lets you do things the YAML options do not cover.

```yaml
      - lambda: |-
          auto t = id(rtc_time).now();
          if (t.is_valid()) {
            ESP_LOGI("rtc", "DS1307 time: %s", t.strftime("%Y-%m-%d %H:%M:%S %Z").c_str());
          } else {
            ESP_LOGW("rtc", "DS1307 time not valid yet (chip not set or no battery?)");
          }
```

id(rtc\_time).now() gets the current time from the clock. is\_valid() checks that it holds a real date; a DS1307 that has never been set, or has lost its battery, returns an invalid time. strftime formats the time as text: %Y-%m-%d is the date, %H:%M:%S is the time and %Z is the time zone name, GMT or BST.

ESP\_LOGI writes an information message to the console under the tag rtc, and ESP\_LOGW writes a warning.

Finally, we add a text sensor so the time also shows on the web page and in Home Assistant.

```yaml
text_sensor:
  - platform: template
    name: "DS1307 Time"
    update_interval: 1s
    lambda: |-
      auto t = id(rtc_time).now();
      if (!t.is_valid()) return {"not set"};
      return t.strftime("%Y-%m-%d %H:%M:%S");
```

A template text sensor shows whatever its lambda returns. Every second it returns the time as text, or not set if the clock is not valid yet.

## Stage 5 – Installing and testing

That is everything we need; your configuration should now look like this.

```yaml
substitutions:
  name: "p4-ds1307-rtc"
  friendly_name: "P4 DS1307 RTC Demo"

esphome:
  name: ${name}
  friendly_name: ${friendly_name}
  on_boot:
    priority: 600
    then:
      - ds1307.read_time:
          id: rtc_time
      - logger.log:
          format: "Boot: system clock loaded from DS1307"
          level: INFO

esp32:
  board: esp32-p4-evboard
  flash_size: 16MB
  framework:
    type: esp-idf

esp32_hosted:
  variant: ESP32C6
  active_high: true
  reset_pin: GPIO54
  cmd_pin: GPIO19
  clk_pin: GPIO18
  d0_pin: GPIO14
  d1_pin: GPIO15
  d2_pin: GPIO16
  d3_pin: GPIO17

logger:
  level: DEBUG
  hardware_uart: UART0

api:

ota:
  - platform: esphome

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

web_server:
  port: 80
  version: 3
  log: true

i2c:
  sda: GPIO7
  scl: GPIO8
  frequency: 100kHz
  scan: true

time:
  - platform: ds1307
    id: rtc_time
    timezone: Europe/London
    update_interval: never

  - platform: sntp
    id: sntp_time
    timezone: Europe/London
    servers:
      - 0.pool.ntp.org
      - 1.pool.ntp.org
      - 2.pool.ntp.org
    on_time_sync:
      then:
        - ds1307.write_time:
            id: rtc_time
        - logger.log:
            format: "NTP sync complete - DS1307 updated from network time"
            level: INFO

interval:
  - interval: 1s
    then:
      - ds1307.read_time:
          id: rtc_time
      - lambda: |-
          auto t = id(rtc_time).now();
          if (t.is_valid()) {
            ESP_LOGI("rtc", "DS1307 time: %s", t.strftime("%Y-%m-%d %H:%M:%S %Z").c_str());
          } else {
            ESP_LOGW("rtc", "DS1307 time not valid yet (chip not set or no battery?)");
          }

text_sensor:
  - platform: template
    name: "DS1307 Time"
    update_interval: 1s
    lambda: |-
      auto t = id(rtc_time).now();
      if (!t.is_valid()) return {"not set"};
      return t.strftime("%Y-%m-%d %H:%M:%S");
```

Click **Save**, then **Install**. For the first installation, choose **Plug into this computer** and connect the DEV-KIT to your computer with a USB cable. After the first installation, you can choose **Wirelessly** to update the board over Wi-Fi.

When the installation finishes, the log window opens. The I2C scan should find the DS1307 at 0x68 and the ES8311 audio chip on the DEV-KIT at 0x18. On a brand new DS1307 that has never been set, you will see warnings until the ESP32-P4 connects to Wi-Fi, then the NTP sync message, and then the time every second:

```
[W][rtc]: DS1307 time not valid yet (chip not set or no battery?)
[I][main]: NTP sync complete - DS1307 updated from network time
[I][rtc]: DS1307 time: 2026-10-26 12:15:01 GMT
[I][rtc]: DS1307 time: 2026-10-26 12:15:02 GMT
```

To test the real-time clock, change the Wi-Fi password in **Secrets** to something wrong, install again, and restart the board. The ESP32-P4 can no longer reach the internet, but you should see the boot message and the correct time straight away, loaded from the DS1307. Remember to set your Wi-Fi password back afterwards.

You can also open http://p4-ds1307-rtc.local in a web browser to see the DS1307 Time sensor update every second.

If the scan does not find 0x68, check that the RTC Pi is pushed fully onto the 40-pin header and that pin 1 lines up with pin 1 on the DEV-KIT. If the time resets to an invalid value every time you remove the power, check the CR2032 battery.
