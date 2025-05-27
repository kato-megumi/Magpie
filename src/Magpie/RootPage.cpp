#include "pch.h"
#include "RootPage.h"
#if __has_include("RootPage.g.cpp")
#include "RootPage.g.cpp"
#endif
#include "App.h"
#include "AppXReader.h"
#include "CandidateWindowItem.h"
#include "CommonSharedConstants.h"
#include "ContentDialogHelper.h"
#include "ControlHelper.h"
#include "IconHelper.h"
#include "LocalizationService.h"
#include "MainWindow.h"
#include "ProfileService.h"
#include "ThemeHelper.h"
#include "TitleBarControl.h"
#include "Win32Helper.h"
#include "XamlHelper.h"
#include <algorithm> // for std::transform
#include <cwctype>

using namespace ::Magpie;
using namespace winrt;
using namespace Windows::Graphics::Display;
using namespace Windows::Graphics::Imaging;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media::Animation;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Windows::UI::Xaml::Controls;

namespace winrt::Magpie::implementation {

// NewProfile is now at index 4, profiles start at index 5
static constexpr uint32_t FIRST_PROFILE_ITEM_IDX = 5;

RootPage::RootPage() {
	// 设置 Language 属性帮助 XAML 选择合适的字体，比如繁体中文使用 Microsoft JhengHei UI，
	// 日语使用 Yu Gothic UI
	Language(LocalizationService::Get().GetLanguage());
}

RootPage::~RootPage() {
	ContentDialogHelper::CloseActiveDialog();

	// 不手动置空会内存泄露
	// 似乎是 XAML Islands 的 bug？
	ContentFrame().Content(nullptr);

	// 每次主窗口关闭都清理 AppXReader 的缓存
	AppXReader::ClearCache();
}

void RootPage::InitializeComponent() {
	RootPageT::InitializeComponent();

	_appThemeChangedRevoker = App::Get().ThemeChanged(
		auto_revoke, [this](bool) { _UpdateTheme(true); });
	_UpdateTheme(false);

	_dpiChangedRevoker = App::Get().MainWindow().DpiChanged(
		auto_revoke, [this](uint32_t) { _UpdateIcons(false); });

	ProfileService& profileService = ProfileService::Get();
	_profileAddedRevoker = profileService.ProfileAdded(
		auto_revoke, std::bind_front(&RootPage::_ProfileService_ProfileAdded, this));
	_profileRenamedRevoker = profileService.ProfileRenamed(
		auto_revoke, std::bind_front(&RootPage::_ProfileService_ProfileRenamed, this));
	_profileRemovedRevoker = profileService.ProfileRemoved(
		auto_revoke, std::bind_front(&RootPage::_ProfileService_ProfileRemoved, this));
	_profileMovedRevoker = profileService.ProfileMoved(
		auto_revoke, std::bind_front(&RootPage::_ProfileService_ProfileReordered, this));
	_profileMovedToTopRevoker = profileService.ProfileMovedToTop(
		auto_revoke, std::bind_front(&RootPage::_ProfileService_ProfileMoveToTop, this));

	IVector<IInspectable> navMenuItems = RootNavigationView().MenuItems();
	const auto& profiles = AppSettings::Get().Profiles();
	for (uint32_t i = 0; i < profiles.size(); ++i) {
		MUXC::NavigationViewItem item;
		item.Content(box_value(profiles[i].name));
		// Store profile index in Tag for reliable navigation
		item.Tag(box_value((int)i));
		// 用于占位
		item.Icon(FontIcon());
		_LoadIcon(item, profiles[i]);

		navMenuItems.Append(item);
	}
}

static void SkipToggleSwitchAnimations(const DependencyObject& elem) {
	FrameworkElement rootGrid = VisualTreeHelper::GetChild(elem, 0).try_as<FrameworkElement>();

	for (VisualStateGroup group : VisualStateManager::GetVisualStateGroups(rootGrid)) {
		for (VisualState state : group.States()) {
			if (Storyboard storyboard = state.Storyboard()) {
				storyboard.SkipToFill();
			}
		}
	}
}

void RootPage::RootPage_Loaded(IInspectable const&, RoutedEventArgs const&) {
	// 设置 NavigationView 内的 Tooltip 的主题
	XamlHelper::UpdateThemeOfTooltips(RootNavigationView(), ActualTheme());

	// 启动时跳过 ToggleSwitch 的动画
	std::vector<DependencyObject> elems{ *this };
	do {
		std::vector<DependencyObject> temp;

		for (const DependencyObject& elem : elems) {
			const int count = VisualTreeHelper::GetChildrenCount(elem);
			for (int i = 0; i < count; ++i) {
				DependencyObject current = VisualTreeHelper::GetChild(elem, i);

				if (get_class_name(current) == name_of<ToggleSwitch>()) {
					SkipToggleSwitchAnimations(current);
				} else {
					temp.emplace_back(std::move(current));
				}
			}
		}

		elems = std::move(temp);
	} while (!elems.empty());
}

void RootPage::NavigationView_SelectionChanged(
	MUXC::NavigationView const&,
	MUXC::NavigationViewSelectionChangedEventArgs const& args
) {
	auto contentFrame = ContentFrame();

	if (args.IsSettingsSelected()) {
		contentFrame.Navigate(xaml_typename<SettingsPage>());
	} else {
		IInspectable selectedItem = args.SelectedItem();
		if (!selectedItem) {
			contentFrame.Content(nullptr);
			return;
		}

		// Prefer Tag when it's a profile index (for filtered results); otherwise treat Tag as string for static pages.
		auto navItem = selectedItem.as<MUXC::NavigationViewItem>();
		IInspectable tag = navItem.Tag();
		if (tag) {
			try {
				int profileIdx = unbox_value<int>(tag);
				contentFrame.Navigate(xaml_typename<ProfilePage>(), box_value(profileIdx));
				return;
			} catch (...) {
				// Not an int, continue to string-based navigation below.
			}
		}
		if (tag) {
			hstring tagStr = unbox_value<hstring>(tag);
			Interop::TypeName typeName;
			if (tagStr == L"Home") {
				typeName = xaml_typename<HomePage>();
			} else if (tagStr == L"ScalingModes") {
				typeName = xaml_typename<ScalingModesPage>();
			} else if (tagStr == L"About") {
				typeName = xaml_typename<AboutPage>();
			} else {
				typeName = xaml_typename<HomePage>();
			}

			contentFrame.Navigate(typeName);
		} else {
			// Fallback: if no tag then compute index based on static offset.
			MUXC::NavigationView nv = RootNavigationView();
			uint32_t index;
			if (nv.MenuItems().IndexOf(nv.SelectedItem(), index)) {
				contentFrame.Navigate(xaml_typename<ProfilePage>(), box_value((int)index - 4));
			}
		}
	}
}

void RootPage::NavigationView_PaneOpening(MUXC::NavigationView const&, IInspectable const&) {
	if (Win32Helper::GetOSVersion().IsWin11()) {
		// Win11 中 Tooltip 自动适应主题
		return;
	}

	XamlHelper::UpdateThemeOfTooltips(*this, ActualTheme());

	// UpdateThemeOfTooltips 中使用的 hack 会使 NavigationViewItem 在展开时不会自动删除 Tooltip
	// 因此这里手动删除
	const MUXC::NavigationView& nv = RootNavigationView();
	for (const IInspectable& item : nv.MenuItems()) {
		ToolTipService::SetToolTip(item.try_as<DependencyObject>(), nullptr);
	}
	for (const IInspectable& item : nv.FooterMenuItems()) {
		ToolTipService::SetToolTip(item.try_as<DependencyObject>(), nullptr);
	}
}

void RootPage::NavigationView_PaneClosing(MUXC::NavigationView const&, MUXC::NavigationViewPaneClosingEventArgs const&) {
	XamlHelper::UpdateThemeOfTooltips(*this, ActualTheme());
}

void RootPage::NavigationView_DisplayModeChanged(MUXC::NavigationView const& nv, MUXC::NavigationViewDisplayModeChangedEventArgs const&) {
	bool isExpanded = nv.DisplayMode() == MUXC::NavigationViewDisplayMode::Expanded;
	nv.IsPaneToggleButtonVisible(!isExpanded);
	if (isExpanded) {
		// 延迟设置 IsPaneOpen 才能起作用
		App::Get().Dispatcher().TryEnqueue(DispatcherQueuePriority::Low, [nv(MUXC::NavigationView(nv))]() {
			nv.IsPaneOpen(true);
		});
	}

	// !!! HACK !!!
	// 使导航栏的可滚动区域不会覆盖标题栏
	FrameworkElement menuItemsScrollViewer = nv.try_as<IControlProtected>()
		.GetTemplateChild(L"MenuItemsScrollViewer").try_as<FrameworkElement>();
	menuItemsScrollViewer.Margin({ 0,isExpanded ? TitleBar().ActualHeight() : 0.0,0,0});

	XamlHelper::UpdateThemeOfTooltips(*this, ActualTheme());
}

void RootPage::NavigationView_ItemInvoked(MUXC::NavigationView const&, MUXC::NavigationViewItemInvokedEventArgs const& args) {
	if (args.InvokedItemContainer() == NewProfileNavigationViewItem()) {
		_newProfileViewModel->PrepareForOpen();

		// 同步调用 ShowAt 有时会失败
		App::Get().Dispatcher().TryEnqueue([that(get_strong())]() {
			that->NewProfileFlyout().ShowAt(that->NewProfileNavigationViewItem());
		});
	}
}

void RootPage::ComboBox_DropDownOpened(IInspectable const& sender, IInspectable const&) const {
	ControlHelper::ComboBox_DropDownOpened(sender);
}

void RootPage::NewProfileConfirmButton_Click(IInspectable const&, RoutedEventArgs const&) {
	_newProfileViewModel->Confirm();
	NewProfileFlyout().Hide();
}

void RootPage::NewProfileNameContextFlyout_Opening(IInspectable const&, IInspectable const&) {
	auto menuItems = NewProfileNameContextFlyout().Items();
	
	int idx = _newProfileViewModel->CandidateWindowIndex();
	if (idx < 0) {
		// 隐藏所有选项
		for (const MenuFlyoutItemBase& item : menuItems) {
			if (IInspectable tag = item.Tag(); tag && tag.try_as<int>()) {
				item.Visibility(Visibility::Collapsed);
			}
		}

		return;
	}

	CandidateWindowItem* selectedItem = get_self<CandidateWindowItem>(
		_newProfileViewModel->CandidateWindows().GetAt(idx).try_as<winrt::Magpie::CandidateWindowItem>());

	// 设置每个选项的可见性
	bool shouldInit = true;
	for (const MenuFlyoutItemBase& item : menuItems) {
		IInspectable tag = item.Tag();
		if (!tag) {
			continue;
		}

		std::optional<int> id = tag.try_as<int>();
		if (!id) {
			continue;
		}

		shouldInit = false;

		if (*id == 1) {
			// 填入进程名选项
			item.Visibility(selectedItem->AUMID().empty() ? Visibility::Visible : Visibility::Collapsed);
		} else if (*id == 2) {
			// 填入应用名选项
			item.Visibility(selectedItem->AUMID().empty() ? Visibility::Collapsed : Visibility::Visible);
		} else {
			// 填入窗口标题选项
			item.Visibility(Visibility::Visible);
		}
	}

	if (!shouldInit) {
		return;
	}

	LocalizationService& ls = LocalizationService::Get();

	// 填入进程名
	MenuFlyoutItem item1;
	FontIcon icon1;
	icon1.Glyph(L"\xE9F5");
	item1.Text(ls.GetLocalizedString(L"Root_NewProfileFlyout_NameContextFlyout_ProcessName"));
	item1.Icon(icon1);
	RoutedEventHandler clickHandler([this](IInspectable const&, IInspectable const&) {
		_UpdateNewProfileNameTextBox(false);
	});
	item1.Click(clickHandler);
	item1.Tag(box_value(1));
	menuItems.Append(item1);

	// 填入应用名
	MenuFlyoutItem item2;
	FontIcon icon2;
	icon2.Glyph(L"\xECAA");
	item2.Text(ls.GetLocalizedString(L"Root_NewProfileFlyout_NameContextFlyout_AppName"));
	item2.Icon(icon2);
	item2.Click(clickHandler);
	item2.Tag(box_value(2));
	menuItems.Append(item2);

	if (selectedItem->AUMID().empty()) {
		item2.Visibility(Visibility::Collapsed);
	} else {
		item1.Visibility(Visibility::Collapsed);
	}

	// 填入窗口标题
	MenuFlyoutItem item3;
	FontIcon icon3;
	icon3.Glyph(L"\xE737");
	item3.Icon(icon3);
	item3.Text(ls.GetLocalizedString(L"Root_NewProfileFlyout_NameContextFlyout_WindowTitle"));
	item3.Click([this](IInspectable const&, IInspectable const&) {
		_UpdateNewProfileNameTextBox(true);
	});
	item3.Tag(box_value(3));
	menuItems.Append(item3);
}

void RootPage::NewProfileNameTextBox_KeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args) {
	if (args.Key() == VirtualKey::Enter && _newProfileViewModel->IsConfirmButtonEnabled()) {
		NewProfileConfirmButton_Click(nullptr, nullptr);
	}
}

