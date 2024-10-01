#pragma once

#include "esp_zigbee_core.h"

#include <esp_ota_ops.h>
#include <vector>
#include <zlib.h>

#include <memory>

/* Zigbee configuration */
#define OTA_UPGRADE_MANUFACTURER                                               \
  0xDB15 /* The attribute indicates the file version of the downloaded image   \
            on the device*/
#define OTA_UPGRADE_IMAGE_TYPE                                                 \
  0x1011 /* The attribute indicates the value for the manufacturer of the      \
            device */
#define OTA_UPGRADE_RUNNING_FILE_VERSION                                       \
  0x01010101 /* The attribute indicates the file version of the running        \
                firmware image on the device */
#define OTA_UPGRADE_DOWNLOADED_FILE_VERSION                                    \
  0x01010101 /* The attribute indicates the file version of the downloaded     \
                firmware image on the device */
#define OTA_UPGRADE_HW_VERSION                                                 \
  0x0001 /* The parameter indicates the version of hardware */
#define OTA_UPGRADE_MAX_DATA_SIZE                                              \
  223 /* The recommended OTA image block size                                  \
       */

class CompressedOTA {
public:
  CompressedOTA() = default;
  ~CompressedOTA();

  esp_err_t start();
  esp_err_t write(uint8_t *data, size_t size);
  esp_err_t finish();
  esp_err_t
  zbOTAUpgradeStatusHandler(esp_zb_zcl_ota_upgrade_value_message_t *message);

private:
  esp_err_t write(uint8_t *data, size_t size, bool flush);

  bool zlib_init_{false};
  z_stream zlib_stream_;
  const esp_partition_t *part_{nullptr};
  esp_ota_handle_t handle_{0};

  std::vector<uint8_t> ota_header;
  bool ota_upgrade_subelement;
  size_t ota_data_len{0};
};