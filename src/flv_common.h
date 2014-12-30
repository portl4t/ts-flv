
#ifndef _FLV_COMMON_H
#define _FLV_COMMON_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <ts/ts.h>
#include <ts/experimental.h>
#include <ts/remap.h>


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


class FlvTransformContext;
typedef int (FlvTransformContext::*FTCHandler) ();

class FlvTransformContext
{
public:
    FlvTransformContext(float s, int64_t n): current_handler(NULL), total(0), pos(0),
        cl(n), content_length(0), start(s), parse_over(false), key_found(false)
    {
        res_buffer = TSIOBufferCreate();
        res_reader = TSIOBufferReaderAlloc(res_buffer);

        head_buffer = TSIOBufferCreate();
        head_reader = TSIOBufferReaderAlloc(head_buffer);
    }

    ~FlvTransformContext()
    {
        if (res_reader) {
            TSIOBufferReaderFree(res_reader);
        }

        if (res_buffer) {
            TSIOBufferDestroy(res_buffer);
        }

        if (head_reader) {
            TSIOBufferReaderFree(head_reader);
        }

        if (head_buffer) {
            TSIOBufferDestroy(head_buffer);
        }
    }

    void init();

    int process_tag();

    int process_header();
    int process_initial_body();
    int process_medial_body();

public:
    IOHandle            output;
    TSIOBuffer          res_buffer;
    TSIOBufferReader    res_reader;
    TSIOBuffer          head_buffer;
    TSIOBufferReader    head_reader;

    FTCHandler          current_handler;
    int64_t             total;
    int64_t             pos;
    int64_t             cl;
    int64_t             content_length;

    float               start;

    bool                parse_over;
    bool                key_found;
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