void RootPage::NavigateToAboutPage() {
	MUXC::NavigationView nv = RootNavigationView();
	nv.SelectedItem(nv.FooterMenuItems().GetAt(0));
}

TitleBarControl& RootPage::TitleBar() {
	return *get_self<TitleBarControl>(RootPageT::TitleBar());
}

static Color Win32ColorToWinRTColor(COLORREF color) {
	return { 255, GetRValue(color), GetGValue(color), GetBValue(color) };
}

void RootPage::_UpdateTheme(bool updateIcons) {
	const bool isLightTheme = App::Get().IsLightTheme();

	if (IsLoaded() && (ActualTheme() == ElementTheme::Light) == isLightTheme) {
		// 无需切换
		return;
	}

	if (!Win32Helper::GetOSVersion().Is22H2OrNewer()) {
		const Windows::UI::Color bkgColor = Win32ColorToWinRTColor(
			isLightTheme ? ThemeHelper::LIGHT_TINT_COLOR : ThemeHelper::DARK_TINT_COLOR);
		Background(SolidColorBrush(bkgColor));
	}

	ElementTheme newTheme = isLightTheme ? ElementTheme::Light : ElementTheme::Dark;
	RequestedTheme(newTheme);

	XamlHelper::UpdateThemeOfXamlPopups(XamlRoot(), newTheme);
	XamlHelper::UpdateThemeOfTooltips(*this, newTheme);

	if (updateIcons && IsLoaded()) {
		_UpdateIcons(true);
	}
}

