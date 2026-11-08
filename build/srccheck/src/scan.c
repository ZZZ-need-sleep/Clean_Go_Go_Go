/*
 * scan.c - directory traversal, quick-scan roots and scan worker thread.
 * SPDX-License-Identifier: MIT
 */
#include "app.h"
#include <knownfolders.h>
#include <string.h>
#include <wchar.h>

typedef struct ScanJob
{
    ScanMode mode;
    wchar_t **roots;
    int root_count;
} ScanJob;

static SRWLOCK s_path_lock = SRWLOCK_INIT;
HANDLE g_scan_thread = NULL;

/* ------------------------------------------------------------------ */
/* small root-list helpers                                             */
/* ------------------------------------------------------------------ */

static void RootsPush(wchar_t ***roots, int *count, int *cap, const wchar_t *path)
{
    if (*count >= *cap)
    {
        *cap = (*cap == 0) ? 8 : (*cap * 2);
        *roots = (wchar_t **)Util_Realloc(*roots, (size_t)(*cap) * sizeof(wchar_t *));
    }
    (*roots)[(*count)++] = Path_NormalizeDirDup(path);
}

void Scan_FreeRoots(wchar_t **roots, int count)
{
    int i;
    if (roots == NULL)
        return;
    for (i = 0; i < count; i++)
        free(roots[i]);
    free(roots);
}

static int CompareRootStrings(const void *pa, const void *pb)
{
    const wchar_t *a = *(const wchar_t *const *)pa;
    const wchar_t *b = *(const wchar_t *const *)pb;
    return _wcsicmp(a, b);
}

static wchar_t **BuildValidatedRoots(wchar_t **roots, int count, int *out_count,
                                     wchar_t *err, size_t cch)
{
    wchar_t **copy;
    wchar_t **result;
    int i;
    int n = 0;

    *out_count = 0;
    if (err != NULL && cch > 0)
        err[0] = 0;
    if (roots == NULL || count <= 0)
    {
        if (err != NULL)
            StringCchPrintfW(err, cch, L"没有可扫描的目录。");
        return NULL;
    }

    copy = (wchar_t **)Util_Alloc((size_t)count * sizeof(wchar_t *));
    for (i = 0; i < count; i++)
    {
        wchar_t *norm;
        DWORD attrs;
        if (roots[i] == NULL || roots[i][0] == 0)
            continue;
        norm = Path_NormalizeDirDup(roots[i]);
        attrs = GetFileAttributesW(norm);
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            Path_IsSameOrUnderAppDir(norm))
        {
            free(norm);
            continue;
        }
        copy[n++] = norm;
    }

    if (n == 0)
    {
        free(copy);
        if (err != NULL)
            StringCchPrintfW(err, cch, L"扫描目录不存在或无权访问。");
        return NULL;
    }

    qsort(copy, (size_t)n, sizeof(wchar_t *), CompareRootStrings);
    result = (wchar_t **)Util_Alloc((size_t)n * sizeof(wchar_t *));
    *out_count = 0;
    for (i = 0; i < n; i++)
    {
        BOOL nested = FALSE;
        int j;
        for (j = 0; j < *out_count; j++)
        {
            if (Path_IsUnder(copy[i], result[j]))
            {
                nested = TRUE;
                break;
            }
        }
        if (nested)
            free(copy[i]);
        else
            result[(*out_count)++] = copy[i];
    }
    free(copy);

    if (*out_count == 0)
    {
        free(result);
        if (err != NULL)
            StringCchPrintfW(err, cch, L"没有可扫描的目录。");
        return NULL;
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* root collection                                                     */
/* ------------------------------------------------------------------ */

int Scan_CollectDriveRoots(wchar_t ***roots, int *count, wchar_t *err, size_t cch)
{
    wchar_t drives[1024];
    DWORD n;
    wchar_t *p;
    int cap = 0;

    *roots = NULL;
    *count = 0;
    if (err != NULL && cch > 0)
        err[0] = 0;

    n = GetLogicalDriveStringsW(ARRAYSIZE(drives) - 1, drives);
    if (n == 0 || n >= ARRAYSIZE(drives))
    {
        if (err != NULL)
            StringCchPrintfW(err, cch, L"无法枚举磁盘驱动器。");
        return -1;
    }
    drives[n] = 0;
    for (p = drives; *p != 0; p += wcslen(p) + 1)
    {
        UINT type = GetDriveTypeW(p);
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_RAMDISK)
            RootsPush(roots, count, &cap, p);
    }
    if (*count == 0)
    {
        if (err != NULL)
            StringCchPrintfW(err, cch, L"没有找到可扫描的本地磁盘。");
        return -1;
    }
    return 0;
}

