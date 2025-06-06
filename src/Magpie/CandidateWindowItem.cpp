#include "pch.h"
#include "CandidateWindowItem.h"
#if __has_include("CandidateWindowItem.g.cpp")
#include "CandidateWindowItem.g.cpp"
#endif
#include "Win32Helper.h"
#include "AppXReader.h"
#include "IconHelper.h"
#include "StrHelper.h"
#include "App.h"
#include "MainWindow.h"

using namespace ::Magpie;
using namespace winrt;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Graphics::Display;

namespace winrt::Magpie::implementation {

static std::wstring GetProcessDesc(HWND hWnd) {
	if (Win32Helper::GetWndClassName(hWnd) == L"ApplicationFrameWindow") {
		// 跳过 UWP 窗口
		return {};
	}

	// 移植自 https://github.com/dotnet/runtime/blob/4a63cb28b69e1c48bccf592150be7ba297b67950/src/libraries/System.Diagnostics.FileVersionInfo/src/System/Diagnostics/FileVersionInfo.Windows.cs
	std::wstring fileName = Win32Helper::GetPathOfWnd(hWnd);
	if (fileName.empty()) {
		return {};
	}

	DWORD dummy;
	DWORD infoSize = GetFileVersionInfoSizeEx(FILE_VER_GET_LOCALISED, fileName.c_str(), &dummy);
	if (infoSize == 0) {
		return {};
	}

	std::unique_ptr<uint8_t[]> infoData = std::make_unique<uint8_t[]>(infoSize);
	if (!GetFileVersionInfoEx(FILE_VER_GET_LOCALISED, fileName.c_str(), 0, infoSize, infoData.get())) {
		return {};
	}

	std::wstring codePage;
	uint8_t* langId = nullptr;
	uint32_t len;
	if (VerQueryValue(infoData.get(), L"\\VarFileInfo\\Translation", (void**)&langId, &len)) {
		codePage = fmt::format(L"{:08X}", uint32_t((*(uint16_t*)langId << 16) | *(uint16_t*)(langId + 2)));
	} else {
		codePage = L"040904E4";
	}

	wchar_t* description = nullptr;
	std::wstring descPath = fmt::format(L"\\StringFileInfo\\{}\\FileDescription", codePage);
	if (!VerQueryValue(infoData.get(), descPath.c_str(), (void**)&description, &len)) {
		return {};
	}

	return description;
}

CandidateWindowItem::CandidateWindowItem(HWND hWnd) {
	_title = Win32Helper::GetWndTitle(hWnd);
	_defaultProfileName = _title;

	_className = Win32Helper::GetWndClassName(hWnd);
	_path = Win32Helper::GetPathOfWnd(hWnd);

	MUXC::ImageIcon placeholder;
	placeholder.Width(16);
	placeholder.Height(16);
	_icon = std::move(placeholder);

	_ResolveWindow(true, true, hWnd);
}

IconElement CandidateWindowItem::Icon() const noexcept {
	// 返回副本，否则在 ComboBox 中绑定会导致崩溃
	if (MUXC::ImageIcon imageIcon = _icon.try_as<MUXC::ImageIcon>()) {
		MUXC::ImageIcon icon;
		icon.Source(imageIcon.Source());
		icon.Width(imageIcon.Width());
		icon.Height(imageIcon.Height());
		return std::move(icon);
	} else if (FontIcon fontIcon = _icon.try_as<FontIcon>()) {
		FontIcon icon;
		icon.Glyph(fontIcon.Glyph());
		icon.FontSize(fontIcon.FontSize());
		return std::move(icon);
	}

	return nullptr;
}

fire_and_forget CandidateWindowItem::_ResolveWindow(bool resolveIcon, bool resolveName, HWND hWnd) {
	assert(resolveIcon || resolveName);

	auto weakThis = get_weak();

	// 解析名称和图标非常耗时，转到后台进行
	co_await resume_background();

	AppXReader reader;
	const bool isPackaged = reader.Initialize(hWnd);
	if (resolveName) {
		std::wstring defaultProfileName = isPackaged ? reader.GetDisplayName() : GetProcessDesc(hWnd);
		StrHelper::Trim(defaultProfileName);

		auto strongThis = weakThis.get();
		if (!strongThis) {
			co_return;
		}

		App::Get().Dispatcher().RunAsync(
			CoreDispatcherPriority::Normal,
			[this, defaultProfileName(std::move(defaultProfileName)), aumid(reader.AUMID())]() {
				if (!defaultProfileName.empty()) {
					_defaultProfileName = defaultProfileName;
				}
				// 即使 defaultProfileName 为空也通知 DefaultProfileName 已更改，
				// 这是为了正确设置 CandidateWindowIndex。
				RaisePropertyChanged(L"DefaultProfileName");

				_aumid = aumid;
			}
		);
	}

	if (!resolveIcon) {
		co_return;
	}

	SoftwareBitmap iconBitmap{ nullptr };
	const uint32_t iconSize = (uint32_t)std::lround(
		16 * App::Get().MainWindow().CurrentDpi() / double(USER_DEFAULT_SCREEN_DPI));

	if (isPackaged) {
		std::variant<std::wstring, SoftwareBitmap> uwpIcon =
			reader.GetIcon(iconSize, App::Get().IsLightTheme(), true);
		if (uwpIcon.index() == 1) {
			iconBitmap = std::get<1>(uwpIcon);
		}
	} else {
		iconBitmap = IconHelper::ExtractIconFormWnd(hWnd, iconSize);
	}

	// 切换到主线程
	co_await App::Get().Dispatcher();

	if (auto strongThis = weakThis.get()) {
		if (iconBitmap) {
			SoftwareBitmapSource imageSource;
			co_await imageSource.SetBitmapAsync(iconBitmap);

			MUXC::ImageIcon imageIcon;
			imageIcon.Width(16);
			imageIcon.Height(16);
			imageIcon.Source(imageSource);

			strongThis->_icon = std::move(imageIcon);
		} else {
			FontIcon fontIcon;
			fontIcon.Glyph(L"\uE737");
			fontIcon.FontSize(16);

			strongThis->_icon = std::move(fontIcon);
		}

		strongThis->RaisePropertyChanged(L"Icon");
	}
}

}
