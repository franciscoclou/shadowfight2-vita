/* The game's whole-file MD5 (0x4a1fa6fc), answered by file.c's cached
 * digest.
 *
 * 0x4a1fa6fc(&out, &path) opens path "rb" through the game's libc (so
 * s3eFileOpen), feeds it to MD5 1 KB at a time and returns the digest as
 * 32 lowercase hex digits ("%02x"), or "" when the file does not open.
 * Its callers check archives and downloads against stored digests; at
 * boot 0x4a69e7a8 checks rom://assets/animations.dz and files.dz is
 * checked twice, 18.5 MB of reads for an answer that only changes when
 * the game data is replaced. sf_file_md5 gives the same answer, cached
 * for unchanged game files.
 */
#include "s3e/game.h"

static gstring *file_md5(gstring *out, const gstring *path)
{
    char hex[33];
    if (!path->begin || sf_file_md5(path->begin, hex) != 0)
        hex[0] = 0;
    sf_gstring(out, hex);
    return out;
}

void sf_hashes_install(void)
{
    sf_game_hook("file md5", 0x4a1fa6fc, 0xe92d4ff0, 0xe24ddb01, (void *)file_md5);
}
