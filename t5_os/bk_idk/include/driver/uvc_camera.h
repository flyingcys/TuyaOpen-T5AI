// Copyright 2020-2021 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <driver/uvc_camera_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief     Init the uvc
 *
 * This API init psram, dma, uvc param, and open uvc
 *
 * @param data configure for camera
 *
 * @return
 *    - kNoErr: succeed
 *    - others: other errors.
 */
bk_err_t bk_uvc_camera_driver_init(uvc_camera_config_t * config);

/**
 * @brief     Deinit the uvc
 *
 * This API deinit dma and close uvc
 *
 * @attation: called after bk_uvc_camera_driver_init
 *
 * @return
 *    - kNoErr: succeed
 *    - others: other errors.
 */
bk_err_t bk_uvc_camera_driver_deinit(void);

/**
 * @brief     uvc power on
 *
 * This API called by user, before calling bk_uvc_camera_driver_init, you should power on uvc
 *
 * @param enable power up/down:1/0
 * @param enable 1:high power leve for power up; 0 low power lever for power up
 *
 * @attation 1. This api config different gpio and different active level
 *
 * @return
 *    - kNoErr: succeed
 *    - others: other errors.
 */
bk_err_t bk_uvc_camera_power_enable(uint8_t enable, uint8_t active_level);

/**
 * @brief     Get uvc config
 *
 * This API called by user, get uvc support fps and resolutions
 *
 * @param param the memory user malloc for save uvc support config
 *
 * @param count the param1 malloc size, the count config of uvc support
 *
 * @return
 *    - BK_OK: succeed
 *    - others: other errors.
 */
bk_err_t bk_uvc_camera_get_config(uvc_camera_device_t *param, uint16_t count);

/**
 * @brief     Set uvc config
 *
 * This API called by user, Set uvc support fps and resolutions
 *
 * @param mode the uvc work type, reference ``media_camera_type_t``
 * @param config the uvc need support param the user set
 *
 * @return
 *    - kNoErr: succeed
 *    - others: other errors.
 */
//bk_err_t bk_uvc_camera_set_config(media_camera_type_t type, uvc_camera_device_t *config);

/**
 * @brief     Set uvc start
 *
 * This API called by user, Set uvc start transfer
 *
 * @attation 1. this api called must after uvc have been init and have stopped
 *
 * @return
 *    - kNoErr: succeed
 *    - others: other errors.
 */
bk_err_t bk_uvc_camera_start(void);

/**
 * @brief     Stop uvc start
 *
 * This API called by user, Set uvc stop transfer
 *
 * @attation 1. this api called must after uvc have been init and have start
 *
 * @return
 *    - kNoErr: succeed
 *    - others: other errors.
 */
bk_err_t bk_uvc_camera_stop(void);

/**
 * @brief     Get current uvc device
 *
 * This API called by user, get currnet uvc device in working
 *
 * @attation 1. if no uvc work, the return will be NULL
 *
 * @return
 *    - uvc_camera_device_t *: current working uvc device ptr
 *    - NULL: no uvc device working
 */
media_camera_device_t *bk_uvc_camera_get_device(void);

/**
 * @brief     register callback notify uvc device info
 *
 * This API called by user, before uvc start work
 *
 * @attation 1. this function will be called when uvc connect success, if you register
 *
 * @return
 *    - BK_OK: succeed
 *    - others: other errors.
 */
bk_err_t bk_uvc_camera_register_info_callback(uvc_device_info_t cb);

/**
 * @brief     set uvc params
 *
 * This API called by user, after uvc bk_uvc_camera_driver_init, if you called bk_uvc_camera_register_info_callback
 *
 * @attation 1. this function need user to call, when register info callback, need set config reference struct bk_uvc_config_t
 *
 * @attation 2. this function will use config param to start uvc
 *
 * @return
 *    - BK_OK: succeed
 *    - others: other errors.
 */
bk_err_t bk_uvc_camera_set_config(bk_uvc_config_t *config);

/**
 * @brief     check input frame length
 *
 * This API called by user, check frame valied length, check eof(0xffd9), some frame after ffd9 have invailed data
 *
 * @param 1. The frame start address
 * @param 2. the frame total length
 *
 * @attation, the check length control by marco CONFIG_JPEG_FRAME_CHECK_LENGTH, default value 1024
 *
 * @return
 *    - length: actual data length
 *    - others: cannot find 0xffd9, this frame have error
 */

int bk_uvc_camera_jpeg_frame_check_eof(uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

