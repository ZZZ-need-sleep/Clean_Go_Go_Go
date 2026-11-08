/*
 * list.c - virtualized owner-drawn file list with 2-line names and delete action.
 * SPDX-License-Identifier: MIT
 */
#include "app.h"
#include <string.h>
#include <wchar.h>

#define LIST_CLASS_NAME L"XZQFileList"

#define C_LIST_BG        RGB(255, 255, 255)
#define C_HEADER_BG      RGB(248, 250, 252)
#define C_BORDER         RGB(226, 232, 240)
#define C_SEPARATOR      RGB(241, 245, 249)
#define C_TEXT           RGB(15, 23, 42)
#define C_MUTED          RGB(100, 116, 139)
#define C_LIGHT          RGB(148, 163, 184)
#define C_BLUE           RGB(37, 99, 235)
#define C_RED            RGB(220, 38, 38)
#define C_ORANGE         RGB(194, 65, 12)
#define C_ORANGE_BG      RGB(255, 247, 237)
#define C_ORANGE_BORDER  RGB(254, 215, 170)
#define C_GREEN          RGB(4, 120, 87)
#define C_GREEN_BG       RGB(236, 253, 245)
#define C_GREEN_BORDER   RGB(167, 243, 208)
#define C_PURPLE         RGB(109, 40, 217)
#define C_PURPLE_BG      RGB(245, 243, 255)
#define C_PURPLE_BORDER  RGB(221, 214, 254)
#define C_SELECT         RGB(239, 246, 255)
#define C_HOVER          RGB(248, 250, 252)

typedef struct Columns
{
    int name_x;
    int name_w;
    int size_x;
    int size_w;
    int date_x;
    int date_w;
    int status_x;
    int status_w;
    int action_x;
    int action_w;
} Columns;

static HWND s_list = NULL;
static int *s_view = NULL;
static int s_view_count = 0;
static int s_view_cap = 0;
static int s_view_built = 0;
static NavFilter s_filter = NAV_ALL;
static wchar_t s_search[256] = {0};
static int s_top_row = 0;
static int s_hover_row = -1;
static int s_selected_row = -1;
static BOOL s_follow = TRUE;
static BOOL s_tracking_leave = FALSE;
static int s_dpi = 96;
static HFONT s_font_header = NULL;
static HFONT s_font_name = NULL;
static HFONT s_font_path = NULL;
static HFONT s_font_body = NULL;
static HFONT s_font_action = NULL;

static int DS(int value)
{
    return MulDiv(value, s_dpi, 96);
}

static int HeaderHeight(void)
{
    return DS(42);
}

static int RowHeight(void)
{
    return DS(58);
}

static void DestroyFonts(void)
{
    if (s_font_header) { DeleteObject(s_font_header); s_font_header = NULL; }
    if (s_font_name) { DeleteObject(s_font_name); s_font_name = NULL; }
    if (s_font_path) { DeleteObject(s_font_path); s_font_path = NULL; }
    if (s_font_body) { DeleteObject(s_font_body); s_font_body = NULL; }
    if (s_font_action) { DeleteObject(s_font_action); s_font_action = NULL; }
}

static HFONT MakeFont(int pt100, int weight)
{
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(pt100, s_dpi, 7200);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    StringCchCopyW(lf.lfFaceName, ARRAYSIZE(lf.lfFaceName), L"Microsoft YaHei UI");
    return CreateFontIndirectW(&lf);
}

static void CreateFonts(void)
{
    DestroyFonts();
    s_font_header = MakeFont(850, FW_SEMIBOLD);
    s_font_name = MakeFont(950, FW_SEMIBOLD);
    s_font_path = MakeFont(800, FW_NORMAL);
    s_font_body = MakeFont(850, FW_NORMAL);
    s_font_action = MakeFont(850, FW_SEMIBOLD);
}

/* ------------------------------------------------------------------ */
/* view/filter                                                         */
/* ------------------------------------------------------------------ */

static BOOL RecordMatches(const FileRec *r)
{
    if (r == NULL || (r->flags & REC_FLAG_DELETED) != 0)
        return FALSE;
    if (s_filter == NAV_OFFICE && (r->flags & REC_FLAG_OFFICE) == 0)
        return FALSE;
    if (s_filter == NAV_PROTECTED && (r->flags & REC_FLAG_PROTECTED) == 0)
        return FALSE;
    if (s_filter == NAV_EXT_PROTECTED && (r->flags & REC_FLAG_EXT_PROTECTED) == 0)
        return FALSE;
    if (s_search[0] != 0 && StrStrIW(r->path, s_search) == NULL)
        return FALSE;
    return TRUE;
}