fire_and_forget RootPage::_LoadIcon(MUXC::NavigationViewItem const& item, const Profile& profile) {
	weak_ref<MUXC::NavigationViewItem> weakRef(item);

	bool preferLightTheme = App::Get().IsLightTheme();
	bool isPackaged = profile.isPackaged;
	std::wstring path = profile.pathRule;
	const uint32_t iconSize = (uint32_t)std::lround(
		16.0f * App::Get().MainWindow().CurrentDpi() / USER_DEFAULT_SCREEN_DPI);

	co_await resume_background();

	std::wstring iconPath;
	SoftwareBitmap iconBitmap{ nullptr };

	if (isPackaged) {
		AppXReader reader;
		if (reader.Initialize(path)) {
			std::variant<std::wstring, SoftwareBitmap> uwpIcon =
				reader.GetIcon(iconSize, preferLightTheme);
			if (uwpIcon.index() == 0) {
				iconPath = std::get<0>(uwpIcon);
			} else {
				iconBitmap = std::get<1>(uwpIcon);
			}
		}
	} else {
		iconBitmap = IconHelper::ExtractIconFromExe(path.c_str(), iconSize);
	}

	co_await App::Get().Dispatcher();

	auto strongRef = weakRef.get();
	if (!strongRef) {
		co_return;
	}

	if (!iconPath.empty()) {
		BitmapIcon icon;
		icon.ShowAsMonochrome(false);
		icon.UriSource(Uri(iconPath));
		icon.Width(16);
		icon.Height(16);

		strongRef.Icon(icon);
	} else if (iconBitmap) {
		SoftwareBitmapSource imageSource;
		co_await imageSource.SetBitmapAsync(iconBitmap);

		MUXC::ImageIcon imageIcon;
		imageIcon.Width(16);
		imageIcon.Height(16);
		imageIcon.Source(imageSource);

		strongRef.Icon(imageIcon);
	} else {
		FontIcon icon;
		icon.Glyph(L"\uECAA");
		strongRef.Icon(icon);
	}
}

