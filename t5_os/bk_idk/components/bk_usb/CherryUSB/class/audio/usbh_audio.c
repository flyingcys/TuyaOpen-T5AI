/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbh_core.h"
#include "usbh_audio.h"

#define DEV_FORMAT "/dev/audio%d"

/* general descriptor field offsets */
#define DESC_bLength            0 /** Length offset */
#define DESC_bDescriptorType    1 /** Descriptor type offset */
#define DESC_bDescriptorSubType 2 /** Descriptor subtype offset */

/* interface descriptor field offsets */
#define INTF_DESC_bInterfaceNumber  2 /** Interface number offset */
#define INTF_DESC_bAlternateSetting 3 /** Alternate setting offset */
#define INTF_DESC_bInterfaceClass   5 /** Interface class offset */
#define INTF_DESC_bInterfaceSubClass 6 /** Interface subclass offset */


#ifndef CONFIG_USBHOST_MAX_AUDIO_CLASS
#define CONFIG_USBHOST_MAX_AUDIO_CLASS 4
#endif

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t g_audio_buf[8];

static struct usbh_audio g_audio_class[CONFIG_USBHOST_MAX_AUDIO_CLASS];
static uint32_t g_devinuse = 0;

static struct usbh_audio *usbh_audio_class_alloc(void)
{
    int devno;

    for (devno = 0; devno < CONFIG_USBHOST_MAX_AUDIO_CLASS; devno++) {
        if ((g_devinuse & (1 << devno)) == 0) {
            g_devinuse |= (1 << devno);
            memset(&g_audio_class[devno], 0, sizeof(struct usbh_audio));
            g_audio_class[devno].minor = devno;
            return &g_audio_class[devno];
        }
    }
    return NULL;
}

static void usbh_audio_class_free(struct usbh_audio *audio_class)
{
    int devno = audio_class->minor;

    if (devno >= 0 && devno < 32) {
        g_devinuse &= ~(1 << devno);
    }
    memset(audio_class, 0, sizeof(struct usbh_audio));
}

