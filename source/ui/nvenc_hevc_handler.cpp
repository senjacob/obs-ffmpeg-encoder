// FFMPEG Video Encoder Integration for OBS Studio
// Copyright (c) 2019 Michael Fabian Dirks <info@xaymar.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "nvenc_hevc_handler.hpp"
#include "codecs/hevc.hpp"
#include "nvenc_shared.hpp"
#include "plugin.hpp"
#include "strings.hpp"
#include "utility.hpp"

extern "C" {
#include <obs-module.h>
#pragma warning(push)
#pragma warning(disable : 4244)
#include <libavutil/opt.h>
#pragma warning(pop)
}

/* Missing Options:

Seem to be covered by initQP_* instead.
- [obs-ffmpeg-encoder]   Option 'cq' with help 'Set target quality level (0 to 51, 0 means automatic) for constant quality mode in VBR rate control' of type 'Float' with default value '0.000000', minimum '0.000000' and maximum '51.000000'.
- [obs-ffmpeg-encoder]   Option 'qp' with help 'Constant quantization parameter rate control method' of type 'Int' with default value '-1', minimum '-1.000000' and maximum '51.000000'.

Not sure what there are useful for.
[obs-ffmpeg-encoder]   Option 'aud' with help 'Use access unit delimiters' of type 'Bool' with default value 'false', minimum '0.000000' and maximum '1.000000'.
[obs-ffmpeg-encoder]   Option 'surfaces' with help 'Number of concurrent surfaces' of type 'Int' with default value '0', minimum '0.000000' and maximum '64.000000'.
[obs-ffmpeg-encoder]   Option 'delay' with help 'Delay frame output by the given amount of frames' of type 'Int' with default value '2147483647', minimum '0.000000' and maximum '2147483647.000000'.

Should probably add this.
[obs-ffmpeg-encoder]   Option 'gpu' with unit (gpu) with help 'Selects which NVENC capable GPU to use. First GPU is 0, second is 1, and so on.' of type 'Int' with default value '-1', minimum '-2.000000' and maximum '2147483647.000000'.
[obs-ffmpeg-encoder]   [gpu] Constant 'any' and help text 'Pick the first device available' with value '-1'.
[obs-ffmpeg-encoder]   [gpu] Constant 'list' and help text 'List the available devices' with value '-2'.

Useless except for strict_gop maybe?
[obs-ffmpeg-encoder]   Option 'forced-idr' with help 'If forcing keyframes, force them as IDR frames.' of type 'Bool' with default value 'false', minimum '-1.000000' and maximum '1.000000'.
[obs-ffmpeg-encoder]   Option 'strict_gop' with help 'Set 1 to minimize GOP-to-GOP rate fluctuations' of type 'Bool' with default value 'false', minimum '0.000000' and maximum '1.000000'.
[obs-ffmpeg-encoder]   Option 'bluray-compat' with help 'Bluray compatibility workarounds' of type 'Bool' with default value 'false', minimum '0.000000' and maximum '1.000000'.
*/

using namespace obsffmpeg::codecs::hevc;

std::map<profile, std::string> profiles{
    {profile::MAIN, "main"},
    {profile::MAIN10, "main10"},
    {profile::RANGE_EXTENDED, "rext"},
};

std::map<tier, std::string> tiers{
    {tier::MAIN, "main"},
    {tier::HIGH, "high"},
};

std::map<level, std::string> levels{
    {level::L1_0, "1.0"}, {level::L2_0, "2.0"}, {level::L2_1, "2.1"}, {level::L3_0, "3.0"}, {level::L3_1, "3.1"},
    {level::L4_0, "4.0"}, {level::L4_1, "4.1"}, {level::L5_0, "5.0"}, {level::L5_1, "5.1"}, {level::L5_2, "5.2"},
    {level::L6_0, "6.0"}, {level::L6_1, "6.1"}, {level::L6_2, "6.2"},
};

INITIALIZER(nvenc_hevc_handler_init)
{
	obsffmpeg::initializers.push_back([]() {
		obsffmpeg::register_codec_handler("hevc_nvenc", std::make_shared<obsffmpeg::ui::nvenc_hevc_handler>());
	});
};

void obsffmpeg::ui::nvenc_hevc_handler::override_visible_name(const AVCodec*, std::string& name)
{
	name = "H.265/HEVC Encoder (NVidia NVENC)";
}

void obsffmpeg::ui::nvenc_hevc_handler::override_lag_in_frames(size_t& lag, obs_data_t* settings, const AVCodec* codec,
                                                               AVCodecContext* context)
{
	nvenc::override_lag_in_frames(lag, settings, codec, context);
}

void obsffmpeg::ui::nvenc_hevc_handler::get_defaults(obs_data_t* settings, const AVCodec* codec,
                                                     AVCodecContext* context, bool)
{
	nvenc::get_defaults(settings, codec, context);

	obs_data_set_default_int(settings, P_HEVC_PROFILE, static_cast<int64_t>(codecs::hevc::profile::MAIN));
	obs_data_set_default_int(settings, P_HEVC_TIER, static_cast<int64_t>(codecs::hevc::profile::MAIN));
	obs_data_set_default_int(settings, P_HEVC_LEVEL, static_cast<int64_t>(codecs::hevc::level::UNKNOWN));
}

