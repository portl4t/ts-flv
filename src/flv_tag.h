
#ifndef _FLV_TAG_H
#define _FLV_TAG_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ts/ts.h>


class FlvTag;
typedef int (FlvTag::*FTHandler) ();

class FlvTag
{
public:
    FlvTag(): tag_buffer(NULL), tag_reader(NULL), dup_reader(NULL),
        head_buffer(NULL), head_reader(NULL), tag_pos(0), dup_pos(0), cl(0),
        content_length(0), start(0), key_found(false)
    {
        tag_buffer = TSIOBufferCreate();
        tag_reader = TSIOBufferReaderAlloc(tag_buffer);
        dup_reader = TSIOBufferReaderAlloc(tag_buffer);

        head_buffer = TSIOBufferCreate();
        head_reader = TSIOBufferReaderAlloc(head_buffer);

        current_handler = &FlvTag::process_header;
    }

    ~FlvTag()
    {
        if (tag_reader) {
            TSIOBufferReaderFree(tag_reader);
            tag_reader = NULL;
        }

        if (dup_reader) {
            TSIOBufferReaderFree(dup_reader);
            dup_reader = NULL;
        }

        if (tag_buffer) {
            TSIOBufferDestroy(tag_buffer);
            tag_buffer = NULL;
        }

        if (head_reader) {
            TSIOBufferReaderFree(head_reader);
            head_reader = NULL;
        }

        if (head_buffer) {
            TSIOBufferDestroy(head_buffer);
            head_buffer = NULL;
        }
    }

    int process_tag(TSIOBufferReader reader, bool complete);
    int64_t write_out(TSIOBuffer buffer);

    int process_header();
    int process_initial_body();
    int process_medial_body();

public:
    TSIOBuffer          tag_buffer;
    TSIOBufferReader    tag_reader;
    TSIOBufferReader    dup_reader;

    TSIOBuffer          head_buffer;
    TSIOBufferReader    head_reader;

    FTHandler           current_handler;
    int64_t             tag_pos;
    int64_t             dup_pos;
    int64_t             cl;
    int64_t             content_length;

    float               start;
    bool                key_found;
};

#endif

