/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usbh_core.h"
#include "usbh_video.h"
#include "usb_driver.h"

#define DEV_FORMAT "/dev/video%d"

/* general descriptor field offsets */
#define DESC_bLength              0 /** Length offset */
#define DESC_bDescriptorType      1 /** Descriptor type offset */
#define DESC_bDescriptorSubType   2 /** Descriptor subtype offset */
#define DESC_bNumFormats          3 /** Descriptor numformat offset */
#define DESC_bNumFrameDescriptors 4 /** Descriptor numframe offset */
#define DESC_bFormatIndex         3 /** Descriptor format index offset */
#define DESC_bFrameIndex          3 /** Descriptor frame index offset */
#define DESC_H26X_FORMAT_CHECK_NUM       8
#define DESC_H264_FORMAT_GUID_INDEX      0x34
#define DESC_H265_FORMAT_GUID_INDEX      0x35
/* interface descriptor field offsets */
#define INTF_DESC_bInterfaceNumber  2 /** Interface number offset */
#define INTF_DESC_bAlternateSetting 3 /** Alternate setting offset */

static uint32_t g_devinuse = 0;

USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t g_video_buf[128];

static const char *format_type[] = { "uncompressed", "mjpeg", "H264", "H265" };

#if CONFIG_YUV_TO_RGB
static int __s_r_1370705v[256] = { 0 };
static int __s_b_1732446u[256] = { 0 };
static int __s_g_337633u[256] = { 0 };
static int __s_g_698001v[256] = { 0 };

static void inityuyv2rgb_table(void)
{
    for (int i = 0; i < 256; i++) {
        __s_r_1370705v[i] = (1.370705 * (i - 128));
        __s_b_1732446u[i] = (1.732446 * (i - 128));
        __s_g_337633u[i] = (0.337633 * (i - 128));
        __s_g_698001v[i] = (0.698001 * (i - 128));
    }
}

static void yuyv2rgb565(void *input, void *output, uint32_t len)
{
    int y0, u, y1, v;
    uint8_t r, g, b;
    int val;

    for (uint32_t i = 0; i < len / 4; i++) {
        y0 = (int)(((uint8_t *)input)[i * 4 + 0]);
        u = (int)(((uint8_t *)input)[i * 4 + 1]);
        y1 = (int)(((uint8_t *)input)[i * 4 + 2]);
        v = (int)(((uint8_t *)input)[i * 4 + 3]);
        val = y0 + __s_r_1370705v[v];
        r = (val < 0) ? 0 : ((val > 255) ? 255 : (uint8_t)val);
        val = y0 - __s_g_337633u[u] - __s_g_698001v[v];
        g = (val < 0) ? 0 : ((val > 255) ? 255 : (uint8_t)val);
        val = y0 + __s_b_1732446u[u];
        b = (val < 0) ? 0 : ((val > 255) ? 255 : (uint8_t)val);
        ((uint16_t *)output)[i * 2] = (uint16_t)(b >> 3) | ((uint16_t)(g >> 2) << 5) | ((uint16_t)(r >> 3) << 11);
        val = y1 + __s_r_1370705v[v];
        r = (val < 0) ? 0 : ((val > 255) ? 255 : (uint8_t)val);
        val = y1 - __s_g_337633u[u] - __s_g_698001v[v];
        g = (val < 0) ? 0 : ((val > 255) ? 255 : (uint8_t)val);
        val = y1 + __s_b_1732446u[u];
        b = (val < 0) ? 0 : ((val > 255) ? 255 : (uint8_t)val);
        ((uint16_t *)output)[i * 2 + 1] = (uint16_t)(b >> 3) | ((uint16_t)(g >> 2) << 5) | ((uint16_t)(r >> 3) << 11);
    }
}
#endif
static int usbh_video_devno_alloc(struct usbh_video *video_class)
{
    int devno;

    for (devno = 0; devno < 32; devno++) {
        uint32_t bitno = 1 << devno;
        if ((g_devinuse & bitno) == 0) {
            g_devinuse |= bitno;
            video_class->minor = devno;
            return 0;
        }
    }

    return -EMFILE;
}

