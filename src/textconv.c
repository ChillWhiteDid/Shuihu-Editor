/*
 * textconv.c —— UTF-8 -> 系统 ANSI
 */
#include "textconv.h"

#include <windows.h>
#include <string.h>

#define SH_WBUF_CHARS 8192

void sh_utf8_to_acp(const char *u8, char *out, int cap)
{
    static WCHAR wbuf[SH_WBUF_CHARS];
    int n;

    if (!out || cap <= 0) return;
    if (!u8) u8 = "";
    out[0] = (char)0;

    n = MultiByteToWideChar(CP_UTF8, 0, u8, -1, wbuf, SH_WBUF_CHARS);
    if (n > 0) {
        if (WideCharToMultiByte(CP_ACP, 0, wbuf, -1, out, cap, NULL, NULL) > 0)
            return;
    }

    /* 转换失败（极少见）：至少保留原文，保证不崩 */
    strncpy(out, u8, (size_t)(cap - 1));
    out[cap - 1] = (char)0;
}
