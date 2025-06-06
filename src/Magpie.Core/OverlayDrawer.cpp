#include "pch.h"
#include "OverlayDrawer.h"
#include "DeviceResources.h"
#include "Renderer.h"
#include "StepTimer.h"
#include "Logger.h"
#include "StrHelper.h"
#include "Win32Helper.h"
#include "FrameSourceBase.h"
#include "CommonSharedConstants.h"
#include "EffectDesc.h"
#include <bit>	// std::bit_ceil
#include <random>
#include "ImGuiHelper.h"
#include "ImGuiFontsCacheManager.h"
#include "ScalingWindow.h"
#include <ShlObj.h>

using namespace std::chrono;

namespace Magpie {

static const char* COLOR_INDICATOR = "■";
static const wchar_t COLOR_INDICATOR_W = L'■';

OverlayDrawer::OverlayDrawer() :
	_resourceLoader(winrt::ResourceLoader::GetForViewIndependentUse(CommonSharedConstants::APP_RESOURCE_MAP_ID))
{}

static constexpr const ImColor TIMELINE_COLORS[] = {
	{229,57,53,255},
	{156,39,176,255},
	{63,81,181,255},
	{30,136,229,255},
	{0,137,123,255},
	{121,85,72,255},
	{117,117,117,255}
};

static uint32_t GetSeed(const std::vector<Renderer::EffectInfo>& effectInfos) noexcept {
	uint32_t result = 0;
	for (const Renderer::EffectInfo& effectInfo : effectInfos) {
		result ^= (uint32_t)std::hash<std::string>()(effectInfo.name);
	}
	return result;
}

static SmallVector<uint32_t> GenerateTimelineColors(const std::vector<Renderer::EffectInfo>& effectInfos) noexcept {
	const uint32_t nEffect = (uint32_t)effectInfos.size();
	uint32_t totalColors = nEffect > 1 ? nEffect : 0;
	for (uint32_t i = 0; i < nEffect; ++i) {
		uint32_t nPass = (uint32_t)effectInfos[i].passNames.size();
		if (nPass > 1) {
			totalColors += nPass;
		}
	}

	if (totalColors == 0) {
		return {};
	}

	constexpr uint32_t nColors = (uint32_t)std::size(TIMELINE_COLORS);

	std::default_random_engine randomEngine(GetSeed(effectInfos));
	SmallVector<uint32_t> result;

	if (totalColors <= nColors) {
		result.resize(nColors);
		for (uint32_t i = 0; i < nColors; ++i) {
			result[i] = i;
		}
		std::shuffle(result.begin(), result.end(), randomEngine);

		result.resize(totalColors);
	} else {
		// 相邻通道颜色不同，相邻效果颜色不同
		result.resize(totalColors);
		std::uniform_int_distribution<uint32_t> uniformDst(0, nColors - 1);

		if (nEffect <= nColors) {
			if (nEffect > 1) {
				// 确保效果的颜色不重复
				std::array<uint32_t, nColors> effectColors{};
				for (uint32_t i = 0; i < nColors; ++i) {
					effectColors[i] = i;
				}
				std::shuffle(effectColors.begin(), effectColors.end(), randomEngine);

				uint32_t i = 0;
				for (uint32_t j = 0; j < nEffect; ++j) {
					result[i] = effectColors[j];
					++i;

					uint32_t nPass = (uint32_t)effectInfos[j].passNames.size();
					if (nPass > 1) {
						i += nPass;
					}
				}
			}
		} else {
			// 仅确保与前一个效果颜色不同
			uint32_t prevColor = std::numeric_limits<uint32_t>::max();
			uint32_t i = 0;
			for (uint32_t j = 0; j < nEffect; ++j) {
				uint32_t c = uniformDst(randomEngine);
				while (c == prevColor) {
					c = uniformDst(randomEngine);
				}

				result[i] = c;
				prevColor = c;
				++i;

				uint32_t nPass = (uint32_t)effectInfos[j].passNames.size();
				if (nPass > 1) {
					i += nPass;
				}
			}
		}

		// 生成通道的颜色
		size_t idx = 0;
		for (uint32_t i = 0; i < nEffect; ++i) {
			uint32_t nPass = (uint32_t)effectInfos[i].passNames.size();

			if (nEffect > 1) {
				++idx;

				if (nPass == 1) {
					continue;
				}
			}

			for (uint32_t j = 0; j < nPass; ++j) {
				uint32_t c = uniformDst(randomEngine);

				if (i > 0 || j > 0) {
					uint32_t prevColor = (i > 0 && j == 0) ? result[idx - 2] : result[idx - 1];

					if (j + 1 == nPass && i + 1 != nEffect && effectInfos[(size_t)i + 1].passNames.size() == 1) {
						// 当前效果的最后一个通道且下一个效果只有一个通道
						uint32_t nextColor = result[idx + 1];
						while (c == prevColor || c == nextColor) {
							c = uniformDst(randomEngine);
						}
					} else {
						while (c == prevColor) {
							c = uniformDst(randomEngine);
						}
					}
				}

				result[idx] = c;
				++idx;
			}
		}
	}

	return result;
}

OverlayDrawer::~OverlayDrawer() {
	if (ScalingWindow::Get().Options().Is3DGameMode() && IsUIVisible()) {
		HWND hwndSrc = ScalingWindow::Get().HwndSrc();
		EnableWindow(hwndSrc, TRUE);
		// 此时用户通过热键退出缩放，应激活源窗口
		Win32Helper::SetForegroundWindow(hwndSrc);
	}
}

bool OverlayDrawer::Initialize(DeviceResources* deviceResources) noexcept {
	if (!_imguiImpl.Initialize(deviceResources)) {
		Logger::Get().Error("初始化 ImGuiImpl 失败");
		return false;
	}

	_dpiScale = GetDpiForWindow(ScalingWindow::Get().Handle()) / float(USER_DEFAULT_SCREEN_DPI);

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.PopupRounding = style.WindowRounding = 6;
	style.FrameBorderSize = 1;
	style.FrameRounding = 2;
	style.WindowMinSize = ImVec2(10, 10);
	style.ScaleAllSizes(_dpiScale);

	if (!_BuildFonts()) {
		Logger::Get().Error("_BuildFonts 失败");
		return false;
	}

	// 将 _fontUI 设为默认字体
	ImGui::GetIO().FontDefault = _fontUI;

	// 获取硬件信息
	DXGI_ADAPTER_DESC desc{};
	HRESULT hr = deviceResources->GetGraphicsAdapter()->GetDesc(&desc);
	_hardwareInfo.gpuName = SUCCEEDED(hr) ? StrHelper::UTF16ToUTF8(desc.Description) : "UNAVAILABLE";

	const std::vector<Renderer::EffectInfo>& effectInfos =
		ScalingWindow::Get().Renderer().EffectInfos();
	_timelineColors = GenerateTimelineColors(effectInfos);

	uint32_t passCount = 0;
	for (const Renderer::EffectInfo& info : effectInfos) {
		passCount += (uint32_t)info.passNames.size();
	}
	_effectTimingsStatistics.resize(passCount);
	_lastestAvgEffectTimings.resize(passCount);

	return true;
}

void OverlayDrawer::Draw(
	uint32_t count,
	uint32_t fps,
	const SmallVector<float>& effectTimings
) noexcept {
	bool isShowFPS = ScalingWindow::Get().Options().IsShowFPS();

	if (!_isUIVisiable && !isShowFPS) {
		return;
	}

	if (_isFirstFrame) {
		// 刚显示时需连续渲染两帧才能显示
		_isFirstFrame = false;
		++count;
	}

	// 很多时候需要多次渲染避免呈现中间状态，但最多只渲染 10 次
	for (int i = 0; i < 10; ++i) {
		_imguiImpl.NewFrame();

		if (isShowFPS) {
			_DrawFPS(fps);
		}

		if (_isUIVisiable) {
			if (_DrawUI(effectTimings, fps)) {
				++count;
			}
		}

		// 中间状态不应执行渲染，因此调用 EndFrame 而不是 Render
		ImGui::EndFrame();
		
		if (--count == 0) {
			break;
		}
	}
	
	_imguiImpl.Draw();
}

// 3D 游戏模式下关闭叠加层将激活源窗口，但有时不希望这么做，比如用户切换
// 窗口导致停止缩放。通过 noSetForeground 禁止激活源窗口
void OverlayDrawer::SetUIVisibility(bool value, bool noSetForeground) noexcept {
	if (_isUIVisiable == value) {
		return;
	}
	_isUIVisiable = value;

	if (value) {
		if (ScalingWindow::Get().Options().Is3DGameMode()) {
			// 使全屏窗口不透明且可以接收焦点
			HWND hwndHost = ScalingWindow::Get().Handle();
			INT_PTR style = GetWindowLongPtr(hwndHost, GWL_EXSTYLE);
			SetWindowLongPtr(hwndHost, GWL_EXSTYLE, style & ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE));
			Win32Helper::SetForegroundWindow(hwndHost);

			// 使源窗口无法接收用户输入
			EnableWindow(ScalingWindow::Get().HwndSrc(), FALSE);
		}

		Logger::Get().Info("已开启叠加层");
	} else {
		if (!ScalingWindow::Get().Options().IsShowFPS()) {
			_imguiImpl.ClearStates();
		}

		if (ScalingWindow::Get().Options().Is3DGameMode()) {
			// 还原全屏窗口样式
			HWND hwndHost = ScalingWindow::Get().Handle();
			INT_PTR style = GetWindowLongPtr(hwndHost, GWL_EXSTYLE);
			SetWindowLongPtr(hwndHost, GWL_EXSTYLE, style | (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE));

			// 重新激活源窗口
			HWND hwndSrc = ScalingWindow::Get().HwndSrc();
			EnableWindow(hwndSrc, TRUE);

			if (!noSetForeground) {
				Win32Helper::SetForegroundWindow(hwndSrc);
			}
		}

		Logger::Get().Info("已关闭叠加层");
	}
}

