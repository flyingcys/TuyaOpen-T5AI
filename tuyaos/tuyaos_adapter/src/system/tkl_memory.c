/**
 * @file tkl_memory.c
 * @brief the default weak implements of tuya hal memory, this implement only used when OS=linux
 * @version 0.1
 * @date 2020-05-15
 * 
 * @copyright Copyright 2020-2021 Tuya Inc. All Rights Reserved.
 * 
 */

#include "tkl_memory.h"
#include <os/mem.h>

#define CONFIG_HAVE_PSRAM 1
extern void *tkl_system_calloc(size_t nitems, size_t size);
extern void *tkl_system_realloc(void* ptr, size_t size);
extern void *tkl_system_psram_malloc(const SIZE_T size);
extern void tkl_system_psram_free(void* ptr);

extern void bk_printf(const char *fmt, ...);

static BOOL_T s_psram_malloc_force = FALSE;
void tkl_system_psram_malloc_force_set(BOOL_T enable)
{
    s_psram_malloc_force = enable;
}

/**
* @brief Alloc memory of system
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
void* tkl_system_malloc(const SIZE_T size)
{
    if (s_psram_malloc_force) {
        return tkl_system_psram_malloc(size);
    } else {
    void* ptr = os_malloc(size);
    if(NULL == ptr) {
        bk_printf("tkl_system_malloc failed, size(%d)!\r\n", size);
    }

    if (size > 4096) {
        bk_printf("tkl_system_malloc big memory, size(%d)!\r\n", size);
    }

        return ptr;
    }
}

/**
* @brief Free memory of system
*
* @param[in] ptr: memory point
*
* @note This API is used to free memory of system.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
void tkl_system_free(void* ptr)
{
    if (s_psram_malloc_force) {
        tkl_system_psram_free(ptr);
    } else {
        os_free(ptr);
    }
}

/**
* @brief set memory
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return the memory address malloced
*/
void *tkl_system_memset(void* src, int ch, const SIZE_T n)
{
    return os_memset(src, ch, n);
}

/**
* @brief Alloc memory of system
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return the memory address malloced
*/
void *tkl_system_memcpy(void* src, const void* dst, const SIZE_T n)
{
    return os_memcpy(src, dst, n);
}

/**
 * @brief Allocate and clear the memory
 * 
 * @param[in]       nitems      the numbers of memory block
 * @param[in]       size        the size of the memory block
 */
void *tkl_system_calloc(size_t nitems, size_t size)
{	
    if (s_psram_malloc_force) {
        if (size && nitems > (~(size_t) 0) / size)
            return NULL;

        void *ptr = psram_zalloc(nitems * size);
        if (ptr == NULL) {
            bk_printf("tkl_system_calloc failed, total_size(%d)! nitems = %d size = %d\r\n", nitems * size,nitems,size);
        }
        return ptr;
    } else {
        if (size && nitems > (~(size_t) 0) / size)
            return NULL;

        void *ptr =  os_zalloc(nitems * size);
        if (ptr == NULL) {
            bk_printf("tkl_system_calloc failed, total_size(%d)! nitems = %d size = %d\r\n", nitems * size,nitems,size);
        }
        return ptr;
    }
}

/**
 * @brief Re-allocate the memory
 *
 * @param[in]       nitems      source memory address
 * @param[in]       size        the size after re-allocate
 */
void *tkl_system_realloc(void* ptr, size_t size)
{
    if (s_psram_malloc_force) {
        return bk_psram_realloc(ptr, size);
    } else {
        return os_realloc(ptr, size);
    }
}

/**
* @brief Alloc memory of system
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
void* tkl_system_psram_malloc(const SIZE_T size)
{
#if CONFIG_HAVE_PSRAM
    void* ptr = psram_malloc(size);
    if(NULL == ptr) {
        bk_printf("tkl_psram_malloc failed, size(%d)!\r\n", size);
    }

    // if (size > 4096) {
    //     bk_printf("tkl_psram_malloc big memory, size(%d)!\r\n", size);
    // }

    return ptr;
#else
    return NULL;
#endif // CONFIG_HAVE_PSRAM
}

/**
* @brief Free memory of system
*
* @param[in] ptr: memory point
*
* @note This API is used to free memory of system.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
void tkl_system_psram_free(void* ptr)
{
#if CONFIG_HAVE_PSRAM
    psram_free(ptr);
#endif // CONFIG_HAVE_PSRAM
}

