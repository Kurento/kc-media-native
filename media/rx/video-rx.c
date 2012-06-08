/*
 * Kurento Android Media: Android Media Library based on FFmpeg.
 * Copyright (C) 2011  Tikal Technologies
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * 
 * @author Miguel París Díaz
 * 
 */

#include "video-rx.h"
#include <util/log.h>
#include <util/utils.h>
#include <init-media.h>
#include <socket-manager.h>

#include <pthread.h>

#include "libswscale/swscale.h"

#include "sdp-manager.h"

#include <util/utils.h>

static char buf[256];
static char* LOG_TAG = "media-video-rx";

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int receive = 0;
static int sws_flags = SWS_BICUBIC;

int
stop_video_rx() {
	pthread_mutex_lock(&mutex);
	receive = 0;
	set_interrrupt_cb(1);
	pthread_mutex_unlock(&mutex);
	return 0;
}

int
start_video_rx(const char* sdp, int maxDelay, FrameManager *frame_manager) {
	AVFormatContext *pFormatCtx = NULL;
	AVFormatParameters params, *ap = &params;
	AVCodecContext *pDecodecCtxVideo = NULL;
	AVCodec *pDecodecVideo = NULL;
	AVFrame *pFrame = NULL;
	DecodedFrame *decoded_frame;

	AVPacket avpkt;
	uint8_t *avpkt_data_init;

	int i, ret, videoStream, len, got_picture;
	int64_t rx_time;

	struct SwsContext *img_convert_ctx;

	media_log(MEDIA_LOG_DEBUG, LOG_TAG, "sdp: %s", sdp);

	if ((ret = init_media()) != 0) {
		media_log(MEDIA_LOG_ERROR, LOG_TAG, "Couldn't init media");
		goto end;
	}

	pthread_mutex_lock(&mutex);
	receive = 1;
	pthread_mutex_unlock(&mutex);
	for(;;) {
		pthread_mutex_lock(&mutex);
		if (!receive) {
			pthread_mutex_unlock(&mutex);
			goto end;
		}
		pthread_mutex_unlock(&mutex);

		pFormatCtx = avformat_alloc_context();
		pFormatCtx->max_delay = maxDelay * 1000;
		ap->prealloced_context = 1;

		// Open video file
		if ((ret = av_open_input_sdp(&pFormatCtx, sdp, ap)) != 0 ) {
			av_strerror(ret, buf, sizeof(buf));
			media_log(MEDIA_LOG_ERROR, LOG_TAG, "Couldn't process sdp: %s", buf);
			ret = -2;
			goto end;
		}

		// Retrieve stream information
		if ((ret = av_find_stream_info(pFormatCtx)) < 0) {
			av_strerror(ret, buf, sizeof(buf));
			media_log(MEDIA_LOG_WARN, LOG_TAG, "Couldn't find stream information: %s", buf);
			close_context(pFormatCtx);
		} else
			break;
	}

	media_log(MEDIA_LOG_INFO, LOG_TAG, "max_delay: %d ms", pFormatCtx->max_delay/1000);

	// Find the first video stream
	videoStream = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO) {
			videoStream = i;
			break;
		}
	}
	if (videoStream == -1) {
		media_log(MEDIA_LOG_ERROR, LOG_TAG, "Didn't find a video stream");
		ret = -4;
		goto end;
	}

	// Get a pointer to the codec context for the video stream
	pDecodecCtxVideo = pFormatCtx->streams[videoStream]->codec;

	// Find the decoder for the video stream
	pDecodecVideo = avcodec_find_decoder(pDecodecCtxVideo->codec_id);
	if (pDecodecVideo == NULL) {
		media_log(MEDIA_LOG_ERROR, LOG_TAG, "Unsupported video codec");
		ret = -5; // Codec not found
		goto end;
	}

	// Open video codec
	if (avcodec_open(pDecodecCtxVideo, pDecodecVideo) < 0) {
		ret = -6; // Could not open codec
		goto end;
	}

	//STORING THE DATA
	//Allocate video frame
	pFrame = avcodec_alloc_frame();

	//READING THE DATA
	for(;;) {
		pthread_mutex_lock(&mutex);
		if (!receive) {
			pthread_mutex_unlock(&mutex);
			break;
		}
		pthread_mutex_unlock(&mutex);
		if (av_read_frame(pFormatCtx, &avpkt) >= 0) {
			rx_time = av_gettime() / 1000;
			avpkt_data_init = avpkt.data;
			//Is this a avpkt from the video stream?
			if (avpkt.stream_index == videoStream) {
				while (avpkt.size > 0) {
					len = avcodec_decode_video2(pDecodecCtxVideo, pFrame, &got_picture, &avpkt);
					if (len < 0) {
						media_log(MEDIA_LOG_ERROR, LOG_TAG, "Error in video decoding");
						break;
					}
					pthread_mutex_lock(&mutex);
					if (!receive) {
						pthread_mutex_unlock(&mutex);
						break;
					}
					//Did we get a video frame?
					if (got_picture) {
						decoded_frame = frame_manager->get_decoded_frame(
									pDecodecCtxVideo->width, pDecodecCtxVideo->height);
						if (!decoded_frame) {
							pthread_mutex_unlock(&mutex);
							break;
						}

						//Convert the image from its native format to RGB
						img_convert_ctx = sws_getContext(pDecodecCtxVideo->width,
								pDecodecCtxVideo->height, pDecodecCtxVideo->pix_fmt,
								pDecodecCtxVideo->width, pDecodecCtxVideo->height, frame_manager->pix_fmt,
								sws_flags, NULL, NULL, NULL);
						if (img_convert_ctx == NULL) {
							ret = -9;
							goto end;
						}
						sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0,
								pDecodecCtxVideo->height, ((AVPicture*) decoded_frame->pFrameRGB)->data,
								((AVPicture*) decoded_frame->pFrameRGB)->linesize);
						sws_freeContext(img_convert_ctx);

						decoded_frame->width = pDecodecCtxVideo->width;
						decoded_frame->height = pDecodecCtxVideo->height;
						decoded_frame->time_base = pFormatCtx->streams[videoStream]->time_base;
						decoded_frame->pts = avpkt.pts;
						decoded_frame->start_time = pFormatCtx->streams[videoStream]->start_time;
						decoded_frame->rx_time = rx_time;
						decoded_frame->encoded_size = len;

						frame_manager->put_video_frame_rx(decoded_frame);
					}
					pthread_mutex_unlock(&mutex);

					avpkt.size -= len;
					avpkt.data += len;
				}
			}
			//Free the packet that was allocated by av_read_frame
			avpkt.data = avpkt_data_init;
			av_free_packet(&avpkt);
		}
	}

	ret = 0;

end:
	frame_manager->release_decoded_frame();
	av_free(pFrame);

	if (pDecodecCtxVideo)
		avcodec_close(pDecodecCtxVideo);
	close_context(pFormatCtx);

	return ret;
}