int usbh_audio_open(struct usbh_audio *audio_class, const char *name, uint32_t samp_freq)
{
    struct usb_setup_packet *setup = &audio_class->hport->setup;
    struct usb_endpoint_descriptor *ep_desc;
    uint8_t mult;
    uint16_t mps;
    int ret;
    uint8_t intf = 0xff;
    uint8_t altsetting = 1;
    uint8_t set_samp_flag = 0;

    //if (audio_class->is_opened) {
    //    return -EMFILE;
    //}

    for (uint8_t i = 0; i < audio_class->module_num; i++) {
        if (strcmp(name, audio_class->module[i].name) == 0) {
            for (uint8_t j = 0; j < audio_class->num_of_intf_altsettings; j++) {
                for (uint8_t k = 0; k < audio_class->module[i].altsetting[0].sampfreq_num; k++) {
                    if (audio_class->module[i].altsetting[0].sampfreq[k] == samp_freq) {
                        intf = audio_class->module[i].data_intf;
                        set_samp_flag = audio_class->module[i].altsetting[0].set_attributes_flag;
                        altsetting = j + 1;
                        goto freq_found;
                    }
                }
            }
        }
    }

    return -ENODEV;

freq_found:

    ep_desc = &audio_class->hport->config.intf[intf].altsetting[altsetting].ep[0].ep_desc;

    if(set_samp_flag & AUDIO_EP_CONTROL_SAMPLING_FEQ) {

        setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_ENDPOINT;
        setup->bRequest = AUDIO_REQUEST_SET_CUR;
        setup->wValue = (AUDIO_EP_CONTROL_SAMPLING_FEQ << 8) | 0x00;
        setup->wIndex = ep_desc->bEndpointAddress;
        setup->wLength = 3;
    
        memcpy(g_audio_buf, &samp_freq, 3);
        ret = usbh_control_transfer(audio_class->hport->ep0, setup, g_audio_buf);
        if (ret < 0) {
            return ret;
        }
    }

    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_STANDARD | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = USB_REQUEST_SET_INTERFACE;
    setup->wValue = altsetting;
    setup->wIndex = intf;
    setup->wLength = 0;

    ret = usbh_control_transfer(audio_class->hport->ep0, setup, NULL);
    if (ret < 0) {
        return ret;
    }

    if(set_samp_flag & AUDIO_EP_CONTROL_SAMPLING_FEQ) {

        setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_ENDPOINT;
        setup->bRequest = AUDIO_REQUEST_SET_CUR;
        setup->wValue = (AUDIO_EP_CONTROL_SAMPLING_FEQ << 8) | 0x00;
        setup->wIndex = ep_desc->bEndpointAddress;
        setup->wLength = 3;
    
        memcpy(g_audio_buf, &samp_freq, 3);
        ret = usbh_control_transfer(audio_class->hport->ep0, setup, g_audio_buf);
        if (ret < 0) {
            return ret;
        }
    }

    //8k set maxpacketsize 16bytes/ms
    if(samp_freq == 8000){
        if(ep_desc->wMaxPacketSize != 0x10) {
            ep_desc->wMaxPacketSize = 0x10;
        }
    } else if(samp_freq == 16000) {
        if(ep_desc->wMaxPacketSize != 0x20) {
            ep_desc->wMaxPacketSize = 0x20;
        }
    } else if(samp_freq == 44100) {
        if(ep_desc->wMaxPacketSize != 0x5A) {
            ep_desc->wMaxPacketSize = 0x5A;
        }
    } else if(samp_freq == 48000) {
        if(ep_desc->wMaxPacketSize != 0x60) {
            ep_desc->wMaxPacketSize = 0x60;
        }
    } else {
        USB_LOG_INFO("please check samp_freq:%d ?\r\n", samp_freq);
    }

    mult = (ep_desc->wMaxPacketSize & USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_MASK) >> USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_SHIFT;
    mps = ep_desc->wMaxPacketSize & USB_MAXPACKETSIZE_MASK;
    if (ep_desc->bEndpointAddress & 0x80) {
        audio_class->isoin_mps = mps * (mult + 1);
        usbh_hport_activate_epx(&audio_class->isoin, audio_class->hport, ep_desc);
    } else {
        audio_class->isoout_mps = mps * (mult + 1);
        usbh_hport_activate_epx(&audio_class->isoout, audio_class->hport, ep_desc);
    }

    USB_LOG_INFO("Open audio module :%s, altsetting: %u\r\n", name, altsetting);
    //audio_class->is_opened = true;
    return ret;
}

int usbh_audio_close(struct usbh_audio *audio_class, const char *name)
{
    struct usb_setup_packet *setup = &audio_class->hport->setup;
    struct usb_endpoint_descriptor *ep_desc;
    int ret;
    uint8_t intf = 0xff;
    uint8_t altsetting = 1;

    for (size_t i = 0; i < audio_class->module_num; i++) {
        if (strcmp(name, audio_class->module[i].name) == 0) {
            intf = audio_class->module[i].data_intf;
        }
    }

    if (intf == 0xff) {
        return -ENODEV;
    }

    USB_LOG_DBG("Close audio module :%s\r\n", name);
    audio_class->is_opened = false;

    ep_desc = &audio_class->hport->config.intf[intf].altsetting[altsetting].ep[0].ep_desc;
    if (ep_desc->bEndpointAddress & 0x80) {
        if (audio_class->isoin) {
            usbh_pipe_free(audio_class->isoin);
            //audio_class->isoin = NULL;
        }
    } else {
        if (audio_class->isoout) {
            usbh_pipe_free(audio_class->isoout);
            //audio_class->isoout = NULL;
        }
    }

    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_STANDARD | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = USB_REQUEST_SET_INTERFACE;
    setup->wValue = 0;
    setup->wIndex = intf;
    setup->wLength = 0;

    ret = usbh_control_transfer(audio_class->hport->ep0, setup, NULL);
	USB_LOG_INFO("Close audio module :%s\r\n", name);

    return ret;
}

