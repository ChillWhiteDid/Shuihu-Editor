/*
 * mapedit.c -- 「关卡编辑」页：MAP 棋盘（图形化网格）+ ACT 布阵表（V2.3）
 *
 * 独立子窗口，自己创建/管理控件；数据读写复用 mapfile.c。
 * 全部界面文字是 UTF-8，交给 Win32 A 接口前必须转成系统 ANSI（GBK）。
 * 只用 Win9x 可用 API：不做 GDI+ / comctl32。
 */
#include "mapedit.h"
#include "mapfile.h"
#include "grpimg.h"
#include "manimg.h"
#include "names.h"

#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "textconv.h"

#ifndef OPENFILENAME_SIZE_VERSION_400
#define OPENFILENAME_SIZE_VERSION_400 76
#endif

/* ---------------- 本页控件 ID（与 ui.c 的 1xxxx/2xxxx 段不冲突） ---------------- */
#define MP_OPEN_MAP 301
#define MP_ACT      302      /* 布阵：未打开则选文件（只列 *.ACT），已打开则直接进编辑窗口 */
#define MP_WRITE    304
#define MP_RELOAD   305
#define MP_X        311
#define MP_Y        312
#define MP_TILE     313
#define MP_READCELL 314
#define MP_APPLY_T  315
#define MP_TILEBROWSE 316
#define MP_UNDO     317
#define MP_REDO     318
#define MP_RESTORE  319
#define MP_INFO     340
#define MP_GRID     350
#define MP_ACTEDIT  306      /* 复选框：在地图上直接编辑布阵 */

/* ACT 子窗口 */
#define MA_LIST     401
#define MA_TYPE     431
#define MA_E0       411     /* +0..+5：类型(下拉另用 MA_TYPE)/队别/X/Y/等级/动作 */
#define MA_APPLY    420
#define MA_DEL      423
#define MA_ADD      424
#define MA_TEAM     425
#define MA_ACT      426
#define MA_COPY     427
#define MA_PASTE    428
#define MA_OPEN     422
#define MA_CLOSE    421

/* ================= 编码与 ini 小工具 ================= */

static const char *A(const char *u8)
{
    static char buf[8][512];
    static int idx;
    char *out = buf[idx++ & 7];
    sh_utf8_to_acp(u8 ? u8 : "", out, (int)sizeof(buf[0]));
    return out;
}

static int msg_utf8(HWND h, const char *txt, const char *title, UINT fl)
{
    char a[2048], b[256];
    sh_utf8_to_acp(txt ? txt : "", a, (int)sizeof(a));
    sh_utf8_to_acp(title ? title : "", b, (int)sizeof(b));
    return MessageBoxA(h, a, b, fl);
}
#define MessageBoxA msg_utf8

static void ini_path(char *out, int n)
{
    char exe[MAX_PATH];
    char *p;
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    p = strrchr(exe, '\\');
    if (p) *(p + 1) = '\0';
    sprintf(out, "%sshuihu-edit.ini", exe);
    out[n - 1] = '\0';
}

static void ini_get(const char *key, char *out, int n)
{
    char p[MAX_PATH];
    ini_path(p, sizeof(p));
    out[0] = '\0';
    GetPrivateProfileStringA("map", key, "", out, (DWORD)n, p);
}

static void ini_set(const char *key, const char *val)
{
    char p[MAX_PATH];
    if (!val || !val[0]) return;
    ini_path(p, sizeof(p));
    WritePrivateProfileStringA("map", key, val, p);
}

/* 文件对话框过滤器：Windows 要的是「描述\0模式\0描述\0模式\0\0」双 NUL 结构。
 * 两个坑都踩过：
 *   ① 用 '|' 分隔 —— Win9x 的 comdlg32 不认，会把竖线原样显示出来（过滤失效）；
 *   ② 把整串 UTF-8 一起交给 sh_utf8_to_acp —— 它只转到第一个 \0，后半截丢失。
 * 所以这里分段转码、按 NUL 拼装。 */
static const char *filter_build(const char *d1, const char *p1, const char *d2, const char *p2)
{
    static char s[640];
    char a[200], b[200];
    size_t n1, n2, off;

    memset(s, 0, sizeof(s));
    sh_utf8_to_acp(d1, a, (int)sizeof(a));
    n1 = strlen(a);
    if (n1 + strlen(p1) + 4 > sizeof(s)) return "*.*";
    memcpy(s, a, n1); off = n1 + 1;
    strcpy(s + off, p1); off += strlen(p1) + 1;
    if (d2 && p2) {
        sh_utf8_to_acp(d2, b, (int)sizeof(b));
        n2 = strlen(b);
        if (off + n2 + strlen(p2) + 4 > sizeof(s)) { s[off] = '\0'; return s; }
        memcpy(s + off, b, n2); off += n2 + 1;
        strcpy(s + off, p2); off += strlen(p2) + 1;
    }
    s[off] = '\0';           /* 末尾保留两个 0（紧随其后的字节已被 memset 清 0） */
    return s;
}

static const char *filter_1(const char *d1, const char *p1)
{
    return filter_build(d1, p1, NULL, NULL);
}

/* 只保留一种类型的过滤器（ACT 对话框不再出现「所有文件」） */
static const char *filter_1(const char *d1, const char *pat);

/* ================= 状态 ================= */

typedef struct {
    HWND      page, grid, parent;
    HINSTANCE hi;
    sh_map    map;
    int       mapOk, mapDirty;
    sh_act    act;
    int       actOk, actDirty;
    int       selx, sely;
    char      mapPath[512], actPath[512];
    /* 真实图块渲染（GROUND.GRP / UPPER.GRP / DEFAULT.PAL） */
    sh_grp    grnd, upr;
    sh_pal    pal;
    int       gfxOk;
    int       offx, offy;       /* 滚动偏移（画布坐标） */
    int       actSel;           /* 当前选中的布阵记录（-1 = 无） */
    int       needCenter;       /* 下次绘制前把视图居中（等控件有了真实尺寸再算） */
    sh_man    man;              /* MAN.LIB 人物精灵（按需读） */
    int       manOk;
} MP;

static MP *g_mp;
static int s_actSel = -1;   /* ACT 子窗口当前选中的记录（用于切条目前自动暂存） */
static HWND s_actWnd = NULL;/* ACT 编辑窗口（非模态，避免嵌套消息循环把主界面卡死） */

/* 前置声明：ACT 子窗口里会用到（定义在后面的「动作」段） */
static int load_map_path(const char *p);
static int load_act_path(const char *p);
static int  gfx_try_dir(const char *mapPath);
static void update_scroll(HWND h);
static void center_view(void);
static void grid_repaint(void);
static void sync_sel_fields(void);
static void scroll_to_cell(int x, int y);
static void do_apply_tile(void);
static void undo_push(void);
static void do_undo(void);
static void do_redo(void);

/* ================= 杂项 ================= */

static void info_set(const char *s)
{
    char a[2048];
    sh_utf8_to_acp(s ? s : "", a, (int)sizeof(a));
    if (g_mp && g_mp->page) SetWindowTextA(GetDlgItem(g_mp->page, MP_INFO), a);
}

static int get_int(HWND page, int id)
{
    char b[32];
    GetDlgItemTextA(page, id, b, sizeof(b));
    return atoi(b);
}

static void set_int(HWND page, int id, int v)
{
    char b[32];
    sprintf(b, "%d", v);
    SetWindowTextA(GetDlgItem(page, id), b);
}

static const char *dir_of(const char *path)
{
    static char d[MAX_PATH];
    char *p;
    if (!path || !path[0]) return NULL;
    strcpy(d, path);
    p = strrchr(d, '\\');
    if (p) *p = '\0'; else d[0] = '\0';
    return d[0] ? d : NULL;
}

static int pick_file(HWND owner, const char *filter, const char *last, char *out, int n)
{
    OPENFILENAMEA ofn;
    char buf[MAX_PATH];

    memset(&ofn, 0, sizeof(ofn));
    memset(buf, 0, sizeof(buf));
    ofn.lStructSize   = OPENFILENAME_SIZE_VERSION_400;
    ofn.hwndOwner     = owner;
    ofn.lpstrFilter   = filter;                 /* filter_2() 已转成 ANSI */
    ofn.lpstrFile     = buf;
    ofn.nMaxFile      = sizeof(buf);
    ofn.lpstrInitialDir = dir_of(last);
    ofn.Flags         = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&ofn)) return 0;
    strncpy(out, buf, (size_t)n - 1);
    out[n - 1] = '\0';
    return 1;
}

/* ================= 刷新显示（固定 4 行，超了会被下面控件压住） ================= */

static void refresh(void)
{
    char b[1700];
    char sel[160];

    b[0] = '\0';
    sel[0] = '\0';
    if (g_mp->actOk && g_mp->actSel >= 0 && g_mp->actSel < g_mp->act.count) {
        int ty = sh_act_get(&g_mp->act, g_mp->actSel, SH_AREC_TYPE, 2);
        int sx = sh_act_get(&g_mp->act, g_mp->actSel, SH_AREC_X, 2);
        int sy = sh_act_get(&g_mp->act, g_mp->actSel, SH_AREC_Y, 2);
        int k, dup = 0;
        sprintf(sel, "   选中 #%d %s (%d,%d)", g_mp->actSel, sh_char_name(ty), sx, sy);
        for (k = 0; k < g_mp->act.count; k++) {
            if (k == g_mp->actSel) continue;
            if (sh_act_get(&g_mp->act, k, SH_AREC_X, 2) == sx &&
                sh_act_get(&g_mp->act, k, SH_AREC_Y, 2) == sy) dup++;
        }
        if (dup > 0) sprintf(sel + strlen(sel), "  [同格另 %d 条，已错开显示]", dup);
    }

    if (g_mp->mapOk) {
        sprintf(b + strlen(b),
                "MAP : %s\r\n      尺寸 %d x %d = %d 格 | 记录 %d 条（附加层 %d）%s\r\n",
                g_mp->mapPath, g_mp->map.w, g_mp->map.h, g_mp->map.cells,
                g_mp->map.n, g_mp->map.layers,
                g_mp->mapDirty ? "   [有未写入修改]" : "");
        if (g_mp->selx >= 0 && g_mp->selx < g_mp->map.w && g_mp->sely >= 0 && g_mp->sely < g_mp->map.h) {
            int t = sh_map_tile(&g_mp->map, g_mp->selx, g_mp->sely);
            char sz[64];
            sz[0] = '\0';
            if (g_mp->gfxOk && t > 0) {
                int tw = 0, th = 0;
                if (sh_grp_entry_wh(&g_mp->grnd, t - 1, &tw, &th) == 0)
                    sprintf(sz, "  图块 %d×%d", tw, th);
            }
            sprintf(b + strlen(b), "      当前格 (%d,%d)：tileId=%d flag=0x%02X sub=0x%02X 附加层=%d%s\r\n",
                    g_mp->selx, g_mp->sely, t,
                    sh_map_flag(&g_mp->map, g_mp->selx, g_mp->sely),
                    sh_map_sub(&g_mp->map, g_mp->selx, g_mp->sely),
                    sh_map_layer_n(&g_mp->map, g_mp->selx, g_mp->sely), sz);
        } else
            strcat(b, "      当前格：未选中（点地图上任意格即可选中）\r\n");
        if (g_mp->actOk)
            sprintf(b + strlen(b), "ACT : %s | 记录 %d 条%s%s", g_mp->actPath, g_mp->act.count,
                    g_mp->actDirty ? "   [有未写入修改]" : "", sel);
        else
            strcat(b, "ACT : 未打开（点「布阵编辑(ACT)」选 Map 目录下的 .ACT）");
    } else {
        strcat(b, "MAP : 未打开 —— 点「打开地图(MAP)」选 游戏目录\\Map\\*.MAP\r\n");
        if (g_mp->actOk)
            sprintf(b + strlen(b), "ACT : %s | 记录 %d 条%s%s", g_mp->actPath, g_mp->act.count,
                    g_mp->actDirty ? "   [有未写入修改]" : "", sel);
        else
            strcat(b, "ACT : 未打开");
    }
    info_set(b);
}

/* ================= 地图网格（自绘） ================= */

static COLORREF tile_color(int t)
{
    int h = (t * 137) % 360;
    int s = 150, v = 220;
    int c = (v * s) / 255;
    int hp = h * 6, x, m;
    int r = 0, g = 0, bl = 0;
    int seg = hp / 60;
    int rem = hp % 60;
    if (rem > 30) x = (c * (120 - rem)) / 60; else x = (c * rem) / 60;
    switch (seg) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; bl = x; break;
    case 3: g = x; bl = c; break;
    case 4: r = x; bl = c; break;
    default: r = c; bl = x; break;
    }
    m = v - c;
    return RGB(r + m, g + m, bl + m);
}

static const char *tip_tile_text(void)
{
    return
        "tileId = GROUND.GRP 里的图块编号（数据里没有名称，只能看图认）\r\n"
        "  输入编号后按【回车】应用到当前选中的格子\r\n"
        "  不知道编号就点「图块浏览」，挑好图块后按「确定」\r\n"
        "改完记得点顶部「写入文件」才会保存到磁盘。";
}

/* tileId 输入框子类化：回车 = 应用到地图（不用再单独放按钮） */
static WNDPROC s_tileEditProc;

static LRESULT CALLBACK tileedit_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN && wp == VK_RETURN) {
        do_apply_tile();
        return 0;
    }
    if (m == WM_CHAR && (wp == '\r' || wp == '\n')) return 0;
    return CallWindowProcA(s_tileEditProc, h, m, wp, lp);
}



/* ================= 真实图块渲染（GROUND/UPPER.GRP + DEFAULT.PAL） ================= */

#define MV_HX 45          /* 图块 90x46 的半宽/半高 */
#define MV_HY 23
#define MV_TW 90
#define MV_TH 46

static uint8_t *s_vbuf;       /* 可视区 24bpp 临时位图 */
static int      s_vbufSize;
static int      s_vbufStride; /* 每行字节数（DIB 要求 4 字节对齐！） */