void RootPage::_UpdateIcons(bool skipDesktop) {
	IVector<IInspectable> navMenuItems = RootNavigationView().MenuItems();
	const std::vector<Profile>& profiles = AppSettings::Get().Profiles();

	for (uint32_t i = 0; i < profiles.size(); ++i) {
		if (skipDesktop && !profiles[i].isPackaged) {
			continue;
		}

		MUXC::NavigationViewItem item = navMenuItems.GetAt(FIRST_PROFILE_ITEM_IDX + i)
			.try_as<MUXC::NavigationViewItem>();
		_LoadIcon(item, profiles[i]);
	}
}

void RootPage::_ProfileService_ProfileAdded(Profile& profile) {
	MUXC::NavigationViewItem item;
	item.Content(box_value(profile.name));
	// Store profile index in Tag - new profile is added at the end
	int newIdx = (int)AppSettings::Get().Profiles().size() - 1;
	item.Tag(box_value(newIdx));
	// 用于占位
	item.Icon(FontIcon());
	_LoadIcon(item, profile);

	IVector<IInspectable> navMenuItems = RootNavigationView().MenuItems();
	navMenuItems.Append(item);
	RootNavigationView().SelectedItem(item);
}

void RootPage::_ProfileService_ProfileRenamed(uint32_t idx) {
	RootNavigationView().MenuItems()
		.GetAt(FIRST_PROFILE_ITEM_IDX + idx)
		.try_as<MUXC::NavigationViewItem>()
		.Content(box_value(AppSettings::Get().Profiles()[idx].name));
}

