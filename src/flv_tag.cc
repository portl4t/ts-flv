
#include "flv_tag.h"


static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length);


int
FlvTag::process_tag(TSIOBufferReader readerp, bool complete)
{
    int64_t     avail, head_avail;
    int         rc;

    avail = TSIOBufferReaderAvail(readerp);
    TSIOBufferCopy(tag_buffer, readerp, avail, 0);

    TSIOBufferReaderConsume(readerp, avail);

    rc = (this->*current_handler)();

    if (rc == 0 && complete) {
        rc = -1;
    }

    if (rc) {       // success or error.
        head_avail = TSIOBufferReaderAvail(head_reader);
        content_length = head_avail + cl - dup_pos;
    }

    return rc;
}

int64_t
FlvTag::write_out(TSIOBuffer buffer)
{
    int64_t     dup_avail, head_avail;

    head_avail = TSIOBufferReaderAvail(head_reader);
    dup_avail = TSIOBufferReaderAvail(dup_reader);

    if (head_avail > 0) {
        TSIOBufferCopy(buffer, head_reader, head_avail, 0);
        TSIOBufferReaderConsume(head_reader, head_avail);
    }

    if (dup_avail > 0) {
        TSIOBufferCopy(buffer, dup_reader, dup_avail, 0);
        TSIOBufferReaderConsume(dup_reader, dup_avail);
    }

    return head_avail + dup_avail;
}

int
FlvTag::process_header()
{
    int64_t     avail;
    char        buf[13];

    avail = TSIOBufferReaderAvail(tag_reader);
    if (avail < 13)
        return 0;

    IOBufferReaderCopy(tag_reader, buf, 13);
    if (buf[0] != 'F' || buf[1] != 'L' || buf[2] != 'V')
        return -1;

    if (*(uint32_t*)(buf + 9) != 0)
        return -1;

    TSIOBufferCopy(head_buffer, tag_reader, 13, 0);
    TSIOBufferReaderConsume(tag_reader, 13);

    tag_pos += 13;

    this->current_handler = &FlvTag::process_initial_body;
    return process_initial_body();
}

int
FlvTag::process_initial_body()
{
    int64_t     avail, sz;
    uint32_t    n, ts;
    char        buf[12];

    avail = TSIOBufferReaderAvail(tag_reader);

    do {
        if (avail < 11 + 1)     // tag head + 1 byte
            return 0;

        IOBufferReaderCopy(tag_reader, buf, 12);

        n = (uint32_t)((uint8_t)buf[1] << 16) +
            (uint32_t)((uint8_t)buf[2] << 8) +
            (uint32_t)((uint8_t)buf[3]);

        sz = 11 + n + 4;

        if (avail < sz)     // insure the whole tag
            return 0;

        ts = (uint32_t)((uint8_t)buf[4] << 16) +
             (uint32_t)((uint8_t)buf[5] << 8) +
             (uint32_t)((uint8_t)buf[6]);

        if (ts != 0)
            goto end;

        if (buf[0] == 9 && (((uint8_t)buf[11]) >> 4) == 1) {
            if (!key_found) {
                key_found = true;

            } else {
                goto end;
            }
        }

        TSIOBufferCopy(head_buffer, tag_reader, sz, 0);
        TSIOBufferReaderConsume(tag_reader, sz);
        avail -= sz;

        tag_pos += sz;

    } while (avail > 0);

    return 0;

end:

    TSIOBufferReaderConsume(dup_reader, tag_pos);
    dup_pos = tag_pos;

    key_found = false;
    this->current_handler = &FlvTag::process_medial_body;
    return process_medial_body();
}

int
FlvTag::process_medial_body()
{
    int64_t     avail, sz, pass;
    uint32_t    n, ts;
    char        buf[12];

    avail = TSIOBufferReaderAvail(tag_reader);

    do {
        if (avail < 11 + 1)     // tag head + 1 byte
            return 0;

        IOBufferReaderCopy(tag_reader, buf, 12);

        n = (uint32_t)((uint8_t)buf[1] << 16) +
            (uint32_t)((uint8_t)buf[2] << 8) +
            (uint32_t)((uint8_t)buf[3]);

        sz = 11 + n + 4;

        if (avail < sz)     // insure the whole tag
            return 0;

        if (buf[0] == 9 && (((uint8_t)buf[11]) >> 4) == 1) {    // key frame

            ts = (uint32_t)((uint8_t)buf[4] << 16) +
                 (uint32_t)((uint8_t)buf[5] << 8) +
                 (uint32_t)((uint8_t)buf[6]);

            if (ts <= (uint32_t)(1000 * start)) {
                pass = tag_pos - dup_pos;
                if (pass > 0) {
                    TSIOBufferReaderConsume(dup_reader, pass);
                    dup_pos = tag_pos;
                }

                key_found = true;

            } else {

                if (!key_found) {
                    pass = tag_pos - dup_pos;
                    if (pass > 0) {
                        TSIOBufferReaderConsume(dup_reader, pass);
                        dup_pos = tag_pos;
                    }
                }

                return 1;
            }
        }

        TSIOBufferReaderConsume(tag_reader, sz);
        avail -= sz;

        tag_pos += sz;

    } while (avail > 0);

    return 0;
}

static int64_t
IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length)
{
    int64_t             avail, need, n;
    const char          *start;
    TSIOBufferBlock     blk;

    n = 0;
    blk = TSIOBufferReaderStart(readerp);

    while (blk) {
        start = TSIOBufferBlockReadStart(blk, readerp, &avail);
        need = length < avail ? length : avail;

        if (need > 0) {
            memcpy((char*)buf + n, start, need);
            length -= need;
            n += need;
        }

        if (length == 0)
            break;

        blk = TSIOBufferBlockNext(blk);
    }

    return n;
}

