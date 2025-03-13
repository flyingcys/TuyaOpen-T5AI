#include "bk_private/bk_init.h"
#include <components/system.h>
#include <components/ate.h>
#include <os/os.h>
#include <components/shell_task.h>
#include "tuya_cloud_types.h"
#include "FreeRTOS.h"

#include "media_service.h"

extern void tuya_app_main(void);
extern void user_reg_usbenum_cb(void);
extern void rtos_set_user_app_entry(beken_thread_function_t entry);
extern VOID_T tkl_system_sleep(const UINT_T num_ms);

#include "FreeRTOS.h"
#include "task.h"
#include "tkl_system.h"
#include "tkl_watchdog.h"
#include "driver/wdt.h"
TaskHandle_t __wdg_handle;
static void __feed_wdg(void *arg)
{
#define WDT_TIME    30000
    TUYA_WDOG_BASE_CFG_T cfg = {.interval_ms = WDT_TIME};
    tkl_watchdog_init(&cfg);

    while (1) {
        tkl_watchdog_refresh();
        tkl_system_sleep(WDT_TIME / 2);
    }
}

#if (CONFIG_SYS_CPU0)
static beken_semaphore_t g_wait_cpu0_media_init_done_sem;
void user_app_main(void)
{
    rtos_get_semaphore(&g_wait_cpu0_media_init_done_sem, BEKEN_NEVER_TIMEOUT);
    rtos_deinit_semaphore(&g_wait_cpu0_media_init_done_sem);

    // disable shell echo for gpio test
    shell_echo_set(0);

    extern void tuya_app_main(void);
    tuya_app_main();

#if (CONFIG_TUYA_TEST_CLI)
    extern int cli_tuya_test_init(void);
    cli_tuya_test_init();
#endif

}
#endif

int main(void)
{
#if (CONFIG_SYS_CPU0)
    bk_printf("-------- left heap: %d, reset reason: %x\r\n",
            xPortGetFreeHeapSize(), bk_misc_get_reset_reason() & 0xFF);
    extern int tuya_upgrade_main(void);
    extern TUYA_OTA_PATH_E tkl_ota_is_under_seg_upgrade(void);
    bk_err_t ret = BK_FAIL;
    ret = rtos_init_semaphore_ex(&g_wait_cpu0_media_init_done_sem, 1, 0);
    if (BK_OK != ret) {
        bk_printf("%s semaphore init failed\n", __func__);
        return ret;
    }

    if(TUYA_OTA_PATH_INVALID != tkl_ota_is_under_seg_upgrade()) {
        bk_printf("goto tuya_upgrade_main\r\n");
        rtos_set_user_app_entry((beken_thread_function_t)tuya_upgrade_main);
    } else {
        if (!ate_is_enabled()) {
            bk_printf("go to tuya\r\n");
            rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
        } else {
            // in ate mode, feed dog
            xTaskCreate(__feed_wdg, "feed_wdg", 1024, NULL, 6, (TaskHandle_t * const )&__wdg_handle);
        }
    }
    // bk_set_printf_sync(true);
    // shell_set_log_level(BK_LOG_WARN);
#endif // CONFIG_SYS_CPUx
	bk_init();
    media_service_init();

#if (CONFIG_SYS_CPU1 && CONFIG_TUYA_LOG_OPTIMIZATION)
    bk_set_printf_enable(1);
#endif

extern OPERATE_RET tuya_ipc_init(void);
    tuya_ipc_init();
#if (CONFIG_SYS_CPU1)
extern void tkl_fs_init(void);
    tkl_fs_init();
    // xTaskCreate(__fs_test, "fs_test", 4096, NULL, 6, (TaskHandle_t * const )&__fs_test_handle);
#endif


#if (CONFIG_SYS_CPU0)
    if(g_wait_cpu0_media_init_done_sem) {
        rtos_set_semaphore(&g_wait_cpu0_media_init_done_sem);
    }
#endif

    return 0;
}
