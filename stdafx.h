#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/helpers/helpers.h>

#include "bass.h"
#include "bass_fx.h"

#ifdef _WIN64
// BASS libraries removed in favor of RubberBand
#endif

#include "resource.h"
