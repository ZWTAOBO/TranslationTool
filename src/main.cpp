// TranslationTool - Windows desktop translation floating window
// Main entry point - Win32 + Dear ImGui + DirectX 11

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include "app.h"
#include "resource.h"

// Forward declare message handler from imgui_impl_win32
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Global application instance
static App* g_App = nullptr;
static NOTIFYICONDATAW g_nid = {};
static const UINT WM_TRAYICON = WM_APP + 1;
static const UINT CMD_EXIT = 1003;
static const UINT CMD_AUTOSTART = 1004;
static const UINT CMD_SETTINGS = 1005;

// Window dimensions (loaded from config at runtime)
static int WIN_W = 440;
static int WIN_H = 500;

// D3D11 globals
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Acrylic/blur effect types (for Windows 10/11)
typedef enum _WINDOWCOMPOSITIONATTRIB {
    WCA_ACCENT_POLICY = 19
} WINDOWCOMPOSITIONATTRIB;

typedef enum _ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLUR = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5
} ACCENT_STATE;

typedef struct _ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

// Forward declare runtime function
typedef BOOL(WINAPI* pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

// Forward declarations for D3D11
static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();

// Resize tracking state (used by main loop BEFORE ImGui rendering)
// SetWindowPos fires BEFORE ImGui::NewFrame so content always matches window
// g_ResizeCorner is updated by UI::Render() every frame (0=none, 1=BR, 2=BL)
bool   g_ResizeActive    = false;
int    g_ResizeCorner    = 0;
int    g_ResizeSavedCorner = 0;  // corner frozen at drag start (used during drag)
POINT  g_ResizeStart     = {};
int    g_ResizeBaseW     = 0;
int    g_ResizeBaseH     = 0;
int    g_ResizeBaseX     = 0;   // window left at drag start (for BL anchor)
int    g_ResizeBaseY     = 0;   // window top at drag start
static int    g_ResizeTargetW = 0;
static int    g_ResizeTargetH = 0;

// Try to set acrylic/blur effect (Windows 10+ only)
static bool ApplyAcrylicEffect(HWND hwnd)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) return FALSE;

    pfnSetWindowCompositionAttribute setWindowCompositionAttribute =
        (pfnSetWindowCompositionAttribute)GetProcAddress(hUser32, "SetWindowCompositionAttribute");
    if (!setWindowCompositionAttribute) return FALSE;

    ACCENT_POLICY accent = { ACCENT_ENABLE_ACRYLICBLUR, 2, 0, 0 };
    WINDOWCOMPOSITIONATTRIBDATA wcad = { WCA_ACCENT_POLICY, &accent, sizeof(accent) };
    return setWindowCompositionAttribute(hwnd, &wcad);
}

