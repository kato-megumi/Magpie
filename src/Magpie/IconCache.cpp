#include "pch.h"
#include "IconCache.h"
#include "IconHelper.h"
#include "AppSettings.h"
#include "Win32Helper.h"
#include "Logger.h"
#include "StrHelper.h"
#include <winrt/Windows.Graphics.Imaging.h>

using namespace winrt;
using namespace Windows::Graphics::Imaging;

namespace Magpie {

static std::filesystem::path _cacheDir;

static std::wstring _MakeCacheKey(const wchar_t* exePath, uint32_t size) {
	// Lowercase path for consistent hashing
	std::wstring key(exePath);
	for (wchar_t& c : key) {
		c = towlower(c);
	}
	key += L':';
	key += std::to_wstring(size);

	size_t h = std::hash<std::wstring>{}(key);
	return fmt::format(L"{:016x}", h);
}

static bool _GetFileLastWriteTime(const wchar_t* path, FILETIME& ft) noexcept {
	WIN32_FILE_ATTRIBUTE_DATA attrs{};
	if (!GetFileAttributesEx(path, GetFileExInfoStandard, &attrs)) {
		return false;
	}
	ft = attrs.ftLastWriteTime;
	return true;
}

// Cache file layout:
//   [0..8)   FILETIME lastWriteTime of the source exe
//   [8..12)  int32 width
//   [12..16) int32 height
//   [16..)   BGRA8 premultiplied pixel data (width * height * 4 bytes)
static SoftwareBitmap _LoadFromCache(const std::filesystem::path& cachePath, const FILETIME& exeWriteTime) {
	std::vector<uint8_t> buf;
	if (!Win32Helper::ReadFile(cachePath.c_str(), buf)) {
		return nullptr;
	}

	constexpr size_t HEADER_SIZE = sizeof(FILETIME) + sizeof(int32_t) * 2;
	if (buf.size() < HEADER_SIZE) {
		return nullptr;
	}

	// Validate last write time
	FILETIME cachedTime{};
	std::memcpy(&cachedTime, buf.data(), sizeof(FILETIME));
	if (cachedTime.dwLowDateTime != exeWriteTime.dwLowDateTime ||
		cachedTime.dwHighDateTime != exeWriteTime.dwHighDateTime) {
		return nullptr;
	}

	int32_t width = 0, height = 0;
	std::memcpy(&width, buf.data() + sizeof(FILETIME), sizeof(int32_t));
	std::memcpy(&height, buf.data() + sizeof(FILETIME) + sizeof(int32_t), sizeof(int32_t));

	if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
		return nullptr;
	}

	const size_t expectedSize = HEADER_SIZE + (size_t)width * height * 4;
	if (buf.size() != expectedSize) {
		return nullptr;
	}

	SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Premultiplied);
	{
		BitmapBuffer buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
		auto ref = buffer.CreateReference();
		std::memcpy(ref.data(), buf.data() + HEADER_SIZE, (size_t)width * height * 4);
	}

	return bitmap;
}

static void _SaveToCache(const std::filesystem::path& cachePath, const FILETIME& exeWriteTime, const SoftwareBitmap& bitmap) {
	const int32_t width = bitmap.PixelWidth();
	const int32_t height = bitmap.PixelHeight();

	constexpr size_t HEADER_SIZE = sizeof(FILETIME) + sizeof(int32_t) * 2;
	const size_t dataSize = (size_t)width * height * 4;
	std::vector<uint8_t> buf(HEADER_SIZE + dataSize);

	std::memcpy(buf.data(), &exeWriteTime, sizeof(FILETIME));
	std::memcpy(buf.data() + sizeof(FILETIME), &width, sizeof(int32_t));
	std::memcpy(buf.data() + sizeof(FILETIME) + sizeof(int32_t), &height, sizeof(int32_t));

	{
		BitmapBuffer buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Read);
		auto ref = buffer.CreateReference();
		std::memcpy(buf.data() + HEADER_SIZE, ref.data(), dataSize);
	}

	Win32Helper::WriteFile(cachePath.c_str(), buf);
}

void IconCache::Initialize() {
	_cacheDir = AppSettings::Get().ConfigDir() / L"icons";
	Win32Helper::CreateDir(_cacheDir.native(), true);
}

SoftwareBitmap IconCache::ExtractIconFromExe(const wchar_t* exePath, uint32_t preferredSize) {
	// Try to get the exe's last write time for cache validation
	FILETIME exeWriteTime{};
	bool hasWriteTime = _GetFileLastWriteTime(exePath, exeWriteTime);

	if (hasWriteTime && !_cacheDir.empty()) {
		std::wstring key = _MakeCacheKey(exePath, preferredSize);
		std::filesystem::path cachePath = _cacheDir / key;

		SoftwareBitmap cached = _LoadFromCache(cachePath, exeWriteTime);
		if (cached) {
			return cached;
		}

		// Cache miss or invalid - extract and save
		SoftwareBitmap bitmap = IconHelper::ExtractIconFromExe(exePath, preferredSize);
		if (bitmap) {
			_SaveToCache(cachePath, exeWriteTime, bitmap);
		}
		return bitmap;
	}

	// No write time available, skip caching
	return IconHelper::ExtractIconFromExe(exePath, preferredSize);
}

}
