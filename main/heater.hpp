#pragma once
#include "clock.hpp"
#include "custom_cluster.hpp"
#include "custom_zigbee_types/schedule.hpp"
#include "esp_zigbee_core.h"
#include "storage.hpp"
#include "temperature_sensor.hpp"
#include "zcl/esp_zigbee_zcl_common.h"

#include <cstdlib>
#include <map>
#include <sys/time.h>
#include <unordered_map>
#include <vector>
#include "driver/gpio.h"

//D3 on SeedStudio ESP32C6
#define HEATER_GPIO_PIN GPIO_NUM_21 

enum class DayOfWeekW : uint8_t { Sun, Mon, Tue, Wed, Thu, Fri, Sat, Vac };

class Heater {

protected:
  typedef struct TimeTempMessage {
    DayOfWeekW DayOfWeek;
    uint16_t Time;
    int16_t Temp;

    inline bool operator==(TimeTempMessage ttm) {
      return ttm.DayOfWeek == DayOfWeek && ttm.Time == Time && ttm.Temp == Temp;
    }

    bool operator==(TimeTempMessage ttm) const {
      return ttm.DayOfWeek == DayOfWeek && ttm.Time == Time && ttm.Temp == Temp;
    }

    inline bool operator!=(TimeTempMessage ttm) {
      return ttm.DayOfWeek != DayOfWeek || ttm.Time != Time || ttm.Temp != Temp;
    }

    bool operator!=(TimeTempMessage ttm) const {
      return ttm.DayOfWeek != DayOfWeek || ttm.Time != Time || ttm.Temp != Temp;
    }
  } TimeTempMessage;

public:
  void init();
  void updateSchedule(esp_zb_weekly_schedule_header_t header,
                      esp_zb_weekly_schedule_single_s *data);
  void updateCustomSchedule(esp_zb_custom_weekly_schedule_header_t header,
                            esp_zb_custom_weekly_schedule_t *data);
  void printSchedule();
  static void loadLatestZigbeeAttributeValues();
  void
  updateSystemMode(uint8_t newMode); // heater->thermostat_cluster.system_mode
  void
  updateManualTemp(int16_t newTarget);    /*      heater->manualTemp = *(int16_t
     *)message->attribute.data.value;
     Clock::getCurrentTime(heater->manualModeRecv);{} */
  void updateRemoteTemp(int16_t newTemp); /*
      heater->remoteTemp = value;
      gettimeofday(&heater->remoteRecv, NULL); */
  void
  updateRuntime(uint32_t newRuntime); //      heater->runtime_in_seconds = ;
  void runHeatCheck();

  static Heater *GetInstance();

  Heater(Heater &other) = delete;
  void operator=(const Heater &) = delete;

protected:
  static Heater *_instance;
  static void checkScheduleTask(void *pvParameters);
  Heater() {}

private:
  bool initialized = false;
  // static const uint8_t HEATERPIN = D3;
  // static const uint8_t HEATERPING = D5;
  // static const uint8_t SENSORPING = D0;
  void loadStoredState();
  static void measuredTemperature(float *temp, const void *parameters);
  void reportHeatingMode(bool mode);
  void insert(std::vector<Heater::TimeTempMessage> &cont,
              Heater::TimeTempMessage value);

  std::unordered_map<DayOfWeekW, std::vector<esp_zb_weekly_schedule_single_s>>
      config;

public:
  uint32_t runtime_in_seconds = 0;
  esp_zb_thermostat_cluster_cfg_s thermostat_cluster;
  int16_t localSensorTemp = 0;
  int16_t manualTemp = 0;
  timeval manualModeRecv;
  int16_t remoteTemp = 0;
  timeval remoteRecv;
  uint8_t setpointChangeSource = 1;
  uint8_t temperatureSource = 0x1;
  uint32_t currentTarget = 0;

private:
  Storage *storage;
  Clock *clock;
  TemperatureSensor *tempSensor;
  std::vector<TimeTempMessage> schedules = {};
  Heater::TimeTempMessage manualMsg = {};
  tm currentTime = {};
  timeval tv = {};
  bool enableHeatCheck = false;
  bool isHeating = false;
};