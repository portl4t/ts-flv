
#include "flv_common.h"


static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length);


void
FlvTransformContext::init()
{
    this->current_handler = &FlvTransformContext::process_header;
}

int
FlvTransformContext::process_tag()
{
    return (this->*current_handler)();
}

int
FlvTransformContext::process_header()
{
    int64_t     avail;
    char        buf[13];

    avail = TSIOBufferReaderAvail(res_reader);
    if (avail < 13)
        return 0;

    IOBufferReaderCopy(res_reader, buf, 13);
    if (buf[0] != 'F' || buf[1] != 'L' || buf[2] != 'V')
        return -1;

    if (*(uint32_t*)(buf + 9) != 0)
        return -1;

    TSIOBufferCopy(head_buffer, res_reader, 13, 0);
    TSIOBufferReaderConsume(res_reader, 13);

    pos += 13;

    this->current_handler = &FlvTransformContext::process_initial_body;
    return process_initial_body();
}

int
FlvTransformContext::process_initial_body()
{
    int64_t     avail, sz;
    uint32_t    n, ts;
    char        buf[12];

    avail = TSIOBufferReaderAvail(res_reader);

    do {
        if (avail < 11 + 1)     // tag head + 1 byte
            return 0;

        IOBufferReaderCopy(res_reader, buf, 12);

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

        TSIOBufferCopy(head_buffer, res_reader, sz, 0);
        TSIOBufferReaderConsume(res_reader, sz);
        avail -= sz;

        pos += sz;

    } while (avail > 0);

    return 0;

end:

    this->current_handler = &FlvTransformContext::process_medial_body;
    return process_medial_body();
}

int
FlvTransformContext::process_medial_body()
{
    int64_t     avail, sz;
    uint32_t    n, ts;
    char        buf[12];

    avail = TSIOBufferReaderAvail(res_reader);

    do {
        if (avail < 11 + 1)     // tag head + 1 byte
            return 0;

        IOBufferReaderCopy(res_reader, buf, 12);

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

            if (ts >= 1000 * start)
                return 1;
        }

        TSIOBufferReaderConsume(res_reader, sz);
        avail -= sz;

        pos += sz;

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

