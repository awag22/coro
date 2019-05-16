/* Original from https://github.com/tylertreat, see license */
/* modified by A. Wagner for uthreads, uthreads M.Feeley */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <time.h>
#include <sys/time.h>

#include "chan.h"
#define EPIPE -1
#define ENOMEM -1
int errno;

static int buffered_chan_init(chan_t* chan, size_t capacity);
static int buffered_chan_send(chan_t* chan, void* data);
static int buffered_chan_recv(chan_t* chan, void** data);

static int unbuffered_chan_init(chan_t* chan);
static int unbuffered_chan_send(chan_t* chan, void* data);
static int unbuffered_chan_recv(chan_t* chan, void** data);

static int chan_can_recv(chan_t* chan);
static int chan_can_send(chan_t* chan);
static int chan_is_buffered(chan_t* chan);

// Allocates and returns a new channel. The capacity specifies whether the
// channel should be buffered or not. A capacity of 0 will create an unbuffered
// channel. Sets errno and returns NULL if initialization failed.
chan_t* chan_init(size_t capacity)
{
    chan_t* chan = (chan_t*) malloc(sizeof(chan_t));
    if (!chan)
    {
        errno = ENOMEM;
        return NULL;
    }

    if (capacity > 0)
    {
        if (buffered_chan_init(chan, capacity) != 0)
        {
            free(chan);
            return NULL;
        }
    }
    else
    {
        if (unbuffered_chan_init(chan) != 0)
        {
        printf("bad result\n");
            free(chan);
            return NULL;
        }
    }
    
    return chan;
}

static int buffered_chan_init(chan_t* chan, size_t capacity)
{
    queue_t* queue = queue_init(capacity);
    if (!queue)
    {
        return -1;
    }

    if (unbuffered_chan_init(chan) != 0)
    {
        queue_dispose(queue);
        return -1;
    }
    
    chan->queue = queue;
    return 0;
}

static int unbuffered_chan_init(chan_t* chan)
{
    chan->w_mu = uthread_mutex_create();
    if (chan->w_mu == 0)
    {
        return -1;
    }

    chan->r_mu = uthread_mutex_create();
    if (chan->r_mu == 0)
    {
        uthread_mutex_destroy(chan->w_mu);
        return -1;
    }

    chan->m_mu = uthread_mutex_create();
    if ( chan->m_mu == 0)
    {
        uthread_mutex_destroy(chan->w_mu);
        uthread_mutex_destroy(chan->r_mu);
        return -1;
    }

    chan->r_cond = uthread_cond_create(chan->m_mu);
    if (chan->r_cond == 0)
    {
        uthread_mutex_destroy(chan->m_mu);
        uthread_mutex_destroy(chan->w_mu);
        uthread_mutex_destroy(chan->r_mu);
        return -1;
    }

    chan->w_cond = uthread_cond_create(chan->m_mu);
    if ( chan->w_cond == 0)
    {
        uthread_mutex_destroy(chan->m_mu);
        uthread_mutex_destroy(chan->w_mu);
        uthread_mutex_destroy(chan->r_mu);
        uthread_cond_destroy(chan->r_cond);
        return -1;
    }

    chan->closed = 0;
    chan->r_waiting = 0;
    chan->w_waiting = 0;
    chan->queue = NULL;
    chan->data = NULL;
    return 0;
}

// Releases the channel resources.
void chan_dispose(chan_t* chan)
{
    if (chan_is_buffered(chan))
    {
        queue_dispose(chan->queue);
    }

    uthread_mutex_destroy(chan->w_mu);
    uthread_mutex_destroy(chan->r_mu);

    uthread_mutex_destroy(chan->m_mu);
    uthread_cond_destroy(chan->r_cond);
    uthread_cond_destroy(chan->w_cond);
    free(chan);
}

// Once a channel is closed, data cannot be sent into it. If the channel is
// buffered, data can be read from it until it is empty, after which reads will
// return an error code. Reading from a closed channel that is unbuffered will
// return an error code. Closing a channel does not release its resources. This
// must be done with a call to chan_dispose. Returns 0 if the channel was
// successfully closed, -1 otherwise. If -1 is returned, errno will be set.
int chan_close(chan_t* chan)
{
    int success = 0;
    uthread_mutex_lock(chan->m_mu);
    if (chan->closed)
    {
        // Channel already closed.
        success = -1;
        errno = EPIPE;
    }
    else
    {
        // Otherwise close it.
        chan->closed = 1;
        uthread_cond_broadcast(chan->r_cond);
        uthread_cond_broadcast(chan->w_cond);
    }
    uthread_mutex_unlock(chan->m_mu);
    return success;
}

// Returns 0 if the channel is open and 1 if it is closed.
int chan_is_closed(chan_t* chan)
{
    uthread_mutex_lock(chan->m_mu);
    int closed = chan->closed;
    uthread_mutex_unlock(chan->m_mu);
    return closed;
}

