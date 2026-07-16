/*
 * gstambaoverlaysrc.c
 *
 * History:
 *    4/19/2024 - [Peng-Xue Duan] created file
 *
 * Copyright (C) 2022 Ambarella International LP
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-amba_overlay_src
 * @title: amba_overlay_src
 * @see_also: amba_vencoverlay
 *
 *
 * prepare on EVK
 * modprobe cavalry
 * cavalry_load -f /lib/firmware/cavalry.bin -r
 * init.sh --na; modprobe max9296  id=0x0303 vinc_id=0xb8; modprobe os08a10_mipi_brg fsync=1
 * test_aaa_service -a &
 * test_encode --resource-cfg cv72_vin0_1_6_streams.lua --vout-cfg /usr/share/ambarella/lua_scripts/vout_hdmi.lua
 * test_encode -A -H 1080p -b 0 -e -B -H 1080p -b 1 -e -C -H 480p -b 2 -e -D -H 480p -b 3 -e -E -h 480p -b 4 -e -S 5 -h 480p -b 5 -e
 * rtsp_server&
 *
 * This element generates cluts and data used for drawing on Ambarella overlay.
 *
 * ## Example pipelines, draw bmp
 * |[
 * gst-launch-1.0 amba_overlay_src osd=area_id:0,enable:1,roi:0.0.256.128,bg_color:8080eb00,buf_num:2,type:picture,bmp:/tmp/picture/Ambarella-256x128-8bit.bmp \
 * ! amba_venc_overlay stream_id=0 osd_offset =0 osd_size=81920 sync=false
 * ]|
 *
 * ## Example pipelines, draw string "HelloAmbarella"
 * |[
 * gst-launch-1.0 amba_overlay_src osd=area_id:0,enable:1,roi:960.540.384.160,bg_color:8080eb00,buf_num:2,type:string,str:HelloAmbarella,\
 * font_name:/tmp/arial.ttf,font_size:32,font_outline_w:4,font_ver_bold:1,font_hor_bold:1,\
 * font_color:9210d2ff,text_bg_color:223691ff,ol_color:f05a52ff ! amba_venc_overlay stream_id=0 osd_offset =0 osd_size=139264 sync=false
 * ]|
 *
 * ## Example pipelines, draw time
 * |[
 * gst-launch-1.0 amba_overlay_src osd=area_id:0,enable:1,roi:1408.0.512.144,bg_color:8080eb00,buf_num:2,type:time,\
 * en_msec:0,format:0,is_12h:0,pre_str:BJT,font_name:/tmp/arial.ttf,font_size:32,font_outline_w:4,\
 * font_ver_bold:1,font_hor_bold:1,font_color:8080ebff,text_bg_color:80800c00,ol_color:f05a52ff,update:30 ! \
 * amba_venc_overlay stream_id=0 osd_offset =0 osd_size=163840 sync=false
 * ]|
 *
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "common_err_code_c.h"

#include "bitstream_state.h"

#include "internal.h"
#include "debug_log.h"

#include "amba_direct_mem.h"

#include "buffer_utils.h"

#include "gstambaoverlaysrc.h"
#include "amba_draw_data_picture.h"
#include "amba_draw_data_string.h"
#include "amba_draw_data_time.h"


/* ClockSync args */
#define DEFAULT_PROVIDE_CLOCK       FALSE
#define DEFAULT_SLEEP_TIME_US       (33000)

GST_DEBUG_CATEGORY_STATIC (gst_amba_overlay_src_debug);
#define GST_CAT_DEFAULT gst_amba_overlay_src_debug

enum
{
  PROP_0,
  PROP_DRAW_FORMAT,
  PROP_PROVIDE_CLOCK,
  PROP_REFRESH,
  PROP_OSD,
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL))
  );

#define gst_amba_overlay_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaOverlaySrc, gst_amba_overlay_src, GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_amba_overlay_src_debug, "amba_overlay_src", 0,
        "amba overlay src"));

#define AMBA_DEFAULT_OSD_PARAMS "area_id:0,enable:1,roi:0.0.4.4,buf_num:2"

static GstFlowReturn gst_amba_overlay_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_amba_overlay_src_start (GstBaseSrc * bsrc);
static gboolean gst_amba_overlay_src_stop (GstBaseSrc * bsrc);

static void gst_amba_overlay_src_finalize (GObject * gobject);
static void gst_amba_overlay_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amba_overlay_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstClock *
gst_amba_overlay_src_provide_clock (GstElement * element);

static GstStateChangeReturn
gst_amba_overlay_src_change_state (GstElement * element, GstStateChange transition);

static gboolean
gst_amba_overlay_src_send_event (GstElement * element, GstEvent * event);