static void AddKnownFolder(wchar_t ***roots, int *count, int *cap, REFKNOWNFOLDERID id)
{
    PWSTR p = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, NULL, &p)) && p != NULL)
    {
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES)
            RootsPush(roots, count, cap, p);
        CoTaskMemFree(p);
    }
}

static int CollectDefaultQuickRoots(wchar_t ***roots, int *count, wchar_t *err, size_t cch)
{
    wchar_t temp[MAX_PATH + 2];
    wchar_t profile[APP_PATH_CCH];
    wchar_t candidate[APP_PATH_CCH];
    int cap = 0;

    *roots = NULL;
    *count = 0;
    if (err != NULL && cch > 0)
        err[0] = 0;

    AddKnownFolder(roots, count, &cap, &FOLDERID_Desktop);
    AddKnownFolder(roots, count, &cap, &FOLDERID_Downloads);
    AddKnownFolder(roots, count, &cap, &FOLDERID_Documents);
    AddKnownFolder(roots, count, &cap, &FOLDERID_Pictures);
    AddKnownFolder(roots, count, &cap, &FOLDERID_Music);
    AddKnownFolder(roots, count, &cap, &FOLDERID_Videos);

    if (GetTempPathW(ARRAYSIZE(temp), temp) > 0)
        RootsPush(roots, count, &cap, temp);

    if (GetEnvironmentVariableW(L"USERPROFILE", profile, ARRAYSIZE(profile)) > 0)
    {
        StringCchPrintfW(candidate, ARRAYSIZE(candidate), L"%ls\\Downloads", profile);
        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES)
            RootsPush(roots, count, &cap, candidate);
    }

    if (*count == 0)
    {
        if (err != NULL)
            StringCchPrintfW(err, cch, L"无法确定默认的快速扫描目录。");
        return -1;
    }
    return 0;
}

int Scan_CollectQuickRoots(wchar_t ***roots, int *count, wchar_t *err, size_t cch)
{
    int i;
    int cap = 0;

    *roots = NULL;
    *count = 0;
    if (err != NULL && cch > 0)
        err[0] = 0;

    for (i = 0; i < g_quick_root_count; i++)
    {
        if (g_quick_roots[i] != NULL && GetFileAttributesW(g_quick_roots[i]) != INVALID_FILE_ATTRIBUTES)
            RootsPush(roots, count, &cap, g_quick_roots[i]);
    }
    if (*count == 0)
        return CollectDefaultQuickRoots(roots, count, err, cch);
    return 0;
}

/* ------------------------------------------------------------------ */
/* quick-root statistics                                               */
/* ------------------------------------------------------------------ */

static int CompareRecParent(const void *pa, const void *pb)
{
    int ia = *(const int *)pa;
    int ib = *(const int *)pb;
    FileRec *a = Data_Get(ia);
    FileRec *b = Data_Get(ib);
    size_t la;
    size_t lb;
    size_t n;
    int c;

    if (a == NULL || b == NULL)
        return ia - ib;
    la = Path_ParentLength(a->path);
    lb = Path_ParentLength(b->path);
    n = (la < lb) ? la : lb;
    c = _wcsnicmp(a->path, b->path, n);
    if (c != 0)
        return c;
    if (la < lb)
        return -1;
    if (la > lb)
        return 1;
    return 0;
}

