AC_INIT([TS2RFB],[0.0.0],[lnussel@suse.de], [ts2rfb])
AC_CONFIG_SRCDIR([ts2rfb.c])
AC_PREREQ(2.52)

AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)

AC_PROG_CC

dnl Checks for header files.
AC_HEADER_STDC


PKG_CHECK_MODULES(FFMPEG, [libavformat libavutil libavcodec libswscale])
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)

PKG_CHECK_MODULES(VNC, [libvncserver])
AC_SUBST(VNC_CFLAGS)
AC_SUBST(VNC_LIBS)

AC_SEARCH_LIBS([pthread_create], [pthread])

dnl Use -Wall if we have gcc.
changequote(,)dnl
if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *[\ \	]-Wall[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac
fi
changequote([,])dnl

AC_CONFIG_FILES([
	Makefile
])
AC_OUTPUT
