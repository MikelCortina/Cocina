#pragma once

#ifdef __clang__
#pragma clang diagnostic ignored "-Woverloaded-virtual"
#endif

#ifdef _EG_WINDOWS_PLATFORM
#include "Engine/Scene.h"
#include "RHI.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Windows/AllowWindowsPlatformTypes.h"

#include <windows.h>   // or wingdi.h, winuser.h, etc.

#include "Windows/HideWindowsPlatformTypes.h"
#endif

