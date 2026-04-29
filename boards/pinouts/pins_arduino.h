#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32)
#include "esp32.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp32s3.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C5)
#include "esp32c5.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#include "esp32c6.h"
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
#include "esp32p4.h"
#else
#error "Unsupported target, add pinouts file for this target on boards/pinouts/ and set include on this file"
#endif
