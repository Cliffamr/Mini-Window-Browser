#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "Application.h"

#include "CoreLogic.h"
#include "Diagnostics.h"

#include "../Resource.h"

#include <WebView2EnvironmentOptions.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <wrl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::Make;

namespace xiaochuang {
namespace {

#ifdef XIAOCHUANG_SMOKE_TEST
constexpr wchar_t kMainWindowClass[] = L"XiaoChuangMainWindowV14Smoke";
constexpr wchar_t kSettingsWindowClass[] = L"XiaoChuangSettingsV14Smoke";
constexpr wchar_t kBookmarkWindowClass[] = L"XiaoChuangBookmarksV14Smoke";
constexpr wchar_t kPresetWindowClass[] = L"XiaoChuangPresetsV14Smoke";
constexpr wchar_t kSingleInstanceName[] = L"MiniWindowBrowser-v1.4-SmokeTest-SingleInstance";
#else
constexpr wchar_t kMainWindowClass[] = L"XiaoChuangMainWindowV14";
constexpr wchar_t kSettingsWindowClass[] = L"XiaoChuangSettingsV14";
constexpr wchar_t kBookmarkWindowClass[] = L"XiaoChuangBookmarksV14";
constexpr wchar_t kPresetWindowClass[] = L"XiaoChuangPresetsV14";
constexpr wchar_t kSingleInstanceName[] = L"MiniWindowBrowser-v1.4-SingleInstance";
#endif
constexpr UINT kCloseCurrentTabCommand = 51001;
constexpr UINT kCloseOtherTabsCommand = 51002;
constexpr UINT kCloseAllTabsCommand = 51003;
constexpr UINT kRepairWebViewCommand = 51004;
constexpr DWORD kWdaExcludeFromCapture = 0x00000011;
constexpr wchar_t kTemporaryProfilePrefix[] = L"VeilBrowser-";

std::wstring WindowText(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::vector<wchar_t> buffer(static_cast<size_t>(std::max(length, 0)) + 1U, L'\0');
    GetWindowTextW(window, buffer.data(), static_cast<int>(buffer.size()));
    return buffer.data();
}

void SetCheck(HWND control, bool checked) {
    SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool IsChecked(HWND control) {
    return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SelectComboValue(HWND combo, int index) {
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

int ComboValue(HWND combo, int fallback = 0) {
    const LRESULT result = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return result == CB_ERR ? fallback : static_cast<int>(result);
}

int ParseInteger(HWND edit, int fallback) {
    const std::wstring text = WindowText(edit);
    if (text.empty()) return fallback;
    wchar_t* end = nullptr;
    const long value = wcstol(text.c_str(), &end, 10);
    return end == text.c_str() ? fallback : static_cast<int>(value);
}

bool IsMouseVirtualKey(UINT virtualKey) {
    return virtualKey == VK_MBUTTON || virtualKey == VK_XBUTTON1 || virtualKey == VK_XBUTTON2;
}

std::wstring FormatTabTitle(const std::wstring& title) {
    return title.empty() ? L"Loading / Memuat…" : title;
}

bool IsPlaceholderTabTitle(const std::wstring& title) {
    return title.empty() || title == L"Home / Beranda" || title == L"New tab / Tab baru" || title == L"Loading / Memuat…";
}

BOOL CALLBACK ApplyControlTheme(HWND control, LPARAM useDarkValue) {
    const bool useDark = useDarkValue != 0;
    SetWindowTheme(control, useDark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SendMessageW(control, WM_THEMECHANGED, 0, 0);
    InvalidateRect(control, nullptr, TRUE);
    return TRUE;
}

void ApplyThemeToWindowTree(HWND window, bool useDark) {
    if (!window || !IsWindow(window)) return;
    SetWindowTheme(window, useDark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    EnumChildWindows(window, ApplyControlTheme, useDark ? 1 : 0);
    SendMessageW(window, WM_THEMECHANGED, 0, 0);
    RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

} // namespace

int Application::Run(HINSTANCE instance, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        ShowError(L"Startup failed / Gagal memulai", L"Cannot initialize COM / Tidak dapat menginisialisasi COM.", comResult);
        return 1;
    }
    if (!Initialize(instance, showCommand)) {
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (captureAction_ && settingsWindow_ &&
            (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) &&
            (message.hwnd == settingsWindow_ || IsChild(settingsWindow_, message.hwnd))) {
            if (HandleHotkeyCaptureKey(static_cast<UINT>(message.wParam))) continue;
        }
        const auto dispatchDialog = [&message](HWND dialog) {
            return dialog && IsWindowVisible(dialog) &&
                (message.hwnd == dialog || IsChild(dialog, message.hwnd)) &&
                IsDialogMessageW(dialog, &message);
        };
        if (dispatchDialog(settingsWindow_) || dispatchDialog(bookmarkWindow_) ||
            dispatchDialog(presetWindow_)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    Shutdown();
    if (SUCCEEDED(comResult)) CoUninitialize();
    CleanupTemporaryProfile();
    return static_cast<int>(message.wParam);
}

bool Application::Initialize(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    singleInstanceMutex_ = CreateMutexW(nullptr, TRUE, kSingleInstanceName);
    if (!singleInstanceMutex_) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kMainWindowClass, nullptr)) {
            PostMessageW(existing, kShowExistingInstance, 0, 0);
        }
        return false;
    }

    config_.Load(state_);
    state_.settings.backgroundMediaHotkeys = false;
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        if (IsMediaAction(static_cast<HotkeyAction>(index))) state_.hotkeys[index].enabled = false;
    }
    logs_.SetRetention(state_.settings.autoCleanLogs, state_.settings.logRetentionDays);
    logs_.Write("application.start",
        "tabs=" + std::to_string(state_.tabs.size()) +
        " auto_cleanup=" + std::to_string(state_.settings.autoCleanLogs ? 1 : 0) +
        " retention_days=" + std::to_string(state_.settings.logRetentionDays) +
        " check_seconds=" + std::to_string(state_.settings.blankPageCheckSeconds));
    for (const TabData& tab : state_.tabs) {
        if (!tab.url.empty() && !IsPlaceholderTabTitle(tab.title)) {
            knownPageTitles_[NormalizeComparableUrl(tab.url)] = tab.title;
        }
    }
    theme_.SetMode(state_.settings.themeMode);
    dpi_ = GetDpiForSystem();
    UpdateDpi(dpi_);

    INITCOMMONCONTROLSEX controls{sizeof(controls),
        ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    RegisterWindowClasses();

    largeIcon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_MY), IMAGE_ICON,
                                                48, 48, LR_DEFAULTCOLOR));
    smallIcon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_SMALL), IMAGE_ICON,
                                                24, 24, LR_DEFAULTCOLOR));
    const RECT initial = state_.session.normalRect;
    const DWORD mainExStyle = (state_.settings.alwaysOnTop ? WS_EX_TOPMOST : 0) | WS_EX_APPWINDOW;
    mainWindow_ = CreateWindowExW(mainExStyle, kMainWindowClass,
        L"Veil Browser", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        initial.left, initial.top, initial.right - initial.left, initial.bottom - initial.top,
        nullptr, nullptr, instance_, this);
    if (!mainWindow_) {
        ShowError(L"Startup failed / Gagal memulai", L"Cannot create main window / Tidak dapat membuat jendela utama.", HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    if (largeIcon_) SendMessageW(mainWindow_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon_));
    if (smallIcon_) SendMessageW(mainWindow_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon_));
    if (!ConfigureCaptureExclusion(mainWindow_)) {
        ShowError(L"Veil Browser", L"Capture protection is unavailable. The window will stay closed.");
        DestroyWindow(mainWindow_);
        mainWindow_ = nullptr;
        return false;
    }
    if (!PrepareTemporaryProfile()) {
        ShowError(L"Veil Browser", L"Temporary private profile could not be created.");
        DestroyWindow(mainWindow_);
        mainWindow_ = nullptr;
        return false;
    }

    windowModes_.Initialize(mainWindow_, &state_.settings, state_.session.normalRect,
        state_.session.maximized, chromeHeight_,
        [this](bool visible) { ShowChrome(visible); },
        [this]() { LayoutMainControls(); });
    hotkeysStarted_ = hotkeys_.Start(mainWindow_, &state_.hotkeys,
        [this](HotkeyAction action, HotkeyGesture gesture) { ExecuteMediaAction(action, gesture); });
    // Register defaults before the window is shown. InputGuard used to sample the
    // launcher/editor focus here and immediately unregister every hotkey.
    RefreshHotkeys();
    RefreshTrayMode();
    ApplyThemeToAllWindows();
    InitializeWebView();

    ShowWindow(mainWindow_, showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand);
    UpdateWindow(mainWindow_);
    inputGuardStarted_ = inputGuard_.Start(mainWindow_, [this](bool) { RefreshHotkeys(); });
    inputGuard_.SetNativeEdit(addressEdit_);
    inputGuard_.SetEnabled(state_.settings.disableHotkeysOnTyping);
    SetTimer(mainWindow_, kInputFallbackTimerId, 1000, nullptr);
    lastHeartbeatTick_ = GetTickCount64();
    inputGuard_.Refresh();
    RefreshHotkeys();
    windowModes_.ReassertTopmost();
    return true;
}

void Application::Shutdown() {
    if (shuttingDown_) return;
    SaveConfiguration();
    logs_.Write("application.shutdown", "tabs=" + std::to_string(state_.tabs.size()));
    shuttingDown_ = true;
    lifetime_->store(false);
    hotkeys_.Stop();
    inputGuard_.Stop();
    windowModes_.Shutdown();
    RemoveTrayIcon();
    CloseWebView();
    CleanupTemporaryProfile();
    if (uiFont_) { DeleteObject(uiFont_); uiFont_ = nullptr; }
    if (titleFont_) { DeleteObject(titleFont_); titleFont_ = nullptr; }
    if (largeIcon_) { DestroyIcon(largeIcon_); largeIcon_ = nullptr; }
    if (smallIcon_) { DestroyIcon(smallIcon_); smallIcon_ = nullptr; }
    if (singleInstanceMutex_) { CloseHandle(singleInstanceMutex_); singleInstanceMutex_ = nullptr; }
}

void Application::RegisterWindowClasses() {
    WNDCLASSEXW mainClass{sizeof(mainClass)};
    mainClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    mainClass.lpfnWndProc = MainWindowProc;
    mainClass.hInstance = instance_;
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mainClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_MY));
    mainClass.hIconSm = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_SMALL));
    mainClass.lpszClassName = kMainWindowClass;
    RegisterClassExW(&mainClass);

    WNDCLASSEXW popupClass{sizeof(popupClass)};
    popupClass.style = CS_HREDRAW | CS_VREDRAW;
    popupClass.hInstance = instance_;
    popupClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    popupClass.hIcon = mainClass.hIcon;
    popupClass.hIconSm = mainClass.hIconSm;
    popupClass.lpszClassName = kSettingsWindowClass;
    popupClass.lpfnWndProc = SettingsWindowProc;
    RegisterClassExW(&popupClass);
    popupClass.lpszClassName = kBookmarkWindowClass;
    popupClass.lpfnWndProc = BookmarkWindowProc;
    RegisterClassExW(&popupClass);
    popupClass.lpszClassName = kPresetWindowClass;
    popupClass.lpfnWndProc = PresetWindowProc;
    RegisterClassExW(&popupClass);
}

void Application::CreateMainControls() {
    addressEdit_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, mainWindow_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Address)), instance_, nullptr);
    oldAddressProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        addressEdit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(AddressProc)));
    SetWindowLongPtrW(addressEdit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    tabControl_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH | TCS_OWNERDRAWFIXED | TCS_FOCUSNEVER,
        0, 0, 0, 0, mainWindow_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Tabs)), instance_, nullptr);
    SetWindowSubclass(tabControl_, TabSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    backButton_ = CreateThemedButton(mainWindow_, L"Back / Kembali", Back);
    forwardButton_ = CreateThemedButton(mainWindow_, L"Next / Lanjut", Forward);
    reloadButton_ = CreateThemedButton(mainWindow_, L"Reload / Muat", Reload);
    goButton_ = CreateThemedButton(mainWindow_, L"Go / Buka", Go);
    addTabButton_ = CreateThemedButton(mainWindow_, L"＋", AddTab);
    topmostButton_ = CreateThemedButton(mainWindow_, L"Top / Atas", Topmost);
    clickThroughButton_ = CreateThemedButton(mainWindow_, L"Pass / Tembus", ClickThrough);
    minimizeButton_ = CreateThemedButton(mainWindow_, L"—", Minimize);
    maximizeButton_ = CreateThemedButton(mainWindow_, L"□", Maximize);
    closeButton_ = CreateThemedButton(mainWindow_, L"×", Close);
    starButton_ = CreateThemedButton(mainWindow_, L"☆", Star);
    bookmarkButton_ = CreateThemedButton(mainWindow_, L"Marks / Markah", Bookmarks);
    presetButton_ = CreateThemedButton(mainWindow_, L"Scenes / Skenario", Presets);
    settingsButton_ = CreateThemedButton(mainWindow_, L"Settings / Pengaturan", Settings);
    SetWindowTextW(topmostButton_, state_.settings.alwaysOnTop
        ? L"Top:on / Atas:aktif" : L"Top:off / Atas:nonaktif");
    SetWindowTextW(clickThroughButton_, windowModes_.IsImmersive()
        ? L"Pass:on / Tembus:aktif" : L"Pass:off / Tembus:nonaktif");

    const HWND controls[] = {addressEdit_, tabControl_, backButton_, forwardButton_, reloadButton_,
        goButton_, addTabButton_, topmostButton_, clickThroughButton_, minimizeButton_,
        maximizeButton_, closeButton_, starButton_, bookmarkButton_, presetButton_, settingsButton_};
    for (HWND control : controls) SetControlFont(control);
    RebuildTabControl();
}

void Application::LayoutMainControls() {
    if (!mainWindow_) return;
    RECT client{};
    GetClientRect(mainWindow_, &client);
    const int width = client.right;
    const bool chromeVisible = IsWindowVisible(tabControl_) != FALSE;
    if (chromeVisible) {
        const int leftMargin = Scale(4);
        const int captionButtonWidth = Scale(36);
        const int buttonY = Scale(1);
        const int buttonHeight = tabRowHeight_ - Scale(2);
        int right = width;
        MoveWindow(closeButton_, right - captionButtonWidth, buttonY, captionButtonWidth, buttonHeight, TRUE);
        right -= captionButtonWidth;
        MoveWindow(maximizeButton_, right - captionButtonWidth, buttonY, captionButtonWidth, buttonHeight, TRUE);
        right -= captionButtonWidth;
        MoveWindow(minimizeButton_, right - captionButtonWidth, buttonY, captionButtonWidth, buttonHeight, TRUE);
        right -= captionButtonWidth;
        MoveWindow(clickThroughButton_, right - Scale(132), buttonY, Scale(132), buttonHeight, TRUE);
        right -= Scale(132);
        MoveWindow(topmostButton_, right - Scale(116), buttonY, Scale(116), buttonHeight, TRUE);
        right -= Scale(116);
        MoveWindow(addTabButton_, right - captionButtonWidth, buttonY,
                   captionButtonWidth, buttonHeight, TRUE);
        right -= captionButtonWidth;

        const int dragReserve = Scale(72);
        const int available = std::max(0, right - leftMargin);
        const int stripCapacity = std::max(0, available - dragReserve);
        const int tabCount = std::max(1, static_cast<int>(state_.tabs.size()));
        const int itemWidth = std::min(Scale(132),
            std::max(1, (stripCapacity - Scale(6)) / tabCount));
        const int tabStripWidth = std::min(stripCapacity, itemWidth * tabCount + Scale(6));
        MoveWindow(tabControl_, leftMargin, Scale(1), tabStripWidth,
                   tabRowHeight_ - Scale(1), TRUE);
        SendMessageW(tabControl_, TCM_SETITEMSIZE, 0,
                     MAKELPARAM(itemWidth, tabRowHeight_ - Scale(5)));

        const int navigationY = tabRowHeight_;
        const int navigationButtonHeight = navigationRowHeight_ - Scale(8);
        const int gap = Scale(5);
        const int margin = Scale(7);
        const int smallWidth = Scale(34);
        const int textWidth = Scale(84);
        const bool compact = width < Scale(900);
        const HWND optionalButtons[] = {
            starButton_, bookmarkButton_, presetButton_, settingsButton_};
        for (const HWND button : optionalButtons) {
            ShowWindow(button, compact ? SW_HIDE : SW_SHOWNA);
        }
        const HWND navigationButtons[] = {backButton_, forwardButton_, reloadButton_, goButton_};
        for (HWND button : navigationButtons) ShowWindow(button, compact ? SW_HIDE : SW_SHOWNA);
        if (compact) {
            const int compactMargin = Scale(4);
            const int compactGoWidth = Scale(38);
            const int goX = std::max(compactMargin, width - compactMargin - compactGoWidth);
            ShowWindow(goButton_, SW_SHOWNA);
            MoveWindow(goButton_, goX, navigationY + Scale(4), compactGoWidth,
                       navigationButtonHeight, TRUE);
            MoveWindow(addressEdit_, compactMargin, navigationY + Scale(7),
                       std::max(1, goX - compactMargin - Scale(4)),
                       navigationRowHeight_ - Scale(14), TRUE);
        } else {
            int xLeft = margin;
            auto placeLeft = [&](HWND button, int buttonWidth) {
                MoveWindow(button, xLeft, navigationY + Scale(4), buttonWidth,
                           navigationButtonHeight, TRUE);
                xLeft += buttonWidth + gap;
            };
            placeLeft(backButton_, textWidth);
            placeLeft(forwardButton_, textWidth);
            placeLeft(reloadButton_, Scale(102));
            int xRight = width - margin;
            auto placeRight = [&](HWND button, int buttonWidth) {
                xRight -= buttonWidth;
                MoveWindow(button, xRight, navigationY + Scale(4), buttonWidth,
                           navigationButtonHeight, TRUE);
                xRight -= gap;
            };
            placeRight(settingsButton_, Scale(130));
            placeRight(presetButton_, Scale(112));
            placeRight(bookmarkButton_, Scale(102));
            placeRight(starButton_, smallWidth);
            placeRight(goButton_, textWidth);
            MoveWindow(addressEdit_, xLeft, navigationY + Scale(7),
                       std::max(Scale(80), xRight - xLeft - gap),
                       navigationRowHeight_ - Scale(14), TRUE);
        }
    }
    ResizeWebView();
    InvalidateRect(mainWindow_, nullptr, FALSE);
}