typedef struct QuickCandidate
{
    wchar_t *dir;
    uint64_t count;
} QuickCandidate;

void Scan_ClearQuickRoots(void)
{
    int i;
    for (i = 0; i < g_quick_root_count; i++)
    {
        free(g_quick_roots[i]);
        g_quick_roots[i] = NULL;
    }
    g_quick_root_count = 0;
}

void Scan_ComputeQuickRoots(void)
{
    int *idx;
    int n;
    int i;
    int run_start;
    QuickCandidate top[QUICK_ROOT_MAX];
    int top_count = 0;
    wchar_t *selected[QUICK_ROOT_MAX];
    int selected_count = 0;

    Scan_ClearQuickRoots();
    n = (int)g_rec_count;
    if (n <= 0)
        return;

    idx = (int *)Util_Alloc((size_t)n * sizeof(int));
    for (i = 0; i < n; i++)
        idx[i] = i;
    qsort(idx, (size_t)n, sizeof(int), CompareRecParent);

    ZeroMemory(top, sizeof(top));
    run_start = 0;
    for (i = 1; i <= n; i++)
    {
        BOOL end = (i == n);
        if (!end && CompareRecParent(&idx[i], &idx[run_start]) == 0)
            continue;
        {
            int count = i - run_start;
            FileRec *rec = Data_Get(idx[run_start]);
            if (rec != NULL && (rec->flags & REC_FLAG_DELETED) == 0)
            {
                size_t plen = Path_ParentLength(rec->path);
                if (plen > 0)
                {
                    wchar_t *dir = (wchar_t *)Util_Alloc((plen + 1) * sizeof(wchar_t));
                    memcpy(dir, rec->path, plen * sizeof(wchar_t));
                    dir[plen] = 0;
                    if (Path_IsSameOrUnderAppDir(dir))
                    {
                        free(dir);
                    }
                    else if (top_count < QUICK_ROOT_MAX)
                    {
                        int pos = top_count;
                        while (pos > 0 && top[pos - 1].count < (uint64_t)count)
                        {
                            top[pos] = top[pos - 1];
                            pos--;
                        }
                        top[pos].dir = dir;
                        top[pos].count = (uint64_t)count;
                        top_count++;
                    }
                    else if ((uint64_t)count > top[QUICK_ROOT_MAX - 1].count)
                    {
                        int pos;
                        free(top[QUICK_ROOT_MAX - 1].dir);
                        pos = QUICK_ROOT_MAX - 1;
                        while (pos > 0 && top[pos - 1].count < (uint64_t)count)
                        {
                            top[pos] = top[pos - 1];
                            pos--;
                        }
                        top[pos].dir = dir;
                        top[pos].count = (uint64_t)count;
                    }
                    else
                    {
                        free(dir);
                    }
                }
            }
        }
        run_start = i;
    }
    free(idx);

    for (i = 0; i < top_count && selected_count < QUICK_ROOT_MAX; i++)
    {
        int j;
        BOOL overlap = FALSE;
        if (top[i].dir == NULL)
            continue;
        for (j = 0; j < selected_count; j++)
        {
            if (Path_IsUnder(top[i].dir, selected[j]) || Path_IsUnder(selected[j], top[i].dir))
            {
                overlap = TRUE;
                break;
            }
        }
        if (!overlap)
            selected[selected_count++] = top[i].dir;
        else
            free(top[i].dir);
        top[i].dir = NULL;
    }
    for (i = 0; i < top_count; i++)
        free(top[i].dir);

    for (i = 0; i < selected_count; i++)
        g_quick_roots[g_quick_root_count++] = selected[i];
}

/* ------------------------------------------------------------------ */
/* scan implementation                                                 */
/* ------------------------------------------------------------------ */

static void SetScanError(const wchar_t *fmt, ...)
{
    va_list ap;
    wchar_t *s;
    if (g_scan_error[0] != 0)
        return;
    va_start(ap, fmt);
    s = Str_FormatV(fmt, ap);
    va_end(ap);
    if (s != NULL)
    {
        StringCchCopyW(g_scan_error, ARRAYSIZE(g_scan_error), s);
        free(s);
    }
}

