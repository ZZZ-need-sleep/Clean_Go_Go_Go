/*
 * main.c - startup, single-instance guard, startup prompts, self-test and CLI.
 * SPDX-License-Identifier: MIT
 */
#include "app.h"
#include <shellapi.h>
#include <string.h>

static HANDLE s_mutex = NULL;

static BOOL MainFileTimeIsZero(const FILETIME *ft)
{
    return ft->dwLowDateTime == 0 && ft->dwHighDateTime == 0;
}

/* ------------------------------------------------------------------ */
/* diagnostics                                                         */
/* ------------------------------------------------------------------ */

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *ep)
{
    static volatile LONG inside = 0;
    if (InterlockedExchange(&inside, 1) == 0)
    {
        wchar_t detail[768];
        wchar_t full[1024];
        DWORD code = 0;
        ULONG_PTR address = 0;
        if (ep != NULL && ep->ExceptionRecord != NULL)
        {
            code = ep->ExceptionRecord->ExceptionCode;
            address = (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress;
        }
        StringCchPrintfW(detail, ARRAYSIZE(detail),
                         L"程序发生未处理异常（异常代码 0x%08lX，地址 0x%llX）。",
                         (unsigned long)code, (unsigned long long)address);
        Util_LogErrorf(L"%ls", detail);
        StringCchPrintfW(full, ARRAYSIZE(full),
                         L"异常报错：%ls\n请将此报错信息反馈至开发者%ls",
                         detail, APP_DEVELOPER_EMAIL);
        MessageBoxW(NULL, full, L"文件清理 - 异常报错",
                    MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void InvalidParameterHandler(const wchar_t *expression, const wchar_t *function,
                                    const wchar_t *file, unsigned int line, uintptr_t reserved)
{
    wchar_t detail[768];
    wchar_t full[1024];
    (void)reserved;
    StringCchPrintfW(detail, ARRAYSIZE(detail),
                     L"程序调用参数异常（%ls，函数 %ls，位置 %ls:%u）。",
                     expression ? expression : L"未知表达式",
                     function ? function : L"未知",
                     file ? file : L"未知", line);
    Util_LogErrorf(L"%ls", detail);
    StringCchPrintfW(full, ARRAYSIZE(full),
                     L"异常报错：%ls\n请将此报错信息反馈至开发者%ls",
                     detail, APP_DEVELOPER_EMAIL);
    MessageBoxW(NULL, full, L"文件清理 - 异常报错",
                MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    ExitProcess(1);
}

static void InitDpi(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI * PFN_SetProcessDpiAwarenessContext)(HANDLE);
    typedef UINT(WINAPI * PFN_GetDpiForSystem)(void);
    PFN_SetProcessDpiAwarenessContext set_ctx = NULL;
    PFN_GetDpiForSystem get_dpi = NULL;
    HDC dc;

    if (user32 != NULL)
    {
        set_ctx = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        get_dpi = (PFN_GetDpiForSystem)GetProcAddress(user32, "GetDpiForSystem");
    }
    if (set_ctx != NULL)
    {
        /* -4 == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
        set_ctx((HANDLE)(INT_PTR)-4);
    }
    else
    {
        SetProcessDPIAware();
    }

    g_dpi = 96;
    if (get_dpi != NULL)
    {
        UINT d = get_dpi();
        if (d >= 72 && d <= 480)
            g_dpi = (int)d;
    }
    if (g_dpi == 96)
    {
        dc = GetDC(NULL);
        if (dc != NULL)
        {
            int d = GetDeviceCaps(dc, LOGPIXELSX);
            ReleaseDC(NULL, dc);
            if (d >= 72 && d <= 480)
                g_dpi = d;
        }
    }
    if (g_dpi <= 0)
        g_dpi = 96;
    g_dpi_scale_100 = MulDiv(g_dpi, 100, 96);
}

/* ------------------------------------------------------------------ */
/* command line / test setup                                           */
/* ------------------------------------------------------------------ */

typedef struct Options
{
    BOOL selftest;
    BOOL test_scan;
    const wchar_t *scan_dir;
    const wchar_t *data_dir;
} Options;

static void ParseOptions(Options *opt)
{
    int argc = 0;
    wchar_t **argv;
    int i;

    ZeroMemory(opt, sizeof(*opt));
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL)
        return;
    for (i = 1; i < argc; i++)
    {
        if (wcscmp(argv[i], L"--selftest") == 0)
            opt->selftest = TRUE;
        else if (wcscmp(argv[i], L"--test-scan") == 0 && i + 1 < argc)
        {
            opt->test_scan = TRUE;
            opt->scan_dir = argv[++i];
        }
        else if (wcscmp(argv[i], L"--data-dir") == 0 && i + 1 < argc)
        {
            opt->data_dir = argv[++i];
        }
    }
    /* argv must live until the paths are copied; copy data/scan dirs here. */
    if (opt->data_dir != NULL)
        opt->data_dir = Util_WcsDup(opt->data_dir);
    if (opt->scan_dir != NULL)
        opt->scan_dir = Util_WcsDup(opt->scan_dir);
    LocalFree(argv);
}

static void FreeOptions(Options *opt)
{
    free((void *)opt->data_dir);
    free((void *)opt->scan_dir);
    opt->data_dir = NULL;
    opt->scan_dir = NULL;
}

static BOOL MakeTempDataDir(wchar_t *out, size_t cch, const wchar_t *tag)
{
    wchar_t temp[MAX_PATH];
    DWORD n = GetTempPathW(ARRAYSIZE(temp), temp);
    if (n == 0 || n >= ARRAYSIZE(temp))
        return FALSE;
    StringCchPrintfW(out, cch, L"%lsXZQ_%ls_%lu", temp, tag, (unsigned long)GetCurrentProcessId());
    return Util_EnsureDirectory(out);
}

/* ------------------------------------------------------------------ */
/* self-test                                                           */
/* ------------------------------------------------------------------ */

static void TestLog(FILE *fp, const wchar_t *fmt, ...)
{
    va_list ap;
    wchar_t *line;
    char *utf8;
    if (fp == NULL)
        return;
    va_start(ap, fmt);
    line = Str_FormatV(fmt, ap);
    va_end(ap);
    utf8 = Util_Utf8Dup(line);
    if (utf8 != NULL)
    {
        fprintf(fp, "%s\r\n", utf8);
        free(utf8);
    }
    free(line);
    fflush(fp);
}

static BOOL TestCheck(FILE *fp, BOOL condition, const wchar_t *name)
{
    TestLog(fp, L"[%ls] %ls", condition ? L"PASS" : L"FAIL", name);
    return condition;
}

static uint64_t FileSizeOf(const wchar_t *path)
{
    wchar_t *api = Path_ApiDup(path);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    uint64_t size = 0;
    if (GetFileAttributesExW(api, GetFileExInfoStandard, &fad))
        size = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    free(api);
    return size;
}

static void ClearTestSettings(void)
{
    int i;
    Scan_ClearQuickRoots();
    for (i = 0; i < g_protected_dir_count; i++)
    {
        free(g_protected_dirs[i]);
        g_protected_dirs[i] = NULL;
    }
    g_protected_dir_count = 0;
    Store_ClearProtectedExts();
}

int SelfTest_Run(void)
{
    wchar_t log_path[APP_PATH_CCH];
    FILE *fp;
    BOOL all_ok = TRUE;
    FILETIME now_ft;
    ULARGE_INTEGER now_u;
    SYSTEMTIME cutoff;
    FILETIME cutoff_ft;
    ULARGE_INTEGER cutoff_u;
    uint64_t age_100ns;
    ScanStats stats;
    int rc;
    int i;

    StringCchPrintfW(log_path, ARRAYSIZE(log_path), L"%ls\\%ls", g_app_dir, SELFTEST_FILENAME);
    fp = _wfopen(log_path, L"wb");
    if (fp != NULL)
        fwrite("\xEF\xBB\xBF", 1, 3, fp);
    TestLog(fp, L"文件清理 自检报告");
    TestLog(fp, L"版本: %ls", APP_VERSION);
    TestLog(fp, L"数据目录: %ls", g_app_dir);

    all_ok &= TestCheck(fp, Guard_IsAllowedExeName(L"C:\\x\\Genshin.exe"), L"允许的文件名 Genshin.exe");
    all_ok &= TestCheck(fp, Guard_IsAllowedExeName(L"C:\\x\\YuanShen.EXE"), L"允许的文件名 YuanShen.EXE");
    all_ok &= TestCheck(fp, Guard_IsAllowedExeName(L"C:\\x\\LXOZVQE.exe"), L"允许的文件名 LXOZVQE.exe");
    all_ok &= TestCheck(fp, !Guard_IsAllowedExeName(L"C:\\x\\Genshin2.exe"), L"拒绝未授权文件名");
    all_ok &= TestCheck(fp, Path_ExtIsOffice(L"docx"), L"办公文件后缀识别");
    all_ok &= TestCheck(fp, Path_ExtIsSystemCritical(L"dll"), L"系统保护后缀识别");
    all_ok &= TestCheck(fp, Path_IsUnder(L"C:\\Users\\A\\B.txt", L"C:\\Users\\A"), L"路径前缀匹配");
    all_ok &= TestCheck(fp, !Path_IsUnder(L"C:\\Users\\AB\\x.txt", L"C:\\Users\\A"), L"路径边界匹配");
    all_ok &= TestCheck(fp, Path_IsSystemCritical(L"C:\\Windows\\System32\\user32.dll", 0),
                        L"Windows 系统文件保护");
    all_ok &= TestCheck(fp, !Path_IsSystemCritical(L"C:\\Users\\Administrator\\Documents\\a.docx", 0),
                        L"普通文档不误判");

    cutoff = Time_GetCleanupCutoff();
    all_ok &= TestCheck(fp, cutoff.wDayOfWeek == 0 && cutoff.wHour == 0 && cutoff.wMinute == 0,
                        L"周清理分界为周日 00:00");
    Time_CutoffFileTime(&cutoff, &cutoff_ft);
    Time_NowFileTime(&now_ft);
    now_u.LowPart = now_ft.dwLowDateTime;
    now_u.HighPart = now_ft.dwHighDateTime;
    cutoff_u.LowPart = cutoff_ft.dwLowDateTime;
    cutoff_u.HighPart = cutoff_ft.dwHighDateTime;
    age_100ns = (now_u.QuadPart > cutoff_u.QuadPart) ? (now_u.QuadPart - cutoff_u.QuadPart) : 0;
    all_ok &= TestCheck(fp, age_100ns >= (uint64_t)7 * 24 * 60 * 60 * 10000000ULL,
                        L"清理分界严格早于一周");

    Data_Reset();
    ClearTestSettings();
    Store_SetDefaultProtectedExts();
    all_ok &= TestCheck(fp, Path_ExtIsUserProtected(L"json") &&
                             Path_ExtIsUserProtected(L"bat") &&
                             Path_ExtIsUserProtected(L"ini") &&
                             Path_ExtIsUserProtected(L"xml"),
                        L"默认后缀保护包含 bat/json/ini/xml");
    Time_NowFileTime(&now_ft);
    {
        FILETIME mtime = now_ft;
        Data_Add(L"C:\\Users\\Administrator\\Documents\\年度报告.docx", 1234, &mtime, REC_FLAG_OFFICE);
        Data_Add(L"C:\\Users\\Administrator\\Pictures\\照片.jpg", 5678, &mtime, 0);
        Data_Add(L"C:\\Users\\Administrator\\Documents\\保护区\\重要合同.docx", 999, &mtime, REC_FLAG_OFFICE);
        Data_Add(L"C:\\Users\\Administrator\\Documents\\settings.json", 4321, &mtime, 0);
        Data_Add(L"C:\\Users\\Administrator\\Documents\\项目\\run.bat", 321, &mtime, 0);
    }
    g_protected_dirs[0] = Util_WcsDup(L"C:\\Users\\Administrator\\Documents\\保护区");
    g_protected_dir_count = 1;
    g_quick_roots[0] = Util_WcsDup(L"C:\\Users\\Administrator\\Documents");
    g_quick_roots[1] = Util_WcsDup(L"C:\\Users\\Administrator\\Pictures");
    g_quick_root_count = 2;
    Data_ApplyProtection();
    g_scan_start_ft = now_ft;
    g_scan_end_ft = now_ft;
    g_scan_seconds = 42;
    InterlockedExchange64(&g_total_scanned, 100);
    InterlockedExchange64(&g_hidden_count, 10);
    InterlockedExchange64(&g_hidden_bytes, 999999);
    InterlockedExchange64(&g_skipped_dirs, 2);
    g_scan_partial = TRUE;

    rc = Store_Save(NULL, 0);
    all_ok &= TestCheck(fp, rc == 0, L"data.xzq 保存");
    all_ok &= TestCheck(fp, Store_DataExists(), L"data.xzq 存在");
    all_ok &= TestCheck(fp, FileSizeOf(g_data_path) > 64, L"data.xzq 非空");

    Data_Reset();
    ClearTestSettings();
    rc = Store_Load(NULL, 0);
    all_ok &= TestCheck(fp, rc == 0, L"data.xzq 重新读取");
    Data_GetStats(&stats);
    all_ok &= TestCheck(fp, Data_Count() == 5, L"记录数量回读一致");
    all_ok &= TestCheck(fp, g_quick_root_count == 2, L"快速扫描目录回读一致");
    all_ok &= TestCheck(fp, g_protected_dir_count == 1, L"保护区目录回读一致");
    all_ok &= TestCheck(fp, g_protected_ext_count > 0, L"后缀保护列表回读一致");
    all_ok &= TestCheck(fp, stats.total_scanned == 100, L"扫描统计回读一致");
    all_ok &= TestCheck(fp, g_scan_partial, L"未完成扫描标记回读一致");
    all_ok &= TestCheck(fp, stats.protected_count == 1, L"保护区统计正确");
    all_ok &= TestCheck(fp, stats.ext_protected_count == 2, L"后缀保护统计正确");
    {
        int office_count = 0;
        int protected_count = 0;
        int ext_protected_count = 0;
        for (i = 0; i < Data_Count(); i++)
        {
            FileRec *r = Data_Get(i);
            if (r != NULL && (r->flags & REC_FLAG_OFFICE) != 0)
                office_count++;
            if (r != NULL && (r->flags & REC_FLAG_PROTECTED) != 0)
                protected_count++;
            if (r != NULL && (r->flags & REC_FLAG_EXT_PROTECTED) != 0)
                ext_protected_count++;
        }
        all_ok &= TestCheck(fp, office_count == 2, L"办公文件标记回读一致");
        all_ok &= TestCheck(fp, protected_count == 1, L"保护区文件标记回读一致");
        all_ok &= TestCheck(fp, ext_protected_count == 2, L"后缀保护文件标记回读一致");
    }

    all_ok &= TestCheck(fp, Store_RunLogOpen(NULL, 0) == 0, L"run_find.xzq 创建");
    all_ok &= TestCheck(fp, Store_RunLogRow(L"C:\\Users\\Administrator\\Documents\\年度报告.docx", L"docx",
                                            1234, &now_ft, L"可清理", NULL, 0) == 0,
                        L"run_find.xzq 实时写入");
    all_ok &= TestCheck(fp, Store_RunLogClose(TRUE, 42, NULL, 0) == 0, L"run_find.xzq 关闭");
    all_ok &= TestCheck(fp, FileSizeOf(g_runlog_path) > 64, L"run_find.xzq 非空");

    g_scan_partial = FALSE;
    Store_Save(NULL, 0); /* leave the test data in a clean, non-partial state */
    TestLog(fp, L"");
    TestLog(fp, L"RESULT: %ls", all_ok ? L"PASS" : L"FAIL");
    TestLog(fp, L"日志: %ls", log_path);
    if (fp != NULL)
        fclose(fp);
    return all_ok ? 0 : 1;
}

int ScanTest_Run(const wchar_t *directory)
{
    wchar_t log_path[APP_PATH_CCH];
    wchar_t err[APP_ERR_CCH];
    wchar_t *roots[1];
    FILE *fp;
    int rc;
    ScanStats st;
    BOOL ok;

    roots[0] = (wchar_t *)directory;
    Store_SetDefaultProtectedExts();
    StringCchPrintfW(log_path, ARRAYSIZE(log_path), L"%ls\\%ls", g_app_dir, SCANTEST_FILENAME);
    fp = _wfopen(log_path, L"wb");
    if (fp != NULL)
        fwrite("\xEF\xBB\xBF", 1, 3, fp);
    TestLog(fp, L"文件清理 扫描测试");
    TestLog(fp, L"扫描目录: %ls", directory);

    rc = Scan_RunBlocking(SCAN_MODE_DIRECTORY, roots, 1, err, ARRAYSIZE(err));
    Data_GetStats(&st);
    TestLog(fp, L"扫描返回码: %d", rc);
    TestLog(fp, L"错误信息: %ls", err[0] ? err : L"(无)");
    TestLog(fp, L"总计扫描: %llu", (unsigned long long)st.total_scanned);
    TestLog(fp, L"可清理: %llu", (unsigned long long)st.clean_count);
    TestLog(fp, L"保护区: %llu", (unsigned long long)st.protected_count);
    TestLog(fp, L"后缀保护: %llu", (unsigned long long)st.ext_protected_count);
    TestLog(fp, L"系统保护: %llu", (unsigned long long)st.hidden_count);
    TestLog(fp, L"跳过目录: %llu", (unsigned long long)st.skipped_dirs);
    TestLog(fp, L"数据文件: %ls (%llu 字节)", g_data_path,
            (unsigned long long)FileSizeOf(g_data_path));
    TestLog(fp, L"实时记录: %ls (%llu 字节)", g_runlog_path,
            (unsigned long long)FileSizeOf(g_runlog_path));

    ok = (rc == 0) && Store_DataExists() && FileSizeOf(g_data_path) > 64 &&
         FileSizeOf(g_runlog_path) > 64 && st.total_scanned > 0;
    TestLog(fp, L"RESULT: %ls", ok ? L"PASS" : L"FAIL");
    if (fp != NULL)
        fclose(fp);
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* startup                                                             */
/* ------------------------------------------------------------------ */

static BOOL LoadStartupData(BOOL *have_data)
{
    wchar_t err[APP_ERR_CCH];
    ScanStats st;
    int rc;

    *have_data = FALSE;
    if (!Store_DataExists())
    {
        if (Store_CreateEmpty(err, ARRAYSIZE(err)) != 0)
        {
            Util_ErrorShowf(L"%ls", err);
            return FALSE;
        }
        return TRUE;
    }

    rc = Store_Load(err, ARRAYSIZE(err));
    if (rc == 0)
    {
        Data_GetStats(&st);
        *have_data = (st.total_scanned > 0 || st.record_count > 0);
        return TRUE;
    }
    if (rc == 1)
    {
        if (Store_CreateEmpty(err, ARRAYSIZE(err)) != 0)
        {
            Util_ErrorShowf(L"%ls", err);
            return FALSE;
        }
        return TRUE;
    }

    Util_ErrorShowf(L"%ls", err[0] ? err : L"无法读取扫描数据。");
    if (MessageBoxW(NULL,
                    L"扫描数据文件已经损坏。\r\n\r\n是否备份损坏文件并重新开始？\r\n（选择“否”将退出程序）",
                    APP_NAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1) != IDYES)
        return FALSE;
    {
        wchar_t stamp[64];
        wchar_t backup[APP_PATH_CCH];
        SYSTEMTIME now;
        GetLocalTime(&now);
        StringCchPrintfW(stamp, ARRAYSIZE(stamp), L"%04u%02u%02u-%02u%02u%02u",
                         now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        StringCchPrintfW(backup, ARRAYSIZE(backup), L"%ls.corrupt-%ls", g_data_path, stamp);
        if (!MoveFileExW(g_data_path, backup, MOVEFILE_REPLACE_EXISTING))
        {
            Util_ErrorShowf(L"无法备份损坏的数据文件（错误码 %lu）。",
                            (unsigned long)GetLastError());
            return FALSE;
        }
        if (Store_CreateEmpty(err, ARRAYSIZE(err)) != 0)
        {
            Util_ErrorShowf(L"%ls", err);
            return FALSE;
        }
        *have_data = FALSE;
    }
    return TRUE;
}

static BOOL BuildPreviousDataText(wchar_t *out, size_t cch)
{
    wchar_t when[64];
    ScanStats st;
    Data_GetStats(&st);
    if (MainFileTimeIsZero(&g_scan_end_ft))
        StringCchCopyW(when, ARRAYSIZE(when), L"未知");
    else
        Time_FileTimeToString(&g_scan_end_ft, when, ARRAYSIZE(when));
    StringCchPrintfW(out, cch,
                     L"检测到上一次扫描结果。\r\n\r\n这是上一次的结果，未重新扫描。\r\n扫描时间：%ls\r\n文件总数：%llu\r\n可清理：%llu\r\n保护区：%llu\r\n系统保护（已隐藏）：%llu\r\n%s\r\n是否直接使用上一次的结果？\r\n\r\n选择“否”将重新扫描；选择“取消”将退出程序。",
                     when,
                     (unsigned long long)st.total_scanned,
                     (unsigned long long)st.clean_count,
                     (unsigned long long)st.protected_count,
                     (unsigned long long)st.hidden_count,
                     g_scan_partial ? L"注意：上一次扫描未完成。" : L"");
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    Options opt;
    HANDLE mutex = NULL;
    BOOL com_initialized = FALSE;
    BOOL have_data = FALSE;
    BOOL pending_scan = FALSE;
    ScanMode pending_mode = SCAN_MODE_QUICK;
    BOOL started = FALSE;
    INITCOMMONCONTROLSEX icc;
    MSG msg;
    int exit_code = 0;

    (void)prev;
    (void)cmdline;
    (void)show;
    g_hinst = hinst;

    InitDpi();
    Guard_Enforce();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(CrashFilter);
    _set_invalid_parameter_handler(InvalidParameterHandler);
    ParseOptions(&opt);

    if (opt.selftest || opt.test_scan)
    {
        wchar_t temp_dir[APP_PATH_CCH];
        if (opt.data_dir != NULL)
            Util_InitPaths(opt.data_dir);
        else
        {
            if (!MakeTempDataDir(temp_dir, ARRAYSIZE(temp_dir),
                                 opt.selftest ? L"selftest" : L"scantest"))
            {
                MessageBoxW(NULL, L"异常报错：无法创建临时测试目录。\n请将此报错信息反馈至开发者3814859587@qq.com",
                            L"文件清理 - 异常报错", MB_OK | MB_ICONERROR);
                FreeOptions(&opt);
                return 1;
            }
            Util_InitPaths(temp_dir);
        }
        if (!Util_EnsureDirectory(g_app_dir))
        {
            MessageBoxW(NULL, L"异常报错：无法创建测试数据目录。\n请将此报错信息反馈至开发者3814859587@qq.com",
                        L"文件清理 - 异常报错", MB_OK | MB_ICONERROR);
            FreeOptions(&opt);
            return 1;
        }
        Util_InitSystemPaths();
        if (opt.selftest)
            exit_code = SelfTest_Run();
        else
            exit_code = ScanTest_Run(opt.scan_dir ? opt.scan_dir : L".");
        FreeOptions(&opt);
        return exit_code;
    }

    /*
     * Initialize COM before any shell path / directory helper.  Shell APIs
     * such as SHCreateDirectoryEx are documented to require COM, and on some
     * systems they fail with an unhelpful error if called too early.
     */
    if (SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        com_initialized = TRUE;

    Util_InitPaths(opt.data_dir);
    {
        DWORD mkdir_error = Util_EnsureDirectoryEx(g_app_dir);
        if (mkdir_error != ERROR_SUCCESS)
        {
            wchar_t sys_error[512];
            Util_FormatWin32Error(mkdir_error, sys_error, ARRAYSIZE(sys_error));
            Util_Fatalf(L"无法创建数据目录：%ls\n系统错误码：%lu（%ls）\n请检查该目录的写入权限，或将本程序加入安全软件的信任名单。",
                        g_app_dir, (unsigned long)mkdir_error, sys_error);
        }
    }
    Util_InitSystemPaths();

    ZeroMemory(&icc, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    mutex = CreateMutexW(NULL, TRUE, L"Local\\XZQ_FileCleaner_SingleInstance_v1");
    if (mutex != NULL && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(NULL, L"文件清理已经在运行中。", APP_NAME,
                    MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        CloseHandle(mutex);
        FreeOptions(&opt);
        if (com_initialized)
            CoUninitialize();
        return 0;
    }
    s_mutex = mutex;

    if (!LoadStartupData(&have_data))
    {
        if (s_mutex != NULL) { ReleaseMutex(s_mutex); CloseHandle(s_mutex); s_mutex = NULL; }
        FreeOptions(&opt);
        if (com_initialized)
            CoUninitialize();
        return 1;
    }

    if (!have_data)
    {
        if (MessageBoxW(NULL,
                        L"没有使用数据。\r\n\r\n是否现在扫盘去查找文件？",
                        APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_DEFBUTTON1) == IDYES)
        {
            int mode = UI_AskScanMode(NULL);
            if (mode >= 0)
            {
                pending_scan = TRUE;
                pending_mode = (ScanMode)mode;
            }
        }
    }
    else
    {
        wchar_t text[2048];
        int answer;
        BuildPreviousDataText(text, ARRAYSIZE(text));
        answer = MessageBoxW(NULL, text, APP_NAME,
                             MB_YESNOCANCEL | MB_ICONQUESTION | MB_TOPMOST | MB_DEFBUTTON1);
        if (answer == IDCANCEL)
        {
            if (s_mutex != NULL) { ReleaseMutex(s_mutex); CloseHandle(s_mutex); s_mutex = NULL; }
            FreeOptions(&opt);
            if (com_initialized)
                CoUninitialize();
            return 0;
        }
        if (answer == IDNO)
        {
            int mode = UI_AskScanMode(NULL);
            if (mode >= 0)
            {
                pending_scan = TRUE;
                pending_mode = (ScanMode)mode;
            }
        }
    }

    UI_SetUsingPreviousData(have_data);
    if (!UI_CreateMainWindow(hinst))
    {
        if (s_mutex != NULL) { ReleaseMutex(s_mutex); CloseHandle(s_mutex); s_mutex = NULL; }
        FreeOptions(&opt);
        if (com_initialized)
            CoUninitialize();
        return 1;
    }
    UI_RefreshAll();
    List_ScrollToTop();

    if (pending_scan)
    {
        if (pending_mode == SCAN_MODE_DIRECTORY)
        {
            wchar_t *dir = UI_PickFolder(g_main_window, L"选择要扫描的文件夹");
            if (dir != NULL)
            {
                UI_StartScan(SCAN_MODE_DIRECTORY, dir);
                free(dir);
                started = TRUE;
            }
        }
        else
        {
            UI_StartScan(pending_mode, NULL);
            started = TRUE;
        }
    }
    (void)started;

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (s_mutex != NULL)
    {
        ReleaseMutex(s_mutex);
        CloseHandle(s_mutex);
        s_mutex = NULL;
    }
    FreeOptions(&opt);
    if (com_initialized)
        CoUninitialize();
    return (int)msg.wParam;
}