// Sends a value into the channel. If the channel is unbuffered, this will
// block until a receiver receives the value. If the channel is buffered and at
// capacity, this will block until a receiver receives a value. Returns 0 if
// the send succeeded or -1 if it failed. If -1 is returned, errno will be set.
int chan_send(chan_t* chan, void* data)
{
    if (chan_is_closed(chan))
    {
        // Cannot send on closed channel.
        errno = EPIPE;
        return -1;
    }

    return chan_is_buffered(chan) ?
        buffered_chan_send(chan, data) :
        unbuffered_chan_send(chan, data);
}

// Receives a value from the channel. This will block until there is data to
// receive. Returns 0 if the receive succeeded or -1 if it failed. If -1 is
// returned, errno will be set.
int chan_recv(chan_t* chan, void** data)
{
    return chan_is_buffered(chan) ?
        buffered_chan_recv(chan, data) :
        unbuffered_chan_recv(chan, data);
}

static int buffered_chan_send(chan_t* chan, void* data)
{
    uthread_mutex_lock(chan->m_mu);
    while (chan->queue->size == chan->queue->capacity)
    {
        // Block until something is removed.
        chan->w_waiting++;
        uthread_cond_wait(chan->w_cond);
        chan->w_waiting--;
    }

    int success = queue_add(chan->queue, data);

    if (chan->r_waiting > 0)
    {
        // Signal waiting reader.
        uthread_cond_signal(chan->r_cond);
    }

    uthread_mutex_unlock(chan->m_mu);
    return success;
}

static int buffered_chan_recv(chan_t* chan, void** data)
{
    uthread_mutex_lock(chan->m_mu);
    while (chan->queue->size == 0)
    {
        if (chan->closed)
        {
            uthread_mutex_unlock(chan->m_mu);
            errno = EPIPE;
            return -1;
        }

        // Block until something is added.
        chan->r_waiting++;
        uthread_cond_wait(chan->r_cond);
        chan->r_waiting--;
    }

    void* msg = queue_remove(chan->queue);
    if (data)
    {
        *data = msg;
    }

    if (chan->w_waiting > 0)
    {
        // Signal waiting writer.
        uthread_cond_signal(chan->w_cond);
    }

    uthread_mutex_unlock(chan->m_mu);
    return 0;
}

static int unbuffered_chan_send(chan_t* chan, void* data)
{
    uthread_mutex_lock(chan->w_mu);
    uthread_mutex_lock(chan->m_mu);

    if (chan->closed)
    {
        uthread_mutex_unlock(chan->m_mu);
        uthread_mutex_unlock(chan->w_mu);
        errno = EPIPE;
        return -1;
    }

    chan->data = data;
    chan->w_waiting++;

    if (chan->r_waiting > 0)
    {
        // Signal waiting reader.
        uthread_cond_signal(chan->r_cond);
    }

    // Block until reader consumed chan->data.
    uthread_cond_wait(chan->w_cond);

    uthread_mutex_unlock(chan->m_mu);
    uthread_mutex_unlock(chan->w_mu);
    return 0;
}

static int unbuffered_chan_recv(chan_t* chan, void** data)
{
    uthread_mutex_lock(chan->r_mu);
    uthread_mutex_lock(chan->m_mu);

    while (!chan->closed && !chan->w_waiting)
    {
        // Block until writer has set chan->data.
        chan->r_waiting++;
        uthread_cond_wait(chan->r_cond);
        chan->r_waiting--;
    }

    if (chan->closed)
    {
        uthread_mutex_unlock(chan->m_mu);
        uthread_mutex_unlock(chan->r_mu);
        errno = EPIPE;
        return -1;
    }

    if (data)
    {
        *data = chan->data;
    }
    chan->w_waiting--;

    // Signal waiting writer.
    uthread_cond_signal(chan->w_cond);

    uthread_mutex_unlock(chan->m_mu);
    uthread_mutex_unlock(chan->r_mu);
    return 0;
}

// Returns the number of items in the channel buffer. If the channel is
// unbuffered, this will return 0.
int chan_size(chan_t* chan)
{
    int size = 0;
    if (chan_is_buffered(chan))
    {
        uthread_mutex_lock(chan->m_mu);
        size = chan->queue->size;
        uthread_mutex_unlock(chan->m_mu);
    }
    return size;
}

typedef struct
{
    int     recv;    // 1 when it is recv, 0 when it is read
    chan_t* chan;    // selected channel
    void*   msg_in;  // message to send
    int     index;   // array index
} select_op_t;

