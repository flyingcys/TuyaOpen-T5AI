/*
 * Copyright (c) 2019-2022, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdbool.h>
#include "target.h"
#include "flash_map/flash_map.h"
#include "flash_map_backend/flash_map_backend.h"
#include "bootutil_priv.h"
#include "bootutil/bootutil_log.h"
#include "Driver_Flash.h"
#include "sys_driver.h"
#include "bk_tfm_mpc.h"
#include <components/log.h>
#include <driver/flash.h>

#define TAG "flash_map"
#define FLASH_PROGRAM_UNIT    TFM_HAL_FLASH_PROGRAM_UNIT

/**
 * Return the greatest value not greater than `value` that is aligned to
 * `alignment`.
 */
#define FLOOR_ALIGN(value, alignment) ((value) & ~((alignment) - 1))

/**
 * Return the least value not less than `value` that is aligned to `alignment`.
 */
#define CEILING_ALIGN(value, alignment) \
                         (((value) + ((alignment) - 1)) & ~((alignment) - 1))

extern const struct flash_area flash_map[];
extern const int flash_map_entry_num;

/* Valid entries for data item width */
static const uint32_t data_width_byte[] = {
    sizeof(uint8_t),
    sizeof(uint16_t),
    sizeof(uint32_t),
};

/*
 * Check the target address in the flash_area_xxx operation.
 */
static bool is_range_valid(const struct flash_area *area,
                           uint32_t off,
                           uint32_t len)
{
    uint32_t size;

    if (!area) {
        return false;
    }

    if (!boot_u32_safe_add(&size, off, len)) {
        return false;
    }

    if (area->fa_size < size) {
        return false;
    }

    return true;
}

/*
 * `open` a flash area.  The `area` in this case is not the individual
 * sectors, but describes the particular flash area in question.
 */
int flash_area_open(uint8_t id, const struct flash_area **area)
{
    int i;

    BOOT_LOG_DBG("area %d", id);

    for (i = 0; i < flash_map_entry_num; i++) {
        if (id == flash_map[i].fa_id) {
            break;
        }
    }
    if (i == flash_map_entry_num) {
        return -1;
    }

    *area = &flash_map[i];
    return 0;
}

void flash_area_close(const struct flash_area *area)
{
    /* Nothing to do. */
}

/*
 * Read/write/erase. Offset is relative from beginning of flash area.
 * `off` and `len` can be any alignment.
 * Return 0 on success, other value on failure.
 */
int flash_area_read_dbus(const struct flash_area *area, uint32_t off, void *dst,
                    uint32_t len)
{
    SYS_LOCK_DECLARATION();
    SYS_LOCK();

    bk_flash_read_bytes(area->fa_off + off, dst, len);

    SYS_UNLOCK();
    return 0;
}

