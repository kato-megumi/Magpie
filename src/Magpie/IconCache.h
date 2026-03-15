#pragma once
#include <winrt/Windows.Graphics.Imaging.h>

namespace Magpie {

struct IconCache {
	// Returns a cached icon bitmap if available and valid, otherwise extracts the
	// icon from the exe, caches it, and returns the result. This avoids repeated
	// slow icon extraction for executables on network drives.
	static winrt::Windows::Graphics::Imaging::SoftwareBitmap ExtractIconFromExe(
		const wchar_t* exePath, uint32_t preferredSize);

	static void Initialize();
};

}