void OverlayDrawer::MessageHandler(UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
	if (_isUIVisiable || ScalingWindow::Get().Options().IsShowFPS()) {
		_imguiImpl.MessageHandler(msg, wParam, lParam);
	}
}

static const std::wstring& GetAppLanguage() noexcept {
	static std::wstring language;
	if (language.empty()) {
		winrt::ResourceContext resourceContext = winrt::ResourceContext::GetForViewIndependentUse();
		language = resourceContext.QualifierValues().Lookup(L"Language");
		StrHelper::ToLowerCase(language);
	}
	return language;
}

static const std::wstring& GetSystemFontsFolder() noexcept {
	static std::wstring result;

	if (result.empty()) {
		wil::unique_cotaskmem_string fontsFolder;
		HRESULT hr = SHGetKnownFolderPath(FOLDERID_Fonts, 0, NULL, fontsFolder.put());
		if (FAILED(hr)) {
			Logger::Get().ComError("SHGetKnownFolderPath 失败", hr);
			return result;
		}

		result = fontsFolder.get();
	}

	return result;
}

bool OverlayDrawer::_BuildFonts() noexcept {
	const std::wstring& language = GetAppLanguage();
	ImFontAtlas& fontAtlas = *ImGui::GetIO().Fonts;

	auto buildFontAtlas = [&]() {
		fontAtlas.Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight | ImFontAtlasFlags_NoMouseCursors;

		std::wstring fontPath = GetSystemFontsFolder();
		if (Win32Helper::GetOSVersion().IsWin11()) {
			fontPath += L"\\SegUIVar.ttf";
		} else {
			fontPath += L"\\segoeui.ttf";
		}

		std::vector<uint8_t> fontData;
		if (!Win32Helper::ReadFile(fontPath.c_str(), fontData)) {
			Logger::Get().Error("读取字体文件失败");
			return false;
		}

		{
			// 构建 ImFontAtlas 前 uiRanges 不能析构，因为 ImGui 只保存了指针
			std::vector<ImWchar> uiRanges;
			_BuildFontUI(language, fontData, uiRanges);
			_BuildFontFPS(fontData);

			if (!fontAtlas.Build()) {
				Logger::Get().Error("构建 ImFontAtlas 失败");
				return false;
			}
		}

		return true;
	};

	if (ScalingWindow::Get().Options().IsFontCacheDisabled()) {
		if (!buildFontAtlas()) {
			return false;
		}
	} else {
		const uint32_t dpi = (uint32_t)std::lroundf(_dpiScale * USER_DEFAULT_SCREEN_DPI);
		if (ImGuiFontsCacheManager::Get().Load(language, dpi, fontAtlas)) {
			_fontUI = fontAtlas.Fonts[0];
			_fontMonoNumbers = fontAtlas.Fonts[1];
			_fontFPS = fontAtlas.Fonts[2];
		} else {
			if (!buildFontAtlas()) {
				return false;
			}

			ImGuiFontsCacheManager::Get().Save(language, dpi, fontAtlas);
		}
	}

	if (!_imguiImpl.BuildFonts()) {
		Logger::Get().Error("构建字体失败");
		return false;
	}

	return true;
}

