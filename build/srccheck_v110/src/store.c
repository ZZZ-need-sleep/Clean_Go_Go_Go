/*
 * store.c - in-memory record store, data.xzq persistence and run_find.xzq log.
 * SPDX-License-Identifier: MIT
 */
#include "app.h"
#include <string.h>
#include <io.h>

#define DATA_MAGIC   "XZQDATA1"
#define DATA_VERSION 2u

#pragma pack(push, 1)
typedef struct DataHeader
{
    char     magic[8];
    uint32_t version;
    uint32_t flags;
    uint32_t scan_mode;
    uint32_t quick_count;
    uint32_t prot_count;
    uint32_t record_count;
    uint64_t scan_start;
    uint64_t scan_end;
    uint64_t total_scanned;
    uint64_t hidden_count;
    uint64_t hidden_bytes;
    uint64_t skipped_dirs;
    uint64_t scan_seconds;
    uint64_t reserved;
} DataHeader;
#pragma pack(pop)

static FILE *s_runlog = NULL;
static uint64_t s_runlog_rows = 0;
static ULONGLONG s_runlog_last_flush = 0;

/* ------------------------------------------------------------------ */
/* record store                                                        */
/* ------------------------------------------------------------------ */

void Data_ResetStats(void)
{
    InterlockedExchange64(&g_total_scanned, 0);
    InterlockedExchange64(&g_hidden_count, 0);
    InterlockedExchange64(&g_hidden_bytes, 0);
    InterlockedExchange64(&g_clean_count, 0);
    InterlockedExchange64(&g_clean_bytes, 0);
    InterlockedExchange64(&g_protected_count, 0);
    InterlockedExchange64(&g_protected_bytes, 0);
    InterlockedExchange64(&g_ext_protected_count, 0);
    InterlockedExchange64(&g_ext_protected_bytes, 0);
    InterlockedExchange64(&g_skipped_dirs, 0);
    ZeroMemory(&g_scan_start_ft, sizeof(g_scan_start_ft));
    ZeroMemory(&g_scan_end_ft, sizeof(g_scan_end_ft));
    g_scan_seconds = 0;
    g_scan_start_tick = 0;
    g_scan_error[0] = 0;
    g_current_scan_path[0] = 0;
    g_scan_partial = FALSE;
    InterlockedExchange(&g_stop_requested, 0);
}

void Data_Reset(void)
{
    int i;
    for (i = 0; i < REC_MAX_CHUNKS; i++)
    {
        FileRec *chunk = g_rec_chunks[i];
        int j;
        if (chunk == NULL)
            continue;
        for (j = 0; j < REC_CHUNK_SIZE; j++)
        {
            if (chunk[j].path != NULL)
                free(chunk[j].path);
        }
        free(chunk);
        g_rec_chunks[i] = NULL;
    }
    InterlockedExchange(&g_rec_count, 0);
    Data_ResetStats();
}

FileRec *Data_Add(const wchar_t *path, uint64_t size, const FILETIME *mtime, uint32_t flags)
{
    int index;
    int chunk_index;
    int offset;
    FileRec *chunk;
    FileRec *rec;
    wchar_t *copy;
    const wchar_t *name;

    if (path == NULL || path[0] == 0)
        return NULL;
    index = (int)InterlockedCompareExchange(&g_rec_count, 0, 0);
    if (index >= REC_MAX_COUNT)
        return NULL;
    chunk_index = index / REC_CHUNK_SIZE;
    offset = index % REC_CHUNK_SIZE;
    if (chunk_index >= REC_MAX_CHUNKS)
        return NULL;

    chunk = g_rec_chunks[chunk_index];
    if (chunk == NULL)
    {
        chunk = (FileRec *)Util_Alloc(sizeof(FileRec) * REC_CHUNK_SIZE);
        ZeroMemory(chunk, sizeof(FileRec) * REC_CHUNK_SIZE);
        g_rec_chunks[chunk_index] = chunk;
    }

    copy = Util_WcsDup(path);
    name = Path_FileName(copy);
    rec = &chunk[offset];
    rec->path = copy;
    rec->name = (wchar_t *)name;
    rec->size = size;
    if (mtime != NULL)
        rec->mtime = *mtime;
    else
        ZeroMemory(&rec->mtime, sizeof(rec->mtime));
    rec->flags = flags;

    MemoryBarrier();
    InterlockedIncrement(&g_rec_count);
    return rec;
}