int usbh_audio_set_volume(struct usbh_audio *audio_class, const char *name, uint8_t ch, uint8_t volume)
{
    struct usb_setup_packet *setup = &audio_class->hport->setup;
    int ret;
    uint8_t intf = 0xff;
    uint8_t feature_id = 0xff;
    uint16_t volume_hex;

    for (size_t i = 0; i < audio_class->module_num; i++) {
        if (strcmp(name, audio_class->module[i].name) == 0) {
            intf = audio_class->ctrl_intf;
            feature_id = audio_class->module[i].feature_unit_id;
        }
    }

    if (intf == 0xff) {
        return -ENODEV;
    }

    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = AUDIO_REQUEST_SET_CUR;
    setup->wValue = (AUDIO_FU_CONTROL_VOLUME << 8) | ch;
    setup->wIndex = (feature_id << 8) | intf;
    setup->wLength = 2;

    volume_hex = -0xDB00 / 100 * volume + 0xdb00;

    memcpy(g_audio_buf, &volume_hex, 2);
    ret = usbh_control_transfer(audio_class->hport->ep0, setup, NULL);

    return ret;
}

int usbh_audio_set_mute(struct usbh_audio *audio_class, const char *name, uint8_t ch, bool mute)
{
    struct usb_setup_packet *setup = &audio_class->hport->setup;
    int ret;
    uint8_t intf = 0xff;
    uint8_t feature_id = 0xff;

    for (size_t i = 0; i < audio_class->module_num; i++) {
        if (strcmp(name, audio_class->module[i].name) == 0) {
            intf = audio_class->ctrl_intf;
            feature_id = audio_class->module[i].feature_unit_id;
        }
    }

    if (intf == 0xff) {
        return -ENODEV;
    }

    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = AUDIO_REQUEST_SET_CUR;
    setup->wValue = (AUDIO_FU_CONTROL_MUTE << 8) | ch;
    setup->wIndex = (feature_id << 8) | intf;
    setup->wLength = 1;

    memcpy(g_audio_buf, &mute, 1);
    ret = usbh_control_transfer(audio_class->hport->ep0, setup, g_audio_buf);

    return ret;
}

void usbh_audio_list_module(struct usbh_audio *audio_class)
{
    USB_LOG_DBG("============= Audio module information ===================\r\n");
    USB_LOG_DBG("bcdADC :%04x\r\n", audio_class->bcdADC);
    USB_LOG_DBG("Num of modules :%u\r\n", audio_class->module_num);
    USB_LOG_DBG("Num of altsettings:%u\r\n", audio_class->num_of_intf_altsettings);

    for (uint8_t i = 0; i < audio_class->module_num; i++) {
        USB_LOG_DBG("  module name :%s\r\n", audio_class->module[i].name);
        USB_LOG_DBG("  module feature unit id :%d\r\n", audio_class->module[i].feature_unit_id);

        for (uint8_t j = 0; j < audio_class->num_of_intf_altsettings; j++) {
            if (j == 0) {
                USB_LOG_DBG("      Ingore altsetting 0\r\n");
                continue;
            }
            USB_LOG_DBG("      Altsetting %u\r\n", j);
            USB_LOG_DBG("          module channels :%u\r\n", audio_class->module[i].altsetting[0].channels);
            USB_LOG_DBG("          module format_type :%u\r\n",audio_class->module[i].altsetting[0].format_type);
            USB_LOG_DBG("          module bitresolution :%u\r\n", audio_class->module[i].altsetting[0].bitresolution);
            USB_LOG_INFO("          module sampfreq num :%u\r\n", audio_class->module[i].altsetting[0].sampfreq_num);

            for (uint8_t k = 0; k < audio_class->module[i].altsetting[0].sampfreq_num; k++) {
                USB_LOG_INFO("              module sampfreq :%d hz\r\n", audio_class->module[i].altsetting[0].sampfreq[k]);
            }
        }
    }

    USB_LOG_DBG("============= Audio module information ===================\r\n");
}