template <size_t SIZE>
static void SetGlyphRanges(std::vector<ImWchar>& uiRanges, const ImWchar (&ranges)[SIZE]) noexcept {
	uiRanges.assign(std::begin(ranges), std::end(ranges));
}

// 指针重载，但不能直接使用指针
template <typename T, typename = std::enable_if_t<std::is_same_v<T, const ImWchar*>>>
static void SetGlyphRanges(std::vector<ImWchar>& uiRanges, T ranges) noexcept {
	// 删除末尾的 0
	for (const ImWchar* range = ranges; *range; ++range) {
		uiRanges.push_back(*range);
	}
}

void OverlayDrawer::_BuildFontUI(
	std::wstring_view language,
	const std::vector<uint8_t>& fontData,
	std::vector<ImWchar>& uiRanges
) noexcept {
	ImFontAtlas& fontAtlas = *ImGui::GetIO().Fonts;

	std::string extraFontPath;
	const ImWchar* extraRanges = nullptr;
	int extraFontNo = 0;
	
	assert(uiRanges.empty());
	if (language == L"en-us") {
		SetGlyphRanges(uiRanges, ImGuiHelper::BASIC_LATIN_RANGES);
	} else if (language == L"ru" || language == L"uk") {
		SetGlyphRanges(uiRanges, fontAtlas.GetGlyphRangesCyrillic());
	} else if (language == L"tr" || language == L"hu" || language == L"pl") {
		SetGlyphRanges(uiRanges, ImGuiHelper::EXTENDED_LATIN_RANGES);
	} else if (language == L"vi") {
		SetGlyphRanges(uiRanges, fontAtlas.GetGlyphRangesVietnamese());
	} else if (language == L"ka" && !Win32Helper::GetOSVersion().IsWin11()) {
		// Win10 中格鲁吉亚语无需加载额外字体
		SetGlyphRanges(uiRanges, ImGuiHelper::GEORGIAN_RANGES);
	} else {
		// Basic Latin 使用默认字体
		SetGlyphRanges(uiRanges, ImGuiHelper::BASIC_LATIN_RANGES);

		// 一些语言需要加载额外的字体:
		// 简体中文 -> Microsoft YaHei UI
		// 繁体中文 -> Microsoft JhengHei UI
		// 日语 -> Yu Gothic UI
		// 韩语/朝鲜语 -> Malgun Gothic
		// 泰米尔语 -> Nirmala UI
		// 格鲁吉亚语 -> Segoe UI (仅限 Win11)
		// 参见 https://learn.microsoft.com/en-us/windows/apps/design/style/typography#fonts-for-non-latin-languages
		if (language == L"zh-hans") {
			// msyh.ttc: 0 是微软雅黑，1 是 Microsoft YaHei UI
			extraFontPath = StrHelper::Concat(StrHelper::UTF16ToUTF8(GetSystemFontsFolder()), "\\msyh.ttc");
			extraFontNo = 1;
			extraRanges = ImGuiHelper::GetGlyphRangesChineseSimplifiedOfficial();
		} else if (language == L"zh-hant") {
			// msjh.ttc: 0 是 Microsoft JhengHei，1 是 Microsoft JhengHei UI
			extraFontPath = StrHelper::Concat(StrHelper::UTF16ToUTF8(GetSystemFontsFolder()), "\\msjh.ttc");
			extraFontNo = 1;
			extraRanges = ImGuiHelper::GetGlyphRangesChineseTraditionalOfficial();
		} else if (language == L"ja") {
			// YuGothM.ttc: 0 是 Yu Gothic Medium，1 是 Yu Gothic UI
			extraFontPath = StrHelper::Concat(StrHelper::UTF16ToUTF8(GetSystemFontsFolder()), "\\YuGothM.ttc");
			extraFontNo = 1;
			extraRanges = fontAtlas.GetGlyphRangesJapanese();
		} else if (language == L"ka") {
			assert(Win32Helper::GetOSVersion().IsWin11());
			// Win11 中的 Segoe UI Variable 不包含格鲁吉亚字母，需额外加载 Segoe UI
			extraFontPath = StrHelper::Concat(StrHelper::UTF16ToUTF8(GetSystemFontsFolder()), "\\segoeui.ttf");
			extraRanges = ImGuiHelper::EXTRA_GEORGIAN_RANGES;
		} else if (language == L"ko") {
			extraFontPath = StrHelper::Concat(StrHelper::UTF16ToUTF8(GetSystemFontsFolder()), "\\malgun.ttf");
			extraRanges = fontAtlas.GetGlyphRangesKorean();
		} else if (language == L"ta") {
			extraFontPath = StrHelper::Concat(StrHelper::UTF16ToUTF8(GetSystemFontsFolder()), "\\Nirmala.ttf");
			extraRanges = ImGuiHelper::EXTRA_TAMIL_RANGES;
		}
	}

	uiRanges.push_back(COLOR_INDICATOR_W);
	uiRanges.push_back(COLOR_INDICATOR_W);
	uiRanges.push_back(0);

	ImFontConfig config;
	config.FontDataOwnedByAtlas = false;

	const float fontSize = 18 * _dpiScale;

	//////////////////////////////////////////////////////////
	// 
	// uiRanges (+ extraRanges) -> _fontUI
	// 
	//////////////////////////////////////////////////////////


#ifdef _DEBUG
	std::char_traits<char>::copy(config.Name, "_fontUI", std::size(config.Name));
#endif

	_fontUI = fontAtlas.AddFontFromMemoryTTF(
		(void*)fontData.data(), (int)fontData.size(), fontSize, &config, uiRanges.data());

	if (extraRanges) {
		assert(Win32Helper::FileExists(StrHelper::UTF8ToUTF16(extraFontPath).c_str()));

		// 在 MergeMode 下已有字符会跳过而不是覆盖
		config.MergeMode = true;
		config.FontNo = extraFontNo;
		// 额外字体数据由 ImGui 管理，退出缩放时释放
		config.FontDataOwnedByAtlas = true;
		fontAtlas.AddFontFromFileTTF(extraFontPath.c_str(), fontSize, &config, extraRanges);
		config.FontDataOwnedByAtlas = false;
		config.FontNo = 0;
		config.MergeMode = false;
	}

	//////////////////////////////////////////////////////////
	//
	// NUMBER_RANGES + NOT_NUMBER_RANGES -> _fontMonoNumbers
	//
	//////////////////////////////////////////////////////////

#ifdef _DEBUG
	std::char_traits<char>::copy(config.Name, "_fontMonoNumbers", std::size(config.Name));
#endif

	// 等宽的数字字符
	config.GlyphMinAdvanceX = config.GlyphMaxAdvanceX = fontSize * 0.42f;
	_fontMonoNumbers = fontAtlas.AddFontFromMemoryTTF(
		(void*)fontData.data(), (int)fontData.size(), fontSize, &config, ImGuiHelper::NUMBER_RANGES);

	// 其他不等宽的字符
	config.MergeMode = true;
	config.GlyphMinAdvanceX = 0;
	config.GlyphMaxAdvanceX = std::numeric_limits<float>::max();
	fontAtlas.AddFontFromMemoryTTF(
		(void*)fontData.data(), (int)fontData.size(), fontSize, &config, ImGuiHelper::NOT_NUMBER_RANGES);
}