static void
gst_amba_overlay_src_class_init (GstAmbaOverlaySrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  gstelement_class->change_state = gst_amba_overlay_src_change_state;
  gstelement_class->provide_clock = GST_DEBUG_FUNCPTR (gst_amba_overlay_src_provide_clock);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_amba_overlay_src_send_event);

  gobject_class->finalize = gst_amba_overlay_src_finalize;
  gobject_class->set_property = gst_amba_overlay_src_set_property;
  gobject_class->get_property = gst_amba_overlay_src_get_property;

  g_object_class_install_property (gobject_class, PROP_DRAW_FORMAT,
      g_param_spec_string ("draw_format", "DrawFormat", "Setup image drawn format?",
          EDRAWFORMATNAME_8BIT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PROVIDE_CLOCK,
      g_param_spec_boolean ("provide-clock", "Provide Clock",
          "Provide a clock to be used as the global pipeline clock",
          DEFAULT_PROVIDE_CLOCK, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REFRESH,
      g_param_spec_uchar ("refresh", "Refresh", "Refresh all the areas  ?",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_OSD,
      g_param_spec_string ("osd", "OSD", "Setup overlay params ?",
          AMBA_DEFAULT_OSD_PARAMS, G_PARAM_READWRITE));

  gst_element_class_set_details_simple(gstelement_class,
    "Amba Overlay source",
    "amba_overlay_src",
    "Generate data drawing on Amba Overlay",
    "pxduan <pxduan@ambarella.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_amba_overlay_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_amba_overlay_src_stop);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_amba_overlay_src_create);
}

static void
gst_amba_overlay_src_init (GstAmbaOverlaySrc * thiz)
{
  priv_overlay_src_ctx_t *filter = (priv_overlay_src_ctx_t *) g_malloc(sizeof(priv_overlay_src_ctx_t));
  if (filter == NULL) {
    GST_ERROR("malloc priv_overlay_src_ctx_t failed\n");
    return;
  }
  memset(filter, 0x0, sizeof(priv_overlay_src_ctx_t));
  thiz->priv_ctx = filter;

  // iav context
  /*filter->iav_ctx = acquire_iav_ctx (1);
  if (!filter->iav_ctx) {
    GST_ERROR("acquire_iav_ctx failed\n");
    return;
  }*/

  filter->give_clock = DEFAULT_PROVIDE_CLOCK;
  filter->gst_clok_time = 0;
  filter->is_clock_started = 0;

  filter->draw_format = AMBA_DRAW_FORMAT_8BIT_CLUT;
  filter->draw_pix_size = 1;
  filter->refresh = 1;
  for (int i = 0; i < MAX_OVERLAY_AREA_NUM; i++) {
    filter->osd_param.area[i].attr.keep_n = -1;
    filter->osd_param.area[i].data.type = AMBA_DRAW_DATA_TYPE_NONE;
    filter->osd_param.area[i].data.pic.colorkey.argb.a = 0xff;
    filter->osd_param.area[i].data.pic.colorkey.rgb = 0xffff;
  }

  gst_base_src_set_live (GST_BASE_SRC (thiz), TRUE);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (thiz), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (thiz), GST_FORMAT_TIME);

  if (filter->give_clock)
    GST_OBJECT_FLAG_SET (thiz, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  else
    GST_OBJECT_FLAG_UNSET (thiz, GST_ELEMENT_FLAG_PROVIDE_CLOCK);

}

static void
gst_amba_overlay_src_finalize (GObject * gobject)
{
  GstAmbaOverlaySrc *thiz = GST_AMBAOVERLAYSRC (gobject);
  priv_overlay_src_ctx_t *filter = (priv_overlay_src_ctx_t *) thiz->priv_ctx;

  if (filter) {
    /*if (filter->iav_ctx) {
      release_iav_ctx (1);
      filter->iav_ctx = NULL;
    }*/

    if (filter->provided_clock) {
      g_object_unref (filter->provided_clock);
    }

    if (filter->osd_info) {
      g_free(filter->osd_info);
      filter->osd_info = NULL;
    }

    if (filter->bitmap.buf) {
      g_free(filter->bitmap.buf);
      filter->bitmap.buf = NULL;
    }

    g_free(thiz->priv_ctx);
    thiz->priv_ctx = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static GstClock *
gst_amba_overlay_src_provide_clock (GstElement * element)
{
  GstAmbaOverlaySrc * src = GST_AMBAOVERLAYSRC (element);
  if (!src->priv_ctx->give_clock) {
    return NULL;
  }
  return gst_system_clock_obtain ();
 }

static void
gst_amba_overlay_src_set_provide_clock (GstAmbaOverlaySrc * src, gboolean provide)
{
  GST_OBJECT_LOCK (src);
  if (provide)
    GST_OBJECT_FLAG_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  else
    GST_OBJECT_FLAG_UNSET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_UNLOCK (src);
}

static gboolean
gst_amba_overlay_src_get_provide_clock (GstAmbaOverlaySrc * src)
{
  gboolean result;

  GST_OBJECT_LOCK (src);
  result = GST_OBJECT_FLAG_IS_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_UNLOCK (src);

  return result;
}

static int parse_roi (const char *custom_properties, amba_rect_t *roi)
{
  if (custom_properties) {
    char **options;
    unsigned int len = 0;

    options = g_strsplit (custom_properties, ".", -1);
    len = g_strv_length (options);

    if (len == 2) {
      roi->x = 0;
      roi->y = 0;
      roi->width = (guint) g_ascii_strtoll (options[0], NULL, 10);
      roi->height = (guint) g_ascii_strtoll (options[1], NULL, 10);
    } else if (len == 4) {
      roi->x = (guint) g_ascii_strtoll (options[0], NULL, 10);
      roi->y = (guint) g_ascii_strtoll (options[1], NULL, 10);
      roi->width = (guint) g_ascii_strtoll (options[2], NULL, 10);
      roi->height = (guint) g_ascii_strtoll (options[3], NULL, 10);
    } else {
      DPRINT_ERROR ("Invalid param, should be roi:offset_x.offset_y.width.height\n");
      return -1;
    }

    g_strfreev (options);

  } else {
    printf("no params\n");
    return -1;
  }

  return 0;
}

static int get_draw_format(const char *type, guint *pixel_size)
{
  if (!strcmp(type, EDRAWFORMATNAME_8BIT)) {
    *pixel_size = 1;
    return AMBA_DRAW_FORMAT_8BIT_CLUT;
  } else if (!strcmp(type, EDRAWFORMATNAME_RGB565)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_RGB565;
  } else if (!strcmp(type, EDRAWFORMATNAME_UYV565)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_UYV565;
  } else if (!strcmp(type, EDRAWFORMATNAME_BGR565)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_BGR565;
  } else if (!strcmp(type, EDRAWFORMATNAME_AYUV4444)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_AYUV4444;
  } else if (!strcmp(type, EDRAWFORMATNAME_RGBA4444)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_RGBA4444;
  } else if (!strcmp(type, EDRAWFORMATNAME_BGRA4444)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_BGRA4444;
  } else if (!strcmp(type, EDRAWFORMATNAME_ABGR4444)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_ABGR4444;
  } else if (!strcmp(type, EDRAWFORMATNAME_ARGB4444)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_ARGB4444;
  } else if (!strcmp(type, EDRAWFORMATNAME_AYUV1555)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_AYUV1555;
  } else if (!strcmp(type, EDRAWFORMATNAME_YUV1555)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_YUV1555;
  } else if (!strcmp(type, EDRAWFORMATNAME_RGBA5551)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_RGBA5551;
  } else if (!strcmp(type, EDRAWFORMATNAME_BGRA5551)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_BGRA5551;
  } else if (!strcmp(type, EDRAWFORMATNAME_ABGR1555)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_ABGR1555;
  } else if (!strcmp(type, EDRAWFORMATNAME_ARGB1555)) {
    *pixel_size = 2;
    return AMBA_DRAW_FORMAT_ARGB1555;
  } else if (!strcmp(type, EDRAWFORMATNAME_AYUV8888)) {
    *pixel_size = 4;
    return AMBA_DRAW_FORMAT_AYUV8888;
  } else if (!strcmp(type, EDRAWFORMATNAME_RGBA8888)) {
    *pixel_size = 4;
    return AMBA_DRAW_FORMAT_RGBA8888;
  } else if (!strcmp(type, EDRAWFORMATNAME_BGRA8888)) {
    *pixel_size = 4;
    return AMBA_DRAW_FORMAT_BGRA8888;
  } else if (!strcmp(type, EDRAWFORMATNAME_ABGR8888)) {
    *pixel_size = 4;
    return AMBA_DRAW_FORMAT_ABGR8888;
  } else if (!strcmp(type, EDRAWFORMATNAME_ARGB8888)) {
    *pixel_size = 4;
    return AMBA_DRAW_FORMAT_ARGB8888;
  } else {
    DPRINT_ERROR("unsupported drawing data type(%s)\n", type);
    return AMBA_DRAW_FORMAT_NONE;
  }

}

static int get_draw_data_type(const char *type)
{
  if (!strcmp(type, EDRAWTYPENAME_STRING)) {
    return AMBA_DRAW_DATA_TYPE_STRING;
  } else if (!strcmp(type, EDRAWTYPENAME_PICTURE)) {
    return AMBA_DRAW_DATA_TYPE_PICTURE;
  } else if (!strcmp(type, EDRAWTYPENAME_TIME)) {
    return AMBA_DRAW_DATA_TYPE_TIME;
  } else {
    DPRINT_ERROR("unsupported drawing data type(%s)\n", type);
    return AMBA_DRAW_DATA_TYPE_NONE;
  }

}

/* STRING uses data.text; TIME uses data.time.text (see amba_draw_time_data). */
static amba_draw_text_box_t *
overlay_area_text_box (amba_overlay_area_param_t *area)
{
  if (area->data.type == AMBA_DRAW_DATA_TYPE_TIME)
    return &area->data.time.text;
  return &area->data.text;
}

static void
overlay_copy_text_style (amba_draw_text_box_t *dst, const amba_draw_text_box_t *src)
{
  dst->spacing = src->spacing;
  dst->font = src->font;
  dst->font_color = src->font_color;
  dst->outline_color = src->outline_color;
  dst->background_color = src->background_color;
  dst->is_cut_off_string = src->is_cut_off_string;
}

static int parse_osd (priv_overlay_src_ctx_t *ctx, const char *custom_properties)
{
  int ret = 0;
  if (custom_properties) {
    char **options;
    unsigned int i = 0, len = 0;
    gint cur_area_id = -1;//cur_stream_id = -1,

    options = g_strsplit (custom_properties, ",", -1);
    len = g_strv_length (options);

    for (i = 0; i < len; ++i) {
      char **option = g_strsplit (options[i], ":", -1);

      if (g_strv_length (option) > 1) {
        g_strstrip (option[0]);
        g_strstrip (option[1]);

        if (g_ascii_strcasecmp (option[0], "area_id") == 0) {
          cur_area_id = (gint) g_ascii_strtoll (option[1], NULL, 10);
          if (cur_area_id >= 0 && cur_area_id < MAX_OVERLAY_AREA_NUM) {
            if (ctx->osd_param.area_num <= cur_area_id) {
              ctx->osd_param.area_num = cur_area_id + 1;
            }
          } else {
            ret = -1;
            GST_ERROR("Invalid area id (%d), should be in the range[0, %d)\n", cur_area_id, MAX_OVERLAY_AREA_NUM);
            break;
          }
        } else if (g_ascii_strcasecmp (option[0], "enable") == 0) {
          ctx->osd_param.area[cur_area_id].attr.enable = !!(guint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].attr_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "rotate") == 0) {
          ctx->osd_param.area[cur_area_id].attr.rotate = (guint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].attr_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "roi") == 0) {
          if (parse_roi(option[1], &ctx->osd_param.area[cur_area_id].attr.rect) < 0) {
            ret = -1;
            GST_ERROR("parse_roi (%s) failed\n", option[1]);
          }
          ctx->osd_param.area[cur_area_id].attr_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "bg_color") == 0) {
          guint bg_color = (guint) g_ascii_strtoull (option[1], NULL, 16);
          ctx->osd_param.area[cur_area_id].attr.bg_color.v = (guchar) ((bg_color >> 24) & 0xff);
          ctx->osd_param.area[cur_area_id].attr.bg_color.u = (guchar) ((bg_color >> 16) & 0xff);
          ctx->osd_param.area[cur_area_id].attr.bg_color.y = (guchar) ((bg_color >> 8) & 0xff);
          ctx->osd_param.area[cur_area_id].attr.bg_color.a = (guchar) (bg_color & 0xff);
          ctx->osd_param.area[cur_area_id].attr_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "buf_num") == 0) {
          ctx->osd_param.area[cur_area_id].attr.buf_num = (gint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].attr_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "keep_n") == 0) {
          ctx->osd_param.area[cur_area_id].attr.keep_n = (guint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].attr_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "update") == 0) {
          ctx->osd_param.area[cur_area_id].attr.update_intervals = (guint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].attr_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "type") == 0) {
          amba_overlay_area_param_t *area = &ctx->osd_param.area[cur_area_id];
          int old_type = area->data.type;
          int new_type = get_draw_data_type (option[1]);

          area->data.type = new_type;
          if (new_type == AMBA_DRAW_DATA_TYPE_TIME &&
              old_type != AMBA_DRAW_DATA_TYPE_TIME) {
            overlay_copy_text_style (&area->data.time.text, &area->data.text);
          } else if (new_type == AMBA_DRAW_DATA_TYPE_STRING &&
              old_type == AMBA_DRAW_DATA_TYPE_TIME) {
            overlay_copy_text_style (&area->data.text, &area->data.time.text);
          }
          area->data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "str") == 0) {
          g_strlcpy(ctx->osd_param.area[cur_area_id].data.text.str, option[1], AMBA_DRAW_STRING_MAX_NUM);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "is_cut_off_str") == 0) {
          amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
          tb->is_cut_off_string = !!(guint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "spacing") == 0) {
          amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
          tb->spacing = (gint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "font_name") == 0) {
          amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
          g_strlcpy (tb->font.ttf_name, option[1], DMAX_FILE_NAME_LENGTH);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "font_size") == 0) {
          amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
          tb->font.width = (gint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "font_outline_w") == 0) {
          amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
          tb->font.outline_width = (gint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "disable_anti_alias") == 0) {
          amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
          tb->font.disable_anti_alias = !!(guint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "font_ver_bold") == 0) {
          amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
          tb->font.ver_bold = (gint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "font_hor_bold") == 0) {
          amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
          tb->font.hor_bold = (gint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "font_italic") == 0) {
          amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
          tb->font.italic = (gint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "font_color") == 0) {
           amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
           guint tmp = (guint) g_ascii_strtoull (option[1], NULL, 16);
           if (tmp < AMBA_DRAW_COLOR_NUM) {
             tb->font_color.id = tmp;
           } else {
             tb->font_color.id = AMBA_DRAW_COLOR_CUSTOM;
             tb->font_color.color.v = (guchar) ((tmp >> 24) & 0xff);
             tb->font_color.color.u = (guchar) ((tmp >> 16) & 0xff);
             tb->font_color.color.y = (guchar) ((tmp >> 8) & 0xff);
             tb->font_color.color.a = (guchar) (tmp & 0xff);
           }
           ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "text_bg_color") == 0) {
           amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
           guint tmp = (guint) g_ascii_strtoull (option[1], NULL, 16);
           tb->background_color.v = (guchar) ((tmp >> 24) & 0xff);
           tb->background_color.u = (guchar) ((tmp >> 16) & 0xff);
           tb->background_color.y = (guchar) ((tmp >> 8) & 0xff);
           tb->background_color.a = (guchar) (tmp & 0xff);
           ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "ol_color") == 0) {
           amba_draw_text_box_t *tb = overlay_area_text_box (&ctx->osd_param.area[cur_area_id]);
           guint tmp = (guint) g_ascii_strtoull (option[1], NULL, 16);
           tb->outline_color.v = (guchar) ((tmp >> 24) & 0xff);
           tb->outline_color.u = (guchar) ((tmp >> 16) & 0xff);
           tb->outline_color.y = (guchar) ((tmp >> 8) & 0xff);
           tb->outline_color.a = (guchar) (tmp & 0xff);
           ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "pre_str") == 0) {
          g_strlcpy(ctx->osd_param.area[cur_area_id].data.time.pre_str, option[1], AMBA_DRAW_STRING_MAX_NUM);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "suf_str") == 0) {
          g_strlcpy(ctx->osd_param.area[cur_area_id].data.time.suf_str, option[1], AMBA_DRAW_STRING_MAX_NUM);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "en_msec") == 0) {
          ctx->osd_param.area[cur_area_id].data.time.en_msec = !!(guint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "format") == 0) {
          ctx->osd_param.area[cur_area_id].data.time.format = (gint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "is_12h") == 0) {
          ctx->osd_param.area[cur_area_id].data.time.is_12h = !!(guint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "ckey") == 0) {
          guint color_key = (guint) g_ascii_strtoull (option[1], NULL, 16);
          //8-bit
          ctx->osd_param.area[cur_area_id].data.pic.colorkey.color.v = (guchar) ((color_key >> 24) & 0xff);
          ctx->osd_param.area[cur_area_id].data.pic.colorkey.color.u = (guchar) ((color_key >> 16) & 0xff);
          ctx->osd_param.area[cur_area_id].data.pic.colorkey.color.y = (guchar) ((color_key >> 8) & 0xff);
          ctx->osd_param.area[cur_area_id].data.pic.colorkey.color.a = (guchar) (color_key & 0xff);
          //32-bit
          ctx->osd_param.area[cur_area_id].data.pic.colorkey.argb.b = (guchar) ((color_key >> 24) & 0xff);
          ctx->osd_param.area[cur_area_id].data.pic.colorkey.argb.g = (guchar) ((color_key >> 16) & 0xff);
          ctx->osd_param.area[cur_area_id].data.pic.colorkey.argb.r = (guchar) ((color_key >> 8) & 0xff);
          ctx->osd_param.area[cur_area_id].data.pic.colorkey.argb.a = (guchar) (color_key & 0xff);
          //16-bit
          ctx->osd_param.area[cur_area_id].data.pic.colorkey.rgb = (gushort) (color_key & 0xffff);

          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "crange") == 0) {
          ctx->osd_param.area[cur_area_id].data.pic.colorkey.range = (gint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "use_bmp_alpha") == 0) {
          ctx->osd_param.area[cur_area_id].data.pic.use_bmp_alpha = !!(guint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "alpha") == 0) {
          ctx->osd_param.area[cur_area_id].data.pic.alpha = (guint) g_ascii_strtoll (option[1], NULL, 10);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "bmp") == 0) {
          g_strlcpy(ctx->osd_param.area[cur_area_id].data.pic.filename, option[1], DMAX_FILE_NAME_LENGTH);
          ctx->osd_param.area[cur_area_id].data_change_flag = 1;
        } else {
          ret = -1;
          GST_ERROR ("Unknown options (%s).", options[i]);
        }
      }
      g_strfreev (option);
    }
    g_strfreev (options);

  } else {
    printf("no params\n");
    return -1;
  }


  return ret;
}

