#include "pch.h"
#include "ProfilePage.h"
#if __has_include("ProfilePage.g.cpp")
#include "ProfilePage.g.cpp"
#endif
#include "Win32Helper.h"
#include "ControlHelper.h"
#include "ProfileService.h"
#include "Profile.h"

using namespace ::Magpie;
using namespace winrt;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Input;

namespace winrt::Magpie::implementation {

void ProfilePage::InitializeComponent() {
	ProfilePageT::InitializeComponent();

	if (!Win32Helper::GetOSVersion().IsWin11()) {
		// Segoe MDL2 Assets 不存在 Move 图标
		AdjustCursorSpeedFontIcon().Glyph(L"\uE962");
	}
}

void ProfilePage::OnNavigatedTo(Navigation::NavigationEventArgs const& args) {
	int profileIdx = args.Parameter().as<int>();
	_viewModel = make_self<ProfileViewModel>(profileIdx);
}

void ProfilePage::ComboBox_DropDownOpened(IInspectable const& sender, IInspectable const&) {
	ControlHelper::ComboBox_DropDownOpened(sender);
}

void ProfilePage::NumberBox_Loaded(IInspectable const& sender, RoutedEventArgs const&) {
	ControlHelper::NumberBox_Loaded(sender);
}

void ProfilePage::CursorScalingComboBox_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
	if ((CursorScaling)_viewModel->CursorScaling() == CursorScaling::Custom) {
		CursorScalingComboBox().MinWidth(0);
		CustomCursorScalingNumberBox().Visibility(Visibility::Visible);
		CustomCursorScalingLabel().Visibility(Visibility::Visible);
	} else {
		double minWidth = Application::Current().Resources()
			.Lookup(box_value(L"SettingBoxMinWidth"))
			.as<double>();
		CursorScalingComboBox().MinWidth(minWidth);
		CustomCursorScalingNumberBox().Visibility(Visibility::Collapsed);
		CustomCursorScalingLabel().Visibility(Visibility::Collapsed);
	}
}

void ProfilePage::RenameMenuItem_Click(IInspectable const&, RoutedEventArgs const&) {
	RenameFlyout().ShowAt(MoreOptionsButton());
}

void ProfilePage::RenameFlyout_Opening(IInspectable const&, IInspectable const&) {
	TextBox tb = RenameTextBox();
	hstring name = _viewModel->Name();
	tb.Text(name);
	tb.SelectionStart(name.size());
}

void ProfilePage::RenameConfirmButton_Click(IInspectable const&, RoutedEventArgs const&) {
	RenameFlyout().Hide();
	_viewModel->Rename();
}

void ProfilePage::RenameTextBox_KeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args) {
	if (args.Key() == VirtualKey::Enter && _viewModel->IsRenameConfirmButtonEnabled()) {
		RenameConfirmButton_Click(nullptr, nullptr);
	}
}

void ProfilePage::ReorderMenuItem_Click(IInspectable const&, RoutedEventArgs const&) {
	ReorderFlyout().ShowAt(MoreOptionsButton());
}

void ProfilePage::DeleteMenuItem_Click(IInspectable const&, RoutedEventArgs const&) {
	DeleteFlyout().ShowAt(MoreOptionsButton());
}

void ProfilePage::DeleteButton_Click(IInspectable const&, RoutedEventArgs const&) {
	DeleteFlyout().Hide();
	_viewModel->Delete();
}

void ProfilePage::LaunchParametersTextBox_KeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args) {
	if (args.Key() == VirtualKey::Enter) {
		Focus(FocusState::Pointer);
	}
}

}
