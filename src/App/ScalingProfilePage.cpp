#include "pch.h"
#include "ScalingProfilePage.h"
#if __has_include("ScalingProfilePage.g.cpp")
#include "ScalingProfilePage.g.cpp"
#endif
#include "ScalingProfileViewModel.h"
#include "Win32Utils.h"
#include "ComboBoxHelper.h"
#include "AppSettings.h"
#include "ScalingProfileService.h"
#include <dxgi1_6.h>


namespace winrt::Magpie::App::implementation {

static std::vector<std::wstring> GetAllGraphicsAdapters() {
	winrt::com_ptr<IDXGIFactory1> dxgiFactory;
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
	if (FAILED(hr)) {
		return {};
	}

	std::vector<std::wstring> result;

	winrt::com_ptr<IDXGIAdapter1> adapter;
	for (UINT adapterIndex = 0;
		SUCCEEDED(dxgiFactory->EnumAdapters1(adapterIndex, adapter.put()));
		++adapterIndex
	) {
		DXGI_ADAPTER_DESC1 desc;
		hr = adapter->GetDesc1(&desc);

		// 不包含 WARP
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			continue;
		}

		result.emplace_back(SUCCEEDED(hr) ? desc.Description : L"???");
	}

	return result;
}

ScalingProfilePage::ScalingProfilePage() {
	InitializeComponent();

	App app = Application::Current().as<App>();
	_magSettings = AppSettings::Get().DefaultScalingProfile().MagSettings();

	if (Win32Utils::GetOSBuild() < 22000) {
		// Segoe MDL2 Assets 不存在 Move 图标
		AdjustCursorSpeedFontIcon().Glyph(L"\uE962");
	}

	if (GetSystemMetrics(SM_CMONITORS) <= 1) {
		// 只有一个显示器时隐藏多显示器选项
		MultiMonitorSettingItem().Visibility(Visibility::Collapsed);
		Is3DGameModeSettingItem().Margin({ 0,0,0,-2 });
	}

	{
		std::vector<std::wstring> adapters = GetAllGraphicsAdapters();
		if (adapters.size() <= 1) {
			// 只有一个显卡时隐藏显示卡选项
			GraphicsAdapterSettingItem().Visibility(Visibility::Collapsed);
			ShowFPSSettingItem().Margin({ 0,-2,0,0 });
			_magSettings.GraphicsAdapter(0);
		} else {
			Controls::ItemCollection items = GraphicsAdapterComboBox().Items();
			for (const std::wstring& adapter : adapters) {
				items.Append(box_value(adapter));
			}

			uint32_t adapter = _magSettings.GraphicsAdapter();
			if (adapter > 0 && adapter >= items.Size()) {
				_magSettings.GraphicsAdapter(0);
				adapter = 0;
			}

			GraphicsAdapterComboBox().SelectedIndex(adapter);
		}
	}
	
	ShowFPSToggleSwitch().IsOn(_magSettings.IsShowFPS());

	VSyncToggleSwitch().IsOn(_magSettings.IsVSync());
	TripleBufferingToggleSwitch().IsOn(_magSettings.IsTripleBuffering());
	_UpdateVSync();

	DisableWindowResizingToggleSwitch().IsOn(_magSettings.IsDisableWindowResizing());
	ReserveTitleBarToggleSwitch().IsOn(_magSettings.IsReserveTitleBar());
}

void ScalingProfilePage::OnNavigatedTo(Navigation::NavigationEventArgs const& args) {
	uint32_t profileId = args.Parameter().as<uint32_t>();
	_viewModel = make<ScalingProfileViewModel>(profileId);
}

void ScalingProfilePage::ComboBox_DropDownOpened(IInspectable const& sender, IInspectable const&) {
	ComboBoxHelper::DropDownOpened(*this, sender);
}

void ScalingProfilePage::GraphicsAdapterComboBox_SelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) {
	if (!IsLoaded()) {
		return;
	}

	_magSettings.GraphicsAdapter(GraphicsAdapterComboBox().SelectedIndex());
}

void ScalingProfilePage::ShowFPSToggleSwitch_Toggled(IInspectable const&, RoutedEventArgs const&) {
	_magSettings.IsShowFPS(ShowFPSToggleSwitch().IsOn());
}

void ScalingProfilePage::VSyncToggleSwitch_Toggled(IInspectable const&, RoutedEventArgs const&) {
	if (!IsLoaded()) {
		return;
	}

	_UpdateVSync();
}

void ScalingProfilePage::TripleBufferingToggleSwitch_Toggled(IInspectable const&, RoutedEventArgs const&) {
	_magSettings.IsTripleBuffering(TripleBufferingToggleSwitch().IsOn());
}

void ScalingProfilePage::DisableWindowResizingToggleSwitch_Toggled(IInspectable const&, RoutedEventArgs const&) {
	_magSettings.IsDisableWindowResizing(DisableWindowResizingToggleSwitch().IsOn());
}

void ScalingProfilePage::ReserveTitleBarToggleSwitch_Toggled(IInspectable const&, RoutedEventArgs const&) {
	_magSettings.IsReserveTitleBar(ReserveTitleBarToggleSwitch().IsOn());
}

void ScalingProfilePage::CroppingToggleSwitch_Toggled(IInspectable const&, RoutedEventArgs const&) {

}

void ScalingProfilePage::_UpdateVSync() {
	bool isOn = VSyncToggleSwitch().IsOn();
	_magSettings.IsVSync(isOn);
	TripleBufferingSettingItem().IsEnabled(isOn);
}

}