void Application::ShowChrome(bool visible) {
    const HWND controls[] = {addressEdit_, tabControl_, backButton_, forwardButton_, reloadButton_,
        goButton_, addTabButton_, topmostButton_, clickThroughButton_, minimizeButton_,
        maximizeButton_, closeButton_, starButton_, bookmarkButton_, presetButton_, settingsButton_};
    for (HWND control : controls) {
        if (control) ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    }
}

void Application::ApplyThemeToAllWindows() {
    const HWND windows[] = {mainWindow_, settingsWindow_, bookmarkWindow_, presetWindow_};
    for (HWND window : windows) {
        if (!window || !IsWindow(window)) continue;
        theme_.ApplyWindowTheme(window);
        ApplyThemeToWindowTree(window, theme_.IsDark());
    }
    windowModes_.RefreshFrameAppearance();
}

int Application::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

void Application::UpdateDpi(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    tabRowHeight_ = Scale(32);
    navigationRowHeight_ = Scale(36);
    chromeHeight_ = tabRowHeight_ + navigationRowHeight_;
    if (uiFont_) DeleteObject(uiFont_);
    if (titleFont_) DeleteObject(titleFont_);
    uiFont_ = CreateFontW(-Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    titleFont_ = CreateFontW(-Scale(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
}

LRESULT CALLBACK Application::MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
        if (application) application->mainWindow_ = window;
    }
    return application ? application->HandleMainMessage(window, message, wParam, lParam)
                       : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleMainMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_CREATE:
        CreateMainControls();
        return 0;
    case WM_NCHITTEST:
        return HitTest(lParam);
    case WM_NCPAINT:
        if (windowModes_.IsSeamlessMaximized()) return 0;
        break;
    case WM_NCACTIVATE:
        if (windowModes_.IsSeamlessMaximized()) return TRUE;
        break;
    case WM_NCLBUTTONDOWN:
        if (wParam == HTCAPTION && windowModes_.IsSeamlessMaximized() &&
            state_.settings.maximizedTopDragEnabled) {
            const POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (windowModes_.RestoreMaximizedForDrag(cursor)) {
                SetWindowTextW(maximizeButton_, L"□");
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }
        break;
    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION &&
            (!windowModes_.IsSeamlessMaximized() || state_.settings.maximizedTopDragEnabled)) {
            windowModes_.ToggleMaximized();
            SetWindowTextW(maximizeButton_, windowModes_.IsSeamlessMaximized() ? L"❐" : L"□");
            return 0;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = windowModes_.IsImmersive()
            ? POINT{Scale(64), Scale(64)} : POINT{Scale(160), Scale(120)};
        return 0;
    }
    case WM_DPICHANGED: {
        UpdateDpi(HIWORD(wParam));
        windowModes_.SetChromeHeight(chromeHeight_);
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        const HWND controls[] = {addressEdit_, tabControl_, backButton_, forwardButton_, reloadButton_,
            goButton_, addTabButton_, topmostButton_, clickThroughButton_, minimizeButton_,
            maximizeButton_, closeButton_, starButton_, bookmarkButton_, presetButton_, settingsButton_};
        for (HWND control : controls) SetControlFont(control);
        LayoutMainControls();
        return 0;
    }
    case WM_PAINT:
        PaintMainWindow();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        DrawOwnerItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme_.Palette().text);
        SetBkColor(dc, theme_.Palette().surface);
        return reinterpret_cast<LRESULT>(theme_.SurfaceBrush());
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme_.Palette().text);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(theme_.WindowBrush());
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == Go) NavigateAddressBar();
        else if (id == Back && webView_) webView_->GoBack();
        else if (id == Forward && webView_) webView_->GoForward();
        else if (id == Reload && webView_) webView_->Reload();
        else if (id == AddTab) CreateNewTab(state_.settings.homeUrl, L"New tab", true);
        else if (id == Topmost) {
            windowModes_.ToggleAlwaysOnTop();
            SetWindowTextW(topmostButton_, windowModes_.IsAlwaysOnTop()
                ? L"Top:on / Atas:aktif" : L"Top:off / Atas:nonaktif");
            SaveConfiguration();
        }
        else if (id == ClickThrough) {
            ToggleImmersion();
            SetWindowTextW(clickThroughButton_, windowModes_.IsImmersive()
                ? L"Pass:on / Tembus:aktif" : L"Pass:off / Tembus:nonaktif");
        }
        else if (id == Minimize) MinimizeWindow();
        else if (id == Maximize) { windowModes_.ToggleMaximized(); SetWindowTextW(maximizeButton_, windowModes_.IsSeamlessMaximized() ? L"❐" : L"□"); }
        else if (id == Close) SendMessageW(window, WM_CLOSE, 0, 0);
        else if (id == Star) ToggleCurrentBookmark();
        else if (id == Bookmarks) ShowBookmarkMenu();
        else if (id == Presets) ShowPresetWindow();
        else if (id == Settings) ShowSettingsWindow();
        return 0;
    }
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header && header->idFrom == Tabs && header->code == TCN_SELCHANGE) {
            SwitchTab(TabCtrl_GetCurSel(tabControl_));
        }
        return 0;
    }
    case WM_MOVING:
        windowModes_.SnapMovingRect(reinterpret_cast<RECT*>(lParam),
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
        return TRUE;
    case WM_SIZING:
        if (windowModes_.ConstrainWebFullscreenSizing(
                static_cast<UINT>(wParam), reinterpret_cast<RECT*>(lParam))) {
            return TRUE;
        }
        break;
    case WM_EXITSIZEMOVE:
        windowModes_.UpdateNormalRectFromWindow();
        SaveConfiguration();
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            if (state_.settings.useSystemTray) SetHidden(true);
            return 0;
        }
        if (!windowModes_.IsHidden() && webViewController_) {
            webViewController_->put_IsVisible(TRUE);
        }
        LayoutMainControls();
        return 0;
    case WM_WINDOWPOSCHANGED:
        if (const auto* position = reinterpret_cast<const WINDOWPOS*>(lParam)) {
            const bool moved = (position->flags & SWP_NOMOVE) == 0;
            const bool sized = (position->flags & SWP_NOSIZE) == 0;
            if (sized) LayoutMainControls();
            if (webViewController_ && (moved || sized)) {
                webViewController_->NotifyParentWindowPositionChanged();
            }
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            windowModes_.ReassertTopmost();
        }
        if (windowModes_.IsSeamlessMaximized()) {
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0U) == SC_RESTORE && IsIconic(window)) {
            break;
        }
        if ((wParam & 0xFFF0U) == SC_MAXIMIZE ||
            ((wParam & 0xFFF0U) == SC_RESTORE && windowModes_.IsSeamlessMaximized())) {
            if (!windowModes_.IsImmersive()) {
                const bool wantsMaximized = (wParam & 0xFFF0U) == SC_MAXIMIZE;
                if (wantsMaximized != windowModes_.IsSeamlessMaximized()) windowModes_.ToggleMaximized();
                SetWindowTextW(maximizeButton_, windowModes_.IsSeamlessMaximized() ? L"❐" : L"□");
            }
            return 0;
        }
        if ((wParam & 0xFFF0U) == SC_MINIMIZE && state_.settings.useSystemTray) {
            SetHidden(true);
            return 0;
        }
        break;
    case WM_HOTKEY:
        hotkeys_.HandleHotkeyMessage(static_cast<int>(wParam));
        return 0;
    case kMessageMouseHotkey:
        hotkeys_.HandleMouseMessage(wParam, lParam);
        return 0;
    case kMessageKeyboardHotkey:
        hotkeys_.HandleKeyboardMessage(wParam, lParam);
        return 0;
    case kMessageInputFocusChanged:
        SetTimer(window, kInputDebounceTimerId, 180, nullptr);
        return 0;
    case kMessageInputGuardResult:
        inputGuard_.HandleAsyncResult(lParam);
        return 0;
    case kCloseTabMessage:
        CloseTab(static_cast<int>(wParam));
        return 0;
    case kDeferredNewTabMessage: {
        std::unique_ptr<std::wstring> uri(reinterpret_cast<std::wstring*>(lParam));
        if (uri && !uri->empty()) CreateNewTab(*uri, SuggestedTitleForUrl(*uri), true);
        return 0;
    }
    case kRecreateWebViewMessage:
        RecreateWebView();
        return 0;
    case kShowRegistrationErrors:
        ShowHotkeyErrors();
        return 0;
    case WM_TIMER:
        if (wParam == kHotkeyHoldTimerId) hotkeys_.Tick();
        else if (wParam == kImmersionTimerId) windowModes_.OnImmersionTimer();
        else if (wParam == kInputFallbackTimerId) {
            const ULONGLONG now = GetTickCount64();
            if (lastHeartbeatTick_ != 0) {
                const ULONGLONG gap = now - lastHeartbeatTick_;
                maximumHeartbeatDelay_ = std::max(maximumHeartbeatDelay_, gap);
                if (gap > 3000) {
                    logs_.Write("ui.heartbeat_gap",
                        "elapsed_ms=" + std::to_string(gap));
                }
            }
            lastHeartbeatTick_ = now;
            inputGuard_.Refresh();
        }
        else if (wParam == kInputDebounceTimerId) {
            KillTimer(window, kInputDebounceTimerId);
            const bool foregroundChanged = inputGuard_.ConsumePendingEvents();
            inputGuard_.Refresh();
            if (foregroundChanged) windowModes_.ReassertTopmost();
        }
        else if (wParam == kContentWaitTimerId) {
            // This is only a configurable UI loading-indicator timeout,
            // not an automatic reload. A separate timer probes even when
            // ContentLoading arrived, because rendering can still be stuck.
            const bool timedOut = waitingForTabContent_;
            if (timedOut) {
                logs_.Write("navigation.wait_timeout",
                    "generation=" + std::to_string(webViewGeneration_) +
                    " elapsed_ms=" + std::to_string(GetTickCount64() - contentWaitStartedAt_) +
                    " check_seconds=" + std::to_string(state_.settings.blankPageCheckSeconds));
            }
            EndContentWait();
            if (timedOut && state_.currentTabIndex >= 0 &&
                state_.currentTabIndex < static_cast<int>(state_.tabs.size())) {
                TabData& tab = state_.tabs[static_cast<size_t>(state_.currentTabIndex)];
                if (IsPlaceholderTabTitle(tab.title)) UpdateCurrentTabTitle(SuggestedTitleForUrl(tab.url));
            }
        }
        else if (wParam == kContentHealthTimerId) {
            KillTimer(window, kContentHealthTimerId);
            logs_.Write("navigation.health_check_due",
                "generation=" + std::to_string(webViewGeneration_));
            ProbeWebViewResponsiveness();
        }
        else if (wParam == kContentProbeTimerId) {
            KillTimer(window, kContentProbeTimerId);
            if (contentProbePending_ && !shuttingDown_ && !webViewRecoveryPending_) {
                contentProbePending_ = false;
                ++contentProbeSerial_;
                lastContentRecoveryTick_ = GetTickCount64();
                logs_.Write("webview.probe.timeout",
                    "generation=" + std::to_string(webViewGeneration_) +
                    " serial=" + std::to_string(contentProbeSerial_));
                RequestWebViewRecovery();
            }
        }
        else if (wParam == kHotkeyRegistrationRetryTimerId) {
            KillTimer(window, kHotkeyRegistrationRetryTimerId);
            RefreshHotkeys();
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (windowModes_.IsWebFullscreen()) {
                ExecuteScript(L"if(document.fullscreenElement) document.exitFullscreen();");
                windowModes_.LeaveWebFullscreen();
            }
            if (windowModes_.IsImmersive()) {
                windowModes_.SetImmersion(false);
                hotkeys_.SetImmersiveWebFullscreen(false);
                SetWindowTextW(clickThroughButton_, L"Pass:off / Tembus:nonaktif");
                state_.immersionActive = false;
            }
            return 0;
        }
        break;
    case WM_SETTINGCHANGE:
        if (theme_.RefreshSystemTheme()) ApplyThemeToAllWindows();
        return 0;
    case kTrayMessage:
    {
        const UINT trayEvent = trayData_.uVersion == NOTIFYICON_VERSION_4
            ? LOWORD(static_cast<DWORD_PTR>(lParam)) : static_cast<UINT>(lParam);
        if (trayEvent == WM_LBUTTONUP || trayEvent == WM_LBUTTONDBLCLK ||
            trayEvent == NIN_SELECT || trayEvent == NIN_KEYSELECT) {
            RestoreFromUserAction();
        } else if (trayEvent == WM_RBUTTONUP || trayEvent == WM_CONTEXTMENU) {
            ShowTrayMenu();
        }
        return 0;
    }
    case kShowExistingInstance:
        RestoreFromUserAction();
        return 0;
#ifdef XIAOCHUANG_SMOKE_TEST
    case kSetWebTypingSmokeMessage:
        inputGuard_.SetWebTyping(wParam != 0);
        return 0;
    case kSetWebFullscreenSmokeMessage:
        if (wParam != 0) {
            windowModes_.EnterWebFullscreen();
            windowModes_.FitWebFullscreenAspect(16.0 / 9.0);
        } else {
            windowModes_.LeaveWebFullscreen();
        }
        return 0;
    case kGetHotkeyCountSmokeMessage:
        return wParam < kHotkeyCount ? smokeHotkeyCounts_[wParam] : 0;
    case kGetInputTypingSmokeMessage:
        return (inputGuard_.IsTyping() ? 1 : 0) |
            (hotkeys_.IsInputSuppressed() ? 2 : 0);
    case WM_APP + 70: {
        if (!webViewReady_ || !webViewController_) return -1;
        double factor = 0;
        if (FAILED(webViewController_->get_ZoomFactor(&factor))) return -1;
        return static_cast<LRESULT>(std::lround(factor * 100));
    }
    case WM_APP + 71: {
        if (!webView_) return -1;
        wil::com_ptr<ICoreWebView2Settings> settings;
        BOOL enabled = TRUE;
        if (FAILED(webView_->get_Settings(&settings)) ||
            FAILED(settings->get_IsZoomControlEnabled(&enabled))) return -1;
        return enabled;
    }
    case WM_APP + 72:
        if (wParam) NavigateTo(L"file:///" +
            (config_.Path().parent_path() / L"zoom-smoke.html").generic_wstring(), true);
        return webViewReady_ && smokeNavigationComplete_ && !waitingForTabContent_;
    case WM_APP + 73:
        RecreateWebView();
        return 0;
    case WM_APP + 74:
        SetFocus(settingsControls_.zoomPercent);
        SetFocus(settingsControls_.fixedZoom);
        return state_.settings.fixedWebZoomPercent;
    case WM_APP + 75:
        if (settingsWindow_) ShowWindow(settingsWindow_, SW_HIDE);
        RestoreFromUserAction();
        return webViewController_ ? webViewController_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC) : E_FAIL;
#endif
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, kInputFallbackTimerId);
        SaveConfiguration();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Application::PaintMainWindow() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(mainWindow_, &paint);
    RECT client{};
    GetClientRect(mainWindow_, &client);
    theme_.FillBackground(dc, client, theme_.Palette().window);
    if (IsWindowVisible(tabControl_)) {
        RECT chrome{0, 0, client.right, chromeHeight_};
        theme_.FillBackground(dc, chrome, theme_.Palette().chrome);
        RECT navigation{0, tabRowHeight_, client.right, chromeHeight_};
        theme_.FillBackground(dc, navigation, theme_.Palette().window);
        RECT addressRect{};
        GetWindowRect(addressEdit_, &addressRect);
        MapWindowPoints(nullptr, mainWindow_, reinterpret_cast<POINT*>(&addressRect), 2);
        InflateRect(&addressRect, Scale(10), Scale(4));
        HPEN addressPen = CreatePen(PS_SOLID, 1, theme_.Palette().border);
        HGDIOBJ addressOldPen = SelectObject(dc, addressPen);
        HGDIOBJ oldBrush = SelectObject(dc, theme_.SurfaceBrush());
        RoundRect(dc, addressRect.left, addressRect.top, addressRect.right, addressRect.bottom,
                  Scale(14), Scale(14));
        SelectObject(dc, oldBrush);
        SelectObject(dc, addressOldPen);
        DeleteObject(addressPen);
        HPEN pen = CreatePen(PS_SOLID, 1, theme_.Palette().border);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, 0, chromeHeight_ - 1, nullptr);
        LineTo(dc, client.right, chromeHeight_ - 1);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    if (waitingForTabContent_) {
        RECT loading{Scale(20), chromeHeight_ + Scale(20), client.right - Scale(20),
                     client.bottom - Scale(20)};
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme_.Palette().secondaryText);
        if (uiFont_) SelectObject(dc, uiFont_);
        DrawTextW(dc, L"Switching tab / Mengganti tab…", -1, &loading,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    EndPaint(mainWindow_, &paint);
}

