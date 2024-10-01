#pragma once

#include "storage.hpp"
#include <stdint.h>
#include <time.h>

class Clock {

public:
  void init();
  void updateTime(uint32_t utcTime);
  /// @brief Calculated time zone based on the local time
  /// @param localTime 
  void updateTimeZone(uint32_t localTime);
  void getCurrentTime(tm &tm);
  void syncTimeRequest();

  uint32_t zb_time = 0;
  uint8_t timeStatus = 0;
  uint32_t localTime = 0;
  int32_t timeZoneOffsetInSeconds = 0;

  static Clock *GetInstance();

  Clock(Clock &other) = delete;
  void operator=(const Clock &) = delete;

protected:
  static void regularTimeSync(void *parameter);
  static Clock *_instance;
  Clock() {}

  Storage *storage;

private:
  
  bool initialized = false;

};