int flash_area_read(const struct flash_area *area, uint32_t off, void *dst,
                    uint32_t len)
{
#if CONFIG_OTA_OVERWRITE
    uint32_t fa_off = area->fa_off;
    if(area->fa_id == 0){
        fa_off = FLASH_PHY2VIRTUAL(CEIL_ALIGN_34(fa_off));
#if CONFIG_DBUS_CHECK_CRC
        if (check_crc(fa_off + off,len) == false) {
            BOOT_LOG_ERR("addr = %#x,len=%#x,crc16 fail", fa_off + off, len);
            return -1;
        }
#endif
        bk_flash_read_cbus(fa_off + off,dst,len);
        return 0;
    }
#endif

#if CONFIG_DIRECT_XIP
    uint32_t fa_off = area->fa_off;
    uint32_t fa_size = (FLASH_PHY2VIRTUAL(partition_get_phy_size(PARTITION_PRIMARY_ALL)))/4096*4096;
    if(off > fa_size){
        BK_LOGE(TAG, "cbus read offset 0x%x error\r\n",off);
        return -1;
    }

    if(area->fa_id == 0){
        fa_off = FLASH_PHY2VIRTUAL(CEIL_ALIGN_34(fa_off));
        bk_flash_read_cbus(fa_off + off,dst,len);
        return 0;
    } else if(area->fa_id == 1){
        fa_off =FLASH_PHY2VIRTUAL(CEIL_ALIGN_34(partition_get_phy_offset(PARTITION_PRIMARY_ALL)));
        enable_dcache(0);
        uint32_t enable = flash_get_excute_enable();
        bk_flash_read_cbus(0,dst,1);
        flash_set_excute_enable(1);
        bk_flash_read_cbus(fa_off + off,dst,len);
        flash_set_excute_enable(enable);
        enable_dcache(1);
        return 0;
    }
#endif

    uint32_t remaining_len, read_length;
    uint32_t aligned_off;
    uint32_t item_number;

    /* The maximum value of data_width is 4 bytes. */
    uint8_t temp_buffer[sizeof(uint32_t)];
    uint8_t data_width, i = 0, j;
    int ret = 0;

    ARM_FLASH_CAPABILITIES DriverCapabilities;

    BOOT_LOG_DBG("read area=%d, off=%#x, len=%#x", area->fa_id, off, len);

    if (!is_range_valid(area, off, len)) {
        return -1;
    }
    remaining_len = len;

    /* CMSIS ARM_FLASH_ReadData API requires the `addr` data type size aligned.
     * Data type size is specified by the data_width in ARM_FLASH_CAPABILITIES.
     */
    DriverCapabilities = DRV_FLASH_AREA(area)->GetCapabilities();
    data_width = data_width_byte[DriverCapabilities.data_width];
    aligned_off = FLOOR_ALIGN(off, data_width);

    /* Read the first data_width long data if `off` is not aligned. */
    if (aligned_off != off) {
        ret = DRV_FLASH_AREA(area)->ReadData(area->fa_off + aligned_off,
                                             temp_buffer,
                                             1);
        if (ret < 0) {
            return ret;
        }

        /* Record how many target data have been read. */
        read_length = off - aligned_off + len >= data_width ?
                                        data_width - (off - aligned_off) : len;

        /* Copy the read data from off. */
        for (i = 0; i < read_length; i++) {
            ((uint8_t *)dst)[i] = temp_buffer[i + off - aligned_off];
        }
        remaining_len -= read_length;
    }

    /* The `cnt` parameter in CMSIS ARM_FLASH_ReadData indicates number of data
     * items to read.
     */
    if (remaining_len) {
        item_number = remaining_len / data_width;
        if (item_number) {
            ret = DRV_FLASH_AREA(area)->ReadData(area->fa_off + off + i,
                                                (uint8_t *)dst + i,
                                                item_number);
            if (ret < 0) {
                return ret;
            }
            remaining_len -= item_number * data_width;
        }
    }
    if (remaining_len) {
        ret = DRV_FLASH_AREA(area)->ReadData(
                            area->fa_off + off + i + item_number * data_width,
                            temp_buffer,
                            1);
        if (ret < 0) {
            return ret;
        }
        for (j = 0; j < remaining_len; j++) {
            ((uint8_t *)dst)[i + item_number * data_width + j] = temp_buffer[j];
        }
    }

#if CONFIG_ENABLE_MCUBOOT_BL2
    extern int flash_area_read_post_hook(const struct flash_area *area, uint32_t off, void *dst, uint32_t len); //TODO move to better place
    flash_area_read_post_hook(area, off, dst, len);
#endif

    /* CMSIS ARM_FLASH_ReadData can return the number of data items read or
     * Status Error Codes which are negative for failures.
     */
    if (ret < 0) {
        return ret;
    } else {
        return 0;
    }
}

#if CONFIG_DIRECT_XIP
uint32_t flash_write_bytes_dbus(uint32_t address, const uint8_t *user_buf, uint32_t size)
{
    SYS_LOCK_DECLARATION();
    SYS_LOCK();

    uint32_t protect_type = bk_flash_get_protect_type();
    bk_flash_set_protect_type(0);
    bk_flash_write_bytes(address, user_buf, size);
    bk_flash_set_protect_type(protect_type);
    SYS_UNLOCK();
    return 0;
}
#endif

uint32_t flash_area_update_dbus(const struct flash_area *area, uint32_t off,
                                const void *src, uint32_t len)
{
    SYS_LOCK_DECLARATION();
    SYS_LOCK();
#if CONFIG_DIRECT_XIP
    uint32_t update_id = (flash_area_read_offset_enable() ^ 1);
    uint32_t fa_off = CEIL_ALIGN_34((partition_get_phy_offset(update_id)));
    uint32_t phy_addr = off + fa_off;
    bk_flash_write_bytes(phy_addr, src, len);
#endif
#if CONFIG_OTA_OVERWRITE
    bk_flash_write_bytes(area->fa_off + off, src, len);
#endif
    SYS_UNLOCK();
    return 0;
}
#if CONFIG_OTA_CONFIRM_UPDATE
void flash_write_confirm_dbus(uint8_t is_confirm)
{
    uint32_t phy_area_size = partition_get_phy_size(PARTITION_OTA_CONTROL);
    uint32_t phy_area_offset = partition_get_phy_offset(PARTITION_OTA_CONTROL);
    uint32_t status = OVERWRITE_CONFIRM;
    SYS_LOCK_DECLARATION();
    SYS_LOCK();
    if (is_confirm == true) {
        uint32_t confirm_offset = phy_area_size + phy_area_offset - 4;
        flash_area_erase_fast(phy_area_offset, phy_area_size);
        bk_flash_write_bytes(confirm_offset, (uint8_t*)&(status), 4);
    } else if(is_confirm == false) {
        flash_area_erase_fast(phy_area_offset, phy_area_size);
    }
    SYS_UNLOCK();
}
#endif
/* Writes `len` bytes of flash memory at `off` from the buffer at `src`.
 * `off` and `len` can be any alignment.
 */