static int usbh_audio_ctrl_connect(struct usbh_hubport *hport, uint8_t intf)
{
    int ret;
    uint8_t cur_iface = 0xff;
    uint8_t cur_iface_count = 0xff;
    uint8_t cur_alt_setting = 0xff;
    uint8_t input_offset = 0;
    uint8_t output_offset = 0;
    uint8_t feature_unit_offset = 0;
    uint8_t format_offset = 0;
    uint8_t *p;

    struct usbh_audio *audio_class = usbh_audio_class_alloc();
    if (audio_class == NULL) {
        USB_LOG_ERR("Fail to alloc audio_class\r\n");
        return -ENOMEM;
    }

    audio_class->hport = hport;
    audio_class->ctrl_intf = intf;
    audio_class->num_of_intf_altsettings = hport->config.intf[intf + 1].altsetting_num;

    hport->config.intf[intf].priv = audio_class;

    p = hport->raw_config_desc;
    while (p[DESC_bLength]) {
        switch (p[DESC_bDescriptorType]) {
            case USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION:
                cur_iface_count = p[3];
                break;
            case USB_DESCRIPTOR_TYPE_INTERFACE:
                if(USB_DEVICE_CLASS_AUDIO == p[INTF_DESC_bInterfaceClass]) {
                    cur_iface = p[INTF_DESC_bInterfaceNumber];
                    cur_alt_setting = p[INTF_DESC_bAlternateSetting];
                }
                break;
            case USB_DESCRIPTOR_TYPE_ENDPOINT:
                break;
            case AUDIO_INTERFACE_DESCRIPTOR_TYPE:
                if (cur_iface == audio_class->ctrl_intf) {
                    switch (p[DESC_bDescriptorSubType]) {
                        case AUDIO_CONTROL_HEADER: {
                            struct audio_cs_if_ac_header_descriptor *desc = (struct audio_cs_if_ac_header_descriptor *)p;
                            audio_class->bcdADC = desc->bcdADC;
                            audio_class->bInCollection = desc->bInCollection;
                        } break;
                        case AUDIO_CONTROL_INPUT_TERMINAL: {
                            struct audio_cs_if_ac_input_terminal_descriptor *desc = (struct audio_cs_if_ac_input_terminal_descriptor *)p;

                            audio_class->module[input_offset].input_terminal_id = desc->bTerminalID;
                            audio_class->module[input_offset].input_terminal_type = desc->wTerminalType;
                            audio_class->module[input_offset].input_channel_config = desc->wChannelConfig;

                            if (desc->wTerminalType == AUDIO_TERMINAL_STREAMING) {
                                audio_class->module[input_offset].terminal_link_id = desc->bTerminalID;
                            }
                            if (desc->wTerminalType == AUDIO_INTERM_MIC) {
                                audio_class->module[input_offset].name = "mic";
                            }
                            input_offset++;
                        } break;
                            break;
                        case AUDIO_CONTROL_OUTPUT_TERMINAL: {
                            struct audio_cs_if_ac_output_terminal_descriptor *desc = (struct audio_cs_if_ac_output_terminal_descriptor *)p;
                            audio_class->module[output_offset].output_terminal_id = desc->bTerminalID;
                            audio_class->module[output_offset].output_terminal_type = desc->wTerminalType;
                            if (desc->wTerminalType == AUDIO_TERMINAL_STREAMING) {
                                audio_class->module[output_offset].terminal_link_id = desc->bTerminalID;
                            }
                            if (desc->wTerminalType == AUDIO_OUTTERM_SPEAKER) {
                                audio_class->module[output_offset].name = "speaker";
                            }
                            output_offset++;
                        } break;
                        case AUDIO_CONTROL_FEATURE_UNIT: {
                            struct audio_cs_if_ac_feature_unit_descriptor *desc = (struct audio_cs_if_ac_feature_unit_descriptor *)p;
                            audio_class->module[feature_unit_offset].feature_unit_id = desc->bUnitID;
                            audio_class->module[feature_unit_offset].feature_unit_controlsize = desc->bControlSize;

                            for (uint8_t j = 0; j < desc->bControlSize; j++) {
                                audio_class->module[feature_unit_offset].feature_unit_controls[j] = p[6 + j];
                            }
                            feature_unit_offset++;
                        } break;
                        case AUDIO_CONTROL_PROCESSING_UNIT:

                            break;
                        default:
                            break;
                    }
                } else if ((cur_iface < (audio_class->ctrl_intf + cur_iface_count)) && (cur_iface > audio_class->ctrl_intf)) {
                    switch (p[DESC_bDescriptorSubType]) {
                        case AUDIO_STREAMING_GENERAL:

                            break;
                        case AUDIO_STREAMING_FORMAT_TYPE: {
                            struct audio_cs_if_as_format_type_descriptor *desc = (struct audio_cs_if_as_format_type_descriptor *)p;

                            audio_class->module[format_offset].data_intf = cur_iface;
                            audio_class->module[format_offset].altsetting[0].channels = desc->bNrChannels;
                            audio_class->module[format_offset].altsetting[0].format_type = desc->bFormatType;
                            audio_class->module[format_offset].altsetting[0].bitresolution = desc->bBitResolution;
                            audio_class->module[format_offset].altsetting[0].sampfreq_num = desc->bSamFreqType;

                            for (uint8_t j = 0; j < desc->bSamFreqType; j++) {
                                audio_class->module[format_offset].altsetting[0].sampfreq[j] = (uint32_t)(p[10 + j*3] << 16) |
                                                                                                             (uint32_t)(p[9 + j*3] << 8) |
                                                                                                             (uint32_t)(p[8 + j*3] << 0);
                            }
                            if (cur_alt_setting == (hport->config.intf[intf + 1].altsetting_num - 1)) {
                                format_offset++;
                            }
                        } break;
                        default:
                            break;
                    }
                }
                break;
            case AUDIO_ENDPOINT_DESCRIPTOR_TYPE:
                audio_class->module[format_offset - 1].altsetting[0].set_attributes_flag = ((struct audio_cs_ep_ep_general_descriptor *)p)->bmAttributes;
                break;
            default:
                break;
        }
        /* skip to next descriptor */
        p += p[DESC_bLength];
    }

    if ((input_offset != output_offset) && (input_offset != feature_unit_offset) && (input_offset != format_offset)) {
        return -EINVAL;
    }

    audio_class->module_num = input_offset;
    audio_class->isoin = 0;
    audio_class->isoout = 0;

    for (size_t i = 0; i < audio_class->module_num; i++) {
        ret = usbh_audio_close(audio_class, audio_class->module[i].name);
        if (ret < 0) {
            USB_LOG_ERR("Fail to close audio module :%s\r\n", audio_class->module[i].name);
            usbh_audio_class_free(audio_class);
            return ret;
        }
    }

    usbh_audio_list_module(audio_class);

    snprintf(hport->config.intf[intf].devname, CONFIG_USBHOST_DEV_NAMELEN, DEV_FORMAT, audio_class->minor);
    USB_LOG_INFO("Register Audio Class:%s\r\n", hport->config.intf[intf].devname);

    usbh_audio_run(audio_class);
    return 0;
}

