#include <common/bk_include.h>
#include "cli.h"
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <driver/int.h>
#include <common/bk_err.h>
#include <getopt.h>


#include <components/video_transfer.h>
#include <driver/dma.h>
#include <driver/audio_ring_buff.h>
#include "aud_intf.h"


#include <driver/dvp_camera_types.h>
#include <driver/lcd.h>

#include "doorbell_comm.h"
#include "doorbell_transmission.h"
#include "doorbell_cmd.h"
#include "doorbell_devices.h"

#include "wifi_transfer.h"
#include "media_app.h"
#include "lcd_act.h"

#include "driver/dvp_camera.h"

#include "doorbell_cs2_service.h"

#include "cli.h"

#define TAG "db-device"

#define LOGI(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define DB_SAMPLE_RARE_8K (8000)
#define DB_SAMPLE_RARE_16K (16000)

#define CAMERA_DEVICES_REPORT (BK_FALSE)//(BK_TRUE)


typedef enum
{
	LCD_STATUS_CLOSE,
	LCD_STATUS_OPEN,
	LCD_STATUS_UNKNOWN,
} lcd_status_t;

extern const dvp_sensor_config_t **get_sensor_config_devices_list(void);
extern int get_sensor_config_devices_num(void);

extern const doorbell_service_interface_t *doorbell_current_service;


#define DEVICE_RESPONSE_SIZE (DOORBELL_NETWORK_MAX_SIZE - sizeof(db_evt_head_t))

#define UVC_DEVICE_ID (0xFDF6)


typedef struct
{
	uint16_t camera_id;
	uint16_t lcd_id;
	uint16_t video_transfer;
	uint8_t pipeline_enable;
	uint8_t audio_enable;
	media_transfer_cb_t *camera_transfer_cb;
	const media_transfer_cb_t *audio_transfer_cb;
} db_device_info_t;


db_device_info_t *db_device_info = NULL;

static aud_intf_drv_setup_t aud_intf_drv_setup = DEFAULT_AUD_INTF_DRV_SETUP_CONFIG();
static aud_intf_work_mode_t aud_work_mode = AUD_INTF_WORK_MODE_NULL;
static aud_intf_voc_setup_t aud_voc_setup = DEFAULT_AUD_INTF_VOC_SETUP_CONFIG();

int doorbell_get_ppis(char *ppi, int capability, int size)
{
	int ret = 0;
	strcat(ppi, "[");

	if (capability & PPI_CAP_320X240)
	{
		strcat(ppi, " \"320X240\",");
	}

	if (capability & PPI_CAP_320X480)
	{
		strcat(ppi, " \"320X480\",");
	}

	if (capability & PPI_CAP_480X272)
	{
		strcat(ppi, " \"480X272\",");
	}

	if (capability & PPI_CAP_480X320)
	{
		strcat(ppi, " \"480X320\",");
	}

	if (capability & PPI_CAP_640X480)
	{
		strcat(ppi, " \"640X480\",");
	}

	if (capability & PPI_CAP_480X800)
	{
		strcat(ppi, " \"480X800\",");
	}

	if (capability & PPI_CAP_800X480)
	{
		strcat(ppi, " \"800X480\",");
	}

	if (capability & PPI_CAP_800X600)
	{
		strcat(ppi, " \"800X600\",");
	}

	if (capability & PPI_CAP_864X480)
	{
		strcat(ppi, " \"864X480\",");
	}

	if (capability & PPI_CAP_1024X600)
	{
		strcat(ppi, " \"1024X600\",");
	}

	if (capability & PPI_CAP_1280X720)
	{
		strcat(ppi, " \"1280X720\",");
	}

	if (capability & PPI_CAP_1600X1200)
	{
		strcat(ppi, " \"1600X1200\",");
	}

	if (capability & PPI_CAP_480X480)
	{
		strcat(ppi, " \"480X480\",");
	}

	if (capability & PPI_CAP_720X288)
	{
		strcat(ppi, " \"720X288\",");
	}

	if (capability & PPI_CAP_720X576)
	{
		strcat(ppi, " \"720X576\",");
	}

	if (capability & PPI_CAP_480X854)
	{
		strcat(ppi, " \"480X854\",");
	}

	ret = strlen(ppi);

	ppi[ret - 1] = ']';

	return ret;
}