void Application::DrawOwnerItem(const DRAWITEMSTRUCT& item) {
    HGDIOBJ oldFont = nullptr;
    if (uiFont_) oldFont = SelectObject(item.hDC, uiFont_);
    if (item.CtlID == Tabs) {
        const int index = static_cast<int>(item.itemID);
        const std::wstring title = index >= 0 && index < static_cast<int>(state_.tabs.size())
            ? FormatTabTitle(state_.tabs[static_cast<size_t>(index)].title) : L"New tab / Tab baru";
        theme_.DrawTab(item, title, index == TabCtrl_GetCurSel(tabControl_));
        if (oldFont) SelectObject(item.hDC, oldFont);
        return;
    }
    wchar_t text[128]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    const bool topChromeButton = GetParent(item.hwndItem) == mainWindow_ &&
        (item.CtlID == AddTab || item.CtlID == Minimize || item.CtlID == Maximize || item.CtlID == Close);
    theme_.DrawOwnerButton(item, text, item.CtlID == Go, item.CtlID == Close,
                           topChromeButton ? theme_.Palette().chrome : theme_.Palette().window);
    if (oldFont) SelectObject(item.hDC, oldFont);
}

LRESULT Application::HitTest(LPARAM lParam) const {
    if (windowModes_.IsImmersive()) return HTTRANSPARENT;
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(mainWindow_, &point);
    RECT client{};
    GetClientRect(mainWindow_, &client);
    if (!windowModes_.IsSeamlessMaximized()) {
        const int edge = Scale(7);
        const bool left = point.x < edge;
        const bool right = point.x >= client.right - edge;
        const bool top = point.y < edge;
        const bool bottom = point.y >= client.bottom - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }
    if (point.y < tabRowHeight_) {
        RECT tabs{};
        RECT addButton{};
        GetWindowRect(tabControl_, &tabs);
        GetWindowRect(addTabButton_, &addButton);
        MapWindowPoints(nullptr, mainWindow_, reinterpret_cast<POINT*>(&tabs), 2);
        MapWindowPoints(nullptr, mainWindow_, reinterpret_cast<POINT*>(&addButton), 2);
        if ((point.x >= Scale(1) && point.x < tabs.left - Scale(2)) ||
            (point.x >= tabs.right + Scale(3) && point.x < addButton.left - Scale(3))) {
            return (!windowModes_.IsSeamlessMaximized() || state_.settings.maximizedTopDragEnabled)
                ? HTCAPTION : HTCLIENT;
        }
    }
    return HTCLIENT;
}

LRESULT CALLBACK Application::AddressProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!application || !application->oldAddressProc_) return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        application->NavigateAddressBar();
        return 0;
    }
    if (message == WM_CHAR && wParam == VK_RETURN) {
        // A single-line EDIT beeps when it receives the translated Return
        // character. Navigation already happened on WM_KEYDOWN, so consume it.
        return 0;
    }
    if (message == WM_LBUTTONDBLCLK) {
        SetFocus(window);
        SendMessageW(window, EM_SETSEL, 0, -1);
        SendMessageW(window, EM_SCROLLCARET, 0, 0);
        return 0;
    }
    if (message == WM_SETFOCUS) {
        SendMessageW(window, EM_SETSEL, 0, -1);
        // Native controls are cheap to inspect. Refresh immediately so there is
        // no window in which a global hotkey can slip through while the user is
        // already typing; keep the posted refresh for cross-process focus/UIA.
        application->inputGuard_.Refresh();
        PostMessageW(application->mainWindow_, kMessageInputFocusChanged, 0, 0);
    } else if (message == WM_KILLFOCUS) {
        application->inputGuard_.Refresh();
        PostMessageW(application->mainWindow_, kMessageInputFocusChanged, 0, 0);
    }
    return CallWindowProcW(application->oldAddressProc_, window, message, wParam, lParam);
}

LRESULT CALLBACK Application::TabSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                               UINT_PTR subclassId, DWORD_PTR referenceData) {
    auto* application = reinterpret_cast<Application*>(referenceData);
    if (!application) return DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_LBUTTONDOWN) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        MapWindowPoints(window, application->mainWindow_, &point, 1);
        application->HandleTabMouseDown(MAKELPARAM(point.x, point.y));
        return 0;
    }
    if (message == WM_LBUTTONUP) return 0;
    if (message == WM_CONTEXTMENU) {
        POINT point{};
        if (lParam == static_cast<LPARAM>(-1)) {
            const int selected = TabCtrl_GetCurSel(window);
            RECT item{};
            if (selected >= 0 && TabCtrl_GetItemRect(window, selected, &item)) {
                point = {item.left + (item.right - item.left) / 2, item.bottom};
                ClientToScreen(window, &point);
            } else {
                GetCursorPos(&point);
            }
        } else {
            point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        }
        application->ShowTabContextMenu(point);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, TabSubclassProc, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

bool Application::ConfigureCaptureExclusion(HWND window) {
    if (!window || !SetWindowDisplayAffinity(window, kWdaExcludeFromCapture)) {
        logs_.Write("capture.exclusion.failed", "error=" + std::to_string(GetLastError()));
        return false;
    }
    DWORD affinity = 0;
    if (!GetWindowDisplayAffinity(window, &affinity) || affinity != kWdaExcludeFromCapture) {
        logs_.Write("capture.exclusion.verify_failed",
                    "affinity=" + std::to_string(affinity) +
                    " error=" + std::to_string(GetLastError()));
        return false;
    }
    logs_.Write("capture.exclusion.ready");
    return true;
}

bool Application::PrepareTemporaryProfile() {
    wchar_t tempDirectory[MAX_PATH]{};
    if (GetTempPathW(static_cast<DWORD>(std::size(tempDirectory)), tempDirectory) == 0) return false;
    const std::filesystem::path tempRoot(tempDirectory);
    std::error_code error;
    for (std::filesystem::directory_iterator iter(tempRoot, error), end;
         !error && iter != end; iter.increment(error)) {
        const auto& entry = *iter;
        const std::wstring name = entry.path().filename().wstring();
        if (!name.starts_with(kTemporaryProfilePrefix)) continue;
        const size_t pidStart = std::size(kTemporaryProfilePrefix) - 1;
        const size_t separator = name.find(L'-', pidStart);
        if (separator == std::wstring::npos) continue;
        const std::wstring pidText = name.substr(pidStart, separator - pidStart);
        wchar_t* end = nullptr;
        const DWORD processId = wcstoul(pidText.c_str(), &end, 10);
        if (processId == 0 || !end || *end != L'\0') continue;
        bool processActive = false;
        if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId)) {
            processActive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            CloseHandle(process);
        } else {
            processActive = GetLastError() != ERROR_INVALID_PARAMETER;
        }
        if (!processActive) {
            std::error_code cleanupError;
            std::filesystem::remove_all(entry.path(), cleanupError);
        }
    }
    temporaryProfilePath_ = tempRoot /
        (std::wstring(kTemporaryProfilePrefix) + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    error.clear();
    if (!std::filesystem::create_directory(temporaryProfilePath_, error) || error) {
        temporaryProfilePath_.clear();
        return false;
    }
    logs_.Write("webview.profile.temporary_ready");
    return true;
}

void Application::CleanupTemporaryProfile() {
    if (temporaryProfilePath_.empty()) return;
    std::error_code error;
    bool removed = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        error.clear();
        std::filesystem::remove_all(temporaryProfilePath_, error);
        std::error_code existsError;
        removed = !std::filesystem::exists(temporaryProfilePath_, existsError) && !existsError;
        if (removed) break;
        Sleep(50);
    }
    logs_.Write(removed ? "webview.profile.cleaned" : "webview.profile.cleanup_deferred");
    if (removed) temporaryProfilePath_.clear();
}

void Application::InitializeWebView() {
    if (shuttingDown_ || temporaryProfilePath_.empty()) return;
    const UINT64 generation = ++webViewGeneration_;
    logs_.Write("webview.environment.create_begin",
        "generation=" + std::to_string(generation) +
        " software=" + std::to_string(state_.settings.renderMode == RenderMode::SoftwareCompatibility ? 1 : 0));
    auto options = Make<CoreWebView2EnvironmentOptions>();
    if (!options) {
        logs_.Write("webview.environment.options_failed");
        ShowError(L"WebView2 initialization failed / Inisialisasi WebView2 gagal", L"Cannot create WebView2 options / Tidak dapat membuat opsi WebView2.");
        return;
    }
    std::wstring browserArguments = L"--disable-sync";
    if (state_.settings.renderMode == RenderMode::SoftwareCompatibility) {
        browserArguments += L" --disable-gpu";
    }
    if (FAILED(options->put_AdditionalBrowserArguments(browserArguments.c_str())) ||
        FAILED(options->put_AllowSingleSignOnUsingOSPrimaryAccount(FALSE))) {
        ShowError(L"Veil Browser", L"WebView2 private-mode options could not be applied.");
        return;
    }
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, temporaryProfilePath_.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, lifetime = lifetime_, generation](HRESULT environmentResult, ICoreWebView2Environment* environment) -> HRESULT {
                if (!lifetime->load() || shuttingDown_ || generation != webViewGeneration_) return S_OK;
                if (FAILED(environmentResult) || !environment) {
                    logs_.Write("webview.environment.failed",
                        "hr=" + std::to_string(static_cast<unsigned long>(environmentResult)));
                    ShowError(L"WebView2 initialization failed / Inisialisasi WebView2 gagal",
                              L"Install or repair Microsoft Edge WebView2 Runtime / Pasang atau perbaiki runtime tersebut.", environmentResult);
                    return S_OK;
                }
                webViewEnvironment_ = environment;
                logs_.Write("webview.environment.ready",
                    "generation=" + std::to_string(generation));
                wil::unique_cotaskmem_string version;
                if (SUCCEEDED(environment->get_BrowserVersionString(&version)) && version) {
                    webViewVersion_ = version.get();
                }
                const HRESULT creationResult = environment->CreateCoreWebView2Controller(
                    mainWindow_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, lifetime, generation](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (!lifetime->load() || shuttingDown_ || generation != webViewGeneration_) {
                                if (controller) controller->Close();
                                return S_OK;
                            }
                            if (FAILED(controllerResult) || !controller) {
                                logs_.Write("webview.controller.failed",
                                    "hr=" + std::to_string(static_cast<unsigned long>(controllerResult)));
                                ShowError(L"WebView2 initialization failed / Inisialisasi WebView2 gagal", L"Cannot create web view / Tidak dapat membuat tampilan web.", controllerResult);
                                return S_OK;
                            }
                            webViewController_ = controller;
                            logs_.Write("webview.controller.ready",
                                "generation=" + std::to_string(generation));
                            if (FAILED(controller->get_CoreWebView2(&webView_)) || !webView_) {
                                logs_.Write("webview.interface.failed");
                                ShowError(L"WebView2 initialization failed / Inisialisasi WebView2 gagal", L"Cannot access the web view / Tidak dapat mengakses tampilan web.");
                                return S_OK;
                            }

                            wil::com_ptr<ICoreWebView2Settings> settings;
                            if (FAILED(webView_->get_Settings(&settings)) || !settings ||
                                FAILED(settings->put_IsScriptEnabled(TRUE)) ||
                                FAILED(settings->put_AreDefaultScriptDialogsEnabled(FALSE)) ||
                                FAILED(settings->put_IsWebMessageEnabled(FALSE)) ||
                                FAILED(settings->put_AreDevToolsEnabled(FALSE)) ||
                                FAILED(settings->put_AreDefaultContextMenusEnabled(FALSE)) ||
                                FAILED(settings->put_AreHostObjectsAllowed(FALSE)) ||
                                FAILED(settings->put_IsStatusBarEnabled(FALSE))) {
                                ShowError(L"Veil Browser", L"Web security settings could not be applied.");
                                controller->Close();
                                webView_.reset();
                                webViewController_.reset();
                                return S_OK;
                            }
                            if (!ConfigureWebViewEvents()) {
                                ShowError(L"Veil Browser", L"Web security event handlers could not be installed.");
                                CloseWebView();
                                ShowWindow(mainWindow_, SW_HIDE);
                                PostMessageW(mainWindow_, WM_CLOSE, 0, 0);
                                return S_OK;
                            }
                            const HRESULT zoomResult = webZoom_.Attach(controller, webView_.get());
                            if (FAILED(zoomResult)) {
                                ShowError(L"Web zoom failed / Zoom web gagal", L"Cannot monitor web zoom / Tidak dapat memantau zoom web.", zoomResult);
                            }
                            webZoom_.Configure(state_.settings);
                            ResizeWebView();
                            webViewController_->put_IsVisible(windowModes_.IsHidden() ? FALSE : TRUE);
                            webViewReady_ = true;
                            FinishInitialNavigation();
                            return S_OK;
                        }).Get());
                if (FAILED(creationResult)) {
                    logs_.Write("webview.controller.submit_failed",
                        "hr=" + std::to_string(static_cast<unsigned long>(creationResult)));
                    ShowError(L"WebView2 initialization failed / Inisialisasi WebView2 gagal", L"Cannot create web view / Tidak dapat membuat tampilan web.", creationResult);
                }
                return S_OK;
            }).Get());
    if (FAILED(result)) {
        logs_.Write("webview.environment.submit_failed",
            "hr=" + std::to_string(static_cast<unsigned long>(result)));
        ShowError(L"WebView2 initialization failed / Inisialisasi WebView2 gagal", L"Cannot start WebView2 / Tidak dapat menjalankan WebView2.", result);
    }
}

bool Application::ConfigureWebViewEvents() {
    if (!webView_) return false;
    const auto lifetime = lifetime_;
    const UINT64 generation = webViewGeneration_;
    const HRESULT navigationRegistration = webView_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this, lifetime, generation](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                if (!lifetime->load() || generation != webViewGeneration_) return S_OK;
#ifdef XIAOCHUANG_SMOKE_TEST
                smokeNavigationComplete_ = false;
#endif
                hotkeys_.CancelActiveGesture();
                inputGuard_.SetWebTyping(false);
                if (!args) {
                    FailClosedWebView("navigation.args_null");
                    return E_POINTER;
                }
                UINT64 navigationId = 0;
                wil::unique_cotaskmem_string uri;
                const HRESULT uriResult = args->get_Uri(&uri);
                const HRESULT idResult = args->get_NavigationId(&navigationId);
                if (FAILED(uriResult) || !uri || FAILED(idResult)) {
                    const HRESULT cancelResult = args->put_Cancel(TRUE);
                    if (FAILED(cancelResult)) FailClosedWebView("navigation.cancel_failed");
                    else FailClosedWebView("navigation.metadata_failed");
                    return FAILED(cancelResult) ? cancelResult : E_FAIL;
                }
                if (!IsSupportedWebNavigationUrl(uri.get())) {
                    const HRESULT cancelResult = args->put_Cancel(TRUE);
                    if (FAILED(cancelResult)) FailClosedWebView("navigation.cancel_failed");
                    logs_.Write("navigation.blocked_non_https");
                    EndContentWait();
                    return cancelResult;
                }
                activeNavigationId_ = navigationId;
                activeNavigationTabIndex_ = state_.currentTabIndex;
                if (waitingForTabContent_) waitingNavigationId_ = navigationId;
                logs_.Write("navigation.start",
                    "generation=" + std::to_string(webViewGeneration_) +
                    " id=" + std::to_string(navigationId) +
                    " tab=" + std::to_string(state_.currentTabIndex));
                return S_OK;
            }).Get(), &navigationStartingToken_);
    const HRESULT contentRegistration = webView_->add_ContentLoading(
        Callback<ICoreWebView2ContentLoadingEventHandler>(
            [this, lifetime, generation](ICoreWebView2*, ICoreWebView2ContentLoadingEventArgs* args) -> HRESULT {
                if (!lifetime->load() || generation != webViewGeneration_) return S_OK;
                UINT64 navigationId = 0;
                if (args) args->get_NavigationId(&navigationId);
                logs_.Write("navigation.content_loading",
                    "generation=" + std::to_string(webViewGeneration_) +
                    " id=" + std::to_string(navigationId));
                if (!waitingForTabContent_ ||
                    (waitingNavigationId_ != 0 && navigationId != waitingNavigationId_)) return S_OK;
                EndContentWait();
                return S_OK;
            }).Get(), &contentLoadingToken_);
    const HRESULT completedRegistration = webView_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this, lifetime, generation](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                if (!lifetime->load() || generation != webViewGeneration_ || !args) return S_OK;
#ifdef XIAOCHUANG_SMOKE_TEST
                smokeNavigationComplete_ = true;
#endif
                UINT64 navigationId = 0;
                BOOL succeeded = FALSE;
                COREWEBVIEW2_WEB_ERROR_STATUS errorStatus = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                if (FAILED(args->get_NavigationId(&navigationId))) return S_OK;
                args->get_IsSuccess(&succeeded);
                if (!succeeded) args->get_WebErrorStatus(&errorStatus);
                logs_.Write("navigation.completed",
                    "generation=" + std::to_string(webViewGeneration_) +
                    " id=" + std::to_string(navigationId) +
                    " success=" + std::to_string(succeeded ? 1 : 0) +
                    " web_error=" + std::to_string(static_cast<int>(errorStatus)));
                if (navigationId != activeNavigationId_ ||
                    activeNavigationTabIndex_ != state_.currentTabIndex) return S_OK;
                if (waitingForTabContent_) {
                    EndContentWait();
                }
                wil::unique_cotaskmem_string source;
                if (webView_ && SUCCEEDED(webView_->get_Source(&source)) && source) {
                    UpdateCurrentTabUrl(source.get());
                }
                wil::unique_cotaskmem_string title;
                if (webView_ && SUCCEEDED(webView_->get_DocumentTitle(&title)) && title) {
                    UpdateCurrentTabTitle(title.get());
                }
                return S_OK;
            }).Get(), &navigationCompletedToken_);
    const HRESULT fullscreenRegistration = webView_->add_ContainsFullScreenElementChanged(
        Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
            [this, lifetime, generation](ICoreWebView2*, IUnknown*) -> HRESULT {
                if (!lifetime->load() || generation != webViewGeneration_) return S_OK;
                HandleWebFullscreenChanged();
                return S_OK;
            }).Get(), &fullscreenToken_);
    const HRESULT popupRegistration = webView_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this, lifetime, generation](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                if (!lifetime->load() || generation != webViewGeneration_) return S_OK;
                if (!args) { FailClosedWebView("popup.args_null"); return E_POINTER; }
                const HRESULT result = args->put_Handled(TRUE);
                if (FAILED(result)) FailClosedWebView("popup.deny_failed");
                return result;
            }).Get(), &newWindowToken_);
    const HRESULT permissionRegistration = webView_->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [this, lifetime, generation](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                if (!lifetime->load() || generation != webViewGeneration_) return S_OK;
                if (!args) { FailClosedWebView("permission.args_null"); return E_POINTER; }
                const HRESULT result = args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                if (FAILED(result)) FailClosedWebView("permission.deny_failed");
                return result;
            }).Get(), &permissionToken_);
    HRESULT downloadRegistration = E_NOINTERFACE;
    wil::com_ptr<ICoreWebView2_4> webView4;
    if (webView_.try_query_to(&webView4) && webView4) {
        downloadRegistration = webView4->add_DownloadStarting(
            Callback<ICoreWebView2DownloadStartingEventHandler>(
                [this, lifetime, generation](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                    if (!lifetime->load() || generation != webViewGeneration_) return S_OK;
                    if (!args) { FailClosedWebView("download.args_null"); return E_POINTER; }
                    const HRESULT result = args->put_Cancel(TRUE);
                    if (FAILED(result)) FailClosedWebView("download.deny_failed");
                    EndContentWait();
                    return result;
                }).Get(), &downloadToken_);
    }
    const HRESULT processRegistration = webView_->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [this, lifetime, generation](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                if (!lifetime->load() || generation != webViewGeneration_) return S_OK;
                COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
                if (args) args->get_ProcessFailedKind(&kind);
                HandleProcessFailure(kind);
                return S_OK;
            }).Get(), &processFailedToken_);
    return SUCCEEDED(navigationRegistration) && SUCCEEDED(contentRegistration) &&
        SUCCEEDED(completedRegistration) && SUCCEEDED(fullscreenRegistration) &&
        SUCCEEDED(popupRegistration) &&
        SUCCEEDED(permissionRegistration) && SUCCEEDED(downloadRegistration) &&
        SUCCEEDED(processRegistration);
}

