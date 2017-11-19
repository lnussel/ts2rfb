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

#include "main.h"

#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <pthread.h>
#include <assert.h>

static pthread_t capture_tid;
static int do_capture;
static int capturing;
static int need_join;

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL;
static int width, height;
static enum AVPixelFormat pix_fmt;
static AVStream *video_stream = NULL;
static char *src_filename = NULL;

static uint8_t *video_dst_data[4] = {NULL};
static int      video_dst_linesize[4];
static int video_dst_bufsize;

static int video_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;
struct SwsContext *sws_ctx;

static int fb_width;
static int fb_height;
static int fb_depth;

//#define DEBUG_PPM

#ifdef DEBUG_PPM
static int video_frame_count = 0;
// FIXME: doesnt work atm
static void ppm_save(const uint8_t *buf, int wrap, int xsize, int ysize, int depth,
                     const char *filename)
{
    FILE *f;
    int x, y;

    f = fopen(filename,"w");
    fprintf(f, "P6\n%d %d 255\n", xsize, ysize);
    if (depth == 24)
	for (y = 0; y < ysize; y++)
	    fwrite(buf + y * wrap, 1, xsize*3, f);
    else
	for (y = 0; y < ysize; y++)
	    for (x = 0; x < xsize; x+=4)
		fwrite(buf + y * wrap + x, 1, 3, f);
    fclose(f);
}
#endif

static void update_framebuffer(const uint8_t *buf, int wrap, int xsize, int ysize, int depth)
{
    memcpy(rfbScreen->frameBuffer, buf, xsize * ysize * (depth>>3));
    rfbMarkRectAsModified(rfbScreen,0,0, xsize, ysize);
    //usleep(500);
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
					 fb_depth, fb_height, (fb_depth == 32 ? AV_PIX_FMT_RGB32 : AV_PIX_FMT_RGB24),
					 0, NULL, NULL, NULL);
		if (!sws_ctx) {
		    fprintf(stderr, "Failed to create scale context for conversion\n");
		    return -1;
		}
            }


#ifdef DEBUG_PPM
            printf("video_frame%s n:%d coded_n:%d\n",
                   cached ? "(cached)" : "",
                   video_frame_count++, frame->coded_picture_number);
#endif

	    /* convert to destination format */
	    sws_scale(sws_ctx,
		    frame->data, frame->linesize, 0, frame->height,
			video_dst_data, video_dst_linesize);

#ifdef DEBUG_PPM
	    char fn[1024];
	    snprintf(fn, sizeof(fn), "frame-%d.ppm", video_frame_count);
	    ppm_save(video_dst_data[0], video_dst_linesize[0],
		 fb_width, fb_height, fb_depth, fn);
#endif

	    update_framebuffer(video_dst_data[0], video_dst_linesize[0],
		 fb_width, fb_height, fb_depth);



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
        fprintf(stderr, "Could not find %s stream in input file\n",
                av_get_media_type_string(type));
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

int video_init (int width, int height, int depth, const char* url)
{
    if(src_filename)
	free(src_filename);

    if (!strcmp(url, "-")) {
	src_filename = "pipe:";
    } else {
	src_filename = strdup(url);
    }

    fb_width = width;
    fb_height = height;
    fb_depth = depth;

    /* register all formats and codecs */
    av_register_all();
    avformat_network_init();

    return 1;
}

void _video_capture()
{
    int ret = 0, got_frame;

    debug("");

    assert(fb_depth == 32 || fb_depth == 24);

    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
	goto end;
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
	goto end;
    }

    do_capture = 1;

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
			 fb_width, fb_height, (fb_depth == 32 ? AV_PIX_FMT_RGB32 : AV_PIX_FMT_RGB24), 1);
    if (ret < 0) {
	fprintf(stderr, "Could not allocate raw video buffer\n");
	goto end;
    }
    video_dst_bufsize = ret;

    /* dump input information to stderr */
    av_dump_format(fmt_ctx, 0, src_filename, 0);

    sws_ctx = sws_getContext(width, height, pix_fmt,
                             fb_width, fb_height, (fb_depth == 32 ? AV_PIX_FMT_RGB32 : AV_PIX_FMT_RGB24),
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

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* read frames from the file */
    while (do_capture && av_read_frame(fmt_ctx, &pkt) >= 0) {
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

    printf("Demuxing done.\n");

    ret = 0;

end:
    video_free();
    capturing = 0;
    pthread_exit(ret);
}

int video_start_capture()
{
    if (capturing) {
	debug("already capturing\n");
	return 0;
    }
    capturing = 1;
    need_join = 1;
    pthread_create(&capture_tid, NULL, _video_capture, NULL);
    return 1;
}

int video_stop_capture()
{
    do_capture = 0;
    if (need_join) {
	if (!capturing)
	    debug("capture thread exited too early\n");
	pthread_join(capture_tid, NULL);
	need_join = 0;
    }

    return 1;
}

void video_free()
{
    avcodec_free_context(&video_dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_free(video_dst_data[0]);
}

// vim: sw=4