int doorbell_get_supported_camera_devices(int opcode, db_channel_t *channel, doorbell_transmission_send_t cb)
{
	db_evt_head_t *evt = os_malloc(sizeof(db_evt_head_t) + DEVICE_RESPONSE_SIZE);
	char *p = (char *)(evt + 1);

	evt->opcode = opcode;
	evt->status = EVT_STATUS_OK;
	evt->flags = EVT_FLAGS_CONTINUE;

	LOGI("DBCMD_GET_CAMERA_SUPPORTED_DEVICES\n");

#if (CAMERA_DEVICES_REPORT == BK_TRUE)

	int ret = 0;
	const dvp_sensor_config_t **sensors = get_sensor_config_devices_list();
	uint32_t i, size = get_sensor_config_devices_num();


	for (i = 0; i < size; i++)
	{
		char ppi[500] = {0};

		ret = doorbell_get_ppis(ppi, sensors[i]->ppi_cap, sizeof(ppi));

		if (ret >= sizeof(ppi))
		{
			LOGE("doorbell_camera_get_ppis overflow\n");
		}

		os_memset(p, 0, DEVICE_RESPONSE_SIZE);

		LOGD("sensor: %s, ppi: %uX%u\n", sensors[i]->name,
		     ppi_to_pixel_x(sensors[i]->def_ppi),
		     ppi_to_pixel_y(sensors[i]->def_ppi));
		sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"DVP\", \"ppi\": %s}",
		        sensors[i]->name,
		        sensors[i]->id,
		        ppi);

		LOGI("dump: %s\n", p);

		evt->length = CHECK_ENDIAN_UINT16(strlen(p));
		doorbell_transmission_pack_send(channel, (uint8_t *)evt, sizeof(db_evt_head_t) + evt->length, cb);
	}

#else
	os_memset(p, 0, DEVICE_RESPONSE_SIZE);

	sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"DVP\", \"ppi\":[\"%uX%u\"]}",
	        "DVP",
	        1,
	        ppi_to_pixel_x(0),
	        ppi_to_pixel_y(0));
	evt->length = CHECK_ENDIAN_UINT16(strlen(p));
	doorbell_transmission_pack_send(channel, (uint8_t *)evt, sizeof(db_evt_head_t) + evt->length, cb);


#endif
	os_memset(p, 0, DEVICE_RESPONSE_SIZE);

	sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"UVC\", \"ppi\":[\"%uX%u\"]}",
	        "UVC",
	        UVC_DEVICE_ID,
	        ppi_to_pixel_x(0),
	        ppi_to_pixel_y(0));
	evt->length = CHECK_ENDIAN_UINT16(strlen(p));
	evt->flags = EVT_FLAGS_COMPLETE;
	doorbell_transmission_pack_send(channel, (uint8_t *)evt, sizeof(db_evt_head_t) + evt->length, cb);

	os_free(evt);

	return 0;
}

int doorbell_get_supported_lcd_devices(int opcode, db_channel_t *channel, doorbell_transmission_send_t cb)
{
	const lcd_device_t **device = NULL;
	uint32_t i, size;
	size = media_app_get_lcd_devices_num();
	uint32_t addr = media_app_get_lcd_devices_list();
	device = (const lcd_device_t**)addr;
	db_evt_head_t *evt = os_malloc(sizeof(db_evt_head_t) + DEVICE_RESPONSE_SIZE);
	char *p = (char *)(evt + 1);

	evt->opcode = opcode;
	evt->status = EVT_STATUS_OK;
	evt->flags = EVT_FLAGS_CONTINUE;

	LOGI("DBCMD_GET_LCD_SUPPORTED_DEVICES\n");

	if ((uint32_t)device != kGeneralErr && device != NULL)
	{
		for (i = 0; i < size; i++)
		{
			os_memset(p, 0, DEVICE_RESPONSE_SIZE);

			LOGD("lcd: %s, ppi: %uX%u\n", device[i]->name,
			     ppi_to_pixel_x(device[i]->ppi),
			     ppi_to_pixel_y(device[i]->ppi));
			sprintf(p, "{\"name\": \"%s\", \"id\": \"%d\", \"type\": \"%s\", \"ppi\":\"%uX%u\"}",
			        device[i]->name,
			        device[i]->id,
			        device[i]->type == LCD_TYPE_RGB565 ? "rgb" : "mcu",
			        ppi_to_pixel_x(device[i]->ppi),
			        ppi_to_pixel_y(device[i]->ppi));

			LOGI("dump: %s\n", p);

			evt->length = CHECK_ENDIAN_UINT16(strlen(p));

			if (i == size - 1)
			{
				evt->flags = EVT_FLAGS_COMPLETE;
			}

			doorbell_transmission_pack_send(channel, (uint8_t *)evt, sizeof(db_evt_head_t) + evt->length, cb);
		}
	}

	os_free(evt);

	return 0;
}

