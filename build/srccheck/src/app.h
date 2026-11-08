/*
 * 文件清理 (File Cleaner)
 * A compact native Win32 file-scanning / cleanup utility.
 *
 * Copyright (c) 2026 File Cleaner contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef XZQ_APP_H
#define XZQ_APP_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <time.h>
#include <strsafe.h>

#ifdef __cplusplus
#error "This project is written in C. Compile it as C, not C++."
#endif

#define APP_NAME              L"文件清理"
#define APP_NAME_EN           L"File Cleaner"
#define APP_VERSION           L"1.0.0"
#define APP_DEVELOPER_EMAIL   L"3814859587@qq.com"
#define APP_EXE_ALLOWED_1     L"LXOZVQE"
#define APP_EXE_ALLOWED_2     L"YuanShen"
#define APP_EXE_ALLOWED_3     L"Genshin"

#define APP_PATH_CCH          1024
#define APP_EXE_PATH_CCH      32768
#define APP_ERR_CCH           2048

#define DATA_FILENAME         L"data.xzq"
#define RUNLOG_FILENAME       L"run_find.xzq"
#define ERRORLOG_FILENAME     L"error.log"
#define SELFTEST_FILENAME     L"selftest.log"
#define SCANTEST_FILENAME     L"scantest.log"

#define REC_CHUNK_SIZE        4096
#define REC_MAX_COUNT         2000000
#define REC_MAX_CHUNKS        ((REC_MAX_COUNT + REC_CHUNK_SIZE - 1) / REC_CHUNK_SIZE)
#define QUICK_ROOT_MAX        16
#define PROTECTED_DIR_MAX     64

#define WM_APP_SCAN_DONE      (WM_APP + 1)
#define WM_APP_SCAN_TICK      (WM_APP + 2)

/* Record flags stored in memory (and in data.xzq for future compatibility). */
#define REC_FLAG_OFFICE       0x00000001u
#define REC_FLAG_PROTECTED    0x00000002u
#define REC_FLAG_DELETED      0x00000004u

/* data.xzq header flags. */
#define DATA_FLAG_PARTIAL     0x00000001u

typedef struct FileRec
{
    wchar_t *path;       /* full, normal (unprefixed) path */
    wchar_t *name;       /* points inside path */
    uint64_t size;
    FILETIME mtime;
    uint32_t flags;
    uint32_t reserved;
} FileRec;

typedef struct ScanStats
{
    uint64_t total_scanned;
    uint64_t hidden_count;
    uint64_t hidden_bytes;
    uint64_t clean_count;
    uint64_t clean_bytes;
    uint64_t protected_count;
    uint64_t protected_bytes;
    uint64_t skipped_dirs;
    uint32_t record_count;
} ScanStats;

typedef enum ScanMode
{
    SCAN_MODE_FULL = 0,
    SCAN_MODE_QUICK = 1,
    SCAN_MODE_DIRECTORY = 2
} ScanMode;

typedef enum NavFilter
{
    NAV_ALL = 0,
    NAV_OFFICE = 1,
    NAV_PROTECTED = 2
} NavFilter;

/* ----------------------------- globals ----------------------------- */

extern HINSTANCE g_hinst;
extern HWND g_main_window;
extern int g_dpi;
extern int g_dpi_scale_100;

extern wchar_t g_app_dir[APP_PATH_CCH];
extern wchar_t g_data_path[APP_PATH_CCH];
extern wchar_t g_runlog_path[APP_PATH_CCH];
extern wchar_t g_errorlog_path[APP_PATH_CCH];
extern wchar_t g_exe_path[APP_EXE_PATH_CCH];
extern wchar_t g_exe_dir[APP_EXE_PATH_CCH];

extern wchar_t g_windows_dir[APP_PATH_CCH];
extern wchar_t g_program_files[APP_PATH_CCH];
extern wchar_t g_program_files_x86[APP_PATH_CCH];
extern wchar_t g_programdata_ms[APP_PATH_CCH];
extern wchar_t g_recycle_hint[APP_PATH_CCH];
extern wchar_t g_svi_hint[APP_PATH_CCH];

extern FileRec *g_rec_chunks[REC_MAX_CHUNKS];
extern volatile LONG g_rec_count;

extern volatile LONG64 g_total_scanned;
extern volatile LONG64 g_hidden_count;
extern volatile LONG64 g_hidden_bytes;
extern volatile LONG64 g_clean_count;
extern volatile LONG64 g_clean_bytes;
extern volatile LONG64 g_protected_count;
extern volatile LONG64 g_protected_bytes;
extern volatile LONG64 g_skipped_dirs;
extern FILETIME g_scan_start_ft;
extern FILETIME g_scan_end_ft;
extern uint64_t g_scan_seconds;
extern ULONGLONG g_scan_start_tick;
extern volatile LONG g_scan_running;
extern volatile LONG g_stop_requested;
extern HANDLE g_scan_thread;
extern wchar_t g_scan_error[APP_ERR_CCH];
extern wchar_t g_current_scan_path[512];
extern BOOL g_scan_partial;
extern BOOL g_data_dirty;

extern wchar_t *g_quick_roots[QUICK_ROOT_MAX];
extern int g_quick_root_count;
extern wchar_t *g_protected_dirs[PROTECTED_DIR_MAX];
extern int g_protected_dir_count;

/* ----------------------------- util.c ------------------------------ */

void  Util_InitPaths(const wchar_t *override_data_dir);
BOOL  Util_EnsureDirectory(const wchar_t *dir);
void  Util_InitSystemPaths(void);
void  Util_LogErrorf(const wchar_t *fmt, ...);
void  Util_ErrorShowf(const wchar_t *fmt, ...);
void  Util_ErrorShow(const wchar_t *message);
void  Util_Fatalf(const wchar_t *fmt, ...);
void  Util_Info(const wchar_t *message, const wchar_t *title);