FileRec *Data_Get(int index)
{
    int chunk_index;
    int offset;
    int count = (int)InterlockedCompareExchange(&g_rec_count, 0, 0);
    if (index < 0 || index >= count)
        return NULL;
    chunk_index = index / REC_CHUNK_SIZE;
    offset = index % REC_CHUNK_SIZE;
    if (chunk_index >= REC_MAX_CHUNKS || g_rec_chunks[chunk_index] == NULL)
        return NULL;
    return &g_rec_chunks[chunk_index][offset];
}

int Data_Count(void)
{
    return (int)InterlockedCompareExchange(&g_rec_count, 0, 0);
}

void Data_RefreshCounters(void)
{
    uint64_t clean_count = 0;
    uint64_t clean_bytes = 0;
    uint64_t prot_count = 0;
    uint64_t prot_bytes = 0;
    uint64_t ext_prot_count = 0;
    uint64_t ext_prot_bytes = 0;
    int i;
    int n = (int)InterlockedCompareExchange(&g_rec_count, 0, 0);

    for (i = 0; i < n; i++)
    {
        FileRec *r = Data_Get(i);
        if (r == NULL || (r->flags & REC_FLAG_DELETED) != 0)
            continue;
        if ((r->flags & REC_FLAG_PROTECTED) != 0)
        {
            prot_count++;
            prot_bytes += r->size;
        }
        else if ((r->flags & REC_FLAG_EXT_PROTECTED) != 0)
        {
            ext_prot_count++;
            ext_prot_bytes += r->size;
        }
        else
        {
            clean_count++;
            clean_bytes += r->size;
        }
    }
    InterlockedExchange64(&g_clean_count, (LONG64)clean_count);
    InterlockedExchange64(&g_clean_bytes, (LONG64)clean_bytes);
    InterlockedExchange64(&g_protected_count, (LONG64)prot_count);
    InterlockedExchange64(&g_protected_bytes, (LONG64)prot_bytes);
    InterlockedExchange64(&g_ext_protected_count, (LONG64)ext_prot_count);
    InterlockedExchange64(&g_ext_protected_bytes, (LONG64)ext_prot_bytes);
}

static BOOL PathIsProtectedByUser(const wchar_t *path)
{
    int i;
    for (i = 0; i < g_protected_dir_count; i++)
    {
        if (g_protected_dirs[i] != NULL && Path_IsUnder(path, g_protected_dirs[i]))
            return TRUE;
    }
    return FALSE;
}

static BOOL PathIsProtectedByExt(const wchar_t *path)
{
    wchar_t ext[PROTECTED_EXT_CCH];
    Path_ExtensionLower(path, ext, ARRAYSIZE(ext));
    return Path_ExtIsUserProtected(ext);
}

void Data_ApplyProtection(void)
{
    int i;
    int n = (int)InterlockedCompareExchange(&g_rec_count, 0, 0);
    for (i = 0; i < n; i++)
    {
        FileRec *r = Data_Get(i);
        if (r == NULL)
            continue;
        if (PathIsProtectedByUser(r->path))
            r->flags |= REC_FLAG_PROTECTED;
        else
            r->flags &= ~REC_FLAG_PROTECTED;
        if (PathIsProtectedByExt(r->path))
            r->flags |= REC_FLAG_EXT_PROTECTED;
        else
            r->flags &= ~REC_FLAG_EXT_PROTECTED;
    }
    Data_RefreshCounters();
}

void Data_GetStats(ScanStats *out)
{
    if (out == NULL)
        return;
    out->total_scanned = (uint64_t)InterlockedCompareExchange64(&g_total_scanned, 0, 0);
    out->hidden_count = (uint64_t)InterlockedCompareExchange64(&g_hidden_count, 0, 0);
    out->hidden_bytes = (uint64_t)InterlockedCompareExchange64(&g_hidden_bytes, 0, 0);
    out->clean_count = (uint64_t)InterlockedCompareExchange64(&g_clean_count, 0, 0);
    out->clean_bytes = (uint64_t)InterlockedCompareExchange64(&g_clean_bytes, 0, 0);
    out->protected_count = (uint64_t)InterlockedCompareExchange64(&g_protected_count, 0, 0);
    out->protected_bytes = (uint64_t)InterlockedCompareExchange64(&g_protected_bytes, 0, 0);
    out->ext_protected_count = (uint64_t)InterlockedCompareExchange64(&g_ext_protected_count, 0, 0);
    out->ext_protected_bytes = (uint64_t)InterlockedCompareExchange64(&g_ext_protected_bytes, 0, 0);
    out->skipped_dirs = (uint64_t)InterlockedCompareExchange64(&g_skipped_dirs, 0, 0);
    out->record_count = (uint32_t)InterlockedCompareExchange(&g_rec_count, 0, 0);
}