static void usbh_video_devno_free(struct usbh_video *video_class)
{
    int devno = video_class->minor;

    if (devno >= 0 && devno < 32) {
        g_devinuse &= ~(1 << devno);
    }
}

int usbh_video_get_cur(struct usbh_video *video_class, uint8_t intf, uint8_t entity_id, uint8_t cs, uint8_t *buf, uint16_t len)
{
    struct usb_setup_packet *setup = &video_class->hport->setup;
    int ret;

    setup->bmRequestType = USB_REQUEST_DIR_IN | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = VIDEO_REQUEST_GET_CUR;
    setup->wValue = cs << 8;
    setup->wIndex = (entity_id << 8) | intf;
    setup->wLength = len;

    ret = usbh_control_transfer(video_class->hport->ep0, setup, g_video_buf);
    if (ret < 0) {
        return ret;
    }
    memcpy(buf, g_video_buf, len);
    return ret;
}

int usbh_video_set_cur(struct usbh_video *video_class, uint8_t intf, uint8_t entity_id, uint8_t cs, uint8_t *buf, uint16_t len)
{
    struct usb_setup_packet *setup = &video_class->hport->setup;

    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = VIDEO_REQUEST_SET_CUR;
    setup->wValue = cs << 8;
    setup->wIndex = (entity_id << 8) | intf;
    setup->wLength = len;

    memcpy(g_video_buf, buf, len);

    return usbh_control_transfer(video_class->hport->ep0, setup, g_video_buf);
}

int usbh_videostreaming_get_cur_probe(struct usbh_video *video_class)
{
    return usbh_video_get_cur(video_class, video_class->data_intf, 0x00, VIDEO_VS_PROBE_CONTROL, (uint8_t *)&video_class->probe, 26);
}

int usbh_videostreaming_set_cur_probe(struct usbh_video *video_class, uint8_t formatindex, uint8_t frameindex, uint32_t dwMaxVideoFrameSize, uint32_t dwMaxPayloadTransferSize)
{
    video_class->probe.bFormatIndex = formatindex;
    video_class->probe.bFrameIndex = frameindex;
    video_class->probe.dwMaxVideoFrameSize = dwMaxVideoFrameSize;
    video_class->probe.dwMaxPayloadTransferSize = dwMaxPayloadTransferSize;
    return usbh_video_set_cur(video_class, video_class->data_intf, 0x00, VIDEO_VS_PROBE_CONTROL, (uint8_t *)&video_class->probe, 26);
}

int usbh_videostreaming_set_cur_commit(struct usbh_video *video_class, uint8_t formatindex, uint8_t frameindex, uint32_t dwMaxVideoFrameSize, uint32_t dwMaxPayloadTransferSize)
{
    memcpy(&video_class->commit, &video_class->probe, 26);
    video_class->commit.bFormatIndex = formatindex;
    video_class->commit.bFrameIndex = frameindex;
    video_class->commit.dwMaxVideoFrameSize = dwMaxVideoFrameSize;
    video_class->commit.dwMaxPayloadTransferSize = dwMaxPayloadTransferSize;
    return usbh_video_set_cur(video_class, video_class->data_intf, 0x00, VIDEO_VS_COMMIT_CONTROL, (uint8_t *)&video_class->commit, 26);
}

