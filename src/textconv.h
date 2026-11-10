/*
 * textconv.h —— 编码转换（UTF-8 -> 系统 ANSI）
 *
 * 程序内部统一用 UTF-8 字符串字面量；Win32 ANSI 接口需要系统代码页（简体中文 = GBK）。
 *
 * 重要：必须整段一次转换。分块或截断会切在 GBK 双字节中间，尾部显示为乱码。
 */
#ifndef SH_TEXTCONV_H
#define SH_TEXTCONV_H

/* 把 u8 转成系统 ANSI 写入 out（容量 cap，含结尾 0）。失败时退回逐字节拷贝。 */
void sh_utf8_to_acp(const char *u8, char *out, int cap);

#endif /* SH_TEXTCONV_H */