void RootPage::_ProfileService_ProfileRemoved(uint32_t idx) {
	MUXC::NavigationView nv = RootNavigationView();
	IVector<IInspectable> menuItems = nv.MenuItems();
	nv.SelectedItem(menuItems.GetAt(FIRST_PROFILE_ITEM_IDX - 2));

	hstring searchText = ProfileSearchBox().Text();
	if (searchText.size() > 0) {
		FilterProfiles(searchText);
		return;
	}

	menuItems.RemoveAt(FIRST_PROFILE_ITEM_IDX + idx);

	// Update Tags for all profiles after the removed one
	uint32_t profileCount = (uint32_t)AppSettings::Get().Profiles().size();
	for (uint32_t i = idx; i < profileCount; ++i) {
		auto navItem = menuItems.GetAt(FIRST_PROFILE_ITEM_IDX + i).try_as<MUXC::NavigationViewItem>();
		if (navItem) {
			navItem.Tag(box_value((int)i));
		}
	}
}

void RootPage::_ProfileService_ProfileReordered(uint32_t profileIdx, bool isMoveUp) {
	hstring searchText = ProfileSearchBox().Text();
	if (searchText.size() > 0) {
		FilterProfiles(searchText);
		return;
	}

	IVector<IInspectable> menuItems = RootNavigationView().MenuItems();

	uint32_t curIdx = FIRST_PROFILE_ITEM_IDX + profileIdx;
	uint32_t otherIdx = isMoveUp ? curIdx - 1 : curIdx + 1;
	
	IInspectable otherItem = menuItems.GetAt(otherIdx);
	menuItems.RemoveAt(otherIdx);
	menuItems.InsertAt(curIdx, otherItem);

	// Update Tags for the two swapped items
	// After RemoveAt/InsertAt, items have swapped positions:
	// - curIdx now contains the item that was at otherIdx
	// - otherIdx now contains the item that was at curIdx
	uint32_t otherProfileIdx = isMoveUp ? profileIdx - 1 : profileIdx + 1;
	auto curNavItem = menuItems.GetAt(curIdx).try_as<MUXC::NavigationViewItem>();
	auto otherNavItem = menuItems.GetAt(otherIdx).try_as<MUXC::NavigationViewItem>();
	if (curNavItem) {
		curNavItem.Tag(box_value((int)profileIdx));  // Was at otherIdx, now represents profileIdx
	}
	if (otherNavItem) {
		otherNavItem.Tag(box_value((int)otherProfileIdx));  // Was at curIdx, now represents otherProfileIdx
	}
}

