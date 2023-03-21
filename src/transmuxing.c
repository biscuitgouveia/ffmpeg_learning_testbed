#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


static void logging(const char *fmt, ...)
{
    va_list args;
    fprintf( stderr, "--- LOG: " );
    va_start( args, fmt );
    vfprintf( stderr, fmt, args );
    va_end( args );
    fprintf( stderr, "\n" );
}


static void log_packet(const AVFormatContext *pFormatContext, const AVPacket *pPacket, const char *pTag)
{
    AVRational *pTimeBase = &pFormatContext->streams[pPacket->stream_index]->time_base;
 
    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           pTag,
           av_ts2str(pPacket->pts), av_ts2timestr(pPacket->pts, pTimeBase),
           av_ts2str(pPacket->dts), av_ts2timestr(pPacket->dts, pTimeBase),
           av_ts2str(pPacket->duration), av_ts2timestr(pPacket->duration, pTimeBase),
           pPacket->stream_index);
}
 

int main(int argc, char **argv){

    const AVOutputFormat *pOutputFormat = NULL;
    AVFormatContext *pInputFormatContext = NULL;
    AVFormatContext *pOutputFormatContext = NULL;
    AVPacket *pPacket;
    const char *pInFilename, *pOutFilename;
    int ret, i;
    int streamIndex = 0;
    int *pStreamsList = NULL;
    int numberOfStreams = 0;

    // Check if enough command-line parameters have been passed
    // ie input file and output file
    if (argc < 3) {
        printf("usage: %s input output\n"
               "API example program to remux a media file with libavformat and libavcodec.\n"
               "The output format is guessed according to the file extension.\n"
               "\n", argv[0]);
        return 1;
    }

    // Allocate pInFilename to first command-line argument, pOutFilename to second
    pInFilename  = argv[1];
    pOutFilename = argv[2];

    // Attempt to allocate an AVPacket with default buffers. If it returns NULL (failure), log to std out and exit program
    logging("Allocating packet memory");
    pPacket = av_packet_alloc();
    if (!pPacket) {
        logging("Could not allocate AVPacket\n");
        return 1;
    }

    // !!! -------------------------------------------------------------------------------------------------------------- !!!
    //     UPON ANY OTHER FAILURE FROM HERE ON SEND TO THE END BLOCK SO THAT THE PACKET CAN BE MANUALLY FREED FROM MEMORY
    //                                   BEFORE RETURNING 1 AND EXITING THE PROGRAM!
    // !!! -------------------------------------------------------------------------------------------------------------- !!!

    // Attempt to open the input stream and read the header. If it returns a value < 0 (failure), log to std out and exit program
    // AVFormat Context will be automatically allocated by this function and sent to address of pInputFormatContext
    logging("Opening input stream");
    if ((ret = avformat_open_input(&pInputFormatContext, pInFilename, NULL, NULL)) < 0 ) {
        logging("Could not open file '%s", pInFilename);
        goto end;
    }

    //Attempt to read stream information from media packets. If it returns a value < 0 (failure), log to std out and exit program
    logging("Reading stream info");
    if ((ret = avformat_find_stream_info(pInputFormatContext, NULL)) < 0 ) {
        logging("Failed to retrieve input stream information");
        goto end;
    }

    // Print detailed information about the input
    logging("Dump 1:\n");
    av_dump_format(pInputFormatContext, 0, pInFilename, 0);

    // Attempt to allocate an AVFormat Context for the output format. If it returns NULL (failure), log to std out and exit program
    logging("Allocating AVFormat Context for output");
    avformat_alloc_output_context2(&pOutputFormatContext, NULL, NULL, pOutFilename);
    if (!pOutputFormatContext) {
        logging("Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    numberOfStreams = pInputFormatContext->nb_streams;

    // Attempt to allocate a memory block for an array listing the stream mapping. If it returns NULL (failure), log to std out and exit program
    logging("Allocating memory for stream mapping");
    pStreamsList = av_calloc(numberOfStreams, sizeof(*pStreamsList));
    if (!pStreamsList) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    pOutputFormat = pOutputFormatContext->oformat;

    // Loop through the input streams
    logging("Analysing input streams");
    for (i = 0; i < pInputFormatContext->nb_streams; i++) {
        AVStream *pOutStream;
        AVStream *pInStream = pInputFormatContext->streams[i];
        AVCodecParameters *pInCodecPar = pInStream->codecpar;
        // Any streams which aren't Audio, Video, or Subtitle - set value to -1 in pStreamsList
        if (pInCodecPar->codec_type != AVMEDIA_TYPE_AUDIO &&
            pInCodecPar->codec_type != AVMEDIA_TYPE_VIDEO &&
            pInCodecPar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
                pStreamsList[i] = -1;
                continue;
        }
        // Any Audio, Video, or Subtitle streams are mapped to incrementing values starting from 1 in pStreamsList
        pStreamsList[i] = streamIndex++;
        // Attempt to add stream to pOutputFormatContext. If it returns NULL (failure), log to std out and exit program
        pOutStream = avformat_new_stream(pOutputFormatContext, NULL);
        if (!pOutStream) {
            logging("Failed allocating output stream\n");
            goto end;
        }
        // Attempt to copy input codec parameters to pOutStream->codecpar. If it returns a value < 0 (failure), log to std out and exit program
        ret = avcodec_parameters_copy(pOutStream->codecpar, pInCodecPar);
        if (ret < 0) {
            logging("Failed to copy codec parameters\n");
            goto end;
        }
        pOutStream->codecpar->codec_tag = 0;
    }
   
    // Print detailed information about the input
    logging("Dump 2:\n");
    av_dump_format(pOutputFormatContext, 0, pOutFilename, 1);

    // Check that the output container format has no flags associated and that it's not a no file
    if (!(pOutputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        // Attempt to create and initialise an AVIO context to access pOutFilename with write permission
        ret = avio_open(&pOutputFormatContext->pb, pOutFilename, AVIO_FLAG_WRITE);
        // If it returns a value < 0 (failure), log to std out and exit program
        if (ret < 0) {
            logging("Could not open output file '%s'", pOutFilename);
            goto end;
        }
    }

    // Attempt to write a header to the output context. If it returns a value < 0 (failure), log to std out and exit program
    logging("Writing output file header");
    ret = avformat_write_header(pOutputFormatContext, NULL);
    if (ret < 0) {
        logging("Error occurred when opening output file\n");
        goto end;
    }
    logging("Attempting to write output streams");
    while (1) {
        // Declare input and ouput streams
        AVStream *pInStream, *pOutStream;
        // Attempt to read the next frame from the input stream into an empty packet. If it returns a value < 0 (failure), break the loop
        ret = av_read_frame(pInputFormatContext, pPacket);
        if (ret < 0) {
            break;
        }
        // Attempt to initialise pInStream with the stream in pInputFormatContext at the same index as the stream index in the
        // newly initialised pPacket from above
        pInStream = pInputFormatContext->streams[pPacket->stream_index];
        // Check if the stream loaded in pPacket is outside of the number of streams previously allocated into memory
        // OR if the stream at this index is less than 0 (ie, not a VIDEO, AUDIO, or SUBTITLE stream as set up earlier)
        if (pPacket->stream_index >= numberOfStreams || pStreamsList[pPacket->stream_index] < 0) {
            // Unload the stream from the packet and move on to the next iteration of the loop
            av_packet_unref(pPacket);
            continue;
        }
        // Set packet stream index to whatever was mapped to that index in pStreamsList earlier,
        // Then initialise pOutStream with the stream at that index in pOutputFormatContext
        pPacket->stream_index = pStreamsList[pPacket->stream_index];
        pOutStream = pOutputFormatContext->streams[pPacket->stream_index];
        log_packet(pInputFormatContext, pPacket, "Input:");
        // Convert packet timescale from input stream timebase to ouput stream timebase
        av_packet_rescale_ts(pPacket, pInStream->time_base, pOutStream->time_base);
        pPacket->pos = -1;
        log_packet(pOutputFormatContext, pPacket, "Output:");
        // Copy packet from packet to OutputFormatConext. If it returns a value < 0 (failure), break the loop
        ret = av_interleaved_write_frame(pOutputFormatContext, pPacket);
        if (ret < 0) {
            logging("Error muxing packet\n");
            break;
        }
    }
    //Write the trailer and free private data
    av_write_trailer(pOutputFormatContext);
    logging("Great success!"); 

end:
    // Free packet
    logging("Freeing Packet");
    av_packet_free(&pPacket);
    // Close Input
    logging("Closing input");
    avformat_close_input(&pInputFormatContext);
    // Close output
    logging("Closing output");
    if (pOutputFormatContext && !(pOutputFormatContext->flags & AVFMT_NOFILE))
        avio_closep(&pOutputFormatContext->pb);
    avformat_free_context(pOutputFormatContext);
 
    av_freep(&pStreamsList);
 
    if (ret < 0 && ret != AVERROR_EOF) {
        logging("Error occurred: %s\n", av_err2str(ret));
        return 1;
    }
    logging("Did it :)");
    return 0;
}
