/*
 * util.c - strings, paths, time, diagnostics and filename guard.
 * SPDX-License-Identifier: MIT
 */
#include "app.h"
#include <wctype.h>
#include <string.h>
#include <io.h>

HINSTANCE g_hinst = NULL;
HWND      g_main_window = NULL;
int       g_dpi = 96;
int       g_dpi_scale_100 = 100;

wchar_t g_app_dir[APP_PATH_CCH] = {0};
wchar_t g_data_path[APP_PATH_CCH] = {0};
wchar_t g_runlog_path[APP_PATH_CCH] = {0};
wchar_t g_errorlog_path[APP_PATH_CCH] = {0};
wchar_t g_exe_path[APP_EXE_PATH_CCH] = {0};
wchar_t g_exe_dir[APP_EXE_PATH_CCH] = {0};

wchar_t g_windows_dir[APP_PATH_CCH] = {0};
wchar_t g_program_files[APP_PATH_CCH] = {0};
wchar_t g_program_files_x86[APP_PATH_CCH] = {0};
wchar_t g_programdata_ms[APP_PATH_CCH] = {0};
wchar_t g_recycle_hint[APP_PATH_CCH] = {0};
wchar_t g_svi_hint[APP_PATH_CCH] = {0};

FileRec *g_rec_chunks[REC_MAX_CHUNKS] = {0};
volatile LONG g_rec_count = 0;

volatile LONG64 g_total_scanned = 0;
volatile LONG64 g_hidden_count = 0;
volatile LONG64 g_hidden_bytes = 0;
volatile LONG64 g_clean_count = 0;
volatile LONG64 g_clean_bytes = 0;
volatile LONG64 g_protected_count = 0;
volatile LONG64 g_protected_bytes = 0;
volatile LONG64 g_ext_protected_count = 0;
volatile LONG64 g_ext_protected_bytes = 0;
volatile LONG64 g_unsupported_count = 0;
volatile LONG64 g_unsupported_bytes = 0;
volatile LONG64 g_skipped_dirs = 0;

FILETIME g_scan_start_ft;
FILETIME g_scan_end_ft;
uint64_t g_scan_seconds = 0;
ULONGLONG g_scan_start_tick = 0;
volatile LONG g_scan_running = 0;
volatile LONG g_stop_requested = 0;
wchar_t g_scan_error[APP_ERR_CCH] = {0};
wchar_t g_current_scan_path[512] = {0};
BOOL g_scan_partial = FALSE;
BOOL g_data_dirty = FALSE;

wchar_t *g_quick_roots[QUICK_ROOT_MAX] = {0};
int g_quick_root_count = 0;
wchar_t *g_protected_dirs[PROTECTED_DIR_MAX] = {0};
int g_protected_dir_count = 0;
wchar_t *g_protected_exts[PROTECTED_EXT_MAX] = {0};
int g_protected_ext_count = 0;

/* ------------------------------------------------------------------ */
/* allocation                                                          */
/* ------------------------------------------------------------------ */