int doorbell_get_lcd_status(int opcode, db_channel_t *channel, doorbell_transmission_send_t cb)
{
	uint32_t lcd_status = media_app_get_lcd_status();
	db_evt_head_t *evt = os_malloc(sizeof(db_evt_head_t) + DEVICE_RESPONSE_SIZE);
	char *p = (char *)(evt + 1);

	evt->opcode = opcode;
	evt->status = EVT_STATUS_OK;
	evt->flags = EVT_FLAGS_CONTINUE;

	LOGI("DBCMD_GET_LCD_STATUS\n");
	os_memset(p, 0, DEVICE_RESPONSE_SIZE);

	if (lcd_status != LCD_STATUS_CLOSE && lcd_status != LCD_STATUS_OPEN)
	{
		lcd_status = LCD_STATUS_UNKNOWN;
	}
	sprintf(p, "{\"status\": \"%d\"}", lcd_status);
	LOGI("dump: %s\n", p);
	evt->length = CHECK_ENDIAN_UINT16(strlen(p));

	evt->flags = EVT_FLAGS_COMPLETE;

	doorbell_transmission_pack_send(channel, (uint8_t *)evt, sizeof(db_evt_head_t) + evt->length, cb);

	os_free(evt);

	return 0;
}

int doorbell_devices_set_camera_transfer_callback(void *cb)
{
	if (db_device_info == NULL)
	{
		LOGE("db_device_info null");
		return  BK_FAIL;
	}

	db_device_info->camera_transfer_cb = (media_transfer_cb_t *)cb;

	return BK_OK;
}

int doorbell_devices_set_audio_transfer_callback(const void *cb)
{
	if (db_device_info == NULL)
	{
		LOGE("db_device_info null");
		return  BK_FAIL;
	}

	db_device_info->audio_transfer_cb = (const media_transfer_cb_t *)cb;

	return BK_OK;
}

int doorbell_camera_turn_on(camera_parameters_t *parameters)
{
	bk_err_t ret = BK_FAIL;
	media_camera_device_t device = {0};

	LOGI("%s, id: %d, %d X %d, format: %d, Protocol: %d\n", __func__, 
		parameters->id, parameters->width, parameters->height,
		parameters->format, parameters->protocol);

	if (db_device_info->camera_id != 0)
	{
		LOGI("%s, id: %d already open\n", __func__, parameters->id);
		return EVT_STATUS_ALREADY;
	}

	if (parameters->id == UVC_DEVICE_ID)
	{
		device.type = UVC_CAMERA;
	}
	else
	{
		device.type = DVP_CAMERA;
	}

	if (parameters->format == 0)
	{
		device.fmt = PIXEL_FMT_JPEG;
		if (device.type == DVP_CAMERA)
			device.mode = JPEG_YUV_MODE;
		else
			device.mode = JPEG_MODE;
	}
	else
	{
		device.fmt = PIXEL_FMT_H264;
		if (device.type == DVP_CAMERA)
			device.mode = H264_YUV_MODE;
		else
			device.mode = H264_MODE;
	}

	if (device.type == UVC_CAMERA && device.mode == H264_MODE)
	{
#if !CONFIG_SOC_BK7256XX
		device.mode = JPEG_MODE;
		device.fmt = PIXEL_FMT_JPEG;
		db_device_info->pipeline_enable = true;
#endif
	}

	db_device_info->camera_transfer_cb->fmt = device.fmt;
	if (db_device_info->pipeline_enable)
		db_device_info->camera_transfer_cb->fmt = PIXEL_FMT_H264;

	device.info.resolution.width = parameters->width;
	device.info.resolution.height = parameters->height;
	device.info.fps = FPS30;

	ret = media_app_camera_open(&device);

	if (ret != BK_OK)
	{
		LOGE("%s failed\n", __func__);
		return ret;
	}

#if (!CONFIG_SOC_BK7256XX)
	uint8_t rot_angle = 0;
	if (db_device_info->pipeline_enable)
	{
		switch (parameters->rotate)
		{
			case 90:
				rot_angle = ROTATE_90;
				break;
			case 180:
				rot_angle = ROTATE_180;
				break;
			case 270:
				rot_angle = ROTATE_270;
				break;
			case 0:
				rot_angle = ROTATE_NONE;
				break;
			default:
				rot_angle = ROTATE_90;
				break;
		}
		media_app_pipline_set_rotate(rot_angle);
	}
#endif

#if !CONFIG_SOC_BK7256XX
	if (db_device_info->pipeline_enable)
	{
		ret = media_app_h264_pipeline_open();
		if (ret != BK_OK)
		{
			LOGE("%s h264_pipeline_open failed\n", __func__);
			return ret;
		}
	}
#endif

	db_device_info->camera_id = parameters->id & 0xFFFF;

	return ret;
}

