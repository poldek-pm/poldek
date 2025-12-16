
/* function taken from snarf <http://www.xach.com/snarf/> sources  */
/* $Id$                                                            */


/* This program and all accompanying files are copyright 1998 Zachary
 * Beane <xach@xach.com>. They come with NO WARRANTY; see the file COPYING
 * for details.
 *
 * This program is licensed to you under the terms of the GNU General
 * Public License. You should have recieved a copy of it with this
 * program; if you did not, you can view the terms at http://www.gnu.org/.
 *
 */

/* written by lauri alanko */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <trurl/nassert.h>

int vhttp_misc_base64(char *b64, int size, const char *bin)
{
	int i =0, j = 0, len;
    char BASE64_END = '=';
    char base64_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    len = strlen(bin);

    n_assert(size > 5 * len);

    while (j < len - 2) {
        b64[i++] = base64_table[bin[j]>>2];
        b64[i++] = base64_table[((bin[j]&3)<<4)|(bin[j+1]>>4)];
        b64[i++] = base64_table[((bin[j+1]&15)<<2)|(bin[j+2]>>6)];
        b64[i++] = base64_table[bin[j+2]&63];
        j += 3;
    }

    switch (len - j) {
    case 1:
        b64[i++] = base64_table[bin[j]>>2];
        b64[i++] = base64_table[(bin[j]&3)<<4];
        b64[i++] = BASE64_END;
        b64[i++] = BASE64_END;
        break;
    case 2:
        b64[i++] = base64_table[ bin[j] >> 2 ];
        b64[i++] = base64_table[ ((bin[j] & 3) << 4) | (bin[j + 1] >> 4)];
        b64[i++] = base64_table[ (bin[j + 1] & 15) << 2];
        b64[i++] = BASE64_END;
        break;
    case 0:
        break;
    }

    b64[i] = '\0';
    return i;
}
