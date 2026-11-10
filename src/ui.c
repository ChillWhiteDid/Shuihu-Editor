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
#include "names.h"
#include "goods.h"
#include "human.h"
#include "tree.h"
#include "textconv.h"
#include "fileio.h"

/* ---- 控件 ID ---- */
#define ID_PATH      1001
#define ID_BROWSE    1002
#define ID_OPEN      1003
#define ID_LIST      1010
#define ID_WRITE     1020
#define ID_RESTORE   1021
#define ID_UNLOCK    1022
#define ID_ABOUT     1023
#define ID_QUIT      1024
#define ID_STATUS    1030
#define ID_EDIT0     1100   /* 1100..1100+NFIELDS-1 */
#define ID_SLOT0     1200   /* 1200..1200+6 : 槽位 id 输入框 */
#define ID_SLOTNAME0 1300   /* 1300..1300+6 : 槽位名称显示 */
#define ID_HIDDEN    1310
#define ID_MONEY     1050   /* 金钱输入框 */
#define ID_TREE      1025   /* 查看技能树按钮 */
#define ID_TREE_EDIT 1026   /* 技能树窗口里的文本框 */
#define ID_TREE_CLOSE 1027  /* 技能树窗口的关闭按钮 */

#define APP_VERSION  "v0.4"
#define APP_BUILD    "2026-11-10"

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
    HFONT   hFont;
    char    argPath[MAX_PATH];         /* 命令行传入的存档路径（可选） */
    HICON   hIconBig, hIconSmall;      /* 运行时从 logo.ico 加载 */
    char    iniPath[MAX_PATH];         /* exe 同目录的 shuihu-edit.ini */
    char    lastDir[MAX_PATH];         /* 上次浏览/打开的目录 */
    char    iconPath[MAX_PATH];
    unsigned long iconErr;
    int     layoutTest;                /* -layouttest：自检控件矩形后退出 */
} App;

static App g;

/* 设置存取（定义在后面，这里先声明） */
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

static void msg(const char *title, const char *text, UINT flags)
{
    MessageBoxA(NULL, ansi(text), ansi(title), flags);
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
}

/* 从界面读取字段并写入内存；失败返回 0 并弹提示 */
static int apply_fields(void)
{
    char buf[64];
    int i;

    if (!g.loaded || g.sel < 0) return 0;

    for (i = 0; i < NFIELDS; i++) {
        long v;
        int rc;
        GetWindowTextA(g.hEdit[i], buf, sizeof(buf));
        v = strtol(buf, NULL, 10);
        if (v < 0) v = 0;
        rc = sh_set_field(&g.s, g.sel, kFields[i], (uint16_t)v);
        if (rc != 0) {
            char m[256];
            sprintf(m, "字段「%s」的输入值 %ld 不被接受。\n\n%s",
                    sh_field_label(kFields[i]), v,
                    rc == -2 ? "超出允许范围（见各字段上限）。"
                             : "会破坏「当前生命/气力 ≤ 上限」的关系。");
            msg("输入有误", m, MB_ICONWARNING);
            return 0;
        }
    }
    return 1;
}

