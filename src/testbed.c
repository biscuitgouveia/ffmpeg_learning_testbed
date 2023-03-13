

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

int main(int argc, const char *argv[]){

    AVFormatContext *pFormatContext = avformat_alloc_context();
    avformat_open_input(&pFormatContext, argv[1], NULL, NULL);
    printf("Format %s, duration %lld us\n", pFormatContext->iformat->long_name, pFormatContext->duration);

    return 0;
};
