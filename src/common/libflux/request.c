/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include "request.h"
#include "message.h"
#include "info.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"

int flux_request_decode (const flux_msg_t *msg, const char **topic,
                         const char **json_str)
{
    int type;
    const char *ts, *js;
    int rc = -1;

    if (msg == NULL) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    if (type != FLUX_MSGTYPE_REQUEST) {
        errno = EPROTO;
        goto done;
    }
    if (flux_msg_get_topic (msg, &ts) < 0)
        goto done;
    if (flux_msg_get_payload_json (msg, &js) < 0)
        goto done;
    if ((json_str && !js) || (!json_str && js)) {
        errno = EPROTO;
        goto done;
    }
    if (topic)
        *topic = ts;
    if (json_str)
        *json_str = js;
    rc = 0;
done:
    return rc;
}

flux_msg_t *flux_request_encode (const char *topic, const char *json_str)
{
    flux_msg_t *msg = NULL;

    if (!topic) {
        errno = EINVAL;
        goto error;
    }
    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto error;
    if (flux_msg_set_topic (msg, topic) < 0)
        goto error;
    if (flux_msg_enable_route (msg) < 0)
        goto error;
    if (json_str && flux_msg_set_payload_json (msg, json_str) < 0)
        goto error;
    return msg;
error:
    if (msg) {
        int saved_errno = errno;
        flux_msg_destroy (msg);
        errno = saved_errno;
    }
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
