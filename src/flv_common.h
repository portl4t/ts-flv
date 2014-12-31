
#ifndef _FLV_COMMON_H
#define _FLV_COMMON_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <ts/ts.h>
#include <ts/experimental.h>
#include <ts/remap.h>

#include "flv_tag.h"


#define REMOVE_HEADER(bufp, hdr_loc, name, len) \
    do { \
        TSMLoc the_loc; \
        the_loc = TSMimeHdrFieldFind(bufp, hdr_loc, name, len); \
        if (the_loc != TS_NULL_MLOC) { \
            TSMimeHdrFieldDestroy(bufp, hdr_loc, the_loc); \
            TSHandleMLocRelease(bufp, hdr_loc, the_loc); \
        } \
    } while (0)


class IOHandle
{
public:
    IOHandle(): vio(NULL), buffer(NULL), reader(NULL)
    {
    }

    ~IOHandle()
    {
        if (reader) {
            TSIOBufferReaderFree(reader);
            reader = NULL;
        }

        if (buffer) {
            TSIOBufferDestroy(buffer);
            buffer = NULL;
        }
    }

public:
    TSVIO               vio;
    TSIOBuffer          buffer;
    TSIOBufferReader    reader;
};


class FlvTransformContext
{
public:
    FlvTransformContext(float s, int64_t n): total(0), parse_over(false)
    {
        res_buffer = TSIOBufferCreate();
        res_reader = TSIOBufferReaderAlloc(res_buffer);

        ftag.start = s;
        ftag.cl = n;
    }

    ~FlvTransformContext()
    {
        if (res_reader) {
            TSIOBufferReaderFree(res_reader);
        }

        if (res_buffer) {
            TSIOBufferDestroy(res_buffer);
        }
    }

public:
    IOHandle            output;
    TSIOBuffer          res_buffer;
    TSIOBufferReader    res_reader;
    FlvTag              ftag;

    int64_t             total;
    bool                parse_over;
};


class FlvContext
{
public:
    FlvContext(float s): start(s), cl(0), transform_added(false), ftc(NULL)
    {
    }

    ~FlvContext()
    {
        if (ftc) {
            delete ftc;
            ftc = NULL;
        }
    }

public:
    float       start;
    int64_t     cl;
    bool        transform_added;

    FlvTransformContext *ftc;
};

#endif

