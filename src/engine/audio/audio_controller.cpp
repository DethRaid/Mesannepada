#include "audio_controller.hpp"

#include "console/cvars.hpp"

static auto cvar_master_volume = AutoCVar_Float{ "a.MasterVolume", "Master audio volume", 100 };

static auto cvar_music_volume = AutoCVar_Float{ "a.MusicVolume", "Music volume", 100 };

static auto cvar_sfx_volume = AutoCVar_Float{ "a.SFXVolume", "Volume of sound FX", 100 };