void *Util_Alloc(size_t bytes)
{
    void *p;
    if (bytes == 0)
        bytes = 1;
    p = malloc(bytes);
    if (p == NULL)
    {
        MessageBoxW(NULL,
                    L"异常报错：内存分配失败，程序无法继续运行。\n请将此报错信息反馈至开发者3814859587@qq.com",
                    L"文件清理 - 异常报错",
                    MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        ExitProcess(1);
    }
    return p;
}

void *Util_Realloc(void *ptr, size_t bytes)
{
    void *p;
    if (bytes == 0)
        bytes = 1;
    p = realloc(ptr, bytes);
    if (p == NULL)
    {
        MessageBoxW(NULL,
                    L"异常报错：内存扩展失败，程序无法继续运行。\n请将此报错信息反馈至开发者3814859587@qq.com",
                    L"文件清理 - 异常报错",
                    MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        ExitProcess(1);
    }
    return p;
}

wchar_t *Util_WcsDup(const wchar_t *s)
{
    size_t n;
    wchar_t *p;
    if (s == NULL)
        s = L"";
    n = wcslen(s);
    p = (wchar_t *)Util_Alloc((n + 1) * sizeof(wchar_t));
    memcpy(p, s, (n + 1) * sizeof(wchar_t));
    return p;
}

char *Util_Utf8Dup(const wchar_t *s)
{
    int need;
    char *p;
    if (s == NULL)
        s = L"";
    need = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, NULL, 0, NULL, NULL);
    if (need <= 0)
        return NULL;
    p = (char *)Util_Alloc((size_t)need);
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, p, need, NULL, NULL) <= 0)
    {
        free(p);
        return NULL;
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* formatting / strings                                                */
/* ------------------------------------------------------------------ */

wchar_t *Str_FormatV(const wchar_t *fmt, va_list ap)
{
    va_list ap2;
    int n;
    wchar_t *buf;

#ifndef va_copy
#define va_copy(d, s) ((d) = (s))
#endif
    va_copy(ap2, ap);
    n = _vscwprintf(fmt, ap2);
    va_end(ap2);
    if (n < 0)
        n = 0;
    buf = (wchar_t *)Util_Alloc(((size_t)n + 1) * sizeof(wchar_t));
    va_copy(ap2, ap);
    if (vswprintf(buf, (size_t)n + 1, fmt, ap2) < 0)
        buf[0] = 0;
    va_end(ap2);
    return buf;
}

wchar_t *Str_Format(const wchar_t *fmt, ...)
{
    wchar_t *result;
    va_list ap;
    va_start(ap, fmt);
    result = Str_FormatV(fmt, ap);
    va_end(ap);
    return result;
}

void Str_ToLower(wchar_t *s)
{
    if (s != NULL)
    {
        while (*s != 0)
        {
            *s = (wchar_t)towlower(*s);
            s++;
        }
    }
}

BOOL Str_EqualsNoCase(const wchar_t *a, const wchar_t *b)
{
    if (a == NULL || b == NULL)
        return FALSE;
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

/* ------------------------------------------------------------------ */
/* logging and errors                                                  */
/* ------------------------------------------------------------------ */

void Util_LogErrorf(const wchar_t *fmt, ...)
{
    va_list ap;
    wchar_t *detail = NULL;
    wchar_t *line = NULL;
    char *utf8 = NULL;
    SYSTEMTIME st;
    FILE *fp;
    wchar_t stamp[64];

    if (g_errorlog_path[0] == 0)
        return;

    va_start(ap, fmt);
    detail = Str_FormatV(fmt, ap);
    va_end(ap);

    GetLocalTime(&st);
    Time_SystemToString(&st, stamp, ARRAYSIZE(stamp));
    line = Str_Format(L"[%ls] %ls\r\n", stamp, detail ? detail : L"(null)");
    utf8 = Util_Utf8Dup(line);

    if (utf8 != NULL)
    {
        fp = _wfopen(g_errorlog_path, L"ab");
        if (fp != NULL)
        {
            fwrite(utf8, 1, strlen(utf8), fp);
            fclose(fp);
        }
        free(utf8);
    }
    free(line);
    free(detail);
}

void Util_ErrorShowf(const wchar_t *fmt, ...)
{
    va_list ap;
    wchar_t *detail;
    wchar_t *full;
    HWND owner = g_main_window;

    va_start(ap, fmt);
    detail = Str_FormatV(fmt, ap);
    va_end(ap);

    if (detail == NULL)
        detail = Util_WcsDup(L"未知错误。");
    Util_LogErrorf(L"%ls", detail);
    full = Str_Format(L"异常报错：%ls\n请将此报错信息反馈至开发者%ls", detail, APP_DEVELOPER_EMAIL);
    if (owner != NULL && !IsWindow(owner))
        owner = NULL;
    MessageBoxW(owner, full, L"文件清理 - 异常报错",
                MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    free(full);
    free(detail);
}

void Util_ErrorShow(const wchar_t *message)
{
    Util_ErrorShowf(L"%ls", message ? message : L"未知错误。");
}

void Util_Fatalf(const wchar_t *fmt, ...)
{
    va_list ap;
    wchar_t *detail;

    va_start(ap, fmt);
    detail = Str_FormatV(fmt, ap);
    va_end(ap);
    Util_ErrorShow(detail ? detail : L"未知致命错误。");
    free(detail);
    ExitProcess(1);
}

void Util_Info(const wchar_t *message, const wchar_t *title)
{
    HWND owner = g_main_window;
    if (owner != NULL && !IsWindow(owner))
        owner = NULL;
    MessageBoxW(owner, message, title ? title : APP_NAME,
                MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

/* ------------------------------------------------------------------ */
/* paths                                                               */
/* ------------------------------------------------------------------ */

void Util_InitPaths(const wchar_t *override_data_dir)
{
    wchar_t base[APP_PATH_CCH];
    wchar_t full[APP_PATH_CCH];
    wchar_t appdata[APP_PATH_CCH];
    wchar_t *p;

    base[0] = 0;
    appdata[0] = 0;

    if (override_data_dir != NULL && override_data_dir[0] != 0)
    {
        if (GetFullPathNameW(override_data_dir, ARRAYSIZE(full), full, NULL) == 0)
            StringCchCopyW(full, ARRAYSIZE(full), override_data_dir);
        StringCchCopyW(base, ARRAYSIZE(base), full);
    }
    else
    {
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL,
                                    SHGFP_TYPE_CURRENT, appdata)))
        {
            if (GetEnvironmentVariableW(L"APPDATA", appdata, ARRAYSIZE(appdata)) == 0)
                StringCchCopyW(appdata, ARRAYSIZE(appdata), L"C:\\Users\\Administrator\\AppData\\Roaming");
        }
        StringCchPrintfW(base, ARRAYSIZE(base), L"%ls\\%ls", appdata, L"文件清理");
    }

    Path_TrimTrailingSlash(base);
    StringCchCopyW(g_app_dir, ARRAYSIZE(g_app_dir), base);
    StringCchPrintfW(g_data_path, ARRAYSIZE(g_data_path), L"%ls\\%ls", g_app_dir, DATA_FILENAME);
    StringCchPrintfW(g_runlog_path, ARRAYSIZE(g_runlog_path), L"%ls\\%ls", g_app_dir, RUNLOG_FILENAME);
    StringCchPrintfW(g_errorlog_path, ARRAYSIZE(g_errorlog_path), L"%ls\\%ls", g_app_dir, ERRORLOG_FILENAME);

    if (GetModuleFileNameW(NULL, g_exe_path, APP_EXE_PATH_CCH) == 0)
        g_exe_path[0] = 0;
    StringCchCopyW(g_exe_dir, ARRAYSIZE(g_exe_dir), g_exe_path);
    p = wcsrchr(g_exe_dir, L'\\');
    if (p != NULL)
        *p = 0;
}

DWORD Util_EnsureDirectoryEx(const wchar_t *dir)
{
    DWORD attrs;
    DWORD rc;
    wchar_t *tmp;
    size_t len;
    size_t i;

    if (dir == NULL || dir[0] == 0)
        return ERROR_INVALID_PARAMETER;

    attrs = GetFileAttributesW(dir);
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        rc = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? ERROR_SUCCESS : ERROR_FILE_EXISTS;
        SetLastError(rc);
        return rc;
    }

    /*
     * SHCreateDirectoryEx can fail before COM/shell initialization on some
     * systems.  Keep it as the first attempt, then fall back to plain
     * CreateDirectoryW component by component so the result is deterministic.
     */
    rc = SHCreateDirectoryExW(NULL, dir, NULL);
    attrs = GetFileAttributesW(dir);
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            SetLastError(ERROR_FILE_EXISTS);
            return ERROR_FILE_EXISTS;
        }
        SetLastError(ERROR_SUCCESS);
        return ERROR_SUCCESS;
    }
    if (rc == ERROR_SUCCESS)
        rc = ERROR_PATH_NOT_FOUND;

    tmp = Util_WcsDup(dir);
    len = wcslen(tmp);
    for (i = 3; i < len; i++)
    {
        if (tmp[i] == L'\\')
        {
            DWORD create_error;
            tmp[i] = 0;
            if (!CreateDirectoryW(tmp, NULL))
            {
                create_error = GetLastError();
                if (create_error != ERROR_ALREADY_EXISTS)
                    rc = create_error;
            }
            tmp[i] = L'\\';
        }
    }
    if (rc != ERROR_ACCESS_DENIED)
    {
        if (!CreateDirectoryW(tmp, NULL))
        {
            DWORD create_error = GetLastError();
            if (create_error != ERROR_ALREADY_EXISTS)
                rc = create_error;
        }
    }
    free(tmp);

    attrs = GetFileAttributesW(dir);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        SetLastError(ERROR_SUCCESS);
        return ERROR_SUCCESS;
    }
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        rc = ERROR_FILE_EXISTS;
    if (rc == ERROR_SUCCESS)
        rc = ERROR_ACCESS_DENIED;
    SetLastError(rc);
    return rc;
}

