/**
 * Copyright (C), 2018-2018, Arm Technology (China) Co., Ltd.
 * All rights reserved
 *
 * The content of this file or document is CONFIDENTIAL and PROPRIETARY
 * to Arm Technology (China) Co., Ltd. It is subject to the terms of a
 * License Agreement between Licensee and Arm Technology (China) Co., Ltd
 * restricting among other things, the use, reproduction, distribution
 * and transfer.  Each of the embodiments, including this information and,,
 * any derivative work shall retain this copyright notice.
 */

#include "hal.h"
#include "pal.h"
#include "mem_layout.h"
#include "hal_src_internal.h"

static uint8_t fota_rsa_key_n[] = {
    0xba, 0xfb, 0x3f, 0x09, 0x98, 0x3e, 0x7f, 0xd7, 0x53, 0x69, 0x2b, 0x97,
    0x03, 0xd6, 0x01, 0x25, 0x6f, 0x70, 0x3e, 0x71, 0xab, 0x46, 0x82, 0x12,
    0xb4, 0xa1, 0x7d, 0x17, 0x65, 0x2a, 0x43, 0x1d, 0x92, 0x01, 0x94, 0x0a,
    0x18, 0xbb, 0xe7, 0xd8, 0x69, 0xd7, 0x4a, 0xc0, 0xe6, 0xdb, 0x99, 0xa3,
    0xed, 0x6d, 0xa8, 0x71, 0x72, 0x2d, 0x6e, 0xb4, 0x43, 0x5b, 0xa8, 0x6e,
    0xa0, 0xf4, 0x2b, 0xcc, 0x6c, 0x1e, 0x0b, 0x16, 0x8f, 0xb5, 0x80, 0x47,
    0x4a, 0x42, 0xf6, 0xfc, 0x5c, 0xe0, 0xd5, 0x80, 0xf7, 0x09, 0x95, 0x79,
    0x11, 0x38, 0x18, 0x8d, 0x65, 0x96, 0x99, 0xe9, 0x6f, 0x93, 0xf4, 0x52,
    0xad, 0xae, 0x6b, 0x26, 0x27, 0x8d, 0xba, 0x38, 0x12, 0xd8, 0x7f, 0x0f,
    0x2a, 0x78, 0x7e, 0xd3, 0xf6, 0xf6, 0x01, 0xd6, 0x5a, 0x8b, 0x70, 0xf7,
    0x10, 0x63, 0x20, 0x17, 0x23, 0xc9, 0x14, 0x27};
static uint8_t fota_rsa_key_e[] = {0x01, 0x00, 0x01};

// clang-format off.
/**
 * How to get rsa_key_n and rsa_key_e from rsa Priviate key:
 *
 * 1. get the PEM format rsa pubkey from FOTA SaaS.
 *  Where the rsaprikey.pem is the FOTA rsa privatekey from FOTA SaaS.
 *
 * 2. run the following command to get the modulus and publicExponent text:
 *  openssl rsa -in pubkey.pem -pubin -text -noout
 *
 * 3. save the modulus to rsa_n, publicExponent to rsa_e.
 * you may need to do some format converting to save as c style.
 * NOTE: delete the first 00 in modulus if you have.
 *
 * One example:
Private-Key: (1024 bit)
modulus:
    00:d0:75:c5:28:66:d7:0f:71:ba:f4:60:c0:9b:59:
    a6:c2:d7:79:7f:b2:b5:03:02:62:f5:77:59:d5:3d:
    b4:b1:7c:b4:53:75:85:35:83:23:9f:99:8e:1c:2d:
    8c:79:18:3d:e6:1c:b3:44:c5:04:cc:84:dc:86:a0:
    a4:d7:6d:57:9b:e8:79:50:94:a9:a8:1e:d6:31:d8:
    b4:4e:c1:21:65:4b:c8:95:f4:cc:58:09:43:66:70:
    79:5e:86:a1:65:5b:d3:e4:1a:1f:a8:ad:9e:65:20:
    f7:73:2b:40:c2:10:36:3b:a3:6d:3f:15:51:62:4b:
    af:39:29:71:4a:81:69:54:7b
publicExponent: 65537 (0x10001)

 * and we convert modulus to rsa_n:
    0xd0, 0x75, 0xc5, 0x28, 0x66, 0xd7, 0x0f, 0x71, 0xba, 0xf4, 0x60, 0xc0,
    0x9b, 0x59, 0xa6, 0xc2, 0xd7, 0x79, 0x7f, 0xb2, 0xb5, 0x03, 0x02, 0x62,
    0xf5, 0x77, 0x59, 0xd5, 0x3d, 0xb4, 0xb1, 0x7c, 0xb4, 0x53, 0x75, 0x85,
    0x35, 0x83, 0x23, 0x9f, 0x99, 0x8e, 0x1c, 0x2d, 0x8c, 0x79, 0x18, 0x3d,
    0xe6, 0x1c, 0xb3, 0x44, 0xc5, 0x04, 0xcc, 0x84, 0xdc, 0x86, 0xa0, 0xa4,
    0xd7, 0x6d, 0x57, 0x9b, 0xe8, 0x79, 0x50, 0x94, 0xa9, 0xa8, 0x1e, 0xd6,
    0x31, 0xd8, 0xb4, 0x4e, 0xc1, 0x21, 0x65, 0x4b, 0xc8, 0x95, 0xf4, 0xcc,
    0x58, 0x09, 0x43, 0x66, 0x70, 0x79, 0x5e, 0x86, 0xa1, 0x65, 0x5b, 0xd3,
    0xe4, 0x1a, 0x1f, 0xa8, 0xad, 0x9e, 0x65, 0x20, 0xf7, 0x73, 0x2b, 0x40,
    0xc2, 0x10, 0x36, 0x3b, 0xa3, 0x6d, 0x3f, 0x15, 0x51, 0x62, 0x4b, 0xaf,
    0x39, 0x29, 0x71, 0x4a, 0x81, 0x69, 0x54, 0x7b
 * convert publicExponnet to rsa_e:
 * 0x01, 0x00, 0x01
 */
// clang-format on.

hal_ret_t hal_fota_get_pubkey(hal_crypto_key_t *key)
{
    hal_ret_t ret = HAL_OK;

    HAL_CHECK_CONDITION(key, HAL_ERR_BAD_PARAM, "Parameter key is NULL!\n");

    /* Currently FOTA only support RSA signing */
    key->key_type          = HAL_CRYPTO_KEY_RSA_1024;
    key->rsa_pubkey.n      = fota_rsa_key_n;
    key->rsa_pubkey.n_size = sizeof(fota_rsa_key_n);
    key->rsa_pubkey.e      = fota_rsa_key_e;
    key->rsa_pubkey.e_size = sizeof(fota_rsa_key_e);

    ret = HAL_OK;
finish:
    return ret;
}

hal_ret_t hal_fota_get_cache_partition_info(hal_addr_t *addr, size_t *size)
{
    if (!addr || !size) {
        PAL_LOG_ERR(
            "Parameter addr (0x%x) or size (0x%x) is NULL!\n", addr, size);
        return HAL_ERR_BAD_PARAM;
    }
    *addr = LAYOUT_SPI_FLASH_START + LAYOUT_CACHE_PARTITION_FLASH_OFFSET;
    *size = LAYOUT_CACHE_PARTITION_FLASH_SIZE;
    return HAL_OK;
}