int flash_area_write(const struct flash_area *area, uint32_t off,
                     const void *src, uint32_t len)
{
#if CONFIG_DIRECT_XIP
    SYS_LOCK_DECLARATION();
    SYS_LOCK();

    uint32_t fa_off = FLASH_PHY2VIRTUAL(CEIL_ALIGN_34(partition_get_phy_offset(PARTITION_PRIMARY_ALL)));
    uint32_t boundary = FLASH_PHY2VIRTUAL(CEIL_ALIGN_34(partition_get_phy_offset(PARTITION_NSPE)));
    uint32_t block_num = (FLASH_PHY2VIRTUAL(partition_get_phy_size(PARTITION_NSPE))) / (64*1024);
    uint32_t write_addr;
    uint32_t sec_off,nsec_off;

    bk_mpc_set_secure_attribute(MPC_DEV_FLASH,boundary,block_num,0); // turn NSPE to S,and resume
    enable_dcache(0);
    write_addr = (fa_off+off);
    write_addr |= 1 << 24;
    bk_flash_write_cbus(write_addr,src,len);
    enable_dcache(1);
    bk_mpc_set_secure_attribute(MPC_DEV_FLASH,boundary,block_num,1);

	SYS_UNLOCK();

    return 0;
#endif
    uint8_t add_padding[FLASH_PROGRAM_UNIT];
#if (FLASH_PROGRAM_UNIT == 1)
    uint8_t len_padding[FLASH_PROGRAM_UNIT]; /* zero sized arrayas are illegal C */
#else
    uint8_t len_padding[FLASH_PROGRAM_UNIT - 1];
#endif
    ARM_FLASH_CAPABILITIES DriverCapabilities;
    uint8_t data_width;
    /* The PROGRAM_UNIT aligned value of `off` */
    uint32_t aligned_off;

    /* The total write length. */
    uint32_t aligned_len;
    uint32_t i, k;

    /* The index in src[] that has been programmed. */
    uint32_t src_written_idx = 0;
    uint32_t add_padding_size, len_padding_size;
    uint32_t write_size;
    uint32_t last_unit_start_off;
    /*
     *    aligned_off           off           last_unit_start_off
     *        |                  |                     |
     *        | add_padding_size |                     |   | len_padding_size  |
     *        |+++++++++++++++++++**|******************|***@@@@@@@@@@@@@@@@@@@@|
     *        |                     |                  |                       |
     * ---->--|---- PROGRAM UNIT ---|-- PROGRAM UNIT --|---- PROGRAM UNIT -----|
     *        |                     |                  |                       |
     *        |+++++++++++++++++++**|******************|***@@@@@@@@@@@@@@@@@@@@|
     *                            |<-------- len --------->|
     */

    BOOT_LOG_DBG("write area=%d, off=%#x, len=%#x", area->fa_id, off, len);

    /* Align the target address. The area->fa_off should already be aligned. */
    aligned_off = FLOOR_ALIGN(off, FLASH_PROGRAM_UNIT);
    add_padding_size = off - aligned_off;
    if (!is_range_valid(area, off, len)) {
        return -1;
    }

    DriverCapabilities = DRV_FLASH_AREA(area)->GetCapabilities();
    data_width = data_width_byte[DriverCapabilities.data_width];

    if (FLASH_PROGRAM_UNIT)
    /* Read the bytes from aligned_off to off. */
    if (flash_area_read(area, aligned_off, add_padding, add_padding_size)) {
        return -1;
    }

    /* Align the write size */
    aligned_len = CEILING_ALIGN(len + add_padding_size, FLASH_PROGRAM_UNIT);
    len_padding_size = aligned_len - len - add_padding_size;
    if (!is_range_valid(area, aligned_off, aligned_len)) {
        return -1;
    }

    /* Read the bytes from (off + len) to (off + aligned_len). */
    if (flash_area_read(area, off + len, len_padding,
                        len_padding_size)) {
        return -1;
    }

    /* Program the first FLASH_PROGRAM_UNIT. */
    if (add_padding_size) {
        /* Fill the first program unit bytes with data from src. */
        for (i = add_padding_size, src_written_idx = 0;
             i < FLASH_PROGRAM_UNIT && src_written_idx < len;
             i++, src_written_idx++) {
            add_padding[i] = ((uint8_t *)src)[src_written_idx];
        }
        if (src_written_idx == len) {
            /* aligned_len equals to FLASH_PROGRAM_UNIT in this case.
             * Fill the len_padding_size datas into add_padding.
             */
            for (k = 0; i < FLASH_PROGRAM_UNIT && k < len_padding_size;
                 i++, k++) {
                add_padding[i] = len_padding[k];
            }
            if (k != len_padding_size) {
                return -1;
            }
        }

        /* Check the first program unit bytes are all filled. */
        if (i != FLASH_PROGRAM_UNIT) {
            return -1;
        }
        if (DRV_FLASH_AREA(area)->ProgramData(area->fa_off + aligned_off,
                                        add_padding,
                                        FLASH_PROGRAM_UNIT / data_width) < 0) {
            return -1;
        }
    }

    /* 'src_written_idx' indicates the number of the src data which has already
     * been programed into flash. 'src_written_idx' equals to 'len' means that
     * all the data in src has been programmed and aligned_len equals to
     * FLASH_PROGRAM_UNIT. This case has been handled above.
     * 'src_written_idx' less than 'len' means that not all the data in src has
     * been programmed.
     */
    if (src_written_idx < len) {
        /* Program from the first aligned bytes(src_written_idx) to the last
         * aligned bytes in src.
         */
        write_size = FLOOR_ALIGN(len - src_written_idx, FLASH_PROGRAM_UNIT);
        if (write_size > 0) {
            if (DRV_FLASH_AREA(area)->ProgramData(
                                           area->fa_off + off + src_written_idx,
                                           src,
                                           write_size / data_width) < 0) {
                return -1;
            }
            src_written_idx += write_size;
        }
        last_unit_start_off = src_written_idx;

        /* Program the last program unit data into flash. */
        if (len_padding_size) {
            /* Copy the last unaligned bytes in src to add_padding. */
            for (i = 0; i < FLASH_PROGRAM_UNIT && src_written_idx < len;
                 i++, src_written_idx++) {
                add_padding[i] = ((uint8_t *)src)[src_written_idx];
            }

            if (src_written_idx != len) {
                return -1;
            }
            /* Copy the len_padding_size bytes in len_padding to add_padding. */
            for (k = 0; i < FLASH_PROGRAM_UNIT && k < len_padding_size;
                 i++, k++) {
                add_padding[i] = len_padding[k];
            }
            write_size = add_padding_size + last_unit_start_off +
                         FLASH_PROGRAM_UNIT;
            if (i != FLASH_PROGRAM_UNIT || k != len_padding_size ||
                aligned_len != write_size) {
                return -1;
            }
            if (DRV_FLASH_AREA(area)->ProgramData(
                                area->fa_off + off + last_unit_start_off,
                                add_padding,
                                FLASH_PROGRAM_UNIT / data_width) < 0) {
                return -1;
            }
        }
    }

    return 0;
}

