--[[
History:
  2018-3-9 - [Zhaoyang Chen] created file

Copyright (c) 2018 Ambarella International LP

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
	pre_dec_type = 0,
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


_resource_config_ = {
	version = 1,
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
			enc_dummy_latency = 2,
			back_pressure_enable = 1,
			cached_items = 5,
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
}

return _resource_config_

