#include "storage.hpp"

Storage *Storage::_instance = nullptr;

static const char *NAMESPACE = "SUSCH";

Storage *Storage::GetInstance() {
  if (_instance == nullptr) {
    _instance = new Storage();
  }
  return _instance;
}

esp_err_t Storage::getReadHandle(nvs_handle_t *out_handle) {
  return nvs_open(NAMESPACE, NVS_READONLY, out_handle);
}
esp_err_t Storage::getWriteHandle(nvs_handle_t *out_handle) {
  return nvs_open(NAMESPACE, NVS_READWRITE, out_handle);

}