static char * get_osd_info_string(priv_overlay_src_ctx_t * filter)
{
  int j = 0;
  GstStructure * s = NULL;
  gchar *osd_info_list = NULL;
  char name[128] = {0};

  GValue v_i = G_VALUE_INIT;

  g_value_init (&v_i, G_TYPE_INT);

  snprintf(name, sizeof(name) - 1, "overlay_info");
  s = gst_structure_new_empty (name);

  g_value_set_int (&v_i, filter->osd_param.area_num);
  gst_structure_set_value (s, "area_num", &v_i);

  for (j = 0; j < filter->osd_param.area_num; j++) {
    g_value_set_int (&v_i, j);
    gst_structure_set_value (s, "area_id", &v_i);

    g_value_set_int (&v_i, filter->osd_param.area[j].attr.enable);
    gst_structure_set_value (s, "enable", &v_i);

    g_value_set_int (&v_i, filter->osd_param.area[j].attr.rect.x);
    gst_structure_set_value (s, "xstart", &v_i);

    g_value_set_int (&v_i, filter->osd_param.area[j].attr.rect.y);
    gst_structure_set_value (s, "ystart", &v_i);

    g_value_set_int (&v_i, filter->osd_param.area[j].attr.rect.width);
    gst_structure_set_value (s, "w", &v_i);

    g_value_set_int (&v_i, filter->osd_param.area[j].attr.rect.height);
    gst_structure_set_value (s, "h", &v_i);

    g_value_set_int (&v_i, filter->osd_param.area[j].attr.buf_num);
    gst_structure_set_value (s, "buf_num", &v_i);

    g_value_set_int (&v_i, filter->osd_param.area[j].data.type);
    gst_structure_set_value (s, "draw_type", &v_i);

  }

  if (s) {
    osd_info_list = gst_structure_to_string(s);
    if (osd_info_list == NULL) {
      DPRINT_ERROR("convert osd configure information to string failed\n");
      goto osd_end;
    }

    gst_structure_free (s);
    s = NULL;

  }

osd_end:

  if (s) {
    gst_structure_free (s);
    s = NULL;
  }

  return osd_info_list;
}


