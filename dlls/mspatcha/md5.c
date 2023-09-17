
/* MD5 algorithm
 *
 * This code is derived from ntdll/crypt.c
 */

#include <string.h>
#include <basetsd.h> /* for WORDS_BIGENDIAN */
#include <windef.h>

#include "md5.h"

struct md5_ctx {
    unsigned int i[2];
    unsigned int buf[4];
    unsigned char in[64];
    unsigned char digest[MD5DIGESTLEN];
};

extern void WINAPI MD5Init( struct md5_ctx * );
extern void WINAPI MD5Update( struct md5_ctx *, const unsigned char *, unsigned int );
extern void WINAPI MD5Final( struct md5_ctx * );

void ComputeMD5Hash(const void *data, unsigned int len, unsigned char digest[MD5DIGESTLEN])
{
    struct md5_ctx ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, data, len);
    MD5Final(&ctx);
    memcpy(digest, ctx.digest, MD5DIGESTLEN);
}
