/* $Id$ */
#ifndef POLDEK_VHTTP_H
#define POLDEK_VHTTP_H

extern int *vhttp_verbose;
extern int vhttp_errno;
extern const char *vhttp_anonpasswd;

extern void (*vhttp_msg_fn)(const char *fmt, ...);
extern void (*vhttp_err_fn)(const char *fmt, ...);


int vhttp_init(int *verbose,
              void (*progress_fn)(long total, long amount, void *data));

void vhttp_destroy(void);
void vhttp_vacuum(void);

int vhttp_retr(FILE *stream, long offset, const char *url, void *progess_data);

const char *vhttp_errmsg(void);

#endif