void OverlayDrawer::_BuildFontFPS(const std::vector<uint8_t>& fontData) noexcept {
	ImFontAtlas& fontAtlas = *ImGui::GetIO().Fonts;

	ImFontConfig config;
	config.FontDataOwnedByAtlas = false;

	const float fpsSize = 24 * _dpiScale;

	//////////////////////////////////////////////////////////
	//
	// NUMBER_RANGES + " FPS" -> _fontFPS
	// 
	//////////////////////////////////////////////////////////

#ifdef _DEBUG
	std::char_traits<char>::copy(config.Name, "_fontFPS", std::size(config.Name));
#endif

	// 等宽的数字字符
	config.MergeMode = false;
	config.GlyphMinAdvanceX = config.GlyphMaxAdvanceX = fpsSize * 0.42f;
	_fontFPS = fontAtlas.AddFontFromMemoryTTF(
		(void*)fontData.data(), (int)fontData.size(), fpsSize, &config, ImGuiHelper::NUMBER_RANGES);

	// 其他不等宽的字符
	config.MergeMode = true;
	config.GlyphMinAdvanceX = 0;
	config.GlyphMaxAdvanceX = std::numeric_limits<float>::max();
	fontAtlas.AddFontFromMemoryTTF(
		(void*)fontData.data(), (int)fontData.size(), fpsSize, &config, (const ImWchar*)L"  FFPPSS");
}

