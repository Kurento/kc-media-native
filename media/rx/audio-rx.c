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

#include "audio-rx.h"
#include <init-media.h>
#include <socket-manager.h>

#include <pthread.h>

#include "libavformat/avformat.h"

#include "sdp-manager.h"

static char buf[256]; //Log
static char* LOG_TAG = "NDK-audio-rx";

enum {
	DATA_SIZE = (AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2,
};

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int receive = 0;

int
stop_audio_rx() {
	pthread_mutex_lock(&mutex);
	receive = 0;
	pthread_mutex_unlock(&mutex);
	return 0;
}

int
start_audio_rx(const char* sdp, int maxDelay, put_audio_samples_rx callback) {
	AVFormatContext *pFormatCtx = NULL;
	AVFormatParameters params, *ap = &params;
	AVCodecContext *pDecodecCtxAudio = NULL;
	AVCodec *pDecodecAudio = NULL;

	AVPacket avpkt;
	uint8_t *avpkt_data_init;
	uint8_t outbuf[DATA_SIZE];

	int i, ret, audioStream, out_size, len;


	snprintf(buf, sizeof(buf), "sdp: \n%s", sdp);
	//__android_log_write(ANDROID_LOG_DEBUG, LOG_TAG, buf);
	
	if( (ret= init_media()) != 0) {
		//__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "Couldn't init media");
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

		// Open audio file
		if ( (ret = av_open_input_sdp(&pFormatCtx, sdp, ap)) != 0 ) {
			av_strerror(ret, buf, sizeof(buf));
			snprintf(buf, sizeof(buf), "%s: Couldn't process sdp.", buf);
			//__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, buf);
			ret = -2;
			goto end;
		}

		// Retrieve stream information
		if ( (ret = av_find_stream_info(pFormatCtx)) < 0) {
			av_strerror(ret, buf, sizeof(buf));
			snprintf(buf, sizeof(buf), "%s: Couldn't find stream information.", buf);
			//__android_log_write(ANDROID_LOG_WARN, LOG_TAG, buf);
			close_context(pFormatCtx);
		} else
			break;
	}

	// Find the first audio stream
	audioStream = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO) {
			audioStream = i;
			break;
		}
	}
	if (audioStream == -1) {
		//__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "Didn't find a audio stream");
		ret = -4;
		goto end;
	}
	
	// Get a pointer to the codec context for the audio stream
	pDecodecCtxAudio = pFormatCtx->streams[audioStream]->codec;

	// Find the decoder for the video stream
	if(pDecodecCtxAudio->codec_id == CODEC_ID_AMR_NB) {
		pDecodecAudio = avcodec_find_decoder_by_name("libopencore_amrnb");
	}
	else {
		pDecodecAudio = avcodec_find_decoder(pDecodecCtxAudio->codec_id);
	}
		
	
	if (pDecodecAudio == NULL) {
		//__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "Unsupported audio codec!");
		ret = -5;
		goto end;
	}
	
	snprintf(buf, sizeof(buf), "max_delay: %d ms", pFormatCtx->max_delay/1000);
	//__android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);
	snprintf(buf, sizeof(buf), "codec: %s", pDecodecAudio->name);
	//__android_log_write(ANDROID_LOG_DEBUG, LOG_TAG, buf);
	
	
	// Open audio codec
	if (avcodec_open(pDecodecCtxAudio, pDecodecAudio) < 0) {
		ret = -6; // Could not open codec
		goto end;
	}

	snprintf(buf, sizeof(buf), "Sample Rate: %d; Frame Size: %d; Bit Rate: %d",
		pDecodecCtxAudio->sample_rate, pDecodecCtxAudio->frame_size,
		pDecodecCtxAudio->bit_rate);
	//__android_log_write(ANDROID_LOG_DEBUG, LOG_TAG, buf);


i = 0;
int n_packet = 0;

	//READING THE DATA
	for(;;) {
		pthread_mutex_lock(&mutex);
		if (!receive) {
			pthread_mutex_unlock(&mutex);
			break;
		}
		pthread_mutex_unlock(&mutex);

		if (av_read_frame(pFormatCtx, &avpkt) >= 0) {
			avpkt_data_init = avpkt.data;
			//Is this a avpkt from the audio stream?
			if (avpkt.stream_index == audioStream) {
snprintf(buf, sizeof(buf), "%d -------------------", n_packet++);
//__android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);
snprintf(buf, sizeof(buf), "avpkt->pts: %lld", avpkt.pts);
//__android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);
snprintf(buf, sizeof(buf), "avpkt->dts: %lld", avpkt.dts);
//__android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);
snprintf(buf, sizeof(buf), "avpkt->size: %d", avpkt.size);
//__android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);
snprintf(buf, sizeof(buf), "dts/size: %lld", avpkt.dts / avpkt.size);
//__android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);
snprintf(buf, sizeof(buf), "time: %lld s", avpkt.dts / pDecodecCtxAudio->sample_rate);
//__android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);

				while (avpkt.size > 0) {
					//Decode audio frame
					//FIXME: do not reuse outbuf.
					out_size = DATA_SIZE;
					len = avcodec_decode_audio3(pDecodecCtxAudio, (int16_t *) outbuf, &out_size, &avpkt);
					if (len < 0) {
						//__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "Error in audio decoding.");
						break;
					}
					
					pthread_mutex_lock(&mutex);
					if (!receive) {
						pthread_mutex_unlock(&mutex);
						break;
					}
					if (out_size > 0) {
						callback(outbuf, out_size, i);
					}
					pthread_mutex_unlock(&mutex);
					
					avpkt.size -= len;
					avpkt.data += len;
					i++;
				}
			}
			//Free the packet that was allocated by av_read_frame
			avpkt.data = avpkt_data_init;
			av_free_packet(&avpkt);
		}
//__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "next");
	}

	ret = 0;

end:
	//Close the codec
	if (pDecodecCtxAudio)
		avcodec_close(pDecodecCtxAudio);
	
	//Close the audio file
	//__android_log_write(ANDROID_LOG_DEBUG, LOG_TAG, "Close the context...");
	close_context(pFormatCtx);
	//__android_log_write(ANDROID_LOG_DEBUG, LOG_TAG, "ok");

	return ret;
}