static void EnsureViewCapacity(int count)
{
    if (count <= s_view_cap)
        return;
    if (s_view_cap == 0)
        s_view_cap = 256;
    while (s_view_cap < count)
        s_view_cap *= 2;
    s_view = (int *)Util_Realloc(s_view, (size_t)s_view_cap * sizeof(int));
}

static int CompareViewItems(const void *pa, const void *pb)
{
    int ia = *(const int *)pa;
    int ib = *(const int *)pb;
    FileRec *a = Data_Get(ia);
    FileRec *b = Data_Get(ib);
    int c;

    if (a == NULL || b == NULL)
        return ia - ib;
    c = CompareFileTime(&a->mtime, &b->mtime);
    if (c != 0)
        return (c > 0) ? -1 : 1; /* newest first */
    if (a->size != b->size)
        return (a->size > b->size) ? -1 : 1;
    return _wcsicmp(a->path, b->path);
}

static int VisibleRows(void)
{
    RECT rc;
    int h;
    if (s_list == NULL)
        return 1;
    GetClientRect(s_list, &rc);
    h = rc.bottom - HeaderHeight();
    if (h < RowHeight())
        return 1;
    return h / RowHeight();
}

static int MaxTopRow(void)
{
    int max = s_view_count - VisibleRows();
    return (max > 0) ? max : 0;
}

static void ClampTop(void)
{
    int max = MaxTopRow();
    if (s_top_row > max)
        s_top_row = max;
    if (s_top_row < 0)
        s_top_row = 0;
}

static void UpdateScrollBar(void)
{
    SCROLLINFO si;
    if (s_list == NULL)
        return;
    ClampTop();
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (s_view_count > 0) ? s_view_count - 1 : 0;
    si.nPage = (UINT)VisibleRows();
    si.nPos = s_top_row;
    SetScrollInfo(s_list, SB_VERT, &si, TRUE);
}

static void EnsureSelectedVisible(void)
{
    int page = VisibleRows();
    if (s_selected_row < 0)
        return;
    if (s_selected_row < s_top_row)
        s_top_row = s_selected_row;
    else if (s_selected_row >= s_top_row + page)
        s_top_row = s_selected_row - page + 1;
    ClampTop();
}

void List_Rebuild(BOOL sort)
{
    int i;
    int n = Data_Count();
    int old_top = s_top_row;

    EnsureViewCapacity(n > 0 ? n : 1);
    s_view_count = 0;
    for (i = 0; i < n; i++)
    {
        FileRec *r = Data_Get(i);
        if (RecordMatches(r))
            s_view[s_view_count++] = i;
    }
    if (sort && s_view_count > 1)
        qsort(s_view, (size_t)s_view_count, sizeof(int), CompareViewItems);
    s_view_built = Data_Count();

    s_top_row = old_top;
    ClampTop();
    if (s_follow)
        s_top_row = MaxTopRow();
    if (s_selected_row >= s_view_count)
        s_selected_row = s_view_count - 1;
    if (s_hover_row >= s_view_count)
        s_hover_row = -1;
    UpdateScrollBar();
    if (s_list != NULL)
        InvalidateRect(s_list, NULL, FALSE);
}

void List_AppendNew(void)
{
    int n = Data_Count();
    int old_count = s_view_count;
    int i;
    if (s_view_built >= n)
        return;
    EnsureViewCapacity(s_view_count + (n - s_view_built));
    for (i = s_view_built; i < n; i++)
    {
        FileRec *r = Data_Get(i);
        if (RecordMatches(r))
            s_view[s_view_count++] = i;
    }
    s_view_built = n;
    if (s_view_count != old_count)
    {
        if (s_follow)
            s_top_row = MaxTopRow();
        UpdateScrollBar();
        InvalidateRect(s_list, NULL, FALSE);
    }
}

void List_Reset(void)
{
    s_view_count = 0;
    s_view_built = 0;
    s_top_row = 0;
    s_hover_row = -1;
    s_selected_row = -1;
    s_follow = TRUE;
    UpdateScrollBar();
    if (s_list != NULL)
        InvalidateRect(s_list, NULL, FALSE);
}

