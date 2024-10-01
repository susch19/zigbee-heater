
#include "esp_ota.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include <algorithm>
#include <esp_err.h>
#include <zlib.h>

static const char *TAG = "OTA";

CompressedOTA::~CompressedOTA() {
  if (part_) {
    esp_ota_abort(handle_);
  }

  if (zlib_init_) {
    inflateEnd(&zlib_stream_);
  }
}

esp_err_t CompressedOTA::start() {
  if (zlib_init_) {
    inflateEnd(&zlib_stream_);
    zlib_init_ = false;
  }

  zlib_stream_ = {};
  zlib_stream_.zalloc = Z_NULL;
  zlib_stream_.zfree = Z_NULL;
  zlib_stream_.opaque = Z_NULL;
  zlib_stream_.next_in = nullptr;
  zlib_stream_.avail_in = 0;

  int ret = inflateInit(&zlib_stream_);

  if (ret == Z_OK) {
    zlib_init_ = true;
  } else {
    ESP_LOGE(TAG, "zlib init failed: %d", ret);
    return ESP_FAIL;
  }

  if (part_) {
    ESP_LOGE(TAG, "OTA already started");
    part_ = nullptr;
    esp_ota_abort(handle_);
    return ESP_FAIL;
  }

  part_ = esp_ota_get_next_update_partition(nullptr);
  if (!part_) {
    ESP_LOGE(TAG, "No next OTA partition");
    return ESP_ERR_INVALID_SIZE;
  }

  esp_err_t err = esp_ota_begin(part_, OTA_WITH_SEQUENTIAL_WRITES, &handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error starting OTA: %d", err);
    part_ = nullptr;
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t CompressedOTA::write(uint8_t *data, size_t size) {
  return write(data, size, false);
}

esp_err_t CompressedOTA::write(uint8_t *data, size_t size, bool flush) {
  uint8_t buf[256];

  if (!part_) {
    return ESP_FAIL;
  }

  zlib_stream_.avail_in = size;
  zlib_stream_.next_in = data;

  do {
    zlib_stream_.avail_out = sizeof(buf);
    zlib_stream_.next_out = buf;

    int ret = inflate(&zlib_stream_, flush ? Z_FINISH : Z_NO_FLUSH);
    if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT || ret == Z_DATA_ERROR ||
        ret == Z_MEM_ERROR) {
      ESP_LOGE(TAG, "zlib error: %d", ret);
      esp_ota_abort(handle_);
      part_ = nullptr;
      return ESP_FAIL;
    }

    size_t available = sizeof(buf) - zlib_stream_.avail_out;

    if (available > 0) {
      esp_err_t err = esp_ota_write(handle_, buf, available);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error writing OTA: %d", err);
        esp_ota_abort(handle_);
        part_ = nullptr;
        return ESP_FAIL;
      }
    }
  } while (zlib_stream_.avail_in > 0 || zlib_stream_.avail_out == 0);

  return ESP_OK;
}

esp_err_t CompressedOTA::finish() {
  if (!part_) {
    ESP_LOGE(TAG, "OTA not running");
    return ESP_FAIL;
  }

  if (!write(nullptr, 0, true)) {
    return ESP_FAIL;
  }

  esp_err_t err = esp_ota_end(handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error ending OTA: %d", err);
    part_ = nullptr;
    return ESP_FAIL;
  }

  inflateEnd(&zlib_stream_);
  zlib_init_ = ESP_FAIL;

  err = esp_ota_set_boot_partition(part_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error setting boot partition: %d", err);
    part_ = nullptr;
    return ESP_FAIL;
  }

  part_ = nullptr;
  return ESP_OK;
}

esp_err_t CompressedOTA::zbOTAUpgradeStatusHandler(
    esp_zb_zcl_ota_upgrade_value_message_t *message) {
  static uint32_t total_size = 0;
  static uint32_t offset = 0;
  static int64_t start_time = 0;
  esp_err_t ret = ESP_OK;
  uint8_t *payload = message->payload;
  size_t payload_size = message->payload_size;

  if (message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS) {
    switch (message->upgrade_status) {
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
      ESP_LOGI(TAG, "-- OTA upgrade start");
      start_time = esp_timer_get_time();
      ESP_RETURN_ON_ERROR(ret, TAG, "Failed to begin OTA partition, status: %s",
                          esp_err_to_name(ret));
      ret = this->start();
      break;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
      /* Read and process the first sub-element, ignoring everything else */
      while (ota_header.size() < 6 && payload_size > 0) {
        ota_header.push_back(payload[0]);
        payload++;
        payload_size--;
        ESP_LOGI(TAG, "Reduced payload size to %d", payload_size);
        total_size = message->ota_header.image_size - 6;
      }

      if (!ota_upgrade_subelement && ota_header.size() == 6) {
        if (ota_header[0] == 0 && ota_header[1] == 0) {
          ota_upgrade_subelement = true;
          ota_data_len = (((int)ota_header[5] & 0xFF) << 24) |
                         (((int)ota_header[4] & 0xFF) << 16) |
                         (((int)ota_header[3] & 0xFF) << 8) |
                         ((int)ota_header[2] & 0xFF);
          ESP_LOGD(TAG, "OTA sub-element size %zu", ota_data_len);
        } else {
          ESP_LOGE(TAG, "OTA sub-element type %02x%02x not supported",
                   ota_header[0], ota_header[1]);
          // this->reset();
          return ESP_FAIL;
        }
      }

      if (ota_data_len) {
        payload_size = std::min(ota_data_len, payload_size);
        ota_data_len -= payload_size;

        offset += payload_size;

        ESP_LOGI(TAG, "-- OTA Client receives data: progress [%ld/%ld]", offset,
                 total_size);
        if (write(payload, payload_size) != ESP_OK) {
          // this->reset();
          return ESP_FAIL;
        }
      }

      // total_size = message->ota_header.image_size;
      // offset += message->payload_size;
      // ESP_LOGI(TAG, "-- OTA Client receives data: progress [%ld/%ld]",
      // offset,
      //          total_size);
      // if (message->payload_size && message->payload) {
      //   ret = this->write((uint8_t *)message->payload,
      //   message->payload_size); ESP_RETURN_ON_ERROR(ret, TAG,
      //                       "Failed to write OTA data to partition, status:
      //                       %s", esp_err_to_name(ret));
      // }
      break;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
      ESP_LOGI(TAG, "-- OTA upgrade apply");
      break;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
      ret = offset == total_size ? ESP_OK : ESP_FAIL;
      ESP_LOGI(TAG, "-- OTA upgrade check status: %s", esp_err_to_name(ret));
      break;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
      ESP_LOGI(TAG, "-- OTA Finish");
      ESP_LOGI(TAG,
               "-- OTA Information: version: 0x%lx, manufacturer code: 0x%x, "
               "image type: 0x%x, total size: %ld bytes, cost time: %lld ms,",
               message->ota_header.file_version,
               message->ota_header.manufacturer_code,
               message->ota_header.image_type, message->ota_header.image_size,
               (esp_timer_get_time() - start_time) / 1000);
      ret = this->finish();
      ESP_LOGW(TAG, "Prepare to restart system");
      esp_restart();
      break;
    default:
      ESP_LOGI(TAG, "OTA status: %d", message->upgrade_status);
      break;
    }
  }
  return ret;
}