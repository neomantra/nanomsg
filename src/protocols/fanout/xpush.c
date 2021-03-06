/*
    Copyright (c) 2012-2013 250bpm s.r.o.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include "xpush.h"

#include "../../nn.h"
#include "../../fanout.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/lb.h"
#include "../../utils/list.h"

struct nn_xpush_data {
    struct nn_lb_data lb;
};

struct nn_xpush {
    struct nn_sockbase sockbase;
    struct nn_lb lb;
};

/*  Private functions. */
static int nn_xpush_init (struct nn_xpush *self,
    const struct nn_sockbase_vfptr *vfptr);
static void nn_xpush_term (struct nn_xpush *self);

/*  Implementation of nn_sockbase's virtual functions. */
static int nn_xpush_ispeer (int socktype);
static void nn_xpush_destroy (struct nn_sockbase *self);
static int nn_xpush_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xpush_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xpush_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xpush_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xpush_events (struct nn_sockbase *self);
static int nn_xpush_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xpush_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int nn_xpush_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct nn_sockbase_vfptr nn_xpush_sockbase_vfptr = {
    NN_SOCKBASE_FLAG_NORECV,
    nn_xpush_ispeer,
    nn_xpush_destroy,
    nn_xpush_add,
    nn_xpush_rm,
    nn_xpush_in,
    nn_xpush_out,
    nn_xpush_events,
    nn_xpush_send,
    NULL,
    nn_xpush_setopt,
    nn_xpush_getopt
};

static int nn_xpush_ispeer (int socktype)
{
    return socktype == NN_PULL ? 1 : 0;
}

static int nn_xpush_init (struct nn_xpush *self,
    const struct nn_sockbase_vfptr *vfptr)
{
    int rc;

    rc = nn_sockbase_init (&self->sockbase, vfptr);
    if (rc < 0)
        return -rc;

    nn_lb_init (&self->lb);

    return 0;
}

static void nn_xpush_term (struct nn_xpush *self)
{
    nn_lb_term (&self->lb);
    nn_sockbase_term (&self->sockbase);
}

void nn_xpush_destroy (struct nn_sockbase *self)
{
    struct nn_xpush *xpush;

    xpush = nn_cont (self, struct nn_xpush, sockbase);

    nn_xpush_term (xpush);
    nn_free (xpush);
}

static int nn_xpush_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xpush *xpush;
    struct nn_xpush_data *data;

    xpush = nn_cont (self, struct nn_xpush, sockbase);
    data = nn_alloc (sizeof (struct nn_xpush_data), "pipe data (push)");
    alloc_assert (data);
    nn_pipe_setdata (pipe, data);
    nn_lb_add (&xpush->lb, pipe, &data->lb, self->sndprio);

    return 0;
}

static void nn_xpush_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xpush *xpush;
    struct nn_xpush_data *data;

    xpush = nn_cont (self, struct nn_xpush, sockbase);
    data = nn_pipe_getdata (pipe);
    nn_lb_rm (&xpush->lb, pipe, &data->lb);
    nn_free (data);
}

static void nn_xpush_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    /*  We are not going to receive any messages, so there's no need to store
        the list of inbound pipes. */
}

static void nn_xpush_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xpush *xpush;
    struct nn_xpush_data *data;

    xpush = nn_cont (self, struct nn_xpush, sockbase);
    data = nn_pipe_getdata (pipe);
    nn_lb_out (&xpush->lb, pipe, &data->lb);
}

static int nn_xpush_events (struct nn_sockbase *self)
{
    return nn_lb_can_send (&nn_cont (self, struct nn_xpush, sockbase)->lb) ?
        NN_SOCKBASE_EVENT_OUT : 0;
}

static int nn_xpush_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    return nn_lb_send (&nn_cont (self, struct nn_xpush, sockbase)->lb, msg);
}

static int nn_xpush_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xpush_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xpush_create (struct nn_sockbase **sockbase)
{
    int rc;
    struct nn_xpush *self;

    self = nn_alloc (sizeof (struct nn_xpush), "socket (push)");
    alloc_assert (self);
    rc = nn_xpush_init (self, &nn_xpush_sockbase_vfptr);
    if (rc < 0) {
        nn_free (self);
        return rc;
    }
    *sockbase = &self->sockbase;

    return 0;
}

static struct nn_socktype nn_xpush_socktype_struct = {
    AF_SP_RAW,
    NN_PUSH,
    nn_xpush_create,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_xpush_socktype = &nn_xpush_socktype_struct;

