
#include "flv_common.h"


char * ts_arg(const char *param, size_t param_len, const char *key, size_t key_len, size_t *val_len);
static int flv_handler(TSCont contp, TSEvent event, void *edata);
static void flv_cache_lookup_complete(FlvContext *fc, TSHttpTxn txnp);
static void flv_read_response(FlvContext *fc, TSHttpTxn txnp);
static void flv_add_transform(FlvContext *fc, TSHttpTxn txnp);
static int flv_transform_entry(TSCont contp, TSEvent event, void *edata);
static int flv_transform_handler(TSCont contp, FlvContext *fc);
static int flv_parse_tag(FlvTransformContext *ftc, bool body_complete);


TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
    if (!api_info)
        return TS_ERROR;

    if (api_info->size < sizeof(TSRemapInterface))
        return TS_ERROR;

    return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char* argv[], void** ih, char* errbuf, int errbuf_size)
{
    *ih = NULL;
    return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void* ih)
{
    return;
}

TSRemapStatus
TSRemapDoRemap(void* ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
    const char          *method, *query;
    int                 method_len, query_len;
    size_t              val_len;
    const char          *val;
    int                 ret;
    float               start;
    char                buf[1024];
    int                 buf_len;
    int                 left, right;
    TSCont              contp;
    FlvContext          *fc;

    method = TSHttpHdrMethodGet(rri->requestBufp, rri->requestHdrp, &method_len);
    if (method != TS_HTTP_METHOD_GET) {
        return TSREMAP_NO_REMAP;
    }

    start = 0;
    query = TSUrlHttpQueryGet(rri->requestBufp, rri->requestUrl, &query_len);

    val = ts_arg(query, query_len, "start", sizeof("start")-1, &val_len);
    if (val != NULL) {
        ret = sscanf(val, "%f", &start);
        if (ret != 1)
            start = 0;
    }

    if (start == 0) {
        return TSREMAP_NO_REMAP;

    } else if (start < 0) {
        TSHttpTxnSetHttpRetStatus(rh, TS_HTTP_STATUS_BAD_REQUEST);
        TSHttpTxnErrorBodySet(rh, TSstrdup("Invalid request."), sizeof("Invalid request.")-1, NULL);
    }

    // reset args
    left = val - sizeof("start") - query;
    right = query + query_len - val - val_len;

    if (left > 0) {
        left--;
    }

    if (left == 0 && right > 0) {
        right--;
    }

    buf_len = sprintf(buf, "%.*s%.*s", left, query, right, query+query_len-right);
    TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, buf, buf_len);

    // remove Accept-Encoding
    REMOVE_HEADER(rri->requestBufp, rri->requestHdrp,
            TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);

    // remove Range
    REMOVE_HEADER(rri->requestBufp, rri->requestHdrp,
                  TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);

    fc = new FlvContext(start);
    contp = TSContCreate(flv_handler, NULL);
    TSContDataSet(contp, fc);

    TSHttpTxnHookAdd(rh, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
    TSHttpTxnHookAdd(rh, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    TSHttpTxnHookAdd(rh, TS_HTTP_TXN_CLOSE_HOOK, contp);
    return TSREMAP_NO_REMAP;
}

static int
flv_handler(TSCont contp, TSEvent event, void *edata)
{
    TSHttpTxn       txnp;
    FlvContext      *fc;

    txnp = (TSHttpTxn)edata;
    fc = (FlvContext*)TSContDataGet(contp);

    switch (event) {

        case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
            flv_cache_lookup_complete(fc, txnp);
            break;

        case TS_EVENT_HTTP_READ_RESPONSE_HDR:
            flv_read_response(fc, txnp);
            break;

        case TS_EVENT_HTTP_TXN_CLOSE:
            delete fc;
            TSContDestroy(contp);
            break;

        default:
            break;
    }

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
}

static void
flv_cache_lookup_complete(FlvContext *fc, TSHttpTxn txnp)
{
    TSMBuffer       bufp;
    TSMLoc          hdrp;
    TSMLoc          cl_field;
    TSHttpStatus    code;
    int             obj_status;
    int64_t         n;

    if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
        TSError("[%s] Couldn't get cache status of object", __FUNCTION__);
        return;
    }

    if (obj_status != TS_CACHE_LOOKUP_HIT_STALE && obj_status != TS_CACHE_LOOKUP_HIT_FRESH)
        return;

    if (TSHttpTxnCachedRespGet(txnp, &bufp, &hdrp) != TS_SUCCESS) {
        TSError("[%s] Couldn't get cache resp", __FUNCTION__);
        return;
    }

    code = TSHttpHdrStatusGet(bufp, hdrp);
    if (code != TS_HTTP_STATUS_OK) {
        goto release;
    }

    n = 0;

    cl_field = TSMimeHdrFieldFind(bufp, hdrp, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
    if (cl_field) {
        n = TSMimeHdrFieldValueInt64Get(bufp, hdrp, cl_field, -1);
        TSHandleMLocRelease(bufp, hdrp, cl_field);
    }

    if (n <= 0)
        goto release;

    fc->cl = n;
    flv_add_transform(fc, txnp);

release:

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);
}