int usbh_video_open(struct usbh_video *video_class, uint8_t altsetting)
{
    struct usb_setup_packet *setup = &video_class->hport->setup;
    struct usb_endpoint_descriptor *ep_desc;
    uint8_t mult;
    uint16_t mps;
    int ret = 0;

//  if (video_class->is_opened) {
//      return -EMFILE;
//  }

    if (altsetting > (video_class->num_of_intf_altsettings - 1)) {
        return -EINVAL;
    }

    ep_desc = &video_class->hport->config.intf[video_class->data_intf].altsetting[altsetting].ep[0].ep_desc;

    switch (ep_desc->bmAttributes & USB_ENDPOINT_TYPE_MASK) {
        case USB_ENDPOINT_TYPE_BULK:
        case USB_ENDPOINT_TYPE_INTERRUPT:
            break;
        case USB_ENDPOINT_TYPE_ISOCHRONOUS:
            {
               setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_STANDARD | USB_REQUEST_RECIPIENT_INTERFACE;
               setup->bRequest = USB_REQUEST_SET_INTERFACE;
               setup->wValue = altsetting;
               setup->wIndex = video_class->data_intf;
               setup->wLength = 0;
           
               ret = usbh_control_transfer(video_class->hport->ep0, setup, NULL);
               if (ret < 0) {
                   return ret;
               }
            }
            break;
        default:
            break;
    }

    mult = (ep_desc->wMaxPacketSize & USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_MASK) >> USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_SHIFT;
    mps  = ep_desc->wMaxPacketSize & USB_MAXPACKETSIZE_MASK;
    if (ep_desc->bEndpointAddress & 0x80) {
        video_class->isoin_mps = mps * (mult + 1);
        usbh_hport_activate_epx(&video_class->isoin, video_class->hport, ep_desc);
    } else {
        video_class->isoout_mps = mps * (mult + 1);
        usbh_hport_activate_epx(&video_class->isoout, video_class->hport, ep_desc);
    }
    USB_LOG_DBG("Open video and select altsetting:%u\r\n", altsetting);
    video_class->is_opened = true;
    return ret;
}

int usbh_video_close(struct usbh_video *video_class)
{
    struct usb_setup_packet *setup = &video_class->hport->setup;
    int ret;

    setup->bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_STANDARD | USB_REQUEST_RECIPIENT_INTERFACE;
    setup->bRequest = USB_REQUEST_SET_INTERFACE;
    setup->wValue = 0;
    setup->wIndex = video_class->data_intf;
    setup->wLength = 0;

    ret = usbh_control_transfer(video_class->hport->ep0, setup, NULL);
    if (ret < 0) {
        return ret;
    }
    if (video_class->isoin) {
        usbh_pipe_free(video_class->isoin);
        //video_class->isoin = NULL;
    }

    if (video_class->isoout) {
        usbh_pipe_free(video_class->isoout);
        //video_class->isoout = NULL;
    }

    USB_LOG_DBG("Close video device\r\n");
    video_class->is_opened = false;
    return ret;
}

void usbh_video_list_info(struct usbh_video *video_class)
{
    struct usbh_hubport *hport;
    struct usb_endpoint_descriptor *ep_desc;
    uint8_t mult;
    uint16_t mps;

    USB_LOG_DBG("============= Video device information ===================\r\n");
    USB_LOG_INFO("bcdVDC:%04x\r\n", video_class->bcdVDC);
    USB_LOG_DBG("Num of altsettings:%02x\r\n", video_class->num_of_intf_altsettings);

    hport = video_class->hport;

    for (uint8_t i = 1; i < video_class->num_of_intf_altsettings; i++) {
        ep_desc = &hport->config.intf[video_class->data_intf].altsetting[i].ep[0].ep_desc;

        mult = (ep_desc->wMaxPacketSize & USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_MASK) >> USB_MAXPACKETSIZE_ADDITIONAL_TRANSCATION_SHIFT;
        mps = ep_desc->wMaxPacketSize & USB_MAXPACKETSIZE_MASK;

        USB_LOG_INFO("Altsetting:%u, Ep=%02x Attr=%02u Mps=%d Interval=%02u Mult=%02u\r\n",
                     i,
                     ep_desc->bEndpointAddress,
                     ep_desc->bmAttributes,
                     mps,
                     ep_desc->bInterval,
                     mult);
    }

    USB_LOG_INFO("bNumFormats:%u\r\n", video_class->num_of_formats);
    for (uint8_t i = 0; i < video_class->num_of_formats; i++) {
        USB_LOG_INFO("  FormatIndex:%u\r\n", i + 1);
        USB_LOG_INFO("  FormatType:%s\r\n", format_type[video_class->format[i].format_type]);
        USB_LOG_INFO("  bNumFrames:%u\r\n", video_class->format[i].num_of_frames);
        USB_LOG_DBG("  Resolution:\r\n");
        for (uint8_t j = 0; j < video_class->format[i].num_of_frames; j++) {
            USB_LOG_INFO("      FrameIndex:%u\r\n", j + 1);
            USB_LOG_INFO("      wWidth: %d, wHeight: %d\r\n",
                         video_class->format[i].frame[j].wWidth,
                         video_class->format[i].frame[j].wHeight);
        }
    }

    usbh_videostreaming_get_cur_probe(video_class);

    USB_LOG_DBG("dwMaxVideoFrameSize:%u,dwMaxPayloadTransferSize:%u\r\n", (int)video_class->probe.dwMaxVideoFrameSize, (int)video_class->probe.dwMaxPayloadTransferSize);

    USB_LOG_DBG("============= Video device information ===================\r\n");
	//USB_LOG_INFO("%s hport:%x\r\n",__func__, hport);
}