static int usbh_audio_ctrl_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
    int ret = 0;
	USB_LOG_DBG("[+]%s\r\n", __func__);

    struct usbh_audio *audio_class = (struct usbh_audio *)hport->config.intf[intf].priv;

	if (hport->raw_config_desc) {
		usb_free(hport->raw_config_desc);
		hport->raw_config_desc = NULL;
	}

    if (audio_class) {
        if (audio_class->isoin) {
            usbh_pipe_free(audio_class->isoin);
        }

        if (audio_class->isoout) {
            usbh_pipe_free(audio_class->isoout);
        }

        if (hport->config.intf[intf].devname[0] != '\0') {
            USB_LOG_INFO("Unregister Audio Class:%s\r\n", hport->config.intf[intf].devname);
            usbh_audio_stop(audio_class);
        }

        usbh_audio_class_free(audio_class);
    }
	USB_LOG_DBG("[-]%s\r\n", __func__);

    return ret;
}

static int usbh_audio_data_connect(struct usbh_hubport *hport, uint8_t intf)
{
    return 0;
}

static int usbh_audio_data_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
    return 0;
}

__WEAK void usbh_audio_run(struct usbh_audio *audio_class)
{
}

__WEAK void usbh_audio_stop(struct usbh_audio *audio_class)
{
}

