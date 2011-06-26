#include "video.h"

Frame::Frame()
{
    _frame = avcodec_alloc_frame();
}

Video::Video(const char* filename, FRAME_UPDATED_CALLBACK callback)
{
    static bool ls_first = true;
    if (ls_first)
    {
        av_register_all();
        ls_first = false;
    }

    _videoStreamIndex = -1;
    _audioStreamIndex = -1;

    _frameUpdatedCallback = callback;
    Load(filename);
}

void Video::Load(const char* filename)
{
    int i, error;
    
    error = av_open_input_file(&_formatCtx, filename, NULL, 0, NULL);
    if (error) throw new FileNotFoundException("File \"%s\" not found", filename);

    error = av_find_stream_info(_formatCtx);
    if (error < 0) throw new AVStreamException("Could not find stream information");

    for (i=0; i<(int)_formatCtx->nb_streams; i++)
    {
        switch (_formatCtx->streams[i]->codec->codec_type)
        {
            case AVMEDIA_TYPE_VIDEO:
                if (_videoStreamIndex == -1) _videoStreamIndex = i;
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (_audioStreamIndex == -1) _audioStreamIndex = i;
                break;
        }
    }

    if (_videoStreamIndex < 0) throw new AVStreamException("No input video stream found");
    if (_audioStreamIndex < 0) throw new AVStreamException("No input audio stream found");

    _videoCodecCtx = _formatCtx->streams[_videoStreamIndex]->codec;
    _videoCodec = avcodec_find_decoder(_videoCodecCtx->codec_id);
    if (!_videoCodec) throw new AVStreamException("Could not find video decoder \"%s\"", _videoCodecCtx->codec_name);

    _audioCodecCtx = _formatCtx->streams[_audioStreamIndex]->codec;
    _audioCodec = avcodec_find_decoder(_audioCodecCtx->codec_id);
    if (!_audioCodec) throw new AVStreamException("Could not find audio decoder \"%s\"", _audioCodecCtx->codec_name);

    error = avcodec_open(_videoCodecCtx, _videoCodec);
    if (error < 0) throw new AVStreamException("Could not open video decoder \"%s\"", _videoCodecCtx->codec_name);

    error = avcodec_open(_audioCodecCtx, _audioCodec);
    if (error < 0) throw new AVStreamException("Could not open audio decoder \"%s\"", _audioCodecCtx->codec_name);

    _swsCtx = sws_getContext(
        _videoCodecCtx->width, _videoCodecCtx->height, _videoCodecCtx->pix_fmt,
        _videoCodecCtx->width, _videoCodecCtx->height, PIX_FMT_RGB24,
        SWS_BICUBIC, NULL, NULL, NULL);

    int size = avpicture_get_size(PIX_FMT_RGB24, _videoCodecCtx->width, _videoCodecCtx->height);
    _currentFrame = avcodec_alloc_frame();
    _currentBuffer = (uint8_t*) av_malloc(size * sizeof(uint8_t));
    avpicture_fill((AVPicture*)_currentFrame, _currentBuffer, PIX_FMT_RGB24, _videoCodecCtx->width, _videoCodecCtx->height);
}

void Video::Start()
{
    CreateThread(NULL, 0, VideoStreamProc, this, NULL, NULL);
}

DWORD WINAPI Video::VideoStreamProc(LPVOID data)
{
    Video* instance = (Video*) data;
    AVPacket packet;
    AVFrame* frame = avcodec_alloc_frame();
    int frameDone;

    while (av_read_frame(instance->_formatCtx, &packet) >= 0)
    {
        if (packet.stream_index == instance->_videoStreamIndex)
        {
            avcodec_decode_video2(instance->_videoCodecCtx, frame, &frameDone, &packet);
            if (frameDone)
            {
                sws_scale(instance->_swsCtx, frame->data, frame->linesize, 0, instance->_videoCodecCtx->height, instance->_currentFrame->data, instance->_currentFrame->linesize);
                if (instance->_frameUpdatedCallback)
                {
                    instance->_frameUpdatedCallback(instance);
                }
            }
        }
        av_free_packet(&packet);
    }
    av_free(frame);
    return 0;
}
