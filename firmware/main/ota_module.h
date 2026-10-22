#ifndef _OTA_MODULE_H_
#define _OTA_MODULE_H_

#include "esp_err.h"

// 从 HTTP 地址下载固件并 OTA 升级（成功后自动重启）
esp_err_t ota_start_from_url(const char *url);

#endif // _OTA_MODULE_H_