static int usbh_video_ctrl_intf_connect(struct usbh_hubport *hport, uint8_t intf)
{
    int ret;
    uint8_t cur_iface = 0xff;
    //uint8_t cur_alt_setting = 0xff;
    uint8_t frame_index = 0xff;
    uint8_t format_index = 0xff;
    uint8_t num_of_frames = 0xff;
    uint8_t *p;

    //USB_LOG_INFO("%s cur_alt_setting:%x\r\n",__func__, cur_alt_setting);
    struct usbh_video *video_class = usb_malloc(sizeof(struct usbh_video));
    if (video_class == NULL) {
        USB_LOG_ERR("Fail to alloc video_class\r\n");
        return -ENOMEM;
    }

    memset(video_class, 0, sizeof(struct usbh_video));
    usbh_video_devno_alloc(video_class);
    video_class->hport = hport;
    video_class->ctrl_intf = intf;
    video_class->data_intf = intf + 1;
    video_class->num_of_intf_altsettings = hport->config.intf[intf + 1].altsetting_num;

    hport->config.intf[intf].priv = video_class;

    ret = usbh_video_close(video_class);
    if (ret < 0) {
        USB_LOG_ERR("Fail to close video device\r\n");
        usbh_video_devno_free(video_class);
        return ret;
    }

    p = hport->raw_config_desc;
    while (p[DESC_bLength]) {
        switch (p[DESC_bDescriptorType]) {
            case USB_DESCRIPTOR_TYPE_INTERFACE:
                cur_iface = p[INTF_DESC_bInterfaceNumber];
                //cur_alt_setting = p[INTF_DESC_bAlternateSetting];
                break;
            case USB_DESCRIPTOR_TYPE_ENDPOINT:
                //ep_desc = (struct usb_endpoint_descriptor *)p;
                break;
            case VIDEO_CS_INTERFACE_DESCRIPTOR_TYPE:
                if (cur_iface == video_class->ctrl_intf) {
                    switch (p[DESC_bDescriptorSubType]) {
                        case VIDEO_VC_HEADER_DESCRIPTOR_SUBTYPE:
                            video_class->bcdVDC = ((uint16_t)p[4] << 8) | (uint16_t)p[3];
                            break;
                        case VIDEO_VC_INPUT_TERMINAL_DESCRIPTOR_SUBTYPE:
                            break;
                        case VIDEO_VC_OUTPUT_TERMINAL_DESCRIPTOR_SUBTYPE:
                            break;
                        case VIDEO_VC_PROCESSING_UNIT_DESCRIPTOR_SUBTYPE:
                            break;

                        default:
                            break;
                    }
                } else if (cur_iface == video_class->data_intf) {
                    switch (p[DESC_bDescriptorSubType]) {
                        case VIDEO_VS_INPUT_HEADER_DESCRIPTOR_SUBTYPE:
                            video_class->num_of_formats = p[DESC_bNumFormats];
                            break;
                        case VIDEO_VS_FORMAT_UNCOMPRESSED_DESCRIPTOR_SUBTYPE:
                            format_index = p[DESC_bFormatIndex];
                            num_of_frames = p[DESC_bNumFrameDescriptors];

                            video_class->format[format_index - 1].num_of_frames = num_of_frames;
                            video_class->format[format_index - 1].format_type = USBH_VIDEO_FORMAT_UNCOMPRESSED;
                            break;
                        case VIDEO_VS_FORMAT_MJPEG_DESCRIPTOR_SUBTYPE:
                            format_index = p[DESC_bFormatIndex];
                            num_of_frames = p[DESC_bNumFrameDescriptors];

                            video_class->format[format_index - 1].num_of_frames = num_of_frames;
                            video_class->format[format_index - 1].format_type = USBH_VIDEO_FORMAT_MJPEG;
                            break;
                        case VIDEO_VS_FORMAT_FRAME_BASED_DESCRIPTOR_SUBTYPE:
                            format_index = p[DESC_bFormatIndex];
                            num_of_frames = p[DESC_bNumFrameDescriptors];

                            video_class->format[format_index - 1].num_of_frames = num_of_frames;
                            if(p[DESC_H26X_FORMAT_CHECK_NUM] == DESC_H264_FORMAT_GUID_INDEX)
                                video_class->format[format_index - 1].format_type = USBH_VIDEO_FORMAT_H264;
                            if(p[DESC_H26X_FORMAT_CHECK_NUM] == DESC_H265_FORMAT_GUID_INDEX)
                                video_class->format[format_index - 1].format_type = USBH_VIDEO_FORMAT_H265;
                            break;
                        case VIDEO_VS_FRAME_UNCOMPRESSED_DESCRIPTOR_SUBTYPE:
                            frame_index = p[DESC_bFrameIndex];

                            video_class->format[format_index - 1].frame[frame_index - 1].wWidth = ((struct video_cs_if_vs_frame_uncompressed_descriptor *)p)->wWidth;
                            video_class->format[format_index - 1].frame[frame_index - 1].wHeight = ((struct video_cs_if_vs_frame_uncompressed_descriptor *)p)->wHeight;
                            video_class->format[format_index - 1].frame[frame_index - 1].frame_index = ((struct video_cs_if_vs_frame_uncompressed_descriptor *)p)->bFrameIndex;
                            video_class->format[format_index - 1].frame[frame_index - 1].fps_num = ((struct video_cs_if_vs_frame_uncompressed_descriptor *)p)->bFrameIntervalType;
							video_class->format[format_index - 1].frame[frame_index - 1].fps =usb_malloc(sizeof(uint32_t) * video_class->format[format_index - 1].frame[frame_index - 1].fps_num);
                            for(int i = 0; i < video_class->format[format_index - 1].frame[frame_index - 1].fps_num; i++)
                            {
                                video_class->format[format_index - 1].frame[frame_index - 1].fps[i] = ((struct video_cs_if_vs_frame_uncompressed_descriptor *)p)->dwFrameInterval[i];
                                USB_LOG_DBG("fps:%d\r\n", video_class->format[format_index - 1].frame[frame_index - 1].fps[i]);
                            }
                            break;
                        case VIDEO_VS_FRAME_MJPEG_DESCRIPTOR_SUBTYPE:
                            frame_index = p[DESC_bFrameIndex];

                            video_class->format[format_index - 1].frame[frame_index - 1].wWidth = ((struct video_cs_if_vs_frame_mjpeg_descriptor *)p)->wWidth;
                            video_class->format[format_index - 1].frame[frame_index - 1].wHeight = ((struct video_cs_if_vs_frame_mjpeg_descriptor *)p)->wHeight;
                            video_class->format[format_index - 1].frame[frame_index - 1].frame_index = frame_index;
                            video_class->format[format_index - 1].frame[frame_index - 1].fps_num = ((struct video_cs_if_vs_frame_mjpeg_descriptor *)p)->bFrameIntervalType;
                            video_class->format[format_index - 1].frame[frame_index - 1].fps =usb_malloc(sizeof(uint32_t) * video_class->format[format_index - 1].frame[frame_index - 1].fps_num);
                            for(int i = 0; i < video_class->format[format_index - 1].frame[frame_index - 1].fps_num; i++)
                            {
                                video_class->format[format_index - 1].frame[frame_index - 1].fps[i] = ((struct video_cs_if_vs_frame_mjpeg_descriptor *)p)->dwFrameInterval[i];
                                USB_LOG_DBG("fps:%d\r\n", video_class->format[format_index - 1].frame[frame_index - 1].fps[i]);
                            }
                            break;
                        case VIDEO_VS_FRAME_FRAME_BASED_DESCRIPTOR_SUBTYPE:
                            frame_index = p[DESC_bFrameIndex];

                            video_class->format[format_index - 1].frame[frame_index - 1].wWidth = ((struct video_cs_if_vs_frame_h26x_descriptor *)p)->wWidth;
                            video_class->format[format_index - 1].frame[frame_index - 1].wHeight = ((struct video_cs_if_vs_frame_h26x_descriptor *)p)->wHeight;
                            video_class->format[format_index - 1].frame[frame_index - 1].frame_index = frame_index;
                            video_class->format[format_index - 1].frame[frame_index - 1].fps_num = ((struct video_cs_if_vs_frame_h26x_descriptor *)p)->bFrameIntervalType;
                            video_class->format[format_index - 1].frame[frame_index - 1].fps =usb_malloc(sizeof(uint32_t) * video_class->format[format_index - 1].frame[frame_index - 1].fps_num);
                            for(int i = 0; i < video_class->format[format_index - 1].frame[frame_index - 1].fps_num; i++)
                            {
                                video_class->format[format_index - 1].frame[frame_index - 1].fps[i] = ((struct video_cs_if_vs_frame_h26x_descriptor *)p)->dwFrameInterval[i];
                                USB_LOG_DBG("fps:%d\r\n", video_class->format[format_index - 1].frame[frame_index - 1].fps[i]);
                            }
                            break;
                        default:
                            break;
                    }
                }

                break;

            default:
                break;
        }
        /* skip to next descriptor */
        p += p[DESC_bLength];
    }

    usbh_video_list_info(video_class);

    snprintf(hport->config.intf[intf].devname, CONFIG_USBHOST_DEV_NAMELEN, DEV_FORMAT, video_class->minor);
#if CONFIG_YUV_TO_RGB
    inityuyv2rgb_table();
#endif
    USB_LOG_INFO("Register Video Class:%s\r\n", hport->config.intf[intf].devname);

    return 0;
}

