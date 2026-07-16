--[[
History:
  2025-11-3 - [pxduan] created file

Copyright (c) 2023 Ambarella International LP.

This file and its contents ("Software") are protected by intellectual
property rights including, without limitation, U.S. and/or foreign
copyrights. This Software is also the confidential and proprietary
information of Ambarella International LP and its licensors. You may not use, reproduce,
disclose, distribute, modify, or otherwise prepare derivative works of this
Software or any portion thereof except pursuant to a signed license agreement
or nondisclosure agreement with Ambarella International LP or its authorized affiliates.
In the absence of such an agreement, you agree to promptly notify and return
this Software to Ambarella International LP.

This file includes sample code and is only for internal testing and evaluation.  If you
distribute this sample code (whether in source, object, or binary code form), it will be
without any warranty or indemnity protection from Ambarella International LP or its affiliates.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF NON-INFRINGEMENT,
MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL AMBARELLA INTERNATIONAL LP OR ITS AFFILIATES BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; COMPUTER FAILURE OR MALFUNCTION; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

--]]
virt_mode_cfg_0 = {
	width = 3840,
	height = 2160,
	bits = 12,
	max_fps = 60,
	default_fps = 30,
	hdr_mode = "linear",
	video_type = "yuv_656", -- options: "yuv_601", "yuv_656", "rgb_601", "rgb_656", "rgb_raw", "yuv_bt1120", "rgb_bt1120"
	sensor_id = 0x3014,
	agc_db_step = 0x00180000,
}

vsrc_0 = {
	vsrc_id = 0,
	mode = "3840x2160",
	hdr_mode = "linear", -- options: "linear", "2x" or "3x"
	fps = 30,
	bits= 0,
	virt_mode_cfg_enable = 1,
	virt_mode_cfg = virt_mode_cfg_0,
}

chan_0 = {
	id = 0,
	vsrc = vsrc_0,
	vsrc_ctx = 0,
	img_stats_src_chan = "chan_0",
	sensor_ctrl = 1,
	max_padding_width = 0,
	idsp_fps = 0,
	lens_warp = 0,
	max_main_input_width = 0, -- 0: VIN raw width
	mctf_cmpr = 1,
	c2y_burst_tile = 1,
	extra_downscale = 0,
	high_perf_enable = 0,
	raw_capture = 1,
	pre_dec_enable = 1,
	pre_dec_type = 3,
	main = {
		max_output = {0, 0}, -- output width
		input      = {0, 0, 0, 0}, -- full VIN
		output     = {0, 0, 3840, 2160},
	},
	second = {
		max_output = {0, 0}, -- output width
		input      = {0, 0, 0, 0}, -- full main
		output     = {0, 0, 720, 480},
	},
	third = {
		max_output = {0, 0}, -- output width
		input      = {0, 0, 0, 0}, -- full main
		output     = {0, 0, 1920, 1080},
	},
	fourth = {
		max_output = {0, 0},
		input      = {0, 0, 0, 0},
		output     = {0, 0, 1920, 1080},
	},
	fifth = {
		max_output = {0, 0},
		input      = {0, 0, 0, 0}, -- full main
		output     = {0, 0, 0, 0},
	},
	pyramid = {
		input_buf_id = 3,	-- 0: Main, 1: Second, 2: Third, 3: Fourth, 4: Fifth
		scale_type = 0,		-- 0: 1/sqrt(2); 1: 1/2; 2: Arbitrary size
		buf_addr = 0x0,
		buf_size = 0x0,
		manual_feed = 0,
		item_num = 0,
		layer_map = 0x2a,
		layers = {
			{
				crop_win = {0, 0, 0 ,0},
			},
			{
				crop_win = {0, 0, 0 ,0},
			},
			{
				crop_win = {0, 0, 0 ,0},
			},
			{
				crop_win = {0, 0, 0 ,0},
			},
			{
				crop_win = {0, 0, 0 ,0},
			},
			{
				crop_win = {0, 0, 0 ,0},
			},
			{
				crop_win = {0, 0, 0 ,0},
			},
		},
	},
}