/* 由地图路径推 Data 目录并载入 GRP + 调色板；成功 0 */
static int gfx_try_dir(const char *mapPath)
{
    char base[512], cand[600];
    char *p;
    int i;

    if (!mapPath || !mapPath[0]) return -1;
    strncpy(base, mapPath, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    p = strrchr(base, '\\');
    if (!p) return -1;
    *p = '\0';

    for (i = 0; i < 3; i++) {
        char gp[640], up[640], pp[640];
        sh_grp g, u;
        sh_pal pal;

        if (i == 0)      sprintf(cand, "%s\\..\\Data", base);
        else if (i == 1) sprintf(cand, "%s\\Data", base);
        else             sprintf(cand, "%s", base);

        sprintf(gp, "%s\\GROUND.GRP", cand);
        sprintf(up, "%s\\UPPER.GRP", cand);
        sprintf(pp, "%s\\DEFAULT.PAL", cand);

        if (sh_grp_load(gp, &g) != 0) continue;
        if (sh_grp_load(up, &u) != 0) { sh_grp_free(&g); continue; }
        if (sh_pal_load(pp, &pal) != 0) { sh_grp_free(&g); sh_grp_free(&u); continue; }

        sh_grp_free(&g_mp->grnd);
        sh_grp_free(&g_mp->upr);
        g_mp->grnd = g;
        g_mp->upr = u;
        g_mp->pal = pal;
        g_mp->gfxOk = 1;

        /* 人物精灵库：同一个 Data 目录下的 MAN.LIB（失败就只用彩色点） */
        if (g_mp->manOk) { sh_man_close(&g_mp->man); g_mp->manOk = 0; }
        {
            char mp[640];
            sprintf(mp, "%s\\MAN.LIB", cand);
            if (sh_man_open(mp, &g_mp->man) == 0) g_mp->manOk = 1;
        }
        return 0;
    }
    g_mp->gfxOk = 0;
    return -1;
}

/* 画一块图块到可视缓冲：画布坐标 → 缓冲坐标，索引 0 = 透明 */
static void blit_tile(const sh_grp *grp, int idx, int canvasX, int canvasY,
                      int vx, int vy, int vw, int vh)
{
    uint8_t *px = NULL;
    int tw = 0, th = 0, ix, iy;
    int x0, y0;

    if (idx < 0 || idx >= grp->count) return;
    if (sh_grp_decode(grp, idx, &px, &tw, &th) != 0 || !px) return;

    x0 = canvasX + MV_HX - tw / 2 - vx;
    y0 = canvasY + MV_HY - th / 2 - vy;
    for (iy = 0; iy < th; iy++) {
        int sy = y0 + iy;
        const uint8_t *srow;
        uint8_t *drow;
        if (sy < 0 || sy >= vh) continue;
        srow = px + (size_t)iy * tw;
        drow = s_vbuf + (size_t)sy * s_vbufStride;
        for (ix = 0; ix < tw; ix++) {
            int sx = x0 + ix;
            uint8_t c;
            if (sx < 0 || sx >= vw) continue;
            c = srow[ix];
            if (!c) continue;
            drow[sx * 3 + 0] = g_mp->pal.rgb[c][0];
            drow[sx * 3 + 1] = g_mp->pal.rgb[c][1];
            drow[sx * 3 + 2] = g_mp->pal.rgb[c][2];
        }
    }
    free(px);
}

/* 把可视区域 (vx,vy,vw,vh) 合成到 s_vbuf */
static void compose_view(int vx, int vy, int vw, int vh)
{
    int x, y, need;
    int hh;

    if (vw <= 0 || vh <= 0) return;
    /* 24bpp DIB 每行必须 4 字节对齐，否则 BitBlt 会错行 -> 画面“歪” */
    s_vbufStride = ((vw * 3 + 3) / 4) * 4;
    need = s_vbufStride * vh;
    if (need > s_vbufSize) {
        uint8_t *nb = (uint8_t *)realloc(s_vbuf, (size_t)need);
        if (!nb) { s_vbuf = NULL; s_vbufSize = 0; return; }
        s_vbuf = nb;
        s_vbufSize = need;
    }
    if (!s_vbuf) return;
    memset(s_vbuf, 0, (size_t)need);

    hh = g_mp->map.h;
    for (y = 0; y < g_mp->map.h; y++) {
        for (x = 0; x < g_mp->map.w; x++) {
            int cx = (x - y) * MV_HX + (hh - 1) * MV_HX;
            int cy = (x + y) * MV_HY;
            int t, k, n;

            if (cx + MV_TW < vx || cx > vx + vw) continue;
            if (cy + MV_TH < vy || cy > vy + vh) continue;

            t = sh_map_tile(&g_mp->map, x, y);
            if (t > 0) blit_tile(&g_mp->grnd, t - 1, cx, cy, vx, vy, vw, vh);

            n = sh_map_layer_n(&g_mp->map, x, y);
            for (k = 0; k < n; k++) {
                int lt = sh_map_layer_tile(&g_mp->map, x, y, k);
                if (lt > 0) blit_tile(&g_mp->upr, lt - 1, cx, cy, vx, vy, vw, vh);
            }
        }
    }
}

static void canvas_size(int *cw, int *ch)
{
    *cw = (g_mp->map.w + g_mp->map.h) * MV_HX;
    *ch = (g_mp->map.w + g_mp->map.h) * MV_HY + MV_TH;
}

/* 把视图滚到指定格（用于点击布阵列表后自动定位该单位） */
static void scroll_to_cell(int x, int y)
{
    int vw, vh, cx, cy;
    RECT rc;

    if (!g_mp || !g_mp->mapOk || !g_mp->grid) return;
    GetClientRect(g_mp->grid, &rc);
    vw = rc.right;
    vh = rc.bottom;
    cx = (x - y) * MV_HX + (g_mp->map.h - 1) * MV_HX;
    cy = (x + y) * MV_HY;
    g_mp->offx = cx + MV_HX - vw / 2;
    g_mp->offy = cy + MV_HY - vh / 2;
    if (g_mp->offx < 0) g_mp->offx = 0;
    if (g_mp->offy < 0) g_mp->offy = 0;
    update_scroll(g_mp->grid);
}

/* 把视图滚到地图中央（打开地图后默认居中显示） */
static void center_view(void)
{
    int cw, ch, vw, vh;
    RECT rc;

    if (!g_mp || !g_mp->mapOk || !g_mp->grid) return;
    canvas_size(&cw, &ch);
    GetClientRect(g_mp->grid, &rc);
    vw = rc.right;
    vh = rc.bottom;
    g_mp->offx = (cw - vw) / 2;
    g_mp->offy = (ch - vh) / 2;
    if (g_mp->offx < 0) g_mp->offx = 0;
    if (g_mp->offy < 0) g_mp->offy = 0;
    update_scroll(g_mp->grid);
}

static void update_scroll(HWND h)
{
    int cw, ch, vw, vh, maxx, maxy;
    RECT rc;

    if (!g_mp || !g_mp->mapOk) {
        SetScrollRange(h, SB_HORZ, 0, 0, TRUE);
        SetScrollRange(h, SB_VERT, 0, 0, TRUE);
        return;
    }
    canvas_size(&cw, &ch);
    GetClientRect(h, &rc);
    vw = rc.right;
    vh = rc.bottom;
    maxx = cw - vw; if (maxx < 0) maxx = 0;
    maxy = ch - vh; if (maxy < 0) maxy = 0;
    if (g_mp->offx > maxx) g_mp->offx = maxx;
    if (g_mp->offy > maxy) g_mp->offy = maxy;
    if (g_mp->offx < 0) g_mp->offx = 0;
    if (g_mp->offy < 0) g_mp->offy = 0;
    SetScrollRange(h, SB_HORZ, 0, maxx, FALSE);
    SetScrollRange(h, SB_VERT, 0, maxy, FALSE);
    SetScrollPos(h, SB_HORZ, g_mp->offx, TRUE);
    SetScrollPos(h, SB_VERT, g_mp->offy, TRUE);
}

static void on_scroll(HWND h, int bar, int code, int pos)
{
    int cw, ch, maxp, newpos, step = 40, page;
    RECT rc;

    if (!g_mp || !g_mp->mapOk) return;
    canvas_size(&cw, &ch);
    GetClientRect(h, &rc);
    maxp = (bar == SB_HORZ) ? cw - rc.right : ch - rc.bottom;
    if (maxp < 0) maxp = 0;
    page = (bar == SB_HORZ) ? 300 : 120;
    newpos = (bar == SB_HORZ) ? g_mp->offx : g_mp->offy;

    switch (code) {
    case SB_LINELEFT:   newpos -= step; break;   /* 竖条 SB_LINEUP 与它同值 */
    case SB_LINERIGHT:  newpos += step; break;   /* 竖条 SB_LINEDOWN 同值 */
    case SB_PAGELEFT:   newpos -= page; break;
    case SB_PAGERIGHT:  newpos += page; break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: newpos = pos; break;
    default: return;
    }
    if (newpos < 0) newpos = 0;
    if (newpos > maxp) newpos = maxp;
    if (bar == SB_HORZ) g_mp->offx = newpos; else g_mp->offy = newpos;
    SetScrollPos(h, bar, newpos, TRUE);
    InvalidateRect(h, NULL, FALSE);
}

/* 由客户区坐标反算格坐标（等距反变换）；格中心落在整数 (x,y) 上，四舍五入无系统性偏移 */
static int pick_cell(HWND h, int mx, int my, int *ox, int *oy)
{
    double s, t, fx, fy;
    int x, y;

    (void)h;
    if (!g_mp || !g_mp->mapOk) return 0;
    /* 画布像素 → 连续等距坐标：s = x-y，t = x+y
     * 格 (x,y) 的中心在画布 ((x-y+h)*45, (x+y+1)*23)，据此减偏移 */
    s = (double)(mx + g_mp->offx) / MV_HX - (double)g_mp->map.h;
    t = (double)(my + g_mp->offy) / MV_HY - 1.0;
    fx = (s + t) / 2.0;
    fy = (t - s) / 2.0;
    x = (int)(fx + (fx >= 0 ? 0.5 : -0.5));
    y = (int)(fy + (fy >= 0 ? 0.5 : -0.5));
    if (x < 0 || y < 0 || x >= g_mp->map.w || y >= g_mp->map.h) return 0;
    *ox = x;
    *oy = y;
    return 1;
}

/* 真实图块绘制（含选中菱形框与 ACT 单位标记） */
/* 把 ACT 单位画进可视缓冲（有 MAN.LIB 画人物模型，否则退化为队别色块） */
static void draw_units_buf(int vw, int vh)
{
    static int *order;
    static int orderCap;
    int i;

    if (!g_mp->actOk || !s_vbuf || g_mp->act.count <= 0) return;

    if (g_mp->manOk) {
        if (g_mp->act.count > orderCap) {
            int *no = (int *)realloc(order, (size_t)g_mp->act.count * sizeof(int));
            if (no) { order = no; orderCap = g_mp->act.count; }
        }
        if (!order) return;
        /* 按 (x+y) 升序 = 屏幕从上到下，近处人物盖住远处 */
        for (i = 0; i < g_mp->act.count; i++) {
            int j = i, key;
            order[i] = i;
            key = sh_act_get(&g_mp->act, i, SH_AREC_X, 2) + sh_act_get(&g_mp->act, i, SH_AREC_Y, 2);
            while (j > 0) {
                int prev = order[j - 1];
                int pk = sh_act_get(&g_mp->act, prev, SH_AREC_X, 2) +
                         sh_act_get(&g_mp->act, prev, SH_AREC_Y, 2);
                if (pk <= key) break;
                order[j] = prev;
                j--;
            }
            order[j] = i;
        }
        for (i = 0; i < g_mp->act.count; i++) {
            int idx2 = order[i];
            int ux = sh_act_get(&g_mp->act, idx2, SH_AREC_X, 2);
            int uy = sh_act_get(&g_mp->act, idx2, SH_AREC_Y, 2);
            int ty = sh_act_get(&g_mp->act, idx2, SH_AREC_TYPE, 2);
            uint8_t *spx = NULL;
            int sw = 0, shh = 0;
            int cx, cy, x0, y0, ix, iy;

            if (ux < 0 || ux >= g_mp->map.w || uy < 0 || uy >= g_mp->map.h) continue;
            if (sh_man_sprite(&g_mp->man, ty, &spx, &sw, &shh, NULL) != 0 || !spx) continue;
            cx = (ux - uy) * MV_HX + (g_mp->map.h - 1) * MV_HX - g_mp->offx;
            cy = (ux + uy) * MV_HY - g_mp->offy;
            {   /* 同一格有多条记录（ACT 原数据就可能重复）：按序号错开几像素，避免完全叠死 */
                int k, dup = 0;
                for (k = 0; k < idx2; k++) {
                    if (sh_act_get(&g_mp->act, k, SH_AREC_X, 2) == ux &&
                        sh_act_get(&g_mp->act, k, SH_AREC_Y, 2) == uy) dup++;
                }
                if (dup > 0) {
                    cx += (dup % 4) * 8;
                    cy += (dup % 4) * 5;
                }
            }
            x0 = cx + MV_HX - sw / 2;
            y0 = cy + MV_HY - shh + 10;        /* 脚踩在格子中心稍下 */
            for (iy = 0; iy < shh; iy++) {
                int sy = y0 + iy;
                uint8_t *drow;
                if (sy < 0 || sy >= vh) continue;
                drow = s_vbuf + (size_t)sy * s_vbufStride;
                for (ix = 0; ix < sw; ix++) {
                    int sx = x0 + ix;
                    uint8_t c;
                    if (sx < 0 || sx >= vw) continue;
                    c = spx[(size_t)iy * sw + ix];
                    if (!c) continue;
                    drow[sx * 3 + 0] = g_mp->pal.rgb[c][0];
                    drow[sx * 3 + 1] = g_mp->pal.rgb[c][1];
                    drow[sx * 3 + 2] = g_mp->pal.rgb[c][2];
                }
            }
            free(spx);
        }
        return;
    }

    /* 没有 MAN.LIB：队别颜色小方块（蓝=我方 / 黄=中立 / 红=敌方） */
    for (i = 0; i < g_mp->act.count; i++) {
        int ux = sh_act_get(&g_mp->act, i, SH_AREC_X, 2);
        int uy = sh_act_get(&g_mp->act, i, SH_AREC_Y, 2);
        int team = sh_act_get(&g_mp->act, i, SH_AREC_TEAM, 1);
        int cx, cy, ix, iy;
        int r = RGB(255, 220, 60), gg = RGB(60, 120, 255), b = RGB(255, 80, 80);
        int col;
        if (ux < 0 || ux >= g_mp->map.w || uy < 0 || uy >= g_mp->map.h) continue;
        cx = (ux - uy) * MV_HX + (g_mp->map.h - 1) * MV_HX - g_mp->offx;
        cy = (ux + uy) * MV_HY - g_mp->offy;
        if (cx + MV_TW < 0 || cy + MV_TH < 0 || cx > vw || cy > vh) continue;
        col = (team == 0) ? gg : (team == 2) ? b : r;
        for (iy = 0; iy < 7; iy++) {
            int sy = cy + MV_HY - 3 + iy;
            uint8_t *drow;
            if (sy < 0 || sy >= vh) continue;
            drow = s_vbuf + (size_t)sy * s_vbufStride;
            for (ix = 0; ix < 7; ix++) {
                int sx = cx + MV_HX - 3 + ix;
                if (sx < 0 || sx >= vw) continue;
                drow[sx * 3 + 0] = GetRValue(col);
                drow[sx * 3 + 1] = GetGValue(col);
                drow[sx * 3 + 2] = GetBValue(col);
            }
        }
    }
}

static void paint_view(HWND h, HDC dc)
{
    RECT rc;
    int vw, vh, vx, vy;

    if (g_mp->needCenter) {
        g_mp->needCenter = 0;
        center_view();
    }
    GetClientRect(h, &rc);
    vw = rc.right;
    vh = rc.bottom;
    vx = g_mp->offx;
    vy = g_mp->offy;
    if (vw <= 0 || vh <= 0) return;

    compose_view(vx, vy, vw, vh);
    draw_units_buf(vw, vh);
    if (s_vbuf) {
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = vw;
        bi.bmiHeader.biHeight = -vh;      /* top-down */
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 24;
        bi.bmiHeader.biCompression = BI_RGB;
        bi.bmiHeader.biSizeImage = (DWORD)(s_vbufStride * vh);
        StretchDIBits(dc, 0, 0, vw, vh, 0, 0, vw, vh, s_vbuf, &bi, DIB_RGB_COLORS, SRCCOPY);
    }

    /* 选中格：菱形框（白+黑双层，深浅背景都可见） */
    if (g_mp->selx >= 0 && g_mp->selx < g_mp->map.w &&
        g_mp->sely >= 0 && g_mp->sely < g_mp->map.h) {
        int cx = (g_mp->selx - g_mp->sely) * MV_HX + (g_mp->map.h - 1) * MV_HX - g_mp->offx;
        int cy = (g_mp->selx + g_mp->sely) * MV_HY - g_mp->offy;
        POINT pt[5];
        HPEN old, pen;
        int w;

        for (w = 0; w < 2; w++) {
            pt[0].x = cx + MV_HX; pt[0].y = cy - w;
            pt[1].x = cx + MV_TW + w; pt[1].y = cy + MV_HY;
            pt[2].x = cx + MV_HX; pt[2].y = cy + MV_TH + w;
            pt[3].x = cx - w; pt[3].y = cy + MV_HY;
            pt[4] = pt[0];
            pen = CreatePen(PS_SOLID, 1, (w == 0) ? RGB(0, 0, 0) : RGB(255, 255, 255));
            old = (HPEN)SelectObject(dc, pen);
            Polyline(dc, pt, 5);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
    }

    /* 当前选中的布阵记录：黄色菱形框（地图上编辑布阵时） */
    if (g_mp->actOk && g_mp->actSel >= 0 && g_mp->actSel < g_mp->act.count) {
        int ux = sh_act_get(&g_mp->act, g_mp->actSel, SH_AREC_X, 2);
        int uy = sh_act_get(&g_mp->act, g_mp->actSel, SH_AREC_Y, 2);
        if (ux >= 0 && ux < g_mp->map.w && uy >= 0 && uy < g_mp->map.h) {
            int cx = (ux - uy) * MV_HX + (g_mp->map.h - 1) * MV_HX - g_mp->offx;
            int cy = (ux + uy) * MV_HY - g_mp->offy;
            POINT pt[5];
            HPEN old, pen;
            pt[0].x = cx + MV_HX; pt[0].y = cy;
            pt[1].x = cx + MV_TW; pt[1].y = cy + MV_HY;
            pt[2].x = cx + MV_HX; pt[2].y = cy + MV_TH;
            pt[3].x = cx;         pt[3].y = cy + MV_HY;
            pt[4] = pt[0];
            pen = CreatePen(PS_SOLID, 2, RGB(255, 220, 0));
            old = (HPEN)SelectObject(dc, pen);
            Polyline(dc, pt, 5);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
    }
}

/* 回退：没找到 GRP 时用颜色网格（旧行为） */
static void grid_geom(HWND h, int *cw, int *ch, int *ox, int *oy)
{
    RECT rc;
    int w, hh;
    GetClientRect(h, &rc);
    w = rc.right - rc.left;
    hh = rc.bottom - rc.top;
    *cw = *ch = 1; *ox = *oy = 0;
    if (!g_mp || !g_mp->mapOk) return;
    *cw = w / g_mp->map.w;
    *ch = hh / g_mp->map.h;
    if (*cw < 1) *cw = 1;
    if (*ch < 1) *ch = 1;
    *ox = (w - *cw * g_mp->map.w) / 2;
    *oy = (hh - *ch * g_mp->map.h) / 2;
}

static void paint_grid_fallback(HWND h, HDC dc)
{
    RECT rc;
    int cw, ch, ox, oy, x, y;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    if (!g_mp || !g_mp->mapOk) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(120, 120, 120));
        DrawTextA(dc, A("（打开地图后显示真实图块；找不到 Data\\GROUND.GRP 时退化为颜色网格）"),
                  -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    grid_geom(h, &cw, &ch, &ox, &oy);
    for (y = 0; y < g_mp->map.h; y++) {
        for (x = 0; x < g_mp->map.w; x++) {
            RECT r;
            HBRUSH br = CreateSolidBrush(tile_color(sh_map_tile(&g_mp->map, x, y)));
            r.left   = ox + x * cw;
            r.top    = oy + y * ch;
            r.right  = r.left + cw;
            r.bottom = r.top + ch;
            FillRect(dc, &r, br);
            DeleteObject(br);
        }
    }
    if (g_mp->selx >= 0 && g_mp->selx < g_mp->map.w &&
        g_mp->sely >= 0 && g_mp->sely < g_mp->map.h) {
        RECT r;
        r.left   = ox + g_mp->selx * cw;
        r.top    = oy + g_mp->sely * ch;
        r.right  = r.left + cw;
        r.bottom = r.top + ch;
        FrameRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
}

static LRESULT CALLBACK grid_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (g_mp && g_mp->mapOk && g_mp->gfxOk) paint_view(h, dc);
        else paint_grid_fallback(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE:
        update_scroll(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_HSCROLL:
        on_scroll(h, SB_HORZ, (int)LOWORD(wp), (int)(short)HIWORD(wp));
        return 0;
    case WM_VSCROLL:
        on_scroll(h, SB_VERT, (int)LOWORD(wp), (int)(short)HIWORD(wp));
        return 0;
    case WM_KEYDOWN: {
        /* 方向键 / WASD 平移视图（按住可连续滚动）；Ctrl+Z / Ctrl+Y 撤销重做 */
        int bar = SB_HORZ, code = -1;
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            if (wp == 'Z' || wp == 'z') { do_undo(); return 0; }
            if (wp == 'Y' || wp == 'y') { do_redo(); return 0; }
        }
        switch (wp) {
        case VK_LEFT:  case 'A': code = SB_LINELEFT;  break;
        case VK_RIGHT: case 'D': code = SB_LINERIGHT; break;
        case VK_UP:    case 'W': code = SB_LINELEFT;  bar = SB_VERT; break;
        case VK_DOWN:  case 'S': code = SB_LINERIGHT; bar = SB_VERT; break;
        default: break;
        }
        if (code >= 0 && g_mp && g_mp->mapOk) {
            on_scroll(h, bar, code, 0);
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int x, y, dbl = (m == WM_LBUTTONDBLCLK);
        SetFocus(h);                     /* 点地图后键盘（WASD/方向键）可平移视图 */
        if (!g_mp || !g_mp->mapOk) return 0;
        if (g_mp->gfxOk) {
            if (!pick_cell(h, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), &x, &y)) return 0;
        } else {
            int cw, ch, ox, oy;
            grid_geom(h, &cw, &ch, &ox, &oy);
            x = ((int)LOWORD(lp) - ox) / cw;
            y = ((int)HIWORD(lp) - oy) / ch;
            if (x < 0 || y < 0 || x >= g_mp->map.w || y >= g_mp->map.h) return 0;
        }
        SendMessageA(g_mp->page, WM_APP + 1, dbl ? 1 : 0, MAKELONG(x, y));
        return 0;
    }
    case WM_RBUTTONDOWN: {
        int x, y;
        if (!g_mp || !g_mp->mapOk) return 0;
        if (g_mp->gfxOk) {
            if (!pick_cell(h, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), &x, &y)) return 0;
        } else {
            int cw, ch, ox, oy;
            grid_geom(h, &cw, &ch, &ox, &oy);
            x = ((int)LOWORD(lp) - ox) / cw;
            y = ((int)HIWORD(lp) - oy) / ch;
            if (x < 0 || y < 0 || x >= g_mp->map.w || y >= g_mp->map.h) return 0;
        }
        SendMessageA(g_mp->page, WM_APP + 2, 0, MAKELONG(x, y));
        return 0;
    }
    }
    return DefWindowProcA(h, m, wp, lp);
}

static void grid_repaint(void)
{
    if (g_mp && g_mp->grid) {
        update_scroll(g_mp->grid);
        InvalidateRect(g_mp->grid, NULL, TRUE);
    }
}

/* ================= 简易 tooltip（不用 comctl32） ================= */

static HWND s_tip;

static void tip_hide(void)
{
    if (s_tip) ShowWindow(s_tip, SW_HIDE);
}

static void tip_show(const char *u8, int sx, int sy)
{
    char a[900];
    HDC dc;
    HFONT f;
    RECT calc;
    int w, h;
    RECT wa;

    if (!g_mp) return;
    if (!s_tip) {
        s_tip = CreateWindowExA(WS_EX_TOOLWINDOW, "STATIC", "",
                                WS_POPUP | WS_BORDER | SS_LEFT,
                                0, 0, 10, 10, g_mp->parent, NULL, g_mp->hi, NULL);
        if (!s_tip) return;
        SendMessageA(s_tip, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
    sh_utf8_to_acp(u8, a, (int)sizeof(a));
    SetWindowTextA(s_tip, a);

    dc = GetDC(s_tip);
    f = (HFONT)SendMessageA(s_tip, WM_GETFONT, 0, 0);
    if (f) SelectObject(dc, f);
    calc.left = 0; calc.top = 0; calc.right = 420; calc.bottom = 0;
    DrawTextA(dc, a, -1, &calc, DT_CALCRECT | DT_WORDBREAK);
    ReleaseDC(s_tip, dc);
    w = calc.right + 10;
    h = calc.bottom + 8;
    if (w > 460) w = 460;

    /* 别超出屏幕右下边缘 */
    wa.left = 0; wa.top = 0;
    wa.right = GetSystemMetrics(SM_CXSCREEN);
    wa.bottom = GetSystemMetrics(SM_CYSCREEN);
    if (sx + w > wa.right)  sx = wa.right - w - 4;
    if (sy + h > wa.bottom) sy = sy - h - 24;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;

    SetWindowPos(s_tip, HWND_TOP, sx, sy, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

/* 「布阵编辑(ACT)」的 tooltip 文本 */
static const char *tip_act_text(void)
{
    return
        "「打开关卡」= 选一张 .MAP，自动载入同目录同名 .ACT（一个按钮开整关）\r\n"
        "  N01..N14 ……… 主线大图（第 1~14 张主图）\r\n"
        "  Act2-2..Act14-3 … 战斗/事件场景（剧情遭遇战等）\r\n"
        "  n-addXX ………… 剧情支线小图\r\n"
        "  train1 / train2 … 训练关\r\n"
        "改哪个：先用存档的「关卡号」查地图名（分析报告 §4.19.2 对照表）。\r\n"
        "注意：ACT 是「关卡初始状态」，改完要重新开始该关卡才生效。";
}

/* ================= ACT 记录子窗口 ================= */

/* 队别取值（报告 §4.35.3）：0=我方、1=中立/第三方、2=敌方；4=特殊（FUN_00403470 特殊分支） */
static const int  s_teamVals[] = {0, 1, 2, 4};
static const char *s_teamText[] = {"0 我方", "1 中立", "2 敌方", "4 特殊"};
/* ACT +0xF 初始行动状态码（报告 §4.19.1，行动状态机 FUN_00407930） */
static const int  s_actVals[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static const char *s_actText[] = {
    "0 无（默认）", "1 攻击指定对象", "2 选技/确认目标", "3 移动后攻击",
    "4 到达判定", "5 移动到目标", "6 施放（特殊）", "7 待机/不可选",
    "8 未定", "9 新建对象（特殊）", "10 攻击变体"
};
#define S_TEAM_N ((int)(sizeof(s_teamVals) / sizeof(s_teamVals[0])))
#define S_ACT_N  ((int)(sizeof(s_actVals) / sizeof(s_actVals[0])))

/* 组合框：按下标 ↔ 取值 互转 */
static int  combo_get_val(HWND wnd, int id, const int *vals, int n)
{
    int idx = (int)SendMessageA(GetDlgItem(wnd, id), CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= n) return -1;
    return vals[idx];
}

static void combo_set_val(HWND wnd, int id, const int *vals, int n, int v)
{
    int i;
    for (i = 0; i < n; i++) {
        if (vals[i] == v) {
            SendMessageA(GetDlgItem(wnd, id), CB_SETCURSEL, (WPARAM)i, 0);
            return;
        }
    }
    SendMessageA(GetDlgItem(wnd, id), CB_SETCURSEL, (WPARAM)-1, 0);
}

/* 填充队别 / 动作两个下拉（启动时一次） */
static void act_panel_fill_combos(void)
{
    int i;
    HWND cb;

    cb = GetDlgItem(g_mp->page, MA_TEAM);
    if (cb) {
        for (i = 0; i < S_TEAM_N; i++) SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)A(s_teamText[i]));
        SendMessageA(cb, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
    cb = GetDlgItem(g_mp->page, MA_ACT);
    if (cb) {
        for (i = 0; i < S_ACT_N; i++) SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)A(s_actText[i]));
        SendMessageA(cb, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
}

static void act_list_fill(HWND list)
{
    char b[200];
    int i;
    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    if (!g_mp->actOk) return;
    for (i = 0; i < g_mp->act.count; i++) {
        int ty = sh_act_get(&g_mp->act, i, SH_AREC_TYPE, 2);
        sprintf(b, "%d %s 队%d (%d,%d) Lv%d A%d", i, sh_char_name(ty),
                sh_act_get(&g_mp->act, i, SH_AREC_TEAM, 1),
                sh_act_get(&g_mp->act, i, SH_AREC_X, 2),
                sh_act_get(&g_mp->act, i, SH_AREC_Y, 2),
                sh_act_get(&g_mp->act, i, SH_AREC_LEVEL, 2),
                sh_act_get(&g_mp->act, i, SH_AREC_ACTION, 1));
        SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)A(b));
    }
}

/* 把当前控件值写回内存的指定记录（切换记录/关闭窗口/点应用时都调它） */
/* 面板 6 个字段是否与内存记录不同（用于决定是否记一次撤销） */
static int act_fields_differ(HWND wnd, int sel)
{
    int ty, team, x, y, lv, act;

    if (!g_mp->actOk || sel < 0 || sel >= g_mp->act.count) return 0;
    ty = (int)SendMessageA(GetDlgItem(wnd, MA_TYPE), CB_GETCURSEL, 0, 0);
    if (ty == (int)CB_ERR) ty = sh_act_get(&g_mp->act, sel, SH_AREC_TYPE, 2);
    team = combo_get_val(wnd, MA_TEAM, s_teamVals, S_TEAM_N);
    x = get_int(wnd, MA_E0 + 2);
    y = get_int(wnd, MA_E0 + 3);
    lv = get_int(wnd, MA_E0 + 4);
    act = combo_get_val(wnd, MA_ACT, s_actVals, S_ACT_N);
    if (ty != sh_act_get(&g_mp->act, sel, SH_AREC_TYPE, 2)) return 1;
    if (team >= 0 && team != sh_act_get(&g_mp->act, sel, SH_AREC_TEAM, 1)) return 1;
    if (x != sh_act_get(&g_mp->act, sel, SH_AREC_X, 2)) return 1;
    if (y != sh_act_get(&g_mp->act, sel, SH_AREC_Y, 2)) return 1;
    if (lv != sh_act_get(&g_mp->act, sel, SH_AREC_LEVEL, 2)) return 1;
    if (act >= 0 && act != sh_act_get(&g_mp->act, sel, SH_AREC_ACTION, 1)) return 1;
    return 0;
}

static void act_commit(HWND wnd, int sel)
{
    int ty;
    if (!g_mp->actOk || sel < 0 || sel >= g_mp->act.count) return;
    if (act_fields_differ(wnd, sel)) undo_push();

    ty = (int)SendMessageA(GetDlgItem(wnd, MA_TYPE), CB_GETCURSEL, 0, 0);
    if (ty == (int)CB_ERR) ty = sh_act_get(&g_mp->act, sel, SH_AREC_TYPE, 2);
    if (ty != sh_act_get(&g_mp->act, sel, SH_AREC_TYPE, 2)) {
        sh_act_set(&g_mp->act, sel, SH_AREC_TYPE, 2, ty);
        g_mp->actDirty = 1;
    }
    if (combo_get_val(wnd, MA_TEAM, s_teamVals, S_TEAM_N) >= 0 &&
        combo_get_val(wnd, MA_TEAM, s_teamVals, S_TEAM_N) != sh_act_get(&g_mp->act, sel, SH_AREC_TEAM, 1)) {
        sh_act_set(&g_mp->act, sel, SH_AREC_TEAM, 1, combo_get_val(wnd, MA_TEAM, s_teamVals, S_TEAM_N));
        g_mp->actDirty = 1;
    }
    if (get_int(wnd, MA_E0 + 2) != sh_act_get(&g_mp->act, sel, SH_AREC_X, 2)) {
        sh_act_set(&g_mp->act, sel, SH_AREC_X, 2, get_int(wnd, MA_E0 + 2));
        g_mp->actDirty = 1;
    }
    if (get_int(wnd, MA_E0 + 3) != sh_act_get(&g_mp->act, sel, SH_AREC_Y, 2)) {
        sh_act_set(&g_mp->act, sel, SH_AREC_Y, 2, get_int(wnd, MA_E0 + 3));
        g_mp->actDirty = 1;
    }
    if (get_int(wnd, MA_E0 + 4) != sh_act_get(&g_mp->act, sel, SH_AREC_LEVEL, 2)) {
        sh_act_set(&g_mp->act, sel, SH_AREC_LEVEL, 2, get_int(wnd, MA_E0 + 4));
        g_mp->actDirty = 1;
    }
    if (combo_get_val(wnd, MA_ACT, s_actVals, S_ACT_N) >= 0 &&
        combo_get_val(wnd, MA_ACT, s_actVals, S_ACT_N) != sh_act_get(&g_mp->act, sel, SH_AREC_ACTION, 1)) {
        sh_act_set(&g_mp->act, sel, SH_AREC_ACTION, 1, combo_get_val(wnd, MA_ACT, s_actVals, S_ACT_N));
        g_mp->actDirty = 1;
    }
}

static void act_show_sel(HWND wnd)
{
    int sel, ty;
    if (!g_mp->actOk) return;
    sel = g_mp->actSel;
    if (sel < 0 || sel >= g_mp->act.count) return;
    ty = sh_act_get(&g_mp->act, sel, SH_AREC_TYPE, 2);
    SendMessageA(GetDlgItem(wnd, MA_TYPE), CB_SETCURSEL, (WPARAM)ty, 0);
    combo_set_val(wnd, MA_TEAM, s_teamVals, S_TEAM_N, sh_act_get(&g_mp->act, sel, SH_AREC_TEAM, 1));
    set_int(wnd, MA_E0 + 2, sh_act_get(&g_mp->act, sel, SH_AREC_X, 2));
    set_int(wnd, MA_E0 + 3, sh_act_get(&g_mp->act, sel, SH_AREC_Y, 2));
    set_int(wnd, MA_E0 + 4, sh_act_get(&g_mp->act, sel, SH_AREC_LEVEL, 2));
    combo_set_val(wnd, MA_ACT, s_actVals, S_ACT_N, sh_act_get(&g_mp->act, sel, SH_AREC_ACTION, 1));
}

/* 刷新列表文字并保持选中行 */
static void act_refresh_list(HWND wnd, int sel)
{
    act_list_fill(GetDlgItem(wnd, MA_LIST));
    if (sel >= 0) SendMessageA(GetDlgItem(wnd, MA_LIST), LB_SETCURSEL, (WPARAM)sel, 0);
}

static HWND mk_child(HWND parent, const char *cls, const char *txt, DWORD st,
                     int x, int y, int cx, int cy, int id)
{
    char a[512];
    HWND c;
    sh_utf8_to_acp(txt ? txt : "", a, (int)sizeof(a));
    c = CreateWindowExA(0, cls, a, WS_CHILD | WS_VISIBLE | st, x, y, cx, cy,
                        parent, (HMENU)(size_t)id, g_mp->hi, NULL);
    if (c && (st & WS_TABSTOP)) SendMessageA(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return c;
}

#if 0  /* 旧的弹窗式布阵编辑（v4.33 起改为页面内面板，代码保留备查） */
static LRESULT CALLBACK actwnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (m == WM_COMMAND) {
        switch (LOWORD(wp)) {
        case MA_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) {
                int nsel = (int)SendMessageA(GetDlgItem(h, MA_LIST), LB_GETCURSEL, 0, 0);
                if (nsel != s_actSel) {
                    act_commit(h, s_actSel);      /* 先暂存上一条 */
                    s_actSel = nsel;
                    act_show_sel(h);
                    refresh();
                }
                return 0;
            }
            break;
        case MA_APPLY:
            act_commit(h, s_actSel);
            act_refresh_list(h, s_actSel);
            act_show_sel(h);
            refresh();
            return 0;
        case MA_OPEN:
        {
            char p[512], last[512];
            act_commit(h, s_actSel);
            ini_get("LastAct", last, sizeof(last));
            if (pick_file(h, filter_1("布阵表 (*.ACT)", "*.ACT"), last, p, sizeof(p))) {
                if (load_act_path(p) != 0) {
                    MessageBoxA(h, "不是有效的 ACT（需 11 B 头 + 38 B 记录、dataOffset=11）",
                                "打开失败", MB_ICONERROR);
                    return 0;
                }
                ini_set("LastAct", p);
                act_list_fill(GetDlgItem(h, MA_LIST));
                s_actSel = 0;
                SendMessageA(GetDlgItem(h, MA_LIST), LB_SETCURSEL, 0, 0);
                act_show_sel(h);
                refresh();
            }
            return 0;
        }
        case MA_CLOSE:
            act_commit(h, s_actSel);
            refresh();
            s_actWnd = NULL;
            DestroyWindow(h);
            return 0;
        }
    } else if (m == WM_CLOSE) {
        act_commit(h, s_actSel);
        refresh();
        s_actWnd = NULL;
        DestroyWindow(h);
        return 0;
    } else if (m == WM_DESTROY) {
        s_actWnd = NULL;
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

static void open_act_window(void)
{
    static const char *alab[6] = { "类型", "队别", "X", "Y", "等级", "动作" };
    WNDCLASSA wc;
    HWND w, cb;
    int i, n, id;

    if (!g_mp->actOk) {
        MessageBoxA(g_mp->page, "先点「布阵编辑(ACT)」选一个 .ACT 文件", "布阵编辑", MB_ICONWARNING);
        return;
    }
    if (s_actWnd && IsWindow(s_actWnd)) {      /* 已经开着：前置即可，不重复建窗 */
        SetForegroundWindow(s_actWnd);
        return;
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = actwnd_proc;
    wc.hInstance     = g_mp->hi;
    wc.lpszClassName = "SHMAACT";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

    w = CreateWindowExA(WS_EX_DLGMODALFRAME, "SHMAACT", A("布阵记录编辑"),
                        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                        CW_USEDEFAULT, CW_USEDEFAULT, 600, 570,
                        g_mp->parent, NULL, g_mp->hi, NULL);
    if (!w) return;
    s_actWnd = w;

    CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
                    12, 12, 310, 440, w, (HMENU)(size_t)MA_LIST, g_mp->hi, NULL);

    /* 类型：下拉列表（id + 名字），避免手抄 id */
    mk_child(w, "STATIC", "类型:", SS_LEFT, 336, 20, 56, 22, 0);
    cb = CreateWindowExA(0, "COMBOBOX", "",
                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                         392, 16, 170, 300, w, (HMENU)(size_t)MA_TYPE, g_mp->hi, NULL);
    if (cb) {
        n = sh_char_name_count();
        for (id = 0; id < n; id++) {
            char it[140];
            sprintf(it, "%d %s", id, sh_char_name(id));
            SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)A(it));
        }
        SendMessageA(cb, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
    for (i = 1; i < 6; i++) {
        char lab[32];
        sprintf(lab, "%s:", alab[i]);
        mk_child(w, "STATIC", lab, SS_LEFT, 336, 20 + i * 40, 56, 22, 0);
        mk_child(w, "EDIT", "0", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
                 392, 16 + i * 40, 100, 26, MA_E0 + i);
    }
    mk_child(w, "STATIC",
             "改动先在内存暂存（切记录不丢），回主页面点「写入文件」统一保存。\r\n"
             "ACT 是关卡初始布阵：已有存档里的单位来自存档，需重新开始该关才生效。",
             SS_LEFT, 12, 466, 572, 56, 0);
    mk_child(w, "BUTTON", "暂存并刷新列表", BS_PUSHBUTTON | WS_TABSTOP, 336, 330, 160, 28, MA_APPLY);
    mk_child(w, "BUTTON", "打开其它 ACT...", BS_PUSHBUTTON | WS_TABSTOP, 336, 364, 160, 28, MA_OPEN);
    mk_child(w, "BUTTON", "关闭", BS_PUSHBUTTON | WS_TABSTOP, 336, 398, 160, 28, MA_CLOSE);

    act_list_fill(GetDlgItem(w, MA_LIST));
    if (g_mp->actSel < 0 || g_mp->actSel >= g_mp->act.count) g_mp->actSel = 0;
    s_actSel = g_mp->actSel;
    SendMessageA(GetDlgItem(w, MA_LIST), LB_SETCURSEL, (WPARAM)s_actSel, 0);
    act_show_sel(w);

    SetForegroundWindow(w);
    SetFocus(GetDlgItem(w, MA_LIST));
}

#endif  /* 旧弹窗式布阵编辑 */

/* ================= 布阵面板（页面内，无弹窗） ================= */

static void act_combo_fill(void)
{
    HWND cb = GetDlgItem(g_mp->page, MA_TYPE);
    int id, n;
    if (!cb) return;
    n = sh_char_name_count();
    for (id = 0; id < n; id++) {
        char it[140];
        sprintf(it, "%d %s", id, sh_char_name(id));
        SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)A(it));
    }
    SendMessageA(cb, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

/* ACT 载入/切换后刷新整个面板 */
static void act_panel_reset(void)
{
    HWND lb = GetDlgItem(g_mp->page, MA_LIST);
    act_list_fill(lb);
    if (!g_mp->actOk || g_mp->act.count <= 0) { g_mp->actSel = -1; return; }
    if (g_mp->actSel < 0 || g_mp->actSel >= g_mp->act.count) g_mp->actSel = 0;
    SendMessageA(lb, LB_SETCURSEL, (WPARAM)g_mp->actSel, 0);
    act_show_sel(g_mp->page);
}

/* 占位：地图与布阵固定同名配对，「打开其它 ACT...」已移除 */

/* 面板列表选择变化：先暂存上一条，再显示新的，并把地图滚到该单位 */
static void act_panel_selchanged(void)
{
    int nsel = (int)SendMessageA(GetDlgItem(g_mp->page, MA_LIST), LB_GETCURSEL, 0, 0);
    if (nsel < 0 || nsel == g_mp->actSel) return;
    act_commit(g_mp->page, g_mp->actSel);
    g_mp->actSel = nsel;
    act_show_sel(g_mp->page);
    {   /* 自动定位：把该单位所在格滚到视口中央，并同步当前格 */
        int ux = sh_act_get(&g_mp->act, nsel, SH_AREC_X, 2);
        int uy = sh_act_get(&g_mp->act, nsel, SH_AREC_Y, 2);
        if (g_mp->mapOk && ux >= 0 && ux < g_mp->map.w && uy >= 0 && uy < g_mp->map.h) {
            g_mp->selx = ux;
            g_mp->sely = uy;
            sync_sel_fields();
            scroll_to_cell(ux, uy);
        }
    }
    refresh();
    grid_repaint();
}

/* 取消列表选中并清空字段（点到没有单位的格子时） */
static void act_clear_sel(void)
{
    int k;
    g_mp->actSel = -1;
    SendMessageA(GetDlgItem(g_mp->page, MA_LIST), LB_SETCURSEL, (WPARAM)-1, 0);
    SendMessageA(GetDlgItem(g_mp->page, MA_TYPE), CB_SETCURSEL, (WPARAM)-1, 0);
    SendMessageA(GetDlgItem(g_mp->page, MA_TEAM), CB_SETCURSEL, (WPARAM)-1, 0);
    SendMessageA(GetDlgItem(g_mp->page, MA_ACT), CB_SETCURSEL, (WPARAM)-1, 0);
    for (k = 2; k <= 4; k++) SetDlgItemTextA(g_mp->page, MA_E0 + k, "");
}

/* 把当前编辑中的记录写回内存并刷新列表/地图（离开输入框或下拉变化时自动调用） */
static void act_apply_live(void)
{
    int ux, uy;

    if (!g_mp->actOk || g_mp->act.count <= 0) return;
    act_commit(g_mp->page, g_mp->actSel);
    act_refresh_list(g_mp->page, g_mp->actSel);
    act_show_sel(g_mp->page);
    ux = sh_act_get(&g_mp->act, g_mp->actSel, SH_AREC_X, 2);
    uy = sh_act_get(&g_mp->act, g_mp->actSel, SH_AREC_Y, 2);
    if (g_mp->mapOk && ux >= 0 && ux < g_mp->map.w && uy >= 0 && uy < g_mp->map.h) {
        g_mp->selx = ux;
        g_mp->sely = uy;
        sync_sel_fields();
        scroll_to_cell(ux, uy);
    }
    refresh();
    grid_repaint();
}

/* ================= 图块浏览器（GROUND.GRP 分页预览，点选填入 tileId） ================= */

#define TP_PREV  451
#define TP_NEXT  452
#define TP_OK    453
#define TP_CANCEL 454
#define TP_COLS  6
#define TP_ROWS  8
#define TP_PAGE  (TP_COLS * TP_ROWS)
#define TP_CW    100
#define TP_CH    56

static HWND     s_tpWnd;
static int      s_tpPage;
static int      s_tpSel = -1;   /* 待确认的图块（点选后高亮，点「确定」才应用） */
static uint8_t *s_tpBuf;
static int      s_tpStride;

static int tp_count(void)
{
    return g_mp && g_mp->gfxOk ? g_mp->grnd.count : 0;
}

static void tp_paint(HWND h, HDC dc)
{
    int w = TP_COLS * TP_CW, hh = TP_ROWS * TP_CH;
    int need, k, cur;

    s_tpStride = ((w * 3 + 3) / 4) * 4;
    need = s_tpStride * hh;
    if (need > 0) {
        uint8_t *nb = (uint8_t *)realloc(s_tpBuf, (size_t)need);
        if (!nb) return;
        s_tpBuf = nb;
    }
    if (!s_tpBuf) return;
    memset(s_tpBuf, 40, (size_t)need);   /* 深灰底 */

    cur = get_int(g_mp->page, MP_TILE) - 1;
    for (k = 0; k < TP_PAGE; k++) {
        int idx = s_tpPage * TP_PAGE + k;
        int cx = (k % TP_COLS) * TP_CW, cy = (k / TP_COLS) * TP_CH;
        int ix, iy;
        int x, y;
        int hi = 0;   /* 0 普通 1 当前格图块 2 待确认选中 */
        if (idx == s_tpSel) hi = 2;
        else if (idx == cur) hi = 1;
        /* 格子边框 + 高亮 */
        for (x = 0; x < TP_CW; x++) {
            uint8_t *d0 = s_tpBuf + (size_t)cy * s_tpStride + (cx + x) * 3;
            uint8_t *d1 = s_tpBuf + (size_t)(cy + TP_CH - 1) * s_tpStride + (cx + x) * 3;
            if (hi == 2) { d0[0] = d1[0] = 255; d0[1] = d1[1] = 220; d0[2] = d1[2] = 0; }
            else if (hi == 1) { d0[0] = d1[0] = 120; d0[1] = d1[1] = 200; d0[2] = d1[2] = 255; }
            else { d0[0] = d1[0] = d0[1] = d1[1] = d0[2] = d1[2] = 70; }
        }
        for (y = 0; y < TP_CH; y++) {
            uint8_t *dl = s_tpBuf + (size_t)(cy + y) * s_tpStride + cx * 3;
            uint8_t *dr = s_tpBuf + (size_t)(cy + y) * s_tpStride + (cx + TP_CW - 1) * 3;
            if (hi == 2) { dl[0] = dr[0] = 255; dl[1] = dr[1] = 220; dl[2] = dr[2] = 0; }
            else if (hi == 1) { dl[0] = dr[0] = 120; dl[1] = dr[1] = 200; dl[2] = dr[2] = 255; }
            else { dl[0] = dr[0] = dl[1] = dr[1] = dl[2] = dr[2] = 70; }
        }
        if (idx < 0 || idx >= tp_count()) continue;
        {
            uint8_t *px = NULL;
            int pw = 0, ph = 0;
            if (sh_grp_decode(&g_mp->grnd, idx, &px, &pw, &ph) != 0 || !px) continue;
            for (iy = 0; iy < ph && iy < TP_CH - 10; iy++) {
                for (ix = 0; ix < pw && ix < TP_CW - 10; ix++) {
                    uint8_t c = px[(size_t)iy * pw + ix];
                    uint8_t *d;
                    if (!c) continue;
                    d = s_tpBuf + (size_t)(cy + 5 + iy) * s_tpStride + (cx + 5 + ix) * 3;
                    d[0] = g_mp->pal.rgb[c][0];
                    d[1] = g_mp->pal.rgb[c][1];
                    d[2] = g_mp->pal.rgb[c][2];
                }
            }
            free(px);
        }
    }
    {
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -hh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 24;
        bi.bmiHeader.biCompression = BI_RGB;
        bi.bmiHeader.biSizeImage = (DWORD)(s_tpStride * hh);
        StretchDIBits(dc, 10, 68, w, hh, 0, 0, w, hh, s_tpBuf, &bi, DIB_RGB_COLORS, SRCCOPY);
    }
    {
        char cap[128];
        int total = tp_count();
        int from = s_tpPage * TP_PAGE + 1;
        int to = from + TP_PAGE - 1;
        RECT r;
        HBRUSH bg;
        if (to > total) to = total;
        r.left = 0; r.top = 0; r.right = 640; r.bottom = 66;
        bg = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(dc, &r, bg);
        if (total <= 0) sprintf(cap, "没有可用的 GROUND.GRP（先打开地图）");
        else if (s_tpSel >= 0) sprintf(cap, "第 %d - %d 号 / 共 %d 个图块    已选：第 %d 号（点「确定」应用到当前格）",
                                       from, to, total, s_tpSel + 1);
        else sprintf(cap, "第 %d - %d 号 / 共 %d 个图块    点选一个图块后再点「确定」",
                     from, to, total);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 0));
        {
            const char *ansi = A(cap);
            TextOutA(dc, 10, 10, ansi, (int)strlen(ansi));
        }
        (void)h;
    }
}

/* 把待确认的图块填入 tileId 并应用到当前选中格 */
static void tp_apply(void)
{
    if (!g_mp || !g_mp->gfxOk || s_tpSel < 0) return;
    SetDlgItemInt(g_mp->page, MP_TILE, s_tpSel + 1, FALSE);
    do_apply_tile();
}

static void tp_click(HWND h, int mx, int my, int dbl)
{
    int col, row, k, idx;

    mx -= 10;
    my -= 68;
    if (mx < 0 || my < 0) return;
    col = mx / TP_CW;
    row = my / TP_CH;
    if (col < 0 || col >= TP_COLS || row < 0 || row >= TP_ROWS) return;
    k = row * TP_COLS + col;
    idx = s_tpPage * TP_PAGE + k;
    if (idx < 0 || idx >= tp_count()) return;
    s_tpSel = idx;
    InvalidateRect(h, NULL, TRUE);
    if (dbl) {
        tp_apply();
        DestroyWindow(h);
    }
}

static LRESULT CALLBACK tilepick_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        tp_paint(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        tp_click(h, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), 0);
        return 0;
    case WM_LBUTTONDBLCLK:
        tp_click(h, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), 1);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case TP_PREV:
            if (s_tpPage > 0) { s_tpPage--; InvalidateRect(h, NULL, TRUE); }
            return 0;
        case TP_NEXT:
            if ((s_tpPage + 1) * TP_PAGE < tp_count()) { s_tpPage++; InvalidateRect(h, NULL, TRUE); }
            return 0;
        case TP_OK:
            tp_apply();
            DestroyWindow(h);
            return 0;
        case TP_CANCEL:
            DestroyWindow(h);
            return 0;
        }
        return 0;
    case WM_DESTROY:
        s_tpWnd = NULL;
        s_tpSel = -1;
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

static void open_tilepick(void)
{
    HINSTANCE hi = (HINSTANCE)GetModuleHandleA(NULL);
    int cur;

    if (!g_mp || !g_mp->gfxOk) {
        MessageBoxA(g_mp->page, "先打开地图（需要 GROUND.GRP 做预览）", "图块浏览", MB_ICONWARNING);
        return;
    }
    if (s_tpWnd) {
        SetForegroundWindow(s_tpWnd);
        InvalidateRect(s_tpWnd, NULL, TRUE);
        return;
    }
    {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = tilepick_proc;
        wc.hInstance = hi;
        wc.lpszClassName = "SHTILEPICK";
        wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.style = CS_DBLCLKS;
        RegisterClassA(&wc);
    }
    cur = get_int(g_mp->page, MP_TILE) - 1;
    if (cur < 0) cur = 0;
    s_tpPage = cur / TP_PAGE;
    s_tpSel = cur;
    s_tpWnd = CreateWindowExA(0, "SHTILEPICK", A("图块浏览 - GROUND.GRP"),
                              WS_POPUP | WS_CAPTION | WS_SYSMENU,
                              CW_USEDEFAULT, CW_USEDEFAULT, 640, 570,
                              g_mp->page, NULL, hi, NULL);
    if (!s_tpWnd) return;
    {
        HWND b;
        b = CreateWindowExA(0, "BUTTON", A("上一页"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            10, 34, 80, 26, s_tpWnd, (HMENU)(size_t)TP_PREV, hi, NULL);
        SendMessageA(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        b = CreateWindowExA(0, "BUTTON", A("下一页"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            98, 34, 80, 26, s_tpWnd, (HMENU)(size_t)TP_NEXT, hi, NULL);
        SendMessageA(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        b = CreateWindowExA(0, "BUTTON", A("确定"), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            440, 34, 90, 26, s_tpWnd, (HMENU)(size_t)TP_OK, hi, NULL);
        SendMessageA(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        b = CreateWindowExA(0, "BUTTON", A("取消"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            538, 34, 90, 26, s_tpWnd, (HMENU)(size_t)TP_CANCEL, hi, NULL);
        SendMessageA(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
    ShowWindow(s_tpWnd, SW_SHOW);
    SetForegroundWindow(s_tpWnd);
}

/* ================= 撤销 / 重做（整关快照，内存内） ================= */

#define UNDO_MAX 32

typedef struct {
    int      used;
    uint8_t *mapData; size_t mapSize; char mapPath[SH_MAP_PATH];
    uint8_t *actData; size_t actSize; char actPath[SH_ACT_PATH];
    int      selx, sely, actSel;
} UndoState;

static UndoState s_undo[UNDO_MAX], s_redo[UNDO_MAX];
static int       s_undoN, s_redoN;

static void undo_state_free(UndoState *s)
{
    if (!s) return;
    free(s->mapData);
    free(s->actData);
    memset(s, 0, sizeof(*s));
}

static void undo_update(void)
{
    HWND u, r;
    if (!g_mp || !g_mp->page) return;
    u = GetDlgItem(g_mp->page, MP_UNDO);
    r = GetDlgItem(g_mp->page, MP_REDO);
    if (u) EnableWindow(u, s_undoN > 0);
    if (r) EnableWindow(r, s_redoN > 0);
}

/* 把当前内存状态复制一份（MAP + ACT 原始字节） */
static int undo_capture(UndoState *s)
{
    memset(s, 0, sizeof(*s));
    if (g_mp->mapOk && g_mp->map.data && g_mp->map.size) {
        s->mapData = (uint8_t *)malloc(g_mp->map.size);
        if (!s->mapData) return -1;
        memcpy(s->mapData, g_mp->map.data, g_mp->map.size);
        s->mapSize = g_mp->map.size;
        strncpy(s->mapPath, g_mp->map.path, sizeof(s->mapPath) - 1);
    }
    if (g_mp->actOk && g_mp->act.data && g_mp->act.size) {
        s->actData = (uint8_t *)malloc(g_mp->act.size);
        if (!s->actData) { free(s->mapData); s->mapData = NULL; return -1; }
        memcpy(s->actData, g_mp->act.data, g_mp->act.size);
        s->actSize = g_mp->act.size;
        strncpy(s->actPath, g_mp->act.path, sizeof(s->actPath) - 1);
    }
    s->selx = g_mp->selx;
    s->sely = g_mp->sely;
    s->actSel = g_mp->actSel;
    s->used = 1;
    return 0;
}

/* 用快照覆盖当前内存（并刷新界面） */
static void undo_restore(const UndoState *s)
{
    if (g_mp->mapOk) sh_map_free(&g_mp->map);
    if (g_mp->actOk) sh_act_free(&g_mp->act);
    g_mp->mapOk = 0;
    g_mp->actOk = 0;

    if (s->mapData) {
        uint8_t *cp = (uint8_t *)malloc(s->mapSize);
        if (cp) {
            memcpy(cp, s->mapData, s->mapSize);
            memset(&g_mp->map, 0, sizeof(g_mp->map));
            strncpy(g_mp->map.path, s->mapPath, SH_MAP_PATH - 1);
            if (sh_map_adopt(&g_mp->map, cp, s->mapSize) == 0) g_mp->mapOk = 1;
        }
    }
    if (s->actData) {
        uint8_t *cp = (uint8_t *)malloc(s->actSize);
        memset(&g_mp->act, 0, sizeof(g_mp->act));
        strncpy(g_mp->act.path, s->actPath, SH_ACT_PATH - 1);
        if (cp) {
            memcpy(cp, s->actData, s->actSize);
            g_mp->act.data = cp;
            g_mp->act.size = s->actSize;
            g_mp->act.count = cp[5];
            g_mp->actOk = 1;
        }
    }
    g_mp->selx = s->selx;
    g_mp->sely = s->sely;
    g_mp->actSel = s->actSel;
    g_mp->mapDirty = 1;
    g_mp->actDirty = 1;

    if (g_mp->actOk) act_panel_reset();
    else SendMessageA(GetDlgItem(g_mp->page, MA_LIST), LB_RESETCONTENT, 0, 0);
    sync_sel_fields();
    refresh();
    grid_repaint();
}

/* 修改前调用：把"改之前"的状态压入撤销栈，并清空重做栈 */
static void undo_push(void)
{
    UndoState st;
    int i;

    if (!g_mp) return;
    if (undo_capture(&st) != 0) return;
    for (i = 0; i < s_redoN; i++) undo_state_free(&s_redo[i]);
    s_redoN = 0;
    if (s_undoN >= UNDO_MAX) {
        undo_state_free(&s_undo[0]);
        memmove(&s_undo[0], &s_undo[1], sizeof(UndoState) * (UNDO_MAX - 1));
        s_undoN--;
    }
    s_undo[s_undoN++] = st;
    undo_update();
}

static void do_undo(void)
{
    UndoState cur, st;

    if (s_undoN <= 0) return;
    if (undo_capture(&cur) != 0) return;
    if (s_redoN >= UNDO_MAX) {
        undo_state_free(&s_redo[0]);
        memmove(&s_redo[0], &s_redo[1], sizeof(UndoState) * (UNDO_MAX - 1));
        s_redoN--;
    }
    s_redo[s_redoN++] = cur;
    st = s_undo[--s_undoN];
    undo_restore(&st);
    undo_state_free(&st);
    undo_update();
    SetFocus(g_mp->grid);
}

static void do_redo(void)
{
    UndoState cur, st;

    if (s_redoN <= 0) return;
    if (undo_capture(&cur) != 0) return;
    if (s_undoN >= UNDO_MAX) {
        undo_state_free(&s_undo[0]);
        memmove(&s_undo[0], &s_undo[1], sizeof(UndoState) * (UNDO_MAX - 1));
        s_undoN--;
    }
    s_undo[s_undoN++] = cur;
    st = s_redo[--s_redoN];
    undo_restore(&st);
    undo_state_free(&st);
    undo_update();
    SetFocus(g_mp->grid);
}

/* 打开新关卡 / 全部关闭时清空历史 */
static void undo_clear(void)
{
    int i;
    for (i = 0; i < s_undoN; i++) undo_state_free(&s_undo[i]);
    for (i = 0; i < s_redoN; i++) undo_state_free(&s_redo[i]);
    s_undoN = s_redoN = 0;
    undo_update();
}

/* 复制文件（用于把 .bak 覆盖回原文件） */
static int file_copy(const char *src, const char *dst)
{
    FILE *in, *out;
    char buf[8192];
    size_t n;

    in = fopen(src, "rb");
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
    }
    fclose(in);
    fclose(out);
    return 0;
}

/* 在 orig 所在目录里找 <原名>_YYYYMMDD-HHMMSS.bak 中文件名最大的一个（时间戳可排序）。
 * 找到返回 0 并写 out；否则返回 -1。 */
static int find_latest_bak(const char *orig, char *out, int outsz)
{
    char dir[512], base[128], pattern[700];
    const char *p;
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int found = 0;

    if (!orig || !orig[0]) return -1;
    strncpy(dir, orig, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    p = strrchr(dir, '\\');
    if (!p) return -1;
    p++;
    strncpy(base, p, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    *((char *)p) = '\0';
    if (dir[0] == '\0') strcpy(dir, ".");

    sprintf(pattern, "%s\\%s_*.bak", dir, base);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!found || strcmp(fd.cFileName, out) > 0) {
            strncpy(out, fd.cFileName, (size_t)outsz - 1);
            out[outsz - 1] = '\0';
            found = 1;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    if (!found) return -1;
    {
        char tmp[700];
        strncpy(tmp, out, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        sprintf(out, "%s\\%s", dir, tmp);
    }
    return 0;
}

/* 从最新的 .bak 恢复 MAP/ACT 到磁盘并重新载入 */
static void do_restore_bak(const char *bakmap, const char *bakact)
{
    char mapPath[512], actPath[512], mbak[600], abak[600];
    char msg[900];
    char stamp[600];

    if (!g_mp->mapOk || !g_mp->mapPath[0]) {
        MessageBoxA(g_mp->page, "先打开关卡（需要知道 MAP 路径才能找备份）", "恢复备份", MB_ICONWARNING);
        return;
    }
    strncpy(mbak, bakmap ? bakmap : "", sizeof(mbak) - 1);
    mbak[sizeof(mbak) - 1] = '\0';
    strncpy(abak, bakact ? bakact : "", sizeof(abak) - 1);
    abak[sizeof(abak) - 1] = '\0';
    strncpy(mapPath, g_mp->mapPath, sizeof(mapPath) - 1);
    mapPath[sizeof(mapPath) - 1] = '\0';
    strncpy(actPath, g_mp->actOk ? g_mp->actPath : "", sizeof(actPath) - 1);
    actPath[sizeof(actPath) - 1] = '\0';
    if (!actPath[0]) {   /* 没载入 ACT 时按同名推导 */
        char *dot;
        strncpy(actPath, mapPath, sizeof(actPath) - 1);
        actPath[sizeof(actPath) - 1] = '\0';
        dot = strrchr(actPath, '.');
        if (dot) strcpy(dot, ".ACT");
    }
    if (!mbak[0] && !abak[0]) {
        MessageBoxA(g_mp->page, "没有选择备份文件", "恢复备份", MB_ICONWARNING);
        return;
    }

    sprintf(msg, "将从备份恢复：\r\nMAP: %s\r\nACT: %s\r\n\r\n当前文件会先自动备份一份。继续？",
            mbak[0] ? mbak : "（无备份，保持不动）",
            abak[0] ? abak : "（无备份，保持不动）");
    if (MessageBoxA(g_mp->page, msg, "恢复备份", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    /* 1) 先备份当前文件，便于反悔 */
    sh_map_backup(&g_mp->map, stamp, sizeof(stamp));
    if (g_mp->actOk) sh_act_backup(&g_mp->act, stamp, sizeof(stamp));

    /* 2) 校验备份可用再覆盖 */
    if (mbak[0]) {
        sh_map chk;
        if (sh_map_load(mbak, &chk) != 0) {
            MessageBoxA(g_mp->page, "MAP 备份文件损坏，未恢复", "恢复备份", MB_ICONERROR);
            return;
        }
        sh_map_free(&chk);
        if (file_copy(mbak, mapPath) != 0) {
            MessageBoxA(g_mp->page, "写回 MAP 失败（文件只读？）", "恢复备份", MB_ICONERROR);
            return;
        }
    }
    if (abak[0]) {
        sh_act chk;
        if (sh_act_load(abak, &chk) != 0) {
            MessageBoxA(g_mp->page, "ACT 备份文件损坏，未恢复", "恢复备份", MB_ICONERROR);
            return;
        }
        sh_act_free(&chk);
        if (file_copy(abak, actPath) != 0) {
            MessageBoxA(g_mp->page, "写回 ACT 失败（文件只读？）", "恢复备份", MB_ICONERROR);
            return;
        }
    }

    /* 3) 重新载入并刷新 */
    load_map_path(mapPath);
    if (abak[0]) load_act_path(actPath);
    undo_clear();
    sync_sel_fields();
    act_panel_reset();
    refresh();
    grid_repaint();
    sprintf(msg, "已恢复：\r\nMAP: %s\r\nACT: %s", mbak[0] ? mbak : "（未动）", abak[0] ? abak : "（未动）");
    MessageBoxA(g_mp->page, msg, "恢复备份", MB_ICONINFORMATION);
}

/* ================= 备份列表（手动挑一份 .bak 恢复） ================= */

#define BAK_MAX 64

typedef struct {
    char suffix[48];
    char mapBak[600];
    char actBak[600];
    long mapSize;
    long actSize;
} BakItem;

static BakItem s_bak[BAK_MAX];
static int     s_bakN;
static HWND    s_bakWnd;

static void bak_split(const char *orig, char *dir, int dn, char *base, int bn)
{
    const char *p;
    strncpy(dir, orig, (size_t)dn - 1);
    dir[dn - 1] = '\0';
    p = strrchr(dir, '\\');
    if (!p) { base[0] = '\0'; return; }
    p++;
    strncpy(base, p, (size_t)bn - 1);
    base[bn - 1] = '\0';
    *((char *)p) = '\0';
    if (!dir[0]) strcpy(dir, ".");
}

/* 收集 orig 同目录的 <原名>_<suffix>.bak（isMap 决定填哪栏） */
static void bak_collect(const char *orig, int isMap)
{
    char dir[512], base[128], pattern[700], full[700];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    if (!orig || !orig[0]) return;
    bak_split(orig, dir, (int)sizeof(dir), base, (int)sizeof(base));
    if (!base[0]) return;
    sprintf(pattern, "%s\\%s_*.bak", dir, base);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        size_t bl, nl, sl;
        const char *name = fd.cFileName;
        char suffix[48];
        int k, found = -1;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        bl = strlen(base);
        nl = strlen(name);
        if (nl < bl + 5) continue;
        if (strncmp(name, base, bl) != 0 || name[bl] != '_') continue;
        if (nl < 4 || strcmp(name + nl - 4, ".bak") != 0) continue;
        sl = nl - bl - 1 - 4;
        if (sl == 0 || sl >= sizeof(suffix)) continue;
        memcpy(suffix, name + bl + 1, sl);
        suffix[sl] = '\0';

        for (k = 0; k < s_bakN; k++) {
            if (strcmp(s_bak[k].suffix, suffix) == 0) { found = k; break; }
        }
        if (found < 0) {
            if (s_bakN >= BAK_MAX) continue;
            found = s_bakN++;
            memset(&s_bak[found], 0, sizeof(BakItem));
            strncpy(s_bak[found].suffix, suffix, sizeof(s_bak[found].suffix) - 1);
        }
        sprintf(full, "%s\\%s", dir, name);
        if (isMap) {
            strncpy(s_bak[found].mapBak, full, sizeof(s_bak[found].mapBak) - 1);
            s_bak[found].mapSize = fd.nFileSizeLow;
        } else {
            strncpy(s_bak[found].actBak, full, sizeof(s_bak[found].actBak) - 1);
            s_bak[found].actSize = fd.nFileSizeLow;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static int bak_cmp_desc(const void *a, const void *b)
{
    const BakItem *x = (const BakItem *)a;
    const BakItem *y = (const BakItem *)b;
    return strcmp(y->suffix, x->suffix);      /* 新的排前面 */
}

static void bak_fill(void)
{
    char actPath[512];

    s_bakN = 0;
    if (!g_mp || !g_mp->mapPath[0]) return;
    bak_collect(g_mp->mapPath, 1);
    if (g_mp->actOk && g_mp->actPath[0]) {
        bak_collect(g_mp->actPath, 0);
    } else {
        const char *dot;
        strncpy(actPath, g_mp->mapPath, sizeof(actPath) - 1);
        actPath[sizeof(actPath) - 1] = '\0';
        dot = strrchr(actPath, '.');
        if (dot) strcpy((char *)dot, ".ACT");
        bak_collect(actPath, 0);
    }
    if (s_bakN > 1) qsort(s_bak, (size_t)s_bakN, sizeof(BakItem), bak_cmp_desc);
}

static LRESULT CALLBACK bakwnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case 461: {   /* 恢复选中备份 */
            int sel = (int)SendMessageA(GetDlgItem(h, 460), LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= s_bakN) {
                MessageBoxA(h, "先在上面选一个备份时间点", "恢复备份", MB_ICONWARNING);
                return 0;
            }
            {
                char mb[600], ab[600];
                strncpy(mb, s_bak[sel].mapBak, sizeof(mb) - 1); mb[sizeof(mb) - 1] = '\0';
                strncpy(ab, s_bak[sel].actBak, sizeof(ab) - 1); ab[sizeof(ab) - 1] = '\0';
                DestroyWindow(h);
                do_restore_bak(mb[0] ? mb : NULL, ab[0] ? ab : NULL);
            }
            return 0;
        }
        case 462:
            DestroyWindow(h);
            return 0;
        }
        return 0;
    case WM_DESTROY:
        s_bakWnd = NULL;
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

static void open_baklist(void)
{
    HINSTANCE hi = (HINSTANCE)GetModuleHandleA(NULL);
    HWND lb;
    int i;

    if (!g_mp->mapOk || !g_mp->mapPath[0]) {
        MessageBoxA(g_mp->page, "先打开关卡（需要 MAP 路径才能找备份）", "恢复备份", MB_ICONWARNING);
        return;
    }
    if (s_bakWnd) {
        SetForegroundWindow(s_bakWnd);
        return;
    }
    bak_fill();
    if (s_bakN <= 0) {
        MessageBoxA(g_mp->page, "没找到备份文件（同目录 <文件名>_日期-时间.bak）", "恢复备份", MB_ICONWARNING);
        return;
    }
    {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = bakwnd_proc;
        wc.hInstance = hi;
        wc.lpszClassName = "SHBAKLIST";
        wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassA(&wc);
    }
    s_bakWnd = CreateWindowExA(0, "SHBAKLIST", A("恢复备份 - 选择时间点"),
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, 640, 440,
                               g_mp->page, NULL, hi, NULL);
    if (!s_bakWnd) return;
    lb = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
                         10, 10, 604, 350, s_bakWnd, (HMENU)(size_t)460, hi, NULL);
    SendMessageA(lb, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    for (i = 0; i < s_bakN; i++) {
        char txt[300];
        sprintf(txt, "%s      MAP: %s (%ld B)      ACT: %s (%ld B)",
                s_bak[i].suffix,
                s_bak[i].mapBak[0] ? "有" : "无", s_bak[i].mapSize,
                s_bak[i].actBak[0] ? "有" : "无", s_bak[i].actSize);
        SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)A(txt));
    }
    SendMessageA(lb, LB_SETCURSEL, 0, 0);
    {
        HWND b;
        b = CreateWindowExA(0, "BUTTON", A("恢复"), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            10, 370, 100, 28, s_bakWnd, (HMENU)(size_t)461, hi, NULL);
        SendMessageA(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        b = CreateWindowExA(0, "BUTTON", A("取消"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            120, 370, 100, 28, s_bakWnd, (HMENU)(size_t)462, hi, NULL);
        SendMessageA(b, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
    ShowWindow(s_bakWnd, SW_SHOW);
    SetForegroundWindow(s_bakWnd);
}

/* ================= 动作 ================= */

static int load_map_path(const char *p)
{
    sh_map m;
    if (sh_map_load(p, &m) != 0) return -1;
    if (g_mp->mapOk) sh_map_free(&g_mp->map);
    g_mp->map = m;
    g_mp->mapOk = 1;
    g_mp->mapDirty = 0;
    strncpy(g_mp->mapPath, p, sizeof(g_mp->mapPath) - 1);
    g_mp->mapPath[sizeof(g_mp->mapPath) - 1] = '\0';
    if (g_mp->selx >= g_mp->map.w) g_mp->selx = 0;
    if (g_mp->sely >= g_mp->map.h) g_mp->sely = 0;
    gfx_try_dir(p);              /* 找 Data\GROUND.GRP 等做真实渲染；失败则退化为颜色网格 */
    g_mp->actSel = -1;
    g_mp->needCenter = 1;        /* 真正居中放到绘制时算（那时控件已有真实尺寸） */
    return 0;
}

static int load_act_path(const char *p)
{
    sh_act a;
    if (sh_act_load(p, &a) != 0) return -1;
    if (g_mp->actOk) sh_act_free(&g_mp->act);
    g_mp->act = a;
    g_mp->actOk = 1;
    g_mp->actDirty = 0;
    strncpy(g_mp->actPath, p, sizeof(g_mp->actPath) - 1);
    g_mp->actPath[sizeof(g_mp->actPath) - 1] = '\0';
    return 0;
}

static void sync_sel_fields(void)
{
    if (!g_mp->mapOk) return;
    set_int(g_mp->page, MP_X, g_mp->selx);
    set_int(g_mp->page, MP_Y, g_mp->sely);
    if (g_mp->selx >= 0 && g_mp->selx < g_mp->map.w && g_mp->sely >= 0 && g_mp->sely < g_mp->map.h)
        set_int(g_mp->page, MP_TILE, sh_map_tile(&g_mp->map, g_mp->selx, g_mp->sely));
}

/* 打开按钮：选一张 .MAP，自动载入同目录同名 .ACT（一个按钮开整关） */
static void do_open_stage(void)
{
    char p[512], last[512], ap[512];
    char *dot;

    ini_get("LastMap", last, sizeof(last));
    if (!pick_file(g_mp->page, filter_1("关卡地图 (*.MAP)", "*.MAP"), last, p, sizeof(p))) return;
    if (load_map_path(p) != 0) {
        MessageBoxA(g_mp->page, "不是有效的 MAP（需 9 B 头 + 5 B 记录、长度 9+5n）", "打开失败", MB_ICONERROR);
        return;
    }
    ini_set("LastMap", p);

    /* 同目录同名 .ACT 一起打开；没有则清空布阵面板 */
    strncpy(ap, p, sizeof(ap) - 1);
    ap[sizeof(ap) - 1] = '\0';
    dot = strrchr(ap, '.');
    if (dot) strcpy(dot, ".ACT");
    if (g_mp->actOk) { sh_act_free(&g_mp->act); g_mp->actOk = 0; }
    g_mp->actSel = -1;
    if (dot && load_act_path(ap) == 0) {
        g_mp->actSel = 0;
        ini_set("LastAct", ap);
    }
    sync_sel_fields();
    act_panel_reset();
    refresh();
    grid_repaint();
    undo_clear();                    /* 换了关卡：撤销/重做历史作废 */
    if (g_mp->grid) SetFocus(g_mp->grid);   /* 打开后直接可用 WASD/方向键平移 */
}

/* 新增一条布阵记录（默认「士兵」、队别 2、等级 1，落在当前选中格） */
static void do_act_add(void)
{
    uint8_t rec[SH_ACT_REC];
    int ty = 32;    /* 士兵 */
    int team = 2;
    int x = 0, y = 0;

    if (!g_mp->actOk) {
        MessageBoxA(g_mp->page, "先打开关卡（会同时载入 ACT）", "新增单位", MB_ICONWARNING);
        return;
    }
    if (g_mp->mapOk) {
        x = g_mp->selx;
        y = g_mp->sely;
        if (x < 0 || x >= g_mp->map.w) x = 0;
        if (y < 0 || y >= g_mp->map.h) y = 0;
    }
    memset(rec, 0, sizeof(rec));
    rec[0] = (uint8_t)(ty & 0xFF);
    rec[1] = (uint8_t)(ty >> 8);
    rec[5] = (uint8_t)team;
    rec[6] = (uint8_t)(x & 0xFF);
    rec[7] = (uint8_t)(x >> 8);
    rec[8] = (uint8_t)(y & 0xFF);
    rec[9] = (uint8_t)(y >> 8);
    rec[12] = 1;    /* 等级 1 */
    rec[13] = 0;
    undo_push();
    if (sh_act_insert(&g_mp->act, g_mp->act.count, rec) != 0) {
        MessageBoxA(g_mp->page, "新增失败（记录数已达上限 255 或内存不足）", "新增单位", MB_ICONERROR);
        return;
    }
    g_mp->actDirty = 1;
    g_mp->actSel = g_mp->act.count - 1;
    act_panel_reset();
    scroll_to_cell(x, y);
    refresh();
    grid_repaint();
}

/* 单位剪贴板（复制/粘贴用，只存一条 38 B 记录） */
static uint8_t s_unitClip[SH_ACT_REC];
static int     s_unitClipOk;

/* 复制当前选中的布阵记录 */
static void do_act_copy(void)
{
    char msg[160];
    int ty;

    if (!g_mp->actOk || g_mp->actSel < 0 || g_mp->actSel >= g_mp->act.count) {
        MessageBoxA(g_mp->page, "先在下方列表选中一条布阵记录再复制", "复制单位", MB_ICONWARNING);
        return;
    }
    memcpy(s_unitClip, g_mp->act.data + SH_ACT_HDR + (size_t)g_mp->actSel * SH_ACT_REC, SH_ACT_REC);
    s_unitClipOk = 1;
    ty = sh_act_get(&g_mp->act, g_mp->actSel, SH_AREC_TYPE, 2);
    sprintf(msg, "已复制 #%d（%s）。选中目标格后点「粘贴单位」即可新增。",
            g_mp->actSel, sh_char_name(ty));
    MessageBoxA(g_mp->page, msg, "复制单位", MB_ICONINFORMATION);
}

/* 把剪贴板里的单位粘贴成一条新记录（坐标取当前选中格） */
static void do_act_paste(void)
{
    uint8_t rec[SH_ACT_REC];
    int x, y;

    if (!s_unitClipOk) {
        MessageBoxA(g_mp->page, "没有已复制的单位数据，请先选中一条记录点「复制单位」", "粘贴单位", MB_ICONWARNING);
        return;
    }
    if (!g_mp->actOk) {
        MessageBoxA(g_mp->page, "先打开关卡（会同时载入 ACT）", "粘贴单位", MB_ICONWARNING);
        return;
    }
    memcpy(rec, s_unitClip, SH_ACT_REC);
    if (g_mp->mapOk && g_mp->selx >= 0 && g_mp->selx < g_mp->map.w &&
        g_mp->sely >= 0 && g_mp->sely < g_mp->map.h) {
        x = g_mp->selx;
        y = g_mp->sely;
        rec[SH_AREC_X]     = (uint8_t)(x & 0xFF);
        rec[SH_AREC_X + 1] = (uint8_t)(x >> 8);
        rec[SH_AREC_Y]     = (uint8_t)(y & 0xFF);
        rec[SH_AREC_Y + 1] = (uint8_t)(y >> 8);
    }
    undo_push();
    if (sh_act_insert(&g_mp->act, g_mp->act.count, rec) != 0) {
        MessageBoxA(g_mp->page, "粘贴失败（记录数已达上限 255 或内存不足）", "粘贴单位", MB_ICONERROR);
        return;
    }
    g_mp->actDirty = 1;
    g_mp->actSel = g_mp->act.count - 1;
    act_panel_reset();
    scroll_to_cell(g_mp->selx, g_mp->sely);
    refresh();
    grid_repaint();
}

/* 删除当前选中的布阵记录（内存内，点「写入文件」才落盘） */
static void do_act_delete(void)
{
    char msg[256];
    int ty;

    if (!g_mp->actOk || g_mp->actSel < 0 || g_mp->actSel >= g_mp->act.count) {
        MessageBoxA(g_mp->page, "先打开关卡并在列表里选中一条记录", "删除记录", MB_ICONWARNING);
        return;
    }
    ty = sh_act_get(&g_mp->act, g_mp->actSel, SH_AREC_TYPE, 2);
    sprintf(msg, "确定删除布阵记录 #%d（%s）？\r\n删除只在内存里生效，点「写入文件」才会保存到磁盘。",
            g_mp->actSel, sh_char_name(ty));
    if (MessageBoxA(g_mp->page, msg, "删除布阵记录", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;
    undo_push();
    if (sh_act_delete(&g_mp->act, g_mp->actSel) != 0) {
        MessageBoxA(g_mp->page, "删除失败（ACT 至少要保留 1 条记录）", "删除记录", MB_ICONERROR);
        return;
    }
    g_mp->actDirty = 1;
    if (g_mp->actSel >= g_mp->act.count) g_mp->actSel = g_mp->act.count - 1;
    act_panel_reset();
    refresh();
    grid_repaint();
}


static void do_apply_tile(void)
{
    int v, old;
    if (!g_mp->mapOk) { MessageBoxA(g_mp->page, "先打开 MAP", "应用到地图", MB_ICONWARNING); return; }
    v = get_int(g_mp->page, MP_TILE);
    old = sh_map_tile(&g_mp->map, g_mp->selx, g_mp->sely);
    if (v == old) return;                 /* 没变化就不进撤销栈 */
    undo_push();
    if (sh_map_set_tile(&g_mp->map, g_mp->selx, g_mp->sely, v) != 0) {
        MessageBoxA(g_mp->page, "tileId 必须在 0..65535", "应用到地图", MB_ICONWARNING);
        return;
    }
    g_mp->mapDirty = 1;
    refresh();
    grid_repaint();
}

static void do_write(void)
{
    char bak[600];
    int ok = 1, wrote = 0;

    /* 先把面板里正在编辑的那条写回内存（否则改了字段直接点写入会丢） */
    if (g_mp->actOk) act_commit(g_mp->page, g_mp->actSel);

    if (g_mp->mapOk && g_mp->mapDirty) {
        if (sh_map_backup(&g_mp->map, bak, sizeof(bak)) != 0) {
            MessageBoxA(g_mp->page, "MAP 备份失败，未写入", "写入", MB_ICONERROR);
            return;
        }
        if (sh_map_write(&g_mp->map) != 0) ok = 0; else { g_mp->mapDirty = 0; wrote = 1; }
    }
    if (g_mp->actOk && g_mp->actDirty) {
        if (sh_act_backup(&g_mp->act, bak, sizeof(bak)) != 0) {
            MessageBoxA(g_mp->page, "ACT 备份失败，未写入", "写入", MB_ICONERROR);
            return;
        }
        if (sh_act_write(&g_mp->act) != 0) ok = 0; else { g_mp->actDirty = 0; wrote = 1; }
    }
    if (!wrote && ok) { MessageBoxA(g_mp->page, "没有需要写入的修改", "写入", MB_ICONINFORMATION); return; }
    MessageBoxA(g_mp->page, ok ? "写入成功（已备份，写后重读校验通过）" : "写入或校验失败！请用同目录 .bak 恢复",
                "写入", ok ? MB_ICONINFORMATION : MB_ICONERROR);
    refresh();
}

static void do_reload(void)
{
    if (g_mp->mapOk) { sh_map_free(&g_mp->map); g_mp->mapOk = 0; g_mp->mapDirty = 0; }
    if (g_mp->actOk) { sh_act_free(&g_mp->act); g_mp->actOk = 0; g_mp->actDirty = 0; }
    undo_clear();
    refresh();
    grid_repaint();
}

/* 网格左键：选中格；该格有单位则自动选中列表里对应记录，没有则清空列表选中 */
static void on_grid_click(int x, int y, int dbl)
{
    int i, first = -1, cur = -1, next = -1, hit;

    g_mp->selx = x;
    g_mp->sely = y;
    sync_sel_fields();          /* 顶部 X/Y/tileId 同步 */

    if (!g_mp->actOk || g_mp->act.count <= 0) {
        refresh();
        grid_repaint();
        return;
    }
    for (i = 0; i < g_mp->act.count; i++) {
        if (sh_act_get(&g_mp->act, i, SH_AREC_X, 2) != x ||
            sh_act_get(&g_mp->act, i, SH_AREC_Y, 2) != y) continue;
        if (first < 0) first = i;
        if (i == g_mp->actSel) cur = i;
        if (cur >= 0 && next < 0 && i > cur) next = i;
    }
    hit = (cur >= 0) ? (next >= 0 ? next : first) : first;
    if (hit >= 0) {
        if (hit != g_mp->actSel) act_commit(g_mp->page, g_mp->actSel);
        g_mp->actSel = hit;
        SendMessageA(GetDlgItem(g_mp->page, MA_LIST), LB_SETCURSEL, (WPARAM)hit, 0);
        act_show_sel(g_mp->page);
        if (dbl) SetFocus(GetDlgItem(g_mp->page, MA_LIST));
    } else {
        act_commit(g_mp->page, g_mp->actSel);   /* 先写回上一条再清空选中 */
        act_clear_sel();
    }
    refresh();
    grid_repaint();
}

/* 网格右键：弹出菜单，把当前选中的布阵单位移动到该格 */
static void on_grid_rclick(int x, int y)
{
    HMENU menu;
    POINT pt;
    char item[160];
    int cmd, have;

    if (!g_mp || !g_mp->mapOk) return;
    GetCursorPos(&pt);
    menu = CreatePopupMenu();
    if (!menu) return;
    have = (g_mp->actOk && g_mp->actSel >= 0 && g_mp->actSel < g_mp->act.count);
    if (have) {
        int ty = sh_act_get(&g_mp->act, g_mp->actSel, SH_AREC_TYPE, 2);
        sprintf(item, "把 #%d %s 移到 (%d,%d)", g_mp->actSel, sh_char_name(ty), x, y);
        AppendMenuA(menu, MF_STRING, 1, A(item));
    } else {
        AppendMenuA(menu, MF_STRING | MF_GRAYED, 1, A("（未选中布阵记录，先点列表选一条）"));
    }
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, 2, A("取消"));
    cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, g_mp->page, NULL);
    DestroyMenu(menu);
    if (cmd == 1 && have) {
        undo_push();
        act_commit(g_mp->page, g_mp->actSel);
        sh_act_set(&g_mp->act, g_mp->actSel, SH_AREC_X, 2, x);
        sh_act_set(&g_mp->act, g_mp->actSel, SH_AREC_Y, 2, y);
        g_mp->actDirty = 1;
        g_mp->selx = x;
        g_mp->sely = y;
        sync_sel_fields();
        act_refresh_list(g_mp->page, g_mp->actSel);
        act_show_sel(g_mp->page);
        scroll_to_cell(x, y);
        refresh();
        grid_repaint();
    }
}

/* ================= 页面窗口过程 ================= */

static LRESULT CALLBACK mp_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_COMMAND) {
        /* 布阵面板：离开输入框 / 下拉改选后自动写回（不需要「应用」按钮） */
        if (LOWORD(wp) == MA_TYPE &&
            (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == CBN_KILLFOCUS)) {
            act_apply_live();
            return 0;
        }
        if ((LOWORD(wp) == MA_TEAM || LOWORD(wp) == MA_ACT) &&
            (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == CBN_KILLFOCUS)) {
            act_apply_live();
            return 0;
        }
        if (LOWORD(wp) >= MA_E0 + 1 && LOWORD(wp) <= MA_E0 + 5 && HIWORD(wp) == EN_KILLFOCUS) {
            act_apply_live();
            return 0;
        }
        switch (LOWORD(wp)) {
        case MP_OPEN_MAP: do_open_stage(); return 0;
        case MP_WRITE:    do_write();     return 0;
        case MP_RELOAD:   do_reload();    return 0;
        case MP_UNDO:     do_undo();      return 0;
        case MP_REDO:     do_redo();      return 0;
        case MP_RESTORE:  open_baklist(); return 0;
        case MP_TILEBROWSE: open_tilepick(); return 0;        case MA_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) act_panel_selchanged();
            return 0;
        case MA_APPLY:
            act_apply_live();
            return 0;
        case MA_DEL:
            do_act_delete();
            return 0;
        case MA_ADD:
            do_act_add();
            return 0;
        case MA_COPY:
            do_act_copy();
            return 0;
        case MA_PASTE:
            do_act_paste();
            return 0;
        }
        return 0;
    }
    if (m == WM_APP + 1) {
        on_grid_click((int)LOWORD(lp), (int)HIWORD(lp), (int)wp);
        return 0;
    }
    if (m == WM_APP + 2) {
        on_grid_rclick((int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    }
    if (m == WM_TIMER && wp == 1) {
        /* 轮询光标：悬停「布阵编辑(ACT)」时显示说明（不依赖 comctl32） */
        POINT pt;
        RECT br;
        HWND btn = GetDlgItem(h, MP_OPEN_MAP);
        HWND ed = GetDlgItem(h, MP_TILE);
        if (!IsWindowVisible(h) || !btn) { tip_hide(); return 0; }
        GetCursorPos(&pt);
        GetWindowRect(btn, &br);
        if (PtInRect(&br, pt)) { tip_show(tip_act_text(), pt.x + 14, pt.y + 18); return 0; }
        if (ed) {
            GetWindowRect(ed, &br);
            if (PtInRect(&br, pt)) { tip_show(tip_tile_text(), pt.x + 14, pt.y + 18); return 0; }
        }
        tip_hide();
        return 0;
    }
    if (m == WM_DESTROY) {
        KillTimer(h, 1);
        if (s_tip) { DestroyWindow(s_tip); s_tip = NULL; }
        sh_grp_free(&g_mp->grnd);
        sh_grp_free(&g_mp->upr);
        sh_man_close(&g_mp->man);
        if (s_vbuf) { free(s_vbuf); s_vbuf = NULL; s_vbufSize = 0; }
        undo_clear();
        if (s_tpWnd) { DestroyWindow(s_tpWnd); s_tpWnd = NULL; }
        if (s_tpBuf) { free(s_tpBuf); s_tpBuf = NULL; }
        if (s_bakWnd) { DestroyWindow(s_bakWnd); s_bakWnd = NULL; }
        return 0;
    }
    if (m == WM_CTLCOLORSTATIC) {
        SetBkColor((HDC)wp, GetSysColor(COLOR_BTNFACE));
        return (LRESULT)(COLOR_BTNFACE + 1);
    }
    return DefWindowProcA(h, m, wp, lp);
}

static HWND mk(const char *cls, const char *txt, DWORD st, int x, int y, int cx, int cy, int id)
{
    return mk_child(g_mp->page, cls, txt, st, x, y, cx, cy, id);
}

HWND sh_mapedit_create(HWND parent, HINSTANCE hi, const RECT *rc)
{
    WNDCLASSA wc;

    if (g_mp) return g_mp->page;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = mp_proc;
    wc.hInstance     = hi;
    wc.lpszClassName = "SHMEPAGE";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return NULL;

    g_mp = (MP *)calloc(1, sizeof(MP));
    if (!g_mp) return NULL;
    g_mp->parent = parent;
    g_mp->hi = hi;
    g_mp->page = CreateWindowExA(0, "SHMEPAGE", "", WS_CHILD | WS_CLIPCHILDREN,
                                 0, 0, rc->right, rc->bottom, parent, NULL, hi, NULL);
    if (!g_mp->page) { free(g_mp); g_mp = NULL; return NULL; }

    mk("BUTTON", "打开关卡(MAP+ACT)", BS_PUSHBUTTON | WS_TABSTOP, 12, 8, 176, 30, MP_OPEN_MAP);
    mk("BUTTON", "写入文件",          BS_PUSHBUTTON | WS_TABSTOP, 196, 8, 112, 30, MP_WRITE);
    mk("BUTTON", "恢复备份",          BS_PUSHBUTTON | WS_TABSTOP, 316, 8, 108, 30, MP_RESTORE);
    mk("BUTTON", "全部关闭",          BS_PUSHBUTTON | WS_TABSTOP, 432, 8, 112, 30, MP_RELOAD);
    mk("BUTTON", "撤销",              BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, 552, 8, 80, 30, MP_UNDO);
    mk("BUTTON", "重做",              BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, 640, 8, 80, 30, MP_REDO);

    mk("STATIC", "", SS_LEFT, 12, 42, 928, 66, MP_INFO);

    mk("STATIC", "格 X:", SS_LEFT, 12, 116, 44, 22, 0);
    mk("EDIT", "0", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 60, 112, 64, 26, MP_X);
    mk("STATIC", "Y:", SS_LEFT, 132, 116, 20, 22, 0);
    mk("EDIT", "0", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 154, 112, 64, 26, MP_Y);
    mk("STATIC", "tileId:", SS_LEFT, 226, 116, 50, 22, 0);
    mk("EDIT", "0", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, 280, 112, 70, 28, MP_TILE);
    mk("BUTTON", "图块浏览", BS_PUSHBUTTON | WS_TABSTOP, 358, 110, 110, 30, MP_TILEBROWSE);
    mk("STATIC", "布阵记录（点选 → 下方改字段）:", SS_LEFT, 590, 116, 358, 22, 0);

    /* 地图：放在上面、占满整宽 */
    {
        WNDCLASSA gw;
        memset(&gw, 0, sizeof(gw));
        gw.lpfnWndProc   = grid_proc;
        gw.hInstance     = hi;
        gw.lpszClassName = "SHMAPGRID";
        gw.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        gw.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_CROSS);
        gw.style         = CS_DBLCLKS;
        RegisterClassA(&gw);
        g_mp->grid = CreateWindowExA(WS_EX_CLIENTEDGE, "SHMAPGRID", "",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL, 12, 146, 930, 264,
                                     g_mp->page, (HMENU)(size_t)MP_GRID, hi, NULL);
    }

    /* 布阵面板：放在地图下面 */
    CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
                    12, 416, 300, 76, g_mp->page, (HMENU)(size_t)MA_LIST, hi, NULL);
    mk("STATIC", "类型:", SS_LEFT, 320, 420, 40, 22, 0);
    CreateWindowExA(0, "COMBOBOX", "",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                    362, 416, 150, 220, g_mp->page, (HMENU)(size_t)MA_TYPE, hi, NULL);
    mk("STATIC", "队别:", SS_LEFT, 520, 420, 40, 22, 0);
    CreateWindowExA(0, "COMBOBOX", "",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                    562, 416, 110, 130, g_mp->page, (HMENU)(size_t)MA_TEAM, hi, NULL);
    mk("STATIC", "X:", SS_LEFT, 680, 420, 20, 22, 0);
    mk("EDIT", "0", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, 702, 416, 40, 24, MA_E0 + 2);
    mk("STATIC", "Y:", SS_LEFT, 750, 420, 20, 22, 0);
    mk("EDIT", "0", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, 772, 416, 40, 24, MA_E0 + 3);
    mk("STATIC", "等级:", SS_LEFT, 820, 420, 40, 22, 0);
    mk("EDIT", "0", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, 862, 416, 40, 24, MA_E0 + 4);
    mk("STATIC", "动作:", SS_LEFT, 320, 452, 40, 22, 0);
    CreateWindowExA(0, "COMBOBOX", "",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                    362, 448, 150, 220, g_mp->page, (HMENU)(size_t)MA_ACT, hi, NULL);
    mk("BUTTON", "新增单位", BS_PUSHBUTTON | WS_TABSTOP, 520, 446, 86, 26, MA_ADD);
    mk("BUTTON", "删除单位", BS_PUSHBUTTON | WS_TABSTOP, 614, 446, 86, 26, MA_DEL);
    mk("BUTTON", "复制单位", BS_PUSHBUTTON | WS_TABSTOP, 708, 446, 86, 26, MA_COPY);
    mk("BUTTON", "粘贴单位", BS_PUSHBUTTON | WS_TABSTOP, 802, 446, 86, 26, MA_PASTE);

    /* tileId 输入框回车即应用（子类化） */
    s_tileEditProc = (WNDPROC)SetWindowLongA(GetDlgItem(g_mp->page, MP_TILE), GWL_WNDPROC,
                                             (LONG)(LONG_PTR)tileedit_proc);

    /* 启动时不做任何预读：用户点按钮时才读 ini 里的上次路径并弹框 */
    act_combo_fill();
    act_panel_fill_combos();
    SetTimer(g_mp->page, 1, 200, NULL);   /* tooltip 轮询 */
    refresh();
    return g_mp->page;
}

void sh_mapedit_show(HWND page, int show)
{
    if (!show) {
        tip_hide();
        /* 离开页面时把面板里正在编辑的那条写回内存，避免切页丢改动 */
        if (g_mp && g_mp->actOk && g_mp->page) act_commit(g_mp->page, g_mp->actSel);
    }
    if (page) ShowWindow(page, show ? SW_SHOW : SW_HIDE);
}

int sh_mapedit_alive(void)
{
    return g_mp != NULL && g_mp->page != NULL;
}

/* ================= 自检 ================= */

int sh_mapedit_selftest(HWND parent)
{
    char data[512], src1[600], src2[600], tmp[MAX_PATH], t1[MAX_PATH], t2[MAX_PATH];
    FILE *f;
    sh_map m;
    sh_act a;
    int fail = 0, t0, w0;
    RECT rc;

    data[0] = '\0';
    if (GetEnvironmentVariableA("SHUIHU_DATA", data, sizeof(data)) == 0) data[0] = '\0';
    if (!data[0]) return 0;

    sprintf(src1, "%s\\Map\\N01.MAP", data);
    sprintf(src2, "%s\\Map\\N01.ACT", data);
    f = fopen(src1, "rb");
    if (!f) return 0;
    fclose(f);

    GetTempPathA(MAX_PATH, tmp);
    sprintf(t1, "%ssh_map_selftest.MAP", tmp);
    sprintf(t2, "%ssh_act_selftest.ACT", tmp);
    CopyFileA(src1, t1, FALSE);
    CopyFileA(src2, t2, FALSE);

    if (sh_map_load(t1, &m) != 0) { fail |= 1; }
    else {
        t0 = sh_map_tile(&m, 0, 0);
        w0 = (t0 == 60) ? 61 : 60;
        if (sh_map_set_tile(&m, 0, 0, w0) != 0) fail |= 2;
        if (sh_map_write(&m) != 0) fail |= 4;
        sh_map_free(&m);
        if (sh_map_load(t1, &m) != 0) fail |= 8;
        else {
            if (sh_map_tile(&m, 0, 0) != w0) fail |= 16;
            if (m.cells <= 0 || m.layers < 0) fail |= 32;
            sh_map_free(&m);
        }
    }

    if (sh_act_load(t2, &a) != 0) { fail |= 64; }
    else {
        int old = sh_act_get(&a, 0, SH_AREC_LEVEL, 2);
        int nv = (old == 42) ? 41 : 42;
        if (sh_act_set(&a, 0, SH_AREC_LEVEL, 2, nv) != 0) fail |= 128;
        if (sh_act_write(&a) != 0) fail |= 256;
        sh_act_free(&a);
        if (sh_act_load(t2, &a) != 0) fail |= 512;
        else {
            if (sh_act_get(&a, 0, SH_AREC_LEVEL, 2) != nv) fail |= 1024;
            sh_act_free(&a);
        }
    }

    rc.left = 0; rc.top = 0; rc.right = 954; rc.bottom = 378;
    if (!sh_mapedit_create(parent, GetModuleHandleA(NULL), &rc)) fail |= 2048;
    else {
        if (!GetDlgItem(g_mp->page, MP_OPEN_MAP)) fail |= 4096;
        if (!g_mp->grid) fail |= 8192;
        if (!GetDlgItem(g_mp->page, MA_LIST)) fail |= 16384;
    }

    DeleteFileA(t1);
    DeleteFileA(t2);
    return fail;
}
