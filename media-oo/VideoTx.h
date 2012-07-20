
#ifndef __VIDEO_TX_H__
#define __VIDEO_TX_H__

extern "C" {
#include <stdint.h>

#include "libavformat/avformat.h"
#include <libavutil/pixfmt.h>
}

namespace media {
	class VideoTx {
	private:
		AVOutputFormat *_fmt;
		AVFormatContext *_oc;
		AVStream *_video_st;

		AVFrame *_picture, *_tmp_picture;
		uint8_t *_picture_buf;
		uint8_t *_video_outbuf;
		int _video_outbuf_size, _n_frame;
		enum PixelFormat _src_pix_fmt;

	public:
		VideoTx(const char* outfile, int width, int height,
			int frame_rate_num, int frame_rate_den,
			int bit_rate, int gop_size, enum CodecID codec_id,
			int payload_type, enum PixelFormat src_pix_fmt);
		~VideoTx();
		int putVideoFrameTx(uint8_t* frame, int width, int height, int64_t time);
	private:
		AVStream* addVideoStream(enum CodecID codec_id,
					int width, int height, int frame_rate_num,
					int frame_rate_den, int bit_rate, int gop_size);
		int openVideo();
	};
}

#endif /* __VIDEO_TX_H__ */