void Application::FailClosedWebView(const char* reason) {
    logs_.Write("webview.security_fail_closed", reason ? reason : "unknown");
    if (!mainWindow_ || shuttingDown_) return;
    ShowWindow(mainWindow_, SW_HIDE);
    PostMessageW(mainWindow_, WM_CLOSE, 0, 0);
}

void Application::FinishInitialNavigation() {
    if (!webView_) return;
    if (state_.tabs.empty()) {
        state_.tabs.push_back({SuggestedTitleForUrl(state_.settings.homeUrl), state_.settings.homeUrl});
    }
    state_.currentTabIndex = std::clamp(state_.currentTabIndex, 0,
        static_cast<int>(state_.tabs.size()) - 1);
    RebuildTabControl();
    NavigateTo(state_.tabs[static_cast<size_t>(state_.currentTabIndex)].url);
}

void Application::ResizeWebView() {
    if (!webViewController_ || !mainWindow_) return;
    RECT bounds{};
    GetClientRect(mainWindow_, &bounds);
    bounds.top = IsWindowVisible(tabControl_) ? chromeHeight_ : 0;
    if (bounds.bottom < bounds.top) bounds.bottom = bounds.top;
    webViewController_->put_Bounds(bounds);
    webViewController_->NotifyParentWindowPositionChanged();
}

void Application::ExecuteScript(const std::wstring& script) {
    if (webView_) webView_->ExecuteScript(script.c_str(), nullptr);
}

void Application::ExecuteMediaAction(HotkeyAction action, HotkeyGesture gesture) {
    (void)gesture;
#ifdef XIAOCHUANG_SMOKE_TEST
    ++smokeHotkeyCounts_[HotkeyIndex(action)];
#endif
    if (action == HotkeyAction::ToggleHidden && gesture == HotkeyGesture::Trigger) {
        ToggleHidden();
        return;
    }
    if (action == HotkeyAction::Immersion && gesture == HotkeyGesture::Trigger) {
        if (!windowModes_.IsHidden()) ToggleImmersion();
        return;
    }
}

void Application::RequestMediaDiagnostics() {
    std::wstring base = BuildSystemDiagnostics(state_, webViewVersion_, windowModes_.Mode());
    base += L"Hotkey manager: ";
    base += hotkeysStarted_ ? L"started\r\n" : L"startup incomplete\r\n";
    base += L"Input guard: ";
    base += inputGuardStarted_ ? L"started, " : L"startup incomplete, ";
    base += inputGuard_.StatusText() + L"\r\n";
    base += L"UI heartbeat maximum delay: " + std::to_wstring(maximumHeartbeatDelay_) + L" ms\r\n";
    const bool copied = CopyTextToClipboard(mainWindow_, base);
    MessageBoxW(settingsWindow_ ? settingsWindow_ : mainWindow_,
                (base + (copied ? L"\n\nDiagnostics copied / Diagnostik disalin." :
                                  L"\n\nCannot write clipboard / Tidak dapat menulis clipboard.")).c_str(),
                L"Veil Browser diagnostics / Diagnostik", MB_OK | MB_ICONINFORMATION);
}

void Application::HandleWebFullscreenChanged() {
    if (!webView_) return;
    BOOL fullscreen = FALSE;
    if (SUCCEEDED(webView_->get_ContainsFullScreenElement(&fullscreen))) {
        if (!fullscreen) {
            windowModes_.LeaveWebFullscreen();
            hotkeys_.SetImmersiveWebFullscreen(false);
            return;
        }

        windowModes_.EnterWebFullscreen();
        hotkeys_.SetImmersiveWebFullscreen(windowModes_.IsImmersive());
        if (!state_.settings.autoFitVideoFullscreen) return;
        static constexpr wchar_t script[] = LR"JS((()=>{
const root=document.fullscreenElement;if(!root)return 0;
let video=root.matches&&root.matches('video')?root:(root.querySelector?root.querySelector('video'):null);
if(!video){video=[...document.querySelectorAll('video')].filter(v=>{const r=v.getBoundingClientRect();return r.width>0&&r.height>0}).sort((a,b)=>{const x=a.getBoundingClientRect(),y=b.getBoundingClientRect();return y.width*y.height-x.width*x.height})[0]}
const width=(video&&(video.videoWidth||video.clientWidth))||root.clientWidth||0;
const height=(video&&(video.videoHeight||video.clientHeight))||root.clientHeight||0;
return width>0&&height>0?width/height:0})())JS";
        webView_->ExecuteScript(script,
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [this, lifetime = lifetime_, generation = webViewGeneration_](HRESULT result, PCWSTR value) -> HRESULT {
                    if (!lifetime->load() || generation != webViewGeneration_) return S_OK;
                    if (FAILED(result) || !value) return S_OK;
                    wchar_t* end = nullptr;
                    const double aspectRatio = wcstod(value, &end);
                    if (end != value && std::isfinite(aspectRatio)) {
                        windowModes_.FitWebFullscreenAspect(aspectRatio);
                    }
                    return S_OK;
                }).Get());
    }
}

void Application::HandleProcessFailure(COREWEBVIEW2_PROCESS_FAILED_KIND kind) {
    logs_.Write("webview.process_failed",
        "kind=" + std::to_string(static_cast<int>(kind)) +
        " generation=" + std::to_string(webViewGeneration_));
    hotkeys_.CancelActiveGesture();
    EndContentWait();
    if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED) {
        logs_.Write("webview.process_failed.reload_requested");
        if (webView_) webView_->Reload();
        return;
    }
    RequestWebViewRecovery();
}

void Application::RequestWebViewRecovery() {
    if (shuttingDown_ || webViewRecoveryPending_ || !mainWindow_) return;
    webViewRecoveryPending_ = PostMessageW(mainWindow_, kRecreateWebViewMessage, 0, 0) != FALSE;
    logs_.Write("webview.recovery.queued",
        "posted=" + std::to_string(webViewRecoveryPending_ ? 1 : 0) +
        " generation=" + std::to_string(webViewGeneration_));
}

void Application::ProbeWebViewResponsiveness() {
    if (shuttingDown_ || !webView_ || windowModes_.IsHidden() ||
        contentProbePending_ || webViewRecoveryPending_) return;
    const ULONGLONG now = GetTickCount64();
    if (lastContentRecoveryTick_ != 0 && now - lastContentRecoveryTick_ < 60000) {
        logs_.Write("webview.probe.skipped_cooldown");
        return;
    }

    const UINT64 generation = webViewGeneration_;
    const UINT64 serial = ++contentProbeSerial_;
    contentProbePending_ = true;
    logs_.Write("webview.probe.start",
        "generation=" + std::to_string(generation) +
        " serial=" + std::to_string(serial));
    if (!SetTimer(mainWindow_, kContentProbeTimerId, 2000, nullptr)) {
        logs_.Write("webview.probe.timer_failed");
        contentProbePending_ = false;
        return;
    }
    const HRESULT hr = webView_->ExecuteScript(L"0",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this, lifetime = lifetime_, generation, serial](HRESULT result, PCWSTR) -> HRESULT {
                if (lifetime->load() && !shuttingDown_ && generation == webViewGeneration_ &&
                    serial == contentProbeSerial_ && contentProbePending_ &&
                    SUCCEEDED(result)) {
                    logs_.Write("webview.probe.ok",
                        "generation=" + std::to_string(generation) +
                        " serial=" + std::to_string(serial));
                    contentProbePending_ = false;
                    ++contentProbeSerial_;
                    if (mainWindow_) KillTimer(mainWindow_, kContentProbeTimerId);
                }
                return S_OK;
            }).Get());
    // A synchronous COM failure still gives the timeout path a chance to
    // recreate the renderer; do not turn transient errors into a UI dialog.
    if (FAILED(hr)) {
        logs_.Write("webview.probe.submit_failed",
            "hr=" + std::to_string(static_cast<unsigned long>(hr)));
    }
}

void Application::RecreateWebView() {
    if (shuttingDown_) return;
    logs_.Write("webview.recovery.begin",
        "generation=" + std::to_string(webViewGeneration_));
    webViewRecoveryPending_ = false;
    hotkeys_.CancelActiveGesture();
    inputGuard_.SetWebTyping(false);

    windowModes_.LeaveWebFullscreen();
    hotkeys_.SetImmersiveWebFullscreen(false);
    CloseWebView();
    InvalidateRect(mainWindow_, nullptr, TRUE);
    InitializeWebView();
}

void Application::CloseWebView() {
    logs_.Write("webview.close", "generation=" + std::to_string(webViewGeneration_));
#ifdef XIAOCHUANG_SMOKE_TEST
    smokeNavigationComplete_ = false;
#endif
    ++webViewGeneration_;
    if (mainWindow_) {
        KillTimer(mainWindow_, kContentWaitTimerId);
        KillTimer(mainWindow_, kContentProbeTimerId);
        KillTimer(mainWindow_, kContentHealthTimerId);
    }
    contentProbePending_ = false;
    ++contentProbeSerial_;
    waitingForTabContent_ = false;
    waitingNavigationId_ = 0;
    activeNavigationId_ = 0;
    activeNavigationTabIndex_ = -1;
    contentWaitStartedAt_ = 0;
    webZoom_.Detach();
    if (webView_) {
        webView_->remove_NavigationStarting(navigationStartingToken_);
        webView_->remove_ContentLoading(contentLoadingToken_);
        webView_->remove_NavigationCompleted(navigationCompletedToken_);
        webView_->remove_ContainsFullScreenElementChanged(fullscreenToken_);
        webView_->remove_NewWindowRequested(newWindowToken_);
        webView_->remove_PermissionRequested(permissionToken_);
        webView_->remove_ProcessFailed(processFailedToken_);
        wil::com_ptr<ICoreWebView2_4> webView4;
        if (webView_.try_query_to(&webView4) && webView4) {
            webView4->remove_DownloadStarting(downloadToken_);
        }
    }
    if (webViewController_) webViewController_->Close();
    webView_.reset();
    webViewController_.reset();
    webViewEnvironment_.reset();
    webViewReady_ = false;
}

void Application::CreateNewTab(const std::wstring& url, const std::wstring& title, bool activate) {
    if (!CanCreateTab(state_.tabs.size())) {
        MessageBoxW(mainWindow_, L"Maximum 5 tabs / Maksimal 5 tab.",
                    L"Veil Browser", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::wstring targetUrl = url.empty() ? state_.settings.homeUrl : url;
    const std::wstring targetTitle = IsPlaceholderTabTitle(title)
        ? SuggestedTitleForUrl(targetUrl) : title;
    state_.tabs.push_back({targetTitle, targetUrl});
    logs_.Write("tabs.created", "count=" + std::to_string(state_.tabs.size()) +
        " activate=" + std::to_string(activate ? 1 : 0));
    RebuildTabControl();
    if (activate) SwitchTab(static_cast<int>(state_.tabs.size()) - 1);
}

void Application::CloseTab(int index) {
    if (index < 0 || index >= static_cast<int>(state_.tabs.size())) return;
    logs_.Write("tabs.close_requested", "index=" + std::to_string(index) +
        " count=" + std::to_string(state_.tabs.size()));
    const bool closingCurrent = index == state_.currentTabIndex;
    if (state_.tabs.size() == 1) {
        state_.tabs[0] = {SuggestedTitleForUrl(state_.settings.homeUrl), state_.settings.homeUrl};
        state_.currentTabIndex = 0;
    } else {
        state_.tabs.erase(state_.tabs.begin() + index);
        if (state_.currentTabIndex >= static_cast<int>(state_.tabs.size())) {
            state_.currentTabIndex = static_cast<int>(state_.tabs.size()) - 1;
        } else if (index < state_.currentTabIndex) {
            --state_.currentTabIndex;
        }
    }
    RebuildTabControl();
    if (closingCurrent) {
        SwitchTab(state_.currentTabIndex);
    } else {
        TabCtrl_SetCurSel(tabControl_, state_.currentTabIndex);
        SaveConfiguration();
    }
}

void Application::CloseOtherTabs(int index) {
    if (index < 0 || index >= static_cast<int>(state_.tabs.size())) return;
    const bool wasCurrent = index == state_.currentTabIndex;
    TabData kept = state_.tabs[static_cast<size_t>(index)];
    state_.tabs.assign(1, std::move(kept));
    state_.currentTabIndex = 0;
    RebuildTabControl();
    if (!wasCurrent) SwitchTab(0); else SaveConfiguration();
}

void Application::CloseAllTabs() {
    state_.tabs.assign(1, TabData{SuggestedTitleForUrl(state_.settings.homeUrl),
                                  state_.settings.homeUrl});
    state_.currentTabIndex = 0;
    RebuildTabControl();
    SwitchTab(0);
}

void Application::SwitchTab(int index) {
    if (index < 0 || index >= static_cast<int>(state_.tabs.size())) return;
    logs_.Write("tabs.switch", "index=" + std::to_string(index) +
        " count=" + std::to_string(state_.tabs.size()));
    state_.currentTabIndex = index;
    TabCtrl_SetCurSel(tabControl_, index);
    const TabData& tab = state_.tabs[static_cast<size_t>(index)];
    SetWindowTextW(addressEdit_, tab.url.c_str());
    UpdateBookmarkStar();
    if (webViewReady_) {
        NavigateTo(tab.url, true);
    }
    SaveConfiguration();
}

void Application::UpdateCurrentTabTitle(const std::wstring& title) {
    if (state_.currentTabIndex < 0 || state_.currentTabIndex >= static_cast<int>(state_.tabs.size())) return;
    TabData& tab = state_.tabs[static_cast<size_t>(state_.currentTabIndex)];
    const std::wstring resolvedTitle = title.empty() ? SuggestedTitleForUrl(tab.url) : title;
    tab.title = resolvedTitle;
    if (!tab.url.empty() && !IsPlaceholderTabTitle(resolvedTitle)) {
        knownPageTitles_[NormalizeComparableUrl(tab.url)] = resolvedTitle;
    }
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    std::wstring display = FormatTabTitle(resolvedTitle);
    item.pszText = display.data();
    TabCtrl_SetItem(tabControl_, state_.currentTabIndex, &item);
    SetWindowTextW(mainWindow_, (resolvedTitle + L" - Veil Browser").c_str());
}

void Application::UpdateCurrentTabUrl(const std::wstring& url) {
    if (url.empty() || state_.currentTabIndex < 0 ||
        state_.currentTabIndex >= static_cast<int>(state_.tabs.size())) return;
    state_.tabs[static_cast<size_t>(state_.currentTabIndex)].url = url;
    SetWindowTextW(addressEdit_, url.c_str());
    UpdateBookmarkStar();
}

void Application::RebuildTabControl() {
    if (!tabControl_) return;
    TabCtrl_DeleteAllItems(tabControl_);
    for (size_t index = 0; index < state_.tabs.size(); ++index) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        std::wstring text = FormatTabTitle(state_.tabs[index].title);
        item.pszText = text.data();
        TabCtrl_InsertItem(tabControl_, static_cast<int>(index), &item);
    }
    if (!state_.tabs.empty()) {
        state_.currentTabIndex = std::clamp(state_.currentTabIndex, 0,
            static_cast<int>(state_.tabs.size()) - 1);
        TabCtrl_SetCurSel(tabControl_, state_.currentTabIndex);
    }
    if (addTabButton_) EnableWindow(addTabButton_, CanCreateTab(state_.tabs.size()) ? TRUE : FALSE);
    LayoutMainControls();
}

std::wstring Application::SuggestedTitleForUrl(const std::wstring& url) const {
    const std::wstring key = NormalizeComparableUrl(url);
    const auto known = knownPageTitles_.find(key);
    if (known != knownPageTitles_.end() && !IsPlaceholderTabTitle(known->second)) {
        return known->second;
    }
    for (const TabData& tab : state_.tabs) {
        if (NormalizeComparableUrl(tab.url) == key && !IsPlaceholderTabTitle(tab.title)) {
            return tab.title;
        }
    }
    if (_wcsicmp(url.c_str(), L"about:blank") == 0) return L"Blank / Kosong";
    const size_t scheme = url.find(L"://");
    if (scheme != std::wstring::npos) {
        const size_t hostStart = scheme + 3;
        const size_t hostEnd = url.find_first_of(L"/?#", hostStart);
        const std::wstring host = url.substr(hostStart, hostEnd - hostStart);
        if (!host.empty()) return host;
    }
    return L"Loading / Memuat…";
}

void Application::HandleTabMouseDown(LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    MapWindowPoints(mainWindow_, tabControl_, &point, 1);
    TCHITTESTINFO hit{};
    hit.pt = point;
    const int index = TabCtrl_HitTest(tabControl_, &hit);
    if (index < 0) return;
    RECT item{};
    if (!TabCtrl_GetItemRect(tabControl_, index, &item)) return;
    const int itemWidth = item.right - item.left;
    const int itemHeight = item.bottom - item.top;
    const int closeWidth = std::min(itemWidth,
        std::max(Scale(16), std::min(std::max(Scale(24), itemHeight), itemWidth / 2)));
    const bool showClose = index == state_.currentTabIndex || itemWidth >= Scale(64);
    if (showClose && point.x >= item.right - closeWidth) {
        PostMessageW(mainWindow_, kCloseTabMessage, static_cast<WPARAM>(index), 0);
    } else if (index != state_.currentTabIndex) {
        SwitchTab(index);
    }
}

void Application::ShowTabContextMenu(POINT screenPoint) {
    POINT clientPoint = screenPoint;
    ScreenToClient(tabControl_, &clientPoint);
    TCHITTESTINFO hit{};
    hit.pt = clientPoint;
    int index = TabCtrl_HitTest(tabControl_, &hit);
    if (index < 0) index = state_.currentTabIndex;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kCloseCurrentTabCommand, L"Close tab / Tutup tab");
    AppendMenuW(menu, MF_STRING | (state_.tabs.size() <= 1 ? MF_GRAYED : 0),
                kCloseOtherTabsCommand, L"Close other tabs / Tutup tab lain");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCloseAllTabsCommand, L"Close all tabs / Tutup semua tab");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kRepairWebViewCommand, L"Repair web view / Perbaiki tampilan web");
    SetForegroundWindow(mainWindow_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        screenPoint.x, screenPoint.y, 0, mainWindow_, nullptr);
    DestroyMenu(menu);
    if (command == kCloseCurrentTabCommand) CloseTab(index);
    else if (command == kCloseOtherTabsCommand) CloseOtherTabs(index);
    else if (command == kCloseAllTabsCommand) CloseAllTabs();
    else if (command == kRepairWebViewCommand) {
        logs_.Write("webview.recovery.manual_requested");
        RequestWebViewRecovery();
    }
    PostMessageW(mainWindow_, WM_NULL, 0, 0);
}

