#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>

#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

#include <stdio.h>
#include <math.h>
//#include <stdarg.h>
#include <stdlib.h>

#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include "libavutil/md5.h"
#include "libavutil/mem.h"

#include <libswresample/swresample.h>
//#include <string.h>
//#include <inttypes.h>
// #include "video_debugging.h"


typedef struct StreamingParams {
  int copyVideo;
  int copyAudio;
  char outputExtension;
  char *muxerOptKey;
  char *muxerOptValue;
  enum AVCodecID videoCodec;
  enum AVCodecID audioCodec;
  int audioStreams;
  int audioChannels;
  int audioSampleRate;
  enum AVSampleFormat audioSampleFormat;
  int audioOutputBitRate;
  AVChannelLayout audioOutputChannelLayout;
  char *codecPrivKey;
  char *codecPrivValue;
  int frameHeight;
  int frameWidth;
  int outputBitRate;
  int bitstreamBufferSize;
  int minBitRate;
  int maxBitRate;
  AVRational pixelAspectRatio;
  AVRational frameRate;
  enum AVPixelFormat videoPixelFormat;
} StreamingParams;

typedef struct StreamingContext {
  AVFormatContext *formatContext;
  AVCodec *videoCodec;
  AVCodec *audioCodec;
  AVStream *videoStream;
  AVStream *audioStream;
  AVCodecContext *videoCodecContext;
  AVCodecContext *audioCodecContext;
  AVChannelLayout *audioChannelLayout;
  int videoIndex;
  int audioIndex;
  int nbVideoStreams;
  int nbAudioStreams;
  char *filename;
} StreamingContext;

typedef struct FilteringContext {
    AVFilterContext *buffersinkContext;
    AVFilterContext *buffersrcContext;
    AVFilterGraph *filterGraph;

    AVPacket *encodePacket;
    AVFrame *filteredFrame;
} FilteringContext;
static FilteringContext *filter_ctx;