/* ------------------------------------------------------------------ */
/* persistence                                                         */
/* ------------------------------------------------------------------ */

static uint64_t FileTimeToU64(const FILETIME *ft)
{
    ULARGE_INTEGER u;
    if (ft == NULL)
        return 0;
    u.LowPart = ft->dwLowDateTime;
    u.HighPart = ft->dwHighDateTime;
    return (uint64_t)u.QuadPart;
}

static void U64ToFileTime(uint64_t value, FILETIME *ft)
{
    ULARGE_INTEGER u;
    u.QuadPart = (ULONGLONG)value;
    if (ft != NULL)
    {
        ft->dwLowDateTime = u.LowPart;
        ft->dwHighDateTime = u.HighPart;
    }
}

static BOOL WriteBytes(FILE *fp, const void *data, size_t size)
{
    return fwrite(data, 1, size, fp) == size;
}

static BOOL ReadBytes(FILE *fp, void *data, size_t size)
{
    return fread(data, 1, size, fp) == size;
}

static BOOL WriteString(FILE *fp, const wchar_t *s, char *utf8, int cap)
{
    uint32_t len;
    int need;
    if (s == NULL)
        s = L"";
    need = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, utf8, cap, NULL, NULL);
    if (need <= 0)
        return FALSE;
    len = (uint32_t)(need - 1);
    if (!WriteBytes(fp, &len, sizeof(len)))
        return FALSE;
    return WriteBytes(fp, utf8, len);
}

static wchar_t *ReadString(FILE *fp, char *utf8, int cap)
{
    uint32_t len;
    int need;
    if (!ReadBytes(fp, &len, sizeof(len)))
        return NULL;
    if (len > (uint32_t)(cap - 1))
        return NULL;
    if (len > 0 && !ReadBytes(fp, utf8, len))
        return NULL;
    utf8[len] = 0;
    need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, (int)len, NULL, 0);
    if (need <= 0 && len != 0)
        return NULL;
    {
        wchar_t *w = (wchar_t *)Util_Alloc(((size_t)need + 1) * sizeof(wchar_t));
        if (need > 0)
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, (int)len, w, need);
        w[need] = 0;
        return w;
    }
}

void Store_ClearQuickRoots(void)
{
    int i;
    for (i = 0; i < g_quick_root_count; i++)
    {
        free(g_quick_roots[i]);
        g_quick_roots[i] = NULL;
    }
    g_quick_root_count = 0;
}

static void StoreClearProtectedDirs(void)
{
    int i;
    for (i = 0; i < g_protected_dir_count; i++)
    {
        free(g_protected_dirs[i]);
        g_protected_dirs[i] = NULL;
    }
    g_protected_dir_count = 0;
}

void Store_ClearProtectedExts(void)
{
    int i;
    for (i = 0; i < g_protected_ext_count; i++)
    {
        free(g_protected_exts[i]);
        g_protected_exts[i] = NULL;
    }
    g_protected_ext_count = 0;
}

void Store_SetDefaultProtectedExts(void)
{
    static const wchar_t *defaults[] = {
        L"bat", L"cmd", L"ps1", L"vbs", L"js",
        L"json", L"log", L"bin", L"ini", L"inf",
        L"cfg", L"conf", L"config", L"xml", L"yaml", L"yml", L"toml",
        L"dat", L"db", L"sqlite", L"mdb", L"reg", L"manifest",
        L"properties", L"env", L"lock", L"pid"
    };
    size_t i;
    Store_ClearProtectedExts();
    for (i = 0; i < ARRAYSIZE(defaults) && g_protected_ext_count < PROTECTED_EXT_MAX; i++)
        g_protected_exts[g_protected_ext_count++] = Util_WcsDup(defaults[i]);
}

