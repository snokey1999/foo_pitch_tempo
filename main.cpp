#include "stdafx.h"

DECLARE_COMPONENT_VERSION(
	u8"时间魔术师 (Time Wizard)",
	"1.2.2",
	u8"一个基于bass引擎的变速变调与loop循环组件\n"
	"Author: snokey\n"
	"1.2.2 增强屏幕阅读器与可访问性能力。"
);

VALIDATE_COMPONENT_FILENAME("foo_pitch_tempo.dll");

class init_bass : public initquit {
public:
	void on_init() override {
		// Initialize BASS with "no sound" device for DSP
		BASS_Init(0, 44100, 0, 0, NULL);
	}
	void on_quit() override {
		BASS_Free();
	}
};

static initquit_factory_t<init_bass> g_init_bass;

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