int open_media(AVFormatContext **inputFormatContext, const char *inputFilename) {
    int ret;

    *inputFormatContext = avformat_alloc_context();
    if (!inputFormatContext) {
        av_log(NULL, AV_LOG_FATAL, "%lu ---- Unable to allocate memory for decoder format context\n", (unsigned long)time(NULL));
        return AVERROR(ENOMEM);
    }

    if ((ret = avformat_open_input(inputFormatContext, inputFilename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "%lu ---- Error reading header from input stream\n", (unsigned long)time(NULL));
        return ret;
    }

    if ((ret = avformat_find_stream_info(*inputFormatContext, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "%lu ---- Error reading input stream\n", (unsigned long)time(NULL));
        return ret;
    }

    return 0;
}


int fill_stream_info(AVStream *inputStream, AVCodec **inputCodec, AVCodecContext **inputCodecContext) {
    int ret;

    *inputCodec = avcodec_find_decoder(inputStream->codecpar->codec_id);
    if (!inputCodec) {
        av_log(NULL, AV_LOG_FATAL, "Failed to find codec for stream #%u\n", inputStream->index);
        return AVERROR_DECODER_NOT_FOUND;
    }

    *inputCodecContext = avcodec_alloc_context3(*inputCodec);
    if (!inputCodecContext) {
        av_log(NULL, AV_LOG_FATAL, "Failed to allocate inputCodecContext for stream #%u\n", inputStream->index);
        return AVERROR(ENOMEM);
    }

    if ((ret = avcodec_parameters_to_context(*inputCodecContext, inputStream->codecpar)) <0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to fill inputCodecContext for stream #%u\n", inputStream->index);
        return ret;
    }

    if ((ret = avcodec_open2(*inputCodecContext, *inputCodec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open the decoder for stream #%u", inputStream->index);
        return ret;
    }

    return 0;
}


int prepare_decoder(StreamingContext *decoder) {
    for (int i = 0; i < decoder->formatContext->nb_streams; i++) {
        if (decoder->formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            decoder->videoStream = decoder->formatContext->streams[i];
            decoder->videoIndex = i;

            if (fill_stream_info(decoder->videoStream, &decoder->videoCodec, &decoder->videoCodecContext)) {
                return -1;
            }

        } else if (decoder->formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            decoder->audioStream = decoder->formatContext->streams[i];
            decoder->audioIndex = i;

            if (fill_stream_info(decoder->audioStream, &decoder->audioCodec, &decoder->audioCodecContext)) {
                return -1;
            }
        } else {
            av_log(NULL, AV_LOG_INFO, "Skipping stream #%u as it is not an audio or video stream\n", i);
        }
    }
    return 0;
}

/*
static int init_filter_graph(AVFilterGraph **graph, AVFilterContext **filterSourceContext, AVFilterContext **filterSinkContext, StreamingContext *decoder) {
    AVFilterGraph *filterGraph;
    AVFilterContext  *aBufferContext;
    const AVFilter *aBuffer;
    AVFilterContext *aVolumeContext;
    const AVFilter *aVolume;
    AVFilterContext *aFormatContext;
    const AVFilter *aFormat;
    AVFilterContext *aBufferSinkContext;
    const AVFilter *aBufferSink;

    AVDictionary *optionsDict = NULL;
    uint8_t optionsString[1024];
    uint8_t channelLayout[64];

    int err;

    filterGraph = avfilter_graph_alloc();
    if (!filterGraph) {
        fprintf(stderr, "Unable to create filter graph.\n");
        return AVERROR(ENOMEM);
    }

    aBuffer = avfilter_get_by_name("abuffer");
    if (!aBuffer) {
        fprintf(stderr, "Could not find the abuffer filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    aBufferContext = avfilter_graph_alloc_filter(filterGraph, aBuffer, "src");
    if (!aBufferContext) {
        fprintf(stderr, "Could not allocate the abuffer instance.\n");
        return AVERROR(ENOMEM);
    }

    av_channel_layout_describe(&decoder->audioCodecContext->ch_layout, channelLayout, sizeof(channelLayout));
    av_opt_set    (aBufferContext, "channel_layout", channelLayout,                            AV_OPT_SEARCH_CHILDREN);
    av_opt_set    (aBufferContext, "sample_fmt",     av_get_sample_fmt_name(decoder->audioCodecContext->sample_fmt), AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q  (aBufferContext, "time_base",      (AVRational){ 1, decoder->audioCodecContext->sample_rate },  AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(aBufferContext, "sample_rate",    decoder->audioCodecContext->sample_rate,                     AV_OPT_SEARCH_CHILDREN);
}
*/

int prepare_video_encoder(StreamingContext *encoder, AVCodecContext *inputCodecContext, AVRational inputFramerate, StreamingParams streamParameters) {
    int ret;

    encoder->videoStream = avformat_new_stream(encoder->formatContext, NULL);
    encoder->videoCodec = avcodec_find_encoder(streamParameters.videoCodec);
    if (!encoder->videoCodec) {
        av_log(NULL, AV_LOG_FATAL, "Failed to find necessary encoder\n");
        return AVERROR_INVALIDDATA;
    }

    encoder->videoCodecContext = avcodec_alloc_context3(encoder->videoCodec);
    if (!encoder->videoCodecContext) {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate memory for output stream codec context\n");
        return AVERROR(ENOMEM);
    }

    av_opt_set(encoder->videoCodecContext->priv_data, "preset", "fast", 0);
    if (streamParameters.codecPrivKey && streamParameters.codecPrivValue) {
        av_opt_set(encoder->videoCodecContext->priv_data, streamParameters.codecPrivKey, streamParameters.codecPrivValue, 0);
    }

    encoder->videoCodecContext->height = streamParameters.frameHeight;
    encoder->videoCodecContext->width = streamParameters.frameWidth;
    encoder->videoCodecContext->sample_aspect_ratio = streamParameters.pixelAspectRatio;
    encoder->videoCodecContext->pix_fmt = streamParameters.videoPixelFormat;
    encoder->videoCodecContext->bit_rate = streamParameters.outputBitRate;
    encoder->videoCodecContext->rc_buffer_size = streamParameters.bitstreamBufferSize;
    encoder->videoCodecContext->rc_max_rate = streamParameters.maxBitRate;
    encoder->videoCodecContext->rc_min_rate = streamParameters.minBitRate;
    encoder->videoCodecContext->time_base = av_inv_q(streamParameters.frameRate);
    encoder->videoStream->time_base = encoder->videoCodecContext->time_base;

    if ((ret = (avcodec_open2(encoder->videoCodecContext, encoder->videoCodec, NULL))) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open output codec\n");
        return ret;
    }
    av_log(NULL, AV_LOG_INFO, "Copy codec parameters from codec context into video stream\n");
    avcodec_parameters_from_context(encoder->videoStream->codecpar, encoder->videoCodecContext);
    
    return 0;
}


int prepare_audio_encoder(StreamingContext *encoder, StreamingContext *decoder, StreamingParams *streamParameters) {
        encoder->audioStream = avformat_new_stream(encoder->formatContext, NULL);

    encoder->audioCodec = avcodec_find_encoder(streamParameters->audioCodec);
    if (!encoder->audioCodec) {
        av_log(NULL, AV_LOG_FATAL, "Could not find audio encoder codec");
        return AVERROR_ENCODER_NOT_FOUND;
    }

    encoder->audioCodecContext = avcodec_alloc_context3(encoder->audioCodec);
    if (!encoder->audioCodecContext) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for codec context");
        return AVERROR(ENOMEM);
    }

    enum AVSampleFormat inputSampleFormat = decoder->audioCodecContext->sample_fmt;
    encoder->audioCodecContext->sample_fmt = streamParameters->audioSampleFormat;

    encoder->audioCodecContext->ch_layout.nb_channels = streamParameters->audioChannels;

    encoder->audioCodecContext->ch_layout = streamParameters->audioOutputChannelLayout;

    encoder->audioCodecContext->sample_rate = decoder->audioCodecContext->sample_rate;

    encoder->audioCodecContext->sample_fmt = streamParameters->audioSampleFormat;

    encoder->audioCodecContext->bit_rate = streamParameters->audioOutputBitRate;

    encoder->audioCodecContext->time_base = (AVRational){1, streamParameters->audioSampleRate};

    encoder->audioCodecContext->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    for (int i = 0; i < FF_ARRAY_ELEMS(decoder->audioStream); i++) {
        encoder->audioStream->time_base = encoder->audioCodecContext->time_base;
    }

    if (avcodec_open2(encoder->audioCodecContext, encoder->audioCodec, NULL) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to open output codec context");
        return AVERROR_UNKNOWN;
    }

    av_log(NULL, AV_LOG_INFO, "5\n");
    avcodec_parameters_from_context(encoder->audioStream->codecpar, encoder->audioCodecContext);

    return 0;
}


int prepare_copy(AVFormatContext *formatContext, AVStream **avStream, AVCodecParameters *decoderParameters) {
    *avStream = avformat_new_stream(formatContext, NULL);
    avcodec_parameters_copy((*avStream)->codecpar, decoderParameters);
    return 0;
}


int remux(AVPacket **packet, AVFormatContext **formatContext, AVRational decoderTimebase, AVRational encoderTimebase) {
    av_packet_rescale_ts(*packet, decoderTimebase, encoderTimebase);
    if (av_interleaved_write_frame(*formatContext, *packet) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while copying stream packet");
        return AVERROR_UNKNOWN;
    }
    return 0;
}


int encode_video(StreamingContext *decoder, StreamingContext *encoder, AVFrame *inputFrame) {
    if (inputFrame != NULL) {
    inputFrame->pict_type = AV_PICTURE_TYPE_I;
    inputFrame->interlaced_frame = 1;
    inputFrame->top_field_first = 1;
    }

    AVPacket *outputPacket = av_packet_alloc();
    if (!outputPacket) {
        av_log(NULL, AV_LOG_FATAL, "Could not alloate memory for output video packet");
        return AVERROR(ENOMEM);
    }

    int response = avcodec_send_frame(encoder->videoCodecContext, inputFrame);

    while (response >= 0) {
        response = avcodec_receive_packet(encoder->videoCodecContext, outputPacket);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while receiving video packet from encoder: %s\n", av_err2str(response));
            return -1;
        }

        outputPacket->stream_index = decoder->videoIndex;
        outputPacket->duration = encoder->videoStream->time_base.den / encoder->videoStream->time_base.num / decoder->videoStream->avg_frame_rate.num * decoder->videoStream->avg_frame_rate.den;

        av_packet_rescale_ts(outputPacket, decoder->videoStream->time_base, encoder->videoStream->time_base);
        response = av_interleaved_write_frame(encoder->formatContext, outputPacket);
        if (response != 0) {
            av_log(NULL, AV_LOG_ERROR, "Error %d while receiving video packet from decoder: %s", response, av_err2str(response));
            return -1;
        }
    }
    av_packet_unref(outputPacket);
    av_packet_free(&outputPacket);
    return 0;
}


int encode_audio(StreamingContext *decoder, StreamingContext *encoder, AVFrame *inputFrame, int streamIndex, int flush) {

    av_log(NULL, AV_LOG_INFO, "1\n");
    //StreamContext *stream = &stream_ctx[stream_index];
    FilteringContext *filter = &filter_ctx[streamIndex];
    AVFrame *filt_frame = flush ? NULL : filter->filteredFrame;
    AVPacket *outputPacket = filter->encodePacket;

    int ret;
    av_log(NULL, AV_LOG_INFO, "2\n");
    av_packet_unref(outputPacket);

    int response = avcodec_send_frame(encoder->audioCodecContext, filt_frame);

    //AVPacket *outputPacket = av_packet_alloc();
   if (!outputPacket) {

        return AVERROR(ENOMEM);
    }

    av_log(NULL, AV_LOG_INFO, "3\n");

    while (response >= 0) {
        response = avcodec_receive_packet(encoder->audioCodecContext, outputPacket);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while receiving audio packet from encoder: %s\n", av_err2str(response));
            return -1;
        }
        av_log(NULL, AV_LOG_INFO, "4\n");
        outputPacket->stream_index = decoder->audioIndex;
        av_log(NULL, AV_LOG_INFO, "5\n");
        av_packet_rescale_ts(outputPacket, decoder->audioStream->time_base, encoder->audioStream->time_base);
        av_log(NULL, AV_LOG_INFO, "6\n");
        response = av_interleaved_write_frame(encoder->formatContext, outputPacket);
         if (response != 0) {
            av_log(NULL, AV_LOG_ERROR, "Error %d while receiving audio packet from decoder: %s", response, av_err2str(response));
            return -1;
        }
    }
    av_log(NULL, AV_LOG_INFO, "7\n");
    av_packet_unref(outputPacket);
    av_log(NULL, AV_LOG_INFO, "8\n");
    //av_packet_free(&outputPacket);
    av_log(NULL, AV_LOG_INFO, "11\n");

    return 0;
}


static int filter_encode_audio(StreamingContext *decoder, StreamingContext *encoder, AVFrame *inputFrame, int streamIndex)
{
    FilteringContext *filter = &filter_ctx[streamIndex];
    int ret;

    av_log(NULL, AV_LOG_INFO, "Pushing decoded frame to filters\n");
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(filter->buffersrcContext,
                                       inputFrame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return ret;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        av_log(NULL, AV_LOG_INFO, "Pulling filtered frame from filters\n");
        ret = av_buffersink_get_frame(filter->buffersinkContext,
                                      filter->filteredFrame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns AVERROR_EOF
             * rewrite retcode to 0 to show it as normal procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            break;
        }

        filter->filteredFrame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_audio(decoder, encoder, inputFrame, streamIndex, 0);
        av_frame_unref(filter->filteredFrame);
        if (ret < 0)
            break;
    }

    return ret;
}


int transcode_audio(StreamingContext *decoder, StreamingContext *encoder, AVPacket *inputPacket, AVFrame *inputFrame) {
    av_log(NULL, AV_LOG_INFO, "Audio Transcode Func\n");

    int response = avcodec_send_packet(decoder->audioCodecContext, inputPacket);
    if (response < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while sending audio packet to decoder: %s", av_err2str(response));
        return response;
    }

    while (response >= 0) {
        response = avcodec_receive_frame(decoder->audioCodecContext, inputFrame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;

        } else if (response < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while receiving audio frame from decoder: %s", av_err2str(response));
            return response;
        }

        if (response >= 0) {
            if (filter_encode_audio(decoder, encoder, inputFrame, inputPacket->stream_index)) {
                return -1;
            }
        }
        av_frame_unref(inputFrame);
    }
    return 0;
}


int transcode_video(StreamingContext *decoder, StreamingContext *encoder, AVPacket *inputPacket, AVFrame *inputFrame) {
    int response = avcodec_send_packet(decoder->videoCodecContext, inputPacket);
    if (response < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while sending video packet to decoder: %s", av_err2str(response));
        return response;
    }
    
    while (response >= 0) {
        response = avcodec_receive_frame(decoder->videoCodecContext, inputFrame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        } else if (response < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while receiving video frame from decoder: %s", av_err2str(response));
            return response;
        }

        if (response >= 0) {
            if (encode_video(decoder, encoder, inputFrame)) {
                return -1;
            }
        }
        av_frame_unref(inputFrame);
    }
    return 0;
}


int init_filter(FilteringContext* filterContext, AVCodecContext *decodeContext,
                       AVCodecContext *encodeContext, const char *filterSpec)
{
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = NULL;
    const AVFilter *buffersink = NULL;
    AVFilterContext *buffersrcContext = NULL;
    AVFilterContext *buffersinkContext = NULL;
    AVFilterInOut *filterOutputs = avfilter_inout_alloc();
    AVFilterInOut *filterInputs  = avfilter_inout_alloc();
    AVFilterGraph *filterGraph = avfilter_graph_alloc();

    if (!filterOutputs || !filterInputs || !filterGraph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (decodeContext->codec_type == AVMEDIA_TYPE_AUDIO) {
        char buf[64];
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if (decodeContext->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&decodeContext->ch_layout, decodeContext->ch_layout.nb_channels);
        av_channel_layout_describe(&decodeContext->ch_layout, buf, sizeof(buf));
        snprintf(args, sizeof(args),
                 "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                 decodeContext->time_base.num, decodeContext->time_base.den, decodeContext->sample_rate,
                 av_get_sample_fmt_name(decodeContext->sample_fmt),
                 buf);
        ret = avfilter_graph_create_filter(&buffersrcContext, buffersrc, "in",
                                           args, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersinkContext, buffersink, "out",
                                           NULL, NULL, filterGraph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersinkContext, "sample_fmts",
                             (uint8_t*)&encodeContext->sample_fmt, sizeof(encodeContext->sample_fmt),
                             AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        av_channel_layout_describe(&encodeContext->ch_layout, buf, sizeof(buf));
        ret = av_opt_set(buffersinkContext, "ch_layouts",
                         buf, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersinkContext, "sample_rates",
                             (uint8_t*)&encodeContext->sample_rate, sizeof(encodeContext->sample_rate),
                             AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
            goto end;
        }
    } else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* Endpoints for the filter graph. */
    filterOutputs->name       = av_strdup("in");
    filterOutputs->filter_ctx = buffersrcContext;
    filterOutputs->pad_idx    = 0;
    filterOutputs->next       = NULL;

    filterInputs->name       = av_strdup("out");
    filterInputs->filter_ctx = buffersinkContext;
    filterInputs->pad_idx    = 0;
    filterInputs->next       = NULL;

    if (!filterOutputs->name || !filterInputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avfilter_graph_parse_ptr(filterGraph, filterSpec,
                                        &filterInputs, &filterOutputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filterGraph, NULL)) < 0)
        goto end;

    /* Fill FilteringContext */
    filterContext->buffersrcContext = buffersrcContext;
    filterContext->buffersinkContext = buffersinkContext;
    filterContext->filterGraph = filterGraph;

    end:
    avfilter_inout_free(&filterInputs);
    avfilter_inout_free(&filterOutputs);

    return ret;
}

int init_filters(AVFormatContext *inputFormatContext, StreamingContext *decoder, StreamingContext *encoder) {
    const char *filter_spec;
    unsigned int i;
    int ret;
    filter_ctx = av_malloc_array(inputFormatContext->nb_streams, sizeof(*filter_ctx));
    if (!filter_ctx)
        return AVERROR(ENOMEM);

    for (i = 0; i < inputFormatContext->nb_streams; i++) {
        filter_ctx[i].buffersrcContext  = NULL;
        filter_ctx[i].buffersinkContext = NULL;
        filter_ctx[i].filterGraph   = NULL;
        if (!(inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
              || inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
            continue;


        if (inputFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            av_log(NULL, AV_LOG_INFO, "One\n");
            filter_spec = "anull"; /* passthrough (dummy) filter for audio */
        av_log(NULL, AV_LOG_INFO, "Two\n");
        ret = init_filter(&filter_ctx[i], decoder->audioCodecContext,
                          encoder->audioCodecContext, filter_spec);
        if (ret)
            return ret;

        filter_ctx[i].encodePacket = av_packet_alloc();
        if (!filter_ctx[i].encodePacket)
            return AVERROR(ENOMEM);

        filter_ctx[i].filteredFrame = av_frame_alloc();
        if (!filter_ctx[i].filteredFrame)
            return AVERROR(ENOMEM);
    }
    return 0;
}


int main(int argc, char **argv) {

    StreamingParams testParameters = {0};

  testParameters.copyAudio = 0;
  testParameters.copyVideo = 0;
  testParameters.videoCodec = AV_CODEC_ID_H264;
  testParameters.audioCodec = AV_CODEC_ID_PCM_S16LE;
  testParameters.audioStreams = 1;
  testParameters.audioChannels = 8;
  testParameters.audioSampleRate = 48000;
  testParameters.audioOutputBitRate = 160000;
  testParameters.audioOutputChannelLayout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO_DOWNMIX;
  testParameters.audioSampleFormat = AV_SAMPLE_FMT_S16;
  testParameters.frameWidth = 1440;
  testParameters.frameHeight = 1080;
  testParameters.pixelAspectRatio = (AVRational){3, 4};
  testParameters.frameRate = (AVRational){25, 1};
  testParameters.outputBitRate = 50000000;
  testParameters.bitstreamBufferSize = 80000000;
  testParameters.minBitRate = 40000000;
  testParameters.maxBitRate = 60000000;
  testParameters.videoPixelFormat = AV_PIX_FMT_YUV420P10LE;
  testParameters.codecPrivKey = "x264-params";
  testParameters.codecPrivValue = "avcintra-class=50:colorprim=bt709:transfer=bt709:colormatrix=bt709:interlaced=1:force-cfr=1:keyint=1:min-keyint=1:scenecut=0";

  StreamingParams *pParams = &testParameters;

  int ret;

  StreamingContext *decoder = (StreamingContext*) calloc(1, sizeof(StreamingContext));
  decoder->filename = argv[1];
  
  StreamingContext *encoder = (StreamingContext*) calloc(1, sizeof(StreamingContext));
  encoder->filename = argv[2];
  
  if (testParameters.outputExtension) {
    strcat(encoder->filename, &testParameters.outputExtension);
    }

    open_media(&decoder->formatContext, decoder->filename);

    prepare_decoder(decoder);
    
    avformat_alloc_output_context2(&encoder->formatContext, NULL, NULL, encoder->filename);
    if (!encoder->formatContext) {
        av_log(NULL, AV_LOG_FATAL, "Couldn't allocate memory for output format context\n");
        return AVERROR(ENOMEM);
    }

    AVRational input_framerate = av_guess_frame_rate(decoder->formatContext, decoder->videoStream, NULL);
    av_log(NULL, AV_LOG_INFO, "Preparing video encoder\n");
    prepare_video_encoder(encoder, decoder->videoCodecContext, input_framerate, testParameters);
    av_log(NULL, AV_LOG_INFO, "Preparing audio encoder\n");
    prepare_audio_encoder(encoder, decoder, pParams);
    av_log(NULL, AV_LOG_INFO, "Preparing flags and header");
    if (encoder->formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
        encoder->formatContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        //encoder->formatContext->flags += AV_CODEC_FLAG_INTERLACED_DCT;
        //encoder->formatContext->flags += AV_CODEC_FLAG_INTERLACED_ME;
    }
    av_log(NULL, AV_LOG_INFO, "Preparing more flag shit\n");
    if (!(encoder->formatContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&encoder->formatContext->pb, encoder->filename, AVIO_FLAG_WRITE) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Could not open output file\n");
            return -1;
        }
    }

    AVDictionary *muxerOps = NULL;
    if (testParameters.muxerOptKey && testParameters.muxerOptValue) {
        av_dict_set(&muxerOps, testParameters.muxerOptKey, testParameters.muxerOptValue, 0);
    }

    if (avformat_write_header(encoder->formatContext, NULL) < 0) {
        av_log(NULL, AV_LOG_FATAL, "An error occurred while opening output file\n");
        return -1;
    }

    AVFrame *inputFrame = av_frame_alloc();
    if (!inputFrame) {
        av_log(NULL, AV_LOG_FATAL, "Failed to allocate memory for AVFrame\n");
        return AVERROR(ENOMEM);
    }

    ret = init_filters(decoder->formatContext, decoder, encoder);

    AVPacket *inputPacket = av_packet_alloc();
    if (!inputPacket) {
        av_log(NULL, AV_LOG_FATAL, "Failed to allocate memory for AVPacket\n");
        return AVERROR(ENOMEM);
    }



    while (av_read_frame(decoder->formatContext, inputPacket) >= 0) {

        if (decoder->formatContext->streams[inputPacket->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            av_log(NULL, AV_LOG_INFO, "Video\n");
            if (transcode_video(decoder, encoder, inputPacket, inputFrame)) { return -1; }
            av_packet_unref(inputPacket);
            av_log(NULL, AV_LOG_INFO, "Success\n");
        } else if (decoder->formatContext->streams[inputPacket->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            av_log(NULL, AV_LOG_INFO, "Audio\n");
            if (transcode_audio(decoder, encoder, inputPacket, inputFrame)) { return -1; }
            av_packet_unref(inputPacket);
            av_log(NULL, AV_LOG_INFO, "Success\n");
        } else {
            av_log(NULL, AV_LOG_INFO, "Ignoring non audio or video packet\n");
        }
        av_packet_unref(inputPacket);
    }

    if (encode_video(decoder, encoder, NULL)) {
        return -1;
    }

    av_write_trailer(encoder->formatContext);

    if (muxerOps != NULL) {
        av_dict_free(&muxerOps);
        muxerOps = NULL;
    }

    if (inputFrame != NULL) {
        av_frame_free(&inputFrame);
        inputFrame = NULL;
    }

    if (inputPacket != NULL) {
        av_packet_free(&inputPacket);
        inputPacket = NULL;
    }

    avformat_close_input(&decoder->formatContext);

    avformat_free_context(decoder->formatContext);
    decoder->formatContext = NULL;
    avformat_free_context(encoder->formatContext);
    encoder->formatContext = NULL;

    avcodec_free_context(&decoder->videoCodecContext);
    decoder->videoCodecContext = NULL;
    avcodec_free_context(&decoder->audioCodecContext);
    decoder->audioCodecContext = NULL;

    free(decoder);
    decoder = NULL;
    free(encoder); encoder = NULL;

    return 0;
}