static void SetCurrentScanDir(const wchar_t *dir)
{
    AcquireSRWLockExclusive(&s_path_lock);
    StringCchCopyW(g_current_scan_path, ARRAYSIZE(g_current_scan_path), dir ? dir : L"");
    ReleaseSRWLockExclusive(&s_path_lock);
}

void Scan_GetCurrentPath(wchar_t *out, size_t cch)
{
    if (out == NULL || cch == 0)
        return;
    AcquireSRWLockShared(&s_path_lock);
    StringCchCopyW(out, cch, g_current_scan_path);
    ReleaseSRWLockShared(&s_path_lock);
}

static wchar_t *JoinPath(const wchar_t *dir, const wchar_t *name)
{
    size_t dl;
    size_t nl;
    BOOL sep;
    wchar_t *out;

    if (dir == NULL)
        dir = L"";
    if (name == NULL)
        name = L"";
    dl = wcslen(dir);
    nl = wcslen(name);
    sep = (dl > 0 && dir[dl - 1] != L'\\' && dir[dl - 1] != L'/');
    out = (wchar_t *)Util_Alloc((dl + (sep ? 1 : 0) + nl + 1) * sizeof(wchar_t));
    memcpy(out, dir, dl * sizeof(wchar_t));
    if (sep)
        out[dl++] = L'\\';
    memcpy(out + dl, name, (nl + 1) * sizeof(wchar_t));
    return out;
}

static BOOL IsSpecialSystemDir(const wchar_t *name)
{
    return _wcsicmp(name, L"$Recycle.Bin") == 0 ||
           _wcsicmp(name, L"System Volume Information") == 0;
}

static void ScanOneDirectory(const wchar_t *dir, int depth, int *fatal)
{
    wchar_t *pattern;
    wchar_t *api_pattern;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    BOOL more;

    if (*fatal || InterlockedCompareExchange(&g_stop_requested, 0, 0) != 0)
        return;
    if (depth > 512)
    {
        InterlockedIncrement64(&g_skipped_dirs);
        return;
    }
    if (Path_IsSameOrUnderAppDir(dir))
        return;

    SetCurrentScanDir(dir);
    pattern = JoinPath(dir, L"*");
    api_pattern = Path_ApiDup(pattern);
    h = FindFirstFileExW(api_pattern, FindExInfoBasic, &fd, FindExSearchNameMatch,
                         NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
    {
        h = FindFirstFileW(api_pattern, &fd);
    }
    free(api_pattern);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE)
    {
        InterlockedIncrement64(&g_skipped_dirs);
        return;
    }

    more = TRUE;
    while (more)
    {
        if (InterlockedCompareExchange(&g_stop_requested, 0, 0) != 0)
            break;
        if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0)
        {
            wchar_t *full = JoinPath(dir, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                    IsSpecialSystemDir(fd.cFileName) ||
                    Path_IsSameOrUnderAppDir(full))
                {
                    InterlockedIncrement64(&g_skipped_dirs);
                }
                else
                {
                    ScanOneDirectory(full, depth + 1, fatal);
                }
            }
            else
            {
                uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | (uint64_t)fd.nFileSizeLow;
                wchar_t ext[64];
                wchar_t status[32];
                uint32_t flags = 0;
                BOOL hidden;
                int i;

                InterlockedIncrement64(&g_total_scanned);
                Path_ExtensionLower(full, ext, ARRAYSIZE(ext));

                hidden = Path_IsSystemCritical(full, fd.dwFileAttributes) ||
                         (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                if (!hidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    for (i = 0; i < g_protected_dir_count; i++)
                    {
                        if (g_protected_dirs[i] != NULL && Path_IsUnder(full, g_protected_dirs[i]))
                        {
                            flags |= REC_FLAG_PROTECTED;
                            break;
                        }
                    }
                }

                if (hidden)
                {
                    InterlockedIncrement64(&g_hidden_count);
                    InterlockedAdd64(&g_hidden_bytes, (LONG64)size);
                    StringCchCopyW(status, ARRAYSIZE(status), L"系统保护");
                }
                else
                {
                    if (Path_ExtIsOffice(ext))
                        flags |= REC_FLAG_OFFICE;
                    if (Data_Add(full, size, &fd.ftLastWriteTime, flags) == NULL)
                    {
                        SetScanError(L"扫描记录数量超过上限（%d 条），扫描已停止。", REC_MAX_COUNT);
                        *fatal = 1;
                        free(full);
                        break;
                    }
                    if ((flags & REC_FLAG_PROTECTED) != 0)
                    {
                        InterlockedIncrement64(&g_protected_count);
                        InterlockedAdd64(&g_protected_bytes, (LONG64)size);
                        StringCchCopyW(status, ARRAYSIZE(status), L"保护区");
                    }
                    else
                    {
                        InterlockedIncrement64(&g_clean_count);
                        InterlockedAdd64(&g_clean_bytes, (LONG64)size);
                        StringCchCopyW(status, ARRAYSIZE(status), L"可清理");
                    }
                }

                {
                    wchar_t logerr[APP_ERR_CCH];
                    if (Store_RunLogRow(full, ext, size, &fd.ftLastWriteTime, status,
                                        logerr, ARRAYSIZE(logerr)) != 0)
                    {
                        SetScanError(L"%ls", logerr[0] ? logerr : L"写入实时扫描记录失败。");
                        *fatal = 1;
                        free(full);
                        break;
                    }
                }
            }
            free(full);
        }
        more = FindNextFileW(h, &fd);
    }
    FindClose(h);
}

