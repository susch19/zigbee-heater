#include "temperature_sensor.hpp"
#include "clock.hpp"
#include "ds18b20.h"
#include "ds18b20_types.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heater.hpp"
#include "onewire_bus.h"
#include "onewire_cmd.h"
#include "onewire_crc.h"
#include <driver/gpio.h>
#include <string.h>


static const char *TAG = "DS18B20";
// D0 on Seedstudio ESP32C6
#define ONEWIRE_BUS_GPIO 0
// D10 on Seedstudio ESP32C6
#define ADDITIONAL_GROUND_GPIO GPIO_NUM_18

void TemperatureSensor::findSensors() {
  if (this->bus == NULL) {

    gpio_config_t gpioConfig = {.pin_bit_mask = 1 << ADDITIONAL_GROUND_GPIO,
                                .mode = GPIO_MODE_OUTPUT,
                                .pull_up_en = GPIO_PULLUP_DISABLE,
                                .pull_down_en = GPIO_PULLDOWN_ENABLE,
                                .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&gpioConfig);

    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(ADDITIONAL_GROUND_GPIO, 0));

    onewire_bus_config_t bus_config = {
        .bus_gpio_num = ONEWIRE_BUS_GPIO,
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes =
            10, // 1byte ROM command + 8byte ROM number + 1byte device command
    };
    ESP_ERROR_CHECK(
        onewire_new_bus_rmt(&bus_config, &rmt_config, &(this->bus)));
  }

  int ds18b20_device_num = 0;
  onewire_device_iter_handle_t iter = NULL;
  onewire_device_t next_onewire_device;
  esp_err_t search_result = ESP_OK;

  // create 1-wire device iterator, which is used for device search
  ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
  ESP_LOGI(TAG, "Device iterator created, start searching...");
  do {
    search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
    if (search_result == ESP_OK) { // found a new device, let's check if we can
                                   // upgrade it to a DS18B20
      ds18b20_config_t ds_cfg = {};
      // check if the device is a DS18B20, if so, return the ds18b20 handle
      if (ds18b20_new_device(&next_onewire_device, &ds_cfg, &ds18b20s) ==
          ESP_OK) {
        ESP_LOGI(TAG, "Found a DS18B20[%d], address: %016llX",
                 ds18b20_device_num, next_onewire_device.address);
        ds18b20_set_resolution(ds18b20s, DS18B20_RESOLUTION_9B);
        ds18b20_device_num++;
        tempSensorFound = true;

      } else {
        ESP_LOGI(TAG, "Found an unknown device, address: %016llX",
                 next_onewire_device.address);
      }
    }
  } while (search_result != ESP_ERR_NOT_FOUND);
  ESP_ERROR_CHECK(onewire_del_device_iter(iter));
  ESP_LOGI(TAG, "Searching done, %d DS18B20 device(s) found",
           ds18b20_device_num);
}

void TemperatureSensor::init() {
  if (initialized)
    return;
  this->findSensors();
  initialized = true;
  xTaskCreate(requestTemp, "Temperature_main", 4096, this, 6, NULL);
}
void TemperatureSensor::requestTemp(void *pvParameters) {

  vTaskDelay((10 * 1000) / portTICK_PERIOD_MS);

  auto _this = (TemperatureSensor *)pvParameters;
  auto clock = Clock::GetInstance();
  // auto _this = TemperatureSensor::GetInstance();
  tm currentTime;

  float temp = 22;
  for (;;) {

    if (_this->tempSensorFound) {
      auto sensor = _this->ds18b20s;
      if (ds18b20_trigger_temperature_conversion(sensor) != ESP_OK ||
          ds18b20_get_temperature(sensor, &temp) != ESP_OK) {
        _this->tempSensorFound = false;
      } else {
        for (auto &&i : _this->tempCallbacks) {
          std::get<0>(i)(&temp, std::get<1>(i));
        }
      }
    } else {
      _this->findSensors();

      // TODO !!!!!!!!!REMOVE BEFORE DEPLOYMENT!!!!!!!!!!
      temp += 0.3;
      temp = temp > 25 ? 22 : temp;

      for (auto &&i : _this->tempCallbacks) {
        std::get<0>(i)(&temp, std::get<1>(i));
      }
    }

    clock->getCurrentTime(currentTime);

    vTaskDelay(
        ((60 - currentTime.tm_sec) * 1000) /
        portTICK_PERIOD_MS); // Wait till 2 seconds before next minute, so that
                             // this should finish before heater checks schedule
  }
}

void TemperatureSensor::addTempCallback(tempCallback callback,
                                        const void *additionalParameters) {

  tempCallbacks.push_back({callback, additionalParameters});
}

TemperatureSensor *TemperatureSensor::_instance = nullptr;

TemperatureSensor *TemperatureSensor::GetInstance() {
  if (_instance == nullptr) {
    _instance = new TemperatureSensor();
  }
  return _instance;
}