BOOL Store_DataExists(void)
{
    DWORD attrs = GetFileAttributesW(g_data_path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

int Store_CreateEmpty(wchar_t *err, size_t cch)
{
    Data_Reset();
    Store_ClearQuickRoots();
    StoreClearProtectedDirs();
    Store_SetDefaultProtectedExts();
    return Store_Save(err, cch);
}

int Store_Load(wchar_t *err, size_t cch)
{
    DataHeader hdr;
    FILE *fp;
    wchar_t *api;
    char *utf8;
    int i;
    int rc = 0;
    uint32_t records;
    uint32_t ext_count = 0;
    uint32_t file_version;
    int utf8cap = APP_EXE_PATH_CCH * 4;

    if (err != NULL && cch > 0)
        err[0] = 0;
    if (!Store_DataExists())
        return 1;

    api = Path_ApiDup(g_data_path);
    fp = _wfopen(api, L"rb");
    free(api);
    if (fp == NULL)
    {
        if (err != NULL)
            StringCchPrintfW(err, cch, L"无法打开数据文件：%ls", g_data_path);
        return -1;
    }

    file_version = 0;
    if (!ReadBytes(fp, &hdr, sizeof(hdr)) ||
        memcmp(hdr.magic, DATA_MAGIC, 8) != 0 ||
        (hdr.version != 1u && hdr.version != 2u) ||
        hdr.quick_count > QUICK_ROOT_MAX ||
        hdr.prot_count > PROTECTED_DIR_MAX ||
        hdr.record_count > REC_MAX_COUNT)
    {
        fclose(fp);
        if (err != NULL)
            StringCchPrintfW(err, cch, L"扫描数据文件损坏或版本不兼容：%ls", g_data_path);
        return -1;
    }
    file_version = hdr.version;
    if (file_version >= 2u)
    {
        if (hdr.reserved > PROTECTED_EXT_MAX)
        {
            fclose(fp);
            if (err != NULL)
                StringCchPrintfW(err, cch, L"扫描数据文件损坏或版本不兼容：%ls", g_data_path);
            return -1;
        }
        ext_count = (uint32_t)hdr.reserved;
    }

    Data_Reset();
    Store_ClearQuickRoots();
    StoreClearProtectedDirs();
    Store_ClearProtectedExts();

    utf8 = (char *)Util_Alloc((size_t)utf8cap);
    for (i = 0; i < (int)hdr.quick_count; i++)
    {
        wchar_t *s = ReadString(fp, utf8, utf8cap);
        if (s == NULL)
        {
            rc = -1;
            break;
        }
        g_quick_roots[g_quick_root_count++] = s;
    }
    if (rc == 0)
    {
        for (i = 0; i < (int)hdr.prot_count; i++)
        {
            wchar_t *s = ReadString(fp, utf8, utf8cap);
            if (s == NULL)
            {
                rc = -1;
                break;
            }
            g_protected_dirs[g_protected_dir_count++] = s;
        }
    }
    if (rc == 0 && file_version >= 2u)
    {
        for (i = 0; i < (int)ext_count; i++)
        {
            wchar_t *s = ReadString(fp, utf8, utf8cap);
            if (s == NULL || s[0] == 0)
            {
                free(s);
                rc = -1;
                break;
            }
            Str_ToLower(s);
            g_protected_exts[g_protected_ext_count++] = s;
        }
    }
    else if (rc == 0)
    {
        /* v1 data has no suffix-protection section: seed the documented defaults. */
        Store_SetDefaultProtectedExts();
    }

    records = 0;
    if (rc == 0)
    {
        for (i = 0; i < (int)hdr.record_count; i++)
        {
            uint32_t path_len;
            uint32_t flags;
            uint64_t size;
            uint64_t mtime;
            wchar_t *path;
            int need;
            if (!ReadBytes(fp, &flags, sizeof(flags)) ||
                !ReadBytes(fp, &size, sizeof(size)) ||
                !ReadBytes(fp, &mtime, sizeof(mtime)) ||
                !ReadBytes(fp, &path_len, sizeof(path_len)) ||
                path_len > (uint32_t)(utf8cap - 1) ||
                (path_len > 0 && !ReadBytes(fp, utf8, path_len)))
            {
                rc = -1;
                break;
            }
            utf8[path_len] = 0;
            need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, (int)path_len, NULL, 0);
            if (need <= 0)
            {
                rc = -1;
                break;
            }
            path = (wchar_t *)Util_Alloc(((size_t)need + 1) * sizeof(wchar_t));
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, (int)path_len, path, need);
            path[need] = 0;
            if (path[0] == 0 || Path_IsSameOrUnderAppDir(path))
            {
                free(path);
                rc = -1;
                break;
            }
            {
                FILETIME ft;
                U64ToFileTime(mtime, &ft);
                if (Data_Add(path, size, &ft, flags) == NULL)
                {
                    free(path);
                    rc = -1;
                    break;
                }
                records++;
            }
        }
    }

    free(utf8);
    fclose(fp);

    if (rc != 0 || records != hdr.record_count)
    {
        Data_Reset();
        if (err != NULL)
            StringCchPrintfW(err, cch, L"扫描数据文件损坏或不完整：%ls", g_data_path);
        return -1;
    }

    U64ToFileTime(hdr.scan_start, &g_scan_start_ft);
    U64ToFileTime(hdr.scan_end, &g_scan_end_ft);
    InterlockedExchange64(&g_total_scanned, (LONG64)hdr.total_scanned);
    InterlockedExchange64(&g_hidden_count, (LONG64)hdr.hidden_count);
    InterlockedExchange64(&g_hidden_bytes, (LONG64)hdr.hidden_bytes);
    InterlockedExchange64(&g_skipped_dirs, (LONG64)hdr.skipped_dirs);
    g_scan_seconds = hdr.scan_seconds;
    g_scan_partial = (hdr.flags & DATA_FLAG_PARTIAL) ? TRUE : FALSE;

    Data_ApplyProtection();
    g_data_dirty = FALSE;
    return 0;
}