static int usbh_video_ctrl_intf_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
    int ret = 0;

	if (hport->raw_config_desc) {
		usb_free(hport->raw_config_desc);
		hport->raw_config_desc = NULL;
	}

    struct usbh_video *video_class = (struct usbh_video *)hport->config.intf[intf].priv;

    if (video_class) {
        for(uint8_t format_index = 0; format_index < USBH_VIDEO_FORMAT_MAX_NUM; format_index++)
        {
            for(uint8_t frame_index = 0; frame_index < USBH_VIDEO_FRAME_MAX_NUM; frame_index++)
                if(video_class->format[format_index].frame[frame_index].fps) {
                    usb_free(video_class->format[format_index].frame[frame_index].fps);
                    video_class->format[format_index].frame[frame_index].fps = NULL;
                }
        }

        usbh_video_devno_free(video_class);

        if (video_class->isoin) {
            usbh_pipe_free(video_class->isoin);
        }

        if (video_class->isoout) {
            usbh_pipe_free(video_class->isoout);
        }

        memset(video_class, 0, sizeof(struct usbh_video));
        usb_free(video_class);
        video_class = NULL;

        if (hport->config.intf[intf].devname[0] != '\0')
            USB_LOG_INFO("Unregister Video Class:%s\r\n", hport->config.intf[intf].devname);
    }

    return ret;
}