int flash_area_erase(const struct flash_area *area, uint32_t off, uint32_t len)
{
#if CONFIG_FLASH_FAST_ERASE
    int flash_area_erase_fast(uint32_t off, uint32_t len);
    return flash_area_erase_fast(area->fa_off + off, len);
#else
    ARM_FLASH_INFO *flash_info;
    uint32_t deleted_len = 0;
    int32_t rc = 0;

    BOOT_LOG_DBG("erase area=%d, off=%#x, len=%#x", area->fa_id, off, len);

    if (!is_range_valid(area, off, len)) {
        return -1;
    }

    flash_info = DRV_FLASH_AREA(area)->GetInfo();

    if (flash_info->sector_info == NULL) {
        /* Uniform sector layout */
        while (deleted_len < len) {
            rc = DRV_FLASH_AREA(area)->EraseSector(area->fa_off + off);
            if (rc != 0) {
                break;
            }
            deleted_len += flash_info->sector_size;
            off         += flash_info->sector_size;
        }
    } else {
        /* Inhomogeneous sector layout, explicitly defined
         * Currently not supported.
         */
    }

    return rc;
#endif
}

uint32_t flash_area_align(const struct flash_area *area)
{
    ARM_FLASH_INFO *flash_info;

    flash_info = DRV_FLASH_AREA(area)->GetInfo();
    return flash_info->program_unit;
}
