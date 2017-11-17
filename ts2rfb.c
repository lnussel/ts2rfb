/*
 * Copyright (c) 2012 Stefano Sabatini
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

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <rfb/rfb.h>
#include <rfb/keysym.h>

#define DIMOF(x) (sizeof(x)/sizeof(x[0]))

#define DST_IMG_W   1024
#define DST_IMG_H   768
#define DST_IMG_FMT AV_PIX_FMT_RGB32

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL;
static int width, height;
static enum AVPixelFormat pix_fmt;
static AVStream *video_stream = NULL;
static const char *src_filename = NULL;

static uint8_t *video_dst_data[4] = {NULL};
static int      video_dst_linesize[4];
static int video_dst_bufsize;

static int video_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;
static int video_frame_count = 0;
struct SwsContext *sws_ctx;


static rfbScreenInfoPtr rfbScreen;

// for AV_PIX_FMT_RGB24
static void ppm_save(const uint8_t *buf, int wrap, int xsize, int ysize,
                     const char *filename)
{
    FILE *f;
    int i;

    f = fopen(filename,"w");
    fprintf(f, "P6\n%d %d 255\n", xsize, ysize);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize*3, f);
    fclose(f);
}

static void update_framebuffer(const uint8_t *buf, int wrap, int xsize, int ysize)
{
    memcpy(rfbScreen->frameBuffer, buf, xsize * ysize * 4);
    rfbMarkRectAsModified(rfbScreen,0,0, xsize, ysize);
}

static int decode_packet(int *got_frame, int cached)
{
    int ret = 0;
    int decoded = pkt.size;

    *got_frame = 0;

    if (pkt.stream_index == video_stream_idx) {
        /* decode video frame */
        ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame) {

            if (frame->width != width || frame->height != height ||
                frame->format != pix_fmt) {
                fprintf(stderr, "Warning: Input video format change:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                        width, height, av_get_pix_fmt_name(pix_fmt),
                        frame->width, frame->height,
                        av_get_pix_fmt_name(frame->format));

		width = frame->width;
		height = frame->height;
		pix_fmt = frame->format;

		sws_ctx = sws_getCachedContext(sws_ctx, width, height, pix_fmt,
					 DST_IMG_W, DST_IMG_H, DST_IMG_FMT,
					 0, NULL, NULL, NULL);
		if (!sws_ctx) {
		    fprintf(stderr, "Failed to create scale context for conversion\n");
		    return -1;
		}
            }


#if 0
            printf("video_frame%s n:%d coded_n:%d\n",
                   cached ? "(cached)" : "",
                   video_frame_count++, frame->coded_picture_number);
#endif

	    /* convert to destination format */
	    sws_scale(sws_ctx,
		    frame->data, frame->linesize, 0, frame->height,
			video_dst_data, video_dst_linesize);

#ifdef WRITE_FILE
	    char fn[1024];
	    snprintf(fn, sizeof(fn), "frame-%d.ppm", video_frame_count);
	    ppm_save(video_dst_data[0], video_dst_linesize[0],
		 DST_IMG_W, DST_IMG_H, fn);
#else

	    update_framebuffer(video_dst_data[0], video_dst_linesize[0],
		 DST_IMG_W, DST_IMG_H);

#endif


#if 0
            /* copy decoded frame to destination buffer:
             * this is required since rawvideo expects non aligned data */
            av_image_copy(video_dst_data, video_dst_linesize,
                          (const uint8_t **)(frame->data), frame->linesize,
                          pix_fmt, width, height);

            /* write to rawvideo file */
            fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);
#endif
        }
    }

    return decoded;
}

static int open_codec_context(int *stream_idx,
                              AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), src_filename);
        return ret;
    } else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx) {
            fprintf(stderr, "Failed to allocate the %s codec context\n",
                    av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                    av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders, without reference counting */
        av_dict_set(&opts, "refcounted_frames", "0", 0);
        if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

static void HandleKey(rfbBool down,rfbKeySym key,rfbClientPtr cl)
{
    if(down && (key==XK_Escape || key=='q' || key=='Q'))
	rfbCloseClient(cl);
}

void run_vnc()
{
    char* argv[] = {
	"main",
	NULL,
    };
    int argc = DIMOF(argv)-1;
    unsigned bitsPerPixel = 24;
    unsigned bytesPerPixel = 4;

    rfbScreen = rfbGetScreen(&argc,argv, DST_IMG_W, DST_IMG_H, 8,(bitsPerPixel+7)/8,bytesPerPixel);
    if(!rfbScreen) {
	fputs("failed to init rfbscreen", stderr);
	exit(1);
    }
    rfbScreen->desktopName = "HDMI";
    rfbScreen->alwaysShared = TRUE;
    rfbScreen->kbdAddEvent = HandleKey;

    rfbScreen->frameBuffer = (char*)malloc(DST_IMG_W*bytesPerPixel*height);

    rfbInitServer(rfbScreen);

    rfbRunEventLoop(rfbScreen,40000,TRUE);
}

int main (int argc, char *argv[])
{
    int ret = 0, got_frame;

    if (argc < 2 || !strcmp(argv[1], "-")) {
	src_filename = "pipe:";
    } else {
	src_filename = argv[1];
    }

    /* register all formats and codecs */
    av_register_all();
    avformat_network_init();

    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    if (open_codec_context(&video_stream_idx, &video_dec_ctx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
        video_stream = fmt_ctx->streams[video_stream_idx];

        /* allocate image where the decoded image will be put */
        width = video_dec_ctx->width;
        height = video_dec_ctx->height;
        pix_fmt = video_dec_ctx->pix_fmt;
    }

    if (!video_stream) {
        fprintf(stderr, "Could not find video stream in the input, aborting\n");
        ret = 1;
        goto end;
    }

    ret = av_image_alloc(video_dst_data, video_dst_linesize,
			 DST_IMG_W, DST_IMG_H, DST_IMG_FMT, 1);
    if (ret < 0) {
	fprintf(stderr, "Could not allocate raw video buffer\n");
	goto end;
    }
    video_dst_bufsize = ret;

    /* dump input information to stderr */
    av_dump_format(fmt_ctx, 0, src_filename, 0);

    sws_ctx = sws_getContext(width, height, pix_fmt,
                             DST_IMG_W, DST_IMG_H, DST_IMG_FMT,
                             0, NULL, NULL, NULL);
    if (!sws_ctx) {
        fprintf(stderr, "Failed to create scale context for conversion\n");
        ret = 1;
        goto end;
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    run_vnc();

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* read frames from the file */
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        AVPacket orig_pkt = pkt;
        do {
            ret = decode_packet(&got_frame, 0);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        } while (pkt.size > 0);
        av_packet_unref(&orig_pkt);
    }

    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    do {
        decode_packet(&got_frame, 1);
    } while (got_frame);

    printf("Demuxing succeeded.\n");

    sleep(1000);
end:
    avcodec_free_context(&video_dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_free(video_dst_data[0]);

    return ret < 0;
}
