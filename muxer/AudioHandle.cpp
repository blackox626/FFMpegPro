//
// Created by blackox626 on 2022/4/26.
//

/**
 * 音频处理
 * 解码音频，并且重采样为22050，然后编码成aac
 */
#ifndef TARGET_AUDIO_SAMPLE_RATE
// 采样率
#define TARGET_AUDIO_SAMPLE_RATE  22050
#endif

#include <iostream>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
};

class AudioHandle {

public:
    void handle_audio(std::vector<char *> mp3_paths,
                      std::function<void(const AVCodecContext *, AVPacket *, bool)> callback) {
        // 音频编码器相关
        const AVCodec *avCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        audio_encoder_context = avcodec_alloc_context3(avCodec);
        audio_encoder_context->sample_rate = TARGET_AUDIO_SAMPLE_RATE;
        // 默认的aac编码器输入的PCM格式为:AV_SAMPLE_FMT_FLTP
        audio_encoder_context->sample_fmt = AV_SAMPLE_FMT_FLTP;
        audio_encoder_context->channel_layout = AV_CH_LAYOUT_STEREO;
//        audio_encoder_context->bit_rate = 128 * 1024;
        audio_encoder_context->codec_type = AVMEDIA_TYPE_AUDIO;
        audio_encoder_context->channels = av_get_channel_layout_nb_channels(audio_encoder_context->channel_layout);
        audio_encoder_context->profile = FF_PROFILE_AAC_LOW;
        //ffmpeg默认的aac是不带adts，而fdk_aac默认带adts，这里我们强制不带
        audio_encoder_context->flags = AV_CODEC_FLAG_GLOBAL_HEADER;
        int ret = avcodec_open2(audio_encoder_context, avCodec, nullptr);
        if (ret < 0) {
            std::cout << "音频编码器打开失败" << std::endl;
            return;
        }

        // 初始化audiofifo
        audiofifo = av_audio_fifo_alloc(audio_encoder_context->sample_fmt, audio_encoder_context->channels,
                                        audio_encoder_context->frame_size);

        AVFormatContext *avFormatContext = nullptr;
        AVCodecContext *decoder_context = nullptr;
        AVPacket *avPacket = av_packet_alloc();
        AVFrame *avFrame = av_frame_alloc();
        std::vector<AVPacket *> pack_vector = std::vector<AVPacket *>();
        while (!mp3_paths.empty()) {
            // 先释放旧的
            avcodec_free_context(&decoder_context);
            avformat_free_context(avFormatContext);
            const char *mp3 = mp3_paths.at(0);
            mp3_paths.erase(mp3_paths.cbegin());
            avFormatContext = avformat_alloc_context();
            ret = avformat_open_input(&avFormatContext, mp3, nullptr, nullptr);
            if (ret < 0) {
                std::cout << "音频文件打开失败" << std::endl;
                break;
            }
            int audio_index = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (audio_index < 0) {
                for (int i = 0; i < avFormatContext->nb_streams; ++i) {
                    if (AVMEDIA_TYPE_AUDIO == avFormatContext->streams[i]->codecpar->codec_type) {
                        audio_index = i;
                        std::cout << "找到音频流，audio_index:" << audio_index << std::endl;
                        break;
                    }
                }
                if (audio_index < 0) {
                    std::cout << "没有找到音频流" << std::endl;
                    break;
                }
            }
            const AVCodec *avCodec = avcodec_find_decoder(avFormatContext->streams[audio_index]->codecpar->codec_id);
            decoder_context = avcodec_alloc_context3(avCodec);
            avcodec_parameters_to_context(decoder_context, avFormatContext->streams[audio_index]->codecpar);
            ret = avcodec_open2(decoder_context, avCodec, nullptr);
            if (ret < 0) {
                std::cout << "音频解码器打开失败" << std::endl;
                break;
            }

            while (true) {
                ret = av_read_frame(avFormatContext, avPacket);
                if (ret < 0) {
                    std::cout << "音频包读取完毕" << std::endl;
                    break;
                }
                if (avPacket->stream_index != audio_index) {
                    av_packet_unref(avPacket);
                    continue;
                }
                ret = avcodec_send_packet(decoder_context, avPacket);
                if (ret < 0) {
                    std::cout << "音频包发送解码失败" << std::endl;
                    break;
                }
                while (true) {
                    ret = avcodec_receive_frame(decoder_context, avFrame);
                    if (ret == AVERROR(EAGAIN)) {
                        std::cout << "音频包获取解码帧：EAGAIN" << std::endl;
                        break;
                    } else if (ret < 0) {
                        std::cout << "音频包获取解码帧：fail" << std::endl;
                        break;
                    } else {
                        std::cout << "重新编码音频" << std::endl;
                        // 先进行重采样
                        resample_audio(avFrame);
                        pack_vector.clear();
                        encode_audio(pack_vector, out_frame);
                        while (!pack_vector.empty()) {
                            AVPacket *packet = pack_vector.at(0);
                            pack_vector.erase(pack_vector.cbegin());
                            // 回调
                            callback(audio_encoder_context, packet, false);
                        }
                    }
                }
                av_packet_unref(avPacket);
            }
        }
        avcodec_free_context(&decoder_context);
        avformat_free_context(avFormatContext);
        // 回调结束
        callback(audio_encoder_context, nullptr, true);
    }

private:
    // 视频编码器
    AVCodecContext *audio_encoder_context = nullptr;

