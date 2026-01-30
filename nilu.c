#define _CRT_SECURE_NO_WARNINGS
#define COBJMACROS // 开启C语言COM宏

// ========================== 头文件 ==========================
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <mmsystem.h> // MCI 接口
#include <tchar.h>
#include <stdio.h>
#include <string.h>
#include "Resource.h" // 必须包含资源头文件

// ========================== 链接库 ==========================
#pragma comment(lib, "winmm.lib")       // MCI 播放音频必须
#pragma comment(lib, "ole32.lib")       // 音量控制必须
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

// ========================== 【核心修复】直接定义 GUID 变量 ==========================
// CLSID_MMDeviceEnumerator: {BCDE0395-E52F-467C-8E3D-C4579291692E}
const GUID CLSID_MMDeviceEnumerator_C = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };

// IID_IMMDeviceEnumerator: {A95664D2-9614-4F35-A746-DE8DB63617E6}
const GUID IID_IMMDeviceEnumerator_C = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };

// IID_IAudioEndpointVolume: {5CDF2C82-841E-4546-9722-0CF74078229A}
const GUID IID_IAudioEndpointVolume_C = { 0x5CDF2C82, 0x841E, 0x4546, { 0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A } };

// IID_IMMDeviceCollection: {0BD7A1BE-7A1A-44DB-8397-CC5392387B5E}
const GUID IID_IMMDeviceCollection_C = { 0x0BD7A1BE, 0x7A1A, 0x44DB, { 0x83, 0x97, 0xCC, 0x53, 0x92, 0x38, 0x7B, 0x5E } };

// IID_IPropertyStore: {886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99}
const GUID IID_IPropertyStore_C = { 0x886D8EEB, 0x8CF2, 0x4446, { 0x8D, 0x02, 0xCD, 0xBA, 0x1D, 0xBD, 0xCF, 0x99 } };

// ========================== 全局配置 ==========================
#define TIMER_VOLUME    1001
#define TIMER_COUNTDOWN 1002
#define TIMER_UPDATE_UI 1003
#define TIMER_KEEP_TOP  1004
#define PASSWORD        _T("350234")
#define RESOURCE_ID     IDR_MP32 
#define RESOURCE_TYPE   _T("MP3")

// ========================== 全局变量 ==========================
HWND g_hMainWnd = NULL;
HWND g_hTimeLabel = NULL;  // 计时器显示标签
HWND g_hAuthorLabel = NULL; // 作者信息标签
BOOL g_isPasswordCorrect = FALSE;
UINT64 g_remainingTime = 7200;  // 2小时 = 7200秒
TCHAR g_szTempMP3Path[MAX_PATH] = { 0 };
BOOL g_isMusicPlaying = FALSE;

// ========================== 函数声明 ==========================
void CleanupAndExit(void);
HRESULT SetAllSpeakersVolumeToMax(void);
BOOL ExtractResourceToTempFile(UINT uResId, LPCTSTR lpResType, LPTSTR lpOutPath);
BOOL PlayMusicMCI(const TCHAR* szPath);
void StopMusicMCI(void);
void UpdateTimeDisplay(void);
void ForceAudioToSpeakers(void);
void EnsureWindowOnTop(void);