static int apply_slots(void)
{
    int i;

    if (!g.loaded || g.sel < 0) return 0;

    for (i = 0; i < SH_SLOT_COUNT; i++) {
        int v = (int)SendMessageA(g.hSlot[i], CB_GETCURSEL, 0, 0);
        if (v == CB_ERR) v = 0;
        if (v < 0 || v > sh_item_count()) v = 0;
        if (sh_set_slot(&g.s, g.sel, i, (uint8_t)v) != 0) return 0;
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

static void reload_from_disk(void)
{
    int keep = g.sel;
    sh_free(&g.s);
    if (sh_load(g.path, &g.s) == 0) {
        fill_list();
        if (keep >= 0 && keep < g.s.count) g.sel = keep;
        show_unit();
    }
}

/* ------------------------------------------------------------------ */
/* 命令                                                                */
/* ------------------------------------------------------------------ */
static void do_open(void)
{
    char path[MAX_PATH];
    GetWindowTextA(g.hPath, path, sizeof(path));
    if (!path[0]) { msg("提示", "请先填写或选择存档文件。", MB_ICONINFORMATION); return; }
    if (g.loaded) { sh_free(&g.s); g.loaded = 0; }

    if (sh_load(path, &g.s) != 0) {
        char m[700];
        sprintf(m, "无法读入：%s\n\n请确认文件存在且是本游戏的 .sav 存档。", path);
        msg("打开失败", m, MB_ICONERROR);
        return;
    }
    g.loaded = 1;
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
    if (!apply_fields()) return;
    if (!apply_slots()) return;
    if (!apply_money()) return;
    if (!commit_and_reload(0)) return;
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
static void do_restore(void)
{
    char dir[MAX_PATH], pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char newest[MAX_PATH];
    FILETIME best;
    int found = 0;

    if (!g.loaded) { msg("提示", "请先打开存档。", MB_ICONINFORMATION); return; }

    /* 备份目录 = 存档目录\backup_save */
    strncpy(dir, g.path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    {
        char *p = strrchr(dir, '\\');
        if (p) *p = '\0'; else strcpy(dir, ".");
    }
    sprintf(pat, "%s\\backup_save\\*.sav", dir);

    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        msg("没有备份", "backup_save 目录里没有找到备份文件。", MB_ICONINFORMATION);
        return;
    }
    memset(&best, 0, sizeof(best));
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (!found || CompareFileTime(&fd.ftLastWriteTime, &best) > 0) {
                best = fd.ftLastWriteTime;
                sprintf(newest, "%s\\backup_save\\%s", dir, fd.cFileName);
                found = 1;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    if (!found) { msg("没有备份", "backup_save 目录里没有找到备份文件。", MB_ICONINFORMATION); return; }

    {
        char m[900];
        sprintf(m, "将用下面这个备份覆盖当前存档：\n\n%s\n\n继续吗？", newest);
        if (MessageBoxA(NULL, ansi(m), ansi("确认恢复"), MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }

    if (!CopyFileA(newest, g.path, FALSE)) {
        msg("恢复失败", "复制备份文件失败。", MB_ICONERROR);
        return;
    }
    reload_from_disk();
    msg("完成", "已从备份恢复。", MB_ICONINFORMATION);
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
        "  移动≤255\n"
        "      —— 单位对象 +0x8A 按单字节写入\n"
        "  经验/升级所需≤65535、金钱≤4294967295\n"
        "      —— 仅受字段宽度限制，非游戏规则\n\n"
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

    mk("BUTTON", "写入存档",   BS_PUSHBUTTON, 282, 390, 96, 30, ID_WRITE);
    mk("BUTTON", "恢复备份",   BS_PUSHBUTTON, 386, 390, 90, 30, ID_RESTORE);
    mk("BUTTON", "查看技能树", BS_PUSHBUTTON, 484, 390, 96, 30, ID_TREE);
    mk("BUTTON", "解锁隐藏技", BS_PUSHBUTTON, 588, 390, 96, 30, ID_UNLOCK);
    mk("BUTTON", "关于",       BS_PUSHBUTTON, 692, 390, 66, 30, ID_ABOUT);
    mk("BUTTON", "退出",       BS_PUSHBUTTON, 766, 390, 66, 30, ID_QUIT);

    g.hStatus = mk("STATIC", "版本 " APP_VERSION " — 请先选择存档文件（可直接在左上角输入路径，再点「打开」）",
                   SS_LEFT | SS_LEFTNOWORDWRAP | SS_SUNKEN,
                   12, 430, 916, 22, ID_STATUS);

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
static LRESULT CALLBACK WndProc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        g.hMain = hwnd;
        create_children(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE:  do_browse();    return 0;
        case ID_OPEN:    do_open();      return 0;
        case ID_WRITE:   do_write();     return 0;
        case ID_UNLOCK:  do_unlock();    return 0;
        case ID_TREE:    do_skilltree(); return 0;
        case ID_RESTORE: do_restore();   return 0;
        case ID_ABOUT:   do_about();     return 0;
        case ID_QUIT:    DestroyWindow(hwnd); return 0;
        case ID_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) {
                int sel = (int)SendMessageA(g.hList, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < g.mapCount) g.sel = g.map[sel];
                show_unit();
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
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 500,
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

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}