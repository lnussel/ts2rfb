bin_PROGRAMS = ts2rfb rfbserver

ts2rfb_SOURCES =  \
		  ts2rfb.c

ts2rfb_CPPFLAGS = $(FFMPEG_CFLAGS) $(VNC_CFLAGS)
ts2rfb_LDADD = $(FFMPEG_LIBS) $(VNC_LIBS)

rfbserver_SOURCES = rfbserver.c
rfbserver_CPPFLAGS = $(VNC_CFLAGS)
rfbserver_LDADD = $(VNC_LIBS)
