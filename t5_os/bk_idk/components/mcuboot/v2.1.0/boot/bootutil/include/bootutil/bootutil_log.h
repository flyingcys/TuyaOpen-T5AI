/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2017 Linaro Limited
 * Copyright (c) 2024 Beken
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef H_BOOTUTIL_LOG_H_
#define H_BOOTUTIL_LOG_H_

#include "ignore.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <mcuboot_config/mcuboot_config.h>

#ifdef MCUBOOT_HAVE_LOGGING
#include <mcuboot_config/mcuboot_logging.h>

#define BOOT_LOG_FORCE(...) MCUBOOT_LOG_FORCE(__VA_ARGS__)
#define BOOT_LOG_ERR(...) MCUBOOT_LOG_ERR(__VA_ARGS__)
#define BOOT_LOG_WRN(...) MCUBOOT_LOG_WRN(__VA_ARGS__)
#define BOOT_LOG_INF(...) MCUBOOT_LOG_INF(__VA_ARGS__)
#define BOOT_LOG_DBG(...) MCUBOOT_LOG_DBG(__VA_ARGS__)
#define BOOT_LOG_SIM(...) MCUBOOT_LOG_SIM(__VA_ARGS__)

#define BOOT_LOG_MODULE_DECLARE(module)  MCUBOOT_LOG_MODULE_DECLARE(module)
#define BOOT_LOG_MODULE_REGISTER(module) MCUBOOT_LOG_MODULE_REGISTER(module)

#else

#define BOOT_LOG_FORCE(...) IGNORE(__VA_ARGS__)
#define BOOT_LOG_ERR(...) IGNORE(__VA_ARGS__)
#define BOOT_LOG_WRN(...) IGNORE(__VA_ARGS__)
#define BOOT_LOG_INF(...) IGNORE(__VA_ARGS__)
#define BOOT_LOG_DBG(...) IGNORE(__VA_ARGS__)
#define BOOT_LOG_SIM(...) IGNORE(__VA_ARGS__)

#define BOOT_LOG_MODULE_DECLARE(module)
#define BOOT_LOG_MODULE_REGISTER(module)

#endif /* MCUBOOT_HAVE_LOGGING */

#ifdef __cplusplus
}
#endif

#endif