int doorbell_camera_turn_off(void)
{
	LOGI("%s, id: %d\r\n", __func__, db_device_info->camera_id);

	if (db_device_info->camera_id == 0)
	{
		LOGI("%s, %d already close\n", __func__);
		return EVT_STATUS_ALREADY;
	}

#if (!CONFIG_SOC_BK7256XX)
	if (db_device_info->pipeline_enable)
	{
		media_app_h264_pipeline_close();
		LOGI("%s h264_pipeline close\n", __func__);
	}
#endif

	if (db_device_info->camera_id == UVC_DEVICE_ID)
	{
		media_app_camera_close(UVC_CAMERA);
	}
	else
	{
		media_app_camera_close(DVP_CAMERA);
	}

	db_device_info->pipeline_enable = false;
	db_device_info->camera_id = 0;

	return 0;
}

int doorbell_video_transfer_turn_on(void)
{
	int ret = -1;

	if (db_device_info->video_transfer != 0)
	{
		LOGI("%s, id: %d already open\n", __func__, db_device_info->video_transfer);
		return EVT_STATUS_ALREADY;
	}

	if (db_device_info->camera_transfer_cb)
	{
		ret = bk_wifi_transfer_frame_open(db_device_info->camera_transfer_cb);
	}
	else
	{
		LOGE("media_transfer_cb: NULL\n");
	}

	db_device_info->video_transfer = 1;

	return ret;
}

int doorbell_video_transfer_turn_off(void)
{
	int ret = -1;

	if (db_device_info->video_transfer == 0)
	{
		LOGI("%s, id: %d already close\n", __func__, db_device_info->video_transfer);
		return EVT_STATUS_ALREADY;
	}

	ret = bk_wifi_transfer_frame_close();

	db_device_info->video_transfer = 0;

	doorbell_cs2_img_timer_deinit();

	return ret;
}

int doorbell_display_turn_on(uint16_t id, uint16_t rotate, uint16_t fmt)
{
	LOGI("%s, id: %d, rotate: %d fmt: %d\n", __func__, id, rotate, fmt);
	if (db_device_info->lcd_id != 0)
	{
		LOGI("%s, id: %d already open\n", __func__, id);
		return EVT_STATUS_ALREADY;
	}
	const lcd_device_t *device = (const lcd_device_t *)media_app_get_lcd_device_by_id(id);
	if ((uint32_t)device == BK_FAIL || device == NULL)
	{
		LOGI("%s, could not find device id: %d\n", __func__, id);
		return EVT_STATUS_ERROR;
	}

	lcd_open_t lcd_open = {0};
	lcd_open.device_ppi = device->ppi;
	lcd_open.device_name = device->name;

#if (!CONFIG_SOC_BK7256XX)
    uint8_t rot_angle = 0;
	db_device_info->pipeline_enable = true;
	if (db_device_info->pipeline_enable)
	{
		if (fmt == 0)
		{
			media_app_lcd_fmt(PIXEL_FMT_RGB565_LE);
		}
		else if (fmt == 1)
		{
			media_app_lcd_fmt(PIXEL_FMT_RGB888);
		}
        switch (rotate)
        {
            case 90:
                rot_angle = ROTATE_90;
                break;
            case 180:
                rot_angle = ROTATE_180;
                break;
            case 270:
                rot_angle = ROTATE_270;
                break;
            case 0:
            default:
                rot_angle = ROTATE_NONE;
                break;
        }
        media_app_pipline_set_rotate(rot_angle);
		media_app_lcd_pipeline_open(&lcd_open);
	}
	else
#endif
	{
		if (rotate == 90)
			media_app_lcd_rotate(ROTATE_90);

		media_app_lcd_open(&lcd_open);
	}

	db_device_info->lcd_id = id;
	return 0;
}