// Window procedure
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;

    case WM_MOVE:
        // Save position in memory (disk write deferred to EXITSIZEMOVE)
        if (g_App) {
            g_App->GetConfig().windowX = (int)(short)LOWORD(lParam);
            g_App->GetConfig().windowY = (int)(short)HIWORD(lParam);
        }
        return 0;

    case WM_EXITSIZEMOVE:
        // User finished moving or resizing — persist to disk
        if (g_App) {
            RECT cr;
            GetClientRect(hWnd, &cr);
            g_App->GetConfig().windowWidth = cr.right;
            g_App->GetConfig().windowHeight = cr.bottom;
            g_App->SaveConfig();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            ShowWindow(hWnd, SW_HIDE);
        }
        return 0;

    case WM_ACTIVATE:
        if (wParam == WA_INACTIVE && g_App && g_App->IsConfig().autoHideOnFocusLost)
        {
            ShowWindow(hWnd, SW_HIDE);
        }
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP)
        {
            ShowWindow(hWnd, SW_SHOW);
            BringWindowToTop(hWnd);
            SetForegroundWindow(hWnd);
        }
        else if (lParam == WM_RBUTTONUP)
        {
            HMENU hMenu = CreatePopupMenu();
            bool autoStart = g_App && g_App->IsConfig().autoStart;
            AppendMenuW(hMenu, MF_STRING | (autoStart ? MF_CHECKED : 0),
                        CMD_AUTOSTART, L"开机自启");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, CMD_SETTINGS, L"设置");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, CMD_EXIT, L"退出");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
            PostMessage(hWnd, WM_NULL, 0, 0);
            DestroyMenu(hMenu);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == CMD_EXIT)
        {
            DestroyWindow(hWnd);
        }
        else if (LOWORD(wParam) == CMD_SETTINGS)
        {
            if (g_App) g_App->ShowSettings();
        }
        else if (LOWORD(wParam) == CMD_AUTOSTART)
        {
            if (g_App) {
                bool& as = g_App->GetConfig().autoStart;
                as = !as;
                // Save triggers ApplyAutoStart which writes/removes the registry key
                g_App->SaveConfig();
            }
        }
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Hotkey ID
#define HOTKEY_ID_TRANSLATE 1

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Create application instance
    App app;
    g_App = &app;

    // Load configuration
    app.LoadConfig();
    WIN_W = app.IsConfig().windowWidth;
    WIN_H = app.IsConfig().windowHeight;

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"TranslationToolClass";
    RegisterClassExW(&wc);

    // Create window with layered + topmost styles
    // Use saved position if available, otherwise center on screen
    int windowX = app.IsConfig().windowX;
    int windowY = app.IsConfig().windowY;
    if (windowX < 0 || windowY < 0) {
        windowX = max(0, (GetSystemMetrics(SM_CXSCREEN) - WIN_W) / 2);
        windowY = max(0, (GetSystemMetrics(SM_CYSCREEN) - WIN_H) / 2);
    }
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"TranslationToolClass",
        L"翻译工具",
        WS_POPUP,
        windowX, windowY,
        WIN_W, WIN_H,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd)
        return 1;

    // Set transparency
    const int opacity = app.IsConfig().windowOpacity;
    SetLayeredWindowAttributes(hwnd, 0, (BYTE)opacity, LWA_ALPHA);

    // Enable acrylic/blur effect on Windows 10/11
    ApplyAcrylicEffect(hwnd);

    // Setup system tray icon
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
    wcscpy_s(g_nid.szTip, L"翻译工具");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // Initialize D3D11
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(L"TranslationToolClass", hInstance);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Disable .ini file

    // Build glyph ranges: cover all scripts the app may encounter
    ImVector<ImWchar> glyphRanges;
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());       // Latin
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());   // CJK
        builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());      // Cyrillic
        const ImWchar hangul[] = { 0xAC00, 0xD7AF, 0 };            // Hangul Syllables
        builder.AddRanges(hangul);
        const ImWchar hangulJamo[] = { 0x1100, 0x11FF, 0 };        // Hangul Jamo
        builder.AddRanges(hangulJamo);
        const ImWchar arabic[] = { 0x0600, 0x06FF, 0 };            // Arabic
        builder.AddRanges(arabic);
        const ImWchar arabicSupp[] = { 0x0750, 0x077F, 0 };        // Arabic Supplement
        builder.AddRanges(arabicSupp);
        const ImWchar arabicExt[] = { 0x08A0, 0x08FF, 0 };         // Arabic Extended-A
        builder.AddRanges(arabicExt);
        const ImWchar thai[] = { 0x0E00, 0x0E7F, 0 };              // Thai
        builder.AddRanges(thai);
        builder.BuildRanges(&glyphRanges);
    }

    // Load main Chinese font (Microsoft YaHei ships with Windows)
    ImFontConfig fontConfig;
    fontConfig.FontNo = 0;
    fontConfig.RasterizerMultiply = 1.0f;
    ImFont* mainFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc", 16.0f, &fontConfig,
        glyphRanges.Data
    );
    if (!mainFont) {
        mainFont = io.Fonts->AddFontDefault();
    }

    // Merge fallback fonts for scripts not covered by main font
    {
        ImFontConfig mergeConfig;
        mergeConfig.MergeMode = true;
        const char* fallbackFonts[] = {
            "C:\\Windows\\Fonts\\malgun.ttf",     // Korean (Malgun Gothic)
            "C:\\Windows\\Fonts\\tahoma.ttf",     // Arabic + Thai (Tahoma)
            "C:\\Windows\\Fonts\\seguiemj.ttf",   // Emoji fallback (Segoe UI Emoji)
        };
        for (auto* ff : fallbackFonts) {
            if (GetFileAttributesA(ff) != INVALID_FILE_ATTRIBUTES) {
                io.Fonts->AddFontFromFileTTF(ff, 16.0f, &mergeConfig, glyphRanges.Data);
            }
        }
    }

    // Set dark theme with custom colors
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.WindowPadding = ImVec2(4, 4);
    style.FramePadding = ImVec2(8, 6);
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    
    // Color scheme - dark theme with accent colors
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]       = ImVec4(0.08f, 0.08f, 0.14f, 0.92f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.12f, 0.20f, 0.65f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.32f, 0.65f);
    colors[ImGuiCol_FrameBgActive]  = ImVec4(0.25f, 0.25f, 0.38f, 0.65f);
    colors[ImGuiCol_TitleBg]        = ImVec4(0.06f, 0.06f, 0.12f, 0.90f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.06f, 0.06f, 0.12f, 0.90f);
    colors[ImGuiCol_Button]         = ImVec4(0.15f, 0.28f, 0.50f, 0.70f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.85f, 0.25f, 0.35f, 0.70f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.90f, 0.30f, 0.40f, 0.85f);
    colors[ImGuiCol_Text]           = ImVec4(0.92f, 0.92f, 0.96f, 1.00f);
    colors[ImGuiCol_TextDisabled]   = ImVec4(0.45f, 0.45f, 0.55f, 1.00f);
    colors[ImGuiCol_Separator]      = ImVec4(0.20f, 0.20f, 0.32f, 0.78f);
    colors[ImGuiCol_ChildBg]        = ImVec4(0.06f, 0.06f, 0.12f, 0.55f);
    colors[ImGuiCol_ScrollbarBg]    = ImVec4(0.06f, 0.06f, 0.12f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]  = ImVec4(0.25f, 0.25f, 0.40f, 0.70f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.55f, 0.80f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.55f, 0.55f, 0.65f, 0.90f);
    colors[ImGuiCol_Header]         = ImVec4(0.20f, 0.20f, 0.35f, 0.60f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.30f, 0.30f, 0.45f, 0.70f);
    colors[ImGuiCol_HeaderActive]   = ImVec4(0.40f, 0.40f, 0.55f, 0.80f);
    colors[ImGuiCol_PopupBg]        = ImVec4(0.10f, 0.10f, 0.18f, 0.95f);

    // Setup ImGui backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Register global hotkey
    app.SetWindowHandle(hwnd);
    if (!RegisterHotKey(hwnd, HOTKEY_ID_TRANSLATE, MOD_CONTROL | MOD_ALT, 'T'))
    {
        app.SetStatus("提示: Ctrl+Alt+T 快捷键注册失败，可能被其他程序占用");
    }

    // Main loop
    bool done = false;
    bool prevLButton = false;
    while (!done)
    {
        // ── Message processing ──
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                done = true;

            TranslateMessage(&msg);
            DispatchMessage(&msg);

            // Handle hotkey
            if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID_TRANSLATE)
            {
                app.OnHotkeyTriggered();
            }
        }

        if (done)
            break;

        // ── Resize tracking ──
        bool curLButton = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        // Update corner-zone detection at frame rate (not stale)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            RECT cr;
            GetClientRect(hwnd, &cr);
            bool cornerBR = (pt.x >= cr.right - 8 && pt.x < cr.right &&
                             pt.y >= cr.bottom - 8 && pt.y < cr.bottom);
            bool cornerBL = (pt.x >= 0 && pt.x < 8 &&
                             pt.y >= cr.bottom - 8 && pt.y < cr.bottom);
            g_ResizeCorner = cornerBR ? 1 : (cornerBL ? 2 : 0);
        }

        // End resize (button released)
        if (g_ResizeActive && !curLButton)
        {
            g_ResizeActive = false;
            g_ResizeCorner = 0;
            ReleaseCapture();
            RECT cr;
            GetClientRect(hwnd, &cr);
            app.GetConfig().windowWidth = cr.right;
            app.GetConfig().windowHeight = cr.bottom;
            app.SaveConfig();
        }
        // Start resize (g_ResizeCorner updated above this frame)
        if (!g_ResizeActive && curLButton && !prevLButton && g_ResizeCorner)
        {
            g_ResizeActive = true;
            SetCapture(hwnd);
            GetCursorPos(&g_ResizeStart);
            RECT wr;
            GetWindowRect(hwnd, &wr);
            g_ResizeBaseW = wr.right - wr.left;
            g_ResizeBaseH = wr.bottom - wr.top;
            g_ResizeBaseX = wr.left;
            g_ResizeBaseY = wr.top;
            g_ResizeSavedCorner = g_ResizeCorner;  // freeze corner for entire drag
            g_ResizeTargetW = g_ResizeBaseW;
            g_ResizeTargetH = g_ResizeBaseH;
        }
        // Apply pending resize — real-time tracking with dual throttle:
        // 1) Size delta >= 8px — skip tiny per-frame jitter
        // 2) At most every 24ms — caps at ~40 fps during fast movement,
        //    halving ResizeBuffers calls during rapid continuous resize
        if (g_ResizeActive)
        {
            POINT cur;
            GetCursorPos(&cur);
            int dx = cur.x - g_ResizeStart.x;
            int dy = cur.y - g_ResizeStart.y;
            int newW = (g_ResizeSavedCorner == 1)
                           ? max(300, g_ResizeBaseW + dx)
                           : max(300, g_ResizeBaseW - dx);
            int newH = max(300, g_ResizeBaseH + dy);
            static DWORD lastResizeMs = 0;
            DWORD now = GetTickCount();
            if ((abs(newW - g_ResizeTargetW) >= 8 || abs(newH - g_ResizeTargetH) >= 8)
                && now - lastResizeMs >= 24)
            {
                lastResizeMs = now;
                g_ResizeTargetW = newW;
                g_ResizeTargetH = newH;
                if (g_ResizeSavedCorner == 1)
                {
                    // Bottom-right: right edge moves, left/top fixed
                    SetWindowPos(hwnd, nullptr, 0, 0,
                                 newW, newH, SWP_NOMOVE | SWP_NOZORDER);
                }
                else
                {
                    // Bottom-left: left edge moves, right edge stays
                    int newX = g_ResizeBaseX + (g_ResizeBaseW - newW);
                    SetWindowPos(hwnd, nullptr, newX, g_ResizeBaseY,
                                 newW, newH, SWP_NOZORDER);
                }
            }
        }
        prevLButton = curLButton;

        // ── Start ImGui frame (at correct window/D3D size) ──
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render application UI
        app.Render();

        // Render ImGui
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.1f, 0.1f, 0.2f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    // Cleanup — save config before exit
    app.SaveConfig();
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    UnregisterHotKey(hwnd, HOTKEY_ID_TRANSLATE);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(L"TranslationToolClass", hInstance);

    return 0;
}

// D3D11 device creation
static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
    );

    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer)
    {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}