std::wstring Application::CurrentUrl() const {
    if (state_.currentTabIndex < 0 || state_.currentTabIndex >= static_cast<int>(state_.tabs.size())) return {};
    return state_.tabs[static_cast<size_t>(state_.currentTabIndex)].url;
}

std::wstring Application::CurrentTitle() const {
    if (state_.currentTabIndex < 0 || state_.currentTabIndex >= static_cast<int>(state_.tabs.size())) return {};
    return state_.tabs[static_cast<size_t>(state_.currentTabIndex)].title;
}

void Application::NavigateAddressBar() {
    NavigateTo(NormalizeInputUrl(WindowText(addressEdit_)));
    SetFocus(mainWindow_);
}

void Application::UpdateBookmarkStar() {
    const std::wstring target = NormalizeComparableUrl(CurrentUrl());
    const bool exists = std::any_of(state_.bookmarks.begin(), state_.bookmarks.end(),
        [&](const Bookmark& bookmark) { return NormalizeComparableUrl(bookmark.url) == target; });
    SetWindowTextW(starButton_, exists ? L"★" : L"☆");
    InvalidateRect(starButton_, nullptr, TRUE);
}

void Application::ToggleCurrentBookmark() {
    const std::wstring url = CurrentUrl();
    if (url.empty()) return;
    const std::wstring target = NormalizeComparableUrl(url);
    const auto iterator = std::find_if(state_.bookmarks.begin(), state_.bookmarks.end(),
        [&](const Bookmark& bookmark) { return NormalizeComparableUrl(bookmark.url) == target; });
    if (iterator == state_.bookmarks.end()) {
        state_.bookmarks.push_back({CurrentTitle().empty() ? url : CurrentTitle(), url});
    } else {
        state_.bookmarks.erase(iterator);
    }
    UpdateBookmarkStar();
    RebuildBookmarkList();
    SaveConfiguration();
}

void Application::ShowBookmarkMenu() {
    constexpr UINT kBookmarkCommandBase = 50000;
    constexpr UINT kManageBookmarksCommand = 52000;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    if (state_.bookmarks.empty()) {
        AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"No bookmarks / Belum ada markah");
    } else {
        const std::wstring current = NormalizeComparableUrl(CurrentUrl());
        for (size_t index = 0; index < state_.bookmarks.size(); ++index) {
            std::wstring title = state_.bookmarks[index].title.empty()
                ? state_.bookmarks[index].url : state_.bookmarks[index].title;
            if (title.size() > 48) title = title.substr(0, 47) + L"…";
            size_t marker = 0;
            while ((marker = title.find(L'&', marker)) != std::wstring::npos) {
                title.insert(marker, 1, L'&');
                marker += 2;
            }
            UINT flags = MF_STRING;
            if (NormalizeComparableUrl(state_.bookmarks[index].url) == current) flags |= MF_CHECKED;
            AppendMenuW(menu, flags, kBookmarkCommandBase + static_cast<UINT>(index), title.c_str());
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kManageBookmarksCommand, L"Manage bookmarks / Kelola markah…");

    RECT button{};
    GetWindowRect(bookmarkButton_, &button);
    SetForegroundWindow(mainWindow_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN |
        TPM_RIGHTBUTTON, button.left, button.bottom + Scale(2), 0, mainWindow_, nullptr);
    DestroyMenu(menu);
    if (command >= kBookmarkCommandBase && command < kBookmarkCommandBase + state_.bookmarks.size()) {
        NavigateTo(state_.bookmarks[static_cast<size_t>(command - kBookmarkCommandBase)].url);
    } else if (command == kManageBookmarksCommand) {
        ShowBookmarkWindow();
    }
}

void Application::NavigateTo(const std::wstring& url, bool concealUntilContent) {
    const std::wstring normalized = NormalizeInputUrl(url);
    if (!IsSupportedWebNavigationUrl(normalized)) {
        logs_.Write("navigation.unsupported_scheme");
        MessageBoxW(mainWindow_, L"Only HTTPS is allowed / Hanya HTTPS yang diizinkan.",
                    L"Veil Browser", MB_OK | MB_ICONWARNING);
        return;
    }
    // Each user navigation owns its own independent 5s health check.
    // Invalidate old probes so late ExecuteScript callbacks cannot cancel new ones.
    if (mainWindow_) {
        KillTimer(mainWindow_, kContentProbeTimerId);
        KillTimer(mainWindow_, kContentHealthTimerId);
    }
    contentProbePending_ = false;
    ++contentProbeSerial_;
    UpdateCurrentTabUrl(normalized);
    logs_.Write("navigation.requested",
        "generation=" + std::to_string(webViewGeneration_) +
        " tab=" + std::to_string(state_.currentTabIndex) +
        " check_seconds=" + std::to_string(state_.settings.blankPageCheckSeconds) +
        " ready=" + std::to_string(webViewReady_ ? 1 : 0));
    if (webView_) {
        if (concealUntilContent) {
            if (windowModes_.IsWebFullscreen()) {
                ExecuteScript(L"if(document.fullscreenElement) document.exitFullscreen();");
                windowModes_.LeaveWebFullscreen();
            }
            // Navigate replaces the pending navigation itself. Calling Stop first can
            // race its completion events against the new tab's navigation.
            BeginContentWait();
            // Check this navigation once after the configurable delay, even
            // when ContentLoading or NavigationCompleted fired normally.
            const UINT delayMs = static_cast<UINT>(state_.settings.blankPageCheckSeconds * 1000);
            SetTimer(mainWindow_, kContentHealthTimerId, delayMs, nullptr);
        }
        const HRESULT result = webView_->Navigate(normalized.c_str());
        if (FAILED(result)) {
            logs_.Write("navigation.submit_failed",
                "hr=" + std::to_string(static_cast<unsigned long>(result)));
            EndContentWait();
            ShowError(L"Navigation failed / Navigasi gagal", L"Cannot open this address / Tidak dapat membuka alamat.", result);
        }
    }
}

void Application::BeginContentWait() {
    waitingForTabContent_ = true;
    waitingNavigationId_ = 0;
    contentWaitStartedAt_ = GetTickCount64();
    // Never conceal the shared WebView while navigating. The user-configured
    // one-shot timeout only clears the loading indicator, not the page.
    const UINT delayMs = static_cast<UINT>(state_.settings.blankPageCheckSeconds * 1000);
    SetTimer(mainWindow_, kContentWaitTimerId, delayMs, nullptr);
    InvalidateRect(mainWindow_, nullptr, TRUE);
}

void Application::EndContentWait() {
    if (mainWindow_) KillTimer(mainWindow_, kContentWaitTimerId);
    const bool wasWaiting = waitingForTabContent_;
    waitingForTabContent_ = false;
    waitingNavigationId_ = 0;
    contentWaitStartedAt_ = 0;
    if (webViewController_) {
        const BOOL render = !windowModes_.IsHidden();
        webViewController_->put_IsVisible(render);
    }
    if (wasWaiting) {
        logs_.Write("navigation.wait_ended",
            "generation=" + std::to_string(webViewGeneration_));
        if (mainWindow_) InvalidateRect(mainWindow_, nullptr, FALSE);
    }
}

void Application::ToggleHidden() {
    SetHidden(!windowModes_.IsHidden());
}

void Application::SetHidden(bool hidden) {
    hotkeys_.CancelActiveGesture();
    if (hidden && state_.settings.autoPauseOnHide) {
        ExecuteScript(L"(()=>{const v=document.querySelector('video');if(v&&!v.paused)v.pause();})()");
    }
    if (webViewController_) {
        const BOOL render = !hidden;
        webViewController_->put_IsVisible(render);
    }
    windowModes_.SetHidden(hidden);
    state_.windowHidden = hidden;
    if (!hidden) {
        ResizeWebView();
        if (webViewController_) {
            webViewController_->put_IsVisible(TRUE);
            webViewController_->NotifyParentWindowPositionChanged();
        }
        RedrawWindow(mainWindow_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    }
    RefreshHotkeys();
}

void Application::ToggleImmersion() {
    hotkeys_.CancelActiveGesture();
    const bool entering = !windowModes_.IsImmersive();
    windowModes_.ToggleImmersion();
    state_.immersionActive = windowModes_.IsImmersive();
    hotkeys_.SetImmersiveWebFullscreen(windowModes_.IsImmersive());
    if (entering && windowModes_.IsImmersive()) {
        TransferFocusAwayFromApplication();
        // Entering immersion removes activation from the host. Re-evaluate the
        // input guard now so a formerly focused address bar cannot leave the
        // immersion/show hotkeys unregistered until the fallback timer fires.
        inputGuard_.Refresh();
    }
    RefreshOpacityControls();
}

void Application::TransferFocusAwayFromApplication() {
    if (!mainWindow_) return;
    const DWORD currentProcessId = GetCurrentProcessId();
    if (const HWND foreground = GetForegroundWindow()) {
        DWORD foregroundProcessId = 0;
        GetWindowThreadProcessId(foreground, &foregroundProcessId);
        if (foregroundProcessId != currentProcessId) return;
    }
    HWND candidate = GetWindow(mainWindow_, GW_HWNDNEXT);
    while (candidate) {
        DWORD processId = 0;
        GetWindowThreadProcessId(candidate, &processId);
        const LONG_PTR exStyle = GetWindowLongPtrW(candidate, GWL_EXSTYLE);
        DWORD cloaked = 0;
        const bool isCloaked = SUCCEEDED(DwmGetWindowAttribute(
            candidate, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
        wchar_t className[64]{};
        GetClassNameW(candidate, className, static_cast<int>(std::size(className)));
        const bool shellSurface = _wcsicmp(className, L"Progman") == 0 ||
            _wcsicmp(className, L"WorkerW") == 0 ||
            _wcsicmp(className, L"Shell_TrayWnd") == 0;
        if (processId != currentProcessId && IsWindowVisible(candidate) &&
            IsWindowEnabled(candidate) && !IsIconic(candidate) &&
            (exStyle & WS_EX_TOOLWINDOW) == 0 && !isCloaked && !shellSurface &&
            GetWindow(candidate, GW_OWNER) == nullptr) {
            if (SetForegroundWindow(candidate)) return;
        }
        candidate = GetWindow(candidate, GW_HWNDNEXT);
    }
    SetFocus(nullptr);
}

void Application::MinimizeWindow() {
    if (state_.settings.useSystemTray) SetHidden(true);
    else ShowWindow(mainWindow_, SW_MINIMIZE);
}

void Application::RefreshHotkeys() {
    const bool suppressed = captureAction_.has_value() ||
        (state_.settings.disableHotkeysOnTyping && inputGuard_.IsTyping());
    hotkeys_.Refresh(windowModes_.IsHidden(), false, suppressed);
    if (hotkeys_.RegistrationErrors().empty()) {
        KillTimer(mainWindow_, kHotkeyRegistrationRetryTimerId);
        currentHotkeyErrorSignature_.clear();
        hotkeyErrorFirstSeenAt_ = 0;
        lastShownHotkeyErrorSignature_.clear();
        return;
    }
    std::wstring signature;
    for (const std::wstring& error : hotkeys_.RegistrationErrors()) signature += error + L"\n";
    const ULONGLONG now = GetTickCount64();
    if (signature != currentHotkeyErrorSignature_) {
        currentHotkeyErrorSignature_ = signature;
        hotkeyErrorFirstSeenAt_ = now;
        SetTimer(mainWindow_, kHotkeyRegistrationRetryTimerId, 500, nullptr);
        return;
    }
    if (now - hotkeyErrorFirstSeenAt_ < 900) {
        SetTimer(mainWindow_, kHotkeyRegistrationRetryTimerId, 500, nullptr);
        return;
    }
    if (signature != lastShownHotkeyErrorSignature_) {
        lastShownHotkeyErrorSignature_ = std::move(signature);
        PostMessageW(mainWindow_, kShowRegistrationErrors, 0, 0);
    }
}

void Application::ShowHotkeyErrors() {
    if (hotkeys_.RegistrationErrors().empty()) return;
    logs_.Write("hotkey.registration_conflict",
        "count=" + std::to_string(hotkeys_.RegistrationErrors().size()));
}

void Application::AddTrayIcon() {
    if (trayIconAdded_ || !mainWindow_) return;
    trayData_ = {};
    trayData_.cbSize = sizeof(trayData_);
    trayData_.hWnd = mainWindow_;
    trayData_.uID = kTrayIconId;
    trayData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    trayData_.uCallbackMessage = kTrayMessage;
    trayData_.hIcon = smallIcon_ ? smallIcon_ : largeIcon_;
    wcscpy_s(trayData_.szTip, L"Veil Browser v0.2");
    trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &trayData_) != FALSE;
    if (trayIconAdded_) {
        trayData_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &trayData_);
    }
}

void Application::RemoveTrayIcon() {
    if (!trayIconAdded_) return;
    Shell_NotifyIconW(NIM_DELETE, &trayData_);
    trayIconAdded_ = false;
}

void Application::RefreshTrayMode() {
    windowModes_.SetSystemTray(state_.settings.useSystemTray);
    if (state_.settings.useSystemTray) AddTrayIcon(); else RemoveTrayIcon();
}

void Application::ShowTrayMenu() {
    POINT point{};
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"Open / Buka Veil");
    AppendMenuW(menu, MF_STRING, 2, L"Settings / Pengaturan");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Exit / Keluar");
    SetForegroundWindow(mainWindow_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        point.x, point.y, 0, mainWindow_, nullptr);
    DestroyMenu(menu);
    if (command == 1) RestoreFromUserAction();
    else if (command == 2) { RestoreFromUserAction(); ShowSettingsWindow(); }
    else if (command == 3) SendMessageW(mainWindow_, WM_CLOSE, 0, 0);
    PostMessageW(mainWindow_, WM_NULL, 0, 0);
}

