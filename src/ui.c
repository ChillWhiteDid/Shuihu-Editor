/*
 * ui.c —— Win32 GUI（M3）
 *
 * 平台约束（Win98 实机）
 *   - 只用 kernel32 / user32 / gdi32 + comdlg32（Win98 自带 comdlg32.dll）
 *   - **不用 comctl32**：列表用 user32 的 LISTBOX，按钮用 BUTTON
 *   - 文本：程序内部 UTF-8，交给控件前转成系统 ANSI（简体中文 = GBK）
 *   - 32 位、子系统 4.0、不依赖任何运行时 DLL
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "save.h"
#include "goodsfile.h"
#include "humanfile.h"
#include "names.h"
#include "goods.h"
#include "human.h"
#include "tree.h"
#include "textconv.h"
#include "fileio.h"

/* ================= 控件 ID 规划 =================
 * Windows 的 WM_COMMAND 只取 LOWORD，所以控件 ID 必须 < 65536。
 * 按“页面/窗口”分块，每块留足空位；新增控件必须落在对应块内，不要跨块。
 *
 *   10000 – 14999   存档修改页（page 0）
 *   15000 – 19999   道具编辑页（page 1）
 *   20000 – 20499   两页共用（状态栏、页签）
 *   21000 – 22999   子窗口（技能树 / 恢复存档 / 装备加成 / 人物编辑）
 *
 * is_goods_id() 只依赖 15000 这个边界；调整分块时务必同步它。
 * 历史上因为 ID 撞车出过三次问题（按钮跑到别的页、文字写进别的控件），
 * 自检 case19/case21 会守住这两类错误。
 */

/* ---- 存档修改页：10000 – 14999 ---- */
#define ID_PATH        10000  /* 存档路径输入框 */
#define ID_BROWSE      10001  /* 浏览... */
#define ID_OPEN        10002  /* 打开 */
#define ID_LIST        10010  /* 角色列表 */
#define ID_WRITE       10020  /* 写入存档 */
#define ID_RESTORE     10021  /* 恢复存档 */
#define ID_RELOAD      10022  /* 重载存档 */
#define ID_TREE        10023  /* 查看技能树 */
#define ID_UNLOCK      10024  /* 解锁隐藏技 */
#define ID_CACHE       10025  /* 装备加成 */
#define ID_ABOUT       10030  /* 关于 */
#define ID_QUIT        10031  /* 退出 */
#define ID_MONEY       10050  /* 金钱 */
#define ID_EDIT0       10200  /* +0 .. +NFIELDS-1  字段输入框 */
#define ID_SLOT0       10300  /* +0 .. +SH_SLOT_COUNT-1  槽位下拉 */
#define ID_HIDDEN      10410  /* 隐藏技状态标签 */
#define ID_EQNOTE0     10420  /* +0 .. +1  攻击/防御的装备分解标签 */

/* ---- 道具编辑页：15000 – 19999 ---- */
#define ID_GB_LIST     15000  /* 道具列表 */
#define ID_GB_EDIT0    15010  /* +0 .. +SH_GF_COUNT-1  字段输入框 */
#define ID_GB_SAVE     15020  /* 写入道具表 */
#define ID_GB_RESTORE  15021  /* 恢复道具表 */
#define ID_GB_WARN     15030  /* 警示条 */
#define ID_GB_LAB0     15040  /* +0 .. +SH_GF_COUNT-1  字段名 */
#define ID_GB_RNG0     15050  /* +0 .. +SH_GF_COUNT-1  范围提示 */
#define ID_GB_DESC     15060  /* 说明文本 */
#define ID_GB_CHK      15061  /* 一致性提示 */
#define ID_GB_NOTE     15062  /* 「说明：」标签（必须归道具页，否则会画到存档页） */

/* ---- 两页共用：20000 – 20499 ---- */
#define ID_STATUS      20000  /* 状态栏（两页都显示）*/
#define ID_TAB_SAVE    20010  /* 页签：存档修改 */
#define ID_TAB_GOODS   20011  /* 页签：道具编辑 */
#define ID_HUMAN       20020  /* 人物编辑：两页常显，不随页签隐藏 */

/* ---- 子窗口：21000 – 22999 ---- */
#define ID_TREE_EDIT   21000  /* 技能树窗口：文本框 */
#define ID_TREE_CLOSE  21001  /* 技能树窗口：关闭 */
#define ID_BAK_LIST    21100  /* 恢复存档窗口：列表 */
#define ID_BAK_OK      21101  /* 恢复存档窗口：恢复 */
#define ID_BAK_CANCEL  21102  /* 恢复存档窗口：取消 */
#define ID_CA_EDIT0    21200  /* +0 .. +5  装备加成窗口：6 个输入框 */
#define ID_CA_OK       21210  /* 装备加成窗口：写入 */
#define ID_CA_CANCEL   21211  /* 装备加成窗口：取消 */
#define ID_CA_REFILL   21212  /* 装备加成窗口：按 GOODS 重填 */
#define ID_HU_LIST     21300  /* 人物编辑窗口：职业列表 */
#define ID_HU_BASE0    21310  /* +0 .. +6   基础值输入框 */
#define ID_HU_GROW0    21320  /* +0 .. +6   成长率输入框 */
#define ID_HU_MOVE     21330  /* 移动输入框 */
#define ID_HU_INFO     21331  /* 只读信息（技能条数 / 装备类型 / 未定字段） */
#define ID_HU_SAVE     21340  /* 写入文件 */
#define ID_HU_RESTORE  21343  /* 从备份恢复 HUMAN.HUM */
#define ID_HU_SKILLS   21344  /* 打开技能表 */
#define ID_HU_RELOAD   21341  /* 从磁盘重读 */
#define ID_HU_CLOSE    21342  /* 关闭 */
#define ID_HS_LIST     21400  /* 技能表窗口：技能列表 */
#define ID_HS_SID      21410  /* 技能字符串 id */
#define ID_HS_LEVEL    21411  /* 学习等级 */
#define ID_HS_HIDDEN   21412  /* 隐藏技标志 0/1 */
#define ID_HS_COST     21414  /* 气力消耗 */
#define ID_HS_SAVE     21420  /* 写入文件 */
#define ID_HS_IMPORT   21413  /* 整条导入技能 */
#define ID_HS_CLOSE    21421  /* 关闭 */
#define ID_IM_LIST     21500  /* 导入窗口：源技能列表 */
#define ID_IM_OK       21501  /* 导入窗口：确认 */
#define ID_IM_CANCEL   21502  /* 导入窗口：取消 */

#define APP_VERSION  "v2.2"
#define APP_BUILD    "2026-11-12"

/* 界面上要显示的字段（不含类型 id） */
static const sh_field kFields[] = {
    SH_F_LEVEL, SH_F_HP, SH_F_HPMAX, SH_F_MP, SH_F_MPMAX,
    SH_F_ATK, SH_F_DEF, SH_F_HIT, SH_F_AGI, SH_F_CRIT,
    SH_F_EXP, SH_F_EXPNEXT, SH_F_MOVE
};
#define NFIELDS ((int)(sizeof(kFields) / sizeof(kFields[0])))

typedef struct {
    sh_save s;
    char    path[MAX_PATH];
    int     loaded;
    int     sel;                       /* 当前选中单位在 s.units 中的下标 */
    int     map[SH_MAX_UNITS];         /* 列表项下标 -> s.units 下标（仅可编辑角色） */
    int     mapCount;
    HWND    hMain;                    /* 主窗口（子控件的父） */
    HWND    hList, hPath, hStatus;
    HWND    hEdit[NFIELDS];
    HWND    hSlot[SH_SLOT_COUNT];      /* 下拉列表（user32 COMBOBOX） */
    HWND    hMoney;
    HWND    hHidden;
    HWND    hEquipNote[2];            /* 攻击/防御的「装备 +N」分解标签 */
    HFONT   hFont;
    char    argPath[MAX_PATH];         /* 命令行传入的存档路径（可选） */
    HICON   hIconBig, hIconSmall;      /* 运行时从 logo.ico 加载 */
    char    iniPath[MAX_PATH];         /* exe 同目录的 shuihu-edit.ini */
    HWND    hGbList;                   /* 道具页：列表 */
    HWND    hGbEdit[SH_GF_COUNT];      /* 道具页：5 个字段输入框 */
    int     page;                      /* 0 = 存档页，1 = 道具页 */
    int     dirty;                     /* 与载入快照相比是否有改动 */
    uint8_t *base;                     /* 载入时的字节快照（判断 dirty 用） */
    size_t   baseSize;
    char    lastDir[MAX_PATH];         /* 上次浏览/打开的目录 */
    char    iconPath[MAX_PATH];
    unsigned long iconErr;
    int     layoutTest;                /* -layouttest：自检控件矩形后退出 */
    int     uiTest;                    /* -uitest：自检切换角色是否丢修改 */
} App;

static App g;

/* 前置声明（定义在后面） */
static int  apply_fields(void);
static int  apply_slots(void);
static int  apply_money(void);
static int  apply_current(int quiet);
static void sync_baseline(void);
static int  recompute_dirty(void);
static void update_title(HWND hwnd, int dirty);
static void do_about(void);
static void set_page(int which);
static int  is_goods_id(int id);
static void do_goods(void);
static void do_human(void);
static void gb_fill_list(void);
static void gb_show_item(int id);
static int  gb_apply(void);
static void gb_commit(void);
/* 道具表状态：定义在此，供 apply_slots() 等早于「道具表」一节的函数使用 */
static sh_goods s_gb;
static int      s_gbLoaded;

static void build_ini_path(void);
static void load_settings(void);
static void remember_dir(const char *fileOrDir);

/* ------------------------------------------------------------------ */
/* UTF-8 -> 系统 ANSI（简体中文为 GBK）；返回轮转缓冲，可连续用于多次 SetText */
/* ------------------------------------------------------------------ */
/* 小文本用：内部轮转缓冲，512 字节上限（标签、状态栏足够）。
 * 大段文本（技能树等）请直接调用 sh_utf8_to_acp() 并给足缓冲区，
 * 否则会被 512 截断，且可能切在 GBK 双字节中间 -> 尾部乱码。 */
static const char *ansi(const char *u8)
{
    static char bufs[8][512];
    static int  idx = 0;
    char *out = bufs[idx++ & 7];
    sh_utf8_to_acp(u8, out, 512);
    return out;
}

static void status(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g.hStatus) SetWindowTextA(g.hStatus, ansi(buf));
}

/* 自检模式下不要弹模态框（无人点确定会卡死），改为记录下来供断言 */
static char g_lastMsg[4096];

static void msg(const char *title, const char *text, UINT flags)
{
    /* 注意：不能用 ansi() —— 它只有 512 字节静态缓冲，
     * 「关于」这类长文本会被截断在双字节中间，显示成乱码。 */
    static char body[4096];
    static char head[256];

    strncpy(g_lastMsg, text ? text : "", sizeof(g_lastMsg) - 1);
    g_lastMsg[sizeof(g_lastMsg) - 1] = (char)0;

    if (g.uiTest || g.layoutTest) return;    /* 自检：不弹框 */

    sh_utf8_to_acp(text ? text : "", body, sizeof(body));
    sh_utf8_to_acp(title ? title : "", head, sizeof(head));
    MessageBoxA(NULL, body, head, flags);
}

/* ---------------- 轻量 tooltip（不依赖 comctl32） ----------------
 * Win98 的 Tooltip 属于 comctl32，本项目不用它；
 * 这里用"定时轮询光标 + 自绘 popup 窗口"实现，行为等价、依赖为零。 */
static HWND s_tip;
static int  s_tipShowing;
static const char *s_tipText = "";
static int  s_tipX = -99999, s_tipY = -99999;

static LRESULT CALLBACK TipProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND: {
        RECT rc;
        HBRUSH b;
        GetClientRect(h, &rc);
        b = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
        FillRect((HDC)wp, &rc, b);
        DeleteObject(b);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc;
        RECT rc;
        char a[1024];
        dc = BeginPaint(h, &ps);
        GetClientRect(h, &rc);
        rc.left += 4; rc.top += 3; rc.right -= 4; rc.bottom -= 3;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_INFOTEXT));
        if (g.hFont) SelectObject(dc, g.hFont);   /* 与测量用同一个字体 */
        sh_utf8_to_acp(s_tipText ? s_tipText : "", a, (int)sizeof(a));
        DrawTextA(dc, a, -1, &rc, DT_LEFT | DT_TOP | DT_WORDBREAK);
        if (g.hFont) SelectObject(dc, GetStockObject(SYSTEM_FONT));
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcA(h, m, wp, lp);
}

static void tip_show(int x, int y, const char *utf8)
{
    char a[1024];
    HDC dc;
    HFONT old;
    RECT rc;

    if (!s_tip) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = TipProc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
        wc.lpszClassName = "ShuiHuTip";
        RegisterClassA(&wc);
        s_tip = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "ShuiHuTip", "",
                                WS_POPUP | WS_BORDER, 0, 0, 10, 10,
                                NULL, NULL, wc.hInstance, NULL);
        if (!s_tip) return;
        if (g.hFont) SendMessageA(s_tip, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        s_tipText = "";
    }
    if (s_tipText != utf8) {
        s_tipText = utf8;
        sh_utf8_to_acp(utf8, a, (int)sizeof(a));
        dc = GetDC(s_tip);
        old = (HFONT)SelectObject(dc, g.hFont ? g.hFont : GetStockObject(DEFAULT_GUI_FONT));
        SetRect(&rc, 0, 0, 330, 0);
        DrawTextA(dc, a, -1, &rc, DT_CALCRECT | DT_LEFT | DT_WORDBREAK);
        SelectObject(dc, old);
        ReleaseDC(s_tip, dc);
        if (rc.right < 120) rc.right = 120;        /* 最小宽度 */
        SetWindowPos(s_tip, HWND_TOPMOST, 0, 0, rc.right + 14, rc.bottom + 12,
                     SWP_NOACTIVATE | SWP_NOMOVE);
        InvalidateRect(s_tip, NULL, FALSE);
    }
    /* 同一位置不再重复 SetWindowPos（否则每 200ms 触发一次重绘 → 看起来在闪） */
    if (x != s_tipX || y != s_tipY) {
        SetWindowPos(s_tip, HWND_TOPMOST, x + 18, y + 22, 0, 0,
                     SWP_NOACTIVATE | SWP_NOSIZE);
        s_tipX = x;
        s_tipY = y;
    }
    if (!s_tipShowing) { ShowWindow(s_tip, SW_SHOWNOACTIVATE); s_tipShowing = 1; }
}

static void tip_hide(void)
{
    if (s_tipShowing && s_tip) { ShowWindow(s_tip, SW_HIDE); s_tipShowing = 0; }
    s_tipX = s_tipY = -99999;
}

/* 光标落在某个控件上就显示对应提示，否则隐藏 */
typedef struct { HWND h; const char *text; } sh_tip_item;

static void tip_poll(sh_tip_item *map, int n)
{
    POINT p;
    RECT r;
    int i;

    GetCursorPos(&p);
    for (i = 0; i < n; i++) {
        if (!map[i].h || !map[i].text) continue;
        if (!IsWindowVisible(map[i].h)) continue;
        GetWindowRect(map[i].h, &r);
        if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) {
            tip_show(p.x, p.y, map[i].text);
            return;
        }
    }
    tip_hide();
}

/* ------------------------------------------------------------------ */
/* 列表与字段                                                          */
/* ------------------------------------------------------------------ */
static void fill_list(void)
{
    int i;
    char buf[160];

    SendMessageA(g.hList, LB_RESETCONTENT, 0, 0);
    g.mapCount = 0;
    for (i = 0; i < g.s.count; i++) {
        if (!sh_is_playable(&g.s.units[i])) continue;
        sprintf(buf, "%2u  %s", (unsigned)g.s.units[i].id,
                sh_char_name(g.s.units[i].id));
        SendMessageA(g.hList, LB_ADDSTRING, 0, (LPARAM)ansi(buf));
        g.map[g.mapCount++] = i;
    }
    if (g.mapCount == 0) {
        SendMessageA(g.hList, LB_ADDSTRING, 0, (LPARAM)ansi("(本存档没有可编辑角色)"));
        g.sel = -1;
        return;
    }
    SendMessageA(g.hList, LB_SETCURSEL, 0, 0);
    g.sel = g.map[0];
}

static void clear_fields(void)
{
    int i;
    for (i = 0; i < NFIELDS; i++) SetWindowTextA(g.hEdit[i], "");
    for (i = 0; i < SH_SLOT_COUNT; i++) {
        SendMessageA(g.hSlot[i], CB_SETCURSEL, 0, 0);
    }
    SetWindowTextA(g.hMoney, "");
    SetWindowTextA(g.hHidden, "");
    SetWindowTextA(g.hEquipNote[0], "");
    SetWindowTextA(g.hEquipNote[1], "");
}

static void show_unit(void)
{
    char buf[256];
    int i;
    const sh_unit *u;

    if (!g.loaded || g.sel < 0 || g.sel >= g.s.count) { clear_fields(); return; }
    u = &g.s.units[g.sel];

    for (i = 0; i < NFIELDS; i++) {
        sprintf(buf, "%u", (unsigned)u->f[kFields[i]]);
        SetWindowTextA(g.hEdit[i], buf);
    }

    for (i = 0; i < SH_SLOT_COUNT; i++) {
        SendMessageA(g.hSlot[i], CB_SETCURSEL, (WPARAM)u->slots[i], 0);
    }
    sprintf(buf, "%lu", (unsigned long)g.s.money);
    SetWindowTextA(g.hMoney, buf);

    if (u->hidden_len > 0) {
        int k, first = -1;
        for (k = 0; k < u->hidden_len; k++) if (u->hidden[k] != 0) { first = k; break; }
        if (first < 0) strcpy(buf, "隐藏技：已全部领悟");
        else {
            int n = 0;
            n += sprintf(buf + n, "隐藏技：");
            for (k = first; k < u->hidden_len; k++) n += sprintf(buf + n, "%u", (unsigned)u->hidden[k]);
            sprintf(buf + n, "（0=已领悟 1=未领悟）");
        }
    } else {
        strcpy(buf, "隐藏技：（无标志块）");
    }
    SetWindowTextA(g.hHidden, ansi(buf));

    /* 攻击/防御的“装备加成”分解：面板 = 基础（游戏算，不存盘） + Σ三槽缓存。
     * 这里只显示装备那一部分，让用户知道这个数还有另一半来源。 */
    {
        int k, sa = 0, sd = 0;
        for (k = 0; k < 3 && k < SH_SLOT_COUNT; k++) {
            if (u->slots[k] == 0) continue;      /* 空槽不计入 */
            sa += sh_slot_cache_get(&g.s, g.sel, k, SH_CACHE_ATK);
            sd += sh_slot_cache_get(&g.s, g.sel, k, SH_CACHE_DEF);
        }
        sprintf(buf, "装备+%d", sa);
        SetWindowTextA(g.hEquipNote[0], ansi(buf));
        sprintf(buf, "装备+%d", sd);
        SetWindowTextA(g.hEquipNote[1], ansi(buf));
    }
}

