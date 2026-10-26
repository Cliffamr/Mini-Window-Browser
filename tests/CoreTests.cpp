#include "../src/AppModel.h"
#include "../src/ConfigStore.h"
#include "../src/CoreLogic.h"
#include "../src/Diagnostics.h"
#include "../src/LogStore.h"


#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void Check(bool condition, const wchar_t* name) {
    ++checks;
    if (condition) {
        std::cout << "[PASS " << checks << "]\n";
    } else {
        std::cout << "[FAIL " << checks << "]\n";
        OutputDebugStringW(name);
        ++failures;
    }
}

void TestConfigMigration() {
    wchar_t tempDirectory[MAX_PATH]{};
    GetTempPathW(static_cast<DWORD>(std::size(tempDirectory)), tempDirectory);
    const std::filesystem::path path = std::filesystem::path(tempDirectory) /
        (L"XiaoChuang-CoreTests-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    DeleteFileW(path.c_str());
    WritePrivateProfileStringW(L"Settings", L"HoleRadius", L"-20", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"SnapThreshold", L"999", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"HoldPlaybackRate", L"8", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"HomeUrl", L"https://www.bilibili.com", path.c_str());
    WritePrivateProfileStringW(L"Session", L"TabCount", L"0", path.c_str());
    WritePrivateProfileStringW(L"Hotkeys", L"Immersion", L"0,48", path.c_str());

    xiaochuang::AppState state;
    const xiaochuang::ConfigStore store(path);
    Check(store.Load(state), L"旧配置可加载");
    Check(state.settings.holeRadius == 0, L"挖孔半径范围校验");
    Check(state.settings.holeRefreshMs == 16, L"旧配置挖孔刷新默认16ms");
    Check(state.settings.autoCleanLogs && state.settings.logRetentionDays == 7,
          L"旧配置日志清理默认开启且保留7天");
    Check(state.settings.blankPageCheckSeconds == 5, L"旧配置白屏检测等待默认5秒");
    Check(state.settings.snapThreshold == 200, L"吸附距离范围校验");
    Check(state.settings.holdPlaybackRate == 5, L"长按倍速范围校验");
    Check(state.settings.holeOpacityPercent == 100 &&
          state.settings.autoHideOpacityPercent == 100, L"新增透明度键默认迁移");
    Check(!state.settings.backgroundMediaHotkeys, L"后台媒体热键默认关闭");
    Check(!state.settings.autoFitVideoFullscreen, L"网页全屏比例适配默认关闭");
    Check(!state.settings.lockVideoFullscreenAspect, L"网页全屏比例锁定默认关闭");
    Check(!state.settings.maximizedTopDragEnabled, L"最大化顶部拖动恢复默认关闭");
    Check(state.hotkeys[xiaochuang::HotkeyIndex(xiaochuang::HotkeyAction::Immersion)].enabled,
          L"旧版双字段快捷键默认启用");
    Check(state.tabs.size() == 1, L"空旧会话自动生成主页标签");
    Check(!state.settings.fixedWebZoomEnabled && state.settings.fixedWebZoomPercent == 100,
          L"旧配置网页缩放默认100且不锁定");
    Check(xiaochuang::NormalizeInputUrl(L"ABOUT:BLANK") == L"about:blank",
          L"空白页地址不会误转为搜索");

    RECT rect{20, 30, 920, 630};
    Check(store.Save(state, rect, false), L"新配置可保存");
    Check(!std::filesystem::exists(path.wstring() + L".tmp"), L"配置原子保存后不遗留临时文件");
    std::wifstream privateConfig(path);
    const std::wstring privateContents((std::istreambuf_iterator<wchar_t>(privateConfig)),
                                       std::istreambuf_iterator<wchar_t>());
    privateConfig.close();
    Check(privateContents.find(L"TabCount=0") != std::wstring::npos &&
          privateContents.find(L"TabUrl0=") == std::wstring::npos,
          L"private mode tidak menyimpan riwayat tab");
    xiaochuang::AppState reloaded;
    Check(store.Load(reloaded), L"新配置可重新加载");
    Check(reloaded.tabs.size() == 1 && reloaded.tabs[0].url == reloaded.settings.homeUrl,
          L"private mode selalu mulai dari home tab");
    Check(reloaded.settings.holeOpacityPercent == 100, L"新增配置往返一致");
    Check(!reloaded.settings.autoFitVideoFullscreen, L"网页全屏比例适配配置往返一致");
    reloaded.settings.autoFitVideoFullscreen = true;
    reloaded.settings.lockVideoFullscreenAspect = true;
    reloaded.settings.maximizedTopDragEnabled = true;
    reloaded.settings.fixedWebZoomEnabled = true;
    reloaded.settings.fixedWebZoomPercent = 135;
    reloaded.settings.holeRefreshMs = 33;
    reloaded.settings.autoCleanLogs = false;
    reloaded.settings.logRetentionDays = 30;
    reloaded.settings.blankPageCheckSeconds = 12;
    Check(store.Save(reloaded, rect, false), L"最大化顶部拖动配置可保存");
    xiaochuang::AppState dragSettingReloaded;
    Check(store.Load(dragSettingReloaded) && dragSettingReloaded.settings.maximizedTopDragEnabled,
          L"最大化顶部拖动配置往返一致");
    Check(dragSettingReloaded.settings.autoFitVideoFullscreen &&
          dragSettingReloaded.settings.lockVideoFullscreenAspect,
          L"网页全屏自动适配与比例锁定配置往返一致");
    Check(dragSettingReloaded.settings.fixedWebZoomEnabled &&
          dragSettingReloaded.settings.fixedWebZoomPercent == 135, L"固定网页缩放配置往返一致");
    Check(dragSettingReloaded.settings.holeRefreshMs == 33, L"挖孔刷新间隔配置往返一致");
    Check(!dragSettingReloaded.settings.autoCleanLogs &&
          dragSettingReloaded.settings.logRetentionDays == 30,
          L"日志自动清理和30天保留配置往返一致");
    Check(dragSettingReloaded.settings.blankPageCheckSeconds == 12,
          L"白屏检测等待秒数配置往返一致");
    WritePrivateProfileStringW(L"Settings", L"HoleRefreshMs", L"1", path.c_str());
    xiaochuang::AppState holeRange;
    store.Load(holeRange);
    Check(holeRange.settings.holeRefreshMs == 10, L"挖孔刷新间隔下限校验");
    WritePrivateProfileStringW(L"Settings", L"HoleRefreshMs", L"200", path.c_str());
    store.Load(holeRange);
    Check(holeRange.settings.holeRefreshMs == 100, L"挖孔刷新间隔上限校验");
    WritePrivateProfileStringW(L"Settings", L"LogRetentionDays", L"999", path.c_str());
    xiaochuang::AppState logRange;
    store.Load(logRange);
    Check(logRange.settings.logRetentionDays == 7, L"非法日志保留时间恢复7天");
    WritePrivateProfileStringW(L"Settings", L"LogRetentionDays", L"30", path.c_str());
    store.Load(logRange);
    Check(!logRange.settings.autoCleanLogs && logRange.settings.logRetentionDays == 30,
          L"关闭自动清理后保留用户选择");
    WritePrivateProfileStringW(L"Settings", L"BlankPageCheckSeconds", L"0", path.c_str());
    xiaochuang::AppState checkRange;
    store.Load(checkRange);
    Check(checkRange.settings.blankPageCheckSeconds == 3, L"白屏检测等待下限");
    WritePrivateProfileStringW(L"Settings", L"BlankPageCheckSeconds", L"100", path.c_str());
    store.Load(checkRange);
    Check(checkRange.settings.blankPageCheckSeconds == 30, L"白屏检测等待上限");
    WritePrivateProfileStringW(L"Settings", L"FixedWebZoomPercent", L"0", path.c_str());
    xiaochuang::AppState zoomRange;
    store.Load(zoomRange);
    Check(zoomRange.settings.fixedWebZoomPercent == 25, L"网页缩放下限校验");
    WritePrivateProfileStringW(L"Settings", L"FixedWebZoomPercent", L"999", path.c_str());
    store.Load(zoomRange);
    Check(zoomRange.settings.fixedWebZoomPercent == 500, L"网页缩放上限校验");

    const std::array<UINT, xiaochuang::kHotkeyCount> auditKeys = {
        L'1', VK_OEM_3, L'3', L'4', VK_XBUTTON1, VK_XBUTTON2, L'9'};
    for (size_t index = 0; index < xiaochuang::kHotkeyCount; ++index) {
        reloaded.hotkeys[index].virtualKey = auditKeys[index];
        reloaded.hotkeys[index].modifiers = index % 2 == 0 ? MOD_CONTROL : 0;
        reloaded.hotkeys[index].enabled = index != xiaochuang::HotkeyIndex(
            xiaochuang::HotkeyAction::Previous);
    }
    Check(store.Save(reloaded, rect, false), L"全部快捷键绑定可保存");
    xiaochuang::AppState hotkeyReloaded;
    Check(store.Load(hotkeyReloaded), L"全部快捷键绑定可重新加载");
    bool allHotkeysRoundTrip = true;
    for (size_t index = 0; index < xiaochuang::kHotkeyCount; ++index) {
        allHotkeysRoundTrip = allHotkeysRoundTrip &&
            hotkeyReloaded.hotkeys[index].virtualKey == auditKeys[index] &&
            hotkeyReloaded.hotkeys[index].modifiers ==
                (index % 2 == 0 ? static_cast<UINT>(MOD_CONTROL) : 0U) &&
            hotkeyReloaded.hotkeys[index].enabled ==
                (index != xiaochuang::HotkeyIndex(xiaochuang::HotkeyAction::Previous));
    }
    Check(allHotkeysRoundTrip, L"键盘与鼠标侧键绑定完整往返且动作顺序不串位");

    WritePrivateProfileStringW(L"Hotkeys", L"Forward", L"4294967295,0", path.c_str());
    xiaochuang::AppState invalidHotkeyState;
    Check(store.Load(invalidHotkeyState) &&
          invalidHotkeyState.hotkeys[xiaochuang::HotkeyIndex(
              xiaochuang::HotkeyAction::SeekForward)].virtualKey == L'6',
          L"无效快捷键配置回退默认值");

    xiaochuang::Preset preset;
    preset.name = L"三标签";
    preset.tabs = {{L"哔哩哔哩", L"https://www.bilibili.com"},
                   {L"Douyin", L"https://www.douyin.com"},
                   {L"YouTube", L"https://www.youtube.com"}};
    preset.activeTab = 1;
    preset.currentUrl = preset.tabs[1].url;
    preset.autoFitVideoFullscreen = true;
    preset.lockVideoFullscreenAspect = true;
    preset.maximizedTopDragEnabled = true;
    preset.fixedWebZoomEnabled = true;
    preset.fixedWebZoomPercent = 80;
    preset.holeRefreshMs = 48;
    preset.blankPageCheckSeconds = 15;
    preset.hotkeys[xiaochuang::HotkeyIndex(xiaochuang::HotkeyAction::Next)].enabled = false;
    reloaded.presets = {preset};
    Check(store.Save(reloaded, rect, false), L"多标签预设可保存");
    xiaochuang::AppState presetReloaded;
    Check(store.Load(presetReloaded), L"多标签预设可加载");
    Check(presetReloaded.presets.size() == 1 && presetReloaded.presets[0].tabs.size() == 3,
          L"预设完整保存全部标签页");
    Check(presetReloaded.presets[0].activeTab == 1 &&
          presetReloaded.presets[0].tabs[1].title == L"Douyin",
          L"预设保存当前标签索引和标题");
    Check(presetReloaded.presets[0].autoFitVideoFullscreen &&
          presetReloaded.presets[0].lockVideoFullscreenAspect &&
          presetReloaded.presets[0].maximizedTopDragEnabled,
          L"预设保存网页全屏比例与顶部拖动设置");
    Check(!presetReloaded.presets[0].hotkeys[xiaochuang::HotkeyIndex(
              xiaochuang::HotkeyAction::Next)].enabled,
          L"预设保存逐项快捷键启用状态");
    Check(presetReloaded.presets[0].fixedWebZoomEnabled &&
          presetReloaded.presets[0].fixedWebZoomPercent == 80, L"预设独立保存网页缩放");
    Check(presetReloaded.presets[0].holeRefreshMs == 48, L"预设独立保存挖孔刷新间隔");
    Check(presetReloaded.presets[0].blankPageCheckSeconds == 15,
          L"预设独立保存白屏检测等待秒数");
    Check(!presetReloaded.settings.autoCleanLogs &&
          presetReloaded.settings.logRetentionDays == 30,
          L"场景预设不会覆盖全局日志保留策略");
    Check(presetReloaded.settings.blankPageCheckSeconds == 12,
          L"读取预设不能覆盖当前全局白屏检测等待设置");

    WritePrivateProfileSectionW(L"Presets", nullptr, path.c_str());
    WritePrivateProfileStringW(L"Presets", L"Count", L"1", path.c_str());
    WritePrivateProfileStringW(L"Presets", L"P0_Name", L"旧版预设", path.c_str());
    WritePrivateProfileStringW(L"Presets", L"P0_CurUrl", L"https://example.com/legacy", path.c_str());
    WritePrivateProfileStringW(L"Hotkeys", L"HideWin", L"0,57,0", path.c_str());
    WritePrivateProfileStringW(L"Presets", L"P0_HkHide", L"0,57", path.c_str());
    xiaochuang::AppState legacyPresetState;
    Check(store.Load(legacyPresetState), L"旧版单网址预设可迁移");
    Check(legacyPresetState.presets.size() == 1 &&
          legacyPresetState.presets[0].tabs.size() == 1 &&
          legacyPresetState.presets[0].tabs[0].url == L"https://example.com/legacy",
          L"旧版预设迁移为单标签数组");
    Check(!legacyPresetState.hotkeys[xiaochuang::HotkeyIndex(xiaochuang::HotkeyAction::ToggleHidden)].enabled &&
          legacyPresetState.presets[0].hotkeys[xiaochuang::HotkeyIndex(xiaochuang::HotkeyAction::ToggleHidden)].enabled,
          L"旧版预设热键不继承当前全局停用状态");
    Check(!legacyPresetState.presets[0].fixedWebZoomEnabled &&
          legacyPresetState.presets[0].fixedWebZoomPercent == 100, L"旧版预设缩放默认兼容");
    Check(legacyPresetState.presets[0].holeRefreshMs == 16, L"旧版预设挖孔刷新默认兼容");
    Check(legacyPresetState.presets[0].blankPageCheckSeconds == 5,
          L"旧版预设白屏检测等待默认5秒");
    DeleteFileW(path.c_str());
}

void TestWindowTransitions() {
    using namespace xiaochuang;
    WindowStateSnapshot state;
    state = ReduceWindowState(state, WindowEvent::ToggleMaximize, ImmersionStyle::Hole);
    Check(state.visibleMode == WindowMode::Maximized, L"普通窗口进入无缝最大化");
    state = ReduceWindowState(state, WindowEvent::ToggleMaximize, ImmersionStyle::Hole);
    Check(state.visibleMode == WindowMode::Normal, L"无缝最大化恢复");
    state = ReduceWindowState(state, WindowEvent::ToggleImmersion, ImmersionStyle::AutoHide);
    Check(state.visibleMode == WindowMode::ImmersionAutoHide, L"进入整体透明沉浸模式");
    state = ReduceWindowState(state, WindowEvent::Hide, ImmersionStyle::AutoHide);
    Check(state.hidden, L"沉浸状态可原子隐藏");
    state = ReduceWindowState(state, WindowEvent::Show, ImmersionStyle::AutoHide);
    Check(!state.hidden && state.visibleMode == WindowMode::ImmersionAutoHide, L"显示后恢复可见模式");
}

void TestVeilBrowserPolicy() {
    using namespace xiaochuang;
    Check(kMaxTabs == 5, L"Veil Browser membatasi lima tab");
    Check(CanCreateTab(0) && CanCreateTab(4) && !CanCreateTab(5),
          L"tab keenam ditolak");
    Check(IsSupportedWebNavigationUrl(L"https://example.com/path"), L"HTTPS diizinkan");
    Check(!IsSupportedWebNavigationUrl(L"https://[::1]/"), L"IPv6 ditolak fail-closed");
    Check(IsSupportedWebNavigationUrl(L"ABOUT:BLANK"), L"about:blank diizinkan");
    Check(!IsSupportedWebNavigationUrl(L"http://example.com"), L"HTTP ditolak");
    Check(!IsSupportedWebNavigationUrl(L"file:///C:/secret.txt"), L"file URL ditolak");
    Check(!IsSupportedWebNavigationUrl(L"https://"), L"HTTPS tanpa host ditolak");
    Check(!IsSupportedWebNavigationUrl(L"https://user@example.com"), L"userinfo ditolak");
    Check(!IsSupportedWebNavigationUrl(L"https://exa mple.com"), L"spasi host ditolak");
    Check(!IsSupportedWebNavigationUrl(L"https://example.com\\@evil.test"), L"backslash host ditolak");
    Check(!IsSupportedWebNavigationUrl(L"https://example..com"), L"label host kosong ditolak");
    Check(!IsSupportedWebNavigationUrl(L"https://example.com:bad"), L"port tidak valid ditolak");
    Check(!IsSupportedWebNavigationUrl(L"https://[::1"), L"IPv6 tidak lengkap ditolak");
    Check(!IsSupportedWebNavigationUrl(L"https://not:ipv6/"), L"host bertitik dua palsu ditolak");
    Check(TopmostInsertAfter(true) == HWND_TOPMOST &&
          TopmostInsertAfter(false) == HWND_NOTOPMOST,
          L"toggle always-on-top memilih z-order yang benar");
}

void TestHoldGestures() {
    using namespace xiaochuang;
    HoldGestureState gesture;
    gesture.Press(1000);
    Check(gesture.Tick(1399) == HoldGestureResult::None, L"400ms 前不触发长按");
    Check(gesture.Release() == HoldGestureResult::Tap, L"短按在松开时触发跳转");
    gesture.Press(2000);
    Check(gesture.Tick(2400) == HoldGestureResult::HoldStart, L"400ms 触发长按开始");
    Check(gesture.Tick(2600) == HoldGestureResult::None, L"长按开始只触发一次");
    Check(gesture.Release() == HoldGestureResult::HoldStop, L"松开完整恢复长按状态");
}

void TestInputAndMediaSelection() {
    using namespace xiaochuang;
    Check(IsEditableClassName(L"RichEditD2DPT"), L"识别原生富文本输入控件");
    Check(IsEditableClassName(L"Scintilla"), L"识别代码/聊天输入控件");
    Check(!IsEditableClassName(L"Chrome_WidgetWin_1"), L"非输入窗口不误判");
    Check(IsLikelyAutomationTextInput(true, true, true), L"明确可写的 UI Automation 编辑控件触发输入保护");
    Check(IsLikelyAutomationTextInput(true, true, false, true), L"Electron TextPattern 编辑控件触发输入保护");
    Check(!IsLikelyAutomationTextInput(true, true, false), L"仅有编辑语义的自绘画布不误判");
    Check(!IsLikelyAutomationTextInput(false, true, true), L"仅有可写 Value 的渲染表面不误判");
    Check(!IsLikelyAutomationTextInput(false, true, false, true), L"普通可聚焦网页文档不误判为输入框");
    Check(!ShouldInspectTextInputProcess(false, false), L"启动时忽略旧外部窗口的遗留输入焦点");
    Check(ShouldInspectTextInputProcess(false, true), L"新外部焦点事件后启用跨程序输入保护");
    Check(ShouldInspectTextInputProcess(true, false), L"应用自身输入控件无需等待外部焦点事件");
    Check(ShouldApplyWebTypingGuard(true, true), L"小窗前台网页输入框触发输入保护");
    Check(!ShouldApplyWebTypingGuard(true, false), L"小窗失焦后不保留网页输入状态阻断媒体热键");
    Check(ShouldRecoverStaleKeyDown(true, false, false), L"丢失抬起消息后下一次按键可自愈");
    Check(!ShouldRecoverStaleKeyDown(true, true, false), L"有效长按手势不会被重复按下打断");
    Check(ShouldForceReleaseMessageGesture(5000, false), L"消息型长按丢失抬起后超时恢复");
    Check(!ShouldForceReleaseMessageGesture(4999, false), L"有效长按在超时前保持状态");
    Check(!IsUsableTextCaret(RECT{0, 0, 0, 0}, RECT{0, 0, 800, 600}, true),
          L"外部窗口零尺寸残留光标不误判为输入");
    Check(!IsUsableTextCaret(RECT{10, 10, 11, 28}, RECT{0, 0, 800, 600}, false),
          L"不拥有键盘焦点的光标不触发输入保护");
    Check(IsUsableTextCaret(RECT{10, 10, 11, 28}, RECT{0, 0, 800, 600}, true),
          L"焦点窗口内有效文本光标触发输入保护");
    for (const int transparency : {0, 1, 25, 50, 99, 100}) {
        int transparentColumns = 0;
        for (int coordinate = 0; coordinate < 100; ++coordinate) {
            if (IsHoleMaskColumnTransparent(coordinate, transparency)) ++transparentColumns;
        }
        Check(transparentColumns == transparency, L"挖孔像素掩码精确匹配透明度百分比");
    }
    Check(IsHoleMaskColumnTransparent(-100, 50) ==
          IsHoleMaskColumnTransparent(0, 50), L"挖孔掩码支持屏幕外负坐标");
    Check(IsSupportedWebNavigationUrl(L"https://www.bilibili.com/video"), L"允许 HTTPS 网页地址");
    Check(IsSupportedWebNavigationUrl(L"ABOUT:BLANK"), L"允许空白页地址");
    Check(!IsSupportedWebNavigationUrl(L"javascript:alert(1)"), L"拒绝脚本伪协议导航");
    Check(SelectMediaSite(L"www.bilibili.com") == MediaSite::Bilibili, L"优先选择 Bilibili 适配器");
    Check(SelectMediaSite(L"www.douyin.com") == MediaSite::Douyin, L"选择抖音适配器");
    Check(SelectMediaSite(L"youtu.be") == MediaSite::YouTube, L"选择 YouTube 适配器");
    Check(SelectMediaSite(L"example.com") == MediaSite::GenericHtml5, L"未知网站回退 HTML5");

    const std::vector<VideoCandidate> videos = {
        {false, true, 800000, 10},
        {true, true, 300000, 20},
        {true, false, 2000000, 30},
    };
    const auto selected = SelectActiveVideo(videos);
    Check(selected && *selected == 1, L"活动视频兼顾播放、可见性与面积");
}

void TestRawMouseButtons() {
    using namespace xiaochuang;
    const auto sideOne = DecodeRawMouseButtons(RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP);
    Check(sideOne.size() == 2 && sideOne[0].virtualKey == VK_XBUTTON1 && sideOne[0].down &&
          sideOne[1].virtualKey == VK_XBUTTON1 && !sideOne[1].down,
          L"Raw Input 识别鼠标侧键1按下和松开");
    const auto sideTwo = DecodeRawMouseButtons(RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP);
    Check(sideTwo.size() == 2 && sideTwo[0].virtualKey == VK_XBUTTON2 && sideTwo[0].down &&
          sideTwo[1].virtualKey == VK_XBUTTON2 && !sideTwo[1].down,
          L"Raw Input 识别鼠标侧键2按下和松开");
    const auto middle = DecodeRawMouseButtons(RI_MOUSE_BUTTON_3_DOWN | RI_MOUSE_BUTTON_3_UP);
    Check(middle.size() == 2 && middle[0].virtualKey == VK_MBUTTON && middle[0].down &&
          middle[1].virtualKey == VK_MBUTTON && !middle[1].down,
          L"Raw Input 识别鼠标中键按下和松开");
    Check(DecodeRawMouseButtons(0).empty(), L"Raw Input 忽略无关鼠标移动");
}


void TestPresetNames() {
    using namespace xiaochuang;
    const std::vector<std::wstring> names = {L"原神", L"BILIBILI"};
    Check(TrimWhitespace(L"  场景名称 \r\n") == L"场景名称", L"预设名称去除首尾空白");
    Check(HasPresetNameConflict(names, L" 原神 "), L"预设名称拒绝空白变体重名");
    Check(HasPresetNameConflict(names, L"bilibili"), L"预设名称不区分拉丁字母大小写");
    Check(!HasPresetNameConflict(names, L"原神", 0), L"重命名时允许保留原名称");
    Check(HasPresetNameConflict(names, L"   "), L"预设名称拒绝纯空白");
}

void TestWindowGeometryHelpers() {
    using namespace xiaochuang;
    const RECT maximized{0, 0, 1920, 1040};
    const RECT normal{100, 100, 1100, 700};
    const RECT work{0, 0, 1920, 1040};
    const RECT restored = CalculateRestoredDragRect(maximized, normal, POINT{960, 20}, work, 68);
    Check(restored.left == 460 && restored.top == 0 && restored.right == 1460 && restored.bottom == 600,
          L"最大化拖动按光标比例恢复普通窗口");
    const SIZE wide = CalculateAspectFitWindowSize(1000, 68, 16.0 / 9.0, 1920, 1040);
    Check(wide.cx == 1000 && wide.cy == 631, L"16:9 视频自动计算小窗高度");
    const SIZE constrained = CalculateAspectFitWindowSize(1900, 68, 1.0, 1920, 1040);
    Check(constrained.cx == 972 && constrained.cy == 1040, L"视频比例适配不超出工作区");
    Check(!ShouldApplyVideoFullscreenFit(WindowMode::Maximized, true, true, 16.0 / 9.0),
          L"小窗全屏期间记录比例但暂不调整窗口");
    Check(ShouldApplyVideoFullscreenFit(WindowMode::Normal, true, true, 16.0 / 9.0),
          L"退出小窗全屏后仍在网页全屏时立即应用比例");
    const RECT monitor{0, 0, 1920, 1080};
    const RECT taskbarWork{0, 0, 1920, 1040};
    const RECT bottomRight = PositionAspectFitRect(
        RECT{1420, 560, 1920, 1040}, SIZE{800, 518}, monitor, taskbarWork, 20);
    Check(bottomRight.left == 1120 && bottomRight.top == 562 &&
          bottomRight.right == 1920 && bottomRight.bottom == 1080,
          L"视频比例适配保持显示器右下角锚定而不抬到任务栏上方");
    const RECT widthDriven = ConstrainAspectSizingRect(
        RECT{100, 100, 900, 900}, WMSZ_RIGHT, 68, 16.0 / 9.0);
    Check(widthDriven.left == 100 && widthDriven.right == 900 &&
          widthDriven.top == 100 && widthDriven.bottom == 618,
          L"全屏拖动左右边缘时保持视频比例");
    const RECT heightDriven = ConstrainAspectSizingRect(
        RECT{100, 100, 700, 600}, WMSZ_BOTTOM, 68, 16.0 / 9.0);
    Check(heightDriven.left == 100 && heightDriven.right == 868 &&
          heightDriven.top == 100 && heightDriven.bottom == 600,
          L"全屏拖动上下边缘时保持视频比例");
    const RECT topSnappedImmersion = CalculateImmersionRect(
        RECT{0, 0, 1000, 668}, monitor, 68, 20);
    Check(topSnappedImmersion.left == 0 && topSnappedImmersion.top == 0 &&
          topSnappedImmersion.right == 1000 && topSnappedImmersion.bottom == 600,
          L"顶部吸附窗口进入沉浸后内容贴紧显示器顶部");
    const RECT floatingImmersion = CalculateImmersionRect(
        RECT{100, 100, 1100, 768}, monitor, 68, 20);
    Check(floatingImmersion.top == 168 && floatingImmersion.bottom == 768,
          L"普通位置进入沉浸仍保持网页内容的屏幕位置");
}

void TestLogStore() {
    namespace fs = std::filesystem;
    wchar_t tempDirectory[MAX_PATH]{};
    GetTempPathW(static_cast<DWORD>(std::size(tempDirectory)), tempDirectory);
    const fs::path root = fs::path(tempDirectory) /
        (L"XiaoChuang-LogTests-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    xiaochuang::LogStore logger(root / L"config.ini");
    Check(logger.EnsureDirectory() && fs::exists(root / L"logs"),
          L"日志目录与config.ini同级创建");

    auto seed = [&](const wchar_t* name, int ageDays) {
        const auto path = logger.Directory() / name;
        { std::ofstream output(path, std::ios::binary); output << "old event"; }
        std::error_code ec;
        fs::last_write_time(path,
            fs::file_time_type::clock::now() - std::chrono::hours(24 * ageDays), ec);
        return !ec;
    };
    Check(seed(L"XiaoChuang-1999-01-01.log", 5) &&
          seed(L"XiaoChuang-1999-01-02.log", 15) &&
          seed(L"XiaoChuang-1999-01-03.log", 40) &&
          seed(L"keep-user-file.txt", 90), L"创建日志清理样本");
    logger.SetRetention(false, 3);
    Check(fs::exists(logger.Directory() / L"XiaoChuang-1999-01-03.log"),
          L"关闭自动清理时不删除旧日志");
    logger.SetRetention(true, 30);
    Check(!fs::exists(logger.Directory() / L"XiaoChuang-1999-01-03.log") &&
          fs::exists(logger.Directory() / L"XiaoChuang-1999-01-02.log"),
          L"30天自动清理只删除超过保留期的日志");
    logger.SetRetention(true, 7);
    Check(!fs::exists(logger.Directory() / L"XiaoChuang-1999-01-02.log") &&
          fs::exists(logger.Directory() / L"XiaoChuang-1999-01-01.log"),
          L"7天自动清理生效");
    logger.SetRetention(true, 3);
    Check(!fs::exists(logger.Directory() / L"XiaoChuang-1999-01-01.log"),
          L"3天自动清理生效");
    Check(fs::exists(logger.Directory() / L"keep-user-file.txt"),
          L"日志清理不会删除用户其他文件");
    logger.Write("webview.probe.timeout", "generation=3 serial=2");
    bool hasLog = false;
    for (const auto& entry : fs::directory_iterator(logger.Directory())) {
        if (entry.path().extension() != L".log") continue;
        std::ifstream input(entry.path(), std::ios::binary);
        const std::string data((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
        if (data.find("webview.probe.timeout") != std::string::npos &&
            data.find("generation=3 serial=2") != std::string::npos) hasLog = true;
    }
    Check(hasLog, L"按天生成含时间和数值事件的日志");
    fs::remove_all(root, error);
    Check(!fs::exists(root), L"日志测试文件夹可清理");
}

void TestScriptResultDecoding() {
    using namespace xiaochuang;
    Check(DecodeExecuteScriptString(L"\"hello\\nworld\"") == L"hello\nworld",
          L"ExecuteScript 结果解码常用转义");
    Check(DecodeExecuteScriptString(L"\"\\u4F60\\u597D\"") == L"你好",
          L"ExecuteScript 结果解码 Unicode");
    Check(DecodeExecuteScriptString(L"\"bad\\u12x4 tail\"") == L"bad\\u12x4 tail",
          L"ExecuteScript 结果保留损坏 Unicode 转义");
}

} // namespace

int wmain() {
    TestConfigMigration();
    TestLogStore();
    TestWindowTransitions();
    TestVeilBrowserPolicy();
    TestHoldGestures();
    TestInputAndMediaSelection();
    TestRawMouseButtons();

    TestPresetNames();
    TestWindowGeometryHelpers();
    TestScriptResultDecoding();
    if (failures == 0) {
        std::cout << "All XiaoChuang core tests passed.\n";
        return 0;
    }
    std::cout << failures << " test(s) failed.\n";
    return 1;
}
