//
// Created by blackox626 on 2022/6/6.
//

#ifndef FFMPEGPRO_FFMPEGSIMPLEPLAYER_H
#define FFMPEGPRO_FFMPEGSIMPLEPLAYER_H


class FFMpegSimplePlayer {
public:
    FFMpegSimplePlayer();

    virtual ~FFMpegSimplePlayer();

    void play(const char *filepath);
};


#endif //FFMPEGPRO_FFMPEGSIMPLEPLAYER_H