// ========================== 确保窗口始终置顶 ==========================
void EnsureWindowOnTop(void)
{
    if (g_hMainWnd && !g_isPasswordCorrect)
    {
        // 强制窗口置顶
        SetWindowPos(g_hMainWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        // 如果窗口被最小化，恢复它
        if (IsIconic(g_hMainWnd))
        {
            ShowWindow(g_hMainWnd, SW_RESTORE);
        }

        // 强制获得焦点
        SetForegroundWindow(g_hMainWnd);
        SetActiveWindow(g_hMainWnd);

        // 禁用其他窗口（可选，更激进的方式）
        // EnableWindow(GetDesktopWindow(), FALSE);
    }
}

// ========================== 更新时间显示 ==========================
void UpdateTimeDisplay(void)
{
    TCHAR szTimeText[128];
    UINT64 hours = g_remainingTime / 3600;
    UINT64 minutes = (g_remainingTime % 3600) / 60;
    UINT64 seconds = g_remainingTime % 60;

    _stprintf_s(szTimeText, 128, _T("剩余时间: %02llu:%02llu:%02llu"), hours, minutes, seconds);

    if (g_hTimeLabel)
    {
        SetWindowText(g_hTimeLabel, szTimeText);
    }
}

// ========================== 强制音频输出到扬声器 ==========================
void ForceAudioToSpeakers(void)
{
    // 使用 MCI 命令强制音频输出到扬声器
    mciSendString(_T("set my_bgm audio all on"), NULL, 0, NULL);

    // 使用 waveOutSetVolume 设置波形输出音量到最大
    DWORD dwVolume = 0xFFFFFFFF; // 左右声道都设为最大
    waveOutSetVolume(NULL, dwVolume);
}

// ========================== 定时器回调 ==========================
VOID CALLBACK CheckVolumeTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    if (g_isPasswordCorrect) return;
    SetAllSpeakersVolumeToMax();
    ForceAudioToSpeakers(); // 确保音频输出到扬声器
}

VOID CALLBACK CountdownTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    if (g_isPasswordCorrect) return;

    if (g_remainingTime > 0)
    {
        g_remainingTime--;
    }
    else
    {
        CleanupAndExit();
    }
}

VOID CALLBACK UpdateUITimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    if (g_isPasswordCorrect) return;
    UpdateTimeDisplay();
}

VOID CALLBACK KeepTopTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    if (g_isPasswordCorrect) return;
    EnsureWindowOnTop();
}

// ========================== 核心功能函数 ==========================