BOOL Util_EnsureDirectory(const wchar_t *dir)
{
    DWORD rc = Util_EnsureDirectoryEx(dir);
    SetLastError(rc);
    return rc == ERROR_SUCCESS;
}

void Util_FormatWin32Error(DWORD code, wchar_t *out, size_t cch)
{
    DWORD n;
    if (out == NULL || cch == 0)
        return;
    out[0] = 0;
    n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       out, (DWORD)cch, NULL);
    if (n == 0)
    {
        StringCchPrintfW(out, cch, L"Windows 错误 %lu", (unsigned long)code);
        return;
    }
    while (n > 0 && (out[n - 1] == L'\r' || out[n - 1] == L'\n' ||
                     out[n - 1] == L' ' || out[n - 1] == L'\t'))
    {
        out[--n] = 0;
    }
}

void Util_InitSystemPaths(void)
{
    wchar_t tmp[APP_PATH_CCH];
    wchar_t drive[16];

    g_windows_dir[0] = 0;
    if (GetWindowsDirectoryW(g_windows_dir, ARRAYSIZE(g_windows_dir)) == 0)
        StringCchCopyW(g_windows_dir, ARRAYSIZE(g_windows_dir), L"C:\\Windows");
    Path_TrimTrailingSlash(g_windows_dir);

    g_program_files[0] = 0;
    if (GetEnvironmentVariableW(L"ProgramFiles", g_program_files, ARRAYSIZE(g_program_files)) == 0)
        StringCchCopyW(g_program_files, ARRAYSIZE(g_program_files), L"C:\\Program Files");
    Path_TrimTrailingSlash(g_program_files);

    g_program_files_x86[0] = 0;
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", g_program_files_x86, ARRAYSIZE(g_program_files_x86)) == 0)
        g_program_files_x86[0] = 0;
    Path_TrimTrailingSlash(g_program_files_x86);

    tmp[0] = 0;
    if (GetEnvironmentVariableW(L"ProgramData", tmp, ARRAYSIZE(tmp)) > 0)
        StringCchPrintfW(g_programdata_ms, ARRAYSIZE(g_programdata_ms), L"%ls\\Microsoft", tmp);
    else
        g_programdata_ms[0] = 0;

    drive[0] = 0;
    if (GetEnvironmentVariableW(L"SystemDrive", drive, ARRAYSIZE(drive)) == 0)
        StringCchCopyW(drive, ARRAYSIZE(drive), L"C:");
    StringCchPrintfW(g_recycle_hint, ARRAYSIZE(g_recycle_hint), L"%ls\\$Recycle.Bin", drive);
    StringCchPrintfW(g_svi_hint, ARRAYSIZE(g_svi_hint), L"%ls\\System Volume Information", drive);
}