static std::string_view GetEffectDisplayName(const Renderer::EffectInfo* effectInfo) noexcept {
	auto delimPos = effectInfo->name.find_last_of('\\');
	if (delimPos == std::string::npos) {
		return effectInfo->name;
	} else {
		return std::string_view(effectInfo->name.begin() + delimPos + 1, effectInfo->name.end());
	}
}

bool OverlayDrawer::_DrawTimingItem(
	int& itemId,
	const char* text,
	const ImColor* color,
	float time,
	bool isExpanded
) const noexcept {
	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	const std::string timeStr = fmt::format("{:.3f} ms", time);
	const float timeWidth = _fontMonoNumbers->CalcTextSizeA(
		ImGui::GetFontSize(), FLT_MAX, 0.0f, timeStr.c_str()).x;

	// 计算布局
	static constexpr float spacingBeforeText = 3;
	static constexpr float spacingAfterText = 8;
	const float descWrapPos = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - timeWidth - spacingAfterText;
	const float descHeight = ImGui::CalcTextSize(
		text, nullptr, false, descWrapPos - ImGui::GetCursorPosX() - (color ? ImGui::CalcTextSize(COLOR_INDICATOR).x + spacingBeforeText : 0)).y;

	const float fontHeight = ImGui::GetFont()->FontSize;

	ImGui::PushID(itemId++);

	bool isHovered = false;
	if (color) {
		ImGui::Selectable("", false, 0, ImVec2(0, descHeight));
		if (ImGui::IsItemHovered()) {
			isHovered = true;
		}
		ImGui::SameLine(0, 0);

		ImGui::PushStyleColor(ImGuiCol_Text, (ImU32)*color);

		if (descHeight >= fontHeight * 2) {
			// 不知为何 SetCursorPos 不起作用
			// 所以这里使用占位竖直居中颜色框
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
			ImGui::BeginGroup();
			ImGui::Dummy(ImVec2(0, (descHeight - fontHeight) / 2));
			ImGui::TextUnformatted(COLOR_INDICATOR);
			ImGui::EndGroup();
			ImGui::PopStyleVar();
		} else {
			ImGui::TextUnformatted(COLOR_INDICATOR);
		}
		ImGui::PopStyleColor();

		ImGui::SameLine(0, spacingBeforeText);
	}

	ImGui::PushTextWrapPos(descWrapPos);
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
	ImGui::SameLine(0, 0);

	// 描述过长导致换行时竖直居中时间
	if (color && descHeight >= fontHeight * 2) {
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (descHeight - fontHeight) / 2);
	}

	if (isExpanded) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.5f));
	}

	ImGui::PushFont(_fontMonoNumbers);
	ImGui::SetCursorPosX(descWrapPos + spacingAfterText);
	ImGui::TextUnformatted(timeStr.c_str());
	ImGui::PopFont();

	if (isExpanded) {
		ImGui::PopStyleColor();
	}

	ImGui::PopID();

	return isHovered;
}

// 返回鼠标悬停的项的序号，未悬停于任何项返回 -1
int OverlayDrawer::_DrawEffectTimings(
	int& itemId,
	const _EffectDrawInfo& drawInfo,
	bool showPasses,
	std::span<const ImColor> colors,
	bool singleEffect
) const noexcept {
	int result = -1;

	showPasses &= drawInfo.passTimings.size() > 1;

	const std::string effectName = drawInfo.info->isFP16
		? StrHelper::Concat(GetEffectDisplayName(drawInfo.info), " (FP16)")
		: std::string(GetEffectDisplayName(drawInfo.info));
	if (_DrawTimingItem(
		itemId,
		effectName.c_str(),
		(!singleEffect && !showPasses) ? &colors[0] : nullptr,
		drawInfo.totalTime,
		showPasses
	)) {
		result = 0;
	}

	if (showPasses) {
		for (size_t j = 0; j < drawInfo.passTimings.size(); ++j) {
			ImGui::Indent(16);

			if (_DrawTimingItem(
				itemId,
				drawInfo.info->passNames[j].c_str(),
				&colors[j],
				drawInfo.passTimings[j]
			)) {
				result = (int)j;
			}

			ImGui::Unindent(16);
		}
	}

	return result;
}

