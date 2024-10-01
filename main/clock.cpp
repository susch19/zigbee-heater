#include "clock.hpp"
#include "esp_log.h"
#include "esp_zb_thermostat.hpp"
#include "storage.hpp"
#include "zcl/esp_zigbee_zcl_command.h"
#include <sys/_timeval.h>
#include <sys/time.h>
#include "freertos/task.h"


static const char *TAG = "CLOCK";

void Clock::init() {
  if (initialized)
    return;
  storage = Storage::GetInstance();
  int32_t timeZone = 0;
  if (storage->readValue("timeZone", &timeZone) == ESP_OK)
    this->timeZoneOffsetInSeconds = timeZone;
  initialized = true;

  xTaskCreate(regularTimeSync, "TimeSync_main", 4096, this, 6, NULL);
}

void Clock::updateTime(uint32_t utcTime) {
  this->zb_time = utcTime;
  uint32_t len;
  auto res = storage->readValue("clock", &len);
  ESP_LOGI(TAG,
           "Last sync time %lx, new value should be %lx. Result from save: %x",
           len, utcTime, res);

  int64_t sinceZero = utcTime;
  //   sinceZero += 59926608000;
  ESP_LOGI(TAG, "Seconds since start: %lld", sinceZero);
  timeval tv{sinceZero, 0};

  settimeofday(&tv, NULL);

  //   gettimeofday(&tv, NULL);
  time_t now;
  struct tm tm;
  time(&now);
  localtime_r(&now, &tm);

  res = storage->writeValue("clock", utcTime);

  ESP_LOGI(TAG, "New Time is: %2d.%2d.%4d %2d:%2d:%d. Saving was: %x",
           tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min,
           tm.tm_sec, res);
}

void Clock::updateTimeZone(uint32_t localTime) {
  auto offset = localTime - this->zb_time;
  if (offset > 86400 && offset < 86400) {
    ESP_LOGI(TAG, "The offset is outside of the 24h bound, value beeing %ld",
             offset);
    return;
  }
  ESP_LOGI(TAG, "Setting the offset to %ld", offset);
  timeZoneOffsetInSeconds = offset;
  storage->writeValue("timeZone", timeZoneOffsetInSeconds);
}

void Clock::getCurrentTime(tm &tm) {
  //   timeval tv;
  //   gettimeofday(&tv, NULL);
  time_t now;
  time(&now);
  localtime_r(&now, &tm);
}

void Clock::syncTimeRequest() {

  esp_zb_zcl_read_attr_cmd_t read_req;
  read_req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;

  read_req.zcl_basic_cmd.dst_addr_u.addr_short = 0x0;
  read_req.zcl_basic_cmd.dst_endpoint = HA_THERMOSTAT_ENDPOINT;
  read_req.zcl_basic_cmd.src_endpoint = HA_THERMOSTAT_ENDPOINT;
  read_req.clusterID = ESP_ZB_ZCL_CLUSTER_ID_TIME;

  uint16_t attributes[] = {ESP_ZB_ZCL_ATTR_TIME_TIME_ID};
  read_req.attr_number = 1;
  read_req.attr_field = attributes;

  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_read_attr_cmd_req(&read_req);
  esp_zb_lock_release();
}

void Clock::regularTimeSync(void *parameter) {
  auto _this = (Clock *)parameter;

  for (;;) {
    vTaskDelay((10000 * 1000) / portTICK_PERIOD_MS); //Sync every ~3h
    _this->syncTimeRequest();
  }
}

Clock *Clock::_instance = nullptr;

Clock *Clock::GetInstance() {
  if (_instance == nullptr) {
    _instance = new Clock();
  }
  return _instance;
}