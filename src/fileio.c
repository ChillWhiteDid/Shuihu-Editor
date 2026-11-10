/*
 * fileio.c —— 备份与安全写入（M2）
 *
 * 设计约束（Win98 实机）：
 *   - 只用 kernel32 里的基础文件 API，不用 MoveFileEx/GetLongPathName 等 NT 专用函数
 *   - 路径为 ANSI（GBK），长度 ≤ MAX_PATH
 *   - 备份失败则中止写入（由调用方判断），绝不无备份落盘
 */
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define SH_MKDIR(p)   _mkdir(p)
#define SH_SEP        '\\'
#else
#include <sys/stat.h>
#include <sys/types.h>
#define SH_MKDIR(p)   mkdir(p, 0755)
#define SH_SEP        '/'
#endif

/* 取路径中的目录部分（不含结尾分隔符）；没有目录则返回 "." */
static void split_dir(const char *path, char *dir, size_t n)
{
    const char *p, *last = NULL;
    size_t len;

    for (p = path; *p; p++) {
        if (*p == '\\' || *p == '/') last = p;
    }
    if (!last) {
        strncpy(dir, ".", n - 1);
        dir[n - 1] = '\0';
        return;
    }
    len = (size_t)(last - path);
    if (len == 0) len = 1;                 /* 根目录 "C:\" */
    if (len >= n) len = n - 1;
    memcpy(dir, path, len);
    dir[len] = '\0';
}

static const char *base_name(const char *path)
{
    const char *p, *last = path;
    for (p = path; *p; p++) {
        if (*p == '\\' || *p == '/') last = p + 1;
    }
    return last;
}

int sh_dir_exists(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return (a != 0xFFFFFFFFu) && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
#endif
}

static int file_size_of(const char *path, long *out)
{
    FILE *f = fopen(path, "rb");
    long sz;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    sz = ftell(f);
    fclose(f);
    if (sz < 0) return -1;
    *out = sz;
    return 0;
}

int sh_make_backup(const char *save_path, char *out, size_t outsz)
{
    char dir[512], bdir[560], base[260], name[600];
    const char *bn;
    time_t now;
    struct tm *tmv;
    char stamp[32];
    int seq;
    long src_sz, bak_sz;

    if (!save_path) return -1;
    if (file_size_of(save_path, &src_sz) != 0) return -1;

    split_dir(save_path, dir, sizeof(dir));
    sprintf(bdir, "%s%cbackup_save", dir, SH_SEP);

    if (!sh_dir_exists(bdir)) {
        if (SH_MKDIR(bdir) != 0 && !sh_dir_exists(bdir)) return -2;
    }

    bn = base_name(save_path);
    strncpy(base, bn, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';

    now = time(NULL);
    tmv = localtime(&now);
    if (tmv) {
        sprintf(stamp, "%04d%02d%02d-%02d%02d%02d",
                tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    } else {
        strcpy(stamp, "00000000-000000");
    }

    for (seq = 0; seq < 100; seq++) {
        FILE *probe;
        if (seq == 0) {
            sprintf(name, "%s%c%s_%s.sav", bdir, SH_SEP, base, stamp);
        } else {
            sprintf(name, "%s%c%s_%s_%d.sav", bdir, SH_SEP, base, stamp, seq + 1);
        }
        probe = fopen(name, "rb");
        if (probe) { fclose(probe); continue; }   /* 已存在，换下一个序号 */
        break;
    }

    /* 复制（目标不存在，故不覆盖任何东西） */
#ifdef _WIN32
    if (!CopyFileA(save_path, name, TRUE)) return -3;
#else
    {
        FILE *in = fopen(save_path, "rb"), *ot = fopen(name, "wb");
        char buf[8192];
        size_t got;
        if (!in || !ot) { if (in) fclose(in); if (ot) fclose(ot); return -3; }
        while ((got = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, got, ot);
        fclose(in); fclose(ot);
    }
#endif

    if (file_size_of(name, &bak_sz) != 0 || bak_sz != src_sz) {
        remove(name);
        return -4;
    }

    if (out && outsz) {
        strncpy(out, name, outsz - 1);
        out[outsz - 1] = '\0';
    }
    return 0;
}

int sh_write_file(const char *path, const uint8_t *data, size_t size)
{
    char tmp[600];
    FILE *f;
    long sz;

    if (!path || !data) return -1;
    sprintf(tmp, "%s.tmp", path);

    f = fopen(tmp, "wb");
    if (!f) return -2;
    if (fwrite(data, 1, size, f) != size) { fclose(f); remove(tmp); return -3; }
    if (fclose(f) != 0) { remove(tmp); return -3; }

    if (file_size_of(tmp, &sz) != 0 || (size_t)sz != size) { remove(tmp); return -4; }

#ifdef _WIN32
    if (!CopyFileA(tmp, path, FALSE)) { remove(tmp); return -5; }
    DeleteFileA(tmp);
#else
    if (rename(tmp, path) != 0) { remove(tmp); return -5; }
#endif
    return 0;
}