stream_0 = {
	id = 0,
	max_size = {3840, 2160},
	max_M = 1,
	fast_seek_enable = 0,
	two_ref_enable = 0,
	max_svct_layers_minus_1 = 0,
	max_num_minus_1_ltrs = 0,
	codec_enable = 0, -- 0: H264/H265/MJPEG; 1: H265/MJPEG; 2: H264/MJPEG; 3: MJPEG
}

vout_0 = {
	id = 0,
	status = "on",					--selection: 0:disable, 1:on, 2:off, 3:reset
							--disable: do not use this vout controller
							--on: use this vout controller and enable video display
							--off: use this vout controller and disable video display
							--reset: reset this vout controller
	mode = "1080p",
	input_yuv422 = "yes",				----selection: yes, no
	video_output_window = {0, 0, 1920, 1080},	--{offsetx, offsety, output_width, output_heigh}

	type = "mipi_dsi",      			--selection: digital, mipi_dsi, fpd_link

	vout_from_image = "disable",
	default_img_format = "yuv422",			--selection: yuv422, yuv420

	video_rotate = "no",    			--selection: 0:no, 1:yes
	video_flip_mode = "no", 			--selection: 0:no, 1:hv, 2:h, 3:v

	own_mixer = "yes",      			--selection: 0:no, 1:yes

	osd_output_window = {0, 0, 0, 0},       	--{offsetx, offsety, output_width, output_heigh}
	osd_flip_mode = "no",   			--selection: 0:no, 1:hv, 2:h, 3:v
	osd_rescaler = "disable", 			--selection: 0:disable, 1:enable
	osd_rescaler_output_window = {0, 0, 0, 0},	--{offsetx, offsety, output_width, output_heigh}
	osd_transparent_color_enable = "enable",	--selection: 0:disable, 1:enable
	default_yuv2rgb_csc = 0,			--selection: 0: BT.601 full range, 1: BT.601 limited range, 2: BT.709 limited range
							--Note: please check current IDSP rgb2yuv CSC before setting VOUT yuv2rgb CSC
							--VOUT yuv2rgb CSC should follow IDSP rgb2yuv CSC.
							--when IDSP rgb2yuv CSC follows BT.601, VOUT yuv2rgb CSC should follow BT.601 as well
							--when IDSP rgb2yuv CSC follows BT.709, VOUT yuv2rgb CSC should follow BT.709 as well
	color_range = {0, 0, 0, 0, 0, 0},       	--{red_low, red_high, green_low, green_high, blue_low, blue_high}
							--maximum range for csi/dsi: {0, 0xff, 0, 0xff, 0, 0xff}
	csc = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},     --{a0, a1, a2, a3, a4, a5, a6, a7, a8, b0, b1, b2}
							--Color Space Conversion(CSC) matrix
	max_osd_bit_depth = 32,				--selection: 0: auto, 8: 8bits, 16: 16bits, 32: 32bits
}

_resource_config_ = {
	version = 3,
	log_level = 0, -- 0: error; 1: warning; 2: info; 3: debug
	channels = {
		chan_0,
	},
	canvas = {
		{
			type = "encode",
			size = {0, 0}, -- min size to contain source buffers
			source = {"chan_0.main",},
			extra_dram_buf = 0,
		},
		{
			type = "encode",
			size = {0, 0}, -- min size to contain source buffers
			source = {"chan_0.second",},
			extra_dram_buf = 0,
		},
		{
			type = "prev",
			size = {0, 0}, -- min size to contain source buffers
			source = {"chan_0.third",},
			vout_id = 0,
			vout_YUV422 = 0,
			extra_dram_buf = 0,
		},
	},
	streams = {
		stream_0,
	},
	vouts = {
		vout_0,	
	},

	system_setup_cfg = {
		encode_mode = 0,
		vsync_detection_disable = 0,
		vsync_loss_dummy_frame_enable = 1,
		raw_capture = 1,
	},
	system_debug_cfg = {
		debug_vsync_loss_timeout = 2000,
	},
	
}

return _resource_config_