void *Util_Alloc(size_t bytes);
void *Util_Realloc(void *ptr, size_t bytes);
wchar_t *Util_WcsDup(const wchar_t *s);
char *Util_Utf8Dup(const wchar_t *s);

wchar_t *Str_Format(const wchar_t *fmt, ...);
wchar_t *Str_FormatV(const wchar_t *fmt, va_list ap);
void Str_ToLower(wchar_t *s);
BOOL Str_EqualsNoCase(const wchar_t *a, const wchar_t *b);

const wchar_t *Path_FileName(const wchar_t *path);
size_t Path_ParentLength(const wchar_t *path);
wchar_t *Path_ParentDup(const wchar_t *path);
wchar_t *Path_NormalizeDirDup(const wchar_t *dir);
void Path_TrimTrailingSlash(wchar_t *path);
BOOL Path_IsUnder(const wchar_t *path, const wchar_t *dir);
const wchar_t *Path_Extension(const wchar_t *path);
void Path_ExtensionLower(const wchar_t *path, wchar_t *out, size_t cch);
BOOL Path_ExtIsOffice(const wchar_t *ext);
BOOL Path_ExtIsSystemCritical(const wchar_t *ext);
BOOL Path_IsSystemCritical(const wchar_t *path, DWORD attrs);
BOOL Path_IsSameOrUnderAppDir(const wchar_t *path);
wchar_t *Path_ApiDup(const wchar_t *path);   /* optional \\?\ prefix */

void Size_Format(uint64_t bytes, wchar_t *out, size_t cch);
void Time_NowFileTime(FILETIME *ft);
void Time_FileTimeToLocal(const FILETIME *ft, SYSTEMTIME *st);
void Time_FileTimeToString(const FILETIME *ft, wchar_t *out, size_t cch);
void Time_SystemToString(const SYSTEMTIME *st, wchar_t *out, size_t cch);
SYSTEMTIME Time_GetCleanupCutoff(void);
void Time_CutoffFileTime(const SYSTEMTIME *cutoff, FILETIME *ft);
BOOL Time_FileTimeOlderThan(const FILETIME *ft, const FILETIME *cutoff);
void Time_NowString(wchar_t *out, size_t cch);

BOOL Guard_IsAllowedExeName(const wchar_t *path);
void Guard_Enforce(void);

/* ----------------------------- store.c ----------------------------- */

void   Data_Reset(void);
void   Data_ResetStats(void);
FileRec *Data_Add(const wchar_t *path, uint64_t size, const FILETIME *mtime, uint32_t flags);
FileRec *Data_Get(int index);
int    Data_Count(void);
void   Data_RefreshCounters(void);
void   Data_ApplyProtection(void);
void   Data_GetStats(ScanStats *out);

BOOL   Store_DataExists(void);
int    Store_CreateEmpty(wchar_t *err, size_t cch);
int    Store_Load(wchar_t *err, size_t cch);
int    Store_Save(wchar_t *err, size_t cch);

int    Store_RunLogOpen(wchar_t *err, size_t cch);
int    Store_RunLogRow(const wchar_t *path, const wchar_t *ext, uint64_t size,
                       const FILETIME *mtime, const wchar_t *status,
                       wchar_t *err, size_t cch);
int    Store_RunLogFlush(void);
int    Store_RunLogClose(BOOL completed, uint64_t elapsed, wchar_t *err, size_t cch);

/* ----------------------------- scan.c ------------------------------ */

void Scan_RequestStop(void);
BOOL Scan_IsRunning(void);
int  Scan_Start(ScanMode mode, wchar_t **roots, int root_count, wchar_t *err, size_t cch);
int  Scan_RunBlocking(ScanMode mode, wchar_t **roots, int root_count, wchar_t *err, size_t cch);
int  Scan_CollectDriveRoots(wchar_t ***roots, int *count, wchar_t *err, size_t cch);
int  Scan_CollectQuickRoots(wchar_t ***roots, int *count, wchar_t *err, size_t cch);
void Scan_FreeRoots(wchar_t **roots, int count);
void Scan_ComputeQuickRoots(void);
void Scan_ClearQuickRoots(void);
void Scan_GetCurrentPath(wchar_t *out, size_t cch);

/* ------------------------------ ui.c ------------------------------- */

BOOL UI_CreateMainWindow(HINSTANCE hinst);
void UI_StartScan(ScanMode mode, const wchar_t *directory);
int  UI_AskScanMode(HWND owner);              /* returns ScanMode or -1 */
wchar_t *UI_PickFolder(HWND owner, const wchar_t *title);
void UI_ShowProtectDialog(HWND owner);
void UI_ClearListAndRecords(void);
void UI_RefreshAll(void);
BOOL UI_IsUsingPreviousData(void);
void UI_SetUsingPreviousData(BOOL value);
void UI_RequestDeleteRecord(int rec_index);
void UI_OpenRecordLocation(int rec_index);

/* ------------------------------ list.c ----------------------------- */

BOOL List_RegisterClass(HINSTANCE hinst);
HWND List_Create(HWND parent);
void List_Layout(int x, int y, int w, int h);
void List_Reset(void);
void List_SetFilter(NavFilter filter, const wchar_t *search);
NavFilter List_GetFilter(void);
void List_SetSearch(const wchar_t *search);
void List_Rebuild(BOOL sort);
void List_AppendNew(void);
void List_DataChanged(void);
void List_ScrollToTop(void);
void List_OnDpiChanged(void);
int  List_GetViewCount(void);

/* ------------------------------ main.c ----------------------------- */

int  SelfTest_Run(void);
int  ScanTest_Run(const wchar_t *directory);

#endif /* XZQ_APP_H */