static void
gst_amba_overlay_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaOverlaySrc * thiz = GST_AMBAOVERLAYSRC (object);
  priv_overlay_src_ctx_t *filter = (priv_overlay_src_ctx_t *) thiz->priv_ctx;

  switch (prop_id) {
    case PROP_DRAW_FORMAT:
      strncpy(filter->draw_fmt_name, g_value_get_string (value), 127);
      filter->draw_format = get_draw_format(filter->draw_fmt_name, &filter->draw_pix_size);
      if (filter->draw_format < 0) {
        DPRINT_ERROR("get_draw_format (%s) failed\n", g_value_get_string (value));
        return;
      }
      break;
    case PROP_PROVIDE_CLOCK:
      filter->give_clock = g_value_get_boolean (value);
      gst_amba_overlay_src_set_provide_clock (thiz, filter->give_clock);
      break;
    case PROP_REFRESH:
      filter->refresh = !!g_value_get_uchar (value);
      filter->osd_param.area_num = 0;
      break;
    case PROP_OSD:
      if (parse_osd(filter, g_value_get_string (value)) < 0) {
        DPRINT_ERROR("parse_osd (%s) failed\n", g_value_get_string (value));
        return;
      }
      break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }break;
  }

}

static void
gst_amba_overlay_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaOverlaySrc *thiz = GST_AMBAOVERLAYSRC (object);
  priv_overlay_src_ctx_t *filter = (priv_overlay_src_ctx_t *) thiz->priv_ctx;

  switch (prop_id) {
    case PROP_DRAW_FORMAT:
      g_value_set_string (value, filter->draw_fmt_name);
      break;
    case PROP_PROVIDE_CLOCK:
      g_value_set_boolean (value, gst_amba_overlay_src_get_provide_clock (thiz));
      break;
    case PROP_REFRESH:
      g_value_set_uchar (value, filter->refresh);
      break;
    case PROP_OSD:{
      if (filter->osd_info) {
        g_free(filter->osd_info);
        filter->osd_info = NULL;
      }
      filter->osd_info = get_osd_info_string(filter);
      if (filter->osd_info == NULL) {
        GST_ERROR("get_osd_info_string failed.");
        return;
      }
      g_value_set_string (value, filter->osd_info);
    } break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }break;
  }
}

