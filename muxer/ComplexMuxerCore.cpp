//
// Created by blackox626 on 2022/4/26.
//

/**
 * 将视频和音频合并成mp4输出文件
 */

#ifndef MAX_QUEUE_SIZE
// 队列缓存最大值
#define MAX_QUEUE_SIZE 6
#endif

#include <iostream>
#include <vector>
#include <thread>
#include <AudioHandle.cpp>
#include <VideoHandle.cpp>

extern "C" {
#include <libavformat/avformat.h>
}

class ComplexMuxerCore {

public:

    ComplexMuxerCore() : audio_queue(new std::vector<AVPacket *>()), video_queue(new std::vector<AVPacket *>()) {

    }

    /**
     * 这个是主函数，也就是说在main中调用这个函数即可
     * @param mp3_paths
     * @param mp4_paths
     * @param mp4_out
     */
    void muxer_media(const std::vector<char *> mp3_paths, const std::vector<char *> mp4_paths,
                     const char *mp4_out) {

        audioHandle = new AudioHandle();
        auto a_call_back = std::bind(&ComplexMuxerCore::audio_callback, this, std::placeholders::_1,
                                     std::placeholders::_2, std::placeholders::_3);

        audio_thread = new std::thread(&AudioHandle::handle_audio, audioHandle, mp3_paths, a_call_back);

        videoHandle = new VideoHandle();
        auto v_call_back = std::bind(&ComplexMuxerCore::video_callback, this, std::placeholders::_1,
                                     std::placeholders::_2, std::placeholders::_3);
        video_thread = new std::thread(&VideoHandle::handle_video, videoHandle, mp4_paths, v_call_back);

        muxer_thread = new std::thread(&ComplexMuxerCore::muxer_out, this, mp4_out);

        muxer_thread->join();
    }

    ~ComplexMuxerCore() {
        // todo 释放资源
    }

private:
    VideoHandle *videoHandle = nullptr;
    AudioHandle *audioHandle = nullptr;

    // 音频处理线程
    std::thread *audio_thread = nullptr;
    // 视频处理线程
    std::thread *video_thread = nullptr;
    // 合并线程
    std::thread *muxer_thread = nullptr;
    // 音频队列
    std::vector<AVPacket *> *audio_queue = nullptr;
    // 视频队列
    std::vector<AVPacket *> *video_queue = nullptr;
    // 音频线程同步互斥量
    std::mutex audio_mutex;
    // 视频线程同步互斥量
    std::mutex video_mutex;
    // 合并线程同步互斥量
    std::mutex muxer_mutex;
    // 音频条件变量
    std::condition_variable audio_conditionVariable;
    // 视频条件变量
    std::condition_variable video_conditionVariable;
    // 合并条件变量
    std::condition_variable conditionVariable;
    // 输入音频是否处理完毕
    volatile bool is_audio_end;
    // 输入视频是否处理完毕
    volatile bool is_video_end;
    // 输出视频流的索引
    int out_video_stream_index = -1;
    // 输入视频流索引
    int out_audio_stream_index = -1;
    // 视频pts
    double video_pts = 0;
    // 音频pts
    double audio_pts = 0;

    AVFormatContext *out_format_context = nullptr;

    void muxer_out(const char *mp4_out) {
        out_format_context = avformat_alloc_context();
        const AVOutputFormat *avOutputFormat = av_guess_format(nullptr, mp4_out, nullptr);
        out_format_context->oformat = avOutputFormat;
        while (out_video_stream_index < 0 || out_audio_stream_index < 0) {
            std::cout << "视频流或音频流还没创建好，陷入等待" << std::endl;
            std::unique_lock<std::mutex> muxer_lock(muxer_mutex);
            conditionVariable.wait(muxer_lock);
        }
        int ret = avio_open(&out_format_context->pb, mp4_out, AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cout << "输出流打开失败" << std::endl;
            return;
        }
        std::cout << "开始写入文件头" << std::endl;
        ret = avformat_write_header(out_format_context, nullptr);
        if (ret < 0) {
            std::cout << "文件头写入失败" << std::endl;
            return;
        }
        while (!is_handle_end()) {
            std::cout << "muxer while" << std::endl;
            // 视频包的pts大于音频包或者视频包写完了则写音频包
            if((video_pts > audio_pts && !audio_queue->empty()) ||
               (is_video_end && video_queue->empty())){
                // 写入音频包
                write_audio();
            } else {
                // 写入视频包
                write_video();
            }
        }

        std::cout << "开始写入文件尾" << std::endl;
        ret = av_write_trailer(out_format_context);
        if (ret < 0) {
            std::cout << "文件尾写入失败" << std::endl;
        } else {
            std::cout << "合并完成" << std::endl;
        }
    }