void List_SetFilter(NavFilter filter, const wchar_t *search)
{
    BOOL changed = (filter != s_filter);
    if (search == NULL)
        search = L"";
    if (wcscmp(search, s_search) != 0)
        changed = TRUE;
    s_filter = filter;
    StringCchCopyW(s_search, ARRAYSIZE(s_search), search);
    if (changed)
    {
        s_top_row = 0;
        s_follow = FALSE;
        s_selected_row = -1;
        s_hover_row = -1;
        List_Rebuild(!Scan_IsRunning());
        UpdateScrollBar();
    }
}

void List_SetSearch(const wchar_t *search)
{
    if (search == NULL)
        search = L"";
    if (wcscmp(search, s_search) == 0)
        return;
    StringCchCopyW(s_search, ARRAYSIZE(s_search), search);
    s_top_row = 0;
    s_follow = FALSE;
    s_selected_row = -1;
    s_hover_row = -1;
    List_Rebuild(!Scan_IsRunning());
    UpdateScrollBar();
}

NavFilter List_GetFilter(void)
{
    return s_filter;
}

void List_DataChanged(void)
{
    UpdateScrollBar();
    if (s_list != NULL)
        InvalidateRect(s_list, NULL, FALSE);
}

void List_ScrollToTop(void)
{
    s_top_row = 0;
    s_follow = FALSE;
    UpdateScrollBar();
    if (s_list != NULL)
        InvalidateRect(s_list, NULL, FALSE);
}

void List_OnDpiChanged(void)
{
    s_dpi = g_dpi;
    CreateFonts();
    ClampTop();
    UpdateScrollBar();
    if (s_list != NULL)
        InvalidateRect(s_list, NULL, FALSE);
}

int List_GetViewCount(void)
{
    return s_view_count;
}

/* ------------------------------------------------------------------ */
/* painting helpers                                                    */
/* ------------------------------------------------------------------ */

static void FillSolid(HDC dc, const RECT *rc, COLORREF color)
{
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, rc, b);
    DeleteObject(b);
}