int doorbell_display_turn_off(void)
{
	LOGI("%s, id: %d", __func__, db_device_info->lcd_id);

	if (db_device_info->lcd_id == 0)
	{
		LOGI("%s, %d already close\n", __func__);
		return EVT_STATUS_ALREADY;
	}

#if (!CONFIG_SOC_BK7256XX)
	db_device_info->pipeline_enable = true;
	if (db_device_info->pipeline_enable)
	{
		media_app_lcd_pipeline_close();
	}
	else
#endif
	{
		media_app_lcd_close();
	}

	db_device_info->lcd_id = 0;
	return 0;
}

int doorbell_udp_voice_send_callback(unsigned char *data, unsigned int len)
{
	uint16_t retry_cnt = 0;
	if (db_device_info == NULL)
	{
		LOGE("%s, db_device_info NULL\n", __func__);
		return BK_FAIL;
	}

	if (db_device_info->audio_transfer_cb == NULL)
	{
		LOGE("%s, audio_transfer_cb NULL\n", __func__);
		return BK_FAIL;
	}

	if (len > db_device_info->audio_transfer_cb->get_tx_size())
	{
		LOGE("%s, buffer over flow %d %d\n", __func__, len, db_device_info->audio_transfer_cb->get_tx_size());
		return BK_FAIL;
	}

	uint8_t *buffer = db_device_info->audio_transfer_cb->get_tx_buf();

	if (db_device_info->audio_transfer_cb->prepare)
	{
		db_device_info->audio_transfer_cb->prepare(data, len);
	}

	return db_device_info->audio_transfer_cb->send(buffer, len, &retry_cnt);
}

static void doorbell_audio_connect_state_cb_handle(uint8_t state)
{
	os_printf("[--%s--] state: %d \n", __func__, state);
}


int doorbell_audio_turn_on(audio_parameters_t *parameters)
{
	int ret;

	if (db_device_info->audio_enable == BK_TRUE)
	{
		LOGI("%s already turn on\n", __func__);

		return EVT_STATUS_ALREADY;
	}

	LOGI("%s, AEC: %d, UAC: %d, sample rate: %d, %d, fmt: %d, %d\n", __func__,
		parameters->aec, parameters->uac, parameters->rmt_recorder_sample_rate,
		parameters->rmt_player_sample_rate, parameters->rmt_recoder_fmt, parameters->rmt_player_fmt);

	if (parameters->aec == 1)
	{
		aud_voc_setup.aec_enable = true;
	}
	else
	{
		aud_voc_setup.aec_enable = false;
	}

	//aud_voc_setup.data_type = AUD_INTF_VOC_DATA_TYPE_G711A;
	//aud_voc_setup.data_type = AUD_INTF_VOC_DATA_TYPE_PCM;
	aud_voc_setup.spk_mode = AUD_DAC_WORK_MODE_SIGNAL_END;
	//aud_voc_setup.mic_en = AUD_INTF_VOC_MIC_OPEN;
	//aud_voc_setup.spk_en = AUD_INTF_VOC_SPK_OPEN;

	if (parameters->uac == 1)
	{
		aud_voc_setup.mic_type = AUD_INTF_MIC_TYPE_UAC;
		aud_voc_setup.spk_type = AUD_INTF_SPK_TYPE_UAC;
		//aud_voc_setup.samp_rate = AUD_INTF_VOC_SAMP_RATE_16K;
	}
	else
	{
		aud_voc_setup.mic_type = AUD_INTF_MIC_TYPE_BOARD;
		aud_voc_setup.spk_type = AUD_INTF_SPK_TYPE_BOARD;
	}

	if (aud_voc_setup.mic_type == AUD_INTF_MIC_TYPE_BOARD && aud_voc_setup.spk_type == AUD_INTF_SPK_TYPE_BOARD) {
			aud_voc_setup.data_type = parameters->rmt_recoder_fmt - 1;
	}

	switch (parameters->rmt_recorder_sample_rate)
	{
		case DB_SAMPLE_RARE_8K:
			aud_voc_setup.samp_rate = 8000;
			break;

		case DB_SAMPLE_RARE_16K:
			aud_voc_setup.samp_rate = 16000;
			break;

		default:
			aud_voc_setup.samp_rate = 8000;
			break;
	}

	aud_intf_drv_setup.aud_intf_tx_mic_data = doorbell_udp_voice_send_callback;
	ret = bk_aud_intf_drv_init(&aud_intf_drv_setup);
	if (ret != BK_ERR_AUD_INTF_OK)
	{
		LOGE("bk_aud_intf_drv_init fail, ret:%d\n", ret);
		goto error;
	}

	aud_work_mode = AUD_INTF_WORK_MODE_VOICE;
	ret = bk_aud_intf_set_mode(aud_work_mode);
	if (ret != BK_ERR_AUD_INTF_OK)
	{
		LOGE("bk_aud_intf_set_mode fail, ret:%d\n", ret);
		goto error;
	}

	/* uac recover connection */
	if (aud_voc_setup.mic_type == AUD_INTF_MIC_TYPE_UAC)
	{
		ret = bk_aud_intf_register_uac_connect_state_cb(doorbell_audio_connect_state_cb_handle);
		if (ret != BK_ERR_AUD_INTF_OK)
		{
			LOGE("bk_aud_intf_register_uac_connect_state_cb fail, ret:%d\n", ret);
			goto error;
		}

		ret = bk_aud_intf_uac_auto_connect_ctrl(true);
		if (ret != BK_ERR_AUD_INTF_OK)
		{
			LOGE("aud_tras_uac_auto_connect_ctrl fail, ret:%d\n", ret);
			goto error;
		}
	}

	ret = bk_aud_intf_voc_init(aud_voc_setup);
	if (ret != BK_ERR_AUD_INTF_OK)
	{
		LOGE("bk_aud_intf_voc_init fail, ret:%d\n", ret);
		goto error;
	}

	ret = bk_aud_intf_voc_start();
	if (ret != BK_ERR_AUD_INTF_OK)
	{
		LOGE("bk_aud_intf_voc_start fail, ret:%d\n", ret);
		goto error;
	}

	db_device_info->audio_enable = BK_TRUE;


	if (doorbell_current_service
	    && doorbell_current_service->audio_state_changed)
	{
		doorbell_current_service->audio_state_changed(DB_TURN_ON);
	}

	return EVT_STATUS_OK;
error:
	bk_aud_intf_set_mode(AUD_INTF_WORK_MODE_NULL);
	bk_aud_intf_drv_deinit();

	return EVT_STATUS_ERROR;
}

