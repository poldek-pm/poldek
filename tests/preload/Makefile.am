# preloaded by sh/01-create-repo, sh/08-rpm, sh/13-cli, sh/16-hooks,
# sh/17-interrupt and sh/19-questions
check_LTLIBRARIES = sigint_at_init.la signal_at_fork.la signal_at_restore.la \
		    sigint_at_dbload.la library_user.la front_end.la close_error.la \
		    rcmd_user.la
sigint_at_init_la_SOURCES = sigint_at_init.c
sigint_at_init_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
signal_at_fork_la_SOURCES = signal_at_fork.c
signal_at_fork_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
signal_at_restore_la_SOURCES = signal_at_restore.c
signal_at_restore_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
sigint_at_dbload_la_SOURCES = sigint_at_dbload.c
sigint_at_dbload_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
library_user_la_SOURCES = library_user.c
library_user_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
front_end_la_SOURCES = front_end.c
front_end_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
close_error_la_SOURCES = close_error.c
close_error_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
rcmd_user_la_SOURCES = rcmd_user.c
rcmd_user_la_LDFLAGS = -module -avoid-version -shared -rpath /nowhere
# libc is all they use: the libraries of LIBS would only tie them to the
# SONAMEs they were linked with, and nothing relinks them when those change;
# a target-specific variable of GNU make, which other makes ignore
$(check_LTLIBRARIES): LIBS = $(DL_LIBS)
