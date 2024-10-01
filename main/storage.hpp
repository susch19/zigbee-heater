#pragma once

#include <nvs.h>
#include <type_traits>

class Storage {

public:
  static Storage *GetInstance();

  Storage(Storage &other) = delete;
  void operator=(const Storage &) = delete;

  template <typename T> esp_err_t readValue(const char *key, T *out_value) {
    nvs_handle_t handle;
    auto res = getReadHandle(&handle);
    if (res != ESP_OK)
      return res;

    if (std::is_same<T, uint8_t>::value) {
      res = nvs_get_u8(handle, key, (uint8_t *)out_value);
    } else if (std::is_same<T, uint16_t>::value) {
      res = nvs_get_u16(handle, key, (uint16_t *)out_value);
    } else if (std::is_same<T, uint32_t>::value) {
      res = nvs_get_u32(handle, key, (uint32_t *)out_value);
    } else if (std::is_same<T, uint64_t>::value) {
      res = nvs_get_u64(handle, key, (uint64_t *)out_value);
    } else if (std::is_same<T, int16_t>::value) {
      res = nvs_get_i16(handle, key, (int16_t *)out_value);
    } else if (std::is_same<T, int32_t>::value) {
      res = nvs_get_i32(handle, key, (int32_t *)out_value);
    } else if (std::is_same<T, int64_t>::value) {
      res = nvs_get_i64(handle, key, (int64_t *)out_value);
    } else {
      res = ESP_FAIL;
    }
    nvs_close(handle);

    return res;
  }

  template <typename T>
  esp_err_t readValue(const char *key, T *out_value, size_t *length) {
    nvs_handle_t handle;
    auto res = getReadHandle(&handle);
    if (res != ESP_OK)
      return res;

    if (std::is_same<T, char>::value) {
      res = nvs_get_str(handle, key, (char *)out_value, length);
    } else if (std::is_same<T, void>::value) {
      res = nvs_get_blob(handle, key, (void *)out_value, length);
    } else {
      res = ESP_FAIL;
    }
    nvs_close(handle);

    return res;
  }

  template <typename T> esp_err_t writeValue(const char *key, T value) {
    nvs_handle_t handle;
    auto res = getWriteHandle(&handle);
    if (res != ESP_OK)
      return res;

    if (std::is_same<T, uint8_t>::value) {
      res = nvs_set_u8(handle, key, (uint8_t)value);
    } else if (std::is_same<T, uint16_t>::value) {
      res = nvs_set_u16(handle, key, (uint16_t)value);
    } else if (std::is_same<T, uint32_t>::value) {
      res = nvs_set_u32(handle, key, (uint32_t)value);
    } else if (std::is_same<T, uint64_t>::value) {
      res = nvs_set_u64(handle, key, (uint64_t)value);
    } else if (std::is_same<T, int16_t>::value) {
      res = nvs_set_i16(handle, key, (int16_t)value);
    } else if (std::is_same<T, int32_t>::value) {
      res = nvs_set_i32(handle, key, (int32_t)value);
    } else if (std::is_same<T, int64_t>::value) {
      res = nvs_set_i64(handle, key, (int64_t)value);
    } else {
      res = ESP_FAIL;
    }

    if (res == ESP_OK)
      res = nvs_commit(handle);
    nvs_close(handle);

    return res;
  }

  template <typename T>
  esp_err_t writeValue(const char *key, const T *value, size_t length) {
    nvs_handle_t handle;
    auto res = getWriteHandle(&handle);
    if (res != ESP_OK)
      return res;

    if (std::is_same<T, char>::value) {
      res = nvs_set_str(handle, key, (const char *)value);
    } else if (std::is_same<T, void>::value) {
      res = nvs_set_blob(handle, key, (const void *)value, length);
    } else {
      res = ESP_FAIL;
    }
    if (res == ESP_OK)
      res = nvs_commit(handle);

    nvs_close(handle);

    return res;
  }

protected:
  static Storage *_instance;
  Storage() {}

private:
  esp_err_t getReadHandle(nvs_handle_t *out_handle);
  esp_err_t getWriteHandle(nvs_handle_t *out_handle);
};