void OverlayDrawer::_DrawTimelineItem(
	int& itemId,
	ImU32 color,
	float dpiScale,
	std::string_view name,
	float time,
	float effectsTotalTime,
	bool selected
) {
	ImGui::PushID(itemId++);

	ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, color);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, color);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, color);
	ImGui::PushStyleColor(ImGuiCol_Header, color);
	ImGui::Selectable("", selected);
	ImGui::PopStyleColor(3);

	if (ImGui::IsItemHovered() || ImGui::IsItemClicked()) {
		std::string content = fmt::format("{}\n{:.3f} ms\n{}%", name, time, std::lroundf(time / effectsTotalTime * 100));
		ImGui::PushFont(_fontMonoNumbers);
		ImGuiImpl::Tooltip(content.c_str(), 500 * dpiScale);
		ImGui::PopFont();
	}

	// 空间足够时显示文字
	std::string text;
	if (selected) {
		text = fmt::format("{}%", std::lroundf(time / effectsTotalTime * 100));
	} else {
		text.assign(name);
	}

	float textWidth = ImGui::CalcTextSize(text.c_str()).x;
	float itemWidth = ImGui::GetItemRectSize().x;
	float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
	if (itemWidth - (selected ? 0 : itemSpacing) > textWidth + 4 * _dpiScale) {
		ImGui::SameLine(0, 0);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (itemWidth - textWidth - itemSpacing) / 2);
		// 竖直方向居中
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 0.5f * _dpiScale);
		ImGui::TextUnformatted(text.c_str());
	}

	ImGui::PopID();
}

void OverlayDrawer::_DrawFPS(uint32_t fps) noexcept {
	static float oldOpacity = 0.0f;
	static float opacity = 0.0f;
	static bool isLocked = false;
	// 背景透明时绘制阴影
	const bool drawShadow = opacity < 1e-5f;

	static constexpr float PADDING_X = 5;
	static constexpr float PADDING_Y = 1;

	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowBgAlpha(opacity);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, drawShadow ? ImVec2() : ImVec2(PADDING_X, PADDING_Y));
	if (!ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing | (isLocked ? ImGuiWindowFlags_NoMove : 0) | (drawShadow ? ImGuiWindowFlags_NoBackground : 0))) {
		// Early out if the window is collapsed, as an optimization.
		ImGui::End();
		return;
	}

	if (oldOpacity != opacity) {
		// 透明时无边距，确保文字位置不变
		if (oldOpacity < 1e-5f) {
			if (opacity >= 1e-5f) {
				ImVec2 windowPos = ImGui::GetWindowPos();
				ImGui::SetWindowPos(ImVec2(windowPos.x - PADDING_X, windowPos.y - PADDING_Y));
			}
		} else {
			if (opacity < 1e-5f) {
				ImVec2 windowPos = ImGui::GetWindowPos();
				ImGui::SetWindowPos(ImVec2(windowPos.x + PADDING_X, windowPos.y + PADDING_Y));
			}
		}
		oldOpacity = opacity;
	}

	ImGui::PushFont(_fontFPS);

	ImVec2 cursorPos = ImGui::GetCursorPos();
	// 不知为何文字无法竖直居中，因此这里调整位置
	cursorPos.y -= 3;
	ImGui::SetCursorPosY(cursorPos.y);

	std::string fpsStr = fmt::format("{} FPS", fps);
	if (drawShadow) {
		ImGui::SetCursorPos(ImVec2(cursorPos.x + 1.0f, cursorPos.y + 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.8f));
		ImGui::TextUnformatted(fpsStr.c_str());
		ImGui::PopStyleColor();

		ImGui::SetCursorPos(cursorPos);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.6f));
		ImGui::TextUnformatted(fpsStr.c_str());
		ImGui::PopStyleColor();

		ImGui::SetCursorPos(cursorPos);
	}
	ImGui::TextUnformatted(fpsStr.c_str());

	ImGui::PopFont();

	ImGui::PopStyleVar();

	if (ImGui::BeginPopupContextWindow()) {
		ImGui::PushItemWidth(150 * _dpiScale);
		ImGui::PushFont(_fontMonoNumbers);
		ImGui::SliderFloat("##FPS_Opacity", &opacity, 0.0f, 1.0f);
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::TextUnformatted(_GetResourceString(L"Overlay_FPS_Opacity").c_str());
		ImGui::Separator();
		const std::string& lockStr = _GetResourceString(isLocked ? L"Overlay_FPS_Unlock" : L"Overlay_FPS_Lock");
		if (ImGui::MenuItem(lockStr.c_str(), nullptr, nullptr)) {
			isLocked = !isLocked;
		}
		ImGui::PopItemWidth();

		ImGui::EndPopup();
	}

	ImGui::End();
	ImGui::PopStyleVar();
}

