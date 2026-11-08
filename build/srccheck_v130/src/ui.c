/*
 * ui.c - Win32 main window, navigation, dialogs, deletion and cleanup actions.
 * SPDX-License-Identifier: MIT
 */
#include "app.h"
#include "resource.h"
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <shellapi.h>

#define ID_BTN_QUICK      2001
#define ID_BTN_FULL       2002
#define ID_BTN_DIR        2003
#define ID_BTN_STOP       2004
#define ID_BTN_BATCH      2005
#define ID_EDIT_SEARCH    2006
#define TIMER_UI          1
#define TIMER_SEARCH      2

#define C_BG              RGB(241, 245, 249)
#define C_WHITE           RGB(255, 255, 255)
#define C_BORDER          RGB(226, 232, 240)
#define C_TEXT            RGB(15, 23, 42)
#define C_MUTED           RGB(100, 116, 139)
#define C_LIGHT           RGB(148, 163, 184)
#define C_BLUE            RGB(37, 99, 235)
#define C_BLUE_DARK       RGB(29, 78, 216)
#define C_BLUE_PRESSED    RGB(30, 64, 175)
#define C_SIDEBAR         RGB(15, 23, 42)
#define C_SIDEBAR_HOVER   RGB(30, 41, 59)
#define C_SIDEBAR_LINE    RGB(51, 65, 85)
#define C_SIDEBAR_TEXT    RGB(203, 213, 225)
#define C_RED             RGB(220, 38, 38)
#define C_RED_DARK        RGB(185, 28, 28)
#define C_RED_SOFT        RGB(254, 242, 242)
#define C_RED_SOFT_HOVER  RGB(254, 226, 226)
#define C_RED_BORDER      RGB(252, 165, 165)
#define C_GREEN           RGB(4, 120, 87)
#define C_ORANGE          RGB(194, 65, 12)
#define C_ORANGE_SOFT     RGB(255, 247, 237)
#define C_ORANGE_BORDER   RGB(254, 215, 170)
#define C_PURPLE          RGB(109, 40, 217)
#define C_PURPLE_SOFT     RGB(245, 243, 255)
#define C_PURPLE_BORDER   RGB(221, 214, 254)

static HWND s_btn_quick = NULL;
static HWND s_btn_full = NULL;
static HWND s_btn_dir = NULL;
static HWND s_btn_stop = NULL;
static HWND s_btn_batch = NULL;
static HWND s_search = NULL;
static HWND s_list = NULL;

static HFONT s_font_title = NULL;
static HFONT s_font_page = NULL;
static HFONT s_font_subtitle = NULL;
static HFONT s_font_card_label = NULL;
static HFONT s_font_card_value = NULL;
static HFONT s_font_card_sub = NULL;
static HFONT s_font_nav = NULL;
static HFONT s_font_nav_value = NULL;
static HFONT s_font_body = NULL;
static HFONT s_font_button = NULL;
static HFONT s_font_small = NULL;

static HBRUSH s_brush_white = NULL;
static HBRUSH s_brush_bg = NULL;
static HICON s_icon_small = NULL;
static HICON s_icon_sidebar = NULL;

static RECT s_nav_rects[4];
static RECT s_protect_rect;
static RECT s_extdel_rect;
static RECT s_support_rect;
static RECT s_search_rect;
static int s_nav_hover = -1;
static BOOL s_protect_hover = FALSE;
static BOOL s_extdel_hover = FALSE;
static BOOL s_support_hover = FALSE;
static BOOL s_tracking_leave = FALSE;
static NavFilter s_nav = NAV_ALL;
static BOOL s_using_previous = FALSE;
static wchar_t s_notice[768] = {0};
static int s_scan_choice = -1;
static BOOL s_protect_changed = FALSE;
static BOOL s_ext_protect_changed = FALSE;

static BOOL ManualDeleteConfirm(HWND owner, const wchar_t *summary);

static int S(int value)
{
    return MulDiv(value, g_dpi, 96);
}

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

static HFONT MakeFont(int pt100, int weight)
{
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(pt100, g_dpi, 7200);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    StringCchCopyW(lf.lfFaceName, ARRAYSIZE(lf.lfFaceName), L"Microsoft YaHei UI");
    return CreateFontIndirectW(&lf);
}

static void DestroyUiFonts(void)
{
    HFONT *fonts[] = {
        &s_font_title, &s_font_page, &s_font_subtitle,
        &s_font_card_label, &s_font_card_value, &s_font_card_sub,
        &s_font_nav, &s_font_nav_value, &s_font_body,
        &s_font_button, &s_font_small
    };
    size_t i;
    for (i = 0; i < ARRAYSIZE(fonts); i++)
    {
        if (*fonts[i] != NULL)
        {
            DeleteObject(*fonts[i]);
            *fonts[i] = NULL;
        }
    }
}

static void CreateUiFonts(void)
{
    DestroyUiFonts();
    s_font_title = MakeFont(1800, FW_BOLD);
    s_font_page = MakeFont(1600, FW_SEMIBOLD);
    s_font_subtitle = MakeFont(900, FW_NORMAL);
    s_font_card_label = MakeFont(820, FW_NORMAL);
    s_font_card_value = MakeFont(1650, FW_BOLD);
    s_font_card_sub = MakeFont(780, FW_NORMAL);
    s_font_nav = MakeFont(1000, FW_SEMIBOLD);
    s_font_nav_value = MakeFont(1150, FW_BOLD);
    s_font_body = MakeFont(900, FW_NORMAL);
    s_font_button = MakeFont(900, FW_SEMIBOLD);
    s_font_small = MakeFont(800, FW_NORMAL);
}

static void ApplyFonts(void)
{
    HWND controls[] = {s_btn_quick, s_btn_full, s_btn_dir, s_btn_stop, s_btn_batch, s_search};
    size_t i;
    for (i = 0; i < ARRAYSIZE(controls); i++)
    {
        if (controls[i] != NULL)
            SendMessageW(controls[i], WM_SETFONT, (WPARAM)s_font_button, TRUE);
    }
    if (s_search != NULL)
        SendMessageW(s_search, WM_SETFONT, (WPARAM)s_font_body, TRUE);
}