    AVFrame *encode_frame = nullptr;
    AVAudioFifo *audiofifo = nullptr;
    int64_t cur_pts = 0;

    // 重采样相关
    SwrContext *swrContext = nullptr;
    AVFrame *out_frame = nullptr;
    int64_t max_dst_nb_samples;

    void init_out_frame(int64_t dst_nb_samples) {
        av_frame_free(&out_frame);
        out_frame = av_frame_alloc();
        out_frame->sample_rate = TARGET_AUDIO_SAMPLE_RATE;
        out_frame->format = AV_SAMPLE_FMT_FLTP;
        out_frame->channel_layout = AV_CH_LAYOUT_STEREO;
        out_frame->nb_samples = dst_nb_samples;
        // 分配buffer
        av_frame_get_buffer(out_frame, 0);
        av_frame_make_writable(out_frame);
    }

    /**
     * 重采样
     * @param avFrame
     */
    void resample_audio(AVFrame *avFrame) {
        if (nullptr == swrContext) {
            /**
             * 以下可以使用 swr_alloc、av_opt_set_channel_layout、av_opt_set_int、av_opt_set_sample_fmt
             * 等API设置，更加灵活
             */
            swrContext = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, TARGET_AUDIO_SAMPLE_RATE,
                                            avFrame->channel_layout, static_cast<AVSampleFormat>(avFrame->format),
                                            avFrame->sample_rate, 0, nullptr);
            swr_init(swrContext);
        }
        // 进行音频重采样
        int src_nb_sample = avFrame->nb_samples;
        // 为了保持从采样后 dst_nb_samples / dest_sample = src_nb_sample / src_sample_rate
        max_dst_nb_samples = av_rescale_rnd(src_nb_sample, TARGET_AUDIO_SAMPLE_RATE, avFrame->sample_rate, AV_ROUND_UP);
        // 从采样器中会缓存一部分，获取缓存的长度
        int64_t delay = swr_get_delay(swrContext, avFrame->sample_rate);
        // 相当于a*b/c
        int64_t dst_nb_samples = av_rescale_rnd(delay + avFrame->nb_samples, TARGET_AUDIO_SAMPLE_RATE,
                                                avFrame->sample_rate,
                                                AV_ROUND_UP);
        if (nullptr == out_frame) {
            init_out_frame(dst_nb_samples);
        }

        if (dst_nb_samples > max_dst_nb_samples) {
            // 需要重新分配buffer
            std::cout << "需要重新分配buffer" << std::endl;
            init_out_frame(dst_nb_samples);
            max_dst_nb_samples = dst_nb_samples;
        }
        // 重采样
        int ret = swr_convert(swrContext, out_frame->data, dst_nb_samples,
                              const_cast<const uint8_t **>(avFrame->data), avFrame->nb_samples);
        if (ret < 0) {
            std::cout << "重采样失败" << std::endl;
        } else {
            // 每帧音频数据量的大小
            int data_size = av_get_bytes_per_sample(static_cast<AVSampleFormat>(out_frame->format));
            // 返回值才是真正的重采样点数
            out_frame->nb_samples = ret;
            std::cout << "重采样成功：" << ret << "----dst_nb_samples:" << dst_nb_samples << "---data_size:" << data_size
                      << std::endl;
        }
    }

    void encode_audio(std::vector<AVPacket *> &pack_vector, AVFrame *avFrame) {
        int cache_size = av_audio_fifo_size(audiofifo);
        std::cout << "cache_size:" << cache_size << std::endl;
        av_audio_fifo_realloc(audiofifo, cache_size + avFrame->nb_samples);
        av_audio_fifo_write(audiofifo, reinterpret_cast<void **>(avFrame->data), avFrame->nb_samples);

        if (nullptr == encode_frame) {
            encode_frame = av_frame_alloc();
            encode_frame->nb_samples = audio_encoder_context->frame_size;
            encode_frame->sample_rate = audio_encoder_context->sample_rate;
            encode_frame->channel_layout = audio_encoder_context->channel_layout;
            encode_frame->channels = audio_encoder_context->channels;
            encode_frame->format = audio_encoder_context->sample_fmt;
            av_frame_get_buffer(encode_frame, 0);
        }

        av_frame_make_writable(encode_frame);
        // todo 如果是冲刷最后几帧数据，不够的可以填充静音  av_samples_set_silence
        while (av_audio_fifo_size(audiofifo) > audio_encoder_context->frame_size) {
            int ret = av_audio_fifo_read(audiofifo, reinterpret_cast<void **>(encode_frame->data),
                                         audio_encoder_context->frame_size);
            if (ret < 0) {
                std::cout << "audiofifo 读取数据失败" << std::endl;
                return;
            }
            // 修改pts
            cur_pts += encode_frame->nb_samples;
            encode_frame->pts = cur_pts;
            ret = avcodec_send_frame(audio_encoder_context, encode_frame);
            if (ret < 0) {
                std::cout << "发送编码失败" << std::endl;
                return;
            }
            while (true) {
                AVPacket *out_pack = av_packet_alloc();
                ret = avcodec_receive_packet(audio_encoder_context, out_pack);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    std::cout << "avcodec_receive_packet end:" << ret << std::endl;
                    break;
                } else if (ret < 0) {
                    std::cout << "avcodec_receive_packet fail:" << ret << std::endl;
                    return;
                } else {
                    pack_vector.push_back(out_pack);
                }
            }
        }
    }
};