

TESTS = test_poldek
noinst_PROGRAMS = test_poldek
test_poldek_SOURCES = test_poldek.c test_match.c test_env.c \
	   	              test_pmdb.c test_op.c

test_poldek_INCLUDES= @CHECK_CFLAGS@
test_poldek_LDADD = @CHECK_LIBS@ ../libpoldek_allsym.la ../vfile/libvfile.la  