void RootPage::_ProfileService_ProfileMoveToTop(uint32_t profileIdx) {
	hstring searchText = ProfileSearchBox().Text();
	if (searchText.size() > 0) {
		FilterProfiles(searchText);
		return;
	}

	IVector<IInspectable> menuItems = RootNavigationView().MenuItems();

	uint32_t curIdx = FIRST_PROFILE_ITEM_IDX + profileIdx;
	IInspectable curItem = menuItems.GetAt(curIdx);
	menuItems.RemoveAt(curIdx);
	menuItems.InsertAt(FIRST_PROFILE_ITEM_IDX, curItem);

	// Update Tags for all affected profiles (from 0 to profileIdx)
	for (uint32_t i = 0; i <= profileIdx; ++i) {
		auto navItem = menuItems.GetAt(FIRST_PROFILE_ITEM_IDX + i).try_as<MUXC::NavigationViewItem>();
		if (navItem) {
			navItem.Tag(box_value((int)i));
		}
	}

	RootNavigationView().SelectedItem(RootNavigationView().MenuItems().GetAt(FIRST_PROFILE_ITEM_IDX));
}

void RootPage::_UpdateNewProfileNameTextBox(bool fillWithTitle) {
	int idx = _newProfileViewModel->CandidateWindowIndex();
	if (idx < 0) {
		return;
	}

	CandidateWindowItem* selectedItem = get_self<CandidateWindowItem>(
		_newProfileViewModel->CandidateWindows().GetAt(idx).try_as<winrt::Magpie::CandidateWindowItem>());
	hstring text = fillWithTitle ? selectedItem->Title() : selectedItem->DefaultProfileName();

	TextBox textBox = NewProfileNameTextBox();
	if (textBox.Text() == text) {
		return;
	}

	const int size = (int)text.size();
	// 遗憾的是设置 Text 属性会导致撤销/重做历史丢失
	textBox.Text(std::move(text));
	// 修改文本后将光标移到最后
	textBox.Select(size, 0);
	// 如果文本太长，这个调用可以使视口移到光标位置
	textBox.Focus(FocusState::Programmatic);
}