static void
flv_read_response(FlvContext *fc, TSHttpTxn txnp)
{
    TSMBuffer       bufp;
    TSMLoc          hdrp;
    TSMLoc          cl_field;
    TSHttpStatus    status;
    int64_t         n;

    if (TSHttpTxnServerRespGet(txnp, &bufp, &hdrp) != TS_SUCCESS) {
        TSError("[%s] could not get request os data", __FUNCTION__);
        return;
    }

    status = TSHttpHdrStatusGet(bufp, hdrp);
    if (status != TS_HTTP_STATUS_OK)
        goto release;

    n = 0;
    cl_field = TSMimeHdrFieldFind(bufp, hdrp, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
    if (cl_field) {
        n = TSMimeHdrFieldValueInt64Get(bufp, hdrp, cl_field, -1);
        TSHandleMLocRelease(bufp, hdrp, cl_field);
    }

    if (n <= 0)
        goto release;

    fc->cl = n;
    flv_add_transform(fc, txnp);

release:

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);
}

static void
flv_add_transform(FlvContext *fc, TSHttpTxn txnp)
{
    TSVConn             connp;
    FlvTransformContext *ftc;

    if (fc->transform_added)
        return;

    ftc = new FlvTransformContext(fc->start, fc->cl);
    ftc->init();

    TSHttpTxnUntransformedRespCache(txnp, 1);
    TSHttpTxnTransformedRespCache(txnp, 0);

    connp = TSTransformCreate(flv_transform_entry, txnp);
    TSContDataSet(connp, fc);
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);

    fc->transform_added = true;
    fc->ftc = ftc;
}

static int
flv_transform_entry(TSCont contp, TSEvent event, void *edata)
{
    TSVIO        input_vio;
    FlvContext   *fc = (FlvContext*)TSContDataGet(contp);

    if (TSVConnClosedGet(contp)) {
        TSContDestroy(contp);
        return 0;
    }

    switch (event) {

        case TS_EVENT_ERROR:
            input_vio = TSVConnWriteVIOGet(contp);
            TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
            break;

        case TS_EVENT_VCONN_WRITE_COMPLETE:
            TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
            break;

        case TS_EVENT_VCONN_WRITE_READY:
        default:
            flv_transform_handler(contp, fc);
            break;
    }

    return 0;
}

static int
flv_transform_handler(TSCont contp, FlvContext *fc)
{
    TSVConn             output_conn;
    TSVIO               input_vio;
    TSIOBufferReader    input_reader;
    int64_t             avail, toread, upstream_done, head_avail;
    int                 ret;
    bool                write_down;
    FlvTransformContext *ftc;

    ftc = fc->ftc;

    output_conn = TSTransformOutputVConnGet(contp);
    input_vio = TSVConnWriteVIOGet(contp);
    input_reader = TSVIOReaderGet(input_vio);

    if (!TSVIOBufferGet(input_vio)) {
        if (ftc->output.vio) {
            TSVIONBytesSet(ftc->output.vio, ftc->total);
            TSVIOReenable(ftc->output.vio);
        }
        return 1;
    }

    avail = TSIOBufferReaderAvail(input_reader);
    upstream_done = TSVIONDoneGet(input_vio);

    TSIOBufferCopy(ftc->res_buffer, input_reader, avail, 0);
    TSIOBufferReaderConsume(input_reader, avail);
    TSVIONDoneSet(input_vio, upstream_done + avail);

    toread = TSVIONTodoGet(input_vio);
    write_down = false;

    if (!ftc->parse_over) {
        ret = flv_parse_tag(ftc, toread <= 0);
        if (ret == 0)
            goto trans;

        ftc->parse_over = true;

        ftc->output.buffer = TSIOBufferCreate();
        ftc->output.reader = TSIOBufferReaderAlloc(ftc->output.buffer);
        ftc->output.vio = TSVConnWrite(output_conn, contp, ftc->output.reader, ftc->content_length);

        head_avail = TSIOBufferReaderAvail(ftc->head_reader);
        if (head_avail > 0) {
            TSIOBufferCopy(ftc->output.buffer, ftc->head_reader, head_avail, 0);
            ftc->total += head_avail;

            write_down = true;
        }
    }

    avail = TSIOBufferReaderAvail(ftc->res_reader);
    if (avail > 0) {
        TSIOBufferCopy(ftc->output.buffer, ftc->res_reader, avail, 0);
        TSIOBufferReaderConsume(ftc->res_reader, avail);
        ftc->total += avail;

        write_down = true;
    }

trans:

    if (write_down)
        TSVIOReenable(ftc->output.vio);

    if (toread > 0) {
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);

    } else {
        TSVIONBytesSet(ftc->output.vio, ftc->total);
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
    }

    return 1;
}

static int
flv_parse_tag(FlvTransformContext *ftc, bool body_complete)
{
    int         ret;
    int64_t     avail;

    ret = ftc->process_tag();

    if (ret == 0 && body_complete)
        ret = -1;

    if (ret) {      // success or error.
        avail = TSIOBufferReaderAvail(ftc->head_reader);
        ftc->content_length = avail + ftc->cl - ftc->pos;
    }

    return ret;
}

char *
ts_arg(const char *param, size_t param_len, const char *key, size_t key_len, size_t *val_len)
{
    const char  *p, *last;
    const char  *val;

    *val_len = 0;

    if (!param || !param_len)
        return NULL;

    p = param;
    last = p + param_len;

    for ( ; p < last; p++) {

        p = (char*)memmem(p, last-p, key, key_len);

        if (p == NULL)
            return NULL;

        if ((p == param || *(p - 1) == '&') && *(p + key_len) == '=') {

            val = p + key_len + 1;

            p = (char*)memchr(p, '&', last-p);

            if (p == NULL)
                p = param + param_len;

            *val_len = p - val;

            return (char*)val;
        }
    }

    return NULL;
}