void obsffmpeg::ui::nvenc_hevc_handler::get_properties(obs_properties_t* props, const AVCodec* codec,
                                                       AVCodecContext* context, bool)
{
	if (!context) {
		this->get_encoder_properties(props, codec);
	} else {
		this->get_runtime_properties(props, codec, context);
	}
}

void obsffmpeg::ui::nvenc_hevc_handler::update(obs_data_t* settings, const AVCodec* codec, AVCodecContext* context)
{
	nvenc::update(settings, codec, context);

	{ // HEVC Options
		auto found = profiles.find(static_cast<profile>(obs_data_get_int(settings, P_HEVC_PROFILE)));
		if (found != profiles.end()) {
			av_opt_set(context->priv_data, "profile", found->second.c_str(), 0);
		}
	}
	{
		auto found = tiers.find(static_cast<tier>(obs_data_get_int(settings, P_HEVC_TIER)));
		if (found != tiers.end()) {
			av_opt_set(context->priv_data, "tier", found->second.c_str(), 0);
		}
	}
	{
		auto found = levels.find(static_cast<level>(obs_data_get_int(settings, P_HEVC_LEVEL)));
		if (found != levels.end()) {
			av_opt_set(context->priv_data, "level", found->second.c_str(), 0);
		} else {
			av_opt_set(context->priv_data, "level", "auto", 0);
		}
	}
}

void obsffmpeg::ui::nvenc_hevc_handler::log_options(obs_data_t* settings, const AVCodec* codec, AVCodecContext* context)
{
	nvenc::log_options(settings, codec, context);

	profile cfg_profile = static_cast<profile>(obs_data_get_int(settings, P_HEVC_PROFILE));
	tier    cfg_tier    = static_cast<tier>(obs_data_get_int(settings, P_HEVC_TIER));
	level   cfg_level   = static_cast<level>(obs_data_get_int(settings, P_HEVC_LEVEL));

	auto found1 = profiles.find(cfg_profile);
	if (found1 != profiles.end())
		PLOG_INFO("[%s]   H.265 Profile: %s", codec->name, found1->second.c_str());

	auto found2 = levels.find(cfg_level);
	if (found2 != levels.end())
		PLOG_INFO("[%s]   H.265 Level: %s", codec->name, found2->second.c_str());

	auto found3 = tiers.find(cfg_tier);
	if (found3 != tiers.end())
		PLOG_INFO("[%s]   H.265 Tier: %s", codec->name, found3->second.c_str());
}

void obsffmpeg::ui::nvenc_hevc_handler::get_encoder_properties(obs_properties_t* props, const AVCodec* codec)
{
	nvenc::get_properties_pre(props, codec);

	{
		obs_properties_t* grp = props;
		if (!obsffmpeg::are_property_groups_broken()) {
			grp = obs_properties_create();
			obs_properties_add_group(props, P_HEVC, TRANSLATE(P_HEVC), OBS_GROUP_NORMAL, grp);
		}

		{
			auto p = obs_properties_add_list(grp, P_HEVC_PROFILE, TRANSLATE(P_HEVC_PROFILE),
			                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
			obs_property_set_long_description(p, TRANSLATE(DESC(P_HEVC_PROFILE)));
			obs_property_list_add_int(p, TRANSLATE(S_STATE_DEFAULT),
			                          static_cast<int64_t>(codecs::hevc::profile::UNKNOWN));
			for (auto const kv : profiles) {
				std::string trans = std::string(P_HEVC_PROFILE) + "." + kv.second;
				obs_property_list_add_int(p, TRANSLATE(trans.c_str()), static_cast<int64_t>(kv.first));
			}
		}
		{
			auto p = obs_properties_add_list(grp, P_HEVC_TIER, TRANSLATE(P_HEVC_TIER), OBS_COMBO_TYPE_LIST,
			                                 OBS_COMBO_FORMAT_INT);
			obs_property_set_long_description(p, TRANSLATE(DESC(P_HEVC_TIER)));
			obs_property_list_add_int(p, TRANSLATE(S_STATE_DEFAULT),
			                          static_cast<int64_t>(codecs::hevc::tier::UNKNOWN));
			for (auto const kv : tiers) {
				std::string trans = std::string(P_HEVC_TIER) + "." + kv.second;
				obs_property_list_add_int(p, TRANSLATE(trans.c_str()), static_cast<int64_t>(kv.first));
			}
		}
		{
			auto p = obs_properties_add_list(grp, P_HEVC_LEVEL, TRANSLATE(P_HEVC_LEVEL),
			                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
			obs_property_set_long_description(p, TRANSLATE(DESC(P_HEVC_LEVEL)));
			obs_property_list_add_int(p, TRANSLATE(S_STATE_AUTOMATIC),
			                          static_cast<int64_t>(codecs::hevc::level::UNKNOWN));
			for (auto const kv : levels) {
				obs_property_list_add_int(p, kv.second.c_str(), static_cast<int64_t>(kv.first));
			}
		}
	}

	nvenc::get_properties_post(props, codec);
}

void obsffmpeg::ui::nvenc_hevc_handler::get_runtime_properties(obs_properties_t* props, const AVCodec* codec,
                                                               AVCodecContext* context)
{
	nvenc::get_runtime_properties(props, codec, context);
}