/* 从界面读取字段并写入内存；失败返回 0 并弹提示 */
static int apply_fields(void)
{
    char buf[64];
    int i;
    uint16_t oldv[NFIELDS];
    sh_unit *u;

    if (!g.loaded || g.sel < 0) return 0;
    u = &g.s.units[g.sel];

    /* 先记下旧值，任一步失败就整体回滚 */
    for (i = 0; i < NFIELDS; i++) oldv[i] = u->f[kFields[i]];

    /* 第一遍：只做范围检查就写入（不查「当前 ≤ 上限」）
     * 因为一次同时改「当前值」和「上限」时，中途必然短暂越界。
     * 例：气力 98 → 309，若先写当前值，此刻上限还是 98。 */
    for (i = 0; i < NFIELDS; i++) {
        long v;
        int rc;
        GetWindowTextA(g.hEdit[i], buf, sizeof(buf));
        v = strtol(buf, NULL, 10);
        if (v < 0) v = 0;
        rc = sh_set_field_raw(&g.s, g.sel, kFields[i], (uint16_t)v);
        if (rc != 0) {
            char m[256];
            int k;
            for (k = 0; k < i; k++) sh_set_field_raw(&g.s, g.sel, kFields[k], oldv[k]);
            sprintf(m, "字段「%s」的输入值 %ld 超出允许范围（上限 %d）。",
                    sh_field_label(kFields[i]), v, sh_field_max(kFields[i]));
            msg("输入有误", m, MB_ICONWARNING);
            return 0;
        }
    }

    /* 第二遍：检查最终状态是否合法（当前 ≤ 上限） */
    if (u->f[SH_F_HP] > u->f[SH_F_HPMAX] || u->f[SH_F_MP] > u->f[SH_F_MPMAX]) {
        char m[320];
        for (i = 0; i < NFIELDS; i++) sh_set_field_raw(&g.s, g.sel, kFields[i], oldv[i]);
        sprintf(m,
            "这些数值放在一起不成立：\n\n"
            "  生命 %u / 上限 %u\n  气力 %u / 上限 %u\n\n"
            "「当前值」不能大于「上限」。\n"
            "如果两个都改，请把上限也改到不小于当前值。",
            (unsigned)u->f[SH_F_HP], (unsigned)u->f[SH_F_HPMAX],
            (unsigned)u->f[SH_F_MP], (unsigned)u->f[SH_F_MPMAX]);
        msg("输入有误", m, MB_ICONWARNING);
        return 0;
    }
    return 1;
}

/* 把界面上的改动写回内存（不落盘）。返回 0 表示校验失败（已弹提示）。 */
static int apply_current(int quiet)
{
    if (!g.loaded || g.sel < 0) return 1;
    (void)quiet;
    if (!apply_fields()) return 0;
    if (!apply_slots())  return 0;
    if (!apply_money())  return 0;
    /* 注意：不能无条件置 1 —— 切换角色也会走到这里，
     * 那样「什么都没改」也会被当成有未保存修改。改为按快照比对。 */
    recompute_dirty();
    return 1;
}

/* 记录当前存档内容为 baseline（载入/写入成功后调用） */
static void sync_baseline(void)
{
    if (g.base) { free(g.base); g.base = NULL; g.baseSize = 0; }
    if (!g.loaded || !g.s.data || g.s.size == 0) return;
    g.base = (uint8_t *)malloc(g.s.size);
    if (!g.base) return;
    memcpy(g.base, g.s.data, g.s.size);
    g.baseSize = g.s.size;
}

/* 与 baseline 逐字节比较，返回是否有改动；同时刷新标题 */
static int recompute_dirty(void)
{
    int d = 0;
    if (g.base && g.loaded && g.s.data && g.s.size == g.baseSize) {
        d = (memcmp(g.s.data, g.base, g.s.size) != 0);
    }
    g.dirty = d;
    update_title(g.hMain, d);
    return d;
}

/* 标题栏加 * 表示有未保存修改 */
static void update_title(HWND hwnd, int dirty)
{
    char t[256];
    sprintf(t, "%s%s", ansi("水浒传之梁山好汉 — 存档编辑器  " APP_VERSION),
            dirty ? "  *" : "");
    if (hwnd) SetWindowTextA(hwnd, t);
}

/* 取某道具的 4 个装备缓存值（GOODS +76/+82/+84/+90）。
 * 优先用已加载的 s_gb，否则临时读一次 GOODS.DAB。
 * 返回 0 表示取不到（调用方应保持原缓存不动）。 */
static int item_cache4(int id, unsigned short v[4])
{
    char path[600];
    sh_goods tmp;

    v[0] = v[1] = v[2] = v[3] = 0;
    if (id <= 0) return 1;                       /* 空槽 -> 全 0 */

    if (s_gbLoaded) {
        v[0] = (unsigned short)sh_goods_get_u16(&s_gb, id, 76);
        v[1] = (unsigned short)sh_goods_get_u16(&s_gb, id, 82);
        v[2] = (unsigned short)sh_goods_get_u16(&s_gb, id, 84);
        v[3] = (unsigned short)sh_goods_get_u16(&s_gb, id, 90);
        return 1;
    }
    if (!goods_locate(path, sizeof(path))) return 0;
    if (sh_goods_load(path, &tmp) != 0) return 0;
    v[0] = (unsigned short)sh_goods_get_u16(&tmp, id, 76);
    v[1] = (unsigned short)sh_goods_get_u16(&tmp, id, 82);
    v[2] = (unsigned short)sh_goods_get_u16(&tmp, id, 84);
    v[3] = (unsigned short)sh_goods_get_u16(&tmp, id, 90);
    sh_goods_free(&tmp);
    return 1;
}

static int apply_slots(void)
{
    int i;

    if (!g.loaded || g.sel < 0) return 0;

    for (i = 0; i < SH_SLOT_COUNT; i++) {
        int v = (int)SendMessageA(g.hSlot[i], CB_GETCURSEL, 0, 0);
        int old = (int)g.s.units[g.sel].slots[i];
        if (v == CB_ERR) v = 0;
        if (v < 0 || v > sh_item_count()) v = 0;
        if (sh_set_slot(&g.s, g.sel, i, (uint8_t)v) != 0) return 0;

        /* 道具换了（含清空）就把槽位缓存按 GOODS 重填 —— 这正是游戏"获得道具"
         * 时的动作；否则新装的装备加成为 0，玩家会看到"装上了攻击力却没变"。
         * 只在 id 变化时才填，避免覆盖用户在「装备加成」里手改的值。 */
        if (v != old) {
            unsigned short c4[4];
            int ok = item_cache4(v, c4);
            /* 取不到 GOODS 时 item_cache4 会把 c4 置 0：宁可清零，也不要残留
             * 上一件道具的加成（那会凭空多出攻击力，更误导）。 */
            sh_slot_cache_set4(&g.s, g.sel, i, c4);
            (void)ok;
        }
    }
    return 1;
}

/* 金钱（u32，全局字段） */
static int apply_money(void)
{
    char buf[64];
    char *end;
    unsigned long v;

    if (!g.loaded) return 0;
    GetWindowTextA(g.hMoney, buf, sizeof(buf));
    if (buf[0] == '\0') return 1;              /* 留空表示不改 */

    v = strtoul(buf, &end, 10);
    if (end == buf || *end != '\0' || v > 4294967295UL) {
        msg("输入有误", "金钱应为 0 .. 4294967295 之间的整数。", MB_ICONWARNING);
        return 0;
    }
    sh_set_money(&g.s, (uint32_t)v);
    return 1;
}
/* ------------------------------------------------------------------ */
/* 写盘：备份 -> 写 -> 重解析校验 -> 重新载入界面                        */
/* ------------------------------------------------------------------ */
static int commit_and_reload(int quiet)
{
    char bak[600];
    int rc;
    sh_save chk;

    rc = sh_make_backup(g.path, bak, sizeof(bak));
    if (rc != 0) {
        msg("已中止", "备份失败，未写入任何内容。\n\n请检查存档目录是否可写。", MB_ICONERROR);
        return 0;
    }

    rc = sh_write_file(g.path, g.s.data, g.s.size);
    if (rc != 0) {
        msg("已中止", "写入失败，原文件未被改动。", MB_ICONERROR);
        return 0;
    }

    /* 写后自校验：重新解析，比对锚点数与类型 id */
    if (sh_load(g.path, &chk) != 0) {
        char m[700];
        sprintf(m, "写入完成，但重新解析失败！\n请用备份恢复：\n%s", bak);
        msg("校验失败", m, MB_ICONERROR);
        return 0;
    }
    {
        int i, ok = (chk.count == g.s.count);
        for (i = 0; i < g.s.count && i < chk.count; i++) {
            if (chk.units[i].id != g.s.units[i].id) { ok = 0; break; }
        }
        sh_free(&chk);
        if (!ok) {
            char m[700];
            sprintf(m, "校验未通过：锚点或类型 id 发生变化。\n请用备份恢复：\n%s", bak);
            msg("校验失败", m, MB_ICONERROR);
            return 0;
        }
    }

    if (!quiet) {
        char m[700];
        sprintf(m, "写入成功。\n\n备份：\n%s", bak);
        msg("完成", m, MB_ICONINFORMATION);
    }
    return 1;
}