const wchar_t *Path_FileName(const wchar_t *path)
{
    const wchar_t *a;
    const wchar_t *b;
    if (path == NULL)
        return L"";
    a = wcsrchr(path, L'\\');
    b = wcsrchr(path, L'/');
    if (a == NULL || (b != NULL && b > a))
        a = b;
    return (a == NULL) ? path : a + 1;
}

size_t Path_ParentLength(const wchar_t *path)
{
    const wchar_t *last;
    if (path == NULL)
        return 0;
    last = wcsrchr(path, L'\\');
    if (last == NULL)
        return 0;
    if (last == path)
        return 0;
    if (last == path + 2 && path[1] == L':')
        return 3;
    return (size_t)(last - path);
}

wchar_t *Path_ParentDup(const wchar_t *path)
{
    size_t n = Path_ParentLength(path);
    wchar_t *p;
    if (n == 0)
        return Util_WcsDup(L"");
    p = (wchar_t *)Util_Alloc((n + 1) * sizeof(wchar_t));
    memcpy(p, path, n * sizeof(wchar_t));
    p[n] = 0;
    return p;
}

void Path_TrimTrailingSlash(wchar_t *path)
{
    size_t n;
    if (path == NULL)
        return;
    n = wcslen(path);
    while (n > 3 && (path[n - 1] == L'\\' || path[n - 1] == L'/'))
    {
        path[n - 1] = 0;
        n--;
    }
    if (n == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        path[2] = L'\\';
    if (n == 2 && path[1] == L':')
    {
        path[2] = L'\\';
        path[3] = 0;
    }
}

wchar_t *Path_NormalizeDirDup(const wchar_t *dir)
{
    wchar_t *tmp;
    wchar_t *full;
    DWORD need;
    size_t i;

    if (dir == NULL)
        return Util_WcsDup(L"");
    tmp = Util_WcsDup(dir);
    while (*tmp == L' ' || *tmp == L'\t')
        memmove(tmp, tmp + 1, (wcslen(tmp)) * sizeof(wchar_t));
    Path_TrimTrailingSlash(tmp);
    for (i = 0; tmp[i] != 0; i++)
    {
        if (tmp[i] == L'/')
            tmp[i] = L'\\';
    }
    need = GetFullPathNameW(tmp, 0, NULL, NULL);
    if (need == 0)
    {
        Path_TrimTrailingSlash(tmp);
        return tmp;
    }
    full = (wchar_t *)Util_Alloc(((size_t)need + 2) * sizeof(wchar_t));
    if (GetFullPathNameW(tmp, need + 1, full, NULL) == 0)
    {
        free(full);
        Path_TrimTrailingSlash(tmp);
        return tmp;
    }
    free(tmp);
    Path_TrimTrailingSlash(full);
    return full;
}

BOOL Path_IsUnder(const wchar_t *path, const wchar_t *dir)
{
    size_t dl;
    if (path == NULL || dir == NULL || dir[0] == 0)
        return FALSE;
    dl = wcslen(dir);
    while (dl > 3 && (dir[dl - 1] == L'\\' || dir[dl - 1] == L'/'))
        dl--;
    if (_wcsnicmp(path, dir, dl) != 0)
        return FALSE;
    if (dir[dl - 1] == L'\\')
        return TRUE;
    if (path[dl] == 0 || path[dl] == L'\\' || path[dl] == L'/')
        return TRUE;
    return FALSE;
}

BOOL Path_IsSameOrUnderAppDir(const wchar_t *path)
{
    if (g_app_dir[0] == 0)
        return FALSE;
    return Path_IsUnder(path, g_app_dir);
}

const wchar_t *Path_Extension(const wchar_t *path)
{
    const wchar_t *base;
    const wchar_t *dot;
    if (path == NULL)
        return L"";
    base = Path_FileName(path);
    dot = wcsrchr(base, L'.');
    if (dot == NULL || dot == base)
        return L"";
    return dot + 1;
}

void Path_ExtensionLower(const wchar_t *path, wchar_t *out, size_t cch)
{
    const wchar_t *ext;
    size_t i;
    if (out == NULL || cch == 0)
        return;
    out[0] = 0;
    ext = Path_Extension(path);
    if (ext == NULL || ext[0] == 0)
        return;
    StringCchCopyW(out, cch, ext);
    for (i = 0; out[i] != 0; i++)
        out[i] = (wchar_t)towlower(out[i]);
}

wchar_t *Path_ApiDup(const wchar_t *path)
{
    wchar_t *p;
    size_t n;
    if (path == NULL)
        return Util_WcsDup(L"");
    if (wcsncmp(path, L"\\\\?\\", 4) == 0)
        return Util_WcsDup(path);
    n = wcslen(path);
    if (n < 248)
        return Util_WcsDup(path);

    if (wcsncmp(path, L"\\\\", 2) == 0)
    {
        /* \\server\share -> \\?\UNC\server\share */
        p = (wchar_t *)Util_Alloc((n + 8) * sizeof(wchar_t));
        StringCchPrintfW(p, n + 8, L"\\\\?\\UNC\\%ls", path + 2);
        return p;
    }
    p = (wchar_t *)Util_Alloc((n + 5) * sizeof(wchar_t));
    StringCchPrintfW(p, n + 5, L"\\\\?\\%ls", path);
    return p;
}

/* ------------------------------------------------------------------ */
/* classification                                                      */
/* ------------------------------------------------------------------ */

static const wchar_t *kOfficeExts[] = {
    L"doc", L"docx", L"docm", L"dot", L"dotx", L"dotm",
    L"xls", L"xlsx", L"xlsm", L"xlsb", L"xlt", L"xltx",
    L"ppt", L"pptx", L"pptm", L"pps", L"ppsx", L"pot", L"potx",
    L"pdf", L"txt", L"rtf", L"csv", L"odt", L"ods", L"odp",
    L"wps", L"et", L"dps", L"md", L"pages", L"numbers", L"key"
};

static const wchar_t *kCriticalExts[] = {
    L"dll", L"sys", L"drv", L"ocx", L"cpl", L"scr", L"efi",
    L"mui", L"msi", L"msp", L"mst", L"cat", L"inf", L"winmd",
    L"tlb", L"ax", L"acm", L"ime", L"rs", L"pdb"
};

/* The cleanup whitelist requested by the product owner.  Deletion is only
 * allowed for these six documented categories.  Scanning still records other
 * files, but they are shown as "不可清理". */
static const wchar_t *kCleanupOffice[] = {
    L"doc", L"docx", L"docm", L"dot", L"dotx", L"dotm", L"rtf", L"txt",
    L"wps", L"wpt", L"odt", L"uof", L"uot", L"mhtml", L"mht", L"html", L"htm",
    L"xls", L"xlsx", L"xlsm", L"xlt", L"xltx", L"xltm", L"csv", L"et", L"ett", L"ods",
    L"ppt", L"pptx", L"pptm", L"pps", L"ppsx", L"ppsm", L"pot", L"potx", L"potm",
    L"dps", L"dpt", L"odp", L"pdf", L"ofd", L"otl", L"dbt", L"one"
};

static const wchar_t *kCleanupMedia[] = {
    L"mp4", L"m4v", L"mkv", L"avi", L"mov", L"wmv", L"asf", L"flv", L"f4v",
    L"webm", L"ts", L"m3u8", L"mpg", L"mpeg", L"3gp", L"3g2", L"mxf",
    L"vob", L"swf", L"mp3", L"aac", L"m4a", L"flac", L"wav", L"ogg",
    L"opus", L"wma", L"ac3", L"eac3", L"amr", L"mid", L"midi", L"cda"
};

static const wchar_t *kCleanupArchive[] = {
    L"zip", L"rar", L"7z", L"tar", L"gz", L"bz2", L"xz", L"br", L"lz",
    L"lzma", L"zst", L"cab", L"tgz", L"tbz2", L"txz", L"cpio", L"shar",
    L"ar", L"a", L"jar", L"war", L"ear", L"apk", L"iso", L"dmg",
    L"deb", L"rpm", L"alz", L"ace", L"zoo", L"lbr", L"lqr"
};

static const wchar_t *kCleanupWeb[] = {
    L"html", L"htm", L"xhtml", L"css", L"js", L"mjs", L"cjs", L"json",
    L"jsonld", L"xml", L"rss", L"atom", L"php", L"asp", L"aspx", L"jsp",
    L"wasm", L"map", L"webmanifest", L"woff", L"woff2", L"eot"
};

static const wchar_t *kCleanupMail[] = {
    L"eml", L"emlx", L"msg", L"oft", L"ics", L"vcf", L"vcard", L"mhtml", L"mht"
};

static const wchar_t *kCleanup3D[] = {
    L"obj", L"mtl", L"fbx", L"stl", L"glb", L"gltf", L"usdz", L"usd",
    L"3mf", L"3ds", L"dae", L"ply", L"blend", L"max", L"ma", L"mb"
};

typedef struct CleanupCategory
{
    const wchar_t *name;
    const wchar_t *const *exts;
    size_t count;
} CleanupCategory;

static const CleanupCategory kCleanupCategories[] = {
    { L"办公文档", kCleanupOffice, ARRAYSIZE(kCleanupOffice) },
    { L"音视频", kCleanupMedia, ARRAYSIZE(kCleanupMedia) },
    { L"压缩与归档", kCleanupArchive, ARRAYSIZE(kCleanupArchive) },
    { L"网页与 Web 资源", kCleanupWeb, ARRAYSIZE(kCleanupWeb) },
    { L"邮件、日历与联系人", kCleanupMail, ARRAYSIZE(kCleanupMail) },
    { L"3D 模型", kCleanup3D, ARRAYSIZE(kCleanup3D) }
};

static BOOL ExtInList(const wchar_t *ext, const wchar_t *const *list, size_t count)
{
    size_t i;
    if (ext == NULL || ext[0] == 0)
        return FALSE;
    for (i = 0; i < count; i++)
    {
        if (_wcsicmp(ext, list[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

BOOL Path_ExtIsOffice(const wchar_t *ext)
{
    return ExtInList(ext, kOfficeExts, ARRAYSIZE(kOfficeExts));
}

BOOL Path_ExtIsSystemCritical(const wchar_t *ext)
{
    return ExtInList(ext, kCriticalExts, ARRAYSIZE(kCriticalExts));
}

BOOL Path_IsWindowsProtected(const wchar_t *path)
{
    return g_windows_dir[0] != 0 && Path_IsUnder(path, g_windows_dir);
}

BOOL Path_ExtIsUserProtected(const wchar_t *ext)
{
    int i;
    if (ext == NULL || ext[0] == 0)
        return FALSE;
    for (i = 0; i < g_protected_ext_count; i++)
    {
        if (g_protected_exts[i] != NULL && _wcsicmp(ext, g_protected_exts[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

BOOL Path_ExtIsCleanupAllowed(const wchar_t *ext)
{
    size_t i;
    if (ext == NULL || ext[0] == 0)
        return FALSE;
    for (i = 0; i < ARRAYSIZE(kCleanupCategories); i++)
    {
        if (ExtInList(ext, kCleanupCategories[i].exts, kCleanupCategories[i].count))
            return TRUE;
    }
    return FALSE;
}

int Cleanup_FormatCategoryCount(void)
{
    return (int)ARRAYSIZE(kCleanupCategories);
}

const wchar_t *Cleanup_FormatCategoryName(int index)
{
    if (index < 0 || index >= (int)ARRAYSIZE(kCleanupCategories))
        return L"";
    return kCleanupCategories[index].name;
}

const wchar_t *const *Cleanup_FormatCategoryExtensions(int index, int *count)
{
    if (count != NULL)
        *count = 0;
    if (index < 0 || index >= (int)ARRAYSIZE(kCleanupCategories))
        return NULL;
    if (count != NULL)
        *count = (int)kCleanupCategories[index].count;
    return kCleanupCategories[index].exts;
}

BOOL Path_IsSystemCritical(const wchar_t *path, DWORD attrs)
{
    wchar_t ext[32];
    const wchar_t *name;

    if (path == NULL)
        return TRUE;
    if ((attrs & FILE_ATTRIBUTE_SYSTEM) != 0)
        return TRUE;

    Path_ExtensionLower(path, ext, ARRAYSIZE(ext));
    if (Path_ExtIsSystemCritical(ext))
        return TRUE;

    if (g_windows_dir[0] != 0 && Path_IsUnder(path, g_windows_dir))
        return TRUE;
    if (g_program_files[0] != 0 && Path_IsUnder(path, g_program_files))
        return TRUE;
    if (g_program_files_x86[0] != 0 && Path_IsUnder(path, g_program_files_x86))
        return TRUE;
    if (g_programdata_ms[0] != 0 && Path_IsUnder(path, g_programdata_ms))
        return TRUE;
    if (g_recycle_hint[0] != 0 && Path_IsUnder(path, g_recycle_hint))
        return TRUE;
    if (g_svi_hint[0] != 0 && Path_IsUnder(path, g_svi_hint))
        return TRUE;
    if (Path_IsSameOrUnderAppDir(path))
        return TRUE;
    if (g_exe_path[0] != 0 && _wcsicmp(path, g_exe_path) == 0)
        return TRUE;

    name = Path_FileName(path);
    if (_wcsicmp(name, L"pagefile.sys") == 0 ||
        _wcsicmp(name, L"hiberfil.sys") == 0 ||
        _wcsicmp(name, L"swapfile.sys") == 0 ||
        _wcsicmp(name, L"bootmgr") == 0 ||
        _wcsicmp(name, L"bootnxt") == 0 ||
        _wcsicmp(name, L"ntuser.dat") == 0)
        return TRUE;

    return FALSE;
}

/* ------------------------------------------------------------------ */
/* formatting time / size                                              */
/* ------------------------------------------------------------------ */

void Size_Format(uint64_t bytes, wchar_t *out, size_t cch)
{
    const wchar_t *unit;
    double value;

    if (out == NULL || cch == 0)
        return;
    if (bytes < 1024ULL)
    {
        StringCchPrintfW(out, cch, L"%llu B", (unsigned long long)bytes);
        return;
    }
    if (bytes < 1024ULL * 1024ULL)
    {
        value = (double)bytes / 1024.0;
        unit = L"KB";
    }
    else if (bytes < 1024ULL * 1024ULL * 1024ULL)
    {
        value = (double)bytes / (1024.0 * 1024.0);
        unit = L"MB";
    }
    else if (bytes < 1024ULL * 1024ULL * 1024ULL * 1024ULL)
    {
        value = (double)bytes / (1024.0 * 1024.0 * 1024.0);
        unit = L"GB";
    }
    else
    {
        value = (double)bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0);
        unit = L"TB";
    }
    StringCchPrintfW(out, cch, L"%.2f %ls", value, unit);
}

void Time_NowFileTime(FILETIME *ft)
{
    if (ft != NULL)
        GetSystemTimeAsFileTime(ft);
}

void Time_FileTimeToLocal(const FILETIME *ft, SYSTEMTIME *st)
{
    FILETIME local;
    if (ft == NULL || st == NULL)
        return;
    if (!FileTimeToLocalFileTime(ft, &local) || !FileTimeToSystemTime(&local, st))
        ZeroMemory(st, sizeof(*st));
}

void Time_SystemToString(const SYSTEMTIME *st, wchar_t *out, size_t cch)
{
    if (out == NULL || cch == 0)
        return;
    if (st == NULL)
    {
        out[0] = 0;
        return;
    }
    StringCchPrintfW(out, cch, L"%04u-%02u-%02u %02u:%02u:%02u",
                     st->wYear, st->wMonth, st->wDay,
                     st->wHour, st->wMinute, st->wSecond);
}

void Time_FileTimeToString(const FILETIME *ft, wchar_t *out, size_t cch)
{
    SYSTEMTIME st;
    if (out == NULL || cch == 0)
        return;
    Time_FileTimeToLocal(ft, &st);
    if (st.wYear == 0)
    {
        StringCchCopyW(out, cch, L"—");
        return;
    }
    StringCchPrintfW(out, cch, L"%04u-%02u-%02u %02u:%02u",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

void Time_NowString(wchar_t *out, size_t cch)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    Time_SystemToString(&st, out, cch);
}

static void Time_ToTm(const time_t *t, struct tm *out)
{
#ifdef _MSC_VER
    if (localtime_s(out, t) != 0)
        ZeroMemory(out, sizeof(*out));
#else
    if (localtime_r(t, out) == NULL)
        ZeroMemory(out, sizeof(*out));
#endif
}

SYSTEMTIME Time_GetCleanupCutoff(void)
{
    time_t now = time(NULL);
    time_t limit = now - (time_t)(7 * 24 * 60 * 60);
    time_t week_start;
    struct tm lt;
    struct tm wk;
    struct tm cut;
    SYSTEMTIME st;

    ZeroMemory(&st, sizeof(st));
    Time_ToTm(&now, &lt);

    wk = lt;
    wk.tm_mday -= wk.tm_wday;   /* Sunday == 0 */
    wk.tm_hour = 0;
    wk.tm_min = 0;
    wk.tm_sec = 0;
    wk.tm_isdst = -1;
    week_start = mktime(&wk);
    if (week_start == (time_t)-1)
        week_start = now - (time_t)(7 * 24 * 60 * 60);

    if (week_start > limit)
        week_start -= (time_t)(7 * 24 * 60 * 60);

    Time_ToTm(&week_start, &cut);
    st.wYear = (WORD)(cut.tm_year + 1900);
    st.wMonth = (WORD)(cut.tm_mon + 1);
    st.wDay = (WORD)cut.tm_mday;
    st.wDayOfWeek = (WORD)cut.tm_wday;
    st.wHour = 0;
    st.wMinute = 0;
    st.wSecond = 0;
    st.wMilliseconds = 0;
    return st;
}

void Time_CutoffFileTime(const SYSTEMTIME *cutoff, FILETIME *ft)
{
    SYSTEMTIME utc;
    if (cutoff == NULL || ft == NULL)
        return;
    if (TzSpecificLocalTimeToSystemTime(NULL, (SYSTEMTIME *)cutoff, &utc))
    {
        if (SystemTimeToFileTime(&utc, ft))
            return;
    }
    SystemTimeToFileTime(cutoff, ft); /* last-resort fallback */
}

BOOL Time_FileTimeOlderThan(const FILETIME *ft, const FILETIME *cutoff)
{
    if (ft == NULL || cutoff == NULL)
        return FALSE;
    return CompareFileTime(ft, cutoff) < 0;
}

/* ------------------------------------------------------------------ */
/* startup filename guard                                              */
/* ------------------------------------------------------------------ */

BOOL Guard_IsAllowedExeName(const wchar_t *path)
{
    const wchar_t *name;
    wchar_t stem[256];
    size_t n;

    if (path == NULL)
        return FALSE;
    name = Path_FileName(path);
    StringCchCopyW(stem, ARRAYSIZE(stem), name);
    n = wcslen(stem);
    if (n > 4 && _wcsicmp(stem + n - 4, L".exe") == 0)
        stem[n - 4] = 0;

    if (Str_EqualsNoCase(stem, APP_EXE_ALLOWED_1))
        return TRUE;
    if (Str_EqualsNoCase(stem, APP_EXE_ALLOWED_2))
        return TRUE;
    if (Str_EqualsNoCase(stem, APP_EXE_ALLOWED_3))
        return TRUE;
    return FALSE;
}

void Guard_Enforce(void)
{
    DWORD n;

    g_exe_path[0] = 0;
    n = GetModuleFileNameW(NULL, g_exe_path, APP_EXE_PATH_CCH);
    if (n == 0 || n >= APP_EXE_PATH_CCH || !Guard_IsAllowedExeName(g_exe_path))
    {
        MessageBoxW(NULL,
                    L"程序异常反编译防护已执行，详情请联系开发者3814859587@qq.com",
                    L"安全异常",
                    MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        ExitProcess(1);
    }
}
