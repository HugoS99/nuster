/*
 * nuster key functions.
 *
 * Copyright (C) Jiang Wenyuan, < koubunen AT gmail DOT com >
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <types/global.h>
#include <types/http_htx.h>

#include <proto/stream_interface.h>
#include <proto/proxy.h>
#include <proto/log.h>
#include <proto/acl.h>
#include <proto/http_htx.h>

#include <nuster/nuster.h>

int nst_key_build(struct stream *s, struct http_msg *msg, struct nst_rule *rule,
        struct nst_http_txn *txn, struct nst_key *key, enum http_meth_t method) {

    struct nst_key_element *ck = NULL;
    struct nst_key_element **pck = rule->key->data;

    struct buffer *buf = nst_key_init();

    nst_debug(s, "Calculate key: ");

    while((ck = *pck++)) {
        int ret = NST_ERR;

        switch(ck->type) {
            case NST_KEY_ELEMENT_METHOD:
                nst_debug2("method.");

                ret = nst_key_catist(buf, http_known_methods[method]);

                break;
            case NST_KEY_ELEMENT_SCHEME:
                nst_debug2("scheme.");

                {
                    struct ist scheme = txn->req.scheme == SCH_HTTPS ? ist("HTTPS") : ist("HTTP");
                    ret = nst_key_catist(buf, scheme);
                }

                break;
            case NST_KEY_ELEMENT_HOST:
                nst_debug2("host.");

                if(txn->req.host.len) {
                    ret = nst_key_catist(buf, txn->req.host);
                } else {
                    ret = nst_key_catdel(buf);
                }

                break;
            case NST_KEY_ELEMENT_URI:
                nst_debug2("uri.");

                if(txn->req.uri.len) {
                    ret = nst_key_catist(buf, txn->req.uri);
                } else {
                    ret = nst_key_catdel(buf);
                }

                break;
            case NST_KEY_ELEMENT_PATH:
                nst_debug2("path.");

                if(txn->req.path.len) {
                    ret = nst_key_catist(buf, txn->req.path);
                } else {
                    ret = nst_key_catdel(buf);
                }

                break;
            case NST_KEY_ELEMENT_DELIMITER:
                nst_debug2("delimiter.");

                if(txn->req.delimiter) {
                    ret = nst_key_catist(buf, ist("?"));
                } else {
                    ret = nst_key_catdel(buf);
                }

                break;
            case NST_KEY_ELEMENT_QUERY:
                nst_debug2("query.");

                if(txn->req.query.len) {
                    ret = nst_key_catist(buf, txn->req.query);
                } else {
                    ret = nst_key_catdel(buf);
                }

                break;
            case NST_KEY_ELEMENT_PARAM:
                nst_debug2("param_%s.", ck->data);

                if(txn->req.query.len) {
                    char *v = NULL;
                    int v_l = 0;

                    if(nst_http_find_param(txn->req.query.ptr,
                                txn->req.query.ptr + txn->req.query.len,
                                ck->data, &v, &v_l) == NST_OK) {

                        ret = nst_key_catist(buf, ist2(v, v_l));
                        break;
                    }
                }

                ret = nst_key_catdel(buf);
                break;
            case NST_KEY_ELEMENT_HEADER:
                {
                    struct htx *htx = htxbuf(&s->req.buf);
                    struct http_hdr_ctx hdr = { .blk = NULL };
                    struct ist h = {
                        .ptr = ck->data,
                        .len = strlen(ck->data),
                    };

                    nst_debug2("header_%s.", ck->data);

                    while(http_find_header(htx, h, &hdr, 0)) {
                        ret = nst_key_catist(buf, hdr.value);

                        if(ret == NST_ERR) {
                            break;
                        }
                    }
                }

                ret = nst_key_catdel(buf);
                break;
            case NST_KEY_ELEMENT_COOKIE:
                nst_debug2("cookie_%s.", ck->data);

                if(txn->req.cookie.len) {
                    char *v = NULL;
                    size_t v_l = 0;

                    if(http_extract_cookie_value(txn->req.cookie.ptr,
                                txn->req.cookie.ptr + txn->req.cookie.len,
                                ck->data, strlen(ck->data), 1, &v, &v_l)) {

                        ret = nst_key_catist(buf, ist2(v, v_l));
                        break;
                    }

                }

                ret = nst_key_catdel(buf);
                break;
            case NST_KEY_ELEMENT_BODY:
                nst_debug2("body.");

                if(s->txn->meth == HTTP_METH_POST || s->txn->meth == HTTP_METH_PUT) {

                    int pos;
                    struct htx *htx = htxbuf(&msg->chn->buf);

                    for(pos = htx_get_first(htx); pos != -1; pos = htx_get_next(htx, pos)) {
                        struct htx_blk *blk = htx_get_blk(htx, pos);
                        uint32_t        sz  = htx_get_blksz(blk);
                        enum htx_blk_type type = htx_get_blk_type(blk);

                        if(type != HTX_BLK_DATA) {
                            continue;
                        }

                        ret = nst_key_cat(buf, htx_get_blk_ptr(htx, blk), sz);

                        if(ret != NST_OK) {
                            break;
                        }
                    }
                }

                ret = nst_key_catdel(buf);
                break;
            default:
                ret = NST_ERR;
                break;
        }

        if(ret != NST_OK) {
            return NST_ERR;
        }
    }

    nst_debug2("\n");

    key->size = buf->data;
    key->data = malloc(key->size);

    if(!key->data) {
        return NST_ERR;
    }

    memcpy(key->data, buf->area, buf->data);

    return NST_OK;
}

void nst_key_debug(struct nst_key *key) {

    if((global.mode & MODE_DEBUG)) {
        int i;

        for(i = 0; i < key->size; i++) {
            char c = key->data[i];

            if(c != 0) {
                fprintf(stderr, "%c", c);
            }
        }

        fprintf(stderr, "\n");
    }
}
