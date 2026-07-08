#include "stdafx.h"

DECLARE_COMPONENT_VERSION(
	u8"时间魔术师 (Time Wizard)",
	"1.0",
	u8"一个基于bass引擎的变速变调与loop循环组件\n"
	"Author: snokey"
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
