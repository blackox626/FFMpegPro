//
// Created by blackox626 on 2022/4/23.
//

#include "AudioEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavutil/samplefmt.h>
#include <libavutil/common.h>
#include <libavutil/channel_layout.h>
}

AudioEncoder::AudioEncoder() {}

AudioEncoder::~AudioEncoder() {

}

/**
     * 检查编码器是否支持该采样格式
     * @param codec
     * @param sample_fmt
     * @return
     */
bool check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt) {
    const enum AVSampleFormat *p = codec->sample_fmts;
    while (*p != AV_SAMPLE_FMT_NONE) { // 通过AV_SAMPLE_FMT_NONE作为结束符
        if (*p == sample_fmt)
            return true;
        p++;
    }
    return false;
}

/**
 * 检查编码器是否支持该采样率
 * @param codec
 * @param sample_rate
 * @return
 */
bool check_sample_rate(const AVCodec *codec, const int sample_rate) {
    const int *p = codec->supported_samplerates;
    while (*p != 0) {// 0作为退出条件，比如libfdk-aacenc.c的aac_sample_rates
        printf("%s support %dhz\n", codec->name, *p);
        if (*p == sample_rate)
            return true;
        p++;
    }
    return false;
}


/// 读取pcm 数据 -> avframe  经过codec 编码成 avpacket  经过avformat 封装  生成 aac 音频文件 （跟视频的逻辑不一样？？？ VideoEncoder）
void AudioEncoder::encode_pcm_to_aac(const char *pcm_path, const char *aac_path) {
    av_log_set_level(AV_LOG_DEBUG);
    const AVCodec *avCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    AVCodecContext *avCodecContext = avcodec_alloc_context3(avCodec);
    avCodecContext->sample_rate = 44100;
    // 默认的aac编码器输入的PCM格式为:AV_SAMPLE_FMT_FLTP
    avCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
    avCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;
    avCodecContext->bit_rate = 128 * 1024;
    avCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
    avCodecContext->channels = av_get_channel_layout_nb_channels(avCodecContext->channel_layout);
    avCodecContext->profile = FF_PROFILE_MPEG2_AAC_HE;
    //ffmpeg默认的aac是不带adts，而fdk_aac默认带adts，这里我们强制不带
    avCodecContext->flags = AV_CODEC_FLAG_GLOBAL_HEADER;

    /* 检测支持采样格式支持情况 */
    if (!check_sample_fmt(avCodec, avCodecContext->sample_fmt)) {
        av_log(nullptr, AV_LOG_DEBUG, "Encoder does not support sample format %s",
               av_get_sample_fmt_name(avCodecContext->sample_fmt));
        return;
    }
    if (!check_sample_rate(avCodec, avCodecContext->sample_rate)) {
        av_log(nullptr, AV_LOG_DEBUG, "Encoder does not support sample rate %d", avCodecContext->sample_rate);
        return;
    }
    AVFormatContext *avFormatContext = avformat_alloc_context();
    const AVOutputFormat *avOutputFormat = av_guess_format(nullptr, aac_path, nullptr);
    avFormatContext->oformat = avOutputFormat;
    AVStream *aac_stream = avformat_new_stream(avFormatContext, avCodec);
    // 打开编码器
    int ret = avcodec_open2(avCodecContext, avCodec, nullptr);
    if (ret < 0) {
        char error[1024];
        av_log(nullptr, AV_LOG_DEBUG, "编码器打开失败: %s",
               av_strerror(ret, error, 1024));
    }
    // 编码信息拷贝，放在打开编码器之后
    ret = avcodec_parameters_from_context(aac_stream->codecpar, avCodecContext);

    // 打开输出流
    avio_open(&avFormatContext->pb, aac_path, AVIO_FLAG_WRITE);
    ret = avformat_write_header(avFormatContext, nullptr);
    if (ret < 0) {
        char error[1024];
        av_log(nullptr, AV_LOG_DEBUG, "avformat_write_header fail: %s",
               av_strerror(ret, error, 1024));
        return;
    }
    AVPacket *avPacket = av_packet_alloc();
    AVFrame *avFrame = av_frame_alloc();
    avFrame->channel_layout = avCodecContext->channel_layout;
    avFrame->format = avCodecContext->sample_fmt;
    avFrame->channels = avCodecContext->channels;
    // 每次送多少数据给编码器 aac是1024个采样点
    avFrame->nb_samples = avCodecContext->frame_size;
    // 分配buffer
    av_frame_get_buffer(avFrame, 0);
    // 每帧数据大小
    int per_sample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(avFrame->format));

    FILE *pcm_file = fopen(pcm_path, "rb");
    int64_t pts = 0;
    while (!feof(pcm_file)) {
        // 设置可写
        ret = av_frame_make_writable(avFrame);

        // 从输入文件中交替读取各个声道的数据
        for (int i = 0; i < avFrame->nb_samples; ++i) {
            for (int ch = 0; ch < avCodecContext->channels; ++ch) {
                fread(avFrame->data[ch] + per_sample * i, 1, per_sample, pcm_file);
            }
        }

        // 设置pts 使用采样率作为pts的单位，具体换算成秒 pts*1/采样率
        pts += avFrame->nb_samples;

        /// 不应该是 这样吗？ avFrame->pts = pts/avCodecContext->sample_rate;
        /// 理解下时间基timebase https://zhuanlan.zhihu.com/p/101480401

        avFrame->pts = pts;

        if (ret < 0) {
            char error[1024];
            av_strerror(ret, error, 1024);
            av_log(nullptr, AV_LOG_DEBUG, "av_samples_fill_arrays fail: %s", error);
            return;
        }

        // 送去编码
        ret = avcodec_send_frame(avCodecContext, avFrame);
        if (ret < 0) {
            char error[1024];
            av_strerror(ret, error, 1024);
            av_log(nullptr, AV_LOG_DEBUG, "avcodec_send_frame fail: %s",
                   error);
            return;
        }

        while (true) {
            ret = avcodec_receive_packet(avCodecContext, avPacket);
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                // 需要更多数据
                av_log(nullptr, AV_LOG_DEBUG, "avcodec_receive_packet need more data");
                break;
            } else if (ret < 0) {
                char error[1024];
                av_log(nullptr, AV_LOG_DEBUG, "avcodec_receive_packet fail: %s",
                       av_strerror(ret, error, 1024));
                break;
            } else {
                avPacket->stream_index = aac_stream->index;
                av_interleaved_write_frame(avFormatContext, avPacket);
                av_packet_unref(avPacket);
            }
        }
    }
    av_write_trailer(avFormatContext);
    // 关闭
    avio_close(avFormatContext->pb);
    av_packet_free(&avPacket);
    av_frame_free(&avFrame);
}