static int Scan_Execute(ScanJob *job, wchar_t *err, size_t cch)
{
    int i;
    int fatal = 0;
    BOOL stopped = FALSE;
    BOOL log_open = FALSE;
    BOOL save_skipped = FALSE;
    int result = 0;
    wchar_t logerr[APP_ERR_CCH];

    if (err != NULL && cch > 0)
        err[0] = 0;
    g_scan_error[0] = 0;

    if (Store_RunLogOpen(logerr, ARRAYSIZE(logerr)) != 0)
    {
        SetScanError(L"%ls", logerr[0] ? logerr : L"无法创建实时扫描记录文件。");
        save_skipped = TRUE;
        result = -1;
    }
    else
    {
        log_open = TRUE;
    }

    if (result == 0)
    {
        SetCurrentScanDir(L"正在准备扫描...");
        for (i = 0; i < job->root_count; i++)
        {
            if (InterlockedCompareExchange(&g_stop_requested, 0, 0) != 0)
            {
                stopped = TRUE;
                break;
            }
            SetCurrentScanDir(job->roots[i]);
            ScanOneDirectory(job->roots[i], 0, &fatal);
            if (fatal)
            {
                result = -1;
                break;
            }
        }
        if (InterlockedCompareExchange(&g_stop_requested, 0, 0) != 0)
            stopped = TRUE;
    }

    Time_NowFileTime(&g_scan_end_ft);
    if (g_scan_start_tick != 0)
        g_scan_seconds = (GetTickCount64() - g_scan_start_tick) / 1000;
    g_scan_partial = (stopped || result != 0) ? TRUE : FALSE;

    if (!save_skipped)
    {
        Scan_ComputeQuickRoots();
        if (Store_Save(logerr, ARRAYSIZE(logerr)) != 0)
        {
            SetScanError(L"%ls", logerr[0] ? logerr : L"保存扫描数据失败。");
            g_data_dirty = TRUE;
            result = -1;
        }
    }

    if (log_open)
    {
        if (Store_RunLogClose(!stopped && result == 0, g_scan_seconds,
                              logerr, ARRAYSIZE(logerr)) != 0)
        {
            SetScanError(L"%ls", logerr[0] ? logerr : L"关闭实时扫描记录失败。");
            result = -1;
        }
    }

    if (result == 0 && stopped)
        result = 1;
    if (result != 0)
        g_scan_partial = TRUE;
    SetCurrentScanDir(L"扫描结束");

    if (err != NULL && cch > 0)
        StringCchCopyW(err, cch, g_scan_error);
    return result;
}

