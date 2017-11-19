/*
 * Copyright (c) 2017 SUSE LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "main.h"

rfbScreenInfoPtr rfbScreen;

static int num_clients_connected = -1;

static void clientgone(rfbClientPtr cl)
{
    video_stop_capture();
    --num_clients_connected;
    debug("%d clients connected\n", num_clients_connected);
    if (!num_clients_connected)
	rfbShutdownServer(rfbScreen, TRUE);
}

static enum rfbNewClientAction newclient(rfbClientPtr cl)
{
    if (num_clients_connected < 0)
	++num_clients_connected;
    ++num_clients_connected;
    debug("%d clients connected\n", num_clients_connected);
    video_start_capture();
    cl->clientGoneHook = clientgone;
    return RFB_CLIENT_ACCEPT;
}

static void HandleKey(rfbBool down,rfbKeySym key,rfbClientPtr cl)
{
    if(down && (key==XK_Escape || key=='q' || key=='Q'))
	rfbCloseClient(cl);
}

int main (int argc, char *argv[])
{
    unsigned width = 1024;
    unsigned height = 768;
    unsigned depth = 32;
    char* port;

    rfbScreen = rfbGetScreen(&argc,argv, width, height, 8, /* actually unused */ 3, depth>>3);
    if(!rfbScreen) {
	fputs("failed to init rfbscreen", stderr);
	exit(1);
    }
    rfbScreen->desktopName = "HDMI";
    rfbScreen->alwaysShared = TRUE;
    rfbScreen->kbdAddEvent = HandleKey;
    rfbScreen->newClientHook = newclient;

    rfbScreen->frameBuffer = (char*)malloc(width*height*(depth>>3));

    // for openQA
    if ((port = getenv("VNC"))) {
        int i = atoi(port);
        rfbScreen->port = 5900 + i;
        rfbScreen->ipv6port = 5900 + i;
    }

    rfbInitServer(rfbScreen);

    if (argc < 2) {
	fputs("missing url\n", stderr);
	exit(1);
    }
    video_init(width, height, depth, argv[1]);

    rfbRunEventLoop(rfbScreen, 40000, FALSE);

    free(rfbScreen->frameBuffer);

    rfbScreenCleanup(rfbScreen);

    return 0;
}

void _debug(const char* file, int line, const char* function, const char* fmt, ...)
{
  va_list argp;

  fprintf(stderr, "DEBUG: %s:%d %s() - ", file, line, function);
  va_start(argp, fmt);
  vfprintf(stderr, fmt, argp);
  va_end(argp);
}

// vim: sw=4