int doorbell_audio_turn_off(void)
{
	if (db_device_info->audio_enable == BK_FALSE)
	{
		LOGI("%s already turn off\n", __func__);

		return EVT_STATUS_ALREADY;
	}

	LOGI("%s entry\n", __func__);

	db_device_info->audio_enable = BK_FALSE;

	if (doorbell_current_service
	    && doorbell_current_service->audio_state_changed)
	{
		doorbell_current_service->audio_state_changed(DB_TURN_OFF);
	}

	bk_aud_intf_voc_stop();
	bk_aud_intf_voc_deinit();
	aud_work_mode = AUD_INTF_WORK_MODE_NULL;
	bk_aud_intf_set_mode(aud_work_mode);
	bk_aud_intf_drv_deinit();

	LOGI("%s out\n", __func__);

	return EVT_STATUS_OK;
}

int doorbell_audio_acoustics(uint32_t index, uint32_t param)
{
	LOGI("%s, %u, %u\n", __func__, index, param);
	bk_err_t ret = BK_FAIL;

	switch (index)
	{
		case AA_ECHO_DEPTH:
			ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_EC_DEPTH, param);
			break;
		case AA_MAX_AMPLITUDE:
			ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_TXRX_THR, param);
			break;
		case AA_MIN_AMPLITUDE:
			ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_TXRX_FLR, param);
			break;
		case AA_NOISE_LEVEL:
			ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_NS_LEVEL, param);
			break;
		case AA_NOISE_PARAM:
			ret = bk_aud_intf_set_aec_para(AUD_INTF_VOC_AEC_NS_PARA, param);
			break;
	}

	return ret;
}

void doorbell_audio_data_callback(uint8_t *data, uint32_t length)
{
	bk_err_t ret = BK_OK;

	ret = bk_aud_intf_write_spk_data(data, length);

	if (ret != BK_OK)
	{
		//LOGE("write speaker data fail\n", length);
	}
}

int doorbell_devices_init(void)
{
	if (db_device_info == NULL)
	{
		db_device_info = os_malloc(sizeof(db_device_info_t));
	}

	if (db_device_info == NULL)
	{
		LOGE("malloc db_device_info failed");
		return  BK_FAIL;
	}

	os_memset(db_device_info, 0, sizeof(db_device_info_t));

	return BK_OK;
}

void doorbell_devices_deinit(void)
{
	if (db_device_info)
	{
		os_free(db_device_info);
		db_device_info = NULL;
	}
}