static int usbh_video_data_intf_connect(struct usbh_hubport *hport, uint8_t intf)
{
    USB_LOG_DBG("Ignore video data intf\r\n");
    return 0;
}

static int usbh_video_data_intf_disconnect(struct usbh_hubport *hport, uint8_t intf)
{
    return 0;
}


static uint8_t usbh_video_sw_init_flag = 0;
void bk_usbh_video_sw_init( struct usbh_hubport *hport, uint8_t interface_num, uint8_t interface_sub_class)
{
	if(!hport)
		return;
    if (interface_sub_class == VIDEO_SC_VIDEOCONTROL)
    {
    	if(usbh_video_sw_init_flag > 1)
    		return;
    	else
    		usbh_video_sw_init_flag++;
    	USB_LOG_DBG("[+]%s\r\n", __func__);
    	int ret = 0;
    	if(interface_sub_class == VIDEO_SC_VIDEOCONTROL) {
    		ret = usbh_video_ctrl_intf_connect(hport, interface_num);
    		if(ret != 0) {
    			usbh_video_sw_init_flag = 0;
    			USB_LOG_DBG("[=]%s VIDEO CONTROL ERROR\r\n", __func__);
    			return;
    		}
    	}

    	USB_LOG_DBG("[-]%s\r\n", __func__);
    }
	return;
}