void Application::RestoreFromUserAction() {
    if (windowModes_.IsHidden()) SetHidden(false);
    if (IsIconic(mainWindow_)) ShowWindow(mainWindow_, SW_RESTORE);
    else ShowWindow(mainWindow_, SW_SHOW);
    SetWindowPos(mainWindow_, TopmostInsertAfter(windowModes_.IsAlwaysOnTop()), 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(mainWindow_);
    SetForegroundWindow(mainWindow_);
    if (webViewController_) {
        ResizeWebView();
        webViewController_->put_IsVisible(TRUE);
        webViewController_->NotifyParentWindowPositionChanged();
    }
    RedrawWindow(mainWindow_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void Application::OpenLogFolder() {
    if (!logs_.EnsureDirectory()) {
        MessageBoxW(settingsWindow_ ? settingsWindow_ : mainWindow_,
                    L"Cannot create log folder / Tidak dapat membuat folder log.",
                    L"Logs unavailable / Log tidak tersedia", MB_OK | MB_ICONWARNING);
        return;
    }
    logs_.Write("logs.folder.opened");
    const auto dir = logs_.Directory().wstring();
    const HINSTANCE opened = ShellExecuteW(
        settingsWindow_ ? settingsWindow_ : mainWindow_,
        L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(opened) <= 32) {
        MessageBoxW(settingsWindow_ ? settingsWindow_ : mainWindow_,
                    L"Cannot open log folder / Tidak dapat membuka folder log.",
                    L"Open failed / Gagal membuka", MB_OK | MB_ICONWARNING);
    }
}

void Application::ShowSettingsWindow() {
    if (windowModes_.IsHidden()) SetHidden(false);
    if (!settingsWindow_) {
        settingsWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, kSettingsWindowClass,
            L"Settings / Pengaturan · Veil Browser", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(720), Scale(700), mainWindow_, nullptr, instance_, this);
        if (!settingsWindow_) return;
        if (!ConfigureCaptureExclusion(settingsWindow_)) {
            DestroyWindow(settingsWindow_);
            settingsWindow_ = nullptr;
            return;
        }
        CenterOwnedWindow(settingsWindow_, Scale(720), Scale(700));
    }
    RefreshSettingsControls();
    ShowWindow(settingsWindow_, SW_SHOWNORMAL);
    BringWindowToTop(settingsWindow_);
    SetForegroundWindow(settingsWindow_);
}

void Application::CreateSettingsControls(HWND window) {
    const int left = Scale(24);
    const int labelWidth = Scale(150);
    const int fieldX = left + labelWidth;
    const int fieldWidth = Scale(490);
    const int row = Scale(38);
    int y = Scale(18);
    auto heading = [&](const wchar_t* text) {
        HWND label = CreateLabel(window, text);
        MoveWindow(label, left, y, Scale(640), Scale(28), FALSE);
        SetControlFont(label, titleFont_);
        y += Scale(34);
    };
    const auto label = [&](const wchar_t* text, int atY) {
        HWND control = CreateLabel(window, text);
        MoveWindow(control, left, atY + Scale(5), labelWidth - Scale(10), Scale(24), FALSE);
        return control;
    };
    const auto checkbox = [&](const wchar_t* text, int id, int atY) {
        HWND control = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            fieldX, atY, fieldWidth, Scale(28), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SetControlFont(control);
        return control;
    };
    const auto edit = [&](int id, int atY, int width) {
        HWND control = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
            fieldX, atY, width, Scale(28), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SetControlFont(control);
        return control;
    };
    const auto combo = [&](int id, int atY, int width) {
        HWND control = CreateWindowExW(0, WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            fieldX, atY, width, Scale(220), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SetControlFont(control);
        return control;
    };

    heading(L"General / Umum");
    label(L"Home URL / URL beranda", y);
    settingsControls_.home = edit(SettingsHome, y, fieldWidth);
    y += row;
    settingsControls_.tray = checkbox(L"Minimize to tray / Minimalkan ke tray", SettingsTray, y);
    y += Scale(30);
    settingsControls_.autoPause = checkbox(L"Pause video when hidden / Jeda video saat disembunyikan", SettingsAutoPause, y);
    y += Scale(30);
    settingsControls_.fixedZoom = checkbox(L"Lock web zoom / Kunci zoom web", SettingsFixedZoom, y);
    y += Scale(30);
    label(L"Web zoom / Zoom web (%)", y);
    settingsControls_.zoomPercent = edit(SettingsZoomPercent, y, Scale(90));
    SendMessageW(settingsControls_.zoomPercent, EM_SETLIMITTEXT, 3, 0);
    HWND zoomNote = CreateLabel(window, L"25–500%, default / bawaan 100%");
    MoveWindow(zoomNote, fieldX + Scale(102), y + Scale(4), Scale(390), Scale(26), FALSE);
    y += row;
    HWND zoomRefreshNote = CreateLabel(window, L"Reload after unlocking to reset zoom / Muat ulang setelah membuka kunci zoom.");
    MoveWindow(zoomRefreshNote, left, y, Scale(650), Scale(28), FALSE);
    y += Scale(32);
    // This adjusts when a *one-shot* white-screen health check starts.
    // The check never blindly refreshes a slowly loading page.
    label(L"Recovery delay / Jeda pemulihan", y);
    settingsControls_.blankPageCheck = edit(SettingsBlankPageCheck, y, Scale(90));
    SendMessageW(settingsControls_.blankPageCheck, EM_SETLIMITTEXT, 2, 0);
    HWND blankCheckUnit = CreateLabel(window, L"sec / detik");
    MoveWindow(blankCheckUnit, fieldX + Scale(102), y + Scale(4),
               Scale(42), Scale(24), FALSE);
    y += row;
    HWND blankCheckNote = CreateLabel(window,
        L"3–30 sec; responsiveness check only / 3–30 detik; hanya cek respons.");
    MoveWindow(blankCheckNote, left, y, Scale(650), Scale(28), FALSE);
    y += Scale(34);
    settingsControls_.autoFitFullscreen = checkbox(
        L"Fit window to fullscreen video / Sesuaikan jendela ke video penuh", SettingsAutoFitFullscreen, y);
    y += Scale(30);
    settingsControls_.lockFullscreenAspect = checkbox(
        L"Lock fullscreen aspect ratio / Kunci rasio layar penuh",
        SettingsLockFullscreenAspect, y);
    y += Scale(30);
    settingsControls_.maximizedTopDrag = checkbox(
        L"Drag top to restore / Seret atas untuk memulihkan",
        SettingsMaximizedTopDrag, y);
    y += Scale(42);

    heading(L"Pass-through / Tembus klik");
    label(L"Pointer hole radius / Radius lubang penunjuk", y);
    settingsControls_.radius = edit(SettingsRadius, y, Scale(130));
    y += row;
    // Keep the input aligned with other settings. The localized label must
    // fit the common label column; show its unit next to the value instead.
    label(L"Pointer refresh / Refresh penunjuk", y);
    settingsControls_.holeRefresh = edit(SettingsHoleRefresh, y, Scale(130));
    SendMessageW(settingsControls_.holeRefresh, EM_SETLIMITTEXT, 3, 0);
    HWND refreshUnit = CreateLabel(window, L"ms");
    MoveWindow(refreshUnit, fieldX + Scale(140), y + Scale(5),
               Scale(50), Scale(24), FALSE);
    y += row;
    // A full-width, dedicated row prevents localization/DPI wrapping from
    // overlapping the next setting (the old inline 380px label was too narrow).
    HWND refreshNote = CreateLabel(window,
        L"10–100ms; lower is faster but costs more / Lebih kecil lebih cepat namun lebih berat.");
    MoveWindow(refreshNote, left, y, Scale(650), Scale(28), FALSE);
    y += Scale(34);
    label(L"Snap distance / Jarak lekat", y);
    settingsControls_.snap = edit(SettingsSnap, y, Scale(130));
    y += row;
    label(L"Pass mode / Mode tembus", y);
    settingsControls_.style = combo(SettingsStyle, y, Scale(230));
    SendMessageW(settingsControls_.style, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Pointer hole / Lubang penunjuk"));
    SendMessageW(settingsControls_.style, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Transparent + click-through / Transparan + tembus klik"));
    y += row;
    settingsControls_.opacityLabel = label(L"Opacity / Opasitas", y);
    settingsControls_.opacitySlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        fieldX, y, Scale(330), Scale(32), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(SettingsOpacitySlider)), instance_, nullptr);
    SendMessageW(settingsControls_.opacitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(settingsControls_.opacitySlider, TBM_SETTICFREQ, 10, 0);
    settingsControls_.opacityEdit = edit(SettingsOpacityEdit, y, Scale(72));
    MoveWindow(settingsControls_.opacityEdit, fieldX + Scale(350), y, Scale(72), Scale(28), FALSE);
    settingsControls_.opacityPercent = CreateLabel(window, L"%");
    MoveWindow(settingsControls_.opacityPercent, fieldX + Scale(428), y + Scale(4), Scale(30), Scale(24), FALSE);
    y += row;
    settingsControls_.typing = checkbox(L"Pause hotkeys while typing / Jeda hotkey saat mengetik", SettingsTyping, y);
    y += Scale(30);
    heading(L"Hotkeys / Tombol pintas (Ctrl / Alt / Shift)");
    const auto defaults = DefaultHotkeys();
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        if (IsMediaAction(static_cast<HotkeyAction>(index))) continue;
        label(defaults[index].displayName.c_str(), y);
        settingsControls_.hotkeys[index] = CreateThemedButton(
            window, L"", SettingsHotkeyBase + static_cast<int>(index));
        MoveWindow(settingsControls_.hotkeys[index], fieldX, y, Scale(270), Scale(29), FALSE);
        settingsControls_.hotkeyEnabled[index] = CreateWindowExW(0, L"BUTTON", L"Enable / Aktif",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            fieldX + Scale(286), y, Scale(100), Scale(29), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                SettingsHotkeyEnabledBase + static_cast<int>(index))), instance_, nullptr);
        SetControlFont(settingsControls_.hotkeyEnabled[index]);
        y += Scale(33);
    }
    y += Scale(12);

    heading(L"Performance & appearance / Performa & tampilan");
    label(L"Rendering / Render web", y);
    settingsControls_.renderMode = combo(SettingsRenderMode, y, Scale(260));
    SendMessageW(settingsControls_.renderMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Automatic GPU / GPU otomatis"));
    SendMessageW(settingsControls_.renderMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Software compatibility / Kompatibilitas software"));
    y += row;
    label(L"Theme / Tema", y);
    settingsControls_.theme = combo(SettingsTheme, y, Scale(220));
    SendMessageW(settingsControls_.theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"System / Sistem"));
    SendMessageW(settingsControls_.theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Light / Terang"));
    SendMessageW(settingsControls_.theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dark / Gelap"));
    y += Scale(40);
    heading(L"Logs / Log");
    settingsControls_.logAutoClean = checkbox(
        L"Clean old logs / Bersihkan log lama", SettingsLogAutoClean, y);
    y += row;
    label(L"Log retention / Masa simpan log", y);
    settingsControls_.logRetention = combo(SettingsLogRetention, y, Scale(150));
    for (const int days : {3, 7, 30}) {
        const std::wstring text = std::to_wstring(days) + L" days / hari";
        SendMessageW(settingsControls_.logRetention, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text.c_str()));
    }
    y += row;
    HWND logNote = CreateLabel(window,
        L"Logs exclude URLs, searches, and cookies / Log tidak merekam URL, pencarian, atau cookie.");
    MoveWindow(logNote, left, y, Scale(650), Scale(28), FALSE);
    y += Scale(34);
    settingsControls_.diagnostics = CreateThemedButton(window, L"Open logs / Buka log", SettingsDiagnostics);
    MoveWindow(settingsControls_.diagnostics, fieldX, y, Scale(190), Scale(32), FALSE);
    settingsControls_.about = CreateThemedButton(window, L"About / Tentang", SettingsAbout);
    MoveWindow(settingsControls_.about, fieldX + Scale(202), y, Scale(130), Scale(32), FALSE);
    settingsControls_.repository = CreateThemedButton(window, L"GitHub source / Sumber", SettingsRepository);
    MoveWindow(settingsControls_.repository, fieldX + Scale(344), y, Scale(160), Scale(32), FALSE);
    HWND restartNote = CreateLabel(window, L"Rendering changes require restart. Nonstandard capture may bypass protection / Perubahan render perlu restart; capture nonstandar dapat melewati proteksi.");
    MoveWindow(restartNote, left, y + Scale(44), Scale(650), Scale(48), FALSE);
    y += Scale(102);

    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin = 0;
    info.nMax = y;
    RECT client{};
    GetClientRect(window, &client);
    info.nPage = static_cast<UINT>(std::max(1L, client.bottom));
    info.nPos = 0;
    SetScrollInfo(window, SB_VERT, &info, TRUE);
}

void Application::ReadSettingsControls() {
    if (!settingsWindow_ || updatingSettingsControls_) return;
    updatingSettingsControls_ = true;
    const auto finishUpdate = wil::scope_exit([this] { updatingSettingsControls_ = false; });
    AppSettings old = state_.settings;
    state_.settings.holeRadius = ParseInteger(settingsControls_.radius, old.holeRadius);
    state_.settings.holeRefreshMs = ParseInteger(settingsControls_.holeRefresh, old.holeRefreshMs);
    state_.settings.blankPageCheckSeconds = ParseInteger(
        settingsControls_.blankPageCheck, old.blankPageCheckSeconds);
    state_.settings.snapThreshold = ParseInteger(settingsControls_.snap, old.snapThreshold);
    state_.settings.autoPauseOnHide = IsChecked(settingsControls_.autoPause);
    state_.settings.fixedWebZoomEnabled = IsChecked(settingsControls_.fixedZoom);
    state_.settings.fixedWebZoomPercent = ParseInteger(
        settingsControls_.zoomPercent, old.fixedWebZoomPercent);
    state_.settings.autoFitVideoFullscreen = IsChecked(settingsControls_.autoFitFullscreen);
    state_.settings.lockVideoFullscreenAspect = IsChecked(settingsControls_.lockFullscreenAspect);
    state_.settings.maximizedTopDragEnabled = IsChecked(settingsControls_.maximizedTopDrag);
    state_.settings.disableHotkeysOnTyping = IsChecked(settingsControls_.typing);
    state_.settings.useSystemTray = IsChecked(settingsControls_.tray);
    state_.settings.backgroundMediaHotkeys = false;
    state_.settings.homeUrl = NormalizeInputUrl(WindowText(settingsControls_.home));
    state_.settings.immersionStyle = static_cast<ImmersionStyle>(ComboValue(settingsControls_.style));
    const int opacity = std::clamp(ParseInteger(settingsControls_.opacityEdit,
        static_cast<int>(SendMessageW(settingsControls_.opacitySlider, TBM_GETPOS, 0, 0))), 0, 100);
    state_.settings.autoHideOpacityPercent = opacity;
    state_.settings.holdPlaybackRate = old.holdPlaybackRate;
    state_.settings.renderMode = static_cast<RenderMode>(ComboValue(settingsControls_.renderMode));
    state_.settings.themeMode = static_cast<ThemeMode>(ComboValue(settingsControls_.theme));
    state_.settings.autoCleanLogs = IsChecked(settingsControls_.logAutoClean);
    const int logIndex = ComboValue(settingsControls_.logRetention);
    state_.settings.logRetentionDays = logIndex == 0 ? 3 : logIndex == 2 ? 30 : 7;
    state_.settings.Clamp();
    SetWindowTextW(settingsControls_.holeRefresh,
                   std::to_wstring(state_.settings.holeRefreshMs).c_str());
    SetWindowTextW(settingsControls_.blankPageCheck,
                   std::to_wstring(state_.settings.blankPageCheckSeconds).c_str());
    EnableWindow(settingsControls_.zoomPercent, state_.settings.fixedWebZoomEnabled ? TRUE : FALSE);
    SetWindowTextW(settingsControls_.zoomPercent,
        std::to_wstring(state_.settings.fixedWebZoomPercent).c_str());
    EnableWindow(settingsControls_.lockFullscreenAspect,
                 state_.settings.autoFitVideoFullscreen ? TRUE : FALSE);

    if (old.themeMode != state_.settings.themeMode) {
        theme_.SetMode(state_.settings.themeMode);
        ApplyThemeToAllWindows();
    }
    if (old.useSystemTray != state_.settings.useSystemTray) RefreshTrayMode();
    if (old.immersionStyle != state_.settings.immersionStyle ||
        old.holeOpacityPercent != state_.settings.holeOpacityPercent ||
        old.autoHideOpacityPercent != state_.settings.autoHideOpacityPercent ||
        old.holeRadius != state_.settings.holeRadius ||
        old.holeRefreshMs != state_.settings.holeRefreshMs) {
        windowModes_.RefreshVisualState();
    }
    if (old.disableHotkeysOnTyping != state_.settings.disableHotkeysOnTyping) {
        inputGuard_.SetEnabled(state_.settings.disableHotkeysOnTyping);
        RefreshHotkeys();
    }
    if (!old.autoFitVideoFullscreen && state_.settings.autoFitVideoFullscreen &&
        windowModes_.IsWebFullscreen()) {
        HandleWebFullscreenChanged();
    }
    if (old.autoCleanLogs != state_.settings.autoCleanLogs ||
        old.logRetentionDays != state_.settings.logRetentionDays) {
        logs_.Write("logs.retention.changed",
            "enabled=" + std::to_string(state_.settings.autoCleanLogs ? 1 : 0) +
            " days=" + std::to_string(state_.settings.logRetentionDays));
        logs_.SetRetention(state_.settings.autoCleanLogs, state_.settings.logRetentionDays);
    }
    EnableWindow(settingsControls_.logRetention, state_.settings.autoCleanLogs ? TRUE : FALSE);
    SaveConfiguration();
    // WebView COM calls may pump new UI messages. Finish native controls and
    // persistence first so a subsequent edit cannot be overwritten by this one.
    updatingSettingsControls_ = false;
    if (old.fixedWebZoomEnabled != state_.settings.fixedWebZoomEnabled ||
        old.fixedWebZoomPercent != state_.settings.fixedWebZoomPercent) {
        webZoom_.Configure(state_.settings);
    }
}

void Application::RefreshSettingsControls() {
    if (!settingsWindow_) return;
    updatingSettingsControls_ = true;
    SetWindowTextW(settingsControls_.radius, std::to_wstring(state_.settings.holeRadius).c_str());
    SetWindowTextW(settingsControls_.holeRefresh,
                   std::to_wstring(state_.settings.holeRefreshMs).c_str());
    SetWindowTextW(settingsControls_.blankPageCheck,
                   std::to_wstring(state_.settings.blankPageCheckSeconds).c_str());
    SetWindowTextW(settingsControls_.snap, std::to_wstring(state_.settings.snapThreshold).c_str());
    SetWindowTextW(settingsControls_.home, state_.settings.homeUrl.c_str());
    SetCheck(settingsControls_.autoPause, state_.settings.autoPauseOnHide);
    SetCheck(settingsControls_.fixedZoom, state_.settings.fixedWebZoomEnabled);
    SetWindowTextW(settingsControls_.zoomPercent, std::to_wstring(state_.settings.fixedWebZoomPercent).c_str());
    EnableWindow(settingsControls_.zoomPercent, state_.settings.fixedWebZoomEnabled ? TRUE : FALSE);
    SetCheck(settingsControls_.autoFitFullscreen, state_.settings.autoFitVideoFullscreen);
    SetCheck(settingsControls_.lockFullscreenAspect, state_.settings.lockVideoFullscreenAspect);
    EnableWindow(settingsControls_.lockFullscreenAspect,
                 state_.settings.autoFitVideoFullscreen ? TRUE : FALSE);
    SetCheck(settingsControls_.maximizedTopDrag, state_.settings.maximizedTopDragEnabled);
    SetCheck(settingsControls_.typing, state_.settings.disableHotkeysOnTyping);
    SetCheck(settingsControls_.tray, state_.settings.useSystemTray);

    SelectComboValue(settingsControls_.style, static_cast<int>(state_.settings.immersionStyle));

    SelectComboValue(settingsControls_.renderMode, static_cast<int>(state_.settings.renderMode));
    SelectComboValue(settingsControls_.theme, static_cast<int>(state_.settings.themeMode));
    SetCheck(settingsControls_.logAutoClean, state_.settings.autoCleanLogs);
    SelectComboValue(settingsControls_.logRetention,
        state_.settings.logRetentionDays == 3 ? 0 :
        state_.settings.logRetentionDays == 30 ? 2 : 1);
    EnableWindow(settingsControls_.logRetention, state_.settings.autoCleanLogs ? TRUE : FALSE);
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        SetWindowTextW(settingsControls_.hotkeys[index], HotkeyDisplayText(state_.hotkeys[index]).c_str());
        SetCheck(settingsControls_.hotkeyEnabled[index], state_.hotkeys[index].enabled);
    }
    updatingSettingsControls_ = false;
    RefreshOpacityControls();
}

void Application::RefreshOpacityControls() {
    if (!settingsWindow_ || !settingsControls_.opacitySlider || updatingSettingsControls_) return;
    updatingSettingsControls_ = true;
    const bool visible = state_.settings.immersionStyle == ImmersionStyle::AutoHide;
    EnableWindow(settingsControls_.holeRefresh, visible ? FALSE : TRUE);
    const int value = state_.settings.autoHideOpacityPercent;
    SendMessageW(settingsControls_.opacitySlider, TBM_SETPOS, TRUE, value);
    SetWindowTextW(settingsControls_.opacityEdit, std::to_wstring(value).c_str());
    const HWND opacityControls[] = {settingsControls_.opacityLabel, settingsControls_.opacitySlider,
                                    settingsControls_.opacityEdit, settingsControls_.opacityPercent};
    for (HWND control : opacityControls) {
        if (control) ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    }
    updatingSettingsControls_ = false;
}

void Application::BeginHotkeyCapture(HotkeyAction action) {
    CancelHotkeyCapture();
    captureAction_ = action;
    HWND button = settingsControls_.hotkeys[HotkeyIndex(action)];
    SetWindowTextW(button, L"Press a key / Tekan tombol (Esc cancels)");
    SetFocus(button ? button : settingsWindow_);
    RefreshHotkeys();
    hotkeys_.BeginMouseCapture([this](UINT key, UINT modifiers) {
        ApplyCapturedHotkey(key, modifiers);
    });
}

bool Application::HandleHotkeyCaptureKey(UINT virtualKey) {
    if (!captureAction_) return false;
    if (virtualKey == VK_ESCAPE) {
        CancelHotkeyCapture();
        return true;
    }
    if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL ||
        virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT ||
        virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU ||
        virtualKey == VK_LWIN || virtualKey == VK_RWIN) {
        return true;
    }
    UINT modifiers = 0;
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= MOD_CONTROL;
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= MOD_SHIFT;
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) modifiers |= MOD_ALT;
    ApplyCapturedHotkey(virtualKey, modifiers);
    return true;
}

