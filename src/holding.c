































#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

static AVFormatContext *pInputFormatHandle;
static AVFormatContext *pOutputFormatHandle;

typedef struct FilteringContext {
    AVFilterContext *pBufferSinkHandle;
    AVFilterContext *pBufferSourceHandle;
    AVFilterGraph *pFilterGraph;

    AVPacket *pEncoderPacket;
    AVFrame *pFilteredFrame;
} FilteringContext;
static FilteringContext *pFilterHandle;

typedef struct StreamContext {
    AVCodecContext *pDecodeHandle;
    AVCodecContext *pEncodeHandle;

    AVFrame *pDecodeFrame;
} StreamContext;
static StreamContext *pStreamHandle;


static int open_input_file(const char *pFilename) {
    int ret;
    unsigned int i;

    pInputFormatHandle = NULL;
    // Attempt to open file and read header, then format pInputFormatHandle accordingly. Log and return error on failure
    if ((ret = avformat_open_input(&pInputFormatHandle, pFilename, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
        return ret;
    } else {
        av_log(NULL, AV_LOG_INFO, "Successfully opened input file\n");
    }
    // Attempt to read a few packets for input stream info. Log and return error on failure
    if ((ret = avformat_find_stream_info(pInputFormatHandle, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
        return ret;
    } else {
        av_log(NULL, AV_LOG_INFO, "Successfully found stream information\n");
    }
    // Attempt to allocate memory for decode handle, encode handle, and decoded frame, all * number of streams. Log and return on failure
    pStreamHandle = av_calloc(pInputFormatHandle->nb_streams, sizeof(pStreamHandle));
    if (!pStreamHandle) {
        return AVERROR(ENOMEM);
    } else {
        av_log(NULL, AV_LOG_INFO, "Successfully allocated memory for stream handle\n");
    }

    // Iterate through streams
    for (i = 0; i < pInputFormatHandle->nb_streams; i++) {
        AVStream *pStream = pInputFormatHandle->streams[i];
        // Attempt to find codec id for current stream
        const AVCodec *pDecoder = avcodec_find_decoder(pStream->codecpar->codec_id);
        if (!pDecoder) {
            av_log(NULL, AV_LOG_ERROR, "Failed to find decoder for stream #%u\n", i);
            return AVERROR_DECODER_NOT_FOUND;
        } else {
        av_log(NULL, AV_LOG_INFO, "Successfully found decoder for stream\n");
    }
        // Attempt to allocate memory a decoder handle for the current stream based on codec id
        AVCodecContext *pCodecHandle = avcodec_alloc_context3(pDecoder);
        if (!pCodecHandle) {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate the decoder context for stream #%u\n", i);
            return AVERROR(ENOMEM);
        } else {
        av_log(NULL, AV_LOG_INFO, "Successfully allocated the decoder context for stream #%u\n", i);
    }
        // Attempt to copy codec parameters into the newly allocated decoder handle from the current stream
        if ((ret = avcodec_parameters_to_context(pCodecHandle, pStream->codecpar)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to copy decoder parameters to input decoder context "
                   "for stream #%u\n", i);
            return ret;
        } else {
        av_log(NULL, AV_LOG_INFO, "Successfully copied decoder parameters to input decoder context for stream #%u\n", i);
    }
        if (pCodecHandle->codec_type == AVMEDIA_TYPE_VIDEO || pCodecHandle->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (pCodecHandle->codec_type == AVMEDIA_TYPE_VIDEO) {
                pCodecHandle->framerate = av_guess_frame_rate(pInputFormatHandle, pStream, NULL);
            }
            if ((ret = avcodec_open2(pCodecHandle, pDecoder, NULL)) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n", i);
                return ret;
            } else {
        av_log(NULL, AV_LOG_INFO, "Successfully opened decoder for stream #%u\n", i);
    }
        }
        pStreamHandle[i].pDecodeHandle = pCodecHandle;
        pStreamHandle[i].pDecodeFrame = av_frame_alloc();
        if (!pStreamHandle[i].pDecodeFrame)
            return AVERROR(ENOMEM);
    }
    av_log(NULL, AV_LOG_DEBUG, "---------- Start Input Dump ----------\n");
    av_dump_format(pInputFormatHandle, 0, pFilename, 0);
    av_log(NULL, AV_LOG_DEBUG, "---------- End Input Dump ----------\n");
    return 0;
}


static int open_output_file(const char *pFilename) {
    AVStream *pInputStream;
    AVStream *pOutputStream;
    AVCodecContext *pDecoderHandle;
    AVCodecContext *pEncoderHandle;
    const AVCodec *pEncoder;
    int ret;
    unsigned int i;

    pOutputFormatHandle = NULL;
    avformat_alloc_output_context2(&pOutputFormatHandle, NULL, NULL, pFilename);
    if (!pOutputFormatHandle) {
        av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
        return AVERROR_UNKNOWN;
    } else {
        av_log(NULL, AV_LOG_INFO, "Successfully created output context\n");
    }

    for (i = 0; i < pInputFormatHandle->nb_streams; i++) {
        pOutputStream = avformat_new_stream(pOutputFormatHandle, NULL);
        if (!pOutputStream) {
            av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        } else {
        av_log(NULL, AV_LOG_INFO, "Successfully allocated output stream\n");
    }

        pInputStream = pInputFormatHandle->streams[i];
        pDecoderHandle = pStreamHandle[i].pDecodeHandle;

        if (pDecoderHandle->codec_type == AVMEDIA_TYPE_VIDEO || pDecoderHandle->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (pDecoderHandle->codec_type == AVMEDIA_TYPE_VIDEO) {
                pEncoder = avcodec_find_encoder(AV_CODEC_ID_H264);
                if (!pEncoder) {
                    av_log(NULL, AV_LOG_FATAL, "Necessary video encoder not found\n");
                    return AVERROR_INVALIDDATA;
                } else {
            av_log(NULL, AV_LOG_INFO, "Successfully found video encoder\n");
        }
                pEncoderHandle = avcodec_alloc_context3(pEncoder);
                if (!pEncoderHandle) {
                    av_log(NULL, AV_LOG_FATAL, "Failed to allocate video encoder context\n");
                    return AVERROR(ENOMEM);
                } else {
            av_log(NULL, AV_LOG_INFO, "Successfully allocated video encoder context\n");
        }

                pEncoderHandle->height = 1080;
                pEncoderHandle->width = 1440;
                pEncoderHandle->sample_aspect_ratio = (AVRational){16, 9};

                pEncoderHandle->pix_fmt = AV_PIX_FMT_YUV420P;
                pEncoderHandle->time_base = av_inv_q((AVRational){25, 1});
            } else if (pDecoderHandle->codec_type == AVMEDIA_TYPE_AUDIO) {
                pEncoder = avcodec_find_encoder(AV_CODEC_ID_PCM_S24LE);
                if (!pEncoder) {
                    av_log(NULL, AV_LOG_FATAL, "Necessary audio encoder not found\n");
                    return AVERROR_INVALIDDATA;
                } else {
            av_log(NULL, AV_LOG_INFO, "Successfully found audio encoder\n");
        }
                pEncoderHandle = avcodec_alloc_context3(pEncoder);
                if (!pEncoderHandle) {
                    av_log(NULL, AV_LOG_FATAL, "Failed to allocate audio encoder context\n");
                    return AVERROR(ENOMEM);
                } else {
            av_log(NULL, AV_LOG_INFO, "Successfully allocated audio encoder context\n");
        }
                pEncoderHandle->sample_rate = 48000;
                if ((ret = av_channel_layout_copy(&pEncoderHandle->ch_layout, &pDecoderHandle->ch_layout)) < 0) {
                    return ret;
                }
                pEncoderHandle->sample_fmt = pEncoder->sample_fmts[0];
                pEncoderHandle->time_base = (AVRational){1, pEncoderHandle->sample_rate};
            }

            if (pOutputFormatHandle->oformat->flags & AVFMT_GLOBALHEADER) {
                pEncoderHandle->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }
            if ((ret = avcodec_open2(pEncoderHandle, pEncoder, NULL)) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n", i);
                return ret;
            } else {
            av_log(NULL, AV_LOG_INFO, "Successfully opened video encoder for stream #%u\n", i);
        }
            ret = avcodec_parameters_from_context(pOutputStream->codecpar, pEncoderHandle);
                if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Failed to copy encoder parameters to output stream #%u\n", i);
                    return ret;
                }

                pOutputStream->time_base = pEncoderHandle->time_base;
                pStreamHandle[i].pEncodeHandle = pEncoderHandle;
        } else if (pDecoderHandle->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            av_log(NULL, AV_LOG_FATAL, "Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return AVERROR_INVALIDDATA;
        } else {
            ret = avcodec_parameters_copy(pOutputStream->codecpar, pInputStream->codecpar);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Copying parameters for stream #%u failed\n", i);
                return ret;
            }
            pOutputStream->time_base = pInputStream->time_base;
        }
    }
    av_dump_format(pOutputFormatHandle, 0, pFilename, 1);

    if (!(pOutputFormatHandle->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&pOutputFormatHandle->pb, pFilename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", pFilename);
            return ret;
        }
    }

    ret = avformat_write_header(pOutputFormatHandle, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
        return ret;
    }

    return 0;
}


static int init_filter(FilteringContext* fctx, AVCodecContext *pDecoderHandle,
        AVCodecContext *pEncoderHandle, const char *pFilterSpec) {
    char args[512];
    int ret = 0;
    const AVFilter *pBufferSource = NULL;
    const AVFilter *pBufferSink = NULL;
    AVFilterContext *pBufferSourceHandle = NULL;
    AVFilterContext *pBufferSinkHandle = NULL;
    AVFilterInOut *pOutputs = avfilter_inout_alloc();
    AVFilterInOut *pInputs  = avfilter_inout_alloc();
    AVFilterGraph *pFilterGraph = avfilter_graph_alloc();

    if (!pOutputs || !pInputs || !pFilterGraph) {
        ret = AVERROR(ENOMEM);
        goto end;
    } else {
        av_log(NULL, AV_LOG_INFO, "Succesfully allocated filter inputs, outputs, and filtergraph\n");
    }

    if (pDecoderHandle->codec_type == AVMEDIA_TYPE_VIDEO) {
        pBufferSource = avfilter_get_by_name("buffer");
        pBufferSink = avfilter_get_by_name("buffersink");
        if (!pBufferSource || !pBufferSink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                pDecoderHandle->width, pDecoderHandle->height, pDecoderHandle->pix_fmt,
                pDecoderHandle->time_base.num, pDecoderHandle->time_base.den,
                pDecoderHandle->sample_aspect_ratio.num,
                pDecoderHandle->sample_aspect_ratio.den);

        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",
                (uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        char buf[64];
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if (dec_ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&dec_ctx->ch_layout, dec_ctx->ch_layout.nb_channels);
        av_channel_layout_describe(&dec_ctx->ch_layout, buf, sizeof(buf));
        snprintf(args, sizeof(args),
                "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
                av_get_sample_fmt_name(dec_ctx->sample_fmt),
                buf);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
                (uint8_t*)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt),
                AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        av_channel_layout_describe(&enc_ctx->ch_layout, buf, sizeof(buf));
        ret = av_opt_set(buffersink_ctx, "ch_layouts",
                         buf, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
                (uint8_t*)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate),
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
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_spec,
                    &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

    /* Fill FilteringContext */
    fctx->buffersrc_ctx = buffersrc_ctx;
    fctx->buffersink_ctx = buffersink_ctx;
    fctx->filter_graph = filter_graph;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}


int main(int argc, char **argv) {

    int ret;
    AVPacket *pPacket = NULL;
    unsigned int streamIndex;
    unsigned int i;

    if (argc != 3) {
        av_log(NULL, AV_LOG_ERROR, "Usage: %s <input file> <output file>\n", argv[0]);
        return 1;
    }

    if ((open_input_file(argv[1])) < 0) {
        goto end;
    }
    if ((ret = open_output_file(argv[2])) < 0) {
        goto end;
    }
    if (!(pPacket = av_packet_alloc())) {
        goto end;
    }





    end:
        return -1;

    return 0;
}