static void StrokeRect(HDC dc, const RECT *rc, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc->left, rc->top, rc->right, rc->bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void RoundedFill(HDC dc, const RECT *rc, int radius, COLORREF fill, COLORREF border)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldB = SelectObject(dc, b);
    HGDIOBJ oldP = SelectObject(dc, pen);
    RoundRect(dc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(b);
    DeleteObject(pen);
}

static void DrawTextIn(HDC dc, const wchar_t *text, RECT rc, HFONT font,
                       COLORREF color, UINT flags)
{
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rc, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

static void DrawTextCentered(HDC dc, const wchar_t *text, RECT rc, HFONT font,
                             COLORREF color)
{
    DrawTextIn(dc, text, rc, font, color,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void GetColumns(int width, Columns *c)
{
    int action = DS(104);
    int status = DS(124);
    int date = DS(150);
    int size = DS(96);
    int pad = DS(16);
    if (width < DS(760))
    {
        action = DS(96);
        status = DS(102);
        date = DS(132);
        size = DS(82);
        pad = DS(12);
    }
    c->name_x = pad;
    c->name_w = width - action - status - date - size - pad;
    if (c->name_w < DS(220))
        c->name_w = DS(220);
    c->size_x = c->name_x + c->name_w;
    c->size_w = size;
    c->date_x = c->size_x + size;
    c->date_w = date;
    c->status_x = c->date_x + date;
    c->status_w = status;
    c->action_x = c->status_x + status;
    c->action_w = width - c->action_x;
}

static void DrawEmptyState(HDC dc, const RECT *client)
{
    int cx = (client->right - client->left) / 2;
    int cy = (client->top + client->bottom) / 2 - DS(12);
    int color = RGB(203, 213, 225);
    HPEN pen = CreatePen(PS_SOLID, DS(3), color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int r = DS(28);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    MoveToEx(dc, cx + r - DS(8), cy + r - DS(8), NULL);
    LineTo(dc, cx + r + DS(18), cy + r + DS(18));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    if (Scan_IsRunning())
    {
        RECT r1 = {client->left, cy + DS(52), client->right, cy + DS(82)};
        RECT r2 = {client->left + DS(40), cy + DS(84), client->right - DS(40), cy + DS(116)};
        wchar_t count[128];
        ScanStats stats;
        Data_GetStats(&stats);
        StringCchPrintfW(count, ARRAYSIZE(count), L"正在扫描，已发现 %llu 个文件", (unsigned long long)stats.clean_count + stats.protected_count);
        DrawTextIn(dc, L"正在扫描，请稍候…", r1, s_font_name, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextIn(dc, count, r2, s_font_body, C_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    if (s_search[0] != 0)
    {
        RECT r1 = {client->left, cy + DS(52), client->right, cy + DS(82)};
        RECT r2 = {client->left + DS(40), cy + DS(84), client->right - DS(40), cy + DS(116)};
        DrawTextIn(dc, L"没有匹配的文件", r1, s_font_name, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextIn(dc, L"试试其他关键词，或切换左侧分类。", r2, s_font_body, C_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    if (Data_Count() > 0)
    {
        RECT r1 = {client->left, cy + DS(52), client->right, cy + DS(82)};
        RECT r2 = {client->left + DS(30), cy + DS(84), client->right - DS(30), cy + DS(120)};
        const wchar_t *title;
        const wchar_t *sub;
        if (s_filter == NAV_PROTECTED)
        {
            title = L"保护区里还没有文件";
            sub = L"文件仍会被扫描，可在「保护区管理」中添加目录。";
        }
        else if (s_filter == NAV_EXT_PROTECTED)
        {
            title = L"后缀保护中没有文件";
            sub = L"带有受保护后缀的文件仍会被扫描和显示；可点击左下角「后缀保护管理」调整。";
        }
        else
        {
            title = L"这个分类下暂无文件";
            sub = L"切换左侧「全部文件」可以查看全部扫描结果。";
        }
        DrawTextIn(dc, title, r1, s_font_name, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextIn(dc, sub, r2, s_font_body, C_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    {
        ScanStats stats;
        Data_GetStats(&stats);
        if (stats.total_scanned > 0)
        {
            RECT r1 = {client->left, cy + DS(52), client->right, cy + DS(82)};
            RECT r2 = {client->left + DS(30), cy + DS(84), client->right - DS(30), cy + DS(122)};
            wchar_t sub[256];
            StringCchPrintfW(sub, ARRAYSIZE(sub),
                             L"扫描到 %llu 个系统保护文件，已隐藏（仅统计）。",
                             (unsigned long long)stats.hidden_count);
            DrawTextIn(dc, L"没有可显示的普通文件", r1, s_font_name, C_TEXT,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextIn(dc, sub, r2, s_font_body, C_MUTED,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }
    }

    {
        RECT r1 = {client->left, cy + DS(52), client->right, cy + DS(82)};
        RECT r2 = {client->left + DS(40), cy + DS(84), client->right - DS(40), cy + DS(120)};
        DrawTextIn(dc, L"还没有扫描数据", r1, s_font_name, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextIn(dc, L"点击上方「快速扫描」「全盘扫描」或「选择目录」开始查找文件。",
                   r2, s_font_body, C_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static void PaintList(HDC dc, const RECT *client)
{
    Columns cols;
    int width = client->right - client->left;
    int visible;
    int i;
    int header_h = HeaderHeight();
    int row_h = RowHeight();
    RECT header = {0, 0, width, header_h};

    FillSolid(dc, client, C_LIST_BG);
    FillSolid(dc, &header, C_HEADER_BG);
    {
        RECT line = {0, header_h - 1, width, header_h};
        FillSolid(dc, &line, C_BORDER);
    }
    GetColumns(width, &cols);

    {
        RECT r = {cols.name_x, 0, cols.name_x + cols.name_w, header_h};
        DrawTextIn(dc, L"文件名称 / 路径", r, s_font_header, C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    {
        RECT r = {cols.size_x + DS(8), 0, cols.size_x + cols.size_w, header_h};
        DrawTextIn(dc, L"大小", r, s_font_header, C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    {
        RECT r = {cols.date_x + DS(8), 0, cols.date_x + cols.date_w, header_h};
        DrawTextIn(dc, L"最后修改", r, s_font_header, C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    {
        RECT r = {cols.status_x + DS(8), 0, cols.status_x + cols.status_w, header_h};
        DrawTextIn(dc, L"状态", r, s_font_header, C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    {
        RECT r = {cols.action_x, 0, cols.action_x + cols.action_w, header_h};
        DrawTextIn(dc, L"操作", r, s_font_header, C_MUTED,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (s_view_count <= 0)
    {
        DrawEmptyState(dc, client);
        return;
    }

    visible = (client->bottom - header_h + row_h - 1) / row_h;
    if (visible < 1)
        visible = 1;
    if (s_top_row > s_view_count - 1)
        s_top_row = (s_view_count > 0) ? s_view_count - 1 : 0;

    for (i = 0; i < visible; i++)
    {
        int view_row = s_top_row + i;
        int y;
        RECT row;
        RECT r;
        FileRec *rec;
        wchar_t size_text[64];
        wchar_t date_text[64];
        BOOL protected_file;
        BOOL ext_protected_file;

        if (view_row >= s_view_count)
            break;
        rec = Data_Get(s_view[view_row]);
        if (rec == NULL)
            continue;
        y = header_h + i * row_h;
        SetRect(&row, 0, y, width, y + row_h);
        if (view_row == s_selected_row)
            FillSolid(dc, &row, C_SELECT);
        else if (view_row == s_hover_row)
            FillSolid(dc, &row, C_HOVER);
        else
            FillSolid(dc, &row, C_LIST_BG);

        protected_file = (rec->flags & REC_FLAG_PROTECTED) != 0;
        ext_protected_file = (rec->flags & REC_FLAG_EXT_PROTECTED) != 0;
        if (protected_file || ext_protected_file)
        {
            RECT bar = {0, y, DS(3), y + row_h};
            FillSolid(dc, &bar, protected_file ? C_ORANGE : C_PURPLE);
        }

        r.left = cols.name_x;
        r.right = cols.name_x + cols.name_w;
        r.top = y + DS(7);
        r.bottom = y + DS(29);
        DrawTextIn(dc, rec->name, r, s_font_name, C_TEXT,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        r.top = y + DS(29);
        r.bottom = y + DS(49);
        DrawTextIn(dc, rec->path, r, s_font_path, C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);

        Size_Format(rec->size, size_text, ARRAYSIZE(size_text));
        r.left = cols.size_x + DS(8);
        r.right = cols.size_x + cols.size_w;
        r.top = y;
        r.bottom = y + row_h;
        DrawTextIn(dc, size_text, r, s_font_body, C_TEXT,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        Time_FileTimeToString(&rec->mtime, date_text, ARRAYSIZE(date_text));
        r.left = cols.date_x + DS(8);
        r.right = cols.date_x + cols.date_w;
        DrawTextIn(dc, date_text, r, s_font_body, C_MUTED,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        {
            RECT chip;
            int chip_w = protected_file ? DS(66) : (ext_protected_file ? DS(82) : DS(66));
            int chip_h = DS(24);
            chip.left = cols.status_x + DS(8);
            chip.top = y + (row_h - chip_h) / 2;
            chip.right = chip.left + chip_w;
            chip.bottom = chip.top + chip_h;
            if (protected_file)
            {
                RoundedFill(dc, &chip, DS(8), C_ORANGE_BG, C_ORANGE_BORDER);
                DrawTextCentered(dc, L"保护区", chip, s_font_body, C_ORANGE);
            }
            else if (ext_protected_file)
            {
                RoundedFill(dc, &chip, DS(8), C_PURPLE_BG, C_PURPLE_BORDER);
                DrawTextCentered(dc, L"后缀保护", chip, s_font_body, C_PURPLE);
            }
            else
            {
                RoundedFill(dc, &chip, DS(8), C_GREEN_BG, C_GREEN_BORDER);
                DrawTextCentered(dc, L"可清理", chip, s_font_body, C_GREEN);
            }
        }

        {
            RECT action = {cols.action_x, y, cols.action_x + cols.action_w, y + row_h};
            if (Scan_IsRunning())
                DrawTextCentered(dc, L"扫描中", action, s_font_action, C_LIGHT);
            else if (protected_file || ext_protected_file)
                DrawTextCentered(dc, L"手动删除", action, s_font_action,
                                 protected_file ? C_ORANGE : C_PURPLE);
            else
                DrawTextCentered(dc, L"删除", action, s_font_action, C_RED);
        }

        {
            RECT sep = {DS(16), y + row_h - 1, width - DS(16), y + row_h};
            FillSolid(dc, &sep, C_SEPARATOR);
        }
    }
}

/* ------------------------------------------------------------------ */
/* hit testing / interaction                                           */
/* ------------------------------------------------------------------ */

static int HitTestRow(int y)
{
    int header_h = HeaderHeight();
    int row_h = RowHeight();
    int row;
    if (y < header_h)
        return -1;
    row = (y - header_h) / row_h + s_top_row;
    if (row < 0 || row >= s_view_count)
        return -1;
    return row;
}

static BOOL HitTestAction(int x)
{
    RECT rc;
    Columns cols;
    GetClientRect(s_list, &rc);
    GetColumns(rc.right - rc.left, &cols);
    return x >= cols.action_x;
}

static void SelectRow(int row)
{
    if (row < 0 || row >= s_view_count)
        return;
    s_selected_row = row;
    s_follow = FALSE;
    EnsureSelectedVisible();
    UpdateScrollBar();
    InvalidateRect(s_list, NULL, FALSE);
}

static void OpenRow(int row)
{
    if (row < 0 || row >= s_view_count)
        return;
    UI_OpenRecordLocation(s_view[row]);
}

static LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        s_list = hwnd;
        s_dpi = g_dpi;
        CreateFonts();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        HDC mem;
        HBITMAP bmp;
        HGDIOBJ old;
        GetClientRect(hwnd, &rc);
        mem = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, rc.right - rc.left, rc.bottom - rc.top);
        old = SelectObject(mem, bmp);
        PaintList(mem, &rc);
        BitBlt(dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        {
            RECT border = {0, 0, rc.right, rc.bottom};
            StrokeRect(dc, &border, C_BORDER);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        UpdateScrollBar();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_VSCROLL:
    {
        SCROLLINFO si;
        int code = LOWORD(wp);
        int page = VisibleRows();
        int new_top = s_top_row;
        ZeroMemory(&si, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS;
        GetScrollInfo(hwnd, SB_VERT, &si);
        switch (code)
        {
        case SB_LINEUP: new_top -= 1; break;
        case SB_LINEDOWN: new_top += 1; break;
        case SB_PAGEUP: new_top -= page - 1; break;
        case SB_PAGEDOWN: new_top += page - 1; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: new_top = si.nTrackPos; break;
        case SB_TOP: new_top = 0; break;
        case SB_BOTTOM: new_top = s_view_count; break;
        default: break;
        }
        s_top_row = new_top;
        s_follow = FALSE;
        ClampTop();
        if (s_follow == FALSE && s_top_row >= MaxTopRow())
            s_follow = TRUE;
        UpdateScrollBar();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        s_top_row -= (delta / WHEEL_DELTA) * 3;
        s_follow = FALSE;
        ClampTop();
        UpdateScrollBar();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int row = HitTestRow(GET_Y_LPARAM(lp));
        if (!s_tracking_leave)
        {
            TRACKMOUSEEVENT tme;
            ZeroMemory(&tme, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            s_tracking_leave = TRUE;
        }
        if (row != s_hover_row)
        {
            s_hover_row = row;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        s_tracking_leave = FALSE;
        s_hover_row = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT)
        {
            POINT pt;
            int row;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            row = HitTestRow(pt.y);
            if (row >= 0 && HitTestAction(pt.x))
            {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN:
    {
        int y = GET_Y_LPARAM(lp);
        int x = GET_X_LPARAM(lp);
        int row = HitTestRow(y);
        SetFocus(hwnd);
        if (row >= 0)
        {
            if (HitTestAction(x))
            {
                UI_RequestDeleteRecord(s_view[row]);
            }
            else
            {
                SelectRow(row);
            }
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        int row = HitTestRow(GET_Y_LPARAM(lp));
        if (row >= 0)
            OpenRow(row);
        return 0;
    }

    case WM_KEYDOWN:
    {
        int page = VisibleRows();
        int row = s_selected_row;
        switch (wp)
        {
        case VK_DOWN:
            if (row < 0) row = s_top_row;
            else row++;
            SelectRow(row);
            return 0;
        case VK_UP:
            if (row < 0) row = s_top_row;
            else row--;
            SelectRow(row);
            return 0;
        case VK_HOME:
            SelectRow(0);
            return 0;
        case VK_END:
            SelectRow(s_view_count - 1);
            return 0;
        case VK_PRIOR:
            SelectRow(row - page);
            return 0;
        case VK_NEXT:
            SelectRow(row + page);
            return 0;
        case VK_RETURN:
            OpenRow(row);
            return 0;
        case VK_DELETE:
            if (row >= 0)
                UI_RequestDeleteRecord(s_view[row]);
            return 0;
        default:
            break;
        }
        break;
    }

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_DESTROY:
        DestroyFonts();
        s_list = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL List_RegisterClass(HINSTANCE hinst)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = ListProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = LIST_CLASS_NAME;
    return RegisterClassExW(&wc) != 0;
}

HWND List_Create(HWND parent)
{
    return CreateWindowExW(0, LIST_CLASS_NAME, L"",
                           WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS,
                           0, 0, 10, 10, parent, NULL, g_hinst, NULL);
}

void List_Layout(int x, int y, int w, int h)
{
    if (s_list != NULL)
        MoveWindow(s_list, x, y, w, h, TRUE);
}