void bk_usbh_audio_sw_init(struct usbh_hubport *hport, uint8_t interface_num, uint8_t interface_sub_class)
{

	if(!hport)
		return;
	USB_LOG_DBG("[+]%s\r\n", __func__);

	if(interface_sub_class == AUDIO_SUBCLASS_AUDIOCONTROL)
		usbh_audio_ctrl_connect(hport, interface_num);

	if(interface_sub_class == AUDIO_SUBCLASS_AUDIOSTREAMING)
		usbh_audio_data_connect(hport, interface_num);

	USB_LOG_DBG("[-]%s\r\n", __func__);

}

void bk_usbh_audio_sw_deinit(struct usbh_hubport *hport, uint8_t interface_num, uint8_t interface_sub_class)
{

	if(!hport)
		return;
	USB_LOG_DBG("[+]%s\r\n", __func__);

	if(interface_sub_class == AUDIO_SUBCLASS_AUDIOCONTROL)
		usbh_audio_ctrl_disconnect(hport, interface_num);

	if(interface_sub_class == AUDIO_SUBCLASS_AUDIOSTREAMING)
		usbh_audio_data_disconnect(hport, interface_num);

	USB_LOG_DBG("[-]%s\r\n", __func__);

}

void bk_usbh_audio_unregister_dev(void)
{
    char undevname[CONFIG_USBHOST_DEV_NAMELEN];
    int devno;

    for (devno = 0; devno < CONFIG_USBHOST_MAX_AUDIO_CLASS; devno++) {
        if ((g_devinuse & (1 << devno)) != 0) {

            snprintf(undevname, CONFIG_USBHOST_DEV_NAMELEN, DEV_FORMAT, devno);

            struct usbh_audio *audio_class = (struct usbh_audio *)usbh_find_class_instance(undevname);
            if(audio_class != NULL) {
                if (audio_class->isoin) {
                    usbh_pipe_free(audio_class->isoin);
                }

                if (audio_class->isoout) {
                    usbh_pipe_free(audio_class->isoout);
                }

                USB_LOG_INFO("%s Check Unregister Audio Class:%s\r\n", __func__, undevname);

                usbh_audio_class_free(audio_class);
            }
        }
    }
}

#if 0
const struct usbh_class_driver audio_ctrl_class_driver = {
    .driver_name = "audio_ctrl",
    .connect = usbh_audio_ctrl_connect,
    .disconnect = usbh_audio_ctrl_disconnect
};

const struct usbh_class_driver audio_streaming_class_driver = {
    .driver_name = "audio_streaming",
    .connect = usbh_audio_data_connect,
    .disconnect = usbh_audio_data_disconnect
};

CLASS_INFO_DEFINE const struct usbh_class_info audio_ctrl_intf_class_info = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS,
    .class = USB_DEVICE_CLASS_AUDIO,
    .subclass = AUDIO_SUBCLASS_AUDIOCONTROL,
    .protocol = 0x00,
    .vid = 0x00,
    .pid = 0x00,
    .class_driver = &audio_ctrl_class_driver
};

CLASS_INFO_DEFINE const struct usbh_class_info audio_streaming_intf_class_info = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS,
    .class = USB_DEVICE_CLASS_AUDIO,
    .subclass = AUDIO_SUBCLASS_AUDIOSTREAMING,
    .protocol = 0x00,
    .vid = 0x00,
    .pid = 0x00,
    .class_driver = &audio_streaming_class_driver
};
#endif
