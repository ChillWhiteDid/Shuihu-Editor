/*
 * fileio.h —— 备份与安全写入（M2）
 *
 * 只用 Win9x 已有的 API：CreateDirectoryA / CopyFileA / DeleteFileA /
 * MoveFileA / GetFileAttributesA；不使用 MoveFileExA（NT 专用）等。
 */
#ifndef SH_FILEIO_H
#define SH_FILEIO_H

#include <stddef.h>
#include <stdint.h>

/* 在存档同目录下创建 backup_save\<原名>_YYYYMMDD-HHMMSS.sav 备份。
 * out 返回备份文件的完整路径（可为 NULL）。
 * 成功 0；失败负值（-1 源不可读 -2 无法建目录 -3 复制失败 -4 校验失败）。 */
int sh_make_backup(const char *save_path, char *out, size_t outsz);

/* 安全写入：先写 <path>.tmp，校验长度，再覆盖目标。
 * 目标已存在时会被覆盖（调用方应已备份）。成功 0。 */
int sh_write_file(const char *path, const uint8_t *data, size_t size);

/* 目录是否存在（1/0） */
int sh_dir_exists(const char *path);

#endif /* SH_FILEIO_H */