// 返回 true 表示应再渲染一次
bool OverlayDrawer::_DrawUI(const SmallVector<float>& effectTimings, uint32_t fps) noexcept {
	const ScalingOptions& options = ScalingWindow::Get().Options();
	const Renderer& renderer = ScalingWindow::Get().Renderer();

	const uint32_t passCount = (uint32_t)_effectTimingsStatistics.size();

	bool needRedraw = false;
	
	// effectTimings 为空表示后端没有渲染新的帧
	if (!effectTimings.empty()) {
		steady_clock::time_point now = steady_clock::now();
		if (_lastUpdateTime == steady_clock::time_point{}) {
			// 后端渲染的第一帧
			_lastUpdateTime = now;

			for (uint32_t i = 0; i < passCount; ++i) {
				_lastestAvgEffectTimings[i] = effectTimings[i];
			}
		} else {
			if (now - _lastUpdateTime > 500ms) {
				// 更新间隔不少于 500ms，而不是 500ms 更新一次
				_lastUpdateTime = now;

				for (uint32_t i = 0; i < passCount; ++i) {
					auto& [total, count] = _effectTimingsStatistics[i];
					if (count > 0) {
						_lastestAvgEffectTimings[i] = total / count;
					}

					count = 0;
					total = 0;
				}
			}

			for (uint32_t i = 0; i < passCount; ++i) {
				auto& [total, count] = _effectTimingsStatistics[i];
				// 有时会跳过某些效果的渲染，即渲染时间为 0，这时不应计入
				if (effectTimings[i] > 1e-3) {
					++count;
					total += effectTimings[i];
				}
			}
		}
	}

#ifdef _DEBUG
	ImGui::ShowDemoWindow();
#endif

	{
		const float windowWidth = 310 * _dpiScale;
		ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, 0.0f), ImVec2(windowWidth, 500 * _dpiScale));

		static float initPosX = Win32Helper::GetSizeOfRect(renderer.DestRect()).cx - windowWidth;
		ImGui::SetNextWindowPos(ImVec2(initPosX, 20), ImGuiCond_FirstUseEver);
	}

	std::string profilerStr = _GetResourceString(L"Overlay_Profiler");
	if (!ImGui::Begin(profilerStr.c_str(), nullptr, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::End();
		return needRedraw;
	}

	ImGui::PushTextWrapPos();
	ImGui::TextUnformatted(StrHelper::Concat("GPU: ", _hardwareInfo.gpuName).c_str());
	const std::string& captureMethodStr = _GetResourceString(L"Overlay_Profiler_CaptureMethod");
	ImGui::TextUnformatted(StrHelper::Concat(captureMethodStr.c_str(), ": ", renderer.FrameSource().Name()).c_str());
	if (options.IsStatisticsForDynamicDetectionEnabled() &&
		options.duplicateFrameDetectionMode == DuplicateFrameDetectionMode::Dynamic) {
		const std::pair<uint32_t, uint32_t> statistics =
			renderer.FrameSource().GetStatisticsForDynamicDetection();
		ImGui::TextUnformatted(StrHelper::Concat(_GetResourceString(L"Overlay_Profiler_DynamicDetection"), ": ").c_str());
		ImGui::SameLine(0, 0);
		ImGui::PushFont(_fontMonoNumbers);
		ImGui::TextUnformatted(fmt::format("{}/{} ({:.1f}%)", statistics.first, statistics.second,
			statistics.second == 0 ? 0.0f : statistics.first * 100.0f / statistics.second).c_str());
		ImGui::PopFont();
	}
	const std::string& frameRateStr = _GetResourceString(L"Overlay_Profiler_FrameRate");
	ImGui::TextUnformatted(fmt::format("{}: {} FPS", frameRateStr, fps).c_str());
	ImGui::PopTextWrapPos();

	ImGui::Spacing();
	// 效果渲染用时
	const std::string& timingsStr = _GetResourceString(L"Overlay_Profiler_Timings");
	if (ImGui::CollapsingHeader(timingsStr.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		const std::vector<Renderer::EffectInfo>& effectInfos = renderer.EffectInfos();
		const uint32_t nEffect = (uint32_t)effectInfos.size();

		SmallVector<_EffectDrawInfo, 4> effectDrawInfos(effectInfos.size());

		{
			uint32_t idx = 0;
			for (uint32_t i = 0; i < nEffect; ++i) {
				auto& effectTiming = effectDrawInfos[i];
				effectTiming.info = &effectInfos[i];

				uint32_t nPass = (uint32_t)effectTiming.info->passNames.size();
				effectTiming.passTimings = { _lastestAvgEffectTimings.begin() + idx, nPass };
				idx += nPass;

				for (float t : effectTiming.passTimings) {
					effectTiming.totalTime += t;
				}
			}
		}

		float effectsTotalTime = 0.0f;
		for (const _EffectDrawInfo& drawInfo : effectDrawInfos) {
			effectsTotalTime += drawInfo.totalTime;
		}

		bool showSwitchButton = false;
		for (const _EffectDrawInfo& drawInfo : effectDrawInfos) {
			// 某个效果有多个通道，显示切换按钮
			if (drawInfo.passTimings.size() > 1) {
				showSwitchButton = true;
				break;
			}
		}

		static bool showPasses = false;
		if (showSwitchButton) {
			ImGui::Spacing();
			const std::string& buttonStr = _GetResourceString(showPasses
				? L"Overlay_Profiler_Timings_SwitchToEffects"
				: L"Overlay_Profiler_Timings_SwitchToPasses");
			if (ImGui::Button(buttonStr.c_str())) {
				showPasses = !showPasses;
				// 需要再次渲染以处理滚动条导致的布局变化
				needRedraw = true;
			}
		} else {
			showPasses = false;
		}

		SmallVector<ImColor, 4> colors;
		colors.reserve(_timelineColors.size());
		if (nEffect == 1) {
			colors.resize(_timelineColors.size());
			for (size_t i = 0; i < _timelineColors.size(); ++i) {
				colors[i] = TIMELINE_COLORS[_timelineColors[i]];
			}
		} else if (showPasses) {
			uint32_t i = 0;
			for (const _EffectDrawInfo& drawInfo : effectDrawInfos) {
				if (drawInfo.passTimings.size() == 1) {
					colors.push_back(TIMELINE_COLORS[_timelineColors[i]]);
					++i;
					continue;
				}

				++i;
				for (uint32_t j = 0; j < drawInfo.passTimings.size(); ++j) {
					colors.push_back(TIMELINE_COLORS[_timelineColors[i]]);
					++i;
				}
			}
		} else {
			size_t i = 0;
			for (const _EffectDrawInfo& drawInfo : effectDrawInfos) {
				colors.push_back(TIMELINE_COLORS[_timelineColors[i]]);

				++i;
				if (drawInfo.passTimings.size() > 1) {
					i += drawInfo.passTimings.size();
				}
			}
		}

		static int selectedIdx = -1;
		// 防止 ID 冲突
		int itemId = 0;

		if (nEffect > 1 || showPasses) {
			ImGui::Spacing();
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 5));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

			if (effectsTotalTime > 0) {
				if (showPasses) {
					if (ImGui::BeginTable("timeline", (int)passCount)) {
						for (uint32_t i = 0; i < passCount; ++i) {
							if (_lastestAvgEffectTimings[i] < 1e-3f) {
								continue;
							}

							ImGui::TableSetupColumn(
								std::to_string(i).c_str(),
								ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoReorder,
								_lastestAvgEffectTimings[i] / effectsTotalTime
							);
						}

						ImGui::TableNextRow();

						uint32_t i = 0;
						for (const _EffectDrawInfo& drawInfo : effectDrawInfos) {
							for (uint32_t j = 0, end = (uint32_t)drawInfo.passTimings.size(); j < end; ++j) {
								if (drawInfo.passTimings[j] < 1e-5f) {
									continue;
								}

								ImGui::TableNextColumn();

								std::string name;
								if (drawInfo.passTimings.size() == 1) {
									name = std::string(GetEffectDisplayName(drawInfo.info));
								} else if (nEffect == 1) {
									name = drawInfo.info->passNames[j];
								} else {
									name = StrHelper::Concat(
										GetEffectDisplayName(drawInfo.info), "/",
										drawInfo.info->passNames[j]
									);
								}

								_DrawTimelineItem(itemId, colors[i], _dpiScale, name, drawInfo.passTimings[j],
									effectsTotalTime, selectedIdx == (int)i);

								++i;
							}
						}

						ImGui::EndTable();
					}
				} else {
					if (ImGui::BeginTable("timeline", nEffect)) {
						for (uint32_t i = 0; i < nEffect; ++i) {
							if (effectDrawInfos[i].totalTime < 1e-5f) {
								continue;
							}

							ImGui::TableSetupColumn(
								std::to_string(i).c_str(),
								ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoReorder,
								effectDrawInfos[i].totalTime / effectsTotalTime
							);
						}

						ImGui::TableNextRow();

						for (uint32_t i = 0; i < nEffect; ++i) {
							auto& drawInfo = effectDrawInfos[i];
							if (drawInfo.totalTime < 1e-5f) {
								continue;
							}

							ImGui::TableNextColumn();
							_DrawTimelineItem(
								itemId,
								colors[i],
								_dpiScale,
								GetEffectDisplayName(drawInfo.info),
								drawInfo.totalTime,
								effectsTotalTime,
								selectedIdx == (int)i
							);
						}

						ImGui::EndTable();
					}
				}
			} else {
				// 还未统计出时间时渲染占位
				if (ImGui::BeginTable("timeline", 1)) {
					ImGui::TableSetupColumn("0", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoReorder);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					ImU32 color = ImColor();
					ImGui::PushStyleColor(ImGuiCol_HeaderActive, color);
					ImGui::PushStyleColor(ImGuiCol_HeaderHovered, color);
					ImGui::Selectable("");
					ImGui::PopStyleColor(2);

					ImGui::EndTable();
				}
			}

			ImGui::PopStyleVar(4);

			ImGui::Spacing();
		}
		
		selectedIdx = -1;

		if (ImGui::BeginTable("timings", 1, ImGuiTableFlags_PadOuterX)) {
			ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoReorder);

			if (nEffect == 1) {
				int hovered = _DrawEffectTimings(itemId, effectDrawInfos[0], showPasses, colors, true);
				if (hovered >= 0) {
					selectedIdx = hovered;
				}
			} else {
				int idx = 0;
				for (const _EffectDrawInfo& effectInfo : effectDrawInfos) {
					int idxBegin = idx;

					std::span<const ImColor> colorSpan;
					if (!showPasses || effectInfo.passTimings.size() == 1) {
						colorSpan = std::span(colors.begin() + idx, colors.begin() + idx + 1);
						++idx;
					} else {
						colorSpan = std::span(colors.begin() + idx, colors.begin() + idx + effectInfo.passTimings.size());
						idx += (int)effectInfo.passTimings.size();
					}

					int hovered = _DrawEffectTimings(itemId, effectInfo, showPasses, colorSpan, false);
					if (hovered >= 0) {
						selectedIdx = idxBegin + hovered;
					}
				}
			}

			ImGui::EndTable();
		}

		if (nEffect > 1) {
			ImGui::Separator();

			if (ImGui::BeginTable("total", 1, ImGuiTableFlags_PadOuterX)) {
				ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoReorder);

				_DrawTimingItem(itemId, _GetResourceString(L"Overlay_Profiler_Timings_Total").c_str(), nullptr, effectsTotalTime);

				ImGui::EndTable();
			}
		}
	}
	
	ImGui::End();
	return needRedraw;
}

const std::string& OverlayDrawer::_GetResourceString(const std::wstring_view& key) noexcept {
	static phmap::flat_hash_map<std::wstring_view, std::string> cache;

	if (auto it = cache.find(key); it != cache.end()) {
		return it->second;
	}

	return cache[key] = StrHelper::UTF16ToUTF8(_resourceLoader.GetString(key));
}

}
