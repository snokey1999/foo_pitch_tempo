#include "stdafx.h"

// Forward declaration from dsp_openal.cpp
bool OpenAL_Init(HMODULE hComponent);
void OpenAL_Uninit();

DECLARE_COMPONENT_VERSION(
	u8"时间魔术师 (Time Wizard)",
	"1.3.0",
	u8"一个基于RubberBand引擎的高质量变速变调与loop循环组件\n"
	"Author: snokey\n"
	"1.3.0 替换底层变速变调算法为 RubberBand；新增基于色度图分析的改进型调性检测与主菜单集成；新增 OpenAL 3D 空间音频 DSP。\n"
	"1.2.2 增强屏幕阅读器与可访问性能力。"
);

VALIDATE_COMPONENT_FILENAME("foo_pitch_tempo.dll");



#include "tolk_wrapper.h"

class init_tolk : public initquit {
public:
	void on_init() override {
		TolkWrapper_Init(core_api::get_my_instance());
	}
	void on_quit() override {
		TolkWrapper_Uninit();
	}
};

static initquit_factory_t<init_tolk> g_init_tolk;

class init_openal : public initquit {
public:
	void on_init() override {
		OpenAL_Init(core_api::get_my_instance());
	}
	void on_quit() override {
		OpenAL_Uninit();
	}
};

static initquit_factory_t<init_openal> g_init_openal;