/* GstElement vmethod implementations */
static GstFlowReturn
gst_amba_overlay_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf)
{
  GstAmbaOverlaySrc * thiz = GST_AMBAOVERLAYSRC (psrc);
  priv_overlay_src_ctx_t *filter = (priv_overlay_src_ctx_t *) thiz->priv_ctx;

  GstFlowReturn flow_ret = GST_FLOW_OK;

  GstBuffer * p_out_buf = NULL;

  amba_overlay_area_param_t *area = NULL;

  GstMemory *mem0 = NULL, *mem = NULL;
  GstMapInfo minfo0 = {0};
  GstMapInfo minfo = {0};
  unsigned char *data_buf = NULL;
  amba_draw_clut_t *m_clut;
  osd_header_param_t *header = NULL;
  gsize size = 0, area_size;

  while (1) {

    p_out_buf = gst_buffer_new();
    // mem 0 for total areas info
    size = ROUND_UP(sizeof(osd_header_param_t), OSD_BUF_PITCH_ALIGN);
    mem0 = gst_allocator_alloc (NULL, size, NULL);
    if (!mem0) {
      GST_ERROR_OBJECT (thiz, "Failed to allocate memory");
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    gst_buffer_append_memory (p_out_buf, mem0);

    if (!gst_memory_map (mem0, &minfo0, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (thiz, "Failed to map memory");
      gst_memory_unref (mem0);
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    memset(minfo0.data, 0x0, size);

    header = (osd_header_param_t *) minfo0.data;
    header->draw_format = (unsigned char) filter->draw_format;
    header->refresh = filter->refresh;

    for (int j = 0; j < filter->osd_param.area_num; j++, header->area_num++) {
      area = &filter->osd_param.area[j];
      header->attr_change_flag[j] = area->attr_change_flag;
      header->data_change_flag[j] = area->data_change_flag;

      if (area->attr_change_flag) {
        filter->reset_bg[j] = 0;
      }
      if (area->attr.keep_n == 0) {
        filter->reset_bg[j] = 1;
        area->data_change_flag = 1;
      }

      if (area->attr.update_intervals && filter->frame_count[j] == 0) {
        area->data_change_flag = 1;
      }

      if (filter->refresh || area->attr_change_flag || area->data_change_flag) {

        if (area->attr.rect.x < 0) {
          area->attr.rect.x = 0;
        }
        if (area->attr.rect.y < 0) {
          area->attr.rect.y = 0;
        }

        if (area->attr.rect.width <= 0 || area->attr.rect.height <= 0) {
          GST_ERROR_OBJECT (thiz, "Invalid width %d or height %d for area %d.",
              area->attr.rect.width, area->attr.rect.height, j);
          gst_memory_unmap (mem0, &minfo0);
          gst_memory_unref (mem0);
          return GST_FLOW_ERROR;
        }

        area->attr.rect.width = ROUND_DOWN(area->attr.rect.width, OVERLAY_WIDTH_ALIGN);
        area->attr.rect.height = ROUND_DOWN(area->attr.rect.height, OVERLAY_HEIGHT_ALIGN);
        area->attr.rect.pitch = ROUND_UP(ROUND_UP(area->attr.rect.width, OSD_BUF_WIDTH_ALIGN) * filter->draw_pix_size, OSD_BUF_PITCH_ALIGN);
        area->data.rect.pitch = area->attr.rect.pitch;
        area_size = area->attr.rect.height * area->attr.rect.pitch;
        if (!area->attr.buf_num) {
          area->attr.buf_num = 1;
        }

        memcpy(&header->attr[j], &area->attr, sizeof(amba_overlay_area_attr_t));

        size = OVERLAY_CLUT_SIZE + area_size;

        mem = gst_allocator_alloc (NULL, size, NULL);
        if (!mem) {
          GST_ERROR_OBJECT (thiz, "Failed to allocate memory");
          gst_memory_unmap (mem0, &minfo0);
          gst_memory_unref (mem0);
          return GST_FLOW_ERROR;
        }

        if (!gst_memory_map (mem, &minfo, GST_MAP_WRITE)) {
          GST_ERROR_OBJECT (thiz, "Failed to map memory");
          gst_memory_unref (mem);
          gst_memory_unmap (mem0, &minfo0);
          gst_memory_unref (mem0);
          return GST_FLOW_ERROR;
        }

        m_clut = (amba_draw_clut_t *) (minfo.data);
        data_buf = minfo.data + OVERLAY_CLUT_SIZE;
        memset(m_clut, 0, OVERLAY_CLUT_SIZE);
        m_clut[AMBA_DRAW_CLUT_ENTRY_BACKGROUND] = area->attr.bg_color;
        memset(data_buf, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, area_size);

        if (area->attr.enable && filter->reset_bg[j] == 0) {
          switch(area->data.type) {
            case AMBA_DRAW_DATA_TYPE_PICTURE:
              if (amba_draw_pic_data(area, m_clut, data_buf, &filter->bitmap, filter->draw_format) < 0) {
                GST_ERROR_OBJECT(thiz, "amba_draw_pic_data failed");
                gst_memory_unmap (mem, &minfo);
                gst_memory_unmap (mem0, &minfo0);
                gst_memory_unref (mem);
                gst_memory_unref (mem0);
                return GST_FLOW_ERROR;
              }
              break;
            case AMBA_DRAW_DATA_TYPE_STRING:
              if (amba_draw_str_data(area, m_clut, data_buf, &filter->bitmap, filter->draw_format, filter->osd_param.stream_rotate) < 0) {
                GST_ERROR_OBJECT(thiz, "amba_draw_str_data failed");
                gst_memory_unmap (mem, &minfo);
                gst_memory_unmap (mem0, &minfo0);
                gst_memory_unref (mem);
                gst_memory_unref (mem0);
                return GST_FLOW_ERROR;
              }
              break;
            case AMBA_DRAW_DATA_TYPE_TIME:
              if (amba_draw_time_data(area, m_clut, data_buf, &filter->bitmap, filter->draw_format, filter->osd_param.stream_rotate) < 0) {
                GST_ERROR_OBJECT(thiz, "amba_draw_time_data failed");
                gst_memory_unmap (mem, &minfo);
                gst_memory_unmap (mem0, &minfo0);
                gst_memory_unref (mem);
                gst_memory_unref (mem0);
                return GST_FLOW_ERROR;
              }
              break;
            default: {
              GST_ERROR_OBJECT(thiz, "Invalid drawing type: %d", area->data.type);
              gst_memory_unmap (mem, &minfo);
              gst_memory_unmap (mem0, &minfo0);
              gst_memory_unref (mem);
              gst_memory_unref (mem0);
              return GST_FLOW_ERROR;
            }
          }
        }

        header->data_change_flag[j] = 1;

        area->attr_change_flag = 0;
        area->data_change_flag = 0;

        gst_memory_unmap (mem, &minfo);
        gst_buffer_append_memory (p_out_buf, mem);

      }

      filter->frame_count[j]++;
      if (filter->frame_count[j] >= area->attr.update_intervals) {
        filter->frame_count[j] = 0;
      } else {
        usleep(DEFAULT_SLEEP_TIME_US);
      }

      if (area->attr.keep_n >= 0) {
        usleep(DEFAULT_SLEEP_TIME_US);
        area->attr.keep_n--;
      }
    }

    filter->refresh = 0;

    gst_memory_unmap (mem0, &minfo0);

    *outbuf = p_out_buf;

    flow_ret = GST_FLOW_OK;
    break;
  }

  return flow_ret;
}

static gboolean
gst_amba_overlay_src_start (GstBaseSrc * bsrc)
{
#if 0
  GstAmbaOverlaySrc * thiz = GST_AMBAOVERLAYSRC (bsrc);
  priv_overlay_src_ctx_t *filter = (priv_overlay_src_ctx_t *) thiz->priv_ctx;
  iav_ctx_t * iav_ctx = filter->iav_ctx;
  enc_config_t config;
  int ret = 0;

  if (!iav_ctx->iav_fd_opened) {
    GST_ERROR("iav not opened\n");
    return FALSE;
  }
  memset(&config, 0x0, sizeof(enc_config_t));
  ret = get_enc_info_config (iav_ctx->iav_fd, &config, DEFAULT_MAX_STREAM_ID, DEFAULT_MAX_VIN_ID);
  if (ret < 0) {
    GST_ERROR("get encoding information failed\n");
    return FALSE;
  }

  for (int i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    filter->fps[i] = config.stream_fps[i];
    filter->fps_d[i] = config.framerate_factor[i][1];
    filter->fps_n[i] = filter->fps[i] * filter->fps_d[i];
      memset(&stream_cfg, 0, sizeof(stream_cfg));
      stream_cfg.id = i;
      stream_cfg.cid = IAV_STMCFG_FORMAT;
      if (ioctl(filter->iav_ctx->iav_fd, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg) < 0) {
        perror("IAV_IOC_GET_STREAM_CONFIG");
        return FALSE;
      }

      stream_format = &stream_cfg.arg.format;
      filter->enc_src_id[i] = stream_format->enc_src_id;

      if (filter->osd_param[i].stream_rotate == AMBA_DRAW_AUTO_ROTATE) {
        filter->osd_param[i].stream_rotate = AMBA_DRAW_NO_ROTATE_FLIP;
        filter->osd_param[i].stream_rotate |= (stream_format->rotate_cw ? AMBA_DRAW_ROTATE_90 : 0);
        filter->osd_param[i].stream_rotate |= (stream_format->hflip ? AMBA_DRAW_HORIZONTAL_FLIP : 0);
        filter->osd_param[i].stream_rotate |= (stream_format->vflip ? AMBA_DRAW_VERTICAL_FLIP : 0);
        if (!is_valid_rotate(filter->osd_param[i].stream_rotate)) {
          GST_WARNING_OBJECT(filter, "Stream %c Unknown rotate type. OSD is "
              "consistent with VIN orientation.\n", i);
          filter->osd_param[i].stream_rotate = AMBA_DRAW_NO_ROTATE_FLIP;
        }
      }
  }
#endif
  DUNUSED(bsrc);
  return TRUE;
}

static gboolean
gst_amba_overlay_src_stop (GstBaseSrc * bsrc)
{
  GstAmbaOverlaySrc * thiz = GST_AMBAOVERLAYSRC (bsrc);
  priv_overlay_src_ctx_t *filter = (priv_overlay_src_ctx_t *) thiz->priv_ctx;

  for (int j = 0; j < MAX_OVERLAY_AREA_NUM; j++) {
    if (filter->osd_param.area[j].data.text.m_bitmap.m_font_lib_init) {
      if (deinit_textinsert_lib(&filter->osd_param.area[j].data.text.m_bitmap) < 0) {
        DPRINT_ERROR("Failed to init text insert library or update font_attribute.\n");
        return FALSE;
      }
    }
  }

  return TRUE;
}

static GstStateChangeReturn
gst_amba_overlay_src_change_state (GstElement * element, GstStateChange transition)
{
  GstAmbaOverlaySrc * thiz = GST_AMBAOVERLAYSRC (element);
  GstStateChangeReturn ret= GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_DEBUG_OBJECT(element, "GST_STATE_CHANGE_NULL_TO_READY\n");
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT(element, "GST_STATE_CHANGE_READY_TO_PAUSED\n");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING: {
      if (thiz->priv_ctx->give_clock) {
        gst_element_post_message (element,
            gst_message_new_clock_provide (GST_OBJECT_CAST (element),
                gst_system_clock_obtain (), TRUE));
      }
      GST_DEBUG_OBJECT(element, "GST_STATE_CHANGE_PAUSED_TO_PLAYING\n");

    } break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      if (thiz->priv_ctx->give_clock) {
        gst_element_post_message (element,
            gst_message_new_clock_lost (GST_OBJECT_CAST (element),
                gst_system_clock_obtain ()));
      }
      GST_DEBUG_OBJECT(element, "GST_STATE_CHANGE_PLAYING_TO_PAUSED\n");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT(element, "GST_STATE_CHANGE_PAUSED_TO_READY\n");
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_DEBUG_OBJECT(element, "GST_STATE_CHANGE_READY_TO_NULL\n");
      break;
    default:
      break;
  }
  return ret;

}

static gboolean
gst_amba_overlay_src_send_event (GstElement * element, GstEvent * event)
{
  GstAmbaOverlaySrc * src = GST_AMBAOVERLAYSRC (element);
  gboolean ret = TRUE;

  const GstStructure *s;
  const gchar *tstr;
  gchar *sstr;

  GST_OBJECT_LOCK (src);

  tstr = gst_event_type_get_name (GST_EVENT_TYPE (event));

  if ((s = gst_event_get_structure (event)))
    sstr = gst_structure_to_string (s);
  else
    sstr = g_strdup ("");
  GST_DEBUG_OBJECT (src, "send event   ******* (%s:%s) E (type: %s (%d), %s) %p\n",
      GST_DEBUG_PAD_NAME (GST_BASE_SRC_CAST (src)->srcpad),
      tstr, GST_EVENT_TYPE (event), sstr, event);
  g_free (sstr);
  GST_OBJECT_UNLOCK (src);

  ret = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);

  GST_DEBUG_OBJECT (src, "send event success  *******\n");

  return ret;
}

