//
// Created by blackox626 on 2022/4/19.
//

#include <iostream>
#include "AudioDecoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

AudioDecoder::AudioDecoder() {}

AudioDecoder::~AudioDecoder() {

}

void AudioDecoder::decode_audio(std::string media_path, std::string pcm_path) {
    AVFormatContext *avFormatContext = avformat_alloc_context();
    avformat_open_input(&avFormatContext, media_path.c_str(), nullptr, nullptr);
    int audio_index = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_index < 0) {
        std::cout << "没有找到可用的音频流" << std::endl;
        // todo 如果找不到可以遍历 avFormatContext->streams的codec type是否是音频来再次寻找
    } else {
        // 打印媒体信息
        av_dump_format(avFormatContext, 0, media_path.c_str(), 0);

        // 初始化解码器相关
        const AVCodec *audio_codec = avcodec_find_decoder(avFormatContext->streams[audio_index]->codecpar->codec_id);
        if (nullptr == audio_codec) {
            std::cout << "没找到对应的解码器:" << std::endl;
            return;
        }
        AVCodecContext *codec_ctx = avcodec_alloc_context3(audio_codec);
        // 如果不加这个可能会 报错Invalid data found when processing input
        avcodec_parameters_to_context(codec_ctx, avFormatContext->streams[audio_index]->codecpar);

        // 打开解码器
        int ret = avcodec_open2(codec_ctx, audio_codec, NULL);
        if (ret < 0) {
            std::cout << "解码器打开失败:" << std::endl;
            return;
        }
        // 初始化包和帧数据结构
        AVPacket *avPacket = av_packet_alloc();
        av_init_packet(avPacket);

        AVFrame *frame = av_frame_alloc();


        std::cout << "采样格式sample_fmt:" << codec_ctx->sample_fmt << std::endl;
        std::cout << "AV_SAMPLE_FMT_U8:" << AV_SAMPLE_FMT_U8 << std::endl;
        std::cout << "采样率sample_rate:" << codec_ctx->sample_rate << std::endl;

        FILE *audio_pcm = fopen(pcm_path.c_str(), "wb");
        while (true) {
            ret = av_read_frame(avFormatContext, avPacket);
            if (ret < 0) {
                std::cout << "音频读取完毕" << std::endl;
                break;
            } else if (audio_index == avPacket->stream_index) { // 过滤音频
                ret = avcodec_send_packet(codec_ctx, avPacket);
                if (ret == AVERROR(EAGAIN)) {
                    std::cout << "发送解码EAGAIN：" << std::endl;
                } else if (ret < 0) {
                    char error[1024];
                    av_strerror(ret, error, 1024);
                    std::cout << "发送解码失败：" << error << std::endl;
                    return;
                }
                while (true) {
                    ret = avcodec_receive_frame(codec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        std::cout << "音频解码失败：" << std::endl;
                        return;
                    }
                    // 每帧音频数据量的大小
                    int data_size = av_get_bytes_per_sample(codec_ctx->sample_fmt);
                    /**
                     * P表示Planar（平面），其数据格式排列方式为 :
                       LLLLLLRRRRRRLLLLLLRRRRRRLLLLLLRRRRRRL...（每个LLLLLLRRRRRR为一个音频帧）
                       而不带P的数据格式（即交错排列）排列方式为：
                       LRLRLRLRLRLRLRLRLRLRLRLRLRLRLRLRLRLRL...（每个LR为一个音频样本）
                       播放范例：   ffplay -ar 44100 -ac 2 -f f32le pcm文件路径
                       并不是每一种都是这样的格式
                     */

                    /**
                     * ffplay -ar 44100 -ac 2 -f f32le -i pcm文件路径
                        -ar 表示采样率
                        -ac 表示音频通道数
                        -f 表示 pcm 格式，sample_fmts + le(小端)或者 be(大端)
                        sample_fmts可以通过ffplay -sample_fmts来查询
                        -i 表示输入文件，这里就是 pcm 文件
                     *
                     */

                    /**
                     * 需要注意的一点是planar仅仅是FFmpeg内部使用的储存模式，我们实际中所使用的音频都是packed模式的，
                     * 也就是说我们使用FFmpeg解码出音频PCM数据后，如果需要写入到输出文件，应该将其转为packed模式的输出。
                     */
                    const char *fmt_name = av_get_sample_fmt_name(codec_ctx->sample_fmt);
                    AVSampleFormat pack_fmt = av_get_packed_sample_fmt(codec_ctx->sample_fmt);
                    std::cout << "fmt_name:" << fmt_name << std::endl;
                    std::cout << "pack_fmt:" << pack_fmt << std::endl;
                    std::cout << "frame->format:" << frame->format << std::endl;
                    if (av_sample_fmt_is_planar(codec_ctx->sample_fmt)) {
                        std::cout << "pcm planar模式" << std::endl;
                        for (int i = 0; i < frame->nb_samples; i++) {
                            for (int ch = 0; ch < codec_ctx->channels; ch++) {
                                // 需要储存为pack模式
                                fwrite(frame->data[ch] + data_size * i, 1, data_size, audio_pcm);
                            }
                        }
                    } else {
                        std::cout << "pcm Pack模式" << std::endl;
                        fwrite(frame->data[0], 1, frame->linesize[0], audio_pcm);
                    }
                }
            } else {
                av_packet_unref(avPacket); // 减少引用计数
            }
        }
    }
}