// A select statement chooses which of a set of possible send or receive
// operations will proceed. The return value indicates which channel's
// operation has proceeded. If more than one operation can proceed, one is
// selected randomly. If none can proceed, -1 is returned. Select is intended
// to be used in conjunction with a switch statement. In the case of a receive
// operation, the received value will be pointed to by the provided pointer. In
// the case of a send, the value at the same index as the channel will be sent.
int chan_select(chan_t* recv_chans[], int recv_count, void** recv_out,
    chan_t* send_chans[], int send_count, void* send_msgs[])
{
    // TODO: Add support for blocking selects.

    select_op_t candidates[recv_count + send_count];
    int count = 0;
    int i;

    // Determine receive candidates.
    for (i = 0; i < recv_count; i++)
    {
        chan_t* chan = recv_chans[i];
        if (chan_can_recv(chan))
        {
            select_op_t op;
            op.recv = 1;
            op.chan = chan;
            op.index = i;
            candidates[count++] = op;
        }
    }

    // Determine send candidates.
    for (i = 0; i < send_count; i++)
    {
        chan_t* chan = send_chans[i];
        if (chan_can_send(chan))
        {
            select_op_t op;
            op.recv = 0;
            op.chan = chan;
            op.msg_in = send_msgs[i];
            op.index = i + recv_count;
            candidates[count++] = op;
        }
    }
    
    if (count == 0) return -1;


    // Seed rand using 3.
    srand(3);

    // Select candidate and perform operation.
    select_op_t select = candidates[rand() % count];
    if (select.recv && chan_recv(select.chan, recv_out) != 0)
    {
        return -1;
    }
    else if (!select.recv && chan_send(select.chan, select.msg_in) != 0)
    {
        return -1;
    }
    return select.index;
}

static int chan_can_recv(chan_t* chan)
{
    if (chan_is_buffered(chan))
    {
        return chan_size(chan) > 0;
    }

    uthread_mutex_lock(chan->m_mu);
    int sender = chan->w_waiting > 0;
    uthread_mutex_unlock(chan->m_mu);
    return sender;
}

static int chan_can_send(chan_t* chan)
{
    int send;
    if (chan_is_buffered(chan))
    {
        // Can send if buffered channel is not full.
        uthread_mutex_lock(chan->m_mu);
        send = chan->queue->size < chan->queue->capacity;
        uthread_mutex_unlock(chan->m_mu);
    }
    else
    {
        // Can send if unbuffered channel has receiver.
        uthread_mutex_lock(chan->m_mu);
        send = chan->r_waiting > 0;
        uthread_mutex_unlock(chan->m_mu);
    }

    return send;
}

static int chan_is_buffered(chan_t* chan)
{
    return chan->queue != NULL;
}

int chan_send_int32(chan_t* chan, int32_t data)
{
    int32_t* wrapped = malloc(sizeof(int32_t));
    if (!wrapped)
    {
        return -1;
    }

    *wrapped = data;

    int success = chan_send(chan, wrapped);
    if (success != 0)
    {
        free(wrapped);
    }

    return success;
}

int chan_recv_int32(chan_t* chan, int32_t* data)
{
    int32_t* wrapped = NULL;
    int success = chan_recv(chan, (void*) &wrapped);
    if (wrapped != NULL)
    {
        *data = *wrapped;
        free(wrapped);
    }

    return success;
}

int chan_send_int64(chan_t* chan, int64_t data)
{
    int64_t* wrapped = malloc(sizeof(int64_t));
    if (!wrapped)
    {
        return -1;
    }

    *wrapped = data;

    int success = chan_send(chan, wrapped);
    if (success != 0)
    {
        free(wrapped);
    }

    return success;
}

int chan_recv_int64(chan_t* chan, int64_t* data)
{
    int64_t* wrapped = NULL;
    int success = chan_recv(chan, (void*) &wrapped);
    if (wrapped != NULL)
    {
        *data = *wrapped;
        free(wrapped);
    }

    return success;
}

int chan_send_double(chan_t* chan, double data)
{
    double* wrapped = malloc(sizeof(double));
    if (!wrapped)
    {
        return -1;
    }

    *wrapped = data;

    int success = chan_send(chan, wrapped);
    if (success != 0)
    {
        free(wrapped);
    }

    return success;
}

int chan_recv_double(chan_t* chan, double* data)
{
    double* wrapped = NULL;
    int success = chan_recv(chan, (void*) &wrapped);
    if (wrapped != NULL)
    {
        *data = *wrapped;
        free(wrapped);
    }

    return success;
}

int chan_send_buf(chan_t* chan, void* data, size_t size)
{
    void* wrapped = malloc(size);
    if (!wrapped)
    {
        return -1;
    }

    memcpy(wrapped, data, size);

    int success = chan_send(chan, wrapped);
    if (success != 0)
    {
        free(wrapped);
    }

    return success;
}

int chan_recv_buf(chan_t* chan, void* data, size_t size)
{
    void* wrapped = NULL;
    int success = chan_recv(chan, (void*) &wrapped);
    if (wrapped != NULL)
    {
        memcpy(data, wrapped, size);
        free(wrapped);
    }

    return success;
}

// returns all of the channels that can proceed with a communication
int chan_alt(chan_t* recv_chans[], int recv_count, int canrecv[])
{
    int i;
    int count = 0;
    // Determine receive candidates.
    for (i = 0; i < recv_count; i++)
    {
        chan_t* chan = recv_chans[i];
        if (chan_can_recv(chan))
        {
            canrecv[i] = 1;
            count++;
        } else canrecv[i] = 0;
    }
    return count;
}


