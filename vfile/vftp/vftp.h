/* $Id$ */
#ifndef POLDEK_VFTP_H
#define POLDEK_VFTP_H

extern int *vftp_verbose;
extern int vftp_errno;

extern void (*vftp_msg_fn)(const char *fmt, ...);
extern void (*vftp_err_fn)(const char *fmt, ...);


int vftp_init(int *verbose,
              void (*progress_fn)(long total, long amount, void *data));

void vftp_destroy(void);
void vftp_vacuum(void);

int vftp_retr(FILE *stream, long offset, const char *url, void *progess_data);

const char *vftp_errmsg(void);

#endif