void bk_usbh_video_sw_deinit( struct usbh_hubport *hport, uint8_t interface_num, uint8_t interface_sub_class)
{
	if(!hport)
		return;
	if(!usbh_video_sw_init_flag) {
		return;
    }
	else {
		usbh_video_sw_init_flag--;
    }
	USB_LOG_DBG("[+]%s\r\n", __func__);

	if(interface_sub_class == VIDEO_SC_VIDEOCONTROL)
		usbh_video_ctrl_intf_disconnect(hport, interface_num);

	if(interface_sub_class == VIDEO_SC_VIDEOSTREAMING)
		usbh_video_data_intf_disconnect(hport, interface_num);

	USB_LOG_DBG("[-]%s\r\n", __func__);
}

void bk_usbh_video_unregister_dev(void)
{
    char undevname[CONFIG_USBHOST_DEV_NAMELEN];
    int devno;

    for (devno = 0; devno < 32; devno++) {
        uint32_t bitno = 1 << devno;
        if ((g_devinuse & bitno) != 0) {
            snprintf(undevname, CONFIG_USBHOST_DEV_NAMELEN, DEV_FORMAT, devno);
            struct usbh_video *video_class = (struct usbh_video *)usbh_find_class_instance(undevname);

            if(video_class) {
                usbh_video_devno_free(video_class);

                if (video_class->isoin) {
                    usbh_pipe_free(video_class->isoin);
                }

                if (video_class->isoout) {
                    usbh_pipe_free(video_class->isoout);
                }

                memset(video_class, 0, sizeof(struct usbh_video));
                usb_free(video_class);

                USB_LOG_INFO("%s Check Unregister Video Class:%s\r\n", __func__, undevname);
            }
        }
    }

}

#if CONFIG_YUV_TO_RGB
void usbh_videostreaming_parse_mjpeg(struct usbh_urb *urb, struct usbh_videostreaming *stream)
{
    struct usbh_iso_frame_packet *iso_packet;
    uint32_t num_of_iso_packets;
    uint8_t *tmp_buf;
    uint8_t data_offset;
    uint32_t data_len;

    num_of_iso_packets = urb->num_of_iso_packets;
    iso_packet = urb->iso_packet;

    for (uint32_t i = 0; i < num_of_iso_packets; i++) {
        /*
            uint8_t frameIdentifier : 1U;
            uint8_t endOfFrame      : 1U;
            uint8_t presentationTimeStamp    : 1U;
            uint8_t sourceClockReference : 1U;
            uint8_t reserved             : 1U;
            uint8_t stillImage           : 1U;
            uint8_t errorBit             : 1U;
            uint8_t endOfHeader          : 1U;
        */
        if (iso_packet[i].actual_length == 0) { /* skip no data */
            continue;
        }
        if (iso_packet[i].actual_length < iso_packet[i].transfer_buffer[0]) { /* do not be illegal */
            while (1) {
            }
        }
        if ((iso_packet[i].transfer_buffer[0] > 12) || (iso_packet[i].transfer_buffer[0] == 0)) { /* do not be illegal */
            while (1) {
            }
        }
        if (iso_packet[i].transfer_buffer[1] & (1 << 6)) { /* error bit, re-receive */
            stream->bufoffset = 0;
            continue;
        }

        data_offset = iso_packet[i].transfer_buffer[0];
        data_len = iso_packet[i].actual_length - iso_packet[i].transfer_buffer[0];

        tmp_buf = stream->bufbase + stream->bufoffset;
        stream->bufoffset += data_len;

        memcpy(tmp_buf, &iso_packet[i].transfer_buffer[data_offset], data_len);
        if ((stream->bufbase[0] != 0xff) && (stream->bufbase[1] != 0xd8)) {
            stream->bufoffset = 0;
            continue;
        }

        if (iso_packet[i].transfer_buffer[1] & (1 << 1)) {
            if ((stream->bufbase[stream->bufoffset - 2] != 0xff) && (stream->bufbase[stream->bufoffset - 1] != 0xd9)) {
                stream->bufoffset = 0;
                continue;
            }
            if (stream->video_one_frame_callback) {
                stream->video_one_frame_callback(stream);
            }
            stream->bufoffset = 0;
        }
    }
}