static DWORD WINAPI ScanThreadProc(LPVOID param)
{
    ScanJob *job = (ScanJob *)param;
    wchar_t err[APP_ERR_CCH];
    int rc;
    UINT msg_result;

    rc = Scan_Execute(job, err, ARRAYSIZE(err));
    Scan_FreeRoots(job->roots, job->root_count);
    free(job);
    InterlockedExchange(&g_scan_running, 0);
    msg_result = (rc == 0) ? 0u : (rc == 1 ? 1u : 2u);
    if (g_main_window != NULL && IsWindow(g_main_window))
        PostMessageW(g_main_window, WM_APP_SCAN_DONE, (WPARAM)msg_result, 0);
    return 0;
}

int Scan_Start(ScanMode mode, wchar_t **roots, int root_count, wchar_t *err, size_t cch)
{
    ScanJob *job;
    wchar_t **valid;
    int valid_count = 0;
    HANDLE thread;

    if (err != NULL && cch > 0)
        err[0] = 0;
    if (InterlockedCompareExchange(&g_scan_running, 0, 0) != 0)
    {
        if (err != NULL)
            StringCchPrintfW(err, cch, L"扫描已经正在进行中。");
        return -1;
    }

    valid = BuildValidatedRoots(roots, root_count, &valid_count, err, cch);
    if (valid == NULL)
        return -1;

    job = (ScanJob *)Util_Alloc(sizeof(ScanJob));
    job->mode = mode;
    job->roots = valid;
    job->root_count = valid_count;

    Data_ResetStats();
    Time_NowFileTime(&g_scan_start_ft);
    g_scan_start_tick = GetTickCount64();
    g_scan_partial = FALSE;
    g_data_dirty = FALSE;
    SetCurrentScanDir(L"正在准备扫描...");
    InterlockedExchange(&g_stop_requested, 0);
    InterlockedExchange(&g_scan_running, 1);

    thread = CreateThread(NULL, 0, ScanThreadProc, job, 0, NULL);
    if (thread == NULL)
    {
        InterlockedExchange(&g_scan_running, 0);
        Scan_FreeRoots(job->roots, job->root_count);
        free(job);
        if (err != NULL)
            StringCchPrintfW(err, cch, L"无法启动扫描线程（错误码 %lu）。",
                             (unsigned long)GetLastError());
        return -1;
    }
    g_scan_thread = thread;
    return 0;
}

int Scan_RunBlocking(ScanMode mode, wchar_t **roots, int root_count, wchar_t *err, size_t cch)
{
    ScanJob job;
    wchar_t **valid;
    int valid_count = 0;
    int rc;

    if (err != NULL && cch > 0)
        err[0] = 0;
    valid = BuildValidatedRoots(roots, root_count, &valid_count, err, cch);
    if (valid == NULL)
        return -1;

    Data_Reset();
    Time_NowFileTime(&g_scan_start_ft);
    g_scan_start_tick = GetTickCount64();
    InterlockedExchange(&g_stop_requested, 0);
    InterlockedExchange(&g_scan_running, 1);

    job.mode = mode;
    job.roots = valid;
    job.root_count = valid_count;
    rc = Scan_Execute(&job, err, cch);

    Scan_FreeRoots(valid, valid_count);
    InterlockedExchange(&g_scan_running, 0);
    return (rc == 0) ? 0 : (rc == 1 ? 1 : -1);
}

void Scan_RequestStop(void)
{
    InterlockedExchange(&g_stop_requested, 1);
}

BOOL Scan_IsRunning(void)
{
    return InterlockedCompareExchange(&g_scan_running, 0, 0) != 0;
}