void RootPage::ProfileSearchBox_QuerySubmitted(IInspectable const& , AutoSuggestBoxQuerySubmittedEventArgs const& ) {
    auto query = ProfileSearchBox().Text();
    FilterProfiles(query);
}

void RootPage::ProfileSearchBox_TextChanged(IInspectable const& , AutoSuggestBoxTextChangedEventArgs const& args) {
    if (args.Reason() == AutoSuggestionBoxTextChangeReason::UserInput) {
        auto query = ProfileSearchBox().Text();
        FilterProfiles(query);
    }
}

void RootPage::FilterProfiles(hstring const& query) {
    auto navMenuItems = RootNavigationView().MenuItems();
    navMenuItems.Clear();

    // Always add static items first
    // Home
    {
        MUXC::NavigationViewItem homeItem;
        homeItem.Content(box_value(L"Home"));
        homeItem.Tag(box_value(L"Home"));
        FontIcon homeIcon;
        homeIcon.Glyph(L"\xE80F");
        homeItem.Icon(homeIcon);
        navMenuItems.Append(homeItem);
    }
    // ScalingModes
    {
        MUXC::NavigationViewItem scalingItem;
        scalingItem.Content(box_value(L"Scaling Modes"));
        scalingItem.Tag(box_value(L"ScalingModes"));
        FontIcon scalingIcon;
        scalingIcon.Glyph(L"\xE740");
        scalingItem.Icon(scalingIcon);
        navMenuItems.Append(scalingItem);
    }
    // Profiles Header
    {
        MUXC::NavigationViewItemHeader profilesHeader;
        profilesHeader.Content(box_value(L"Profiles"));
        navMenuItems.Append(profilesHeader);
    }
    // Defaults
    {
        MUXC::NavigationViewItem defaultsItem;
        defaultsItem.Content(box_value(L"Defaults"));
        FontIcon defaultsIcon;
        defaultsIcon.Glyph(L"\xE81E");
        defaultsItem.Icon(defaultsIcon);
        navMenuItems.Append(defaultsItem);
    }

    // Add the "New Profile" item before profiles (defined in XAML with flyout)
    {
        navMenuItems.Append(NewProfileNavigationViewItem());
    }

    // Now add filtered profiles
    const auto& profiles = AppSettings::Get().Profiles();
    std::wstring q = query.c_str();
    std::transform(q.begin(), q.end(), q.begin(), [](wchar_t c) { return std::towlower(c); });

    for (size_t i = 0; i < profiles.size(); ++i)
    {
        // Lowercase name and pathRule for match check
        std::wstring name = profiles[i].name;
        std::wstring path = profiles[i].pathRule;
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return std::towlower(c); });
        std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return std::towlower(c); });

        // Match if query is in name or pathRule
        if (q.empty() ||
            name.find(q) != std::wstring::npos ||
            path.find(q) != std::wstring::npos)
        {
            MUXC::NavigationViewItem item;
            item.Content(box_value(profiles[i].name));
            item.Icon(FontIcon());
            // Store the original profile index (as int) in the Tag.
            item.Tag(box_value((int)i));
            _LoadIcon(item, profiles[i]);
            navMenuItems.Append(item);
        }
    }

    // Ensure a valid selection so ProfilePage has data.
    // Menu structure: 0=Home, 1=ScalingModes, 2=Header, 3=Defaults, 4=NewProfile, 5..N=profiles
    // So first profile is at index 5
    if (navMenuItems.Size() > 5)  // More than 5 static items means we have profiles
    {
        RootNavigationView().SelectedItem(navMenuItems.GetAt(5));  // First profile
    }
    else if (navMenuItems.Size() >= 4)
    {
        RootNavigationView().SelectedItem(navMenuItems.GetAt(3)); // Defaults
    }
}

}