void Application::ApplyCapturedHotkey(UINT virtualKey, UINT modifiers) {
    if (!captureAction_) return;
    const size_t target = HotkeyIndex(*captureAction_);
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        if (index != target && state_.hotkeys[index].virtualKey == virtualKey &&
            state_.hotkeys[index].modifiers == modifiers) {
            MessageBoxW(settingsWindow_, L"This shortcut is already used / Pintasan sudah dipakai.", L"Duplicate shortcut / Pintasan duplikat",
                        MB_OK | MB_ICONWARNING);
            CancelHotkeyCapture();
            return;
        }
    }
    state_.hotkeys[target].virtualKey = virtualKey;
    state_.hotkeys[target].modifiers = modifiers;
    captureAction_.reset();
    hotkeys_.CancelMouseCapture();
    RefreshSettingsControls();
    inputGuard_.Refresh();
    RefreshHotkeys();
    SaveConfiguration();
}

void Application::CancelHotkeyCapture() {
    const bool wasCapturing = captureAction_.has_value();
    if (captureAction_) {
        const size_t index = HotkeyIndex(*captureAction_);
        SetWindowTextW(settingsControls_.hotkeys[index], HotkeyDisplayText(state_.hotkeys[index]).c_str());
    }
    captureAction_.reset();
    hotkeys_.CancelMouseCapture();
    if (wasCapturing) {
        inputGuard_.Refresh();
        RefreshHotkeys();
    }
}

LRESULT CALLBACK Application::SettingsWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }
    return application ? application->HandleSettingsMessage(window, message, wParam, lParam)
                       : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleSettingsMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateSettingsControls(window);
        theme_.ApplyWindowTheme(window);
        ApplyThemeToWindowTree(window, theme_.IsDark());
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id >= SettingsHotkeyBase && id < SettingsHotkeyBase + static_cast<int>(kHotkeyCount)) {
            BeginHotkeyCapture(static_cast<HotkeyAction>(id - SettingsHotkeyBase));
            return 0;
        }
        if (id >= SettingsHotkeyEnabledBase &&
            id < SettingsHotkeyEnabledBase + static_cast<int>(kHotkeyCount)) {
            if (HIWORD(wParam) == BN_CLICKED) {
                CancelHotkeyCapture();
                const size_t index = static_cast<size_t>(id - SettingsHotkeyEnabledBase);
                state_.hotkeys[index].enabled = IsChecked(settingsControls_.hotkeyEnabled[index]);
                RefreshHotkeys();
                SaveConfiguration();
            }
            return 0;
        }
        if (id == SettingsDiagnostics) {
            OpenLogFolder();
            return 0;
        }
        if (id == SettingsAbout) {
            MessageBoxW(window,
                L"Veil Browser v0.2.0\n\n"
                L"Lightweight Windows overlay browser / Browser overlay Windows ringan\n"
                L"C++20 / Win32 / WebView2 1.0.4078.44\n\n"
                L"Fork: https://github.com/Cliffamr/Mini-Window-Browser\n"
                L"Upstream: https://github.com/azurplain/Mini-Window-Browser",
                L"About / Tentang Veil Browser", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (id == SettingsRepository) {
            const HINSTANCE opened = ShellExecuteW(
                window, L"open", L"https://github.com/Cliffamr/Mini-Window-Browser",
                nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(opened) <= 32) {
                MessageBoxW(window, L"Cannot open the source URL / Tidak dapat membuka URL sumber.",
                            L"Open failed / Gagal membuka", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        const int notification = HIWORD(wParam);
        if (id == SettingsStyle && notification == CBN_SELCHANGE) {
            state_.settings.immersionStyle = static_cast<ImmersionStyle>(ComboValue(settingsControls_.style));
            state_.settings.Clamp();
            RefreshOpacityControls();
            windowModes_.RefreshVisualState();
            SaveConfiguration();
        } else if (notification == BN_CLICKED || notification == CBN_SELCHANGE ||
            notification == EN_KILLFOCUS) {
            ReadSettingsControls();
        }
        return 0;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == settingsControls_.opacitySlider) {
            const int value = static_cast<int>(SendMessageW(settingsControls_.opacitySlider, TBM_GETPOS, 0, 0));
            updatingSettingsControls_ = true;
            SetWindowTextW(settingsControls_.opacityEdit, std::to_wstring(value).c_str());
            updatingSettingsControls_ = false;
            state_.settings.autoHideOpacityPercent = value;
            windowModes_.RefreshVisualState();
            SetTimer(window, kSettingsSaveTimerId, 250, nullptr);
        }
        return 0;
    case WM_TIMER:
        if (wParam == kSettingsSaveTimerId) {
            KillTimer(window, kSettingsSaveTimerId);
            SaveConfiguration();
            return 0;
        }
        break;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info), SIF_ALL};
        GetScrollInfo(window, SB_VERT, &info);
        int next = info.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: next -= Scale(30); break;
        case SB_LINEDOWN: next += Scale(30); break;
        case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK: next = info.nTrackPos; break;
        default: break;
        }
        next = std::clamp(next, info.nMin, std::max(info.nMin, info.nMax - static_cast<int>(info.nPage) + 1));
        if (next != info.nPos) {
            ScrollWindowEx(window, 0, info.nPos - next, nullptr, nullptr, nullptr, nullptr,
                           SW_INVALIDATE | SW_ERASE | SW_SCROLLCHILDREN);
            info.fMask = SIF_POS;
            info.nPos = next;
            SetScrollInfo(window, SB_VERT, &info, TRUE);
            settingsScrollPosition_ = next;
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        SendMessageW(window, WM_VSCROLL,
            MAKEWPARAM(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? SB_LINEUP : SB_LINEDOWN, 0), 0);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (HandleHotkeyCaptureKey(static_cast<UINT>(wParam))) return 0;
        break;
    case WM_DRAWITEM:
        DrawOwnerItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            RedrawWindow(mainWindow_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        break;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme_.Palette().text);
        SetBkColor(dc, theme_.Palette().surface);
        return reinterpret_cast<LRESULT>(theme_.SurfaceBrush());
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, theme_.Palette().text);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(theme_.WindowBrush());
    }
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        theme_.FillBackground(reinterpret_cast<HDC>(wParam), client, theme_.Palette().window);
        return 1;
    }
    case WM_CLOSE:
        KillTimer(window, kSettingsSaveTimerId);
        SaveConfiguration();
        CancelHotkeyCapture();
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_DESTROY:
        settingsWindow_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Application::ShowBookmarkWindow() {
    if (!bookmarkWindow_) {
        bookmarkWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, kBookmarkWindowClass,
            L"Bookmarks / Markah · Veil Browser", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(650), Scale(500), mainWindow_, nullptr, instance_, this);
        if (!bookmarkWindow_) return;
        if (!ConfigureCaptureExclusion(bookmarkWindow_)) {
            DestroyWindow(bookmarkWindow_);
            bookmarkWindow_ = nullptr;
            return;
        }
        CenterOwnedWindow(bookmarkWindow_, Scale(650), Scale(500));
    }
    RebuildBookmarkList();
    ShowWindow(bookmarkWindow_, SW_SHOWNORMAL);
    SetForegroundWindow(bookmarkWindow_);
}

void Application::CreateBookmarkControls(HWND window) {
    bookmarkControls_.list = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        Scale(18), Scale(18), Scale(240), Scale(400), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(BookmarkList)), instance_, nullptr);
    HWND titleLabel = CreateLabel(window, L"Title / Judul");
    MoveWindow(titleLabel, Scale(282), Scale(24), Scale(70), Scale(24), FALSE);
    bookmarkControls_.title = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        Scale(282), Scale(52), Scale(330), Scale(30), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(BookmarkTitle)), instance_, nullptr);
    HWND urlLabel = CreateLabel(window, L"URL / Alamat");
    MoveWindow(urlLabel, Scale(282), Scale(98), Scale(70), Scale(24), FALSE);
    bookmarkControls_.url = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
        Scale(282), Scale(126), Scale(330), Scale(90), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(BookmarkUrl)), instance_, nullptr);
    HWND save = CreateThemedButton(window, L"Save / Simpan", BookmarkSave);
    HWND open = CreateThemedButton(window, L"Open / Buka", BookmarkOpen);
    HWND remove = CreateThemedButton(window, L"Delete / Hapus", BookmarkDelete);
    MoveWindow(save, Scale(282), Scale(242), Scale(126), Scale(34), FALSE);
    MoveWindow(open, Scale(420), Scale(242), Scale(92), Scale(34), FALSE);
    MoveWindow(remove, Scale(520), Scale(242), Scale(92), Scale(34), FALSE);
    const HWND controls[] = {bookmarkControls_.list, bookmarkControls_.title,
        bookmarkControls_.url, titleLabel, urlLabel, save, open, remove};
    for (HWND control : controls) SetControlFont(control);
}

void Application::RebuildBookmarkList() {
    if (!bookmarkControls_.list) return;
    SendMessageW(bookmarkControls_.list, LB_RESETCONTENT, 0, 0);
    for (const Bookmark& bookmark : state_.bookmarks) {
        SendMessageW(bookmarkControls_.list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(bookmark.title.c_str()));
    }
    if (selectedBookmark_ >= static_cast<int>(state_.bookmarks.size())) selectedBookmark_ = -1;
    if (selectedBookmark_ >= 0) SendMessageW(bookmarkControls_.list, LB_SETCURSEL, selectedBookmark_, 0);
}

void Application::LoadSelectedBookmark() {
    const LRESULT selected = SendMessageW(bookmarkControls_.list, LB_GETCURSEL, 0, 0);
    selectedBookmark_ = selected == LB_ERR ? -1 : static_cast<int>(selected);
    if (selectedBookmark_ < 0 || selectedBookmark_ >= static_cast<int>(state_.bookmarks.size())) return;
    const Bookmark& bookmark = state_.bookmarks[static_cast<size_t>(selectedBookmark_)];
    SetWindowTextW(bookmarkControls_.title, bookmark.title.c_str());
    SetWindowTextW(bookmarkControls_.url, bookmark.url.c_str());
}

LRESULT CALLBACK Application::BookmarkWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }
    return application ? application->HandleBookmarkMessage(window, message, wParam, lParam)
                       : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleBookmarkMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateBookmarkControls(window);
        theme_.ApplyWindowTheme(window);
        ApplyThemeToWindowTree(window, theme_.IsDark());
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == BookmarkList && HIWORD(wParam) == LBN_SELCHANGE) LoadSelectedBookmark();
        else if (id == BookmarkList && HIWORD(wParam) == LBN_DBLCLK) {
            LoadSelectedBookmark();
            if (selectedBookmark_ >= 0) {
                NavigateTo(state_.bookmarks[static_cast<size_t>(selectedBookmark_)].url);
                ShowWindow(window, SW_HIDE);
            }
        } else if (id == BookmarkSave) {
            Bookmark value{WindowText(bookmarkControls_.title),
                           NormalizeInputUrl(WindowText(bookmarkControls_.url))};
            if (value.title.empty()) value.title = value.url;
            if (selectedBookmark_ >= 0 && selectedBookmark_ < static_cast<int>(state_.bookmarks.size())) {
                state_.bookmarks[static_cast<size_t>(selectedBookmark_)] = std::move(value);
            } else {
                state_.bookmarks.push_back(std::move(value));
                selectedBookmark_ = static_cast<int>(state_.bookmarks.size()) - 1;
            }
            RebuildBookmarkList(); UpdateBookmarkStar(); SaveConfiguration();
        } else if (id == BookmarkOpen && selectedBookmark_ >= 0 &&
                   selectedBookmark_ < static_cast<int>(state_.bookmarks.size())) {
            NavigateTo(state_.bookmarks[static_cast<size_t>(selectedBookmark_)].url);
            ShowWindow(window, SW_HIDE);
        } else if (id == BookmarkDelete && selectedBookmark_ >= 0 &&
                   selectedBookmark_ < static_cast<int>(state_.bookmarks.size())) {
            state_.bookmarks.erase(state_.bookmarks.begin() + selectedBookmark_);
            selectedBookmark_ = -1;
            SetWindowTextW(bookmarkControls_.title, L"");
            SetWindowTextW(bookmarkControls_.url, L"");
            RebuildBookmarkList(); UpdateBookmarkStar(); SaveConfiguration();
        }
        return 0;
    }
    case WM_SIZE: {
        RECT client{}; GetClientRect(window, &client);
        const int margin = Scale(18), split = Scale(250), rightX = Scale(282);
        MoveWindow(bookmarkControls_.list, margin, margin, split - margin,
                   std::max(Scale(120), static_cast<int>(client.bottom) - margin * 2), TRUE);
        MoveWindow(bookmarkControls_.title, rightX, Scale(52),
                   std::max(Scale(160), static_cast<int>(client.right) - rightX - margin), Scale(30), TRUE);
        MoveWindow(bookmarkControls_.url, rightX, Scale(126),
                   std::max(Scale(160), static_cast<int>(client.right) - rightX - margin), Scale(90), TRUE);
        return 0;
    }
    case WM_DRAWITEM:
        DrawOwnerItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam)); return TRUE;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            RedrawWindow(mainWindow_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, theme_.Palette().text);
        SetBkColor(dc, theme_.Palette().surface); return reinterpret_cast<LRESULT>(theme_.SurfaceBrush());
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, theme_.Palette().text);
        SetBkMode(dc, TRANSPARENT); return reinterpret_cast<LRESULT>(theme_.WindowBrush());
    }
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        theme_.FillBackground(reinterpret_cast<HDC>(wParam), client, theme_.Palette().window); return 1;
    }
    case WM_CLOSE: ShowWindow(window, SW_HIDE); return 0;
    case WM_DESTROY: bookmarkWindow_ = nullptr; return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Application::ShowPresetWindow() {
    if (!presetWindow_) {
        presetWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, kPresetWindowClass,
            L"Scenes / Skenario · Veil Browser", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(620), Scale(490), mainWindow_, nullptr, instance_, this);
        if (!presetWindow_) return;
        if (!ConfigureCaptureExclusion(presetWindow_)) {
            DestroyWindow(presetWindow_);
            presetWindow_ = nullptr;
            return;
        }
        CenterOwnedWindow(presetWindow_, Scale(620), Scale(490));
    }
    RebuildPresetList();
    ShowWindow(presetWindow_, SW_SHOWNORMAL);
    SetForegroundWindow(presetWindow_);
}