    void write_audio(){
        while (audio_queue->empty() && !is_audio_end) {
            std::cout << "等待音频包生产" << std::endl;
            std::unique_lock<std::mutex> uniqueLock(audio_mutex);
            audio_conditionVariable.wait(uniqueLock);
        }

        if (!audio_queue->empty()) {
            // 锁住
            std::lock_guard<std::mutex> lockGuard(audio_mutex);
            AVPacket *pack = audio_queue->at(0);
            // pts转换
//                    av_packet_rescale_ts(pack,pack->time_base,out_format_context->streams[out_audio_stream_index]->time_base);
            audio_pts = pack->pts * av_q2d(out_format_context->streams[out_audio_stream_index]->time_base);
            std::cout << "写入音频包  audio_pts:" << audio_pts << std::endl;
            av_write_frame(out_format_context, pack);
            av_packet_free(&pack);
            audio_queue->erase(audio_queue->cbegin());
        }
        // 唤醒
        audio_conditionVariable.notify_all();
        // 休眠一下，模拟消费比生产慢
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    void write_video(){
        while (video_queue->empty() && !is_video_end) {
            std::cout << "等待视频包生产" << std::endl;
            std::unique_lock<std::mutex> uniqueLock(video_mutex);
            video_conditionVariable.wait(uniqueLock);
        }
        // 大括号括起来可以及时释放锁 （lock_guard 超出作用域会自动析构，释放锁，unique_lock也是同理）
        if (!video_queue->empty()) {
            // 加锁
            std::lock_guard<std::mutex> lockGuard(video_mutex);
            AVPacket *pack = video_queue->at(0);
            // https://www.cnblogs.com/leisure_chn/p/10584910.html  ffmpeg 时间戳详解
            // 之前在VideoHandle 中转换了统一的pts，现在要转换回去
            av_packet_rescale_ts(pack,AV_TIME_BASE_Q,out_format_context->streams[out_video_stream_index]->time_base);
            video_pts = pack->pts * av_q2d(out_format_context->streams[out_video_stream_index]->time_base);
            std::cout << "写入视频包  video_pts:" << video_pts << std::endl;
            // pts转换
//                    av_packet_rescale_ts(pack,pack->time_base,out_format_context->streams[out_video_stream_index]->time_base);
            av_write_frame(out_format_context, pack);
            av_packet_free(&pack);
            video_queue->erase(video_queue->cbegin());
        }
        // 唤醒
        video_conditionVariable.notify_all();
        // 休眠一下，模拟消费比生产慢
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    void audio_callback(const AVCodecContext *codecContext, AVPacket *avPacket, bool is_end) {
        if (nullptr == out_format_context) {
            // 复用器还没初始化好
            std::cout << "复用器还没初始化" << std::endl;
            return;
        }
        if (out_audio_stream_index < 0) {
            // 加锁
            std::cout << "audio_callback" << std::endl;
            std::lock_guard<std::mutex> lockGuard(muxer_mutex);
            AVStream *audio_stream = avformat_new_stream(out_format_context, codecContext->codec);
            avcodec_parameters_from_context(audio_stream->codecpar, codecContext);
            out_audio_stream_index = audio_stream->index;
            // 唤醒
            conditionVariable.notify_all();
        }
        // 队列超了就阻塞在这里
        while (audio_queue->size() >= MAX_QUEUE_SIZE) {
            std::cout << "音频队列超出缓存，等待消费" << std::endl;
            std::unique_lock<std::mutex> uniqueLock(audio_mutex);
            audio_conditionVariable.wait(uniqueLock);
        }
        {
            if (nullptr != avPacket) {
                std::lock_guard<std::mutex> video_lock(audio_mutex);
                avPacket->stream_index = out_audio_stream_index;
                audio_queue->push_back(avPacket);
            }
        }
        is_audio_end = is_end;
        // 唤醒消费队列
        audio_conditionVariable.notify_all();
    }

    void video_callback(const AVCodecContext *codecContext, AVPacket *avPacket, bool is_end) {
        std::cout << "video_callback" << std::endl;
        // 队列超了就阻塞在这里
        if (nullptr == out_format_context) {
            // 复用器还没初始化好
            return;
        }
        if (out_video_stream_index < 0) {
            // 加锁
            std::cout << "video_callback" << std::endl;
            std::lock_guard<std::mutex> lockGuard(muxer_mutex);
            AVStream *video_stream = avformat_new_stream(out_format_context, codecContext->codec);
            avcodec_parameters_from_context(video_stream->codecpar, codecContext);
            out_video_stream_index = video_stream->index;
            std::cout << "创建视频输出流:" << out_video_stream_index << std::endl;
            // 唤醒
            conditionVariable.notify_all();
        }

        std::cout << "video_callback：" << video_queue->size() << std::endl;

        while (video_queue->size() >= MAX_QUEUE_SIZE) {
            std::cout << "视频队列超出缓存，等待消费" << std::endl;
            std::unique_lock<std::mutex> uniqueLock(video_mutex);
            video_conditionVariable.wait(uniqueLock);
        }

        {
            if (nullptr != avPacket) {
                std::lock_guard<std::mutex> video_lock(video_mutex);
                avPacket->stream_index = out_video_stream_index;
                video_queue->push_back(avPacket);
            }
        }
        is_video_end = is_end;
        // 唤醒消费队列
        video_conditionVariable.notify_all();
    }

    /**
     * 是否处理完毕
     * @return
     */
    bool is_handle_end() {
        return is_video_end &&
               is_audio_end &&
               (nullptr == audio_queue || audio_queue->empty()) &&
               (nullptr == video_queue || video_queue->empty());
    }

};