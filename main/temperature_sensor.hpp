#pragma once

#include "ds18b20.h"
#include <esp_log.h>
#include <tuple>
#include <vector>

typedef void (*tempCallback)(float *temp, const void *additionalParameters);

class TemperatureSensor {

public:
  void init();
  void addTempCallback(tempCallback, const void *additionalParameters);

  static TemperatureSensor *GetInstance();

  TemperatureSensor(TemperatureSensor &other) = delete;
  void operator=(const TemperatureSensor &) = delete;

protected:
  static TemperatureSensor *_instance;
  TemperatureSensor() {}

private:
  static void requestTemp(void *pvParameters);
  void findSensors();

public:
  bool tempSensorFound = false;

private:
  bool initialized = false;
  ds18b20_device_handle_t ds18b20s;
  std::vector<std::tuple<tempCallback, const void *>> tempCallbacks;
  onewire_bus_handle_t bus = NULL;
};