// 1. 提取资源
BOOL ExtractResourceToTempFile(UINT uResId, LPCTSTR lpResType, LPTSTR lpOutPath)
{
    TCHAR szTempDir[MAX_PATH];
    TCHAR szTempFileName[MAX_PATH];
    HRSRC hRes;
    HGLOBAL hResData;
    DWORD dwResSize;
    void* pResData;
    HANDLE hFile;
    DWORD dwWritten;

    if (!lpOutPath) return FALSE;

    if (GetTempPath(MAX_PATH, szTempDir) == 0) return FALSE;
    if (!GetTempFileName(szTempDir, _T("mp3"), 0, szTempFileName)) return FALSE;

    DeleteFile(szTempFileName);
    _stprintf_s(lpOutPath, MAX_PATH, _T("%s.mp3"), szTempFileName);

    hRes = FindResource(NULL, MAKEINTRESOURCE(uResId), lpResType);
    if (!hRes) return FALSE;

    hResData = LoadResource(NULL, hRes);
    if (!hResData) return FALSE;

    dwResSize = SizeofResource(NULL, hRes);
    if (dwResSize == 0) return FALSE;

    pResData = LockResource(hResData);
    if (!pResData) return FALSE;

    hFile = CreateFile(lpOutPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    if (!WriteFile(hFile, pResData, dwResSize, &dwWritten, NULL) || dwWritten != dwResSize)
    {
        CloseHandle(hFile);
        DeleteFile(lpOutPath);
        return FALSE;
    }

    CloseHandle(hFile);
    return TRUE;
}

// 2. MCI 播放音乐 - 强制使用扬声器
BOOL PlayMusicMCI(const TCHAR* szPath)
{
    TCHAR szShortPath[MAX_PATH] = { 0 };
    TCHAR szCommand[1024] = { 0 };
    MCIERROR mciError;

    if (!szPath || _tcslen(szPath) == 0) return FALSE;
    if (GetFileAttributes(szPath) == INVALID_FILE_ATTRIBUTES) return FALSE;

    if (GetShortPathName(szPath, szShortPath, MAX_PATH) == 0)
    {
        _tcscpy_s(szShortPath, MAX_PATH, szPath);
    }

    mciSendString(_T("close my_bgm"), NULL, 0, NULL);

    // 打开音频文件并指定使用扬声器
    _stprintf_s(szCommand, 1024, _T("open \"%s\" type mpegvideo alias my_bgm"), szShortPath);
    mciError = mciSendString(szCommand, NULL, 0, NULL);
    if (mciError != 0) return FALSE;

    // 设置音频输出到扬声器
    mciSendString(_T("set my_bgm audio all on"), NULL, 0, NULL);

    // 设置音量到最大
    mciSendString(_T("setaudio my_bgm volume to 1000"), NULL, 0, NULL);

    // 开始循环播放
    mciError = mciSendString(_T("play my_bgm repeat"), NULL, 0, NULL);
    if (mciError != 0)
    {
        mciSendString(_T("close my_bgm"), NULL, 0, NULL);
        return FALSE;
    }

    g_isMusicPlaying = TRUE;

    // 强制音频输出到扬声器
    ForceAudioToSpeakers();

    return TRUE;
}

// 3. 停止播放
void StopMusicMCI(void)
{
    if (g_isMusicPlaying)
    {
        mciSendString(_T("stop my_bgm"), NULL, 0, NULL);
        mciSendString(_T("close my_bgm"), NULL, 0, NULL);
        g_isMusicPlaying = FALSE;
    }
}

// 4. 设置所有扬声器音量到最大
HRESULT SetAllSpeakersVolumeToMax(void)
{
    HRESULT hr;
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDeviceCollection* pCollection = NULL;
    IMMDevice* pDevice = NULL;
    IAudioEndpointVolume* pEndpointVolume = NULL;
    UINT deviceCount = 0;
    UINT i;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_C, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator_C, (void**)&pEnumerator);
    if (FAILED(hr)) return hr;

    // 首先设置默认设备
    hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(pEnumerator, eRender, eConsole, &pDevice);
    if (SUCCEEDED(hr))
    {
        hr = pDevice->lpVtbl->Activate(pDevice, &IID_IAudioEndpointVolume_C, CLSCTX_ALL, NULL, (void**)&pEndpointVolume);
        if (SUCCEEDED(hr))
        {
            pEndpointVolume->lpVtbl->SetMute(pEndpointVolume, FALSE, NULL);
            pEndpointVolume->lpVtbl->SetMasterVolumeLevelScalar(pEndpointVolume, 1.0f, NULL);
            pEndpointVolume->lpVtbl->Release(pEndpointVolume);
        }
        pDevice->lpVtbl->Release(pDevice);
        pDevice = NULL;
    }

    // 然后设置所有活动的音频设备
    hr = pEnumerator->lpVtbl->EnumAudioEndpoints(pEnumerator, eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (SUCCEEDED(hr))
    {
        hr = pCollection->lpVtbl->GetCount(pCollection, &deviceCount);
        if (SUCCEEDED(hr))
        {
            for (i = 0; i < deviceCount; i++)
            {
                hr = pCollection->lpVtbl->Item(pCollection, i, &pDevice);
                if (SUCCEEDED(hr))
                {
                    hr = pDevice->lpVtbl->Activate(pDevice, &IID_IAudioEndpointVolume_C, CLSCTX_ALL, NULL, (void**)&pEndpointVolume);
                    if (SUCCEEDED(hr))
                    {
                        // 取消静音并设置最大音量
                        pEndpointVolume->lpVtbl->SetMute(pEndpointVolume, FALSE, NULL);
                        pEndpointVolume->lpVtbl->SetMasterVolumeLevelScalar(pEndpointVolume, 1.0f, NULL);
                        pEndpointVolume->lpVtbl->Release(pEndpointVolume);
                    }
                    pDevice->lpVtbl->Release(pDevice);
                }
            }
        }
        pCollection->lpVtbl->Release(pCollection);
    }

    pEnumerator->lpVtbl->Release(pEnumerator);

    // 使用传统的 waveOut API 设置音量
    DWORD dwVolume = 0xFFFFFFFF; // 左右声道都设为最大
    waveOutSetVolume(NULL, dwVolume);

    return hr;
}

// 5. 清理退出
void CleanupAndExit(void)
{
    if (g_hMainWnd)
    {
        KillTimer(g_hMainWnd, TIMER_VOLUME);
        KillTimer(g_hMainWnd, TIMER_COUNTDOWN);
        KillTimer(g_hMainWnd, TIMER_UPDATE_UI);
        KillTimer(g_hMainWnd, TIMER_KEEP_TOP);
    }

    StopMusicMCI();

    if (g_szTempMP3Path[0] != 0)
    {
        SetFileAttributes(g_szTempMP3Path, FILE_ATTRIBUTE_NORMAL);
        DeleteFile(g_szTempMP3Path);
        memset(g_szTempMP3Path, 0, sizeof(g_szTempMP3Path));
    }

    PostQuitMessage(0);
}

// ========================== 窗口过程 ==========================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HFONT hFont = NULL;
    static HFONT hTimeFont = NULL;
    static HFONT hAuthorFont = NULL;
    TCHAR inputPwd[256];

    switch (message)
    {
    case WM_CREATE:
        // 启动定时器
        SetTimer(hWnd, TIMER_VOLUME, 500, CheckVolumeTimer);
        SetTimer(hWnd, TIMER_COUNTDOWN, 1000, CountdownTimer);
        SetTimer(hWnd, TIMER_UPDATE_UI, 1000, UpdateUITimer);
        SetTimer(hWnd, TIMER_KEEP_TOP, 2000, KeepTopTimer); // 每2秒检查一次置顶状态

        // 创建时间显示标签 (顶部，红色大字)
        g_hTimeLabel = CreateWindow(_T("STATIC"), _T("剩余时间: 02:00:00"),
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            50, 30, 400, 40, hWnd, (HMENU)103, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        // 创建密码提示标签
        CreateWindow(_T("STATIC"), _T("输入解锁密码"), WS_CHILD | WS_VISIBLE | SS_CENTER,
            100, 100, 300, 30, hWnd, (HMENU)100, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        // 创建密码输入框 (更大)
        CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
            100, 140, 300, 40, hWnd, (HMENU)101, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        // 创建验证按钮 (更大)
        CreateWindow(_T("BUTTON"), _T("验证密码"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            100, 200, 300, 40, hWnd, (HMENU)102, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        // 创建作者信息标签 (右下角)
        g_hAuthorLabel = CreateWindow(_T("STATIC"), _T("作者：缪佳聪"),
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            320, 320, 150, 20, hWnd, (HMENU)104, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        // 设置普通字体 (稍大)
        hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

        // 设置时间显示字体 (更大的红色字体)
        hTimeFont = CreateFont(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

        // 设置作者信息字体 (小字体)
        hAuthorFont = CreateFont(12, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

        if (hFont)
        {
            SendDlgItemMessage(hWnd, 100, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hWnd, 101, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hWnd, 102, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        if (hTimeFont && g_hTimeLabel)
        {
            SendMessage(g_hTimeLabel, WM_SETFONT, (WPARAM)hTimeFont, TRUE);
        }

        if (hAuthorFont && g_hAuthorLabel)
        {
            SendMessage(g_hAuthorLabel, WM_SETFONT, (WPARAM)hAuthorFont, TRUE);
        }

        // 设置焦点到密码输入框
        SetFocus(GetDlgItem(hWnd, 101));

        // 初始化时间显示
        UpdateTimeDisplay();

        // 确保窗口置顶
        EnsureWindowOnTop();
        break;

    case WM_ACTIVATE:
        // 当窗口激活状态改变时，确保置顶
        if (!g_isPasswordCorrect && LOWORD(wParam) != WA_INACTIVE)
        {
            EnsureWindowOnTop();
        }
        break;

    case WM_WINDOWPOSCHANGING:
        // 阻止窗口失去置顶状态
        if (!g_isPasswordCorrect)
        {
            LPWINDOWPOS lpwp = (LPWINDOWPOS)lParam;
            if (!(lpwp->flags & SWP_NOZORDER))
            {
                lpwp->hwndInsertAfter = HWND_TOPMOST;
            }
        }
        break;

    case WM_CTLCOLORSTATIC:
        // 设置时间显示为红色
        if ((HWND)lParam == g_hTimeLabel)
        {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 0, 0)); // 红色文字
            SetBkColor(hdc, GetSysColor(COLOR_BTNFACE)); // 背景色
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        // 设置作者信息为灰色
        else if ((HWND)lParam == g_hAuthorLabel)
        {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(128, 128, 128)); // 灰色文字
            SetBkColor(hdc, GetSysColor(COLOR_BTNFACE)); // 背景色
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == 102) // 验证按钮
        {
            memset(inputPwd, 0, sizeof(inputPwd));
            GetDlgItemText(hWnd, 101, inputPwd, 256);

            if (_tcscmp(inputPwd, PASSWORD) == 0)
            {
                g_isPasswordCorrect = TRUE;
                CleanupAndExit();
                DestroyWindow(hWnd);
            }
            else
            {
                SetDlgItemText(hWnd, 101, _T("")); // 清空输入框
                SetFocus(GetDlgItem(hWnd, 101));   // 重新设置焦点

                // 惩罚机制：延长播放时间
                g_remainingTime = (UINT64)(g_remainingTime * 1.5);
                UpdateTimeDisplay(); // 立即更新显示

                // 确保窗口仍然置顶
                EnsureWindowOnTop();
            }
        }
        break;

    case WM_KEYDOWN:
        // 支持回车键验证
        if (wParam == VK_RETURN)
        {
            SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(102, BN_CLICKED), (LPARAM)GetDlgItem(hWnd, 102));
        }
        break;

    case WM_SYSCOMMAND:
        // 阻止某些系统命令（如关闭、最小化等）
        if (!g_isPasswordCorrect)
        {
            switch (wParam & 0xFFF0)
            {
            case SC_CLOSE:
            case SC_MINIMIZE:
                return 0; // 阻止关闭和最小化
            }
        }
        break;

    case WM_CLOSE:
        if (!g_isPasswordCorrect)
        {
            return 0; // 阻止窗口关闭
        }
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        // 清理字体资源
        if (hFont)
        {
            DeleteObject(hFont);
            hFont = NULL;
        }
        if (hTimeFont)
        {
            DeleteObject(hTimeFont);
            hTimeFont = NULL;
        }
        if (hAuthorFont)
        {
            DeleteObject(hAuthorFont);
            hAuthorFont = NULL;
        }

        // 如果不是正常退出，执行清理
        if (!g_isPasswordCorrect)
        {
            CleanupAndExit();
        }
        else
        {
            PostQuitMessage(0);
        }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ========================== 主函数 ==========================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEX wcex;
    MSG msg;

    // 初始化 COM
    if (FAILED(CoInitialize(NULL))) return 1;

    // 1. 提取资源
    if (!ExtractResourceToTempFile(RESOURCE_ID, RESOURCE_TYPE, g_szTempMP3Path))
    {
        CoUninitialize();
        return 1;
    }

    // 2. 播放音乐（强制使用扬声器）
    PlayMusicMCI(g_szTempMP3Path);

    // 3. 设置所有扬声器音量到最大
    SetAllSpeakersVolumeToMax();

    // 4. 注册窗口类
    memset(&wcex, 0, sizeof(WNDCLASSEX));
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszClassName = _T("MusicPlayerPwdWnd");
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wcex))
    {
        CleanupAndExit();
        CoUninitialize();
        return 1;
    }

    // 5. 创建主窗口 (更大的尺寸)
    g_hMainWnd = CreateWindowEx(
        WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_TOPMOST,
        _T("MusicPlayerPwdWnd"),
        _T("解锁系统"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, // 移除最小化按钮
        (GetSystemMetrics(SM_CXSCREEN) - 500) / 2, // 居中显示更大的窗口
        (GetSystemMetrics(SM_CYSCREEN) - 350) / 2,
        500, 350, // 扩大窗口尺寸
        NULL, NULL, hInstance, NULL
    );

    if (!g_hMainWnd)
    {
        CleanupAndExit();
        CoUninitialize();
        return 1;
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    // 确保窗口置顶
    EnsureWindowOnTop();

    // 6. 消息循环
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 7. 程序退出清理
    StopMusicMCI();
    CoUninitialize();

    return (int)msg.wParam;
}