int Store_Save(wchar_t *err, size_t cch)
{
    DataHeader hdr;
    FILE *fp;
    wchar_t *tmp_path;
    wchar_t *api_tmp;
    wchar_t *api_data;
    char *utf8;
    int utf8cap = APP_EXE_PATH_CCH * 4;
    int i;
    BOOL ok = TRUE;
    uint32_t record_count = 0;
    DWORD last_error = 0;
    int n;

    if (err != NULL && cch > 0)
        err[0] = 0;

    tmp_path = Str_Format(L"%ls.tmp", g_data_path);
    api_tmp = Path_ApiDup(tmp_path);
    api_data = Path_ApiDup(g_data_path);
    fp = _wfopen(api_tmp, L"wb");
    if (fp == NULL)
    {
        if (err != NULL)
            StringCchPrintfW(err, cch, L"无法写入数据文件：%ls", g_data_path);
        free(api_tmp);
        free(api_data);
        free(tmp_path);
        return -1;
    }

    n = (int)InterlockedCompareExchange(&g_rec_count, 0, 0);
    for (i = 0; i < n; i++)
    {
        FileRec *r = Data_Get(i);
        if (r != NULL && (r->flags & REC_FLAG_DELETED) == 0)
            record_count++;
    }

    ZeroMemory(&hdr, sizeof(hdr));
    memcpy(hdr.magic, DATA_MAGIC, 8);
    hdr.version = DATA_VERSION;
    hdr.flags = g_scan_partial ? DATA_FLAG_PARTIAL : 0;
    hdr.scan_mode = 0;
    hdr.quick_count = (uint32_t)g_quick_root_count;
    hdr.prot_count = (uint32_t)g_protected_dir_count;
    hdr.record_count = record_count;
    hdr.reserved = (uint64_t)g_protected_ext_count;
    hdr.scan_start = FileTimeToU64(&g_scan_start_ft);
    hdr.scan_end = FileTimeToU64(&g_scan_end_ft);
    hdr.total_scanned = (uint64_t)g_total_scanned;
    hdr.hidden_count = (uint64_t)g_hidden_count;
    hdr.hidden_bytes = (uint64_t)g_hidden_bytes;
    hdr.skipped_dirs = (uint64_t)g_skipped_dirs;
    hdr.scan_seconds = g_scan_seconds;

    utf8 = (char *)Util_Alloc((size_t)utf8cap);
    if (!WriteBytes(fp, &hdr, sizeof(hdr)))
        ok = FALSE;
    for (i = 0; ok && i < g_quick_root_count; i++)
        ok = WriteString(fp, g_quick_roots[i], utf8, utf8cap);
    for (i = 0; ok && i < g_protected_dir_count; i++)
        ok = WriteString(fp, g_protected_dirs[i], utf8, utf8cap);
    for (i = 0; ok && i < g_protected_ext_count; i++)
        ok = WriteString(fp, g_protected_exts[i], utf8, utf8cap);

    for (i = 0; ok && i < n; i++)
    {
        FileRec *r = Data_Get(i);
        uint32_t path_len;
        uint64_t mtime_u;
        int need;
        if (r == NULL || (r->flags & REC_FLAG_DELETED) != 0)
            continue;
        need = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, r->path, -1, utf8, utf8cap, NULL, NULL);
        if (need <= 0)
        {
            ok = FALSE;
            break;
        }
        path_len = (uint32_t)(need - 1);
        mtime_u = FileTimeToU64(&r->mtime);
        if (!WriteBytes(fp, &r->flags, sizeof(r->flags)) ||
            !WriteBytes(fp, &r->size, sizeof(r->size)) ||
            !WriteBytes(fp, &mtime_u, sizeof(mtime_u)) ||
            !WriteBytes(fp, &path_len, sizeof(path_len)) ||
            !WriteBytes(fp, utf8, path_len))
        {
            ok = FALSE;
        }
    }

    free(utf8);
    if (fflush(fp) != 0)
        ok = FALSE;
    if (fclose(fp) != 0)
        ok = FALSE;

    if (ok)
    {
        if (!MoveFileExW(api_tmp, api_data, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            last_error = GetLastError();
            ok = FALSE;
        }
    }
    if (!ok)
    {
        DeleteFileW(api_tmp);
        if (err != NULL)
        {
            if (last_error != 0)
                StringCchPrintfW(err, cch, L"保存扫描数据失败（错误码 %lu）：%ls",
                                 (unsigned long)last_error, g_data_path);
            else
                StringCchPrintfW(err, cch, L"保存扫描数据失败：%ls", g_data_path);
        }
    }
    else
    {
        g_data_dirty = FALSE;
    }

    free(api_tmp);
    free(api_data);
    free(tmp_path);
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* real-time scan log                                                  */
/* ------------------------------------------------------------------ */

int Store_RunLogOpen(wchar_t *err, size_t cch)
{
    static char path_utf8[APP_EXE_PATH_CCH * 4];
    static char time_utf8[128];
    wchar_t now[64];
    wchar_t *api;
    int need;

    if (err != NULL && cch > 0)
        err[0] = 0;
    if (s_runlog != NULL)
    {
        fclose(s_runlog);
        s_runlog = NULL;
    }
    s_runlog_rows = 0;
    s_runlog_last_flush = GetTickCount64();

    api = Path_ApiDup(g_runlog_path);
    s_runlog = _wfopen(api, L"wb");
    free(api);
    if (s_runlog == NULL)
    {
        if (err != NULL)
            StringCchPrintfW(err, cch, L"无法创建实时扫描记录文件：%ls", g_runlog_path);
        return -1;
    }
    setvbuf(s_runlog, NULL, _IOFBF, 1024 * 1024);

    fwrite("\xEF\xBB\xBF", 1, 3, s_runlog);
    Time_NowString(now, ARRAYSIZE(now));
    need = WideCharToMultiByte(CP_UTF8, 0, now, -1, time_utf8, sizeof(time_utf8), NULL, NULL);
    fprintf(s_runlog, "# 文件清理 - 实时扫描记录\r\n");
    fprintf(s_runlog, "# 开始时间: %s\r\n", need > 0 ? time_utf8 : "");
    fprintf(s_runlog, "# 字段: 完整路径\t后缀\t大小(字节)\t修改时间\t状态\r\n");
    fflush(s_runlog);
    return 0;
}

int Store_RunLogRow(const wchar_t *path, const wchar_t *ext, uint64_t size,
                    const FILETIME *mtime, const wchar_t *status,
                    wchar_t *err, size_t cch)
{
    static char path_utf8[APP_EXE_PATH_CCH * 4];
    static char ext_utf8[128];
    static char status_utf8[128];
    static char time_utf8[128];
    wchar_t time_str[64];
    int need;
    int written;

    if (s_runlog == NULL)
        return -1;
    if (err != NULL && cch > 0)
        err[0] = 0;

    need = WideCharToMultiByte(CP_UTF8, 0, path ? path : L"", -1, path_utf8, sizeof(path_utf8), NULL, NULL);
    if (need <= 0)
        return -1;
    need = WideCharToMultiByte(CP_UTF8, 0, ext ? ext : L"", -1, ext_utf8, sizeof(ext_utf8), NULL, NULL);
    if (need <= 0)
        ext_utf8[0] = 0;
    need = WideCharToMultiByte(CP_UTF8, 0, status ? status : L"", -1, status_utf8, sizeof(status_utf8), NULL, NULL);
    if (need <= 0)
        status_utf8[0] = 0;
    if (mtime != NULL)
        Time_FileTimeToString(mtime, time_str, ARRAYSIZE(time_str));
    else
        StringCchCopyW(time_str, ARRAYSIZE(time_str), L"—");
    need = WideCharToMultiByte(CP_UTF8, 0, time_str, -1, time_utf8, sizeof(time_utf8), NULL, NULL);
    if (need <= 0)
        time_utf8[0] = 0;

    written = fprintf(s_runlog, "%s\t%s\t%llu\t%s\t%s\r\n",
                      path_utf8,
                      ext_utf8[0] ? ext_utf8 : "-",
                      (unsigned long long)size,
                      time_utf8,
                      status_utf8);
    if (written < 0)
    {
        if (err != NULL)
            StringCchPrintfW(err, cch, L"写入实时扫描记录失败：%ls", g_runlog_path);
        return -1;
    }
    s_runlog_rows++;
    if ((s_runlog_rows & 0x7F) == 0 ||
        GetTickCount64() - s_runlog_last_flush >= 1000)
    {
        if (fflush(s_runlog) != 0)
        {
            if (err != NULL)
                StringCchPrintfW(err, cch, L"刷新实时扫描记录失败：%ls", g_runlog_path);
            return -1;
        }
        s_runlog_last_flush = GetTickCount64();
    }
    return 0;
}

int Store_RunLogFlush(void)
{
    if (s_runlog == NULL)
        return 0;
    if (fflush(s_runlog) != 0)
        return -1;
    s_runlog_last_flush = GetTickCount64();
    return 0;
}

int Store_RunLogClose(BOOL completed, uint64_t elapsed, wchar_t *err, size_t cch)
{
    static char time_utf8[128];
    wchar_t now[64];
    int need;

    if (err != NULL && cch > 0)
        err[0] = 0;
    if (s_runlog == NULL)
        return 0;

    Time_NowString(now, ARRAYSIZE(now));
    need = WideCharToMultiByte(CP_UTF8, 0, now, -1, time_utf8, sizeof(time_utf8), NULL, NULL);
    fprintf(s_runlog, "# 结束时间: %s\r\n", need > 0 ? time_utf8 : "");
    fprintf(s_runlog, "# 扫描状态: %s\r\n", completed ? "完成" : "用户中止");
    fprintf(s_runlog, "# 耗时(秒): %llu\r\n", (unsigned long long)elapsed);
    fprintf(s_runlog, "# 记录行数: %llu\r\n", (unsigned long long)s_runlog_rows);
    fprintf(s_runlog, "# 总计扫描文件: %llu\r\n", (unsigned long long)g_total_scanned);
    fprintf(s_runlog, "# 可清理: %llu\r\n", (unsigned long long)g_clean_count);
    fprintf(s_runlog, "# 保护区: %llu\r\n", (unsigned long long)g_protected_count);
    fprintf(s_runlog, "# 后缀保护: %llu\r\n", (unsigned long long)g_ext_protected_count);
    fprintf(s_runlog, "# 系统保护未显示: %llu\r\n", (unsigned long long)g_hidden_count);
    fprintf(s_runlog, "# 跳过目录: %llu\r\n", (unsigned long long)g_skipped_dirs);

    if (fflush(s_runlog) != 0)
    {
        if (err != NULL)
            StringCchPrintfW(err, cch, L"刷新实时扫描记录失败：%ls", g_runlog_path);
        fclose(s_runlog);
        s_runlog = NULL;
        return -1;
    }
    if (fclose(s_runlog) != 0)
    {
        s_runlog = NULL;
        if (err != NULL)
            StringCchPrintfW(err, cch, L"关闭实时扫描记录失败：%ls", g_runlog_path);
        return -1;
    }
    s_runlog = NULL;
    return 0;
}
