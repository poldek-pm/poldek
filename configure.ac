dnl Process this file with autoconf to produce a configure script.

m4_define([poldek_major_version], [0])
m4_define([poldek_minor_version], [44])
m4_define([poldek_micro_version], [0])
m4_define([poldek_version],
	  [poldek_major_version.poldek_minor_version.poldek_micro_version])

AC_PREREQ([2.63])
AC_INIT([poldek], [poldek_version], [https://github.com/poldek-pm/poldek/issues/new])
AC_CONFIG_SRCDIR([capreq.c])
AC_CONFIG_HEADERS([config.h])

#VERSION_STATUS="beta"
VERSION_YEAR=$(date +%Y)

AH_TEMPLATE([ENABLE_TRACE],[])
AH_TEMPLATE([HAVE_RPM_4_0_4],[])
AH_TEMPLATE([HAVE_RPM_4_1],[])
AH_TEMPLATE([HAVE_RPM_4_2],[])
AH_TEMPLATE([HAVE_RPM_EXTDEPS],[])
AH_TEMPLATE([HAVE_RPM_HGETCOLOR],[])
AH_TEMPLATE([HAVE_TM_GMTOFF],[])
AH_TEMPLATE([HAVE_TM___GMTOFF],[])
AH_TEMPLATE([HAVE_RPM_HEADER_MAGIC_YES],[])
AH_TEMPLATE([HAVE_RPM_INT32_TYPE],[])
AH_TEMPLATE([HAVE_RPM_5],[])
AH_TEMPLATE([HAVE_RPMORG],[])

AM_INIT_AUTOMAKE([1.11 foreign no-dist-gzip dist-xz])

if test -n "${VERSION_STATUS}"; then
   AC_DEFINE_UNQUOTED([VERSION_STATUS], "$VERSION_STATUS", [version status])
fi
AC_DEFINE_UNQUOTED([PACKAGE_VERSION_FUNCNAME],
                   ${PACKAGE}_version_${VERSION_TAG},  [ version function name ])

AC_DEFINE_UNQUOTED([PACKAGE_SERIES_FUNCNAME],
                   ${PACKAGE}_version_series_${PACKAGE_VERSION_SERIES},
                    [ series function name ])

if test "X${VERSION_STATUS}" = "Xdevel"; then
    AC_DEFINE([DEVEL], 1, [ am I development release?  ])
fi

AC_SUBST(VERSION_CVSTAG)
AC_DEFINE_UNQUOTED([VERSION_YEAR], "$VERSION_YEAR", [version year])

dnl cond. building NFY
dnl AC_DEFINE([ENABLE_VFILE_TRURLIO],1,[defined if trurlio is used for vfile operations])

AC_DISABLE_STATIC

AC_SYS_LARGEFILE

dnl Checks for programs.
m4_ifdef([LT_INIT], [LT_INIT], [AC_PROG_LIBTOOL])
AC_PROG_CC()
#AC_PROG_CXX()
AC_C_INLINE
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MKDIR_P
AC_CHECK_PROG(POD2MAN, pod2man, pod2man)
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION(0.11.5)

AM_CFLAGS="-D_GNU_SOURCE=1"
AC_SUBST(AM_CFLAGS)

AC_ARG_ENABLE(strict, AS_HELP_STRING([--enable-strict], [Enable strict compilation options]), enable_strict=$enableval,
                                                         enable_strict=no)
if test x$enable_strict != xno; then
    if test -n "$GCC"; then
        AM_CFLAGS="$AM_CFLAGS -Werror"
    fi
fi

if test -n "$GCC"; then
    AM_CFLAGS="$AM_CFLAGS -W -Wall -Wextra -Wformat-security -Werror=format-security -Wshadow -fbounds-check -pipe"
fi

if test -n "$GCC"; then
    cflags_save="$CFLAGS"
    CFLAGS="$CFLAGS -fvisibility=hidden"
    AC_MSG_CHECKING([whether gcc supports visibility])
    AC_TRY_COMPILE(
        [extern __attribute__((__visibility__("hidden"))) int hiddenvar;
    	 extern __attribute__((__visibility__("default"))) int exportedvar;
    	 extern __attribute__((__visibility__("hidden"))) int hiddenfunc (void);
    	 extern __attribute__((__visibility__("default"))) int exportedfunc (void);],
    	[],
    	AC_MSG_RESULT(yes),
    	[
    	    AC_MSG_WARN([compiler does not support visibility])
    	    CFLAGS="$cflags_save"
	    ]
    )
fi

PKG_PROG_PKG_CONFIG()

PKG_CHECK_MODULES(CHECK, check >= 0.9.4, have_check=yes, have_check=no)

dnl Process features.
AC_CHECK_LIB(tinfo, tigetstr,,[
	AC_CHECK_LIB(ncurses, tigetstr, ,[
		AC_MSG_ERROR(["tinfo nor ncurses not found"]) ]) ])

AC_CHECK_HEADERS([ncurses/curses.h],
	[ CPPFLAGS="-I/usr/include/ncurses $CPPFLAGS" ],
	[ AC_CHECK_HEADERS(curses.h,, [AC_MSG_ERROR(["curses.h not found"])],) ])

AC_ARG_ENABLE(imode,
[  --disable-imode               turn off interactive mode],
INTERACTIVE_MODE=$enableval, INTERACTIVE_MODE=yes)

if test "${INTERACTIVE_MODE}." != "yes."; then
	AM_CONDITIONAL(ENABLE_INTERACTIVE_MODE, false)
else
	AC_CHECK_LIB(readline, readline,,[ AC_MSG_ERROR(["readline not found"])])
	AC_CHECK_LIB(pcre, pcre_compile,,[ AC_MSG_ERROR(["pcre not found"])])
	AC_CHECK_HEADERS([pcre.h],,[
		AC_CHECK_HEADERS([pcre/pcre.h],
			         [CPPFLAGS="-I/usr/include/pcre $CPPFLAGS"], )], )

	AM_CONDITIONAL(ENABLE_INTERACTIVE_MODE, true)
	AC_DEFINE([ENABLE_INTERACTIVE_MODE],1,[defined if interactive mode is requested])

	AC_CHECK_FUNCS(rl_completion_matches, have_rl_4_2=yes, have_rl_4_2=no,
		     $CONF_IN_LDFLAGS)
	if test "${have_rl_4_2}." = "yes."; then
		AC_DEFINE([HAVE_READLINE_4_2],1,[defined if readline 4.2 is avialiable])
	fi
fi

AC_ARG_ENABLE(threads,
[  --disable-threads         turn off threading],
ENABLE_THREADS=$enableval, ENABLE_THREADS=yes)

if test "${ENABLE_THREADS}." != "yes."; then
	AM_CONDITIONAL(ENABLE_THREADS, false)
else
	AC_CHECK_LIB(pthread, pthread_create,,[ AC_MSG_ERROR(["pthread not found"])])
	AM_CONDITIONAL(ENABLE_THREADS, true)
	AC_DEFINE([ENABLE_THREADS],1,[defined if threading is on])
fi

AC_CHECK_LIB(ssl, SSL_CTX_new,,[ AC_MSG_ERROR(["openssl not found"])])

AC_ARG_WITH(sys-trurlib,
[  --with-sys-trurlib	compile and link with system installed trurlib],
ENABLE_SYS_TRURL=$withval, ENABLE_SYS_TRURL=no)


AC_ARG_ENABLE(static,
[  --enable-static       build static binary],
ENABLE_STATIC=$enableval, ENABLE_STATIC=no)

if test "${ENABLE_STATIC}." = "yes."; then
	AC_DEFINE([ENABLE_STATIC],1, [defined if static linking is requested])
	poldek_LDFLAGS="-all-static"

	if test "${INTERACTIVE_MODE}." = "yes."; then
		LIBS="$LIBS -lncurses"
	fi
fi

AC_SUBST(poldek_LDFLAGS)


AC_ARG_ENABLE(trace,
[  --enable-trace		turn on trace messages],
ENABLE_TRACE=$enableval, ENABLE_TRACE=no)

if test "${ENABLE_TRACE}." = "yes."; then
	AC_DEFINE([ENABLE_TRACE],1,[])
fi


dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T

AC_CHECK_MEMBER(struct tm.tm_gmtoff,
	[AC_DEFINE([HAVE_TM_GMTOFF],1,[])],
	[AC_CHECK_MEMBER(struct tm.__tm_gmtoff,[ AC_DEFINE([HAVE_TM___GMTOFF],1,[])]
	,,[#include <time.h>])],
[#include <time.h>])


dnl Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_FNMATCH
AC_FUNC_STRFTIME
AC_FUNC_VPRINTF
AC_CHECK_FUNCS(gettimeofday mkdir strdup uname mkdtemp canonicalize_file_name)
AC_CHECK_FUNCS(strerror strstr strtol strtoul strsignal)
AC_CHECK_FUNCS(mallopt mallinfo2)
AC_CHECK_FUNCS(setenv clearenv)
AC_CHECK_FUNCS(ngettext setlocale)

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h limits.h malloc.h locale.h])
AC_CHECK_HEADERS([sys/file.h sys/time.h syslog.h unistd.h])

AC_CHECK_HEADER([stdint.h],,AC_MSG_ERROR(["C9X compiler is needed by $PACKAGE"]))
AC_CHECK_HEADER([argp.h],,AC_MSG_ERROR(["missing required argp.h"]))

AC_CHECK_FUNCS(argp_usage, ,
        [AC_CHECK_LIB(argp, argp_usage, [LIBS="$LIBS -largp"],
                      [AC_MSG_ERROR(["argp is required"])])
        ])

#dnl FreeBSD (bug?) - netinet/in.h depends on in_systm.h
#AC_CHECK_HEADERS(netinet/in_systm.h)

dnl FreeBSD; FIONBIO definition
AC_CHECK_HEADERS([sys/filio.h])

dnl RH rpm needs this
CPPFLAGS="-I/usr/include/rpm $CPPFLAGS"
AC_CHECK_HEADER([rpm/rpmdb.h], [], AC_MSG_ERROR(["missing required rpmdb.h"]))

AC_MSG_CHECKING([for rpm 4.x])
AC_EGREP_CPP(yes,
    [#include <rpm/rpmlib.h>
     #ifdef RPMDBI_PACKAGES
	yes
     #endif
    ], is_rpm4=yes, is_rpm4=no)

if test "${is_rpm4}." = "no." ; then
	AC_MSG_RESULT(no)
else
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_RPM_4_0],1,[defined if rpm 4.x is installed])
	AC_EGREP_CPP(RPMSENSE_SCRIPT_PRE,
		[ #include <rpm/rpmlib.h>
		], is_rpm_extdeps=yes, is_rpm_extdeps=no)
	if test "${is_rpm_extdeps}." = "yes." ; then
		AC_DEFINE([HAVE_RPM_EXTDEPS])
	fi

	AC_MSG_CHECKING([for rpm >= 4.0.4])
	AC_EGREP_CPP(RPMSENSE_KEYRING,
		[ #include <rpm/rpmcli.h>
		], is_rpm4_0_4=yes, is_rpm4_0_4=no)

	if test "${is_rpm4_0_4}." = "yes." ; then
		AC_DEFINE([HAVE_RPM_4_0_4],1,[])
		CFLAGS="$CFLAGS -D_REENTRANT"
		AC_MSG_RESULT(yes)
	else
	        AC_MSG_RESULT(no)
	fi

	AC_EGREP_CPP(RPMTAG_PUBKEYS,
		[ #include <rpm/rpmlib.h>
		], have_tag_pubkeys=yes, have_tag_pubkeys=no)

        AC_EGREP_CPP(RPMTAG_SUGGESTS,
		[ #include <rpm/rpmlib.h>
		], have_tag_suggests=yes, have_tag_suggests=no)
fi


AC_MSG_CHECKING([for rpmorg (>= 4.14)])
PKG_CHECK_EXISTS([rpm >= 4.14 rpm < 5.0], [is_rpmorg=yes], [is_rpmorg=no])
if test "${is_rpmorg}." = "yes."; then
    AC_MSG_RESULT(yes)
    AM_CONDITIONAL(WITH_RPMORG, true)
    AC_DEFINE([HAVE_RPMORG], [1], [])
    AC_EGREP_CPP(RPMTAG_SUGGESTNAME,
         [ #include <rpm/rpmtag.h>
	 ], have_tag_suggests=yes, have_tag_suggests=no)
else
    AC_MSG_RESULT(no)
    AM_CONDITIONAL(WITH_RPMORG, false)
fi

AC_MSG_CHECKING([for rpm5])
PKG_CHECK_EXISTS([rpm >= 5.0], [is_rpm5=yes], [is_rpm5=no])

if test "${is_rpm5}." = "yes."; then
    AC_DEFINE([HAVE_RPM_5], [1], [])
    LIBS="$LIBS -lrpmdb"
    AC_MSG_RESULT(yes)
    AC_EGREP_CPP(RPMTAG_SUGGESTSNAME,
         [ #include <rpm/rpmtag.h>
	 ], have_tag_suggests=yes, have_tag_suggests=no)

dnl rpm5 linking w/o
    AC_CHECK_LIB(bz2, BZ2_bzCompress)
    AC_CHECK_LIB(z, gzopen)
    AC_CHECK_FUNCS(gzungetc)
    AC_CHECK_LIB(popt, poptReadConfigFile)
else
    AC_MSG_RESULT(no)
fi

if test "${have_tag_suggests}." = "yes." ; then
    AC_DEFINE([HAVE_RPMTAG_SUGGESTS], 1, [rpm with RPMTAG_SUGGESTS support])
fi

AC_EGREP_CPP(RPMTAG_PUBKEYS,
	 [ #include <rpm/rpmtag.h>
         ], have_tag_pubkeys=yes, have_tag_pubkeys=no)

if test "${have_tag_pubkeys}." = "yes." ; then
    AC_DEFINE([HAVE_RPMTAG_PUBKEYS], 1, [rpm with RPMTAG_PUBKEYS])
fi

dnl Checks for libraries.
dnl AC_CHECK_LIB(bz2, BZ2_bzCompress)
dnl AC_CHECK_LIB(z, gzopen)
dnl AC_CHECK_FUNCS(gzungetc)

DBLIB=""
if test "${is_rpm4_0_4}." = "no." ; then
	dnl tocheck: -ldb-3.1 causes link with static lib from /usr/lib
	dnl instead shared one from /lib
	#AC_CHECK_LIB(db-3.1, db_create, [DBLIB="-ldb-3.1"],
	#  AC_CHECK_LIB(db-3.0, db_create, [DBLIB="-ldb-3.0"],
	#    AC_CHECK_LIB(db, db_create, [DBLIB="-ldb"])
	#  )
	#)
	CPPFLAGS="-I/usr/include/db3 $CPPFLAGS"

	AC_CHECK_LIB(db, db_create, [DBLIB="-ldb"],,[$LIBS])
	# assume that librpm complied with --enable-db1
	#if test "${ENABLE_STATIC}." = "yes."; then
	#	DBLIB="$DBLIB -ldb1"
	#fi
else
    if test "${is_rpmorg}." = "yes."; then
	: ;
    else
        AC_CHECK_LIB(db4, db_create, [DBLIB="-ldb4" DBINCLUDE="-I/usr/include/db4"],
            [AC_CHECK_LIB(db-4.1, db_create, [DBLIB="-ldb-4.1" DBINCLUDE="-I/usr/include/db4.1"],
                 [AC_CHECK_LIB(db-4.0, db_create, [DBLIB="-ldb-4.0" DBINCLUDE="-I/usr/include/db4.0"],
                     [AC_CHECK_LIB(db-5.2, db_create, [DBLIB="-ldb-5.2" DBINCLUDE="-I/usr/include/db5.2"],,[$LIBS])],
                              [$LIBS])],, [$LIBS])],, [$LIBS])
    fi

    if test "${ENABLE_STATIC}." = "yes."; then
    	DBLIB="$DBLIB"
    fi

    CPPFLAGS="$DBINCLUDE $CPPFLAGS"
fi

if test "${is_rpm4_0_4}." = "yes." ; then
	AC_CHECK_LIB(beecrypt, dhaes_pContextInitEncrypt,
	have_beecrypt=yes, have_beecrypt=no)
fi

AC_CHECK_LIB(rpmio, rpmCleanPath,,AC_MSG_ERROR(["rpmio not found"]))

AC_CHECK_LIB(rpm, rpmReadPackageInfo,[LIBS="-lrpm $DBLIB $LIBS"],
	AC_CHECK_LIB(rpm, rpmReadPackageHeader,
		[LIBS="-lrpm $DBLIB $LIBS"],
		AC_CHECK_LIB(rpm, rpmReadPackageFile,
			[AC_CHECK_LIB(elf, elf_begin)
			 LIBS="-lrpm $DBLIB $LIBS"
			 AC_DEFINE([HAVE_RPM_4_1],1,[])],
			AC_MSG_ERROR(["rpmlib not found"]),
			[$DBLIB]),
		[$DBLIB]),
	    [$DBLIB])

AC_CHECK_FUNCS(rpmlog vrpmlog rpmCheckSig rpmVerifySignature)

dnl rpm 4.2?
AC_CHECK_FUNCS(rpmtsColor, AC_DEFINE([HAVE_RPM_4_2],1,[]))
AC_CHECK_FUNCS(hGetColor, AC_DEFINE([HAVE_RPM_HGETCOLOR],1,[]))

if test "${is_rpmorg}." != "yes."; then
AC_CHECK_HEADERS([db_185.h],,[
	AC_MSG_WARN(["disabled synchronization with db file-dependencies"]) ])
AC_CHECK_FUNCS(__db185_open)
fi

dnl 4.0.4 has cross dependenecies(?)
if test "${is_rpm4_0_4}." = "yes." -a "${ENABLE_STATIC}." = "yes." ; then
	if "${have_beecrypt}." = "yes." ; then
		LIBS="$LIBS -lbz2 -lrpmio -lbeecrypt -lpthread"
	fi
	LIBS="$LIBS -lbz2 -lrpmio"
fi

AC_CHECK_FUNCS(rpmdsRpmlib,,
               [ AC_CHECK_FUNCS(rpmGetRpmlibProvides,,
          	 [AC_MSG_WARN(["poldek will not work fine with rpmlib\(...\) capabilities"])],
          	 [$CONF_IN_LDFLAGS])], [$CONF_IN_LDFLAGS])

dnl FIXME: it is inline function, how to detect it without AC_EGREP_CPP?
AC_CHECK_FUNCS(headerFreeData)

dnl rpm 4.4.6
AC_CHECK_FUNCS(rpmdsUname)
AC_CHECK_FUNCS(rpmdsSysinfo)
AC_CHECK_FUNCS(rpmdsGetconf)
AC_CHECK_FUNCS(rpmdsCpuinfo)

dnl rpm 4.4.7
dnl  - killed rpmMachineScore and rpmGetVar
AC_CHECK_FUNCS(rpmMachineScore)
AC_CHECK_FUNCS(rpmPlatformScore)
AC_CHECK_FUNCS(rpmGetVar)
AC_CHECK_FUNCS(rpmioAccess)
dnl  - new _rpmvercmp()
AC_CHECK_FUNCS(_rpmvercmp)

dnl rpm 4.4.9
AC_CHECK_HEADERS([rpm/rpmevr.h])

dnl rpm 5.x
dnl fnpyKey provider
AC_CHECK_HEADERS([rpm/rpmcb.h])
AC_CHECK_FUNCS(rpmpkgRead)

AC_CHECK_FUNCS(headerNVR)     # removed since 5.0
AC_CHECK_FUNCS(headerNEVRA)
AC_CHECK_FUNCS(rpmErrorString)

AC_CHECK_TYPE([rpmmi],
	      [AC_DEFINE([HAVE_RPMMI], 1, [rpm with database iterator])],
	      [],
	      [[#include <rpm/rpmtypes.h>]])

dnl  - removed enum (not macro) HEADER_MAGIC_YES
if test "${is_rpm4}." = "yes." ; then
   AC_EGREP_CPP(HEADER_MAGIC_YES,
               [ #include <rpm/header.h>
               ], AC_DEFINE([HAVE_RPM_HEADER_MAGIC_YES], 1, []))
   dnl and renamed RPM_INT32_TYPE to RPM_UIINT32_TYPE
   AC_EGREP_CPP(RPM_INT32_TYPE,
               [ #include <rpm/header.h>
               ], AC_DEFINE([HAVE_RPM_INT32_TYPE], 1, []))
fi

dnl metadata & xml2
AC_ARG_WITH(xml-metadata,
[  --without-xml-metadata   build without XML metadata repository support],
METADATA=$withval, METADATA=yes)
LIBXML2_INCLUDE=
if test "${METADATA}." = "yes."; then
 	AC_CHECK_LIB(xml2, xmlReadFile,, [ AC_MSG_ERROR(["libxml2 not found"]) ])

    AC_CHECK_FILE([/usr/include/libxml2/libxml/xmlversion.h],
        [ LIBXML2_INCLUDE="-I/usr/include/libxml2" ],
        [ AC_MSG_ERROR(["/usr/include/libxml2/libxml/xmlversion.h not found"])])
    AC_DEFINE([WITH_METADATA_REPOSITORY],1,[defined if metadata is enabled])
    AM_CONDITIONAL(WITH_METADATA_REPOSITORY, true)
else
    AM_CONDITIONAL(WITH_METADATA_REPOSITORY, false)
fi
AC_SUBST(LIBXML2_INCLUDE)


dnl Stolen from xemacs configure.in
dnl openpty() might be declared in:
dnl - pty.h (Tru64 or Linux)
dnl - libutil.h (FreeBSD)
dnl - util.h (NetBSD)
AC_CHECK_FUNC(openpty, have_openpty=yes, [
  AC_CHECK_LIB(util, openpty, have_openpty=yes need_libutil=yes)])

if test "${have_openpty}." = "yes."; then
  AC_DEFINE([HAVE_OPENPTY],1,[defined if openpty() is avialable])
  AC_CHECK_HEADERS([pty.h libutil.h util.h], break)
  test "$need_libutil" = "yes" && LIBS="-lutil $LIBS"
fi

AC_CHECK_FUNCS(EVP_DigestInit,,
	[AC_CHECK_LIB(crypto, EVP_DigestInit,,
	 [AC_MSG_ERROR(["libcrypto is needed by $PACKAGE"])]
	)],
	[$CONF_IN_LDFLAGS])

if test -f /etc/redhat-release; then
        LIBS="$LIBS -ldl" # rh's crypto needs this
fi

dnl use local ../trurlib copy if it exists
CONF_IN_LDFLAGS=

if test "${ENABLE_SYS_TRURL}." = "no."; then
	if test -f "${srcdir}/trurlib/n_array_new.c"; then
		TRURL_DIR="trurlib"
	fi
fi

AC_SUBST(TRURL_DIR)

TRURL_INCLUDE_LOCAL=
if test -n "$TRURL_DIR"; then
	AC_MSG_CHECKING(for local trurlib copy in $TRURL_DIR)
	AC_MSG_RESULT([yes])
	TRURL_INCLUDE_LOCAL="-I${srcdir}/$TRURL_DIR/include"
	TRURL_INCLUDE="-I\$(top_srcdir)/$TRURL_DIR/include"
	TRURL_LDFLAG="-L\$(top_builddir)/$TRURL_DIR/"
	LDFLAGS="$LDFLAGS $TRURL_LDFLAG"
	CPPFLAGS="$CPPFLAGS $TRURL_INCLUDE"
	LIBS="-ltrurl $LIBS"
else
	AC_CHECK_HEADER([trurl/trurl.h],,AC_MSG_ERROR("trurl.h is missing"))
	AC_CHECK_LIB(trurl, n_cfree)
fi

AC_SUBST(TRURL_INCLUDE)
AC_SUBST(TRURL_LDFLAG)

dnl verify trurlib's version
TN_TEST_INCLUDE="<trurl/nstream.h>"
if test -z "$TRURL_DIR"; then
	AC_CHECK_LIB(trurl, n_stream_open,,
		[AC_MSG_ERROR(["trurlib >= 0.44.0 not found"])], [-Ltrurlib])
else
	AC_CONFIG_SUBDIRS(trurlib)
	TN_TEST_INCLUDE="\"$srcdir/$TRURL_DIR/include/trurl/nstream.h\""
fi

AC_MSG_CHECKING([for trullib >= 0.44.0])
SAVE_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS -I${srcdir}/$TRURL_DIR/include"
AC_EGREP_CPP(yes,
       [#include $TN_TEST_INCLUDE
        #ifdef TN_STREAM_STDIO
          yes
        #endif
       ], is_valid_trurlib_ver=yes, is_valid_trurlib_ver=no)
CPPFLAGS="$SAVE_CPPFLAGS"

if test "${is_valid_trurlib_ver}." = "no." ; then
	AC_MSG_RESULT(no)
   	AC_MSG_ERROR(["trurlib >= 0.44.0 not found"])
else
	AC_MSG_RESULT(yes)
fi

dnl use local tndb copy if it exists
ENABLE_SYS_TNDB="no"
TNDB_DIR=
if test "${ENABLE_SYS_TNDB}." = "no."; then
	if test -f "${srcdir}/tndb/tndb.c"; then
		TNDB_DIR="tndb"
	fi
fi

AC_SUBST(TNDB_DIR)

if test -n "TNDB_DIR"; then
	AC_MSG_CHECKING(for local tndb copy in $TNDB_DIR)
	AC_MSG_RESULT([yes])
	TNDB_INCLUDE="-I\$(top_srcdir)/$TNDB_DIR/"
	TNDB_LDFLAG="-L\$(top_builddir)/$TNDB_DIR/"
	CPPFLAGS="$CPPFLAGS $TNDB_INCLUDE"
    AC_CONFIG_SUBDIRS(tndb)
else
	AC_CHECK_HEADER([tndb/tndb.h],,AC_MSG_ERROR("tndb.h is missing"))
fi

AC_SUBST(TNDB_INCLUDE)
AC_SUBST(TNDB_LDFLAG)

dnl python stuff

AC_ARG_WITH(python,
[  --with-python	build python interface (unusable)],
ENABLE_PYTHON=$withval, ENABLE_PYTHON=no)
if test "${ENABLE_PYTHON}." = "yes."; then
    AM_PATH_PYTHON
    AC_CONFIG_SUBDIRS(python)
    AM_CONDITIONAL(ENABLE_PYTHON, true)
else
    AM_CONDITIONAL(ENABLE_PYTHON, false)
fi

#CPPFLAGS="$CPPFLAGS -DPKGLIBDIR=\\\"\$(pkglibdir)\\\" -DSYSCONFDIR=\\\"\$(sysconfdir)\\\""
saved_prefix="$prefix"; saved_exec_prefix="$exec_prefix"
if test "X$prefix" = "XNONE"; then prefix="$ac_default_prefix"; fi
if test "X$exec_prefix" = "XNONE"; then exec_prefix="$prefix"; fi


SYSCONFDIR="`echo $sysconfdir`"
while echo ${SYSCONFDIR} | grep '\$' >/dev/null; do
        SYSCONFDIR=$(eval echo ${SYSCONFDIR});
done

AC_DEFINE_UNQUOTED(SYSCONFDIR, "$SYSCONFDIR",
        [Full path to system configuration directory (usually /etc)])
AC_SUBST(SYSCONFDIR)


AC_ARG_WITH(pkglibdir,
[  --with-pkglibdir	program executables],
PKGLIBDIR=$withval, PKGLIBDIR="`echo $libdir/$PACKAGE`")

while echo ${PKGLIBDIR} | grep '\$' >/dev/null; do
        PKGLIBDIR=$(eval echo ${PKGLIBDIR});
done
AC_DEFINE_UNQUOTED(PKGLIBDIR, "$PKGLIBDIR",
        [Full path to package lib directory (usually /usr/lib/$PACKAGE)])
AC_SUBST(PKGLIBDIR)

prefix="$saved_prefix"; exec_prefix="$saved_exec_prefix"

AC_CONFIG_FILES([
        po/Makefile.in
        pkgdir/Makefile
        pkgdir/dir/Makefile
        pkgdir/pndir/Makefile
        pkgdir/rpmdb/Makefile
        pkgdir/metadata/Makefile
        pm/Makefile
        pm/rpm/Makefile
        pm/rpmorg/Makefile
        pm/pset/Makefile
        sigint/Makefile
        vfile/Makefile
        vfile/vfff/Makefile
        vfile/tests/Makefile
        python/Makefile
        conf/Makefile
        doc/Makefile
        doc/homepage/Makefile
        doc/pl/Makefile
        install3/Makefile
        Makefile
        poldek.spec
        tests/Makefile
        cli/Makefile
	])
AC_OUTPUT