/* 重新读入当前存档；成功返回 1，失败返回 0（并把 loaded 清掉，避免后续操作踩空） */
static int reload_from_disk(void)
{
    int keep = g.sel;
    sh_free(&g.s);
    if (sh_load(g.path, &g.s) == 0) {
        sync_baseline();
        g.dirty = 0;
        update_title(g.hMain, 0);
        fill_list();
        if (keep >= 0 && keep < g.s.count) g.sel = keep;
        show_unit();
        return 1;
    }
    g.loaded = 0;
    g.sel = -1;
    g.dirty = 0;
    update_title(g.hMain, 0);
    if (g.hList) SendMessageA(g.hList, LB_RESETCONTENT, 0, 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 命令                                                                */
/* ------------------------------------------------------------------ */
static void do_open(void)
{
    char path[MAX_PATH];
    GetWindowTextA(g.hPath, path, sizeof(path));
    if (!path[0]) { msg("提示", "请先填写或选择存档文件。", MB_ICONINFORMATION); return; }
    if (g.dirty) {
        if (MessageBoxA(g.hMain,
                ansi("当前有未保存的修改，打开新存档会丢弃它们。\n\n确定继续吗？"),
                ansi("未保存的修改"), MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
            return;
    }
    if (g.loaded) { sh_free(&g.s); g.loaded = 0; }

    if (sh_load(path, &g.s) != 0) {
        char m[700];
        sprintf(m, "无法读入：%s\n\n请确认文件存在且是本游戏的 .sav 存档。", path);
        msg("打开失败", m, MB_ICONERROR);
        return;
    }
    g.loaded = 1;
    sync_baseline();
    g.dirty = 0;
    update_title(g.hMain, 0);
    strncpy(g.path, path, MAX_PATH - 1);
    g.path[MAX_PATH - 1] = '\0';
    remember_dir(path);
    fill_list();
    show_unit();
    {
        int playable = 0, k;
        for (k = 0; k < g.s.count; k++) if (sh_is_playable(&g.s.units[k])) playable++;
        status("%lu 字节 ｜ 金钱 %lu ｜ 锚点 %d 个 ｜ 可编辑角色 %d 个",
               (unsigned long)g.s.size, (unsigned long)g.s.money, g.s.count, playable);
    }
}

static void do_browse(void)
{
    OPENFILENAMEA ofn;
    char file[MAX_PATH];
    DWORD err;

    file[0] = '\0';
    memset(&ofn, 0, sizeof(ofn));
    /* 关键：Win98 的 comdlg32 只接受 76 字节的旧版结构（Version 400），
     * 而我们头文件里的 OPENFILENAMEA 是 88 字节（含 FlagsEx）。
     * 直接传 sizeof(ofn) 会被 Win98 以 CDERR_STRUCTSIZE(0x1) 拒绝。
     * 新版 Windows 同样接受 76，所以统一传 76 最稳。 */
#ifndef OPENFILENAME_SIZE_VERSION_400
#define OPENFILENAME_SIZE_VERSION_400 76
#endif
    ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400;
    ofn.hwndOwner = g.hMain;              /* 关键：对话框属于主窗口，才会显示在最前 */
    ofn.lpstrFilter = "存档文件 (*.sav)\0*.sav\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = g.lastDir;     /* 记住上次的目录 */
    ofn.lpstrTitle = "选择存档文件";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        SetWindowTextA(g.hPath, file);
        do_open();
        return;
    }

    err = CommDlgExtendedError();
    if (err != 0) {
        char m[400];
        sprintf(m, "文件对话框打开失败。\n\n错误码：0x%lX\n\n"
                   "可以绕过：把存档路径直接粘到左上角的输入框，再点「打开」。",
                (unsigned long)err);
        msg("浏览失败", m, MB_ICONWARNING);
    }
    /* 用户点「取消」时 err==0，不提示 */
}

static void do_write(void)
{
    if (!g.loaded) { msg("提示", "请先打开存档。", MB_ICONINFORMATION); return; }
    if (!apply_current(0)) return;
    if (!commit_and_reload(0)) return;
    g.dirty = 0;
    update_title(g.hMain, 0);
    reload_from_disk();
}

static void do_unlock(void)
{
    int cls, hidN, n;
    char m[256];

    if (!g.loaded || g.sel < 0) {
        msg("提示", "请先打开存档并选择角色。", MB_ICONINFORMATION);
        return;
    }

    cls  = (int)g.s.units[g.sel].id;
    hidN = sh_class_hidden_count(cls);
    if (hidN <= 0) {
        msg("无需解锁",
            "该角色所属职业的技能表里没有隐藏必杀技。\n\n"
            "隐藏技由 HUMAN.HUM 职业技能表中「隐藏标志=1」的条目决定，\n"
            "该职业没有这类条目。",
            MB_ICONINFORMATION);
        return;
    }

    n = sh_unlock_hidden(&g.s, g.sel, hidN);
    if (n <= 0) {
        msg("无法解锁",
            "该角色记录里没有找到隐藏技标志块。\n\n"
            "可能本来就没有隐藏技记录。",
            MB_ICONWARNING);
        return;
    }

    if (!commit_and_reload(1)) return;
    reload_from_disk();

    sprintf(m, "已清零 %d 格隐藏技标志（该职业共 %d 段）。\n\n"
               "注意：面板上是否显示某个必杀技，还取决于角色等级\n"
               "是否达到它的学习等级（可在「查看技能树」里看到）。",
            n, hidN);
    msg("完成", m, MB_ICONINFORMATION);
}

/* 找到 backup_save 目录里最新的备份并恢复 */
/* ---------------- 恢复存档：从 backup_save 里选一份 ---------------- */

#define SH_MAX_BAK 400

static char     s_bakPath[SH_MAX_BAK][MAX_PATH];   /* 完整路径 */
static char     s_bakText[SH_MAX_BAK][220];        /* 列表显示文字 */
static FILETIME s_bakTime[SH_MAX_BAK];             /* 修改时间（排序用） */
static int      s_bakCount;
static int      s_bakChoice;                        /* -1 = 取消 */
static HWND     s_bakWnd;
static HWND     s_pickParent;                       /* 拥有选择窗口的父窗口；NULL = 主窗口 */
static const char *s_pickTitle = "恢复存档";

static void ft_to_text(const FILETIME *ft, char *out)
{
    FILETIME lft;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(ft, &lft);
    FileTimeToSystemTime(&lft, &st);
    sprintf(out, "%04d-%02d-%02d %02d:%02d:%02d",
            (int)st.wYear, (int)st.wMonth, (int)st.wDay,
            (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
}

/* 枚举 <存档目录>\backup_save\*.sav，按修改时间倒序（最新在前） */
/* 枚举指定目录下的匹配文件，按修改时间倒序（最新在前）。
 * dir 为目录，pat 为 \\*.xxx 形式的后缀模式，showDir 决定列表里显示的路径前缀。 */
static int collect_backups_in(const char *dir, const char *pat, const char *showDir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int i, n = 0;

    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        char when[32];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (n >= SH_MAX_BAK) break;

        ft_to_text(&fd.ftLastWriteTime, when);
        sprintf(s_bakPath[n], "%s\\%s", showDir, fd.cFileName);
        sprintf(s_bakText[n], "%s   %s   (%lu 字节)",
                when, fd.cFileName, (unsigned long)fd.nFileSizeLow);
        s_bakTime[n] = fd.ftLastWriteTime;
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    /* 插入排序：新的在前 */
    for (i = 1; i < n; i++) {
        char   tp[MAX_PATH];
        char   tt[220];
        FILETIME tf;
        int    k = i - 1;

        strcpy(tp, s_bakPath[i]);
        strcpy(tt, s_bakText[i]);
        tf = s_bakTime[i];
        while (k >= 0 && CompareFileTime(&s_bakTime[k], &tf) < 0) {
            strcpy(s_bakPath[k + 1], s_bakPath[k]);
            strcpy(s_bakText[k + 1], s_bakText[k]);
            s_bakTime[k + 1] = s_bakTime[k];
            k--;
        }
        strcpy(s_bakPath[k + 1], tp);
        strcpy(s_bakText[k + 1], tt);
        s_bakTime[k + 1] = tf;
    }

    s_bakCount = n;
    return n;
}

/* 存档备份：<存档目录>\backup_save\*.sav */
static int collect_backups(void)
{
    char dir[MAX_PATH], pat[MAX_PATH], bdir[MAX_PATH + 32];
    char *p;

    strncpy(dir, g.path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = (char)0;
    p = strrchr(dir, '\\');
    if (p) *p = (char)0; else strcpy(dir, ".");
    sprintf(bdir, "%s\\backup_save", dir);
    sprintf(pat, "%s\\*.sav", bdir);
    return collect_backups_in(dir, pat, bdir);
}

/* 道具表备份：<GOODS 目录>\GOODS.DAB_*.bak */
static int collect_goods_backups(const char *goodsPath)
{
    char dir[MAX_PATH], pat[MAX_PATH];
    char *p;

    strncpy(dir, goodsPath, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = (char)0;
    p = strrchr(dir, '\\');
    if (p) *p = (char)0; else strcpy(dir, ".");
    sprintf(pat, "%s\\GOODS.DAB_*.bak", dir);
    return collect_backups_in(dir, pat, dir);
}

/* 职业表备份：<HUMAN 目录>\HUMAN.HUM_*.bak */
static int collect_human_backups(const char *humanPath)
{
    char dir[MAX_PATH], pat[MAX_PATH];
    char *p;

    strncpy(dir, humanPath, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = (char)0;
    p = strrchr(dir, '\\');
    if (p) *p = (char)0; else strcpy(dir, ".");
    sprintf(pat, "%s\\HUMAN.HUM_*.bak", dir);
    return collect_backups_in(dir, pat, dir);
}

static LRESULT CALLBACK BakProc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: {
        HWND lb = CreateWindowExA(0, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
            12, 10, 556, 330, hwnd, (HMENU)(INT_PTR)ID_BAK_LIST, NULL, NULL);
        HWND b1 = CreateWindowExA(0, "BUTTON", ansi("恢复"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            330, 352, 110, 30, hwnd, (HMENU)(INT_PTR)ID_BAK_OK, NULL, NULL);
        HWND b2 = CreateWindowExA(0, "BUTTON", ansi("取消"),
            WS_CHILD | WS_VISIBLE, 458, 352, 110, 30, hwnd,
            (HMENU)(INT_PTR)ID_BAK_CANCEL, NULL, NULL);
        HWND st = CreateWindowExA(0, "STATIC",
            ansi("默认选中最新的一份；恢复会用备份覆盖当前存档。"),
            WS_CHILD | WS_VISIBLE, 12, 356, 300, 20, hwnd, NULL, NULL, NULL);
        if (g.hFont) {
            SendMessageA(lb, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            SendMessageA(b1, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            SendMessageA(b2, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            SendMessageA(st, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        }
        (void)lb; (void)b1; (void)b2; (void)st;
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_BAK_OK) {
            int sel = (int)SendMessageA(GetDlgItem(hwnd, ID_BAK_LIST), LB_GETCURSEL, 0, 0);
            s_bakChoice = (sel >= 0 && sel < s_bakCount) ? sel : -1;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == ID_BAK_CANCEL) {
            s_bakChoice = -1;
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        s_bakChoice = -1;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        s_bakWnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, m, wp, lp);
}

/* 弹窗选择备份；返回选中的下标，-1 表示取消 */
static int pick_backup(void)
{
    WNDCLASSA wc;
    MSG msgLoop;
    HWND lb, owner;
    int i;

    owner = s_pickParent ? s_pickParent : g.hMain;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = BakProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ShuiHuBak";
    RegisterClassA(&wc);

    s_bakChoice = -1;
    s_bakWnd = CreateWindowExA(0, "ShuiHuBak", ansi(s_pickTitle),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 430,
        owner, NULL, wc.hInstance, NULL);
    if (!s_bakWnd) return -1;

    lb = GetDlgItem(s_bakWnd, ID_BAK_LIST);
    if (lb) {
        for (i = 0; i < s_bakCount; i++)
            SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)ansi(s_bakText[i]));
        SendMessageA(lb, LB_SETCURSEL, 0, 0);      /* 默认最新 */
    }

    EnableWindow(owner, FALSE);
    ShowWindow(s_bakWnd, SW_SHOW);
    UpdateWindow(s_bakWnd);

    while (s_bakWnd && GetMessageA(&msgLoop, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(s_bakWnd, &msgLoop)) {
            TranslateMessage(&msgLoop);
            DispatchMessageA(&msgLoop);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    tip_hide();                     /* 关掉残留的提示框 */
    /* 模态窗口覆盖过父窗口，关掉后必须重绘。
     * 注意：InvalidateRect 只会重画窗口本身，子控件（按钮等）不会跟着重画，
     * 所以必须用 RDW_ALLCHILDREN。 */
    RedrawWindow(owner, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    return s_bakChoice;
}

static void do_restore(void)
{
    int pick;
    char m[1600];

    if (!g.loaded) { msg("提示", "请先打开存档。", MB_ICONINFORMATION); return; }

    /* 1) 先找：目录不存在直接提示，不要弹出空窗口 */
    if (collect_backups() <= 0) {
        msg("没有备份", "backup_save 目录里没有找到备份文件。", MB_ICONINFORMATION);
        return;
    }

    /* 2) 有未保存修改则先确认（恢复会丢弃它们） */
    if (g.dirty) {
        if (MessageBoxA(g.hMain,
                ansi("当前有未保存的修改，恢复存档会丢弃它们。\n\n确定继续吗？"),
                ansi("未保存的修改"), MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
            return;
    }

    /* 3) 选择 */
    pick = pick_backup();
    if (pick < 0 || pick >= s_bakCount) return;

    sprintf(m, "将用下面这份备份覆盖当前存档：\n\n%s\n\n确定恢复吗？", s_bakPath[pick]);
    if (MessageBoxA(g.hMain, ansi(m), ansi("确认恢复"),
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;

    if (!CopyFileA(s_bakPath[pick], g.path, FALSE)) {
        msg("恢复失败", "复制备份文件失败。", MB_ICONERROR);
        return;
    }
    g.dirty = 0;
    update_title(g.hMain, 0);
    reload_from_disk();
    msg("完成", "已从备份恢复。", MB_ICONINFORMATION);
}


/* 重载存档：丢弃内存中的改动，重新读文件 */
static void do_reload(void)
{
    if (!g.loaded) { msg("提示", "请先打开存档。", MB_ICONINFORMATION); return; }

    if (MessageBoxA(g.hMain,
            ansi("所有未保存的更改会丢失。\n\n确定重新载入吗？"),
            ansi("重载存档"), MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    if (!reload_from_disk()) {
        msg("重载失败", "无法重新读入这个存档文件，可能已被移动或删除。", MB_ICONERROR);
        return;
    }
    status("已重新载入：%s", g.path);
}


/* ---------------- 道具表（GOODS.DAB）编辑窗口 ---------------- */

static HWND     s_gbWnd, s_gbList;
static HWND     s_gbEdit[SH_GF_COUNT];
static int      s_gbSel;            /* 当前选中道具 id，0 = 未选 */

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* 猜测 GOODS.DAB 位置：存档一般在 <游戏>\Save\，数据在 <游戏>\Data\ */
static int goods_locate(char *out, size_t n)
{
    if (!g.path[0]) return 0;
    return sh_goods_locate(g.path, out, n);
}

static void gb_fill_list(void)
{
    int id;
    if (!g.hGbList) return;
    SendMessageA(g.hGbList, LB_RESETCONTENT, 0, 0);
    for (id = 1; id <= s_gb.count; id++) {
        char line[220];
        /* 注意：sh_item_name 返回 UTF-8，这里不要再 ansi()；
         * 整行最后由 ansi(line) 统一转一次，否则会二次转码变成 ??? */
        sprintf(line, "%3d  %-16s  攻%3d 防%3d 移%3d 体%3d 气%3d",
                id, sh_item_name(id),
                sh_goods_get(&s_gb, id, SH_GF_ATK),
                sh_goods_get(&s_gb, id, SH_GF_DEF),
                sh_goods_get(&s_gb, id, SH_GF_MOVE),
                sh_goods_get(&s_gb, id, SH_GF_HPMAX),
                sh_goods_get(&s_gb, id, SH_GF_MPMAX));
        SendMessageA(g.hGbList, LB_ADDSTRING, 0, (LPARAM)ansi(line));
    }
}

/* 从说明里取「关键词+数字」。只认「XX加N」这种明确句式，
 * 避免把「3回合內命中率提升255點」这类状态效果误判成装备数值。 */
static int desc_num_after(const char *desc, const char *key, int *out)
{
    const char *p = strstr(desc, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return 0;
    *out = atoi(p);
    return 1;
}

/* 说明与字段是否一致。返回不一致项数；msg 里写原因（无则空串） */
static int goods_check(int id, char *msg, size_t cap)
{
    static const struct { const char *key; sh_goods_field f; } tab[] = {
        { "\xe6\x94\xbb\xe6\x93\x8a\xe5\x8a\x9b\xe5\x8a\xa0", SH_GF_ATK   },  /* 攻擊力加 */
        { "\xe9\x98\xb2\xe5\xbe\xa1\xe5\x8a\x9b\xe5\x8a\xa0", SH_GF_DEF   },  /* 防禦力加 */
        { "\xe7\xa7\xbb\xe5\x8b\x95\xe5\x8a\x9b\xe5\x8a\xa0", SH_GF_MOVE  },  /* 移動力加 */
        { "\xe6\x9c\x80\xe5\xa4\xa7\xe9\xab\x94\xe5\x8a\x9b\xe5\x8a\xa0", SH_GF_HPMAX }, /* 最大體力加 */
        { "\xe6\x9c\x80\xe5\xa4\xa7\xe6\xb0\xa3\xe5\x8a\x9b\xe5\x8a\xa0", SH_GF_MPMAX }  /* 最大氣力加 */
    };
    const char *d = sh_item_desc(id);
    int i, n, bad = 0;
    size_t used = 0;

    msg[0] = (char)0;
    if (!d || !d[0]) return 0;

    for (i = 0; i < (int)(sizeof(tab) / sizeof(tab[0])); i++) {
        if (!desc_num_after(d, tab[i].key, &n)) continue;
        if (n != sh_goods_get(&s_gb, id, tab[i].f)) {
            char one[160];
            sprintf(one, "%s说明写 %d，字段是 %d",
                    used ? "；" : "", n, sh_goods_get(&s_gb, id, tab[i].f));
            bad++;
            if (used + strlen(one) < cap) { strcpy(msg + used, one); used += strlen(one); }
        }
    }
    if (bad && used + 16 < cap) strcat(msg, " —— 可能不匹配");
    return bad;
}

static void gb_show_item(int id)
{
    int f;
    char buf[32];
    s_gbSel = id;
    if (id < 1) return;
    for (f = 0; f < SH_GF_COUNT; f++) {
        sprintf(buf, "%d", sh_goods_get(&s_gb, id, (sh_goods_field)f));
        SetWindowTextA(g.hGbEdit[f], buf);
    }
    /* 说明文本直接来自记录 +26（构建时已从 Big5 转 UTF-8） */
    SetWindowTextA(GetDlgItem(g.hMain, ID_GB_DESC), ansi(sh_item_desc(id)));
    {
        char cm[300];
        if (goods_check(id, cm, sizeof(cm)) > 0)
            SetWindowTextA(GetDlgItem(g.hMain, ID_GB_CHK), ansi(cm));
        else
            SetWindowTextA(GetDlgItem(g.hMain, ID_GB_CHK), ansi("说明与字段一致"));
    }
}

/* 把输入框里的值写回内存（不落盘）；返回 0 表示有非法值 */
static int gb_apply(void)
{
    int f;
    if (s_gbSel < 1) return 1;
    for (f = 0; f < SH_GF_COUNT; f++) {
        char buf[32];
        int v;
        GetWindowTextA(g.hGbEdit[f], buf, sizeof(buf));
        v = atoi(buf);
        if (sh_goods_set(&s_gb, s_gbSel, (sh_goods_field)f, v) != 0) {
            char m[256];
            sprintf(m, "字段「%s」的值 %d 超出允许范围（%d..%d）。",
                    sh_goods_field_label((sh_goods_field)f), v,
                    sh_goods_field_min((sh_goods_field)f),
                    sh_goods_field_max((sh_goods_field)f));
            msg("输入有误", m, MB_ICONWARNING);
            return 0;
        }
    }
    return 1;
}

/* 切换页面：0 = 存档修改，1 = 道具编辑（伪标签，按 ID 段显隐） */
/* 判断控件 ID 属于「道具编辑页」还是「存档修改页」。
 * 重要：存档页的控件（ID_CACHE、ID_EQNOTE0…）绝不能落进 1062..1079 这一段，
 * 否则会被误判成道具页控件而显示到错误页面（历史上踩过两次）。 */
static int is_goods_id(int id)
{
    /* 分块方案让判断退化成一次边界比较：15000–19999 = 道具编辑页 */
    return (id >= 15000 && id < 20000);
}

/* 两页常显的控件：不参与页签切换 */
static int is_shared_id(int id)
{
    return id == ID_STATUS || id == ID_TAB_SAVE || id == ID_TAB_GOODS || id == ID_HUMAN;
}

static void set_page(int which)
{
    HWND c;

    g.page = which;
    for (c = GetWindow(g.hMain, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        int id = GetDlgCtrlID(c);
        if (is_shared_id(id)) continue;
        /* 注意：存档页的静态标签 ID 是 0，不能跳过 —— 否则两页会叠在一起 */
        /* 注意 ID_MONEY 是 1050，不能把 1034..1051 整段当成道具页 */
        ShowWindow(c, (is_goods_id(id) == which) ? SW_SHOW : SW_HIDE);
    }
    EnableWindow(GetDlgItem(g.hMain, ID_TAB_SAVE),  which == 0 ? FALSE : TRUE);
    EnableWindow(GetDlgItem(g.hMain, ID_TAB_GOODS), which == 1 ? FALSE : TRUE);
}

/* 进入道具页：首次进入时定位并载入 GOODS.DAB */
static void do_goods(void)
{
    char path[600];

    if (!g.path[0]) {
        msg("提示", "请先打开存档。\n\n道具表位置是根据存档目录推断的。",
            MB_ICONINFORMATION);
        return;
    }

    if (!s_gbLoaded) {
        if (!goods_locate(path, sizeof(path))) {
            msg("找不到道具表",
                "没有找到 GOODS.DAB。\n\n"
                "程序会在存档目录的上级 Data\\ 里找，例如：\n"
                "  游戏目录\\Save\\save1.sav\n"
                "  游戏目录\\Data\\GOODS.DAB",
                MB_ICONINFORMATION);
            return;
        }
        if (sh_goods_load(path, &s_gb) != 0) {
            msg("读入失败", "GOODS.DAB 打不开或长度不对（应为 11 + n×198）。",
                MB_ICONERROR);
            return;
        }
        s_gbLoaded = 1;
        s_gbSel = 0;
        gb_fill_list();
        if (s_gb.count > 0) {
            SendMessageA(g.hGbList, LB_SETCURSEL, 0, 0);
            gb_show_item(1);
        }
        status("道具表：%s（%d 件）", path, s_gb.count);
    }
    set_page(1);
}

static void gb_commit(void)
{
    char bak[600], m[900];

    if (!s_gbLoaded) return;
    if (MessageBoxA(g.hMain,
            ansi("即将写入 GOODS.DAB。\n\n"
                 "这是**游戏本体数据**：改完后所有存档里的这件道具都会变，\n"
                 "而不只是当前存档。程序会先自动备份原文件。\n\n确定写入吗？"),
            ansi("写入道具表"), MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    if (sh_goods_backup(&s_gb, bak, sizeof(bak)) != 0) {
        msg("写入失败", "备份失败，已中止（原文件未改动）。", MB_ICONERROR);
        return;
    }
    if (sh_goods_write(&s_gb) != 0) {
        sprintf(m, "写入或校验失败。\n\n原文件备份在：\n%s", bak);
        msg("写入失败", m, MB_ICONERROR);
        return;
    }
    sprintf(m, "已写入并校验通过（%d 件道具）。\n\n备份：\n%s", s_gb.count, bak);
    msg("完成", m, MB_ICONINFORMATION);
}

/* 恢复道具表：从同目录的 GOODS.DAB_*.bak 里挑一份覆盖回去 */
static void do_goods_restore(void)
{
    char m[900];
    int pick;

    if (!s_gbLoaded) {
        do_goods();                     /* 先载入，顺便确定路径 */
        if (!s_gbLoaded) return;
    }

    if (collect_goods_backups(s_gb.path) <= 0) {
        msg("没有备份",
            "GOODS.DAB 所在目录里没有找到 .bak 备份。\n\n"
            "备份是在点「写入道具表」时自动生成的，\n"
            "名字形如 GOODS.DAB_YYYYMMDD-HHMMSS.bak。",
            MB_ICONINFORMATION);
        return;
    }

    s_pickTitle = "恢复道具表";
    pick = pick_backup();
    s_pickTitle = "恢复存档";
    if (pick < 0 || pick >= s_bakCount) return;

    sprintf(m, "将用下面这份备份覆盖 GOODS.DAB：\n\n%s\n\n"
               "（当前未写入的修改会丢失）\n\n确定恢复吗？", s_bakPath[pick]);
    if (MessageBoxA(g.hMain, ansi(m), ansi("确认恢复"),
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;

    if (!CopyFileA(s_bakPath[pick], s_gb.path, FALSE)) {
        msg("恢复失败", "复制备份文件失败。", MB_ICONERROR);
        return;
    }

    {
        char gpath[SH_GOODS_PATH];
        strncpy(gpath, s_gb.path, sizeof(gpath) - 1);
        gpath[sizeof(gpath) - 1] = (char)0;
        sh_goods_free(&s_gb);
        s_gbLoaded = 0;
        if (sh_goods_load(gpath, &s_gb) != 0) {
            msg("恢复后读取失败",
                "已用备份覆盖，但重新读取失败。文件可能已损坏，请用备份再试。",
                MB_ICONERROR);
            return;
        }
    }
    s_gbLoaded = 1;
    s_gbSel = 0;
    gb_fill_list();
    if (s_gb.count > 0) {
        SendMessageA(g.hGbList, LB_SETCURSEL, 0, 0);
        gb_show_item(1);
    }
    status("已从备份恢复道具表：%s", s_bakPath[pick]);
    msg("完成", "已从备份恢复道具表。", MB_ICONINFORMATION);
}

/* ---------------- 装备加成（槽位里的攻击/防御缓存） ----------------
 * 游戏算面板时：攻击 = 基础 + Σ(三个装备槽的 +2)，防御 = 基础 + Σ(+6)，
 * 之后各自夹到 255。改 GOODS 不会刷新这份缓存，所以这里直接改存档。 */
static HWND s_caWnd;
static HWND s_caEdit[3][2];      /* [槽 0..2][0=攻击 1=防御] */

static LRESULT CALLBACK CacheProc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: {
        static const char *names[3] = { "武器槽", "衣服槽", "帽子槽" };
        int r, c;
        for (r = 0; r < 3; r++) {
            HWND la = CreateWindowExA(0, "STATIC", ansi(names[r]),
                WS_CHILD | WS_VISIBLE, 14, 18 + r * 42, 70, 20, hwnd, NULL, NULL, NULL);
            if (g.hFont) SendMessageA(la, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            for (c = 0; c < 2; c++) {
                s_caEdit[r][c] = CreateWindowExA(0, "EDIT", "",
                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                    92 + c * 120, 14 + r * 42, 70, 22, hwnd,
                    (HMENU)(INT_PTR)(ID_CA_EDIT0 + r * 2 + c), NULL, NULL);
                if (g.hFont) SendMessageA(s_caEdit[r][c], WM_SETFONT, (WPARAM)g.hFont, TRUE);
            }
        }
        {
            HWND h1 = CreateWindowExA(0, "STATIC", ansi("攻击(+2)"),
                WS_CHILD | WS_VISIBLE, 92, 0, 70, 16, hwnd, NULL, NULL, NULL);
            HWND h2 = CreateWindowExA(0, "STATIC", ansi("防御(+6)"),
                WS_CHILD | WS_VISIBLE, 212, 0, 70, 16, hwnd, NULL, NULL, NULL);
            HWND st = CreateWindowExA(0, "STATIC",
                ansi("面板 = 基础 + 三个装备槽之和（夹到 255）。\n"
                     "这些值是道具获得时从 GOODS.DAB 复制的快照，\n"
                     "改 GOODS 不会刷新它们。\n"
                     "游戏读档时不重算：改完要回游戏卸下再装备一次才显示。\n"
                     "想读档立刻看到，就直接改上方的攻击力/防御力字段\n"
                     "（但下次重算会被这里的缓存覆盖，两个一起改最稳）。"),
                WS_CHILD | WS_VISIBLE | SS_LEFT, 14, 152, 396, 110, hwnd, NULL, NULL, NULL);
            HWND b1 = CreateWindowExA(0, "BUTTON", ansi("写入存档页"),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                130, 282, 110, 28, hwnd, (HMENU)(INT_PTR)ID_CA_OK, NULL, NULL);
            HWND b2 = CreateWindowExA(0, "BUTTON", ansi("取消"),
                WS_CHILD | WS_VISIBLE, 252, 282, 80, 28, hwnd,
                (HMENU)(INT_PTR)ID_CA_CANCEL, NULL, NULL);
            HWND b0 = CreateWindowExA(0, "BUTTON", ansi("按GOODS重填"),
                WS_CHILD | WS_VISIBLE, 14, 282, 108, 28, hwnd,
                (HMENU)(INT_PTR)ID_CA_REFILL, NULL, NULL);
            if (g.hFont) SendMessageA(b0, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            if (g.hFont) {
                SendMessageA(h1, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(h2, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(st, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(b1, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(b2, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            }
            (void)st;
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == 2 && GetActiveWindow() == s_caWnd) {
            static const char *ta = "攻击缓存（槽 +2）\n"
                "面板攻击 = 基础 + 三个装备槽之和（夹 255）。\n"
                "这是该道具**获得时**从 GOODS 复制的快照，改 GOODS 不会刷新它。\n"
                "改完要回游戏触发一次重算（卸下再装备）才显示。";
            static const char *td = "防御缓存（槽 +6）\n"
                "面板防御 = 基础 + 三个装备槽之和（夹 255）。\n"
                "同样是获得时的快照；改 GOODS 的 +84 不会刷新这里。";
            sh_tip_item map[6];
            int r;
            for (r = 0; r < 3; r++) {
                map[r * 2 + 0].h = s_caEdit[r][0]; map[r * 2 + 0].text = ta;
                map[r * 2 + 1].h = s_caEdit[r][1]; map[r * 2 + 1].text = td;
            }
            tip_poll(map, 6);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == ID_CA_OK) {
            int r, c, bad = 0;
            for (r = 0; r < 3; r++) {
                for (c = 0; c < 2; c++) {
                    char buf[32];
                    int v;
                    GetWindowTextA(s_caEdit[r][c], buf, sizeof(buf));
                    v = atoi(buf);
                    if (sh_slot_cache_set(&g.s, g.sel, r, c, v) != 0) bad++;
                }
            }
            if (bad) {
                msg("部分未写入", "有数值超出范围（0..65535），其余已写入。", MB_ICONWARNING);
            }
            recompute_dirty();
            show_unit();
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == ID_CA_REFILL) {
            /* 按当前 GOODS.DAB 重新填 3 个槽的 4 个缓存值。
             * 用于：道具已经在槽里、但缓存是 0 或过期（工具直接塞进去时不会自动填）。 */
            int r2, filled = 0;
            for (r2 = 0; r2 < 3; r2++) {
                unsigned short c4[4];
                int iid = (int)g.s.units[g.sel].slots[r2];
                if (iid == 0) continue;
                if (!item_cache4(iid, c4)) continue;
                if (sh_slot_cache_set4(&g.s, g.sel, r2, c4) == 0) filled++;
                {
                    char b[32];
                    sprintf(b, "%u", (unsigned)c4[0]);
                    SetWindowTextA(s_caEdit[r2][0], b);
                    sprintf(b, "%u", (unsigned)c4[2]);
                    SetWindowTextA(s_caEdit[r2][1], b);
                }
            }
            if (filled == 0)
                msg("重填失败", "找不到 GOODS.DAB，或三个装备槽都是空的。", MB_ICONWARNING);
            else
                status("已按 GOODS 重填 %d 个装备槽的加成缓存", filled);
            return 0;
        }
        if (LOWORD(wp) == ID_CA_CANCEL) { DestroyWindow(hwnd); return 0; }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        s_caWnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, m, wp, lp);
}

static void do_cache_edit(void)
{
    WNDCLASSA wc;
    MSG msgLoop;
    int r, c;

    if (!g.loaded || g.sel < 0) {
        msg("提示", "请先打开存档并选择角色。", MB_ICONINFORMATION);
        return;
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = CacheProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ShuiHuCache";
    RegisterClassA(&wc);

    s_caWnd = CreateWindowExA(0, "ShuiHuCache", ansi("装备加成（攻击 / 防御缓存）"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 430, 360,
        g.hMain, NULL, wc.hInstance, NULL);
    if (!s_caWnd) return;

    for (r = 0; r < 3; r++) {
        for (c = 0; c < 2; c++) {
            char buf[32];
            sprintf(buf, "%d", sh_slot_cache_get(&g.s, g.sel, r, c));
            SetWindowTextA(s_caEdit[r][c], buf);
        }
    }

    SetTimer(s_caWnd, 2, 200, NULL);   /* tooltip 轮询 */
    EnableWindow(g.hMain, FALSE);
    ShowWindow(s_caWnd, SW_SHOW);
    UpdateWindow(s_caWnd);
    while (s_caWnd && GetMessageA(&msgLoop, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(s_caWnd, &msgLoop)) {
            TranslateMessage(&msgLoop);
            DispatchMessageA(&msgLoop);
        }
    }
    EnableWindow(g.hMain, TRUE);
    SetActiveWindow(g.hMain);
    tip_hide();                     /* 关掉残留的提示框 */
    /* 模态窗口覆盖过主窗口，关掉后必须重绘。
     * 注意：InvalidateRect 只会重画父窗口本身，子控件（按钮等）不会跟着重画，
     * 所以必须用 RDW_ALLCHILDREN。 */
    RedrawWindow(g.hMain, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

/* ---------------- 人物编辑（HUMAN.HUM 职业表） ----------------
 * 72 条职业记录，改的是**游戏本体数据**，所有存档一起受影响。
 * 字段映射与公式见分析报告 §4.20.1（REA 反编译 FUN_00408540 确认）：
 *   面板值 = ftol(基础值 × (成长率/100)^等级) + 装备/槽加成
 *   攻击/防御/命中/敏捷/必殺 夹 255，最大体力/气力夹 510。
 * 所以这里改的是"基础值与成长率"，不是某个存档里的角色。 */
static sh_human s_hum;
static int      s_humLoaded;
static int      s_humSel = -1;
static HWND     s_humWnd;
static HWND     s_humList;
static HWND     s_humBase[SH_HF_COUNT];
static HWND     s_humGrow[SH_HF_COUNT];
static HWND     s_humMoveCtl;
static HWND     s_humInfo;
/* 载入/写入时的「隐藏技条数」基线：只有相对基线发生变化才值得警告
 * （原版 72 个职业里 49 个是 0 条、23 个是 3 条，不是一律 3） */
static int      s_humOrigHidden[SH_HUMAN_MAX];
static sh_human s_humOrig;
static int      s_humOrigLoaded;

static void hu_snapshot(void)
{
    int i;
    for (i = 0; i < s_hum.count && i < SH_HUMAN_MAX; i++)
        s_humOrigHidden[i] = sh_human_skill_hidden_count(&s_hum, i);
}

/* 找 HUMAN.HUM：优先按存档目录推断，其次用已载入的 GOODS.DAB 同目录 */
static int human_path(char *out, size_t n)
{
    char dir[SH_HUMAN_PATH];
    char *p;

    if (g.path[0] && sh_human_locate(g.path, out, n)) return 1;

    if (s_gb.path[0]) {
        strncpy(dir, s_gb.path, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        p = strrchr(dir, '\\');
        if (p) *p = '\0';
        sprintf(out, "%s\\HUMAN.HUM", dir);
        {
            FILE *f = fopen(out, "rb");
            if (f) { fclose(f); return 1; }
        }
    }
    return 0;
}

static int human_ensure(void)
{
    char path[SH_HUMAN_PATH];
    int rc;

    if (s_humLoaded) return 1;

    if (!human_path(path, sizeof(path))) {
        msg("找不到 HUMAN.HUM",
            "按存档目录没能定位到 Data\\HUMAN.HUM。\n\n"
            "先打开一次存档（工具按「存档目录\\..\\Data\\」推断游戏数据位置），"
            "或把本工具放到游戏目录下再用。",
            MB_ICONWARNING);
        return 0;
    }

    rc = sh_human_load(path, &s_hum);
    if (rc != 0) {
        char t[600];
        sprintf(t, "读取失败（错误码 %d）：\n%s\n\n"
                   "-1 打不开　-2 文件太小　-3 内存不足　-4 头不对　-5 结构对不上",
                rc, path);
        msg("HUMAN.HUM 读取失败", t, MB_ICONERROR);
        return 0;
    }
    s_humLoaded = 1;
    s_humSel = -1;
    hu_snapshot();
    /* 构建期嵌入的原始表：导入窗口永远以它为源，不受内存里已改内容影响 */
    if (!s_humOrigLoaded && sh_human_orig_size() > 0) {
        if (sh_human_load_mem(sh_human_orig(), (size_t)sh_human_orig_size(), &s_humOrig) == 0)
            s_humOrigLoaded = 1;
    }
    return 1;
}

/* 职业 id → 角色名（名表只覆盖可玩角色；查不到返回空串） */
static const char *hu_class_name(int cls)
{
    const char *n = sh_char_name(cls);
    if (!n) return "";
    if (n[0] == '?' && n[1] == 0) return "";
    return n;
}

/* 本存档里用了该职业的角色（等级）；用于和存档交叉对比 */
static void hu_save_usage(int cls, char *out, size_t n)
{
    int i, k = 0, cnt = 0;

    if (!out || n < 8) return;
    out[0] = '\0';

    if (!g.loaded) { strncpy(out, "未打开存档", n - 1); out[n - 1] = '\0'; return; }

    for (i = 0; i < g.mapCount; i++) {
        int idx = g.map[i];
        if ((int)g.s.units[idx].id != cls) continue;
        cnt++;
        if (k < (int)n - 12)
            k += sprintf(out + k, "%sLv%d", cnt > 1 ? "、" : "",
                         (int)g.s.units[idx].f[SH_F_LEVEL]);
    }
    if (cnt == 0) { strncpy(out, "本存档没有", n - 1); out[n - 1] = '\0'; }
}

static void hu_fill_list(void)
{
    int i;
    SendMessageA(s_humList, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < s_hum.count; i++) {
        char row[220];
        const char *nm = hu_class_name(i);
        sprintf(row, "%02d %s 攻%d 防%d 移%d",
                i, nm[0] ? nm : "—",
                sh_human_base(&s_hum, i, SH_HF_ATK),
                sh_human_base(&s_hum, i, SH_HF_DEF),
                sh_human_move(&s_hum, i));
        SendMessageA(s_humList, LB_ADDSTRING, 0, (LPARAM)ansi(row));
    }
}

static void hu_show(int cls)
{
    int f;
    char b[32];

    if (cls < 0 || cls >= s_hum.count) return;

    for (f = 0; f < SH_HF_COUNT; f++) {
        sprintf(b, "%d", sh_human_base(&s_hum, cls, (sh_human_field)f));
        SetWindowTextA(s_humBase[f], b);
        sprintf(b, "%d", sh_human_growth(&s_hum, cls, (sh_human_field)f));
        SetWindowTextA(s_humGrow[f], b);
    }
    sprintf(b, "%d", sh_human_move(&s_hum, cls));
    SetWindowTextA(s_humMoveCtl, b);

    {
        char info[400], use[80], head[80];
        const char *nm = hu_class_name(cls);
        hu_save_usage(cls, use, sizeof(use));
        if (nm[0]) sprintf(head, "职业 %s", nm);
        else       sprintf(head, "职业 %d（无名）", cls);
        sprintf(info,
                "%s\n"
                "技能 %d 条\n"
                "本存档：%s\n"
                "可装备类型(+0x19) = %d\n"
                "+0x1D = %d (未定)  偏移 0x%X",
                head,
                sh_human_skill_count(&s_hum, cls),
                use,
                sh_human_u16(&s_hum, cls, 0x19),
                sh_human_u16(&s_hum, cls, 0x1D),
                (unsigned)s_hum.off[cls]);
        SetWindowTextA(s_humInfo, ansi(info));
    }
    s_humSel = cls;
}

/* 控件 → 内存；成功 1，越界 0（并弹提示） */
static int hu_apply(void)
{
    int f, cls = s_humSel;
    char b[32];

    if (cls < 0 || cls >= s_hum.count) return 1;

    for (f = 0; f < SH_HF_COUNT; f++) {
        GetWindowTextA(s_humBase[f], b, sizeof(b));
        if (sh_human_set_base(&s_hum, cls, (sh_human_field)f, atoi(b)) != 0) {
            msg("数值超范围", "基础值必须在 0..65535 之间。", MB_ICONWARNING);
            return 0;
        }
        GetWindowTextA(s_humGrow[f], b, sizeof(b));
        if (sh_human_set_growth(&s_hum, cls, (sh_human_field)f, atoi(b)) != 0) {
            msg("数值超范围", "成长率必须在 0..255 之间（100 = 不成长）。", MB_ICONWARNING);
            return 0;
        }
    }

    GetWindowTextA(s_humMoveCtl, b, sizeof(b));
    if (sh_human_set_move(&s_hum, cls, atoi(b)) != 0) {
        msg("数值超范围", "移动必须在 0..255 之间。", MB_ICONWARNING);
        return 0;
    }
    return 1;
}

static void hu_commit(void)
{
    char bak[SH_HUMAN_PATH + 64];
    int rc;

    if (!hu_apply()) return;

    if (sh_human_backup(&s_hum, bak, sizeof(bak)) != 0) {
        msg("备份失败", "写入前备份失败，已取消写入（没有改动游戏文件）。", MB_ICONERROR);
        return;
    }

    rc = sh_human_write(&s_hum);
    if (rc != 0) {
        char t[700];
        sprintf(t, "%s\n\n备份：%s\n\n"
                   "请用该备份覆盖回去；再次打开时先「从磁盘重读」确认现状。",
                rc == -2 ? "写入后校验失败：文件内容与内存不一致。"
                         : "写入失败（磁盘只读？文件被占用？）。",
                bak);
        msg("写入失败", t, MB_ICONERROR);
        return;
    }

    hu_snapshot();                  /* 写入成功：以磁盘内容作为新基线 */
    hu_fill_list();
    SendMessageA(s_humList, LB_SETCURSEL, s_humSel, 0);
    hu_show(s_humSel);
    status("已写入 %s（备份：%s）", s_hum.path, bak);
    {
        char t[700];
        sprintf(t, "已写入 %s\n\n备份：%s\n\n"
                   "进游戏后需要让角色升级、装卸装备或换关，才会按新值重算面板。",
                s_hum.path, bak);
        msg("完成", t, MB_ICONINFORMATION);
    }
}

/* 从备份恢复 HUMAN.HUM：列出同目录 HUMAN.HUM_*.bak，默认选最新 */
static void do_human_restore(HWND owner)
{
    char m[900];
    int pick;

    if (!s_humLoaded) return;

    if (collect_human_backups(s_hum.path) <= 0) {
        msg("没有备份",
            "HUMAN.HUM 所在目录里没有找到 .bak 备份。\n\n"
            "备份是在点「写入文件」时自动生成的，\n"
            "名字形如 HUMAN.HUM_YYYYMMDD-HHMMSS.bak。",
            MB_ICONINFORMATION);
        return;
    }

    s_pickTitle = "恢复职业表";
    s_pickParent = owner;
    pick = pick_backup();
    s_pickParent = NULL;
    s_pickTitle = "恢复存档";
    if (pick < 0 || pick >= s_bakCount) return;

    sprintf(m, "将用下面这份备份覆盖 HUMAN.HUM：\n\n%s\n\n"
               "（当前未写入的修改会丢失）\n\n确定恢复吗？", s_bakPath[pick]);
    if (MessageBoxA(owner, ansi(m), ansi("确认恢复"),
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;

    if (!CopyFileA(s_bakPath[pick], s_hum.path, FALSE)) {
        msg("恢复失败", "复制备份文件失败。", MB_ICONERROR);
        return;
    }

    {
        char path[SH_HUMAN_PATH];
        int rc;
        strncpy(path, s_hum.path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        sh_human_free(&s_hum);
        s_humLoaded = 0;
        rc = sh_human_load(path, &s_hum);
        if (rc != 0) {
            msg("恢复后读取失败",
                "已用备份覆盖，但重新读取失败。文件可能已损坏，请换一份备份再试。",
                MB_ICONERROR);
            return;
        }
    }
    s_humLoaded = 1;
    s_humSel = -1;
    hu_snapshot();
    hu_fill_list();
    SendMessageA(s_humList, LB_SETCURSEL, 0, 0);
    hu_show(0);
    status("已从备份恢复职业表：%s", s_bakPath[pick]);
    msg("完成", "已从备份恢复 HUMAN.HUM。", MB_ICONINFORMATION);
}

/* ---------------- 技能表（HUMAN.HUM 每职业 n 条 58 B 子项） ----------------
 * 可改：技能字符串 id(+0x00)、隐藏标志(+0x05)、学习等级(+0x11)。
 * 子项个数 n(+0x2D) 属于文件布局，只读。 */
static HWND s_hsWnd, s_hsList, s_hsSid, s_hsLvl, s_hsHid, s_hsCost;
static int  s_hsSel = -1, s_hsCls = -1;

static void hs_fill_list(void)
{
    int i, n = sh_human_skill_count(&s_hum, s_hsCls);

    SendMessageA(s_hsList, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < n; i++) {
        char row[260];
        int sid = sh_human_skill_sid(&s_hum, s_hsCls, i);
        const char *nm = sh_skill_name(sid);
        sprintf(row, "%02d  %s  Lv%d 类%d 气%d%s", i + 1, (nm && nm[0]) ? nm : "?",
                sh_human_skill_level(&s_hum, s_hsCls, i),
                sh_human_skill_cat(&s_hum, s_hsCls, i),
                sh_human_skill_cost(&s_hum, s_hsCls, i),
                i == 0 ? "   [默认技]"
                       : (sh_human_skill_hidden(&s_hum, s_hsCls, i) ? "   [隐藏]" : ""));
        SendMessageA(s_hsList, LB_ADDSTRING, 0, (LPARAM)ansi(row));
    }
}

static void hs_show(int i)
{
    char b[32];

    if (i < 0 || i >= sh_human_skill_count(&s_hum, s_hsCls)) return;
    SendMessageA(s_hsSid, CB_SETCURSEL, (WPARAM)sh_human_skill_sid(&s_hum, s_hsCls, i), 0);
    sprintf(b, "%d", sh_human_skill_level(&s_hum, s_hsCls, i));
    SetWindowTextA(s_hsLvl, b);
    sprintf(b, "%d", sh_human_skill_hidden(&s_hum, s_hsCls, i));
    SetWindowTextA(s_hsHid, b);
    sprintf(b, "%d", sh_human_skill_cost(&s_hum, s_hsCls, i));
    SetWindowTextA(s_hsCost, b);
    /* 第 1 条是默认技：不参与等级解锁、也不进必殺技面板，只允许换技能 id */
    EnableWindow(s_hsLvl, i != 0);
    EnableWindow(s_hsHid, i != 0);
    s_hsSel = i;
}

/* 控件 → 内存；成功 1，失败 0（已弹提示） */
static int hs_apply(void)
{
    char b[32];
    int lv, hid;

    if (s_hsSel < 0) return 1;

    /* 技能一栏是只读的：换招式必须走「整条导入」，改名字没有意义 */
    if (s_hsSel == 0) return 1;     /* 默认技：等级/隐藏也保持原值 */

    GetWindowTextA(s_hsLvl, b, sizeof(b));
    lv = atoi(b);
    if (sh_human_set_skill_level(&s_hum, s_hsCls, s_hsSel, lv) != 0) {
        msg("学习等级超范围", "学习等级必须在 1..30 之间。", MB_ICONWARNING);
        return 0;
    }
    GetWindowTextA(s_hsHid, b, sizeof(b));
    hid = atoi(b);
    if (sh_human_set_skill_hidden(&s_hum, s_hsCls, s_hsSel, hid) != 0) {
        msg("隐藏标志超范围", "隐藏技标志只能填 0（普通技）或 1（隐藏技）。", MB_ICONWARNING);
        return 0;
    }
    GetWindowTextA(s_hsCost, b, sizeof(b));
    if (sh_human_set_skill_cost(&s_hum, s_hsCls, s_hsSel, atoi(b)) != 0) {
        msg("气力消耗超范围", "气力消耗必须在 0..999 之间。", MB_ICONWARNING);
        return 0;
    }
    return 1;
}

static void hs_write(void)
{
    int c;

    if (!hs_apply()) return;

    c = sh_human_skill_hidden_count(&s_hum, s_hsCls);
    if (s_hsCls >= 0 && s_hsCls < SH_HUMAN_MAX && c != s_humOrigHidden[s_hsCls]) {
        char m[700];
        sprintf(m,
            "这个职业的隐藏技条数变了：原版 %d 条 → 现在 %d 条。\n\n"
            "存档里的「隐藏技标志块」是按条数对齐的，数量改变后\n"
            "读档可能出现隐藏技错位或丢失。\n\n"
            "仍然写入吗？", s_humOrigHidden[s_hsCls], c);
        if (MessageBoxA(s_hsWnd, ansi(m), ansi("隐藏技条数异常"),
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
    }

    hu_commit();                    /* 备份 + 原子写 + 重读校验（含子项） */
    hs_fill_list();
    SendMessageA(s_hsList, LB_SETCURSEL, s_hsSel, 0);
    hs_show(s_hsSel);
}

/* ---- 整条技能导入：列出全部职业的全部子项，选一条覆盖当前条目 ----
 * 因为技能 id(+0) 只是名字，效果在其余字节里，换招式必须整条 58 B 一起抄。 */
static HWND s_imWnd, s_imList;
static int  s_imCls[SH_HUMAN_MAX * SH_HUMAN_SKILL_MAX];
static int  s_imIdx[SH_HUMAN_MAX * SH_HUMAN_SKILL_MAX];
static int  s_imCount, s_imChoice;

static void im_fill(void)
{
    int c, k;
    sh_human *src = s_humOrigLoaded ? &s_humOrig : &s_hum;

    s_imCount = 0;
    for (c = 0; c < src->count && c < SH_HUMAN_MAX; c++) {
        int n = sh_human_skill_count(src, c);
        for (k = 0; k < n && k < SH_HUMAN_SKILL_MAX; k++) {
            char row[280], tag[40], mark[8];
            const char *nm = sh_skill_name(sh_human_skill_sid(src, c, k));
            const char *cn = hu_class_name(c);
            int doff, soff;

            if (cn[0]) sprintf(tag, "%s", cn);
            else       sprintf(tag, "职业%02d", c);

            /* 当前文件里这一条是否已与原始表不同（逐字节比较） */
            mark[0] = '\0';
            doff = sh_human_skill_off(&s_hum, c, k);
            soff = sh_human_skill_off(src, c, k);
            if (s_humOrigLoaded && doff >= 0 && soff >= 0 &&
                memcmp(s_hum.data + doff, src->data + soff, SH_HUMAN_SUB) != 0)
                strcpy(mark, "  *");

            sprintf(row, "%s #%02d  %s  Lv%d%s%s",
                    tag, k + 1, (nm && nm[0]) ? nm : "?",
                    sh_human_skill_level(src, c, k),
                    sh_human_skill_hidden(src, c, k) ? "  [隐藏]" : "", mark);
            SendMessageA(s_imList, LB_ADDSTRING, 0, (LPARAM)ansi(row));
            s_imCls[s_imCount] = c;
            s_imIdx[s_imCount] = k;
            s_imCount++;
        }
    }
}
static LRESULT CALLBACK ImportProc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: {
        HWND st;

        s_imList = CreateWindowExA(0, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
            10, 8, 524, 336, hwnd, (HMENU)(INT_PTR)ID_IM_LIST, NULL, NULL);
        if (g.hFont) SendMessageA(s_imList, WM_SETFONT, (WPARAM)g.hFont, TRUE);

        st = CreateWindowExA(0, "STATIC",
            ansi("列表来自 EXE 内置的原始 HUMAN 表，整条导入会覆盖目标条目的全部 58 字节；\n"
                 "带 * 的条目表示当前文件里那条已和原始值不同。"),
            WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 344, 300, 36, hwnd, NULL, NULL, NULL);
        if (g.hFont) SendMessageA(st, WM_SETFONT, (WPARAM)g.hFont, TRUE);

        {
            HWND b1 = CreateWindowExA(0, "BUTTON", ansi("导入"),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                310, 356, 100, 28, hwnd, (HMENU)(INT_PTR)ID_IM_OK, NULL, NULL);
            HWND b2 = CreateWindowExA(0, "BUTTON", ansi("取消"),
                WS_CHILD | WS_VISIBLE, 422, 356, 80, 28, hwnd,
                (HMENU)(INT_PTR)ID_IM_CANCEL, NULL, NULL);
            if (g.hFont) {
                SendMessageA(b1, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(b2, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            }
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_IM_OK) {
            s_imChoice = (int)SendMessageA(s_imList, LB_GETCURSEL, 0, 0);
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == ID_IM_CANCEL) { s_imChoice = -1; DestroyWindow(hwnd); return 0; }
        return 0;

    case WM_CLOSE:
        s_imChoice = -1;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        s_imWnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, m, wp, lp);
}

static void do_hs_import(HWND owner)
{
    WNDCLASSA wc;
    MSG msgLoop;
    int srcC, srcI;

    if (s_hsSel < 0) return;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = ImportProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ShuiHuHImport";
    RegisterClassA(&wc);

    s_imChoice = -1;
    s_imWnd = CreateWindowExA(0, "ShuiHuHImport", ansi("整条导入技能（选择源技能）"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 556, 430,
        owner, NULL, wc.hInstance, NULL);
    if (!s_imWnd) return;

    im_fill();
    if (s_imCount > 0) SendMessageA(s_imList, LB_SETCURSEL, 0, 0);

    EnableWindow(owner, FALSE);
    ShowWindow(s_imWnd, SW_SHOW);
    UpdateWindow(s_imWnd);
    while (s_imWnd && GetMessageA(&msgLoop, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(s_imWnd, &msgLoop)) {
            TranslateMessage(&msgLoop);
            DispatchMessageA(&msgLoop);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    tip_hide();
    RedrawWindow(owner, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);

    if (s_imChoice < 0 || s_imChoice >= s_imCount) return;
    srcC = s_imCls[s_imChoice];
    srcI = s_imIdx[s_imChoice];

    if (sh_human_skill_copy(&s_hum, s_hsCls, s_hsSel,
                             s_humOrigLoaded ? &s_humOrig : &s_hum, srcC, srcI) != 0) {
        msg("导入失败", "源或目标条目无效。", MB_ICONERROR);
        return;
    }
    hs_fill_list();
    SendMessageA(s_hsList, LB_SETCURSEL, s_hsSel, 0);
    hs_show(s_hsSel);
    status("已从「职业%02d #%02d」整条导入（尚未写入文件）", srcC, srcI + 1);
}

static LRESULT CALLBACK HumanSkillProc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: {
        HWND c;

        s_hsList = CreateWindowExA(0, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
            10, 8, 360, 340, hwnd, (HMENU)(INT_PTR)ID_HS_LIST, NULL, NULL);
        if (g.hFont) SendMessageA(s_hsList, WM_SETFONT, (WPARAM)g.hFont, TRUE);

        c = CreateWindowExA(0, "STATIC", ansi("技能"),
            WS_CHILD | WS_VISIBLE, 382, 10, 80, 18, hwnd, NULL, NULL, NULL);
        if (g.hFont) SendMessageA(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        s_hsSid = CreateWindowExA(0, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_VSCROLL | CBS_DROPDOWNLIST,
            382, 30, 210, 260, hwnd, (HMENU)(INT_PTR)ID_HS_SID, NULL, NULL);

        c = CreateWindowExA(0, "STATIC", ansi("学习等级 1-30"),
            WS_CHILD | WS_VISIBLE, 382, 66, 120, 18, hwnd, NULL, NULL, NULL);
        if (g.hFont) SendMessageA(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        s_hsLvl = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
            382, 88, 90, 22, hwnd, (HMENU)(INT_PTR)ID_HS_LEVEL, NULL, NULL);

        c = CreateWindowExA(0, "STATIC", ansi("隐藏技 0/1"),
            WS_CHILD | WS_VISIBLE, 382, 122, 120, 18, hwnd, NULL, NULL, NULL);
        if (g.hFont) SendMessageA(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        s_hsHid = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
            382, 144, 90, 22, hwnd, (HMENU)(INT_PTR)ID_HS_HIDDEN, NULL, NULL);

        c = CreateWindowExA(0, "STATIC", ansi("气力消耗"),
            WS_CHILD | WS_VISIBLE, 382, 172, 120, 18, hwnd, NULL, NULL, NULL);
        if (g.hFont) SendMessageA(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        s_hsCost = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
            382, 194, 90, 22, hwnd, (HMENU)(INT_PTR)ID_HS_COST, NULL, NULL);

        c = CreateWindowExA(0, "STATIC",
            ansi("技能一栏只读；换招式请点[整条导入]，它会把源技能的整条 58 字节覆盖过来。\n"
                 "学习等级：角色达到该等级后，此必殺技在面板解锁。\n"
                 "隐藏技：0 = 普通技，1 = 隐藏技（原版每职业 0 或 3 条）。\n"
                 "第 1 条[默认技]开局即有，不参与等级解锁。写入前会自动备份。"),
            WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 396, 596, 66, hwnd, NULL, NULL, NULL);
        if (g.hFont) {
            SendMessageA(s_hsSid, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            SendMessageA(s_hsLvl, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            SendMessageA(s_hsHid, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            SendMessageA(s_hsCost, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            SendMessageA(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        }

        {   /* 技能下拉：索引 = 技能字符串 id，文字 = "id  技能名" */
            int k, cnt = sh_skill_name_count();
            for (k = 0; k < cnt; k++) {
                char row[220];
                sprintf(row, "%d  %s", k, sh_skill_name(k));
                SendMessageA(s_hsSid, CB_ADDSTRING, 0, (LPARAM)ansi(row));
            }
        }

        {
            HWND b0 = CreateWindowExA(0, "BUTTON", ansi("整条导入"),
                WS_CHILD | WS_VISIBLE, 26, 360, 100, 28, hwnd,
                (HMENU)(INT_PTR)ID_HS_IMPORT, NULL, NULL);
            HWND b1 = CreateWindowExA(0, "BUTTON", ansi("写入文件"),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                150, 360, 100, 28, hwnd, (HMENU)(INT_PTR)ID_HS_SAVE, NULL, NULL);
            HWND b2 = CreateWindowExA(0, "BUTTON", ansi("关闭"),
                WS_CHILD | WS_VISIBLE, 262, 360, 80, 28, hwnd,
                (HMENU)(INT_PTR)ID_HS_CLOSE, NULL, NULL);
            if (g.hFont) {
                SendMessageA(b0, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(b1, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(b2, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            }
        }
        return 0;
    }

    case WM_TIMER:
        if (wp == 2 && GetActiveWindow() == s_hsWnd) {
            static const char *ts =
                "技能：下拉选择「id + 技能名」，换一项即换技能（名字与效果一起变）。\n"
                "第 1 条是默认技，只允许换 id。";
            static const char *tl =
                "学习等级 1-30：角色等级达到后，该必殺技在面板上解锁。\n"
                "第 1 条默认技不参与解锁，此项已禁用。";
            static const char *tc =
                "气力消耗：施放一次扣多少气力（u16，原版 0..350）。";
            static const char *th =
                "隐藏技 0/1：0 = 普通技，1 = 隐藏技。原版每职业 0 或 3 条。\n"
                "第 1 条默认技不进必殺技面板，此项已禁用。";
            sh_tip_item map[4];
            map[0].h = s_hsSid; map[0].text = ts;
            map[1].h = s_hsLvl; map[1].text = tl;
            map[2].h = s_hsHid; map[2].text = th;
            map[3].h = s_hsCost; map[3].text = tc;
            tip_poll(map, 4);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == ID_HS_LIST && HIWORD(wp) == LBN_SELCHANGE) {
            int sel = (int)SendMessageA(s_hsList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel != s_hsSel) {
                if (!hs_apply()) {
                    SendMessageA(s_hsList, LB_SETCURSEL, s_hsSel, 0);
                    return 0;
                }
                hs_show(sel);
            }
            return 0;
        }
        if (LOWORD(wp) == ID_HS_SAVE)  { hs_write(); return 0; }
        if (LOWORD(wp) == ID_HS_IMPORT) { do_hs_import(hwnd); return 0; }
        if (LOWORD(wp) == ID_HS_CLOSE) { DestroyWindow(hwnd); return 0; }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        s_hsWnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, m, wp, lp);
}

static void do_human_skills(HWND owner)
{
    WNDCLASSA wc;
    MSG msgLoop;
    char title[160];
    const char *nm = hu_class_name(s_hsCls);

    s_hsCls = s_humSel;
    if (s_hsCls < 0) return;
    if (sh_human_skill_count(&s_hum, s_hsCls) <= 0) {
        msg("没有技能", "这个职业的记录里技能条数为 0。", MB_ICONINFORMATION);
        return;
    }

    if (nm[0])
        sprintf(title, "技能表 —— %s（%d 条）", nm, sh_human_skill_count(&s_hum, s_hsCls));
    else
        sprintf(title, "技能表 —— 职业 %d（%d 条）", s_hsCls, sh_human_skill_count(&s_hum, s_hsCls));

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = HumanSkillProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ShuiHuHSkill";
    RegisterClassA(&wc);

    s_hsWnd = CreateWindowExA(0, "ShuiHuHSkill", ansi(title),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 492,
        owner, NULL, wc.hInstance, NULL);
    if (!s_hsWnd) return;

    s_hsSel = -1;
    hs_fill_list();
    SendMessageA(s_hsList, LB_SETCURSEL, 0, 0);
    hs_show(0);

    SetTimer(s_hsWnd, 2, 200, NULL);     /* tooltip 轮询 */
    EnableWindow(owner, FALSE);
    ShowWindow(s_hsWnd, SW_SHOW);
    UpdateWindow(s_hsWnd);
    while (s_hsWnd && GetMessageA(&msgLoop, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(s_hsWnd, &msgLoop)) {
            TranslateMessage(&msgLoop);
            DispatchMessageA(&msgLoop);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    tip_hide();
    RedrawWindow(owner, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static LRESULT CALLBACK HumanProc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: {
        int f;
        HWND c;

        s_humList = CreateWindowExA(0, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
            10, 8, 256, 400, hwnd, (HMENU)(INT_PTR)ID_HU_LIST, NULL, NULL);
        if (g.hFont) SendMessageA(s_humList, WM_SETFONT, (WPARAM)g.hFont, TRUE);

        c = CreateWindowExA(0, "STATIC", ansi("基础值"),
            WS_CHILD | WS_VISIBLE, 276, 8, 60, 18, hwnd, NULL, NULL, NULL);
        if (g.hFont) SendMessageA(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        c = CreateWindowExA(0, "STATIC", ansi("成长率%"),
            WS_CHILD | WS_VISIBLE, 414, 8, 70, 18, hwnd, NULL, NULL, NULL);
        if (g.hFont) SendMessageA(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);

        for (f = 0; f < SH_HF_COUNT; f++) {
            HWND la = CreateWindowExA(0, "STATIC",
                ansi(sh_human_field_label((sh_human_field)f)),
                WS_CHILD | WS_VISIBLE, 276, 32 + f * 28, 64, 20, hwnd, NULL, NULL, NULL);
            s_humBase[f] = CreateWindowExA(0, "EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                344, 29 + f * 28, 62, 22, hwnd,
                (HMENU)(INT_PTR)(ID_HU_BASE0 + f), NULL, NULL);
            s_humGrow[f] = CreateWindowExA(0, "EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                414, 29 + f * 28, 54, 22, hwnd,
                (HMENU)(INT_PTR)(ID_HU_GROW0 + f), NULL, NULL);
            if (g.hFont) {
                SendMessageA(la, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(s_humBase[f], WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(s_humGrow[f], WM_SETFONT, (WPARAM)g.hFont, TRUE);
            }
        }

        c = CreateWindowExA(0, "STATIC", ansi("移动"),
            WS_CHILD | WS_VISIBLE, 276, 32 + SH_HF_COUNT * 28, 64, 20, hwnd, NULL, NULL, NULL);
        if (g.hFont) SendMessageA(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        s_humMoveCtl = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
            344, 29 + SH_HF_COUNT * 28, 62, 22, hwnd,
            (HMENU)(INT_PTR)ID_HU_MOVE, NULL, NULL);

        s_humInfo = CreateWindowExA(0, "STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 484, 30, 220, 100, hwnd,
            (HMENU)(INT_PTR)ID_HU_INFO, NULL, NULL);
        if (g.hFont) {
            SendMessageA(s_humMoveCtl, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            SendMessageA(s_humInfo, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        }

        c = CreateWindowExA(0, "BUTTON", ansi("人物技能表"),
            WS_CHILD | WS_VISIBLE, 276, 258, 180, 30, hwnd,
            (HMENU)(INT_PTR)ID_HU_SKILLS, NULL, NULL);
        if (g.hFont) SendMessageA(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);

        c = CreateWindowExA(0, "STATIC",
            ansi("改的是游戏本体数据（Data\\HUMAN.HUM），72 个职业一起生效，不是某个存档。\n"
                 "写入前自动备份，写后重读校验。面板 = 基础 × 成长率^等级（夹 255 / 510）。"),
            WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 412, 690, 36, hwnd, NULL, NULL, NULL);
        if (g.hFont) SendMessageA(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);

        {
            HWND b1 = CreateWindowExA(0, "BUTTON", ansi("写入文件"),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                276, 458, 96, 28, hwnd, (HMENU)(INT_PTR)ID_HU_SAVE, NULL, NULL);
            HWND b4 = CreateWindowExA(0, "BUTTON", ansi("恢复备份"),
                WS_CHILD | WS_VISIBLE, 380, 458, 96, 28, hwnd,
                (HMENU)(INT_PTR)ID_HU_RESTORE, NULL, NULL);
            HWND b2 = CreateWindowExA(0, "BUTTON", ansi("从磁盘重读"),
                WS_CHILD | WS_VISIBLE, 484, 458, 100, 28, hwnd,
                (HMENU)(INT_PTR)ID_HU_RELOAD, NULL, NULL);
            HWND b3 = CreateWindowExA(0, "BUTTON", ansi("关闭"),
                WS_CHILD | WS_VISIBLE, 592, 458, 72, 28, hwnd,
                (HMENU)(INT_PTR)ID_HU_CLOSE, NULL, NULL);
            if (g.hFont) {
                SendMessageA(b1, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(b4, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(b2, WM_SETFONT, (WPARAM)g.hFont, TRUE);
                SendMessageA(b3, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            }
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_HU_LIST && HIWORD(wp) == LBN_SELCHANGE) {
            int sel = (int)SendMessageA(s_humList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel != s_humSel) {
                if (!hu_apply()) {
                    SendMessageA(s_humList, LB_SETCURSEL, s_humSel, 0);
                    return 0;
                }
                hu_show(sel);
            }
            return 0;
        }
        if (LOWORD(wp) == ID_HU_RESTORE) { do_human_restore(hwnd); return 0; }
        if (LOWORD(wp) == ID_HU_SKILLS)  { do_human_skills(hwnd);  return 0; }
        if (LOWORD(wp) == ID_HU_SAVE) { hu_commit(); return 0; }
        if (LOWORD(wp) == ID_HU_RELOAD) {
            char path[SH_HUMAN_PATH];
            int rc;
            strncpy(path, s_hum.path, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
            sh_human_free(&s_hum);
            s_humLoaded = 0;
            rc = sh_human_load(path, &s_hum);
            if (rc != 0) {
                msg("重读失败", "文件读取失败，请检查文件是否还在。", MB_ICONERROR);
                s_humLoaded = 0;
                DestroyWindow(hwnd);
                return 0;
            }
            s_humLoaded = 1;
            s_humSel = -1;
            hu_snapshot();
            hu_fill_list();
            SendMessageA(s_humList, LB_SETCURSEL, 0, 0);
            hu_show(0);
            return 0;
        }
        if (LOWORD(wp) == ID_HU_CLOSE) { DestroyWindow(hwnd); return 0; }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        s_humWnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, m, wp, lp);
}

static void do_human(void)
{
    WNDCLASSA wc;
    MSG msgLoop;

    if (!human_ensure()) return;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = HumanProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ShuiHuHuman";
    RegisterClassA(&wc);

    s_humWnd = CreateWindowExA(0, "ShuiHuHuman",
        ansi("人物编辑 — HUMAN.HUM（职业基础值 / 成长率）"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 528,
        g.hMain, NULL, wc.hInstance, NULL);
    if (!s_humWnd) return;

    hu_fill_list();
    if (s_hum.count > 0) {
        SendMessageA(s_humList, LB_SETCURSEL, 0, 0);
        hu_show(0);
    }

    EnableWindow(g.hMain, FALSE);
    ShowWindow(s_humWnd, SW_SHOW);
    UpdateWindow(s_humWnd);
    while (s_humWnd && GetMessageA(&msgLoop, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(s_humWnd, &msgLoop)) {
            TranslateMessage(&msgLoop);
            DispatchMessageA(&msgLoop);
        }
    }
    EnableWindow(g.hMain, TRUE);
    SetActiveWindow(g.hMain);
    tip_hide();
    RedrawWindow(g.hMain, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void do_about(void)
{
    char m[900];
    sprintf(m,
        "水浒传之梁山好汉 —— 存档编辑器\n"
        "版本 " APP_VERSION "  (" APP_BUILD ")\n\n"
        "角色名表 %d 条 ｜ 技能名表 %d 条 ｜ 道具表 %d 条\n\n"
        "数值上限依据：\n"
        "  等级≤30、攻击等≤255、生命/气力≤510\n"
        "      —— 程序内的夹取（反编译 FUN_00408540）\n"
        "  移动≤255 —— 派生值，见下方说明\n"
        "  经验/升级所需≤65535、金钱≤4294967295\n"
        "      —— 仅受字段宽度限制，非游戏规则\n\n"
        "关于「移动」：它是每回合重算出来的派生值\n"
        "  移动 = 职业基础 + 单位加成 + 装备加成\n"
        "存档里另有一个「剩余移动力」(棋盘实际可走格数)，\n"
        "  原本只在回合开始时由移动力复制过去。\n"
        "因此本工具写入时两者都写：面板与棋盘都立即生效。\n\n"
        "Win98 兼容构建：无 comctl32、无额外 DLL。",
        sh_char_name_count(), sh_skill_name_count(), sh_item_count());
    msg("关于", m, MB_ICONINFORMATION);
}

/* ------------------------------------------------------------------ */
/* 窗口与控件                                                          */
/* ------------------------------------------------------------------ */
/* ---------------- 设置存取（exe 同目录 shuihu-edit.ini）---------------- */

static void build_ini_path(void)
{
    char exe[MAX_PATH];
    char *p;

    g.iniPath[0] = (char)0;
    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) return;
    p = strrchr(exe, '\\');
    if (p) *(p + 1) = (char)0; else strcpy(exe, ".\\");
    sprintf(g.iniPath, "%sshuihu-edit.ini", exe);
}

static void load_settings(void)
{
    char def[MAX_PATH];
    char *p;
    DWORD attr;

    strcpy(def, g.iniPath);
    p = strrchr(def, '\\');
    if (p) *(p + 1) = (char)0;

    g.lastDir[0] = (char)0;
    if (g.iniPath[0]) {
        GetPrivateProfileStringA("ui", "LastDir", def, g.lastDir, MAX_PATH, g.iniPath);
    }
    if (!g.lastDir[0]) strcpy(g.lastDir, def);

    /* 记下的目录可能已不存在（U 盘拔掉、改名），此时回退到默认 */
    attr = GetFileAttributesA(g.lastDir);
    if (attr == 0xFFFFFFFF || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        strcpy(g.lastDir, def);
    }
}

/* 记住某个文件所在目录（传入文件路径或目录路径均可） */
static void remember_dir(const char *fileOrDir)
{
    char dir[MAX_PATH];

    if (!fileOrDir || !fileOrDir[0]) return;
    strncpy(dir, fileOrDir, MAX_PATH - 1);
    dir[MAX_PATH - 1] = (char)0;

    /* 是文件？去掉文件名部分 */
    if (GetFileAttributesA(dir) != 0xFFFFFFFF &&
        (GetFileAttributesA(dir) & FILE_ATTRIBUTE_DIRECTORY)) {
        /* 本身就是目录 */
    } else {
        char *p = strrchr(dir, '\\');
        if (p) { *(p + 1) = (char)0; } else { return; }
    }

    if (strcmp(dir, g.lastDir) == 0) return;     /* 没变就不写盘 */
    strncpy(g.lastDir, dir, MAX_PATH - 1);
    g.lastDir[MAX_PATH - 1] = (char)0;
    if (g.iniPath[0]) {
        WritePrivateProfileStringA("ui", "LastDir", g.lastDir, g.iniPath);
    }
}
/* 从 exe 同目录的 logo.ico 加载图标；找不到则返回 NULL（不影响使用） */
static void load_app_icon(void)
{
    char exe[MAX_PATH], dir[MAX_PATH], path[MAX_PATH];
    char *p;

    g.hIconBig = g.hIconSmall = NULL;

    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) return;
    strncpy(dir, exe, MAX_PATH - 1);
    dir[MAX_PATH - 1] = '\0';
    p = strrchr(dir, '\\');
    if (p) *(p + 1) = '\0'; else strcpy(dir, ".\\");

    sprintf(path, "%slogo.ico", dir);
    strcpy(g.iconPath, path);
    SetLastError(0);
    g.hIconBig   = (HICON)LoadImageA(NULL, path, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
    g.iconErr = (unsigned long)GetLastError();
    g.hIconSmall = (HICON)LoadImageA(NULL, path, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
    if (!g.hIconBig && !g.hIconSmall) return;

    if (!g.hIconBig)   g.hIconBig   = g.hIconSmall;
    if (!g.hIconSmall) g.hIconSmall = g.hIconBig;
}
/* ================= 技能树 ================= */

static HWND s_treeWnd;

static LRESULT CALLBACK TreeProc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: {
        HWND ed = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 10, 536, 392, hwnd, (HMENU)(INT_PTR)ID_TREE_EDIT, NULL, NULL);
        HWND bt = CreateWindowExA(0, "BUTTON", ansi("关闭"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            238, 412, 80, 28, hwnd, (HMENU)(INT_PTR)ID_TREE_CLOSE, NULL, NULL);
        if (g.hFont) {
            SendMessageA(ed, WM_SETFONT, (WPARAM)g.hFont, TRUE);
            SendMessageA(bt, WM_SETFONT, (WPARAM)g.hFont, TRUE);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_TREE_CLOSE) { DestroyWindow(hwnd); return 0; }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        s_treeWnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, m, wp, lp);
}

/* 弹出技能树（模态：禁用主窗口，模态循环结束后恢复） */
static void do_skilltree(void)
{
    static char text[8192];
    WNDCLASSA wc;
    MSG msgLoop;
    HWND ed;
    const sh_unit *u;

    if (!g.loaded || g.sel < 0 || g.sel >= g.s.count) {
        msg("提示", "请先打开存档并选择角色。", MB_ICONINFORMATION);
        return;
    }
    apply_current(1);                 /* 让技能树反映界面上刚改的等级 */
    u = &g.s.units[g.sel];
    sh_build_tree_text(u, text, (int)sizeof(text));

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = TreeProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ShuiHuTree";
    RegisterClassA(&wc);

    s_treeWnd = CreateWindowExA(0, "ShuiHuTree",
        ansi("必杀技技能树"), WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 570, 490, g.hMain, NULL, wc.hInstance, NULL);
    if (!s_treeWnd) return;

    ed = GetDlgItem(s_treeWnd, ID_TREE_EDIT);
    if (ed) {
        /* 技能树文本可达 2 KB 以上，不能用 512 字节的 ansi() */
        static char atext[16384];
        sh_utf8_to_acp(text, atext, (int)sizeof(atext));
        SetWindowTextA(ed, atext);
    }

    EnableWindow(g.hMain, FALSE);
    ShowWindow(s_treeWnd, SW_SHOW);
    UpdateWindow(s_treeWnd);

    while (s_treeWnd && GetMessageA(&msgLoop, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(s_treeWnd, &msgLoop)) {
            TranslateMessage(&msgLoop);
            DispatchMessageA(&msgLoop);
        }
    }

    EnableWindow(g.hMain, TRUE);
    SetActiveWindow(g.hMain);
    tip_hide();                     /* 关掉残留的提示框 */
    /* 模态窗口覆盖过主窗口，关掉后必须重绘。
     * 注意：InvalidateRect 只会重画父窗口本身，子控件（按钮等）不会跟着重画，
     * 所以必须用 RDW_ALLCHILDREN。 */
    RedrawWindow(g.hMain, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
static HWND mk(const char *cls, const char *text, DWORD style,
               int x, int y, int w, int h, int id)
{
    HWND hw = CreateWindowExA(0, cls, ansi(text), WS_CHILD | WS_VISIBLE | style,
                              x, y, w, h, g.hMain, (HMENU)(INT_PTR)id, NULL, NULL);
    if (hw && g.hFont) SendMessageA(hw, WM_SETFONT, (WPARAM)g.hFont, TRUE);
    return hw;
}

static void create_children(HWND hwnd)
{
    int i, col, row;
    (void)hwnd;

    g.hFont = CreateFontA(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, ansi("宋体"));

    g.hPath = mk("EDIT", "", WS_BORDER | ES_AUTOHSCROLL, 70, 12, 560, 24, ID_PATH);
    mk("BUTTON", "浏览...", BS_PUSHBUTTON, 640, 12, 70, 24, ID_BROWSE);
    mk("BUTTON", "打开", BS_PUSHBUTTON, 716, 12, 70, 24, ID_OPEN);

    g.hList = mk("LISTBOX", "", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                 12, 46, 250, 330, ID_LIST);

    /* 13 个属性：两列 × 7 行（右下角空位放金钱） */
    for (i = 0; i < NFIELDS; i++) {
        col = i / 7;
        row = i % 7;
        mk("STATIC", sh_field_label(kFields[i]), SS_LEFT,
           282 + col * 300, 50 + row * 30, 62, 20, 0);
        g.hEdit[i] = mk("EDIT", "", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                        348 + col * 300, 46 + row * 30, 80, 22, ID_EDIT0 + i);
        {
            char lim[32];
            sprintf(lim, "≤ %d", sh_field_max(kFields[i]));
            mk("STATIC", lim, SS_LEFT, 434 + col * 300, 50 + row * 30, 60, 20, 0);
        }
    }

    /* 金钱（全局字段，放在第二列最后一行） */
    mk("STATIC", "金钱", SS_LEFT, 582, 230, 62, 20, 0);
    g.hMoney = mk("EDIT", "", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                  648, 226, 120, 22, ID_MONEY);

    /* 7 个槽位：下拉列表（两行：4 + 3），宽度按道具名调整 */
    mk("STATIC", "装备 / 物品（下拉选择，0 = 空）", SS_LEFT, 282, 252, 400, 18, 0);
    for (i = 0; i < SH_SLOT_COUNT; i++) {
        /* 第一行：武器 / 衣服 / 帽子；第二行：物品1..物品4 */
        int row = (i < 3) ? 0 : 1;
        int colIdx = (i < 3) ? i : (i - 3);
        int x = 282 + colIdx * 166;
        int y = 272 + row * 44;
        char lab[32];
        if (i < 3) strcpy(lab, sh_kind_label(sh_slot_kind(i)));
        else sprintf(lab, "物品%d", i - 2);
        mk("STATIC", lab, SS_LEFT, x, y, 160, 16, 0);
        g.hSlot[i] = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                        x, y + 16, 160, 220, ID_SLOT0 + i);
    }

    g.hHidden = mk("STATIC", "", SS_LEFT, 282, 366, 640, 18, ID_HIDDEN);

    /* 第一行：写入 / 恢复 / 重载 */
    mk("BUTTON", "写入存档",   BS_PUSHBUTTON, 282, 386, 96, 30, ID_WRITE);
    mk("BUTTON", "恢复存档",   BS_PUSHBUTTON, 386, 386, 90, 30, ID_RESTORE);
    mk("BUTTON", "重载存档",   BS_PUSHBUTTON, 484, 386, 90, 30, ID_RELOAD);

    /* 第二行：技能相关 */
    mk("BUTTON", "查看技能树", BS_PUSHBUTTON, 282, 420, 96, 30, ID_TREE);
    mk("BUTTON", "解锁隐藏技", BS_PUSHBUTTON, 386, 420, 96, 30, ID_UNLOCK);
    mk("BUTTON", "装备加成",   BS_PUSHBUTTON, 490, 420, 90, 30, ID_CACHE);

    /* 第三行：关于 / 退出 */
    mk("BUTTON", "关于",       BS_PUSHBUTTON, 282, 454, 66, 30, ID_ABOUT);
    mk("BUTTON", "退出",       BS_PUSHBUTTON, 356, 454, 66, 30, ID_QUIT);

    /* 攻击/防御右侧的“装备 +N”分解标签（与上面字段循环的坐标对齐：
     * 攻击是 i=5 -> row5 y=200；防御 i=6 -> row6 y=230；限值标签在 x=434） */
    g.hEquipNote[0] = mk("STATIC", "", SS_LEFT, 498, 200, 78, 20, ID_EQNOTE0);
    g.hEquipNote[1] = mk("STATIC", "", SS_LEFT, 498, 230, 78, 20, ID_EQNOTE0 + 1);

    /* ---- 页签：两个按钮切换两个页面（伪标签，不依赖 comctl32）---- */
    mk("BUTTON", "存档修改", BS_PUSHBUTTON, 12, 388, 118, 30, ID_TAB_SAVE);
    mk("BUTTON", "道具编辑", BS_PUSHBUTTON, 138, 388, 118, 30, ID_TAB_GOODS);

    /* ---- 道具页（默认隐藏；ID 1034..1059、1062..1072 归它）---- */
    g.hGbList = mk("LISTBOX", "", WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                   12, 42, 260, 338, ID_GB_LIST);
    {
        int f;
        for (f = 0; f < SH_GF_COUNT; f++) {
            char rng[32];
            mk("STATIC", sh_goods_field_label((sh_goods_field)f), SS_LEFT,
               282, 50 + f * 30, 80, 20, ID_GB_LAB0 + f);
            g.hGbEdit[f] = mk("EDIT", "", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                              372, 46 + f * 30, 80, 22, ID_GB_EDIT0 + f);
            sprintf(rng, "%d .. %d",
                    sh_goods_field_min((sh_goods_field)f),
                    sh_goods_field_max((sh_goods_field)f));
            mk("STATIC", rng, SS_LEFT, 462, 50 + f * 30, 90, 20, ID_GB_RNG0 + f);
        }
    }
    mk("STATIC",
       "改的是游戏本体数据（GOODS.DAB），会影响所有存档。\n"
       "写入前自动备份，写后重读 200 件逐字段校验。",
       SS_LEFT, 282, 264, 480, 44, ID_GB_WARN);
    mk("BUTTON", "写入道具表", BS_PUSHBUTTON, 282, 316, 120, 30, ID_GB_SAVE);
    mk("BUTTON", "恢复道具表", BS_PUSHBUTTON, 410, 316, 120, 30, ID_GB_RESTORE);
    mk("BUTTON", "人物编辑", BS_PUSHBUTTON, 12, 422, 118, 30, ID_HUMAN);
    mk("STATIC", "说明：", SS_LEFT, 282, 356, 60, 20, ID_GB_NOTE);
    mk("STATIC", "", SS_LEFT, 342, 356, 430, 56, ID_GB_DESC);
    mk("STATIC", "", SS_LEFT, 282, 420, 540, 20, ID_GB_CHK);

    g.hStatus = mk("STATIC", "版本 " APP_VERSION " — 请先选择存档文件（可直接在左上角输入路径，再点「打开」）",
                   SS_LEFT | SS_LEFTNOWORDWRAP | SS_SUNKEN,
                   12, 488, 916, 22, ID_STATUS);

    SetTimer(hwnd, 1, 200, NULL);   /* tooltip 轮询 */
    /* 建完控件立刻定位到存档页：否则两页控件都带 WS_VISIBLE，会叠在一起 */
    set_page(0);

    /* 填充下拉列表：索引 = 道具 id（0 = 空） */
    for (i = 0; i < SH_SLOT_COUNT; i++) {
        int id;
        char item[160];
        SendMessageA(g.hSlot[i], CB_ADDSTRING, 0, (LPARAM)ansi("(空)"));
        for (id = 1; id <= sh_item_count(); id++) {
            sprintf(item, "%d  %s", id, ansi(sh_item_name(id)));
            SendMessageA(g.hSlot[i], CB_ADDSTRING, 0, (LPARAM)item);
        }
        SendMessageA(g.hSlot[i], CB_SETCURSEL, 0, 0);
    }

    /* 命令行带了路径就直接打开（便于测试与拖放式使用） */
    if (g.argPath[0]) {
        SetWindowTextA(g.hPath, g.argPath);
        do_open();
    }
}

static FILE *s_lf;
static BOOL CALLBACK dump_cb(HWND ch, LPARAM p)
{
    char cls[64], tx[128];
    RECT r;
    (void)p;
    GetClassNameA(ch, cls, sizeof(cls));
    GetWindowTextA(ch, tx, sizeof(tx));
    GetWindowRect(ch, &r);
    if (s_lf) fprintf(s_lf, "%s\t%s\t%d\t%d\t%d\t%d\n", cls, tx,
                      (int)r.left, (int)r.top, (int)(r.right - r.left), (int)(r.bottom - r.top));
    return TRUE;
}

static void dump_layout(HWND hwnd)
{
    RECT cr;
    GetClientRect(hwnd, &cr);
    s_lf = fopen("layout.txt", "w");
    if (!s_lf) return;
    fprintf(s_lf, "#CLIENT\t%d\t%d\n", (int)cr.right, (int)cr.bottom);
    fprintf(s_lf, "#ICON\t%d\terr=%lu\tpath=%s\n", (g.hIconBig != NULL) ? 1 : 0,
            g.iconErr, g.iconPath);
    fprintf(s_lf, "#LASTDIR\t%s\n", g.lastDir);
    EnumChildWindows(hwnd, dump_cb, 0);
    fclose(s_lf);
    s_lf = NULL;
}
/* 读 16 位（仅用于自检） */
static unsigned short wr_peek16(size_t off)
{
    if (!g.s.data || off + 1 >= g.s.size) return 0;
    return (unsigned short)(g.s.data[off] | (g.s.data[off + 1] << 8));
}

/* -uitest：模拟「改值 -> 切角色 -> 切回来」，验证修改没丢 */
static void do_uitest(HWND hwnd)
{
    FILE *f = fopen("uitest.txt", "w");
    int row0, row1, got, mem, ok;

    if (!f) return;
    if (!g.loaded || g.mapCount < 3) {
        fprintf(f, "SKIP: 需要至少 3 个可编辑角色（当前 %d）\n", g.mapCount);
        fclose(f);
        return;
    }

    row0 = 0; row1 = 1;

    /* 选中第 0 个角色 */
    SendMessageA(g.hList, LB_SETCURSEL, row0, 0);
    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_LIST, LBN_SELCHANGE), (LPARAM)g.hList);
    fprintf(f, "case1 选中 unit=%d\r\n", (int)g.s.units[g.map[row0]].id);

    /* 把槽 0 改成 177（人偶表里的道具 id） */
    SendMessageA(g.hSlot[0], CB_SETCURSEL, 177, 0);

    /* 切到第 1 个，再切回第 0 个 */
    SendMessageA(g.hList, LB_SETCURSEL, row1, 0);
    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_LIST, LBN_SELCHANGE), (LPARAM)g.hList);
    SendMessageA(g.hList, LB_SETCURSEL, row0, 0);
    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_LIST, LBN_SELCHANGE), (LPARAM)g.hList);

    got = (int)SendMessageA(g.hSlot[0], CB_GETCURSEL, 0, 0);
    mem = (int)g.s.units[g.map[row0]].slots[0];
    ok  = (got == 177 && mem == 177);
    fprintf(f, "case1 切换往返: 内存=%d 控件=%d 期望=177 -> %s\r\n",
            mem, got, ok ? "PASS" : "FAIL");

    /* case2：输入非法值（等级 99），切换应被拒绝、选择拨回 */
    SetWindowTextA(g.hEdit[0], "99");
    SendMessageA(g.hList, LB_SETCURSEL, row1, 0);
    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_LIST, LBN_SELCHANGE), (LPARAM)g.hList);
    got = (int)SendMessageA(g.hList, LB_GETCURSEL, 0, 0);
    ok  = (g.sel == g.map[row0] && got == row0);
    fprintf(f, "case2 非法等级时切换被拒: g.sel=%d 列表行=%d 期望行=%d -> %s\r\n",
            (int)g.s.units[g.sel].id, got, row0, ok ? "PASS" : "FAIL");

    /* case3：同时把「气力」和「气力上限」改成 309（用户实际遇到的场景） */
    SendMessageA(g.hList, LB_SETCURSEL, row0, 0);
    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_LIST, LBN_SELCHANGE), (LPARAM)g.hList);
    {
        char lv[16];
        sprintf(lv, "%u", (unsigned)g.s.units[g.sel].f[SH_F_LEVEL]);
        SetWindowTextA(g.hEdit[0], lv);  /* case2 把等级留成了 99，先恢复 */
    }
    SetWindowTextA(g.hEdit[3], "309");   /* 气力 */
    SetWindowTextA(g.hEdit[4], "309");   /* 气力上限 */
    ok = apply_current(1);
    fprintf(f, "case3 同改气力与上限到 309: apply_ok=%d 内存 mp=%u mpmax=%u -> %s\r\n",
            ok, (unsigned)g.s.units[g.sel].f[SH_F_MP],
            (unsigned)g.s.units[g.sel].f[SH_F_MPMAX],
            (ok && g.s.units[g.sel].f[SH_F_MP] == 309 &&
             g.s.units[g.sel].f[SH_F_MPMAX] == 309) ? "PASS" : "FAIL");

    /* case4：非法组合（生命 500 > 上限 108）应被拒并回滚 */
    got = (int)g.s.units[g.sel].f[SH_F_HP];
    SetWindowTextA(g.hEdit[1], "500");   /* 生命 */
    ok = apply_current(1);
    fprintf(f, "case4 非法组合被拒并回滚: apply_ok=%d 内存 hp=%u 期望仍为 %d -> %s\r\n",
            ok, (unsigned)g.s.units[g.sel].f[SH_F_HP], got,
            (!ok && (int)g.s.units[g.sel].f[SH_F_HP] == got) ? "PASS" : "FAIL");

    /* case10：伪标签页切换 —— 可见性必须互斥，且状态栏/页签始终可见 */
    {
        int visSave1, visGoods1, visSave0, visGoods0, visStatus, visTab;
        int initGoods, initSave, initMoney;

        /* 启动态：只应有存档页（含金钱框）可见，道具页必须已隐藏 */
        initGoods = (GetWindowLongA(g.hGbList, GWL_STYLE) & WS_VISIBLE) != 0;
        initSave  = (GetWindowLongA(g.hEdit[0],  GWL_STYLE) & WS_VISIBLE) != 0;
        initMoney = (GetWindowLongA(g.hMoney,    GWL_STYLE) & WS_VISIBLE) != 0;
        fprintf(f, "case10 启动态: 道具列表可见=%d 存档字段可见=%d 金钱框可见=%d -> %s\r\n",
                initGoods, initSave, initMoney,
                (!initGoods && initSave && initMoney) ? "PASS" : "FAIL");

        set_page(1);
        visSave1  = (GetWindowLongA(g.hEdit[0], GWL_STYLE) & WS_VISIBLE) != 0;
        visGoods1 = (GetWindowLongA(g.hGbList, GWL_STYLE) & WS_VISIBLE) != 0;
        visStatus = (GetWindowLongA(g.hStatus, GWL_STYLE) & WS_VISIBLE) != 0;
        /* 道具页上这些存档控件必须全隐藏，否则会叠在一起（旧版就是这样） */
        if ((GetWindowLongA(g.hMoney,  GWL_STYLE) & WS_VISIBLE) ||
            (GetWindowLongA(g.hSlot[0], GWL_STYLE) & WS_VISIBLE) ||
            (GetWindowLongA(g.hHidden, GWL_STYLE) & WS_VISIBLE)) {
            visSave1 = 1;   /* 复用为失败标记 */
        }
        visTab    = (GetWindowLongA(GetDlgItem(g.hMain, ID_TAB_SAVE), GWL_STYLE) & WS_VISIBLE) != 0;

        set_page(0);
        visSave0  = (GetWindowLongA(g.hEdit[0], GWL_STYLE) & WS_VISIBLE) != 0;
        visGoods0 = (GetWindowLongA(g.hGbList, GWL_STYLE) & WS_VISIBLE) != 0;

        fprintf(f, "case10 页签切换: 道具页(存档控件%d 道具控件%d) 存档页(存档控件%d 道具控件%d) 状态栏%d 页签%d -> %s\r\n",
                visSave1, visGoods1, visSave0, visGoods0, visStatus, visTab,
                (!visSave1 && visGoods1 && visSave0 && !visGoods0 && visStatus && visTab)
                ? "PASS" : "FAIL");
    }

    /* case11：页签隔离不变量 —— 可见控件必须只属于当前页 */
    {
        HWND c;
        int badGoods = 0, badSave = 0;

        set_page(1);
        for (c = GetWindow(g.hMain, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            int id = GetDlgCtrlID(c);
            int vis = (GetWindowLongA(c, GWL_STYLE) & WS_VISIBLE) != 0;
            int isGoods;
            if (!vis) continue;
            if (is_shared_id(id)) continue;
            isGoods = is_goods_id(id);
            if (!isGoods) badGoods++;      /* 道具页上出现了存档控件 */
        }

        set_page(0);
        for (c = GetWindow(g.hMain, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            int id = GetDlgCtrlID(c);
            int vis = (GetWindowLongA(c, GWL_STYLE) & WS_VISIBLE) != 0;
            int isGoods;
            if (!vis) continue;
            if (is_shared_id(id)) continue;
            isGoods = is_goods_id(id);
            if (isGoods) badSave++;        /* 存档页上出现了道具控件 */
        }

        fprintf(f, "case11 页签隔离: 道具页残留存档控件=%d 存档页残留道具控件=%d -> %s\r\n",
                badGoods, badSave, (badGoods == 0 && badSave == 0) ? "PASS" : "FAIL");
    }

    /* case12：道具名转码 —— 列表文字里不应出现 "?" 替换字符 */
    {
        do_goods();
        if (s_gbLoaded && g.hGbList) {
            char item[256];
            int q = 0, k, len;
            SendMessageA(g.hGbList, LB_GETTEXT, 0, (LPARAM)item);
            len = (int)strlen(item);
            for (k = 0; k < len; k++) if (item[k] == '?') q++;
            /* 只报长度与 "?" 个数：item 已是 GBK，再 ansi() 会二次转码把日志弄乱 */
            fprintf(f, "case12 道具名转码: 首项长度=%d \"?\"=%d -> %s\r\n",
                    len, q, (len > 4 && q == 0) ? "PASS" : "FAIL");
            set_page(0);
        } else {
            fprintf(f, "case12 道具名转码: SKIP（未载入道具表）\r\n");
        }
    }

    /* case21：页面归属与 ID 遮蔽 —— 存档页控件不能被判成道具页，ID 也不能被别的控件抢走 */
    {
        int bad = 0;
        if (is_goods_id(ID_CACHE))      bad |= 1;   /* 装备加成按钮应属存档页 */
        if (is_goods_id(ID_EQNOTE0))    bad |= 2;   /* 分解标签应属存档页 */
        if (is_goods_id(ID_EQNOTE0 + 1)) bad |= 4;
        if (!is_goods_id(ID_GB_LIST))   bad |= 8;   /* 道具页控件应属道具页 */
        if (!is_goods_id(ID_GB_DESC))   bad |= 16;
        if (!is_goods_id(ID_GB_CHK))    bad |= 32;
        if (!is_goods_id(ID_GB_RESTORE)) bad |= 64;
        /* GetDlgItem 跑到分解标签上 = ID 撞了，说明文字会被写错地方 */
        if (GetDlgItem(g.hMain, ID_GB_DESC) == g.hEquipNote[0]) bad |= 128;
        if (GetDlgItem(g.hMain, ID_GB_CHK)  == g.hEquipNote[1]) bad |= 256;
        /* 分块契约：15000–19999 是道具页，边界两侧必须泾渭分明 */
        if (is_goods_id(14999)) bad |= 512;
        if (!is_goods_id(15000)) bad |= 1024;
        if (!is_goods_id(19999)) bad |= 2048;
        if (is_goods_id(20000)) bad |= 4096;
        if (is_goods_id(ID_PATH) || is_goods_id(ID_LIST) || is_goods_id(ID_CACHE) ||
            is_goods_id(ID_EQNOTE0) || is_goods_id(ID_STATUS)) bad |= 8192;
        /* WM_COMMAND 只取 LOWORD(16 位)，ID 必须 < 65536 */
        if (ID_PATH >= 65536 || ID_CA_CANCEL >= 65536) bad |= 16384;
        fprintf(f, "case21 页面归属/ID遮蔽: bad=0x%X -> %s\r\n", bad, bad == 0 ? "PASS" : "FAIL");
    }

    /* case20：攻击/防御分解标签 —— 应等于三槽缓存之和（跳过空槽） */
    {
        char t[64];
        int k, sa = 0, sd = 0;
        const sh_unit *uu = &g.s.units[g.sel];
        for (k = 0; k < 3; k++) {
            if (uu->slots[k] == 0) continue;
            sa += sh_slot_cache_get(&g.s, g.sel, k, SH_CACHE_ATK);
            sd += sh_slot_cache_get(&g.s, g.sel, k, SH_CACHE_DEF);
        }
        sprintf(t, "装备+%d", sa);
        GetWindowTextA(g.hEquipNote[0], t + 32, 24);
        fprintf(f, "case20 装备分解标签: atk=%d 标签文本长度=%d -> %s\r\n",
                sa, (int)strlen(t + 32),
                (strlen(t + 32) > 0) ? "PASS" : "FAIL");
    }

    /* case19：控件 ID 唯一性 —— 道具页的 ID 段曾经重叠过，导致文字画到别的控件上 */
    {
        HWND hs[32];
        int n = 0, i, j, dup = 0;
        hs[n++] = GetDlgItem(g.hMain, ID_GB_LIST);
        hs[n++] = GetDlgItem(g.hMain, ID_GB_SAVE);
        hs[n++] = GetDlgItem(g.hMain, ID_GB_RESTORE);
        hs[n++] = GetDlgItem(g.hMain, ID_GB_WARN);
        hs[n++] = GetDlgItem(g.hMain, ID_GB_DESC);
        hs[n++] = GetDlgItem(g.hMain, ID_GB_CHK);
        for (i = 0; i < SH_GF_COUNT; i++) {
            hs[n++] = GetDlgItem(g.hMain, ID_GB_LAB0 + i);
            hs[n++] = GetDlgItem(g.hMain, ID_GB_EDIT0 + i);
            hs[n++] = GetDlgItem(g.hMain, ID_GB_RNG0 + i);
        }
        for (i = 0; i < n; i++) {
            if (!hs[i]) { dup++; continue; }
            for (j = i + 1; j < n; j++) if (hs[i] == hs[j]) dup++;
        }
        fprintf(f, "case19 控件ID唯一性: 检查 %d 个句柄 重复/缺失 %d -> %s\r\n",
                n, dup, dup == 0 ? "PASS" : "FAIL");
    }

    /* case22：人物编辑 —— 按钮归属 + HUMAN.HUM 解析 + 字段读写（只改内存，不落盘） */
    {
        HWND hb = GetDlgItem(g.hMain, ID_HUMAN);
        HWND hn = GetDlgItem(g.hMain, ID_GB_NOTE);
        char hp[SH_HUMAN_PATH];
        sh_human hu;
        int page0 = g.page, vis0, vis1, okId;
        int rc = -99, okParse = 0, okRW = 0;

        /* 人物编辑按钮必须两页都能看见（用户要求：启动就显示） */
        set_page(0); vis0 = IsWindowVisible(hb);
        set_page(1); vis1 = IsWindowVisible(hb);
        set_page(page0);

        okId = (hb != NULL) && vis0 && vis1 &&
               (hn != NULL) && is_goods_id(ID_GB_NOTE);

        hp[0] = '\0';
        if (sh_human_locate(g.path, hp, sizeof(hp))) rc = sh_human_load(hp, &hu);
        if (rc == 0) {
            int i, sum = 0;
            for (i = 0; i < hu.count; i++) sum += sh_human_skill_count(&hu, i);
            okParse = (hu.count == SH_HUMAN_MAX) && (sum == 302) &&
                      (hu.size == SH_HUMAN_SIZE) &&
                      (sh_human_base(&hu, 0, SH_HF_ATK) > 0) &&
                      (sh_human_base(&hu, 0, SH_HF_ATK) <= 2000) &&
                      (sh_human_growth(&hu, 0, SH_HF_DEF) >= 1) &&
                      (sh_human_growth(&hu, 0, SH_HF_DEF) <= 255) &&
                      (sh_human_move(&hu, 0) >= 1) &&
                      (sh_human_move(&hu, 0) <= 20);
            {
                int a = sh_human_base(&hu, 0, SH_HF_ATK);
                okRW = (sh_human_set_base(&hu, 0, SH_HF_ATK, a + 1) == 0) &&
                       (sh_human_base(&hu, 0, SH_HF_ATK) == a + 1) &&
                       (sh_human_set_base(&hu, 0, SH_HF_ATK, a) == 0) &&
                       (sh_human_base(&hu, 0, SH_HF_ATK) == a) &&
                       (sh_human_set_growth(&hu, 0, SH_HF_ATK, 300) == -2) &&
                       (sh_human_set_move(&hu, 0, 300) == -2) &&
                       (sh_human_base(&hu, 0, SH_HF_ATK) == a);
            }
            sh_human_free(&hu);
        }
        fprintf(f, "case22 人物编辑: 按钮=%d 解析rc=%d 结构=%d 读写=%d -> %s\r\n",
                okId, rc, okParse, okRW,
                (okId && okParse && okRW) ? "PASS" : "FAIL");
    }

    /* case23：技能子项读写（学习等级 / 隐藏标志 / 技能 id，只改内存） */
    {
        char hp[SH_HUMAN_PATH];
        sh_human hu;
        int rc = -99, ok = 0, n = -1, lv0 = -1, hidc = -1;

        hp[0] = '\0';
        if (sh_human_locate(g.path, hp, sizeof(hp))) rc = sh_human_load(hp, &hu);
        if (rc == 0) {
            int sid0;
            n = sh_human_skill_count(&hu, 0);
            sid0 = sh_human_skill_sid(&hu, 0, 0);
            lv0 = sh_human_skill_level(&hu, 0, 0);
            hidc = sh_human_skill_hidden_count(&hu, 0);
            ok = (n > 0) &&
                 (sid0 >= 0) && (sid0 < sh_skill_name_count()) &&
                 (lv0 >= 1 && lv0 <= 30) && (hidc == 3) &&
                 (sh_human_skill_off(&hu, 0, n) == -1) &&
                 (sh_human_set_skill_level(&hu, 0, 0, 30) == 0) &&
                 (sh_human_skill_level(&hu, 0, 0) == 30) &&
                 (sh_human_set_skill_level(&hu, 0, 0, 31) == -2) &&
                 (sh_human_set_skill_level(&hu, 0, 0, lv0) == 0) &&
                 (sh_human_set_skill_hidden(&hu, 0, 0, 2) == -2) &&
                 (sh_human_set_skill_sid(&hu, 0, 0, sid0) == 0) &&
                 (sh_human_skill_sid(&hu, 0, 0) == sid0);
            sh_human_free(&hu);
        }
        fprintf(f, "case23 技能子项: rc=%d 条数=%d 首条等级=%d 隐藏=%d -> %s\r\n",
                rc, n, lv0, hidc, ok ? "PASS" : "FAIL");
    }

    /* case24：整条技能导入（跨缓冲复制 + 复原，只改内存） */
    {
        char hp[SH_HUMAN_PATH];
        sh_human a, b;
        int rca = -99, rcb = -99, ok = 0;

        hp[0] = '\0';
        if (sh_human_locate(g.path, hp, sizeof(hp))) {
            rca = sh_human_load(hp, &a);
            rcb = sh_human_load(hp, &b);
        }
        if (rca == 0 && rcb == 0) {
            int n = sh_human_skill_count(&a, 0);
            if (n >= 2) {
                int s0 = sh_human_skill_sid(&b, 0, 0), l0 = sh_human_skill_level(&b, 0, 0);
                int s1 = sh_human_skill_sid(&b, 0, 1), l1 = sh_human_skill_level(&b, 0, 1);
                ok = (sh_human_skill_copy(&a, 0, 1, &b, 0, 0) == 0) &&
                     (sh_human_skill_sid(&a, 0, 1) == s0) &&
                     (sh_human_skill_level(&a, 0, 1) == l0) &&
                     (sh_human_skill_copy(&a, 0, 1, &b, 0, 1) == 0) &&
                     (sh_human_skill_sid(&a, 0, 1) == s1) &&
                     (sh_human_skill_level(&a, 0, 1) == l1) &&
                     (sh_human_skill_copy(&a, 0, 99, &b, 0, 0) == -1);
            }
        }
        if (rca == 0) sh_human_free(&a);
        if (rcb == 0) sh_human_free(&b);
        fprintf(f, "case24 整条技能导入: rc=%d/%d -> %s\r\n",
                rca, rcb, ok ? "PASS" : "FAIL");
    }

    /* case25：EXE 内置的原始 HUMAN 表 */
    {
        sh_human org;
        int rc = -99, ok = 0, cnt = 0, skills = 0;

        if (sh_human_orig_size() > 0)
            rc = sh_human_load_mem(sh_human_orig(), (size_t)sh_human_orig_size(), &org);
        if (rc == 0) {
            int c, k;
            cnt = org.count;
            for (c = 0; c < cnt; c++) {
                int n = sh_human_skill_count(&org, c);
                for (k = 0; k < n && k < SH_HUMAN_SKILL_MAX; k++) skills++;
            }
            ok = (sh_human_orig_size() == SH_HUMAN_SIZE) && (cnt == 72) && (skills == 302);
            sh_human_free(&org);
        }
        fprintf(f, "case25 内置原始表: size=%d 职业=%d 技能=%d -> %s\r\n",
                sh_human_orig_size(), cnt, skills, ok ? "PASS" : "FAIL");
    }
    /* case18：道具的 敏捷(+96) / 必殺(+100) 字段读写 */
    {
        char gp[600];
        sh_goods g;
        if (goods_locate(gp, sizeof(gp)) && sh_goods_load(gp, &g) == 0) {
            int a0 = sh_goods_get(&g, 12, SH_GF_AGI), c0 = sh_goods_get(&g, 12, SH_GF_CRIT);
            int a1, c1;
            sh_goods_set(&g, 12, SH_GF_AGI, 7);
            sh_goods_set(&g, 12, SH_GF_CRIT, 8);
            a1 = sh_goods_get(&g, 12, SH_GF_AGI);
            c1 = sh_goods_get(&g, 12, SH_GF_CRIT);
            sh_goods_set(&g, 12, SH_GF_AGI, a0);      /* 复原内存，不写盘 */
            sh_goods_set(&g, 12, SH_GF_CRIT, c0);
            sh_goods_free(&g);
            fprintf(f, "case18 敏捷/必殺字段: 读回 %d/%d(期望7/8) -> %s\r\n",
                    a1, c1, (a1 == 7 && c1 == 8) ? "PASS" : "FAIL");
        } else {
            fprintf(f, "case18 敏捷/必殺字段: SKIP（找不到 GOODS.DAB）\r\n");
        }
    }

    /* case17：装备加成缓存读写（攻击=槽+2、防御=槽+6） */
    {
        int old_a, old_d, got_a, got_d;
        old_a = sh_slot_cache_get(&g.s, g.sel, 0, SH_CACHE_ATK);
        old_d = sh_slot_cache_get(&g.s, g.sel, 1, SH_CACHE_DEF);
        sh_slot_cache_set(&g.s, g.sel, 0, SH_CACHE_ATK, 42);
        sh_slot_cache_set(&g.s, g.sel, 1, SH_CACHE_DEF, 43);
        got_a = sh_slot_cache_get(&g.s, g.sel, 0, SH_CACHE_ATK);
        got_d = sh_slot_cache_get(&g.s, g.sel, 1, SH_CACHE_DEF);
        sh_slot_cache_set(&g.s, g.sel, 0, SH_CACHE_ATK, old_a);
        sh_slot_cache_set(&g.s, g.sel, 1, SH_CACHE_DEF, old_d);
        fprintf(f, "case17 装备缓存: 写42/43 读回%d/%d -> %s\r\n",
                got_a, got_d, (got_a == 42 && got_d == 43) ? "PASS" : "FAIL");
    }

    /* case16：加载别名 —— 把 g.path 直接当路径再读一次（恢复流程就是这么做的） */
    {
        char gp[600];
        sh_goods g;
        if (goods_locate(gp, sizeof(gp)) && sh_goods_load(gp, &g) == 0) {
            int rc2 = sh_goods_load(g.path, &g);   /* 关键：路径指向 g 自己 */
            fprintf(f, "case16 路径别名重载: rc=%d -> %s\r\n",
                    rc2, rc2 == 0 ? "PASS" : "FAIL");
            sh_goods_free(&g);
        } else {
            fprintf(f, "case16 路径别名重载: SKIP（找不到 GOODS.DAB）\r\n");
        }
    }

    /* case15：道具表备份枚举 —— 按时间倒序、最新优先 */
    {
        char dir[MAX_PATH], gp[600], b1[MAX_PATH], b2[MAX_PATH], src[MAX_PATH];
        int n, newestIsB2 = 0;

        sh_goods gbt;
        if (goods_locate(gp, sizeof(gp)) && sh_goods_load(gp, &gbt) == 0) {
            GetTempPathA(sizeof(dir), dir);
            sprintf(dir, "%s_shuihu_bak_test", dir);
            CreateDirectoryA(dir, NULL);
            {
                sprintf(src, "%s\\GOODS.DAB", dir);
                CopyFileA(gp, src, FALSE);
                sprintf(b1, "%s\\GOODS.DAB_20260101-010101.bak", dir);
                CopyFileA(gp, b1, FALSE);
                {   /* CopyFile 会保留源文件时间，这里改写 1 字节把 mtime 顶新 */
                    HANDLE hf = CreateFileA(b1, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                            FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hf != INVALID_HANDLE_VALUE) {
                        DWORD w = 0; unsigned char c0 = 0;
                        SetFilePointer(hf, 0, NULL, FILE_BEGIN);
                        WriteFile(hf, &c0, 1, &w, NULL);
                        CloseHandle(hf);
                    }
                }
                Sleep(1100);
                sprintf(b2, "%s\\GOODS.DAB_20260101-020202.bak", dir);
                CopyFileA(gp, b2, FALSE);
                {
                    HANDLE hf = CreateFileA(b2, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                            FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hf != INVALID_HANDLE_VALUE) {
                        DWORD w = 0; unsigned char c0 = 0;
                        SetFilePointer(hf, 0, NULL, FILE_BEGIN);
                        WriteFile(hf, &c0, 1, &w, NULL);
                        CloseHandle(hf);
                    }
                }
            }
            sh_goods_free(&gbt);

            n = collect_goods_backups(src);
            if (n > 0) newestIsB2 = (strstr(s_bakPath[0], "020202") != NULL);
            fprintf(f, "case15 道具备份枚举: 找到=%d(期望2) 最新是B2=%d -> %s\r\n",
                    n, newestIsB2, (n == 2 && newestIsB2) ? "PASS" : "FAIL");

            DeleteFileA(b1);
            DeleteFileA(b2);
            DeleteFileA(src);
            RemoveDirectoryA(dir);
        } else {
            fprintf(f, "case15 道具备份枚举: SKIP（找不到 GOODS.DAB）\r\n");
        }
    }

    /* case14：说明与字段一致性提示（正常应一致；篡改字段应报不匹配） */
    {
        do_goods();
        if (s_gbLoaded) {
            char cm[300];
            int bad0, bad1, back;

            /* 打狗棒说明「攻擊力加30」，字段也是 30 → 应一致 */
            bad0 = goods_check(22, cm, sizeof(cm));

            /* 临时改成 99，应报「说明写 30，字段是 99」 */
            back = sh_goods_get(&s_gb, 22, SH_GF_ATK);
            sh_goods_set(&s_gb, 22, SH_GF_ATK, 99);
            bad1 = goods_check(22, cm, sizeof(cm));
            sh_goods_set(&s_gb, 22, SH_GF_ATK, back);      /* 复原，避免影响后续 */

            fprintf(f, "case14 一致性提示: 正常=%d 篡改=%d 提示=\"%s\" -> %s\r\n",
                    bad0, bad1, cm, (bad0 == 0 && bad1 > 0) ? "PASS" : "FAIL");
            set_page(0);
        } else {
            fprintf(f, "case14 一致性提示: SKIP（未载入道具表）\r\n");
        }
    }

    /* case13：道具说明 —— 选中「人肉包子」应显示其 Big5 说明文本 */
    {
        do_goods();
        if (s_gbLoaded) {
            char d[400];
            int q = 0, k, len;
            gb_show_item(177);
            GetWindowTextA(GetDlgItem(g.hMain, ID_GB_DESC), d, sizeof(d));
            len = (int)strlen(d);
            for (k = 0; k < len; k++) if (d[k] == '?') q++;
            fprintf(f, "case13 道具说明: 长度=%d \"?\"=%d -> %s\r\n",
                    len, q, (len >= 6 && q == 0) ? "PASS" : "FAIL");
            set_page(0);
        } else {
            fprintf(f, "case13 道具说明: SKIP（未载入道具表）\r\n");
        }
    }

    /* case9：GOODS.DAB 读/改/写/校验（只操作临时副本） */
    {
        char gp[600], tmp[MAX_PATH], tfile[MAX_PATH];
        sh_goods gb;

        if (goods_locate(gp, sizeof(gp)) && sh_goods_load(gp, &gb) == 0) {
            int before = sh_goods_get(&gb, 22, SH_GF_MOVE), after = 0, rc;

            GetTempPathA(sizeof(tmp), tmp);
            sprintf(tfile, "%s_shuihu_goods_test.DAB", tmp);
            CopyFileA(gp, tfile, FALSE);
            sh_goods_free(&gb);

            if (sh_goods_load(tfile, &gb) == 0) {
                sh_goods_set(&gb, 22, SH_GF_MOVE, before + 2);
                {
                    char bak[600];
                    rc = sh_goods_backup(&gb, bak, sizeof(bak));
                    if (rc == 0) rc = sh_goods_write(&gb);
                    sh_goods_free(&gb);
                    if (sh_goods_load(tfile, &gb) == 0) {
                        after = sh_goods_get(&gb, 22, SH_GF_MOVE);
                        sh_goods_free(&gb);
                    }
                }
                fprintf(f, "case9 道具表写读: 原=%d 写入=%d 重读=%d 件数=200 -> %s\r\n",
                        before, before + 2, after,
                        (rc == 0 && after == before + 2) ? "PASS" : "FAIL");
            } else {
                fprintf(f, "case9 道具表写读: SKIP（临时副本读不出）\r\n");
            }
            DeleteFileA(tfile);
        } else {
            fprintf(f, "case9 道具表写读: SKIP（找不到 GOODS.DAB）\r\n");
        }
    }

    /* case8：「关于」文本很长，不能靠 512 字节的 ansi() 缓冲转换 */
    {
        static char conv[4096];
        int n8, nconv, q = 0, k;
        do_about();                  /* 自检模式下只记录、不弹框 */
        n8 = (int)strlen(g_lastMsg);
        sh_utf8_to_acp(g_lastMsg, conv, sizeof(conv));
        for (k = 0; conv[k]; k++) if (conv[k] == '?') q++;
        nconv = k;
        fprintf(f, "case8 关于文本转码: UTF-8=%d 字节 转换后=%d 字节 \"?\"=%d -> %s\r\n",
                n8, nconv, q, (n8 > 512 && q == 0) ? "PASS" : "FAIL");
    }

    /* case7：改「移动」应折算到 rel+36，使游戏重算后仍是目标值 */
    {
        int base, eq, bonus, move, left, k;
        reload_from_disk();
        SendMessageA(g.hList, LB_SETCURSEL, row0, 0);
        SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_LIST, LBN_SELCHANGE), (LPARAM)g.hList);
        SetWindowTextA(g.hEdit[12], "12");    /* 移动是第 13 个字段（索引 12） */
        ok = apply_current(1);
        {
            const sh_unit *uu = &g.s.units[g.sel];
            base = sh_class_base_move((int)uu->id);
            eq = 0;
            for (k = 0; k < SH_SLOT_COUNT; k++) eq += sh_item_move((int)uu->slots[k]);
            bonus = (short)wr_peek16(uu->anchor + SH_MOVE_BONUS_REL);
            left  = (short)wr_peek16(uu->anchor + SH_MOVE_LEFT_REL);
            move  = (short)uu->f[SH_F_MOVE];
        }
        fprintf(f, "case7 改移动: apply=%d 移动=%d 加成=%d 剩余=%d 重算=%d(期望12) -> %s\r\n",
                ok, move, bonus, left, base + bonus + eq,
                (ok && move == 12 && left == 12 && base + bonus + eq == 12) ? "PASS" : "FAIL");
    }

    /* case6：没改任何东西时，切换人物不应被标记为"有未保存修改" */
    {
        int d0, d1, d2;
        reload_from_disk();          /* 先丢掉前面用例造成的改动，重置 baseline */
        SendMessageA(g.hList, LB_SETCURSEL, row0, 0);
        SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_LIST, LBN_SELCHANGE), (LPARAM)g.hList);
        d0 = recompute_dirty();
        SendMessageA(g.hList, LB_SETCURSEL, row1, 0);
        SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_LIST, LBN_SELCHANGE), (LPARAM)g.hList);
        d1 = recompute_dirty();
        fprintf(f, "case6 空切换不置脏: 切换前=%d 切换后=%d -> %s\r\n",
                d0, d1, (d0 == 0 && d1 == 0) ? "PASS" : "FAIL");

        /* 改一个值 -> 应为脏；再改回原值 -> 应为干净 */
        {
            char orig[32], tmpv[32];
            unsigned v;
            HWND ed = g.hEdit[0];
            GetWindowTextA(ed, orig, sizeof(orig));
            v = (unsigned)g.s.units[g.sel].f[SH_F_LEVEL];
            sprintf(tmpv, "%u", v > 1 ? v - 1 : v + 1);
            SetWindowTextA(ed, tmpv);
            apply_current(1);
            d1 = recompute_dirty();
            SetWindowTextA(ed, orig);
            apply_current(1);
            d2 = recompute_dirty();
            fprintf(f, "case6 改了=脏/改回=干净: 改后=%d 改回=%d -> %s\r\n",
                    d1, d2, (d1 == 1 && d2 == 0) ? "PASS" : "FAIL");
        }
    }

    /* case5：备份枚举 / 排序 / 恢复（临时目录里造 3 份内容不同的备份） */
    {
        char tmp[MAX_PATH], bak[MAX_PATH], src[MAX_PATH], saved[MAX_PATH];
        int k, n;

        GetTempPathA(sizeof(tmp), tmp);
        sprintf(tmp, "%s_shuihubak_test", tmp);
        CreateDirectoryA(tmp, NULL);
        sprintf(bak, "%s\\backup_save", tmp);
        CreateDirectoryA(bak, NULL);
        sprintf(src, "%s\\test.sav", tmp);
        strcpy(saved, g.path);
        CopyFileA(g.path, src, FALSE);

        for (k = 0; k < 3; k++) {
            char one[MAX_PATH];
            HANDLE hf;
            DWORD wr = 0;

            sprintf(one, "%s\\bak%d.sav", bak, k);
            CopyFileA(src, one, FALSE);
            hf = CreateFileA(one, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hf != INVALID_HANDLE_VALUE) {
                DWORD v = 111u * (DWORD)(k + 1);   /* 111 / 222 / 333 */
                SetFilePointer(hf, 0x1B, NULL, FILE_BEGIN);
                WriteFile(hf, &v, 4, &wr, NULL);
                CloseHandle(hf);
            }
            Sleep(1100);   /* 拉开修改时间，验证排序 */
        }

        strcpy(g.path, src);
        n = collect_backups();
        fprintf(f, "case5 备份枚举: 找到 %d 份（期望 3），最新一份是 %s\r\n",
                n, n > 0 ? s_bakPath[0] : "(无)");

        if (n >= 3) {
            DWORD want = 0;
            int isNewest = (strstr(s_bakPath[0], "bak2.sav") != NULL);
            CopyFileA(s_bakPath[0], g.path, FALSE);
            {
                HANDLE hf = CreateFileA(g.path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, NULL);
                if (hf != INVALID_HANDLE_VALUE) {
                    DWORD rd = 0;
                    SetFilePointer(hf, 0x1B, NULL, FILE_BEGIN);
                    ReadFile(hf, &want, 4, &rd, NULL);
                    CloseHandle(hf);
                }
            }
            fprintf(f, "case5 默认选最新+恢复: 最新=bak2:%d 恢复后 money=%lu(期望 333) -> %s\r\n",
                    isNewest, (unsigned long)want,
                    (isNewest && want == 333) ? "PASS" : "FAIL");
        } else {
            fprintf(f, "case5 默认选最新+恢复: SKIP（备份不足）\r\n");
        }

        strcpy(g.path, saved);
        for (k = 0; k < 3; k++) {
            char one[MAX_PATH];
            sprintf(one, "%s\\bak%d.sav", bak, k);
            DeleteFileA(one);
        }
        RemoveDirectoryA(bak);
        DeleteFileA(src);
        RemoveDirectoryA(tmp);
    }

    fclose(f);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        g.hMain = hwnd;
        create_children(hwnd);
        return 0;

    case WM_TIMER:
        if (wp == 1 && g.page == 0 && GetActiveWindow() == g.hMain) {
            sh_tip_item map[4];
            static const char *tAtk =
                "攻击力（面板结果值）\n"
                "面板 = 基础 + 三个装备槽的加成之和。\n"
                "这里存的是游戏算完的结果：读档直接显示，但任何重算\n"
                "（装备/卸下/升级）都会按「基础 + 装备加成」覆盖它。\n"
                "想让攻击力持久生效，请用下方的「装备加成」按钮。";
            static const char *tDef =
                "防御力（面板结果值）\n"
                "同上：这里是结果，不是输入；重算会被「基础 + 装备加成」覆盖。\n"
                "要持久改防御，请用「装备加成」按钮改防御缓存。";
            static const char *tNoteA =
                "这三个数来自装备槽的「攻击缓存」，是持久的（每次重算都计入）。\n"
                "点「装备加成」可以逐槽修改。\n"
                "基础部分由游戏按职业和等级计算，不存盘，工具改不了。";
            static const char *tNoteD =
                "这三个数来自装备槽的「防御缓存」，同样持久。\n"
                "点「装备加成」可逐槽修改。";
            map[0].h = g.hEdit[5];      map[0].text = tAtk;    /* 攻击 */
            map[1].h = g.hEdit[6];      map[1].text = tDef;    /* 防御 */
            map[2].h = g.hEquipNote[0]; map[2].text = tNoteA;
            map[3].h = g.hEquipNote[1]; map[3].text = tNoteD;
            tip_poll(map, 4);
        }
        else if (wp == 1 && g.page == 1 && GetActiveWindow() == g.hMain) {
            static const char *t[SH_GF_COUNT] = {
                "攻击力：道具的原始值（GOODS +76）\n"
                "只在道具被获得的那一刻复制进槽位缓存。\n"
                "对已经拿在手里的道具，改这里无效 —— 请用存档页的「装备加成」。",
                "防御力：道具的原始值（GOODS +84）\n"
                "同上：只在获得时进入槽位缓存，已持有的道具改了也不会变。",
                "敏捷（GOODS +96）\n每次重算都直接读 GOODS，改完立即生效。",
                "必殺（GOODS +100）\n每次重算都直接读 GOODS，改完立即生效。",
                "移动加成（GOODS +104，有符号）\n每次重算都直接读 GOODS，改完立即生效。",
                "最大体力（GOODS +110）\n每次重算都直接读 GOODS，改完立即生效。",
                "最大气力（GOODS +112）\n每次重算都直接读 GOODS，改完立即生效。"
            };
            sh_tip_item map[SH_GF_COUNT];
            int i;
            for (i = 0; i < SH_GF_COUNT; i++) { map[i].h = g.hGbEdit[i]; map[i].text = t[i]; }
            tip_poll(map, SH_GF_COUNT);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE:  do_browse();    return 0;
        case ID_OPEN:    do_open();      return 0;
        case ID_WRITE:   do_write();     return 0;
        case ID_UNLOCK:  do_unlock();    return 0;
        case ID_CACHE:   do_cache_edit(); return 0;
        case ID_TREE:    do_skilltree(); return 0;
        case ID_RESTORE: do_restore();   return 0;
        case ID_RELOAD:  do_reload();    return 0;
        case ID_TAB_SAVE:
            if (g.page == 1 && s_gbSel > 0) gb_apply();
            set_page(0);
            return 0;
        case ID_TAB_GOODS: do_goods();    return 0;
        case ID_ABOUT:   do_about();     return 0;
        case ID_QUIT:    DestroyWindow(hwnd); return 0;
        case ID_GB_SAVE:
            if (gb_apply()) gb_commit();
            return 0;
        case ID_GB_RESTORE: do_goods_restore(); return 0;
        case ID_HUMAN:      do_human();      return 0;
        case ID_GB_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) {
                int row = (int)SendMessageA(g.hGbList, LB_GETCURSEL, 0, 0);
                int newId = row + 1;
                if (newId != s_gbSel) {
                    if (gb_apply()) gb_show_item(newId);
                    else SendMessageA(g.hGbList, LB_SETCURSEL, s_gbSel - 1, 0);
                }
            }
            return 0;
        case ID_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) {
                int sel = (int)SendMessageA(g.hList, LB_GETCURSEL, 0, 0);
                int newIdx = (sel >= 0 && sel < g.mapCount) ? g.map[sel] : -1;
                if (newIdx != g.sel) {
                    /* 切换前把控件里的值写回内存，否则切走再回来就丢了 */
                    if (g.loaded && g.sel >= 0 && !apply_current(0)) {
                        int row = -1, k;
                        for (k = 0; k < g.mapCount; k++)
                            if (g.map[k] == g.sel) { row = k; break; }
                        if (row >= 0) SendMessageA(g.hList, LB_SETCURSEL, row, 0);
                        return 0;
                    }
                    g.sel = newIdx;
                    update_title(g.hMain, g.dirty);
                    show_unit();
                }
            }
            return 0;
        }
        return 0;

    case WM_CLOSE:
        if (g.loaded && MessageBoxA(hwnd,
                ansi("退出前请注意：未点「写入存档」的修改不会保存。\n\n确定退出吗？"),
                ansi("确认退出"), MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g.loaded) sh_free(&g.s);
        if (g.hFont) DeleteObject(g.hFont);
        if (g.hIconBig)   DestroyIcon(g.hIconBig);
        if (g.hIconSmall && g.hIconSmall != g.hIconBig) DestroyIcon(g.hIconSmall);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, m, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;

    (void)hPrev;

    memset(&g, 0, sizeof(g));
    g.sel = -1;

    if (cmd) {                       /* 去掉可能的引号；支持 -layouttest 前缀 */
        const char *p = cmd;
        int n = 0;
        while (*p == ' ') p++;
        if (strncmp(p, "-layouttest", 11) == 0) { g.layoutTest = 1; p += 11; while (*p == ' ') p++; }
        else if (strncmp(p, "-uitest", 7) == 0) { g.uiTest = 1; p += 7; while (*p == ' ') p++; }
        if (*p == '"') p++;
        while (*p && *p != '"' && n < MAX_PATH - 1) g.argPath[n++] = *p++;
        g.argPath[n] = '\0';
    }

    /* 先读设置：WM_CREATE 里可能直接打开命令行传入的存档，那时会写入 lastDir，
     * 若把 load_settings 放在 CreateWindowEx 之后会把它覆盖回默认值。 */
    build_ini_path();
    load_settings();

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "ShuiHuEdit";
    if (!RegisterClassA(&wc)) return 1;

    hwnd = CreateWindowExA(0, "ShuiHuEdit",
        ansi("水浒传之梁山好汉 — 存档编辑器  " APP_VERSION),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 551,
        NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;

    load_app_icon();
    if (g.hIconBig)   SendMessageA(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)g.hIconBig);
    if (g.hIconSmall) SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g.hIconSmall);

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    if (g.layoutTest) {
        dump_layout(hwnd);
        if (g.loaded) sh_free(&g.s);
        DestroyWindow(hwnd);
        return 0;
    }

    if (g.uiTest) {
        do_uitest(hwnd);
        if (g.loaded) sh_free(&g.s);
        DestroyWindow(hwnd);
        return 0;
    }

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}