void Application::CreatePresetControls(HWND window) {
    presetControls_.list = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        Scale(18), Scale(18), Scale(250), Scale(365), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(PresetList)), instance_, nullptr);
    HWND label = CreateLabel(window, L"Scene name / Nama skenario");
    MoveWindow(label, Scale(292), Scale(24), Scale(220), Scale(24), FALSE);
    presetControls_.name = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        Scale(292), Scale(54), Scale(290), Scale(30), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(PresetName)), instance_, nullptr);
    HWND apply = CreateThemedButton(window, L"Apply / Terapkan", PresetApply);
    HWND add = CreateThemedButton(window, L"Save scene / Simpan skenario", PresetAdd);
    HWND rename = CreateThemedButton(window, L"Rename / Ganti nama", PresetSaveName);
    HWND remove = CreateThemedButton(window, L"Delete / Hapus", PresetDelete);
    HWND reset = CreateThemedButton(window, L"Reset / Atur ulang", PresetReset);
    MoveWindow(apply, Scale(292), Scale(110), Scale(140), Scale(35), FALSE);
    MoveWindow(add, Scale(442), Scale(110), Scale(140), Scale(35), FALSE);
    MoveWindow(rename, Scale(292), Scale(158), Scale(140), Scale(35), FALSE);
    MoveWindow(remove, Scale(442), Scale(158), Scale(140), Scale(35), FALSE);
    MoveWindow(reset, Scale(292), Scale(220), Scale(180), Scale(35), FALSE);
    HWND note = CreateWindowExW(0, L"STATIC",
        L"Scenes save window, pass-through, hotkeys, and tabs / Skenario menyimpan jendela, mode tembus, hotkey, dan tab.",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL | SS_NOPREFIX,
        Scale(292), Scale(272), Scale(290), Scale(130), window, nullptr, instance_, nullptr);
    const HWND controls[] = {presetControls_.list, presetControls_.name, label, apply,
        add, rename, remove, reset, note};
    for (HWND control : controls) SetControlFont(control);
}

void Application::RebuildPresetList() {
    if (!presetControls_.list) return;
    SendMessageW(presetControls_.list, LB_RESETCONTENT, 0, 0);
    for (const Preset& preset : state_.presets) {
        SendMessageW(presetControls_.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preset.name.c_str()));
    }
    if (selectedPreset_ >= static_cast<int>(state_.presets.size())) selectedPreset_ = -1;
    if (selectedPreset_ >= 0) SendMessageW(presetControls_.list, LB_SETCURSEL, selectedPreset_, 0);
}

void Application::LoadSelectedPreset() {
    const LRESULT selected = SendMessageW(presetControls_.list, LB_GETCURSEL, 0, 0);
    selectedPreset_ = selected == LB_ERR ? -1 : static_cast<int>(selected);
    if (selectedPreset_ >= 0 && selectedPreset_ < static_cast<int>(state_.presets.size())) {
        SetWindowTextW(presetControls_.name, state_.presets[static_cast<size_t>(selectedPreset_)].name.c_str());
    }
}

void Application::AddCurrentPreset() {
    std::wstring name = WindowText(presetControls_.name);
    if (TrimWhitespace(name).empty()) {
        size_t suffix = state_.presets.size() + 1;
        std::vector<std::wstring> names;
        names.reserve(state_.presets.size());
        for (const Preset& preset : state_.presets) names.push_back(preset.name);
        do {
            name = L"Scene / Skenario " + std::to_wstring(suffix++);
        } while (HasPresetNameConflict(names, name));
    } else if (!NormalizeAndValidatePresetName(name, -1)) {
        return;
    }
    state_.presets.push_back(CapturePreset(name));
    selectedPreset_ = static_cast<int>(state_.presets.size()) - 1;
    RebuildPresetList();
    SaveConfiguration();
}

void Application::ApplySelectedPreset() {
    if (selectedPreset_ < 0 || selectedPreset_ >= static_cast<int>(state_.presets.size())) return;
    ApplyPreset(state_.presets[static_cast<size_t>(selectedPreset_)]);
}

bool Application::NormalizeAndValidatePresetName(std::wstring& name, int ignoredIndex) const {
    name = TrimWhitespace(std::move(name));
    std::vector<std::wstring> names;
    names.reserve(state_.presets.size());
    for (const Preset& preset : state_.presets) names.push_back(preset.name);
    const size_t ignored = ignoredIndex < 0 ? static_cast<size_t>(-1)
        : static_cast<size_t>(ignoredIndex);
    if (name.empty()) {
        MessageBoxW(presetWindow_, L"Scene name is required / Nama skenario wajib.", L"Invalid name / Nama tidak valid", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (HasPresetNameConflict(names, name, ignored)) {
        MessageBoxW(presetWindow_, L"Scene name already exists / Nama skenario sudah ada.", L"Duplicate name / Nama duplikat", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

void Application::ResetToDefaults() {
    if (MessageBoxW(presetWindow_, L"Reset window and feature settings? / Atur ulang jendela dan fitur?",
                    L"Reset / Atur ulang", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    const ThemeMode retainedTheme = state_.settings.themeMode;
    const RenderMode retainedRender = state_.settings.renderMode;
    state_.settings = AppSettings{};
    logs_.SetRetention(state_.settings.autoCleanLogs, state_.settings.logRetentionDays);
    logs_.Write("settings.reset");
    webZoom_.Configure(state_.settings);
    state_.settings.themeMode = retainedTheme;
    state_.settings.renderMode = retainedRender;
    state_.hotkeys = DefaultHotkeys();
    inputGuard_.SetEnabled(state_.settings.disableHotkeysOnTyping);
    RECT rect{100, 100, 1100, 700};
    windowModes_.ApplyPresetGeometry(rect, false);
    windowModes_.RefreshVisualState();
    RefreshTrayMode(); RefreshHotkeys(); RefreshSettingsControls(); SaveConfiguration();
}

LRESULT CALLBACK Application::PresetWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        application = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }
    return application ? application->HandlePresetMessage(window, message, wParam, lParam)
                       : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandlePresetMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreatePresetControls(window);
        theme_.ApplyWindowTheme(window);
        ApplyThemeToWindowTree(window, theme_.IsDark());
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == PresetList && HIWORD(wParam) == LBN_SELCHANGE) LoadSelectedPreset();
        else if (id == PresetList && HIWORD(wParam) == LBN_DBLCLK) { LoadSelectedPreset(); ApplySelectedPreset(); }
        else if (id == PresetApply) ApplySelectedPreset();
        else if (id == PresetAdd) AddCurrentPreset();
        else if (id == PresetSaveName && selectedPreset_ >= 0 &&
                 selectedPreset_ < static_cast<int>(state_.presets.size())) {
            std::wstring name = WindowText(presetControls_.name);
            if (NormalizeAndValidatePresetName(name, selectedPreset_)) {
                state_.presets[static_cast<size_t>(selectedPreset_)].name = std::move(name);
                RebuildPresetList(); SaveConfiguration();
            }
        } else if (id == PresetDelete && selectedPreset_ >= 0 &&
                   selectedPreset_ < static_cast<int>(state_.presets.size())) {
            state_.presets.erase(state_.presets.begin() + selectedPreset_);
            selectedPreset_ = -1; SetWindowTextW(presetControls_.name, L"");
            RebuildPresetList(); SaveConfiguration();
        } else if (id == PresetReset) ResetToDefaults();
        return 0;
    }
    case WM_DRAWITEM: DrawOwnerItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam)); return TRUE;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            RedrawWindow(mainWindow_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, theme_.Palette().text);
        SetBkColor(dc, theme_.Palette().surface); return reinterpret_cast<LRESULT>(theme_.SurfaceBrush());
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, theme_.Palette().text);
        SetBkMode(dc, TRANSPARENT); return reinterpret_cast<LRESULT>(theme_.WindowBrush());
    }
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        theme_.FillBackground(reinterpret_cast<HDC>(wParam), client, theme_.Palette().window); return 1;
    }
    case WM_CLOSE: ShowWindow(window, SW_HIDE); return 0;
    case WM_DESTROY: presetWindow_ = nullptr; return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Application::SaveConfiguration() {
    if (!mainWindow_ || shuttingDown_) return;
    windowModes_.UpdateNormalRectFromWindow();
    state_.session.normalRect = windowModes_.NormalRect();
    state_.session.maximized = windowModes_.IsSeamlessMaximized();
    state_.session.activeTab = state_.currentTabIndex;
    config_.Save(state_, state_.session.normalRect, state_.session.maximized);
}

void Application::ApplyPreset(const Preset& preset) {
    logs_.Write("preset.apply",
        "tabs=" + std::to_string(preset.tabs.size()) +
        " check_seconds=" + std::to_string(preset.blankPageCheckSeconds));
    const ThemeMode retainedTheme = state_.settings.themeMode;
    const RenderMode retainedRender = state_.settings.renderMode;
    state_.settings.holeRadius = preset.holeRadius;
    state_.settings.holeRefreshMs = preset.holeRefreshMs;
    state_.settings.blankPageCheckSeconds = preset.blankPageCheckSeconds;
    state_.settings.snapThreshold = preset.snapThreshold;
    state_.settings.holeOpacityPercent = preset.holeOpacityPercent;
    state_.settings.autoHideOpacityPercent = preset.autoHideOpacityPercent;
    state_.settings.holdPlaybackRate = preset.holdPlaybackRate;
    state_.settings.fixedWebZoomEnabled = preset.fixedWebZoomEnabled;
    state_.settings.fixedWebZoomPercent = preset.fixedWebZoomPercent;
    state_.settings.autoPauseOnHide = preset.autoPauseOnHide;
    state_.settings.disableHotkeysOnTyping = preset.disableHotkeysOnTyping;
    state_.settings.useSystemTray = preset.useSystemTray;
    state_.settings.backgroundMediaHotkeys = false;
    state_.settings.autoFitVideoFullscreen = preset.autoFitVideoFullscreen;
    state_.settings.lockVideoFullscreenAspect = preset.lockVideoFullscreenAspect;
    state_.settings.maximizedTopDragEnabled = preset.maximizedTopDragEnabled;
    state_.settings.immersionStyle = preset.immersionStyle;
    state_.settings.homeUrl = preset.homeUrl;
    state_.settings.themeMode = retainedTheme;
    state_.settings.renderMode = retainedRender;
    state_.settings.Clamp();
    state_.hotkeys = preset.hotkeys;
    for (size_t index = 0; index < kHotkeyCount; ++index) {
        if (IsMediaAction(static_cast<HotkeyAction>(index))) state_.hotkeys[index].enabled = false;
    }
    webZoom_.Configure(state_.settings);
    windowModes_.ApplyPresetGeometry(preset.normalRect, preset.maximized);
    windowModes_.RefreshVisualState();
    if (!preset.tabs.empty()) {
        state_.tabs.assign(preset.tabs.begin(),
            preset.tabs.begin() + std::min(preset.tabs.size(), kMaxTabs));
        for (TabData& tab : state_.tabs) {
            if (tab.url.empty()) tab.url = state_.settings.homeUrl;
            if (IsPlaceholderTabTitle(tab.title)) tab.title = SuggestedTitleForUrl(tab.url);
        }
        state_.currentTabIndex = std::clamp(preset.activeTab, 0,
            static_cast<int>(state_.tabs.size()) - 1);
        RebuildTabControl();
        SwitchTab(state_.currentTabIndex);
    } else if (!preset.currentUrl.empty()) {
        NavigateTo(preset.currentUrl);
    }
    inputGuard_.SetEnabled(state_.settings.disableHotkeysOnTyping);
    RefreshTrayMode(); RefreshHotkeys(); RefreshSettingsControls(); SaveConfiguration();
}

Preset Application::CapturePreset(const std::wstring& name) const {
    Preset preset;
    preset.name = name;
    preset.normalRect = windowModes_.NormalRect();
    preset.maximized = windowModes_.IsSeamlessMaximized();
    preset.holeRadius = state_.settings.holeRadius;
    preset.holeRefreshMs = state_.settings.holeRefreshMs;
    preset.blankPageCheckSeconds = state_.settings.blankPageCheckSeconds;
    preset.snapThreshold = state_.settings.snapThreshold;
    preset.holeOpacityPercent = state_.settings.holeOpacityPercent;
    preset.autoHideOpacityPercent = state_.settings.autoHideOpacityPercent;
    preset.holdPlaybackRate = state_.settings.holdPlaybackRate;
    preset.fixedWebZoomEnabled = state_.settings.fixedWebZoomEnabled;
    preset.fixedWebZoomPercent = state_.settings.fixedWebZoomPercent;
    preset.autoPauseOnHide = state_.settings.autoPauseOnHide;
    preset.disableHotkeysOnTyping = state_.settings.disableHotkeysOnTyping;
    preset.useSystemTray = state_.settings.useSystemTray;
    preset.backgroundMediaHotkeys = state_.settings.backgroundMediaHotkeys;
    preset.autoFitVideoFullscreen = state_.settings.autoFitVideoFullscreen;
    preset.lockVideoFullscreenAspect = state_.settings.lockVideoFullscreenAspect;
    preset.maximizedTopDragEnabled = state_.settings.maximizedTopDragEnabled;
    preset.immersionStyle = state_.settings.immersionStyle;
    preset.homeUrl = state_.settings.homeUrl;
    preset.currentUrl = CurrentUrl();
    preset.tabs = state_.tabs;
    preset.activeTab = state_.currentTabIndex;
    preset.hotkeys = state_.hotkeys;
    return preset;
}

void Application::SetControlFont(HWND control, HFONT font) const {
    if (control) SendMessageW(control, WM_SETFONT,
        reinterpret_cast<WPARAM>(font ? font : uiFont_), TRUE);
}

HWND Application::CreateThemedButton(HWND parent, const wchar_t* text, int id) const {
    HWND button = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SetControlFont(button);
    return button;
}

HWND Application::CreateLabel(HWND parent, const wchar_t* text) const {
    HWND label = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, instance_, nullptr);
    SetControlFont(label);
    return label;
}

void Application::CenterOwnedWindow(HWND window, int width, int height) const {
    RECT owner{};
    GetWindowRect(mainWindow_, &owner);
    HMONITOR monitor = MonitorFromWindow(mainWindow_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    width = std::min(width, static_cast<int>(info.rcWork.right - info.rcWork.left) - Scale(24));
    height = std::min(height, static_cast<int>(info.rcWork.bottom - info.rcWork.top) - Scale(24));
    int x = owner.left + ((owner.right - owner.left) - width) / 2;
    int y = owner.top + ((owner.bottom - owner.top) - height) / 2;
    const int workLeft = static_cast<int>(info.rcWork.left);
    const int workTop = static_cast<int>(info.rcWork.top);
    const int workRight = static_cast<int>(info.rcWork.right);
    const int workBottom = static_cast<int>(info.rcWork.bottom);
    x = std::clamp(x, workLeft, std::max(workLeft, workRight - width));
    y = std::clamp(y, workTop, std::max(workTop, workBottom - height));
    SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
}

void Application::ShowError(const wchar_t* title, const wchar_t* message, HRESULT result) const {
    std::wstring text = message ? message : L"Unknown error / Kesalahan tidak diketahui.";
    if (FAILED(result)) {
        wchar_t system[512]{};
        const DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        if (FormatMessageW(flags, nullptr, static_cast<DWORD>(result),
                           MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), system,
                           static_cast<DWORD>(std::size(system)), nullptr) > 0) {
            text += L"\n\n";
            text += system;
        }
        wchar_t code[24]{};
        swprintf_s(code, L"0x%08X", static_cast<unsigned int>(result));
        text += L"\nError / Kode kesalahan: ";
        text += code;
    }
    MessageBoxW(mainWindow_, text.c_str(), title, MB_OK | MB_ICONERROR);
}

} // namespace xiaochuang