void usbh_videostreaming_parse_yuyv2rgb565(struct usbh_urb *urb, struct usbh_videostreaming *stream)
{
    struct usbh_iso_frame_packet *iso_packet;
    uint32_t num_of_iso_packets;
    uint8_t *tmp_buf;
    uint8_t data_offset;
    uint32_t data_len;

    num_of_iso_packets = urb->num_of_iso_packets;
    iso_packet = urb->iso_packet;

    for (uint32_t i = 0; i < num_of_iso_packets; i++) {
        /*
            uint8_t frameIdentifier : 1U;
            uint8_t endOfFrame      : 1U;
            uint8_t presentationTimeStamp    : 1U;
            uint8_t sourceClockReference : 1U;
            uint8_t reserved             : 1U;
            uint8_t stillImage           : 1U;
            uint8_t errorBit             : 1U;
            uint8_t endOfHeader          : 1U;
        */

        if (iso_packet[i].actual_length == 0) { /* skip no data */
            continue;
        }
        if (iso_packet[i].actual_length < iso_packet[i].transfer_buffer[0]) { /* do not be illegal */
            while (1) {
            }
        }
        if ((iso_packet[i].transfer_buffer[0] > 12) || (iso_packet[i].transfer_buffer[0] == 0)) { /* do not be illegal */
            while (1) {
            }
        }
        if (iso_packet[i].transfer_buffer[1] & (1 << 6)) { /* error bit, re-receive */
            stream->bufoffset = 0;
            continue;
        }

        data_offset = iso_packet[i].transfer_buffer[0];
        data_len = iso_packet[i].actual_length - iso_packet[i].transfer_buffer[0];

        tmp_buf = stream->bufbase + stream->bufoffset;
        stream->bufoffset += data_len;

        yuyv2rgb565(&iso_packet[i].transfer_buffer[data_offset], tmp_buf, data_len);
        if (iso_packet[i].transfer_buffer[1] & (1 << 1)) {
            if (stream->video_one_frame_callback) {
                stream->video_one_frame_callback(stream);
            }
            stream->bufoffset = 0;
        }
    }
}
#endif
#if 0
const struct usbh_class_driver video_class_ctrl_intf_driver = {
    .driver_name = "video",
    .connect = usbh_video_ctrl_intf_connect,
    .disconnect = usbh_video_ctrl_intf_disconnect
};

const struct usbh_class_driver video_class_data_intf_driver = {
    .driver_name = "video",
    .connect = usbh_video_data_intf_connect,
    .disconnect = usbh_video_data_intf_disconnect
};

CLASS_INFO_DEFINE const struct usbh_class_info video_ctrl_intf_class_info = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
    .class = USB_DEVICE_CLASS_VIDEO,
    .subclass = VIDEO_SC_VIDEOCONTROL,
    .protocol = VIDEO_PC_PROTOCOL_UNDEFINED,
    .vid = 0x00,
    .pid = 0x00,
    .class_driver = &video_class_ctrl_intf_driver
};

CLASS_INFO_DEFINE const struct usbh_class_info video_data_intf_class_info = {
    .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS | USB_CLASS_MATCH_INTF_PROTOCOL,
    .class = USB_DEVICE_CLASS_VIDEO,
    .subclass = VIDEO_SC_VIDEOSTREAMING,
    .protocol = VIDEO_PC_PROTOCOL_UNDEFINED,
    .vid = 0x00,
    .pid = 0x00,
    .class_driver = &video_class_data_intf_driver
};
#endif