static void CenterWindow(HWND hwnd, HWND owner)
{
    RECT rc, area;
    int w, h, x, y;
    GetWindowRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (owner != NULL && IsWindow(owner))
        GetWindowRect(owner, &rc);
    else
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
    area = rc;
    x = area.left + (area.right - area.left - w) / 2;
    y = area.top + (area.bottom - area.top - h) / 2;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* ------------------------------------------------------------------ */
/* formatting helpers                                                  */
/* ------------------------------------------------------------------ */

static void FormatCount(uint64_t value, wchar_t *out, size_t cch)
{
    wchar_t digits[64];
    wchar_t result[80];
    int n;
    int i;
    int j = 0;
    int group = 0;

    StringCchPrintfW(digits, ARRAYSIZE(digits), L"%llu", (unsigned long long)value);
    n = (int)wcslen(digits);
    for (i = n - 1; i >= 0; i--)
    {
        result[j++] = digits[i];
        group++;
        if (group == 3 && i > 0)
        {
            result[j++] = L',';
            group = 0;
        }
    }
    result[j] = 0;
    for (i = 0; i < j / 2; i++)
    {
        wchar_t t = result[i];
        result[i] = result[j - 1 - i];
        result[j - 1 - i] = t;
    }
    StringCchCopyW(out, cch, result);
}

static const wchar_t *NavTitle(void)
{
    if (s_nav == NAV_OFFICE)
        return L"办公文件";
    if (s_nav == NAV_PROTECTED)
        return L"保护区";
    if (s_nav == NAV_EXT_PROTECTED)
        return L"后缀保护";
    return L"全部文件";
}

static BOOL FileTimeIsZero(const FILETIME *ft)
{
    return ft->dwLowDateTime == 0 && ft->dwHighDateTime == 0;
}

static void BuildHeaderSubtitle(wchar_t *out, size_t cch)
{
    if (Scan_IsRunning())
        StringCchCopyW(out, cch, L"正在扫描，结果会实时更新 · 可随时停止");
    else if (s_using_previous)
        StringCchCopyW(out, cch, g_scan_partial ? L"当前显示上一次扫描结果（上次扫描未完成）"
                                                : L"当前显示上一次扫描结果（未重新扫描）");
    else if (!FileTimeIsZero(&g_scan_end_ft))
        StringCchCopyW(out, cch, g_scan_partial ? L"扫描已停止，当前结果已保存到 data.xzq"
                                                : L"扫描完成，结果已保存到 data.xzq");
    else
        StringCchCopyW(out, cch, L"就绪 · 尚未扫描");
}

static void BuildFooter(wchar_t *out, size_t cch)
{
    ScanStats st;
    Data_GetStats(&st);
    if (s_notice[0] != 0)
    {
        StringCchCopyW(out, cch, s_notice);
        return;
    }
    if (Scan_IsRunning())
    {
        wchar_t path[512];
        Scan_GetCurrentPath(path, ARRAYSIZE(path));
        StringCchPrintfW(out, cch,
                         L"正在扫描 · 已发现 %llu 个文件 · 系统保护 %llu 个 · 当前：%ls",
                         (unsigned long long)st.clean_count + st.protected_count +
                             st.ext_protected_count + st.unsupported_count,
                         (unsigned long long)st.hidden_count,
                         path[0] ? path : L"准备中...");
        return;
    }
    if (s_using_previous && !FileTimeIsZero(&g_scan_end_ft))
    {
        wchar_t when[64];
        Time_FileTimeToString(&g_scan_end_ft, when, ARRAYSIZE(when));
        StringCchPrintfW(out, cch,
                         L"上一次扫描结果 · 时间 %ls · 共扫描 %llu 个文件 · 可清理 %llu 个 · 不支持 %llu 个",
                         when, (unsigned long long)st.total_scanned,
                         (unsigned long long)st.clean_count,
                         (unsigned long long)st.unsupported_count);
        return;
    }
    if (!FileTimeIsZero(&g_scan_end_ft))
    {
        StringCchPrintfW(out, cch,
                         L"扫描完成 · 共扫描 %llu 个文件 · 可清理 %llu 个 · 不支持 %llu 个 · 系统保护 %llu 个 · 跳过目录 %llu 个 · 耗时 %llu 秒",
                         (unsigned long long)st.total_scanned,
                         (unsigned long long)st.clean_count,
                         (unsigned long long)st.unsupported_count,
                         (unsigned long long)st.hidden_count,
                         (unsigned long long)st.skipped_dirs,
                         (unsigned long long)g_scan_seconds);
        return;
    }
    StringCchCopyW(out, cch, L"就绪 · 仅支持六类格式清理，点击「快速扫描」或「全盘扫描」开始。");
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */

static void DrawMagnifier(HDC dc, int cx, int cy, int r, COLORREF color, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    MoveToEx(dc, cx + r - width, cy + r - width, NULL);
    LineTo(dc, cx + r + S(6), cy + r + S(6));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void PaintSidebar(HDC dc, const RECT *client)
{
    RECT side = {0, 0, S(218), client->bottom};
    static const wchar_t *labels[4] = {L"全部文件", L"办公文件", L"保护区", L"后缀保护"};
    static const wchar_t *glyphs[4] = {L"全", L"办", L"保", L"缀"};
    int i;
    ScanStats st;
    wchar_t value[64];

    FillSolid(dc, &side, C_SIDEBAR);

    if (s_icon_sidebar != NULL)
        DrawIconEx(dc, S(20), S(24), s_icon_sidebar, S(36), S(36), 0, NULL, DI_NORMAL);

    {
        RECT r = {S(66), S(20), S(210), S(48)};
        DrawTextIn(dc, L"文件清理", r, s_font_title, C_WHITE,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        r.top = S(48);
        r.bottom = S(66);
        DrawTextIn(dc, L"FILE CLEANER", r, s_font_small, RGB(148, 163, 184),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    for (i = 0; i < 4; i++)
    {
        RECT r = s_nav_rects[i];
        BOOL active = ((int)s_nav == i);
        BOOL hover = (s_nav_hover == i);
        if (active || hover)
        {
            RECT fill = r;
            RoundedFill(dc, &fill, S(9), active ? C_BLUE : C_SIDEBAR_HOVER,
                        active ? C_BLUE : C_SIDEBAR_HOVER);
        }
        {
            RECT badge = {r.left + S(12), r.top + S(11), r.left + S(34), r.top + S(33)};
            RoundedFill(dc, &badge, S(6), active ? RGB(59, 130, 246) : RGB(30, 41, 59),
                        active ? RGB(59, 130, 246) : RGB(51, 65, 85));
            DrawTextIn(dc, glyphs[i], badge, s_font_small,
                       active ? C_WHITE : C_SIDEBAR_TEXT,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        {
            RECT text = {r.left + S(46), r.top, r.right - S(12), r.bottom};
            DrawTextIn(dc, labels[i], text, s_font_nav,
                       active ? C_WHITE : C_SIDEBAR_TEXT,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    {
        RECT r = s_extdel_rect;
        BOOL hover = s_extdel_hover;
        RECT text;
        RoundedFill(dc, &r, S(9), hover ? RGB(30, 41, 59) : RGB(22, 32, 50),
                    RGB(51, 65, 85));
        text.left = r.left + S(4);
        text.right = r.right - S(4);
        text.top = r.top;
        text.bottom = r.bottom;
        DrawTextIn(dc, L"按后缀删除", text, s_font_small, RGB(252, 165, 165),
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    {
        RECT r = s_support_rect;
        BOOL hover = s_support_hover;
        RECT text;
        RoundedFill(dc, &r, S(9), hover ? RGB(30, 41, 59) : RGB(22, 32, 50),
                    RGB(51, 65, 85));
        text.left = r.left + S(4);
        text.right = r.right - S(4);
        text.top = r.top;
        text.bottom = r.bottom;
        DrawTextIn(dc, L"支持格式", text, s_font_small, RGB(226, 232, 240),
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    Data_GetStats(&st);
    {
        int y = client->bottom - S(280);
        RECT line = {S(18), y, S(200), y + 1};
        RECT label = {S(20), y + S(18), S(200), y + S(40)};
        RECT val = {S(20), y + S(40), S(200), y + S(70)};
        FillSolid(dc, &line, C_SIDEBAR_LINE);
        DrawTextIn(dc, L"可清理空间", label, s_font_small, RGB(148, 163, 184),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Size_Format(st.clean_bytes, value, ARRAYSIZE(value));
        DrawTextIn(dc, value, val, s_font_nav_value, C_WHITE,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        label.top = y + S(82);
        label.bottom = y + S(104);
        DrawTextIn(dc, L"系统保护（仅统计）", label, s_font_small, RGB(148, 163, 184),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        FormatCount(st.hidden_count, value, ARRAYSIZE(value));
        val.top = y + S(104);
        val.bottom = y + S(130);
        DrawTextIn(dc, value, val, s_font_nav_value, RGB(226, 232, 240),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    {
        RECT r = s_protect_rect;
        BOOL hover = s_protect_hover;
        RoundedFill(dc, &r, S(9), hover ? RGB(30, 41, 59) : RGB(22, 32, 50),
                    RGB(51, 65, 85));
        {
            RECT text = {r.left + S(14), r.top, r.right - S(34), r.bottom};
            DrawTextIn(dc, (s_nav == NAV_EXT_PROTECTED) ? L"后缀保护管理" : L"保护区管理",
                       text, s_font_body, RGB(226, 232, 240),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        {
            RECT arrow = {r.right - S(32), r.top, r.right - S(12), r.bottom};
            DrawTextIn(dc, L"›", arrow, s_font_nav, RGB(148, 163, 184),
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    {
        RECT ver = {S(20), client->bottom - S(34), S(200), client->bottom - S(14)};
        DrawTextIn(dc, L"v" APP_VERSION, ver, s_font_small, RGB(100, 116, 139),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void PaintHeader(HDC dc, const RECT *client)
{
    RECT header = {S(218), 0, client->right, S(66)};
    RECT line = {S(218), S(65), client->right, S(66)};
    wchar_t subtitle[512];
    RECT r;
    FillSolid(dc, &header, C_WHITE);
    FillSolid(dc, &line, C_BORDER);

    r.left = S(242);
    r.top = S(10);
    r.right = client->right - S(100);
    r.bottom = S(40);
    DrawTextIn(dc, NavTitle(), r, s_font_page, C_TEXT,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    BuildHeaderSubtitle(subtitle, ARRAYSIZE(subtitle));
    r.top = S(38);
    r.bottom = S(60);
    DrawTextIn(dc, subtitle, r, s_font_subtitle, C_MUTED,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    r.left = client->right - S(104);
    r.right = client->right - S(24);
    r.top = S(18);
    r.bottom = S(48);
    DrawTextIn(dc, L"v" APP_VERSION, r, s_font_small, C_LIGHT,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

static void PaintSearchFrame(HDC dc)
{
    RECT r = s_search_rect;
    RoundedFill(dc, &r, S(9), C_WHITE, RGB(203, 213, 225));
    DrawMagnifier(dc, r.left + S(17), (r.top + r.bottom) / 2, S(5), RGB(100, 116, 139), 1);
}

static void PaintCards(HDC dc, const RECT *client)
{
    ScanStats st;
    int gap = S(14);
    int left = S(242);
    int right = client->right - S(24);
    int total = right - left;
    int card_w = (total - gap * 3) / 4;
    int y = S(138);
    int h = S(86);
    int i;
    wchar_t value[64];
    wchar_t sub[160];
    wchar_t total_text[64];
    wchar_t ext_text[64];

    Data_GetStats(&st);
    for (i = 0; i < 4; i++)
    {
        RECT card;
        RECT accent;
        RECT label;
        RECT val;
        RECT subr;
        COLORREF color;
        card.left = left + i * (card_w + gap);
        card.right = card.left + card_w;
        card.top = y;
        card.bottom = y + h;
        RoundedFill(dc, &card, S(10), C_WHITE, C_BORDER);

        accent.left = card.left + 1;
        accent.right = card.left + S(4);
        accent.top = card.top + S(17);
        accent.bottom = card.bottom - S(17);
        switch (i)
        {
        case 0: color = C_BLUE; break;
        case 1: color = RGB(14, 165, 233); break;
        case 2: color = C_ORANGE; break;
        default: color = RGB(148, 163, 184); break;
        }
        FillSolid(dc, &accent, color);

        label.left = card.left + S(16);
        label.right = card.right - S(12);
        label.top = card.top + S(10);
        label.bottom = card.top + S(30);
        val.left = label.left;
        val.right = label.right;
        val.top = card.top + S(28);
        val.bottom = card.bottom - S(18);
        subr.left = label.left;
        subr.right = label.right;
        subr.top = card.bottom - S(20);
        subr.bottom = card.bottom - S(6);

        switch (i)
        {
        case 0:
            DrawTextIn(dc, L"可清理文件", label, s_font_card_label, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            FormatCount(st.clean_count, value, ARRAYSIZE(value));
            DrawTextIn(dc, value, val, s_font_card_value, C_TEXT,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            FormatCount(st.total_scanned, total_text, ARRAYSIZE(total_text));
            FormatCount(st.unsupported_count, ext_text, ARRAYSIZE(ext_text));
            StringCchPrintfW(sub, ARRAYSIZE(sub), L"总 %ls · 不可清理 %ls",
                             total_text, ext_text);
            break;
        case 1:
            DrawTextIn(dc, L"可清理空间", label, s_font_card_label, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            Size_Format(st.clean_bytes, value, ARRAYSIZE(value));
            DrawTextIn(dc, value, val, s_font_card_value, C_TEXT,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            StringCchCopyW(sub, ARRAYSIZE(sub), L"删除后移入回收站");
            break;
        case 2:
            DrawTextIn(dc, L"受保护文件", label, s_font_card_label, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            FormatCount(st.protected_count + st.ext_protected_count, value, ARRAYSIZE(value));
            DrawTextIn(dc, value, val, s_font_card_value, C_ORANGE,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            FormatCount(st.protected_count, total_text, ARRAYSIZE(total_text));
            FormatCount(st.ext_protected_count, ext_text, ARRAYSIZE(ext_text));
            StringCchPrintfW(sub, ARRAYSIZE(sub), L"保护区 %ls · 后缀保护 %ls",
                             total_text, ext_text);
            break;
        default:
            DrawTextIn(dc, L"系统保护（已隐藏）", label, s_font_card_label, C_MUTED,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            FormatCount(st.hidden_count, value, ARRAYSIZE(value));
            DrawTextIn(dc, value, val, s_font_card_value, RGB(100, 116, 139),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            Size_Format(st.hidden_bytes, total_text, ARRAYSIZE(total_text));
            StringCchPrintfW(sub, ARRAYSIZE(sub), L"%ls · 仅统计不显示", total_text);
            break;
        }
        DrawTextIn(dc, sub, subr, s_font_card_sub, C_LIGHT,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void PaintFooter(HDC dc, const RECT *client)
{
    RECT r;
    wchar_t text[1024];
    BuildFooter(text, ARRAYSIZE(text));
    r.left = S(242);
    r.right = client->right - S(24);
    r.top = client->bottom - S(32);
    r.bottom = client->bottom - S(8);
    DrawTextIn(dc, text, r, s_font_small, C_MUTED,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void PaintMain(HWND hwnd)
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

    FillSolid(mem, &rc, C_BG);
    PaintSidebar(mem, &rc);
    PaintHeader(mem, &rc);
    PaintCards(mem, &rc);
    PaintSearchFrame(mem);
    PaintFooter(mem, &rc);

    BitBlt(dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* ------------------------------------------------------------------ */
/* layout                                                              */
/* ------------------------------------------------------------------ */

static void LayoutControls(HWND hwnd)
{
    RECT rc;
    int sidebar = S(218);
    int margin = S(24);
    int content_x = sidebar + margin;
    int content_w;
    int y = S(82);
    int h = S(38);
    int gap = S(8);
    int x;
    int search_w;
    int batch_w;
    int search_x;
    int batch_x;
    int stats_w;
    int list_y = S(240);
    int footer_top;
    int list_h;
    int i;

    GetClientRect(hwnd, &rc);
    content_w = rc.right - content_x - margin;
    if (content_w < S(600))
        content_w = S(600);

    search_w = S(230);
    batch_w = S(190);
    if (content_w < S(840))
    {
        search_w = S(190);
        batch_w = S(165);
    }
    search_x = content_x + content_w - search_w;
    batch_x = search_x - S(12) - batch_w;

    x = content_x;
    MoveWindow(s_btn_quick, x, y, S(96), h, TRUE);
    x += S(96) + gap;
    MoveWindow(s_btn_full, x, y, S(96), h, TRUE);
    x += S(96) + gap;
    MoveWindow(s_btn_dir, x, y, S(96), h, TRUE);
    x += S(96) + gap;
    MoveWindow(s_btn_stop, x, y, S(80), h, TRUE);

    MoveWindow(s_btn_batch, batch_x, y, batch_w, h, TRUE);
    s_search_rect.left = search_x;
    s_search_rect.top = y;
    s_search_rect.right = search_x + search_w;
    s_search_rect.bottom = y + h;
    if (s_search != NULL)
        MoveWindow(s_search, search_x + S(34), y + S(9), search_w - S(46), S(20), TRUE);

    stats_w = content_w;
    (void)stats_w;

    footer_top = rc.bottom - S(36);
    list_h = footer_top - list_y - S(6);
    if (list_h < S(120))
        list_h = S(120);
    if (s_list != NULL)
        List_Layout(content_x, list_y, content_w, list_h);

    for (i = 0; i < 4; i++)
    {
        s_nav_rects[i].left = S(16);
        s_nav_rects[i].right = sidebar - S(16);
        s_nav_rects[i].top = S(126) + i * S(48);
        s_nav_rects[i].bottom = s_nav_rects[i].top + S(44);
    }
    s_extdel_rect.left = S(16);
    s_extdel_rect.right = S(106);
    s_extdel_rect.top = S(324);
    s_extdel_rect.bottom = s_extdel_rect.top + S(36);

    s_support_rect.left = S(112);
    s_support_rect.right = sidebar - S(16);
    s_support_rect.top = s_extdel_rect.top;
    s_support_rect.bottom = s_extdel_rect.bottom;

    s_protect_rect.left = S(16);
    s_protect_rect.right = sidebar - S(16);
    s_protect_rect.bottom = rc.bottom - S(98);
    s_protect_rect.top = s_protect_rect.bottom - S(40);

    InvalidateRect(hwnd, NULL, FALSE);
}

/* ------------------------------------------------------------------ */
/* owner-draw buttons                                                  */
/* ------------------------------------------------------------------ */

typedef enum ButtonKind
{
    BTN_PRIMARY,
    BTN_SECONDARY,
    BTN_DANGER_OUTLINE,
    BTN_DANGER_SOFT
} ButtonKind;

static ButtonKind ButtonKindForId(UINT id)
{
    if (id == ID_BTN_QUICK)
        return BTN_PRIMARY;
    if (id == ID_BTN_BATCH)
        return BTN_DANGER_SOFT;
    if (id == ID_BTN_STOP)
        return BTN_DANGER_OUTLINE;
    return BTN_SECONDARY;
}

static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                           UINT_PTR id, DWORD_PTR ref)
{
    (void)id;
    (void)ref;
    switch (msg)
    {
    case WM_MOUSEMOVE:
        if (GetWindowLongPtrW(hwnd, GWLP_USERDATA) == 0)
        {
            TRACKMOUSEEVENT tme;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 1);
            InvalidateRect(hwnd, NULL, FALSE);
            ZeroMemory(&tme, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        break;
    case WM_MOUSELEAVE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, ButtonSubclassProc, id);
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void DrawOwnerButton(const DRAWITEMSTRUCT *dis)
{
    ButtonKind kind = ButtonKindForId(dis->CtlID);
    BOOL disabled = (dis->itemState & ODS_DISABLED) != 0;
    BOOL pressed = (dis->itemState & ODS_SELECTED) != 0;
    BOOL hover = GetWindowLongPtrW(dis->hwndItem, GWLP_USERDATA) != 0;
    RECT r = dis->rcItem;
    RECT inner = r;
    wchar_t text[128];
    COLORREF fill;
    COLORREF border;
    COLORREF text_color;
    int radius = S(9);

    GetWindowTextW(dis->hwndItem, text, ARRAYSIZE(text));
    if (disabled)
    {
        fill = RGB(241, 245, 249);
        border = RGB(226, 232, 240);
        text_color = RGB(148, 163, 184);
    }
    else if (kind == BTN_PRIMARY)
    {
        fill = pressed ? C_BLUE_PRESSED : (hover ? C_BLUE_DARK : C_BLUE);
        border = fill;
        text_color = C_WHITE;
    }
    else if (kind == BTN_DANGER_SOFT)
    {
        fill = pressed ? RGB(254, 202, 202) : (hover ? C_RED_SOFT_HOVER : C_RED_SOFT);
        border = C_RED_BORDER;
        text_color = C_RED_DARK;
    }
    else if (kind == BTN_DANGER_OUTLINE)
    {
        fill = pressed ? C_RED_SOFT : (hover ? C_RED_SOFT : C_WHITE);
        border = C_RED_BORDER;
        text_color = C_RED;
    }
    else
    {
        fill = pressed ? RGB(241, 245, 249) : (hover ? RGB(248, 250, 252) : C_WHITE);
        border = RGB(203, 213, 225);
        text_color = RGB(51, 65, 85);
    }

    InflateRect(&inner, -1, -1);
    RoundedFill(dis->hDC, &inner, radius, fill, border);
    DrawTextIn(dis->hDC, text, inner, s_font_button, text_color,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if ((dis->itemState & ODS_FOCUS) != 0 && !disabled)
    {
        RECT focus = inner;
        InflateRect(&focus, -S(4), -S(4));
        DrawFocusRect(dis->hDC, &focus);
    }
}

static void InstallButton(HWND button, int id)
{
    SetWindowSubclass(button, ButtonSubclassProc, (UINT_PTR)id, 0);
    SetWindowLongPtrW(button, GWLP_USERDATA, 0);
}

/* ------------------------------------------------------------------ */
/* deletion                                                            */
/* ------------------------------------------------------------------ */

static void SaveDataOrError(void)
{
    wchar_t err[APP_ERR_CCH];
    if (Store_Save(err, ARRAYSIZE(err)) != 0)
        Util_ErrorShowf(L"%ls", err[0] ? err : L"保存扫描数据失败。");
}

static void MarkRecordDeleted(int index)
{
    FileRec *r = Data_Get(index);
    if (r == NULL || (r->flags & REC_FLAG_DELETED) != 0)
        return;
    if ((r->flags & REC_FLAG_PROTECTED) != 0)
    {
        r->flags |= REC_FLAG_DELETED;
        InterlockedDecrement64(&g_protected_count);
        InterlockedAdd64(&g_protected_bytes, -(LONG64)r->size);
    }
    else if ((r->flags & REC_FLAG_EXT_PROTECTED) != 0)
    {
        r->flags |= REC_FLAG_DELETED;
        InterlockedDecrement64(&g_ext_protected_count);
        InterlockedAdd64(&g_ext_protected_bytes, -(LONG64)r->size);
    }
    else
    {
        wchar_t ext[PROTECTED_EXT_CCH];
        Path_ExtensionLower(r->path, ext, ARRAYSIZE(ext));
        r->flags |= REC_FLAG_DELETED;
        if (Path_ExtIsCleanupAllowed(ext))
        {
            InterlockedDecrement64(&g_clean_count);
            InterlockedAdd64(&g_clean_bytes, -(LONG64)r->size);
        }
        else
        {
            InterlockedDecrement64(&g_unsupported_count);
            InterlockedAdd64(&g_unsupported_bytes, -(LONG64)r->size);
        }
    }
    g_data_dirty = TRUE;
}

static BOOL PathExists(const wchar_t *path)
{
    wchar_t *api = Path_ApiDup(path);
    DWORD attrs = GetFileAttributesW(api);
    free(api);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

static BOOL RecordIsHardProtected(const FileRec *r)
{
    if (r == NULL)
        return TRUE;
    if (Path_IsWindowsProtected(r->path))
        return TRUE;
    if (Path_IsSystemCritical(r->path, 0))
        return TRUE;
    if (Path_IsSameOrUnderAppDir(r->path))
        return TRUE;
    return FALSE;
}

static BOOL RecordIsCleanupAllowed(const FileRec *r)
{
    wchar_t ext[PROTECTED_EXT_CCH];
    if (r == NULL)
        return FALSE;
    Path_ExtensionLower(r->path, ext, ARRAYSIZE(ext));
    return Path_ExtIsCleanupAllowed(ext);
}

static int DeleteIndexes(const int *indices, int count, const wchar_t *success_note)
{
    wchar_t **paths;
    int path_count = 0;
    int i;
    size_t buf_chars = 1;
    wchar_t *from;
    SHFILEOPSTRUCTW op;
    int deleted = 0;
    int failed = 0;
    wchar_t first_failed[1024];
    int rc;

    if (count <= 0)
        return 0;
    paths = (wchar_t **)Util_Alloc((size_t)count * sizeof(wchar_t *));
    first_failed[0] = 0;

    for (i = 0; i < count; i++)
    {
        FileRec *r = Data_Get(indices[i]);
        if (r == NULL || (r->flags & REC_FLAG_DELETED) != 0)
            continue;
        if ((r->flags & (REC_FLAG_PROTECTED | REC_FLAG_EXT_PROTECTED)) != 0)
            continue;
        if (!RecordIsCleanupAllowed(r) || RecordIsHardProtected(r))
            continue;
        if (!PathExists(r->path))
        {
            MarkRecordDeleted(indices[i]);
            deleted++;
            continue;
        }
        paths[path_count++] = r->path;
        buf_chars += wcslen(r->path) + 1;
    }

    if (path_count > 0)
    {
        from = (wchar_t *)Util_Alloc((buf_chars + 1) * sizeof(wchar_t));
        {
            wchar_t *p = from;
            for (i = 0; i < path_count; i++)
            {
                size_t len = wcslen(paths[i]);
                memcpy(p, paths[i], len * sizeof(wchar_t));
                p += len;
                *p++ = 0;
            }
            *p = 0;
        }
        ZeroMemory(&op, sizeof(op));
        op.hwnd = g_main_window;
        op.wFunc = FO_DELETE;
        op.pFrom = from;
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

        SetCursor(LoadCursorW(NULL, IDC_WAIT));
        rc = SHFileOperationW(&op);
        SetCursor(LoadCursorW(NULL, IDC_ARROW));
        if (rc != 0 || op.fAnyOperationsAborted)
        {
            Util_ErrorShowf(L"删除文件失败（系统返回码 %d）。\r\n可能原因：文件被占用、权限不足或回收站不可用。",
                            rc);
        }
        free(from);
    }

    for (i = 0; i < count; i++)
    {
        FileRec *r = Data_Get(indices[i]);
        if (r == NULL || (r->flags & REC_FLAG_DELETED) != 0)
            continue;
        if ((r->flags & (REC_FLAG_PROTECTED | REC_FLAG_EXT_PROTECTED)) != 0)
            continue;
        if (!RecordIsCleanupAllowed(r) || RecordIsHardProtected(r))
            continue;
        if (!PathExists(r->path))
        {
            MarkRecordDeleted(indices[i]);
            deleted++;
        }
        else
        {
            failed++;
            if (first_failed[0] == 0)
                StringCchCopyW(first_failed, ARRAYSIZE(first_failed), r->path);
        }
    }
    free(paths);

    if (deleted > 0)
    {
        g_data_dirty = TRUE;
        SaveDataOrError();
        UI_RefreshAll();
    }
    if (failed > 0)
    {
        Util_ErrorShowf(L"有 %d 个文件未能删除。\r\n示例：%ls", failed, first_failed);
    }
    if (deleted > 0 && success_note != NULL)
    {
        StringCchCopyW(s_notice, ARRAYSIZE(s_notice), success_note);
        if (g_main_window != NULL)
            InvalidateRect(g_main_window, NULL, FALSE);
    }
    return deleted;
}

void UI_RequestDeleteRecord(int rec_index)
{
    FileRec *r = Data_Get(rec_index);
    wchar_t text[1200];
    wchar_t size_text[64];
    wchar_t date_text[64];
    int index = rec_index;
    int deleted;

    if (Scan_IsRunning())
    {
        Util_Info(L"扫描正在进行中，请先点击「停止扫描」，再删除文件。", APP_NAME);
        return;
    }
    if (r == NULL || (r->flags & REC_FLAG_DELETED) != 0)
        return;
    if (Path_IsWindowsProtected(r->path))
    {
        Util_Info(L"C:\\Windows 属于系统默认保护路径，程序不会删除其中的任何文件。",
                  L"系统保护");
        return;
    }
    {
        wchar_t ext[PROTECTED_EXT_CCH];
        Path_ExtensionLower(r->path, ext, ARRAYSIZE(ext));
        if (!Path_ExtIsCleanupAllowed(ext))
        {
            Util_Info(L"本程序仅支持清理办公文档、音视频、压缩归档、网页资源、邮件日历联系人、3D 模型六类格式。\r\n该文件不在支持范围内，不能删除。",
                      L"不支持的格式");
            return;
        }
    }
    if ((r->flags & REC_FLAG_PROTECTED) != 0)
    {
        UI_OpenRecordLocation(rec_index);
        Util_Info(L"该文件位于保护区，程序不会删除它。\r\n已为你打开文件所在位置，请手动处理。",
                  L"保护区");
        return;
    }
    if ((r->flags & REC_FLAG_EXT_PROTECTED) != 0)
    {
        UI_OpenRecordLocation(rec_index);
        Util_Info(L"该文件的后缀在「后缀保护」中，程序不会删除它。\r\n已为你打开文件所在位置，请手动处理。",
                  L"后缀保护");
        return;
    }

    Size_Format(r->size, size_text, ARRAYSIZE(size_text));
    Time_FileTimeToString(&r->mtime, date_text, ARRAYSIZE(date_text));
    StringCchPrintfW(text, ARRAYSIZE(text),
                     L"确定要删除这个文件吗？\r\n\r\n%ls\r\n%ls\r\n大小：%ls\r\n修改时间：%ls\r\n\r\n文件将被移入回收站。",
                     r->name, r->path, size_text, date_text);
    if (!ManualDeleteConfirm(g_main_window, text))
        return;

    deleted = DeleteIndexes(&index, 1, L"文件已移入回收站。");
    (void)deleted;
    InvalidateRect(g_main_window, NULL, FALSE);
}

void UI_OpenRecordLocation(int rec_index)
{
    FileRec *r = Data_Get(rec_index);
    wchar_t *parent;
    wchar_t *params;
    SHELLEXECUTEINFOW sei;

    if (r == NULL)
        return;
    if (!PathExists(r->path))
    {
        MarkRecordDeleted(rec_index);
        Data_RefreshCounters();
        UI_RefreshAll();
        StringCchCopyW(s_notice, ARRAYSIZE(s_notice), L"该文件已经不存在，已从列表中移除。");
        return;
    }

    parent = Path_ParentDup(r->path);
    params = Str_Format(L"/select,\"%ls\"", r->path);
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = L"explorer.exe";
    sei.lpParameters = params;
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
    {
        wchar_t *api = Path_ApiDup(parent);
        ZeroMemory(&sei, sizeof(sei));
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = L"open";
        sei.lpFile = api;
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
        free(api);
    }
    free(params);
    free(parent);
}

static void DeleteOldOfficeFiles(HWND owner)
{
    SYSTEMTIME cutoff;
    FILETIME cutoff_ft;
    wchar_t cutoff_text[64];
    int *indices = NULL;
    int count = 0;
    int cap = 0;
    int i;
    int n = Data_Count();
    uint64_t total_size = 0;
    wchar_t text[1600];
    wchar_t size_text[64];
    int deleted;

    if (Scan_IsRunning())
        return;
    cutoff = Time_GetCleanupCutoff();
    Time_SystemToString(&cutoff, cutoff_text, ARRAYSIZE(cutoff_text));
    Time_CutoffFileTime(&cutoff, &cutoff_ft);

    for (i = 0; i < n; i++)
    {
        FileRec *r = Data_Get(i);
        if (r == NULL || (r->flags & REC_FLAG_DELETED) != 0)
            continue;
        if ((r->flags & (REC_FLAG_PROTECTED | REC_FLAG_EXT_PROTECTED)) != 0)
            continue;
        if (!RecordIsCleanupAllowed(r) || RecordIsHardProtected(r))
            continue;
        if ((r->flags & REC_FLAG_OFFICE) == 0)
            continue;
        if (!Time_FileTimeOlderThan(&r->mtime, &cutoff_ft))
            continue;
        if (count >= cap)
        {
            cap = (cap == 0) ? 64 : cap * 2;
            indices = (int *)Util_Realloc(indices, (size_t)cap * sizeof(int));
        }
        indices[count++] = i;
        total_size += r->size;
    }

    if (count == 0)
    {
        free(indices);
        StringCchPrintfW(text, ARRAYSIZE(text),
                         L"没有找到修改时间早于 %ls 的办公文件。\r\n\r\n规则：以每周六 24:00（周日 00:00）为分界，只清理严格超过一周的办公文件。",
                         cutoff_text);
        MessageBoxW(owner, text, L"没有可清理文件", MB_OK | MB_ICONINFORMATION);
        return;
    }

    Size_Format(total_size, size_text, ARRAYSIZE(size_text));
    StringCchPrintfW(text, ARRAYSIZE(text),
                     L"确定要清理 %d 个办公文件吗？\r\n\r\n修改时间早于：%ls\r\n合计大小：%ls\r\n\r\n这些文件将被移入回收站。保护区中的文件不会受到影响。",
                     count, cutoff_text, size_text);
    if (!ManualDeleteConfirm(owner, text))
    {
        free(indices);
        return;
    }

    deleted = DeleteIndexes(indices, count, L"已清理一周前的办公文件。");
    free(indices);
    if (deleted > 0)
    {
        StringCchPrintfW(text, ARRAYSIZE(text),
                         L"已将 %d 个办公文件移入回收站。\r\n清空回收站后才会真正释放磁盘空间。", deleted);
        MessageBoxW(owner, text, L"清理完成", MB_OK | MB_ICONINFORMATION);
    }
}

/* ------------------------------------------------------------------ */
/* protection dialog                                                   */
/* ------------------------------------------------------------------ */

static void SetDlgItemFont(HWND dlg, int id, HFONT font)
{
    HWND h = GetDlgItem(dlg, id);
    if (h != NULL && font != NULL)
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
}

static void ProtectRefreshList(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_PROT_LIST);
    wchar_t info[256];
    int i;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_protected_dir_count; i++)
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)g_protected_dirs[i]);
    EnableWindow(GetDlgItem(dlg, IDC_PROT_REMOVE), FALSE);
    EnableWindow(GetDlgItem(dlg, IDC_PROT_ADD), g_protected_dir_count < PROTECTED_DIR_MAX);
    StringCchPrintfW(info, ARRAYSIZE(info), L"已保护 %d 个目录（最多 %d 个）。保护区内文件仍会被扫描，但无法在本程序中删除。",
                     g_protected_dir_count, PROTECTED_DIR_MAX);
    SetDlgItemTextW(dlg, IDC_PROT_INFO, info);
    SetDlgItemTextW(dlg, IDC_PROT_FOOT,
                    L"提示：双击文件列表可打开文件所在位置。C:\\Windows 为默认系统保护路径，不可移除。设置保存在 data.xzq 中。");
}

static void ProtectAddDir(HWND dlg)
{
    wchar_t *dir;
    int i;
    if (g_protected_dir_count >= PROTECTED_DIR_MAX)
        return;
    dir = UI_PickFolder(dlg, L"选择要加入保护区的目录");
    if (dir == NULL)
        return;
    for (i = 0; i < g_protected_dir_count; i++)
    {
        if (_wcsicmp(g_protected_dirs[i], dir) == 0)
        {
            MessageBoxW(dlg, L"该目录已经在保护区中。", APP_NAME, MB_OK | MB_ICONINFORMATION);
            free(dir);
            return;
        }
    }
    g_protected_dirs[g_protected_dir_count++] = dir;
    Data_ApplyProtection();
    g_data_dirty = TRUE;
    s_protect_changed = TRUE;
    ProtectRefreshList(dlg);
    List_Rebuild(TRUE);
    InvalidateRect(g_main_window, NULL, FALSE);
}

static void ProtectRemoveDir(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_PROT_LIST);
    int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    int i;
    if (sel < 0 || sel >= g_protected_dir_count)
        return;
    if (g_windows_dir[0] != 0 && _wcsicmp(g_protected_dirs[sel], g_windows_dir) == 0)
    {
        MessageBoxW(dlg,
                    L"C:\\Windows 是程序默认的系统保护路径，不能从列表中移除。\r\n该路径下的文件始终不会显示，也不会被本程序删除。",
                    L"系统默认保护", MB_OK | MB_ICONINFORMATION);
        return;
    }
    free(g_protected_dirs[sel]);
    for (i = sel; i + 1 < g_protected_dir_count; i++)
        g_protected_dirs[i] = g_protected_dirs[i + 1];
    g_protected_dir_count--;
    g_protected_dirs[g_protected_dir_count] = NULL;
    Data_ApplyProtection();
    g_data_dirty = TRUE;
    s_protect_changed = TRUE;
    ProtectRefreshList(dlg);
    List_Rebuild(TRUE);
    InvalidateRect(g_main_window, NULL, FALSE);
}

static INT_PTR CALLBACK ProtectDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg)
    {
    case WM_INITDIALOG:
        SetWindowTextW(dlg, L"保护区管理");
        SetDlgItemTextW(dlg, IDC_PROT_ADD, L"添加目录");
        SetDlgItemTextW(dlg, IDC_PROT_REMOVE, L"移出保护区");
        SetDlgItemTextW(dlg, IDCANCEL, L"关闭");
        SetDlgItemFont(dlg, IDC_PROT_INFO, s_font_body);
        SetDlgItemFont(dlg, IDC_PROT_LIST, s_font_body);
        SetDlgItemFont(dlg, IDC_PROT_ADD, s_font_button);
        SetDlgItemFont(dlg, IDC_PROT_REMOVE, s_font_button);
        SetDlgItemFont(dlg, IDCANCEL, s_font_button);
        SetDlgItemFont(dlg, IDC_PROT_FOOT, s_font_small);
        s_protect_changed = FALSE;
        ProtectRefreshList(dlg);
        CenterWindow(dlg, g_main_window);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_PROT_ADD:
            ProtectAddDir(dlg);
            return TRUE;
        case IDC_PROT_REMOVE:
            ProtectRemoveDir(dlg);
            return TRUE;
        case IDC_PROT_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE)
            {
                int sel = (int)SendMessageW(GetDlgItem(dlg, IDC_PROT_LIST), LB_GETCURSEL, 0, 0);
                EnableWindow(GetDlgItem(dlg, IDC_PROT_REMOVE), sel >= 0);
            }
            return TRUE;
        case IDCANCEL:
            if (s_protect_changed)
            {
                SaveDataOrError();
                s_protect_changed = FALSE;
            }
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void UI_ShowProtectDialog(HWND owner)
{
    if (Scan_IsRunning())
    {
        Util_Info(L"扫描进行中，暂时不能修改保护区。\r\n请先停止扫描。", APP_NAME);
        return;
    }
    DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_PROTECT), owner, ProtectDlgProc, 0);
    List_Rebuild(TRUE);
    InvalidateRect(g_main_window, NULL, FALSE);
}

/* ------------------------------------------------------------------ */
/* suffix protection dialog                                            */
/* ------------------------------------------------------------------ */

static void ExtRefreshList(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_EXT_LIST);
    wchar_t info[256];
    int i;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_protected_ext_count; i++)
    {
        wchar_t display[PROTECTED_EXT_CCH + 8];
        StringCchPrintfW(display, ARRAYSIZE(display), L".%ls", g_protected_exts[i]);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)display);
    }
    EnableWindow(GetDlgItem(dlg, IDC_EXT_REMOVE), FALSE);
    EnableWindow(GetDlgItem(dlg, IDC_EXT_ADD), g_protected_ext_count < PROTECTED_EXT_MAX);
    StringCchPrintfW(info, ARRAYSIZE(info),
                     L"已启用 %d 个后缀保护（最多 %d 个）。这些文件仍会被扫描和显示，但程序拒绝删除。",
                     g_protected_ext_count, PROTECTED_EXT_MAX);
    SetDlgItemTextW(dlg, IDC_EXT_INFO, info);
    SetDlgItemTextW(dlg, IDC_EXT_FOOT,
                    L"添加时无需输入点号，例如输入 bat 即可。\n默认包含 bat、log、json、bin、ini、xml 等。\n设置保存在 data.xzq。");
}

static BOOL NormalizeExtension(const wchar_t *input, wchar_t *out, size_t cch)
{
    wchar_t tmp[128];
    size_t start = 0;
    size_t end;
    size_t i;
    size_t n = 0;

    if (input == NULL || out == NULL || cch == 0)
        return FALSE;
    StringCchCopyW(tmp, ARRAYSIZE(tmp), input);
    end = wcslen(tmp);
    while (end > 0 && (tmp[end - 1] == L' ' || tmp[end - 1] == L'\t' ||
                       tmp[end - 1] == L'\r' || tmp[end - 1] == L'\n'))
        end--;
    tmp[end] = 0;
    while (tmp[start] == L' ' || tmp[start] == L'\t')
        start++;
    while (tmp[start] == L'.')
        start++;
    if (tmp[start] == 0)
        return FALSE;
    for (i = start; tmp[i] != 0; i++)
    {
        wchar_t c = (wchar_t)towlower(tmp[i]);
        if (!iswalnum((wint_t)c) && c != L'_' && c != L'-')
            return FALSE;
        if (n + 1 >= cch)
            return FALSE;
        out[n++] = c;
    }
    out[n] = 0;
    return n > 0;
}

static void ExtAddExtension(HWND dlg)
{
    wchar_t raw[128];
    wchar_t ext[PROTECTED_EXT_CCH];
    int i;
    int new_index;

    if (g_protected_ext_count >= PROTECTED_EXT_MAX)
        return;
    GetDlgItemTextW(dlg, IDC_EXT_EDIT, raw, ARRAYSIZE(raw));
    if (!NormalizeExtension(raw, ext, ARRAYSIZE(ext)))
    {
        MessageBoxW(dlg, L"请输入有效的后缀，例如：bat、json、ini。\r\n只允许字母、数字、下划线和连字符，不需要输入点号。",
                    L"后缀格式不正确", MB_OK | MB_ICONINFORMATION);
        return;
    }
    for (i = 0; i < g_protected_ext_count; i++)
    {
        if (_wcsicmp(g_protected_exts[i], ext) == 0)
        {
            MessageBoxW(dlg, L"该后缀已经在后缀保护列表中。", APP_NAME, MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
    new_index = g_protected_ext_count;
    g_protected_exts[g_protected_ext_count++] = Util_WcsDup(ext);
    Data_ApplyProtection();
    s_ext_protect_changed = TRUE;
    g_data_dirty = TRUE;
    SetDlgItemTextW(dlg, IDC_EXT_EDIT, L"");
    ExtRefreshList(dlg);
    SendMessageW(GetDlgItem(dlg, IDC_EXT_LIST), LB_SETCURSEL, (WPARAM)new_index, 0);
    EnableWindow(GetDlgItem(dlg, IDC_EXT_REMOVE), TRUE);
    List_Rebuild(TRUE);
    InvalidateRect(g_main_window, NULL, FALSE);
}

static void ExtRemoveExtension(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_EXT_LIST);
    int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    int i;
    if (sel < 0 || sel >= g_protected_ext_count)
        return;
    free(g_protected_exts[sel]);
    for (i = sel; i + 1 < g_protected_ext_count; i++)
        g_protected_exts[i] = g_protected_exts[i + 1];
    g_protected_ext_count--;
    g_protected_exts[g_protected_ext_count] = NULL;
    Data_ApplyProtection();
    s_ext_protect_changed = TRUE;
    g_data_dirty = TRUE;
    ExtRefreshList(dlg);
    if (g_protected_ext_count > 0)
    {
        int next = (sel < g_protected_ext_count) ? sel : g_protected_ext_count - 1;
        SendMessageW(list, LB_SETCURSEL, (WPARAM)next, 0);
        EnableWindow(GetDlgItem(dlg, IDC_EXT_REMOVE), TRUE);
    }
    List_Rebuild(TRUE);
    InvalidateRect(g_main_window, NULL, FALSE);
}

static INT_PTR CALLBACK ExtProtectDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg)
    {
    case WM_INITDIALOG:
        SetWindowTextW(dlg, L"后缀保护管理");
        SetDlgItemTextW(dlg, IDC_EXT_ADD, L"添加后缀");
        SetDlgItemTextW(dlg, IDC_EXT_REMOVE, L"移除后缀");
        SetDlgItemTextW(dlg, IDCANCEL, L"关闭");
        SetDlgItemFont(dlg, IDC_EXT_INFO, s_font_body);
        SetDlgItemFont(dlg, IDC_EXT_LIST, s_font_body);
        SetDlgItemFont(dlg, IDC_EXT_EDIT, s_font_body);
        SetDlgItemFont(dlg, IDC_EXT_ADD, s_font_button);
        SetDlgItemFont(dlg, IDC_EXT_REMOVE, s_font_button);
        SetDlgItemFont(dlg, IDCANCEL, s_font_button);
        SetDlgItemFont(dlg, IDC_EXT_FOOT, s_font_small);
        SendDlgItemMessageW(dlg, IDC_EXT_EDIT, EM_SETLIMITTEXT, 31, 0);
        SendDlgItemMessageW(dlg, IDC_EXT_EDIT, EM_SETCUEBANNER, TRUE, (LPARAM)L"例如 bat、json、ini");
        s_ext_protect_changed = FALSE;
        ExtRefreshList(dlg);
        CenterWindow(dlg, g_main_window);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_EXT_ADD:
            ExtAddExtension(dlg);
            return TRUE;
        case IDC_EXT_REMOVE:
            ExtRemoveExtension(dlg);
            return TRUE;
        case IDC_EXT_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE)
            {
                int sel = (int)SendMessageW(GetDlgItem(dlg, IDC_EXT_LIST), LB_GETCURSEL, 0, 0);
                EnableWindow(GetDlgItem(dlg, IDC_EXT_REMOVE), sel >= 0);
            }
            return TRUE;
        case IDCANCEL:
            if (s_ext_protect_changed)
            {
                SaveDataOrError();
                s_ext_protect_changed = FALSE;
            }
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void UI_ShowExtProtectDialog(HWND owner)
{
    if (Scan_IsRunning())
    {
        Util_Info(L"扫描进行中，暂时不能修改后缀保护。\r\n请先停止扫描。", APP_NAME);
        return;
    }
    DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_EXT_PROTECT), owner, ExtProtectDlgProc, 0);
    List_Rebuild(TRUE);
    InvalidateRect(g_main_window, NULL, FALSE);
}

/* ------------------------------------------------------------------ */
/* delete by specified extension                                       */
/* ------------------------------------------------------------------ */

static int *s_extdel_records = NULL;
static int s_extdel_count = 0;
static int s_extdel_capacity = 0;

static void ExtDeleteFreeRecords(void)
{
    free(s_extdel_records);
    s_extdel_records = NULL;
    s_extdel_count = 0;
    s_extdel_capacity = 0;
}

static BOOL ExtDeleteIsSeparator(wchar_t c)
{
    return c == L';' || c == L',' || c == L'|' || c == L'、' ||
           c == L'，' || c == L'；' || c == L' ' || c == L'\t' ||
           c == L'\r' || c == L'\n';
}

static int ParseDeleteExtensions(const wchar_t *input, wchar_t exts[][PROTECTED_EXT_CCH],
                                 int max_exts, wchar_t *bad, size_t bad_cch)
{
    const wchar_t *p = input;
    int count = 0;
    if (bad != NULL && bad_cch > 0)
        bad[0] = 0;
    while (*p != 0)
    {
        wchar_t token[128];
        wchar_t ext[PROTECTED_EXT_CCH];
        size_t n = 0;
        int i;
        BOOL duplicate = FALSE;
        while (*p != 0 && ExtDeleteIsSeparator(*p))
            p++;
        if (*p == 0)
            break;
        while (*p != 0 && !ExtDeleteIsSeparator(*p))
        {
            if (n + 1 < ARRAYSIZE(token))
                token[n++] = *p;
            p++;
        }
        token[n] = 0;
        if (!NormalizeExtension(token, ext, ARRAYSIZE(ext)))
        {
            if (bad != NULL)
                StringCchCopyW(bad, bad_cch, token);
            return -1;
        }
        for (i = 0; i < count; i++)
        {
            if (_wcsicmp(exts[i], ext) == 0)
            {
                duplicate = TRUE;
                break;
            }
        }
        if (!duplicate && count < max_exts)
            StringCchCopyW(exts[count++], PROTECTED_EXT_CCH, ext);
    }
    return count;
}

static BOOL ExtDeleteMatches(const wchar_t *path, wchar_t exts[][PROTECTED_EXT_CCH], int ext_count)
{
    wchar_t ext[PROTECTED_EXT_CCH];
    int i;
    Path_ExtensionLower(path, ext, ARRAYSIZE(ext));
    if (ext[0] == 0)
        return FALSE;
    for (i = 0; i < ext_count; i++)
    {
        if (_wcsicmp(ext, exts[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

static int CompareExtDeleteRecords(const void *pa, const void *pb)
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
        return (c > 0) ? -1 : 1;
    if (a->size != b->size)
        return (a->size > b->size) ? -1 : 1;
    return _wcsicmp(a->path, b->path);
}

static void ExtDeletePopulate(HWND dlg)
{
    wchar_t input[1024];
    wchar_t exts[64][PROTECTED_EXT_CCH];
    wchar_t bad[128];
    wchar_t info[256];
    wchar_t size_text[64];
    int ext_count;
    int n;
    int i;
    uint64_t total_size = 0;
    HWND list = GetDlgItem(dlg, IDC_EXTDEL_LIST);
    int max_extent = 0;

    ExtDeleteFreeRecords();
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    SetDlgItemTextW(dlg, IDC_EXTDEL_INFO, L"请输入后缀后点击“查找匹配”。");
    EnableWindow(GetDlgItem(dlg, IDC_EXTDEL_DELETE), FALSE);

    GetDlgItemTextW(dlg, IDC_EXTDEL_EDIT, input, ARRAYSIZE(input));
    ext_count = ParseDeleteExtensions(input, exts, ARRAYSIZE(exts), bad, ARRAYSIZE(bad));
    if (ext_count < 0)
    {
        StringCchPrintfW(info, ARRAYSIZE(info), L"后缀格式不正确：%ls", bad);
        SetDlgItemTextW(dlg, IDC_EXTDEL_INFO, info);
        return;
    }
    if (ext_count == 0)
    {
        SetDlgItemTextW(dlg, IDC_EXTDEL_INFO, L"请输入至少一个后缀，例如 tmp;bak;old。");
        return;
    }
    for (i = 0; i < ext_count; i++)
    {
        if (!Path_ExtIsCleanupAllowed(exts[i]))
        {
            StringCchPrintfW(info, ARRAYSIZE(info),
                             L"后缀 .%ls 不在允许清理的六类格式中，不能按此删除。",
                             exts[i]);
            SetDlgItemTextW(dlg, IDC_EXTDEL_INFO, info);
            return;
        }
    }

    n = Data_Count();
    s_extdel_capacity = (n > 0) ? n : 1;
    s_extdel_records = (int *)Util_Alloc((size_t)s_extdel_capacity * sizeof(int));
    for (i = 0; i < n; i++)
    {
        FileRec *r = Data_Get(i);
        if (r == NULL || (r->flags & REC_FLAG_DELETED) != 0)
            continue;
        if ((r->flags & (REC_FLAG_PROTECTED | REC_FLAG_EXT_PROTECTED)) != 0)
            continue;
        if (!RecordIsCleanupAllowed(r) || RecordIsHardProtected(r))
            continue;
        if (!ExtDeleteMatches(r->path, exts, ext_count))
            continue;
        s_extdel_records[s_extdel_count++] = i;
    }

    if (s_extdel_count > 1)
        qsort(s_extdel_records, (size_t)s_extdel_count, sizeof(int), CompareExtDeleteRecords);

    for (i = 0; i < s_extdel_count; i++)
    {
        FileRec *r = Data_Get(s_extdel_records[i]);
        wchar_t line[2048];
        int width;
        if (r == NULL)
            continue;
        Size_Format(r->size, size_text, ARRAYSIZE(size_text));
        StringCchPrintfW(line, ARRAYSIZE(line), L"%ls   [%ls]   %ls",
                         r->name, size_text, r->path);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line);
        total_size += r->size;
        width = (int)wcslen(line) * 7;
        if (width > max_extent)
            max_extent = width;
    }
    SendMessageW(list, LB_SETHORIZONTALEXTENT, (WPARAM)max_extent, 0);

    Size_Format(total_size, size_text, ARRAYSIZE(size_text));
    StringCchPrintfW(info, ARRAYSIZE(info),
                     L"匹配 %d 个文件，合计 %ls。受保护区、后缀保护和系统路径不会列出。",
                     s_extdel_count, size_text);
    SetDlgItemTextW(dlg, IDC_EXTDEL_INFO, info);
    EnableWindow(GetDlgItem(dlg, IDC_EXTDEL_DELETE), s_extdel_count > 0);
}

static void ExtDeleteSelected(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_EXTDEL_LIST);
    int selected_count = (int)SendMessageW(list, LB_GETSELCOUNT, 0, 0);
    int *selected_items;
    int *record_indices;
    wchar_t text[1200];
    wchar_t size_text[64];
    uint64_t total_size = 0;
    int i;
    int deleted;

    if (selected_count <= 0)
    {
        MessageBoxW(dlg, L"请先选择文件，或点击“全选”后再执行删除。",
                    L"未选择文件", MB_OK | MB_ICONINFORMATION);
        return;
    }
    selected_items = (int *)Util_Alloc((size_t)selected_count * sizeof(int));
    record_indices = (int *)Util_Alloc((size_t)selected_count * sizeof(int));
    SendMessageW(list, LB_GETSELITEMS, (WPARAM)selected_count, (LPARAM)selected_items);
    for (i = 0; i < selected_count; i++)
    {
        int line_index = selected_items[i];
        if (line_index < 0 || line_index >= s_extdel_count)
        {
            record_indices[i] = -1;
            continue;
        }
        record_indices[i] = s_extdel_records[line_index];
        {
            FileRec *r = Data_Get(record_indices[i]);
            if (r != NULL)
                total_size += r->size;
        }
    }
    free(selected_items);

    Size_Format(total_size, size_text, ARRAYSIZE(size_text));
    StringCchPrintfW(text, ARRAYSIZE(text),
                     L"确定要删除选中的 %d 个文件吗？\r\n\r\n合计大小：%ls\r\n\r\n文件将被移入回收站。\r\n受保护文件和系统路径会被再次跳过。",
                     selected_count, size_text);
    if (!ManualDeleteConfirm(dlg, text))
    {
        free(record_indices);
        return;
    }

    deleted = DeleteIndexes(record_indices, selected_count, L"已按指定后缀删除选中文件。");
    free(record_indices);
    if (deleted > 0)
        EndDialog(dlg, IDOK);
    else
        MessageBoxW(dlg, L"没有文件被删除。它们可能已不存在或受到保护。",
                    APP_NAME, MB_OK | MB_ICONINFORMATION);
}

static INT_PTR CALLBACK ExtDeleteDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg)
    {
    case WM_INITDIALOG:
        SetWindowTextW(dlg, L"按后缀删除");
        SetDlgItemTextW(dlg, IDC_EXTDEL_INTRO,
                        L"输入一个或多个后缀，查找扫描结果中匹配的文件；支持多选和全选后一键删除。");
        SetDlgItemTextW(dlg, IDC_EXTDEL_SEARCH, L"查找匹配");
        SetDlgItemTextW(dlg, IDC_EXTDEL_HINT,
                        L"仅支持六类格式；多个后缀用分号、逗号或空格分隔，例如：bak;old。不需要输入点号。");
        SetDlgItemTextW(dlg, IDC_EXTDEL_SELECTALL, L"全选");
        SetDlgItemTextW(dlg, IDC_EXTDEL_DELETE, L"一键删除选中");
        SetDlgItemTextW(dlg, IDCANCEL, L"关闭");
        SetDlgItemTextW(dlg, IDC_EXTDEL_FOOT,
                        L"说明：只处理当前扫描结果，仅支持办公/音视频/压缩/网页/邮件/3D 六类格式；保护区、后缀保护和 C:\\Windows 等系统路径始终跳过。删除前需手动输入确认两次。");
        SetDlgItemFont(dlg, IDC_EXTDEL_INTRO, s_font_body);
        SetDlgItemFont(dlg, IDC_EXTDEL_EDIT, s_font_body);
        SetDlgItemFont(dlg, IDC_EXTDEL_SEARCH, s_font_button);
        SetDlgItemFont(dlg, IDC_EXTDEL_HINT, s_font_small);
        SetDlgItemFont(dlg, IDC_EXTDEL_LIST, s_font_body);
        SetDlgItemFont(dlg, IDC_EXTDEL_INFO, s_font_body);
        SetDlgItemFont(dlg, IDC_EXTDEL_SELECTALL, s_font_button);
        SetDlgItemFont(dlg, IDC_EXTDEL_DELETE, s_font_button);
        SetDlgItemFont(dlg, IDCANCEL, s_font_button);
        SetDlgItemFont(dlg, IDC_EXTDEL_FOOT, s_font_small);
        SendDlgItemMessageW(dlg, IDC_EXTDEL_EDIT, EM_SETLIMITTEXT, 512, 0);
        SendDlgItemMessageW(dlg, IDC_EXTDEL_EDIT, EM_SETCUEBANNER, TRUE, (LPARAM)L"例如 tmp;bak;old");
        SetDlgItemTextW(dlg, IDC_EXTDEL_INFO, L"请输入后缀后点击“查找匹配”。");
        EnableWindow(GetDlgItem(dlg, IDC_EXTDEL_DELETE), FALSE);
        CenterWindow(dlg, g_main_window);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_EXTDEL_SEARCH:
            ExtDeletePopulate(dlg);
            return TRUE;
        case IDC_EXTDEL_SELECTALL:
            SendDlgItemMessageW(dlg, IDC_EXTDEL_LIST, LB_SETSEL, TRUE, (LPARAM)-1);
            EnableWindow(GetDlgItem(dlg, IDC_EXTDEL_DELETE), s_extdel_count > 0);
            SetFocus(GetDlgItem(dlg, IDC_EXTDEL_LIST));
            return TRUE;
        case IDC_EXTDEL_DELETE:
            ExtDeleteSelected(dlg);
            return TRUE;
        case IDC_EXTDEL_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE)
            {
                int selected = (int)SendDlgItemMessageW(dlg, IDC_EXTDEL_LIST, LB_GETSELCOUNT, 0, 0);
                EnableWindow(GetDlgItem(dlg, IDC_EXTDEL_DELETE), selected > 0);
            }
            return TRUE;
        case IDCANCEL:
            ExtDeleteFreeRecords();
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        ExtDeleteFreeRecords();
        return TRUE;
    }
    return FALSE;
}

void UI_ShowExtDeleteDialog(HWND owner)
{
    if (Scan_IsRunning())
    {
        Util_Info(L"扫描进行中，暂时不能按后缀删除。\r\n请先停止扫描。", APP_NAME);
        return;
    }
    if (Data_Count() == 0)
    {
        Util_Info(L"当前没有扫描结果。请先执行快速扫描或全盘扫描。", APP_NAME);
        return;
    }
    DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_EXT_DELETE), owner, ExtDeleteDlgProc, 0);
    List_Rebuild(TRUE);
    List_DataChanged();
    InvalidateRect(g_main_window, NULL, FALSE);
}

/* ------------------------------------------------------------------ */
/* supported-format help                                               */
/* ------------------------------------------------------------------ */

static void BuildSupportedFormatsText(wchar_t *out, size_t cch)
{
    int category;
    StringCchCopyW(out, cch,
                   L"本程序仅支持清理以下六大类格式。\r\n"
                   L"扫描结果中的其他格式仍会显示，但标记为“不可清理”，无法删除。\r\n\r\n");
    for (category = 0; category < Cleanup_FormatCategoryCount(); category++)
    {
        int count = 0;
        int i;
        const wchar_t *const *exts = Cleanup_FormatCategoryExtensions(category, &count);
        wchar_t head[128];
        StringCchPrintfW(head, ARRAYSIZE(head), L"【%d. %ls】\r\n",
                         category + 1, Cleanup_FormatCategoryName(category));
        StringCchCatW(out, cch, head);
        for (i = 0; i < count; i++)
        {
            wchar_t item[64];
            StringCchPrintfW(item, ARRAYSIZE(item), L".%ls ", exts[i]);
            StringCchCatW(out, cch, item);
        }
        StringCchCatW(out, cch, L"\r\n\r\n");
    }
    StringCchCatW(out, cch,
                  L"注意：\r\n"
                  L"- 文件夹保护区、后缀保护区、C:\\Windows 和系统保护文件始终跳过。\r\n"
                  L"- 部分允许格式（例如 json/xml/js）默认在后缀保护中，需先在“后缀保护管理”中移除后才能删除。\r\n"
                  L"- 每次删除都需要手动输入两次“确认删除”，Ctrl+V 和右键粘贴被禁用。\r\n");
}

static INT_PTR CALLBACK SupportFormatsDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        static wchar_t text[16384];
        SetWindowTextW(dlg, L"支持的清理格式");
        SetDlgItemTextW(dlg, IDC_SUPPORT_INTRO,
                        L"清理仅支持以下六大类格式；其他格式会扫描显示但不可删除。");
        BuildSupportedFormatsText(text, ARRAYSIZE(text));
        SetDlgItemTextW(dlg, IDC_SUPPORT_TEXT, text);
        SetDlgItemTextW(dlg, IDCANCEL, L"关闭");
        SetDlgItemFont(dlg, IDC_SUPPORT_INTRO, s_font_body);
        SetDlgItemFont(dlg, IDC_SUPPORT_TEXT, s_font_body);
        SetDlgItemFont(dlg, IDCANCEL, s_font_button);
        SendDlgItemMessageW(dlg, IDC_SUPPORT_TEXT, EM_SETSEL, 0, 0);
        SetFocus(GetDlgItem(dlg, IDCANCEL));
        CenterWindow(dlg, g_main_window);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL)
        {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void UI_ShowSupportedFormatsDialog(HWND owner)
{
    DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_SUPPORT_FORMATS), owner,
                    SupportFormatsDlgProc, 0);
}

/* ------------------------------------------------------------------ */
/* manual double confirmation (paste disabled)                         */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK NoPasteEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                        UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    switch (msg)
    {
    case WM_PASTE:
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
            (wp == L'V' || wp == L'v'))
            return 0;
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && wp == VK_INSERT)
            return 0;
        break;
    case WM_CHAR:
        if (wp == 0x16) /* Ctrl+V */
            return 0;
        break;
    case WM_CONTEXTMENU:
        return 0;
    case WM_MBUTTONDOWN:
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, NoPasteEditProc, id);
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static BOOL ManualFieldMatches(HWND edit)
{
    wchar_t text[64];
    wchar_t *start;
    wchar_t *end;
    GetWindowTextW(edit, text, ARRAYSIZE(text));
    start = text;
    while (*start == L' ' || *start == L'\t' || *start == L'\r' || *start == L'\n')
        start++;
    end = start + wcslen(start);
    while (end > start && (end[-1] == L' ' || end[-1] == L'\t' ||
                           end[-1] == L'\r' || end[-1] == L'\n'))
        end--;
    *end = 0;
    return wcscmp(start, L"确认删除") == 0;
}

static void ManualUpdateState(HWND dlg)
{
    BOOL ok = ManualFieldMatches(GetDlgItem(dlg, IDC_MANUAL_EDIT1)) &&
              ManualFieldMatches(GetDlgItem(dlg, IDC_MANUAL_EDIT2));
    EnableWindow(GetDlgItem(dlg, IDOK), ok);
    SetDlgItemTextW(dlg, IDC_MANUAL_HINT,
                    ok ? L"已手动输入两次，可以确认删除。"
                       : L"必须手动输入两次「确认删除」；Ctrl+V、右键和中键粘贴已禁用。");
}

static INT_PTR CALLBACK ManualDeleteDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        SetWindowTextW(dlg, L"手动确认删除");
        SetDlgItemTextW(dlg, IDC_MANUAL_INTRO,
                        L"这是危险操作。请手动输入两次「确认删除」。\r\n粘贴功能已被禁用。");
        SetDlgItemTextW(dlg, IDC_MANUAL_LABEL1, L"第一次输入「确认删除」：");
        SetDlgItemTextW(dlg, IDC_MANUAL_LABEL2, L"第二次输入「确认删除」：");
        SetDlgItemTextW(dlg, IDOK, L"确认删除");
        SetDlgItemTextW(dlg, IDCANCEL, L"取消");
        SetDlgItemTextW(dlg, IDC_MANUAL_SUMMARY,
                        (lp != 0) ? (const wchar_t *)lp : L"将删除选中的文件。");
        SetDlgItemTextW(dlg, IDC_MANUAL_HINT,
                        L"必须手动输入两次「确认删除」；Ctrl+V、右键和中键粘贴已禁用。");
        SetDlgItemFont(dlg, IDC_MANUAL_INTRO, s_font_body);
        SetDlgItemFont(dlg, IDC_MANUAL_SUMMARY, s_font_body);
        SetDlgItemFont(dlg, IDC_MANUAL_LABEL1, s_font_body);
        SetDlgItemFont(dlg, IDC_MANUAL_LABEL2, s_font_body);
        SetDlgItemFont(dlg, IDC_MANUAL_EDIT1, s_font_body);
        SetDlgItemFont(dlg, IDC_MANUAL_EDIT2, s_font_body);
        SetDlgItemFont(dlg, IDC_MANUAL_HINT, s_font_small);
        SetDlgItemFont(dlg, IDOK, s_font_button);
        SetDlgItemFont(dlg, IDCANCEL, s_font_button);
        SendDlgItemMessageW(dlg, IDC_MANUAL_EDIT1, EM_SETLIMITTEXT, 31, 0);
        SendDlgItemMessageW(dlg, IDC_MANUAL_EDIT2, EM_SETLIMITTEXT, 31, 0);
        SendDlgItemMessageW(dlg, IDC_MANUAL_EDIT1, EM_SETCUEBANNER, TRUE,
                            (LPARAM)L"手动输入：确认删除");
        SendDlgItemMessageW(dlg, IDC_MANUAL_EDIT2, EM_SETCUEBANNER, TRUE,
                            (LPARAM)L"再次手动输入：确认删除");
        SetWindowSubclass(GetDlgItem(dlg, IDC_MANUAL_EDIT1), NoPasteEditProc, 1, 0);
        SetWindowSubclass(GetDlgItem(dlg, IDC_MANUAL_EDIT2), NoPasteEditProc, 2, 0);
        EnableWindow(GetDlgItem(dlg, IDOK), FALSE);
        CenterWindow(dlg, g_main_window);
        SetFocus(GetDlgItem(dlg, IDC_MANUAL_EDIT1));
        return FALSE; /* focus explicitly set above */

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_MANUAL_EDIT1:
        case IDC_MANUAL_EDIT2:
            if (HIWORD(wp) == EN_CHANGE)
            {
                ManualUpdateState(dlg);
                return TRUE;
            }
            break;
        case IDOK:
            if (ManualFieldMatches(GetDlgItem(dlg, IDC_MANUAL_EDIT1)) &&
                ManualFieldMatches(GetDlgItem(dlg, IDC_MANUAL_EDIT2)))
            {
                EndDialog(dlg, IDOK);
            }
            else
            {
                MessageBoxW(dlg, L"两次输入都必须是「确认删除」，且不能使用粘贴。",
                            L"确认失败", MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static BOOL ManualDeleteConfirm(HWND owner, const wchar_t *summary)
{
    return DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_MANUAL_DELETE), owner,
                           ManualDeleteDlgProc, (LPARAM)summary) == IDOK;
}

/* ------------------------------------------------------------------ */
/* scan-mode dialog and folder picker                                  */
/* ------------------------------------------------------------------ */

static INT_PTR CALLBACK ScanModeDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg)
    {
    case WM_INITDIALOG:
        SetWindowTextW(dlg, L"选择扫描方式");
        SetDlgItemTextW(dlg, IDC_SCAN_INTRO,
                        L"没有现成结果可用。请选择一种方式查找文件：");
        SetDlgItemTextW(dlg, IDC_RADIO_QUICK, L"快速扫描（可能有遗漏）");
        SetDlgItemTextW(dlg, IDC_QUICK_DESC,
                        L"基于我们的扫描统计，只扫描文件数量最多的若干目录，速度最快，但可能漏掉其他目录。");
        SetDlgItemTextW(dlg, IDC_RADIO_FULL, L"全盘扫描");
        SetDlgItemTextW(dlg, IDC_FULL_DESC,
                        L"自动遍历所有本地磁盘。最完整，但文件很多时耗时较长，可随时停止。");
        SetDlgItemTextW(dlg, IDC_RADIO_DIR, L"选择指定目录");
        SetDlgItemTextW(dlg, IDC_DIR_DESC,
                        L"只扫描你指定的一个文件夹，适合只清理某个位置。");
        SetDlgItemTextW(dlg, IDOK, L"开始扫描");
        SetDlgItemTextW(dlg, IDCANCEL, L"暂不扫描");
        CheckRadioButton(dlg, IDC_RADIO_QUICK, IDC_RADIO_DIR, IDC_RADIO_QUICK);
        CenterWindow(dlg, g_main_window);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDOK:
            if (IsDlgButtonChecked(dlg, IDC_RADIO_FULL) == BST_CHECKED)
                s_scan_choice = SCAN_MODE_FULL;
            else if (IsDlgButtonChecked(dlg, IDC_RADIO_DIR) == BST_CHECKED)
                s_scan_choice = SCAN_MODE_DIRECTORY;
            else
                s_scan_choice = SCAN_MODE_QUICK;
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            s_scan_choice = -1;
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int UI_AskScanMode(HWND owner)
{
    s_scan_choice = -1;
    if (DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_SCAN_MODE), owner,
                        ScanModeDlgProc, 0) != IDOK)
        return -1;
    return s_scan_choice;
}

wchar_t *UI_PickFolder(HWND owner, const wchar_t *title)
{
    BROWSEINFOW bi;
    PIDLIST_ABSOLUTE pidl;
    wchar_t path[MAX_PATH];
    wchar_t *result = NULL;

    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    pidl = SHBrowseForFolderW(&bi);
    if (pidl == NULL)
        return NULL;
    path[0] = 0;
    if (SHGetPathFromIDListW(pidl, path) && path[0] != 0)
    {
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
            result = Path_NormalizeDirDup(path);
    }
    CoTaskMemFree(pidl);
    return result;
}

/* ------------------------------------------------------------------ */
/* scanning / controls                                                 */
/* ------------------------------------------------------------------ */

static void UpdateControls(void)
{
    BOOL scanning = Scan_IsRunning();
    ScanStats st;
    Data_GetStats(&st);
    EnableWindow(s_btn_quick, !scanning);
    EnableWindow(s_btn_full, !scanning);
    EnableWindow(s_btn_dir, !scanning);
    EnableWindow(s_btn_stop, scanning);
    EnableWindow(s_btn_batch, !scanning && st.clean_count > 0);
    EnableWindow(s_search, TRUE);
}

static BOOL ConfirmQuickRootsAndAsk(HWND owner, wchar_t **roots, int count)
{
    wchar_t text[2048];
    size_t used;
    int i;

    StringCchPrintfW(text, ARRAYSIZE(text),
                     L"快速扫描只扫描以下 %d 个目录（按文件数量统计选取）：\r\n\r\n", count);
    used = wcslen(text);
    for (i = 0; i < count && i < 8; i++)
    {
        wchar_t line[512];
        StringCchPrintfW(line, ARRAYSIZE(line), L"%d. %ls\r\n", i + 1, roots[i]);
        if (used + wcslen(line) + 120 >= ARRAYSIZE(text))
            break;
        StringCchCatW(text, ARRAYSIZE(text), line);
        used += wcslen(line);
    }
    if (count > 8)
        StringCchCatW(text, ARRAYSIZE(text), L"…\r\n");
    StringCchCatW(text, ARRAYSIZE(text), L"\r\n快速扫描可能有遗漏。是否开始扫描？");
    return MessageBoxW(owner, text, L"快速扫描",
                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1) == IDYES;
}

static void RecoverData(void)
{
    wchar_t err[APP_ERR_CCH];
    if (!Store_DataExists())
        return;
    if (Store_Load(err, ARRAYSIZE(err)) == 0)
    {
        s_using_previous = TRUE;
        List_Rebuild(TRUE);
        List_ScrollToTop();
        StringCchCopyW(s_notice, ARRAYSIZE(s_notice), L"已恢复上一次保存的扫描数据。");
    }
    InvalidateRect(g_main_window, NULL, FALSE);
}

void UI_StartScan(ScanMode mode, const wchar_t *directory)
{
    wchar_t **roots = NULL;
    int count = 0;
    wchar_t err[APP_ERR_CCH];
    int rc;

    if (Scan_IsRunning())
        return;

    if (mode == SCAN_MODE_FULL)
    {
        if (Scan_CollectDriveRoots(&roots, &count, err, ARRAYSIZE(err)) != 0)
        {
            Util_ErrorShowf(L"%ls", err);
            return;
        }
    }
    else if (mode == SCAN_MODE_QUICK)
    {
        if (g_quick_root_count == 0)
            Scan_ComputeQuickRoots();
        if (Scan_CollectQuickRoots(&roots, &count, err, ARRAYSIZE(err)) != 0)
        {
            Util_ErrorShowf(L"%ls", err);
            return;
        }
        if (!ConfirmQuickRootsAndAsk(g_main_window, roots, count))
        {
            Scan_FreeRoots(roots, count);
            return;
        }
    }
    else
    {
        wchar_t *dir_copy;
        if (directory == NULL || directory[0] == 0)
            return;
        dir_copy = Path_NormalizeDirDup(directory);
        roots = (wchar_t **)Util_Alloc(sizeof(wchar_t *));
        roots[0] = dir_copy;
        count = 1;
    }

    Data_Reset();
    List_Reset();
    s_using_previous = FALSE;
    s_notice[0] = 0;
    InvalidateRect(g_main_window, NULL, FALSE);

    rc = Scan_Start(mode, roots, count, err, ARRAYSIZE(err));
    Scan_FreeRoots(roots, count);
    if (rc != 0)
    {
        Util_ErrorShowf(L"%ls", err);
        RecoverData();
        UpdateControls();
        return;
    }

    UpdateControls();
    InvalidateRect(g_main_window, NULL, FALSE);
}

void UI_ClearListAndRecords(void)
{
    Data_Reset();
    List_Reset();
    s_using_previous = FALSE;
    s_notice[0] = 0;
    if (g_main_window != NULL)
        InvalidateRect(g_main_window, NULL, FALSE);
}

void UI_RefreshAll(void)
{
    List_Rebuild(TRUE);
    List_DataChanged();
    UpdateControls();
    if (g_main_window != NULL)
        InvalidateRect(g_main_window, NULL, FALSE);
}

BOOL UI_IsUsingPreviousData(void)
{
    return s_using_previous;
}

void UI_SetUsingPreviousData(BOOL value)
{
    s_using_previous = value;
    if (g_main_window != NULL)
        InvalidateRect(g_main_window, NULL, FALSE);
}

/* ------------------------------------------------------------------ */
/* main window                                                         */
/* ------------------------------------------------------------------ */

static void SetSearchCue(void)
{
    if (s_search != NULL)
        SendMessageW(s_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索文件名或路径…");
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        g_main_window = hwnd;
        CreateUiFonts();
        s_brush_white = CreateSolidBrush(C_WHITE);
        s_brush_bg = CreateSolidBrush(C_BG);
        if (!List_RegisterClass(g_hinst))
        {
            Util_ErrorShowf(L"无法注册文件列表窗口类（错误码 %lu）。", (unsigned long)GetLastError());
            return -1;
        }
        s_btn_quick = CreateWindowExW(0, L"BUTTON", L"快速扫描",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_BTN_QUICK, g_hinst, NULL);
        s_btn_full = CreateWindowExW(0, L"BUTTON", L"全盘扫描",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_BTN_FULL, g_hinst, NULL);
        s_btn_dir = CreateWindowExW(0, L"BUTTON", L"选择目录",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                    0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_BTN_DIR, g_hinst, NULL);
        s_btn_stop = CreateWindowExW(0, L"BUTTON", L"停止扫描",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_BTN_STOP, g_hinst, NULL);
        s_btn_batch = CreateWindowExW(0, L"BUTTON", L"删除一周前办公文件",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_BTN_BATCH, g_hinst, NULL);
        s_search = CreateWindowExW(0, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT,
                                   0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_EDIT_SEARCH, g_hinst, NULL);
        s_list = List_Create(hwnd);
        if (s_btn_quick == NULL || s_btn_full == NULL || s_btn_dir == NULL ||
            s_btn_stop == NULL || s_btn_batch == NULL || s_search == NULL || s_list == NULL)
        {
            Util_ErrorShowf(L"创建界面控件失败（错误码 %lu）。", (unsigned long)GetLastError());
            return -1;
        }
        InstallButton(s_btn_quick, ID_BTN_QUICK);
        InstallButton(s_btn_full, ID_BTN_FULL);
        InstallButton(s_btn_dir, ID_BTN_DIR);
        InstallButton(s_btn_stop, ID_BTN_STOP);
        InstallButton(s_btn_batch, ID_BTN_BATCH);
        SetWindowTheme(s_btn_quick, L"Explorer", NULL);
        SetWindowTheme(s_btn_full, L"Explorer", NULL);
        SetWindowTheme(s_btn_dir, L"Explorer", NULL);
        SetWindowTheme(s_btn_stop, L"Explorer", NULL);
        SetWindowTheme(s_btn_batch, L"Explorer", NULL);
        SetWindowTheme(s_search, L"Explorer", NULL);
        ApplyFonts();
        SetSearchCue();
        LayoutControls(hwnd);
        UpdateControls();
        SetTimer(hwnd, TIMER_UI, 250, NULL);
        return 0;

    case WM_PAINT:
        PaintMain(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            LayoutControls(hwnd);
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = S(1120);
        mmi->ptMinTrackSize.y = S(700);
        return 0;
    }

    case WM_DPICHANGED:
    {
        RECT *suggested = (RECT *)lp;
        int new_dpi = HIWORD(wp);
        if (new_dpi <= 0)
            new_dpi = 96;
        g_dpi = new_dpi;
        g_dpi_scale_100 = MulDiv(g_dpi, 100, 96);
        CreateUiFonts();
        ApplyFonts();
        List_OnDpiChanged();
        SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutControls(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lp;
        DrawOwnerButton(dis);
        return TRUE;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wp;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_WHITE);
        return (LRESULT)s_brush_white;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        if (id == ID_EDIT_SEARCH && HIWORD(wp) == EN_CHANGE)
        {
            KillTimer(hwnd, TIMER_SEARCH);
            SetTimer(hwnd, TIMER_SEARCH, 300, NULL);
            return 0;
        }
        switch (id)
        {
        case ID_BTN_QUICK:
            UI_StartScan(SCAN_MODE_QUICK, NULL);
            return 0;
        case ID_BTN_FULL:
            UI_StartScan(SCAN_MODE_FULL, NULL);
            return 0;
        case ID_BTN_DIR:
        {
            wchar_t *dir = UI_PickFolder(hwnd, L"选择要扫描的文件夹");
            if (dir != NULL)
            {
                UI_StartScan(SCAN_MODE_DIRECTORY, dir);
                free(dir);
            }
            return 0;
        }
        case ID_BTN_STOP:
            if (Scan_IsRunning())
            {
                Scan_RequestStop();
                StringCchCopyW(s_notice, ARRAYSIZE(s_notice), L"正在停止扫描，已发现的结果稍后会保存…");
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case ID_BTN_BATCH:
            DeleteOldOfficeFiles(hwnd);
            return 0;
        }
        break;
    }

    case WM_TIMER:
        if (wp == TIMER_SEARCH)
        {
            wchar_t search[256];
            KillTimer(hwnd, TIMER_SEARCH);
            GetWindowTextW(s_search, search, ARRAYSIZE(search));
            List_SetSearch(search);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wp == TIMER_UI)
        {
            if (Scan_IsRunning())
            {
                List_AppendNew();
                UpdateControls();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else
            {
                UpdateControls();
            }
            return 0;
        }
        break;

    case WM_APP_SCAN_DONE:
    {
        UINT result = (UINT)wp;
        if (g_scan_thread != NULL)
        {
            WaitForSingleObject(g_scan_thread, INFINITE);
            CloseHandle(g_scan_thread);
            g_scan_thread = NULL;
        }
        if (result == 2)
        {
            Util_ErrorShowf(L"%ls", g_scan_error[0] ? g_scan_error : L"扫描过程中发生未知错误。");
            if (Data_Count() == 0)
                RecoverData();
            else
                List_Rebuild(TRUE);
        }
        else
        {
            List_Rebuild(TRUE);
            List_ScrollToTop();
            s_using_previous = FALSE;
            if (result == 1)
                StringCchCopyW(s_notice, ARRAYSIZE(s_notice),
                               L"扫描已停止，当前找到的结果已保存。");
            else
                StringCchCopyW(s_notice, ARRAYSIZE(s_notice),
                               L"扫描完成，结果已保存。");
        }
        UpdateControls();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int i;
        int hover = -1;
        BOOL protect_hover = PtInRect(&s_protect_rect, pt);
        BOOL extdel_hover = PtInRect(&s_extdel_rect, pt);
        BOOL support_hover = PtInRect(&s_support_rect, pt);
        for (i = 0; i < 4; i++)
        {
            if (PtInRect(&s_nav_rects[i], pt))
            {
                hover = i;
                break;
            }
        }
        if (hover != s_nav_hover || protect_hover != s_protect_hover ||
            extdel_hover != s_extdel_hover || support_hover != s_support_hover)
        {
            s_nav_hover = hover;
            s_protect_hover = protect_hover;
            s_extdel_hover = extdel_hover;
            s_support_hover = support_hover;
            InvalidateRect(hwnd, NULL, FALSE);
        }
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
        return 0;
    }

    case WM_MOUSELEAVE:
        s_tracking_leave = FALSE;
        if (s_nav_hover != -1 || s_protect_hover || s_extdel_hover || s_support_hover)
        {
            s_nav_hover = -1;
            s_protect_hover = FALSE;
            s_extdel_hover = FALSE;
            s_support_hover = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT)
        {
            POINT pt;
            int i;
            BOOL hand = FALSE;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            for (i = 0; i < 4; i++)
            {
                if (PtInRect(&s_nav_rects[i], pt))
                {
                    hand = TRUE;
                    break;
                }
            }
            if (PtInRect(&s_protect_rect, pt) || PtInRect(&s_extdel_rect, pt) ||
                PtInRect(&s_support_rect, pt))
                hand = TRUE;
            if (hand)
            {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN:
    {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int i;
        for (i = 0; i < 4; i++)
        {
            if (PtInRect(&s_nav_rects[i], pt))
            {
                wchar_t search[256];
                s_nav = (NavFilter)i;
                GetWindowTextW(s_search, search, ARRAYSIZE(search));
                List_SetFilter(s_nav, search);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        if (PtInRect(&s_extdel_rect, pt))
        {
            UI_ShowExtDeleteDialog(hwnd);
            return 0;
        }
        if (PtInRect(&s_support_rect, pt))
        {
            UI_ShowSupportedFormatsDialog(hwnd);
            return 0;
        }
        if (PtInRect(&s_protect_rect, pt))
        {
            if (s_nav == NAV_EXT_PROTECTED)
                UI_ShowExtProtectDialog(hwnd);
            else
                UI_ShowProtectDialog(hwnd);
            return 0;
        }
        return 0;
    }

    case WM_CLOSE:
        if (Scan_IsRunning())
        {
            if (MessageBoxW(hwnd, L"扫描正在进行中。\r\n\r\n是否停止扫描并退出？",
                            APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                return 0;
            Scan_RequestStop();
            SetCursor(LoadCursorW(NULL, IDC_WAIT));
            if (g_scan_thread != NULL)
            {
                WaitForSingleObject(g_scan_thread, INFINITE);
                CloseHandle(g_scan_thread);
                g_scan_thread = NULL;
            }
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
            InterlockedExchange(&g_scan_running, 0);
        }
        if (g_data_dirty)
        {
            wchar_t err[APP_ERR_CCH];
            if (Store_Save(err, ARRAYSIZE(err)) != 0)
            {
                Util_ErrorShowf(L"%ls", err[0] ? err : L"保存数据失败。");
                if (MessageBoxW(hwnd, L"数据保存失败，是否仍然退出？", APP_NAME,
                                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                    return 0;
            }
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
    {
        HFONT *fonts[] = {
            &s_font_title, &s_font_page, &s_font_subtitle,
            &s_font_card_label, &s_font_card_value, &s_font_card_sub,
            &s_font_nav, &s_font_nav_value, &s_font_body,
            &s_font_button, &s_font_small
        };
        size_t i;
        KillTimer(hwnd, TIMER_UI);
        KillTimer(hwnd, TIMER_SEARCH);
        for (i = 0; i < ARRAYSIZE(fonts); i++)
        {
            if (*fonts[i] != NULL)
            {
                DeleteObject(*fonts[i]);
                *fonts[i] = NULL;
            }
        }
        if (s_brush_white != NULL) { DeleteObject(s_brush_white); s_brush_white = NULL; }
        if (s_brush_bg != NULL) { DeleteObject(s_brush_bg); s_brush_bg = NULL; }
        if (s_icon_sidebar != NULL) { DestroyIcon(s_icon_sidebar); s_icon_sidebar = NULL; }
        if (s_icon_small != NULL) { DestroyIcon(s_icon_small); s_icon_small = NULL; }
        g_main_window = NULL;
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL UI_CreateMainWindow(HINSTANCE hinst)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    RECT work;
    int width;
    int height;
    int x;
    int y;

    s_icon_small = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                     GetSystemMetrics(SM_CXSMICON),
                                     GetSystemMetrics(SM_CYSMICON), 0);
    s_icon_sidebar = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                       S(36), S(36), 0);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = s_icon_small;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"XzqFileCleanerMain";
    if (!RegisterClassExW(&wc))
    {
        Util_ErrorShowf(L"无法注册主窗口类（错误码 %lu）。", (unsigned long)GetLastError());
        return FALSE;
    }
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    width = S(1280);
    height = S(820);
    if (width > work.right - work.left - S(40))
        width = work.right - work.left - S(40);
    if (height > work.bottom - work.top - S(40))
        height = work.bottom - work.top - S(40);
    if (width < S(1120))
        width = S(1120);
    if (height < S(700))
        height = S(700);
    x = work.left + (work.right - work.left - width) / 2;
    y = work.top + (work.bottom - work.top - height) / 2;

    hwnd = CreateWindowExW(0, L"XzqFileCleanerMain", L"文件清理",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                           x, y, width, height, NULL, NULL, hinst, NULL);
    if (hwnd == NULL)
    {
        Util_ErrorShowf(L"创建主窗口失败（错误码 %lu）。", (unsigned long)GetLastError());
        return FALSE;
    }
    g_main_window = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    return TRUE;
}
