// Copyright 2016 iNuron NV
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <thread>
#include <functional>
#include <system_error>

#include <youtils/Assert.h>

#include "NetworkXioClient.h"

#define POLLING_TIME_USEC   20

namespace volumedriverfs
{

template<class T>
static int
static_on_session_event(xio_session *session,
                        xio_session_event_data *event_data,
                        void *cb_user_context)
{
    T *obj = reinterpret_cast<T*>(cb_user_context);
    if (obj == NULL)
    {
        assert(obj != NULL);
        return -1;
    }
    return obj->on_session_event(session, event_data);
}

template<class T>
static int
static_on_response(xio_session *session,
                   xio_msg *req,
                   int last_in_rxq,
                   void *cb_user_context)
{
    T *obj = reinterpret_cast<T*>(cb_user_context);
    if (obj == NULL)
    {
        assert(obj != NULL);
        return -1;
    }
    return obj->on_response(session, req, last_in_rxq);
}

template<class T>
static int
static_on_msg_error(xio_session *session,
                    xio_status error,
                    xio_msg_direction direction,
                    xio_msg *msg,
                    void *cb_user_context)
{
    T *obj = reinterpret_cast<T*>(cb_user_context);
    if (obj == NULL)
    {
        assert(obj != NULL);
        return -1;
    }
    return obj->on_msg_error(session, error, direction, msg);
}

template<class T>
static int
static_assign_data_in_buf(xio_msg *msg,
                          void *cb_user_context)
{
    T *obj = reinterpret_cast<T*>(cb_user_context);
    if (obj == NULL)
    {
        assert(obj != NULL);
        return -1;
    }
    return obj->assign_data_in_buf(msg);
}

NetworkXioClient::NetworkXioClient(const std::string& uri,
                                   const size_t msg_q_depth)
    : uri_(uri)
    , queue_(msg_q_depth)
    , stopping(false)
    , disconnected(false)
{
    ses_ops.on_session_event = static_on_session_event<NetworkXioClient>;
    ses_ops.on_session_established = NULL;
    ses_ops.on_msg = static_on_response<NetworkXioClient>;
    ses_ops.on_msg_error = static_on_msg_error<NetworkXioClient>;
    ses_ops.on_cancel_request = NULL;
    ses_ops.assign_data_in_buf = static_assign_data_in_buf<NetworkXioClient>;

    memset(&params, 0, sizeof(params));
    memset(&cparams, 0, sizeof(cparams));

    params.type = XIO_SESSION_CLIENT;
    params.ses_ops = &ses_ops;
    params.user_context = this;
    params.uri = uri_.c_str();

    xio_init();

    int xopt = 0;
    int queue_depth = 2048;
    xio_set_opt(NULL,
                XIO_OPTLEVEL_ACCELIO,
                XIO_OPTNAME_ENABLE_FLOW_CONTROL,
                &xopt, sizeof(int));

    xopt = queue_depth;
    xio_set_opt(NULL,
                XIO_OPTLEVEL_ACCELIO, XIO_OPTNAME_SND_QUEUE_DEPTH_MSGS,
                &xopt, sizeof(int));

    xio_set_opt(NULL,
                XIO_OPTLEVEL_ACCELIO, XIO_OPTNAME_RCV_QUEUE_DEPTH_MSGS,
                &xopt, sizeof(int));

    ctx = xio_context_create(NULL, POLLING_TIME_USEC, -1);
    if (ctx == NULL)
    {
        goto err_exit2;
    }

    session = xio_session_create(&params);

    cparams.session = session;
    cparams.ctx = ctx;
    cparams.conn_user_context = this;

    conn = xio_connect(&cparams);
    if (conn == NULL)
    {
        goto err_exit1;
    }

    try
    {
        xio_thread_ = std::thread([&](){
                    auto fp = std::bind(&NetworkXioClient::xio_run_loop_worker,
                                        this,
                                        std::placeholders::_1);
                    pthread_setname_np(pthread_self(), "xio_run_loop_worker");
                    fp(this);
                });
    }
    catch (const std::system_error&)
    {
        goto err_exit1;
    }
    return;

err_exit1:
    xio_session_destroy(session);
    xio_context_destroy(ctx);
err_exit2:
    xio_shutdown();
    throw;
}

NetworkXioClient::~NetworkXioClient()
{
    stopping = true;
    xio_context_stop_loop(ctx);
    xio_thread_.join();
    while (not queue_.empty())
    {
        xio_msg_s *req;
        queue_.pop(req);
        delete req;
    }
}

void
NetworkXioClient::xio_run_loop_worker(void *arg)
{
    NetworkXioClient *cli = reinterpret_cast<NetworkXioClient*>(arg);
    while (not stopping)
    {
        int ret = xio_context_run_loop(cli->ctx, XIO_INFINITE);
        ASSERT(ret == 0);
        while (not cli->queue_.empty())
        {
            xio_msg_s *req;
            cli->queue_.pop(req);
            int r = xio_send_request(cli->conn, &req->xreq);
            if (r < 0)
            {
                int saved_errno = xio_errno();
                if (saved_errno == XIO_E_TX_QUEUE_OVERFLOW)
                {
                    /* not supported yet - flow control is disabled */
                    ovs_xio_aio_complete_request(const_cast<void*>(req->opaque),
                                                 -1,
                                                 EBUSY);
                }
                else
                {
                    /* fail request with EIO in any other case */
                    ovs_xio_aio_complete_request(const_cast<void*>(req->opaque),
                                                 -1,
                                                 EIO);
                }
                delete req;
            }
        }
    }

    xio_disconnect(cli->conn);
    if (not disconnected)
    {
        xio_context_run_loop(cli->ctx, XIO_INFINITE);
        xio_session_destroy(cli->session);
    }
    else
    {
        xio_session_destroy(cli->session);
    }
    xio_context_destroy(cli->ctx);
    xio_shutdown();
    return;
}

int
NetworkXioClient::assign_data_in_buf(xio_msg *msg)
{
    xio_iovec_ex *sglist = vmsg_sglist(&msg->in);
    xio_reg_mem xbuf;

    if (!sglist[0].iov_len)
    {
        return 0;
    }

    xio_mem_alloc(sglist[0].iov_len, &xbuf);

    sglist[0].iov_base = xbuf.addr;
    sglist[0].mr = xbuf.mr;
    return 0;
}

int
NetworkXioClient::on_msg_error(xio_session *session __attribute__((unused)),
                               xio_status error __attribute__((unused)),
                               xio_msg_direction direction,
                               xio_msg *msg)
{
    if (direction == XIO_MSG_DIRECTION_OUT)
    {
        NetworkXioMsg i_msg;
        try
        {
            i_msg.unpack_msg(static_cast<const char*>(msg->out.header.iov_base),
                             msg->out.header.iov_len);
        }
        catch (...)
        {
            //cnanakos: client logging?
            return 0;
        }
        xio_msg_s *xio_msg = reinterpret_cast<xio_msg_s*>(i_msg.opaque());
        ovs_xio_aio_complete_request(const_cast<void*>(xio_msg->opaque),
                                     -1,
                                     EIO);
        delete xio_msg;
    }
    else /* XIO_MSG_DIRECTION_IN */
    {
        xio_release_response(msg);
    }
    return 0;
}

int
NetworkXioClient::on_session_event(xio_session *session,
                                   xio_session_event_data *event_data)
{

    switch (event_data->event)
    {
    case XIO_SESSION_CONNECTION_TEARDOWN_EVENT:
        xio_connection_destroy(event_data->conn);
        disconnected = true;
        break;
    case XIO_SESSION_TEARDOWN_EVENT:
        xio_context_stop_loop(ctx);
        break;
    default:
        break;
    };
    return 0;
}

int
NetworkXioClient::xio_send_open_request(const std::string& volname,
                                        const void *opaque)
{
    xio_msg_s *xmsg;
    try{ xmsg = new xio_msg_s; }catch(const std::bad_alloc&) { return -1; };
    xmsg->opaque = opaque;
    xmsg->msg.opcode(NetworkXioMsgOpcode::OpenReq);
    xmsg->msg.opaque((uintptr_t)xmsg);
    xmsg->msg.volume_name(volname);

    xmsg->s_msg = xmsg->msg.pack_msg();

    memset(static_cast<void*>(&xmsg->xreq), 0, sizeof(xio_msg));

    vmsg_sglist_set_nents(&xmsg->xreq.out, 0);
    xmsg->xreq.out.header.iov_base = (void*)xmsg->s_msg.c_str();
    xmsg->xreq.out.header.iov_len = xmsg->s_msg.length();
    queue_.push(xmsg);
    xio_context_stop_loop(ctx);
    return 0;
}

int
NetworkXioClient::xio_send_read_request(void *buf,
                                        const uint64_t size_in_bytes,
                                        const uint64_t offset_in_bytes,
                                        const void *opaque)
{
    xio_msg_s *xmsg;
    try{ xmsg = new xio_msg_s; }catch(const std::bad_alloc&) { return -1; };
    xmsg->opaque = opaque;
    xmsg->msg.opcode(NetworkXioMsgOpcode::ReadReq);
    xmsg->msg.opaque((uintptr_t)xmsg);
    xmsg->msg.size(size_in_bytes);
    xmsg->msg.offset(offset_in_bytes);

    xmsg->s_msg = xmsg->msg.pack_msg();

    memset(static_cast<void*>(&xmsg->xreq), 0, sizeof(xio_msg));

    vmsg_sglist_set_nents(&xmsg->xreq.out, 0);
    xmsg->xreq.out.header.iov_base = (void*)xmsg->s_msg.c_str();
    xmsg->xreq.out.header.iov_len = xmsg->s_msg.length();

    vmsg_sglist_set_nents(&xmsg->xreq.in, 1);
    xmsg->xreq.in.data_iov.sglist[0].iov_base = buf;
    xmsg->xreq.in.data_iov.sglist[0].iov_len = size_in_bytes;
    queue_.push(xmsg);
    xio_context_stop_loop(ctx);
    return 0;
}

int
NetworkXioClient::xio_send_write_request(const void *buf,
                                         const uint64_t size_in_bytes,
                                         const uint64_t offset_in_bytes,
                                         const void *opaque)
{
    xio_msg_s *xmsg;
    try{ xmsg = new xio_msg_s; }catch(const std::bad_alloc&) { return -1; };
    xmsg->opaque = opaque;
    xmsg->msg.opcode(NetworkXioMsgOpcode::WriteReq);
    xmsg->msg.opaque((uintptr_t)xmsg);
    xmsg->msg.size(size_in_bytes);
    xmsg->msg.offset(offset_in_bytes);

    xmsg->s_msg = xmsg->msg.pack_msg();

    memset(static_cast<void*>(&xmsg->xreq), 0, sizeof(xio_msg));

    vmsg_sglist_set_nents(&xmsg->xreq.out, 1);
    xmsg->xreq.out.header.iov_base = (void*)xmsg->s_msg.c_str();
    xmsg->xreq.out.header.iov_len = xmsg->s_msg.length();
    xmsg->xreq.out.data_iov.sglist[0].iov_base = const_cast<void*>(buf);
    xmsg->xreq.out.data_iov.sglist[0].iov_len = size_in_bytes;
    queue_.push(xmsg);
    xio_context_stop_loop(ctx);
    return 0;
}

int
NetworkXioClient::xio_send_close_request(const void *opaque)
{
    xio_msg_s *xmsg;
    try{ xmsg = new xio_msg_s; }catch(const std::bad_alloc&) { return -1; };
    xmsg->opaque = opaque;
    xmsg->msg.opcode(NetworkXioMsgOpcode::CloseReq);
    xmsg->msg.opaque((uintptr_t)xmsg);

    xmsg->s_msg = xmsg->msg.pack_msg();

    memset(static_cast<void*>(&xmsg->xreq), 0, sizeof(xio_msg));

    vmsg_sglist_set_nents(&xmsg->xreq.out, 0);
    xmsg->xreq.out.header.iov_base = (void*)xmsg->s_msg.c_str();
    xmsg->xreq.out.header.iov_len = xmsg->s_msg.length();
    queue_.push(xmsg);
    xio_context_stop_loop(ctx);
    return 0;
}

int
NetworkXioClient::xio_send_flush_request(const void *opaque)
{
    xio_msg_s *xmsg;
    try{ xmsg = new xio_msg_s; }catch(const std::bad_alloc&) { return -1; };
    xmsg->opaque = opaque;
    xmsg->msg.opcode(NetworkXioMsgOpcode::FlushReq);
    xmsg->msg.opaque((uintptr_t)xmsg);

    xmsg->s_msg = xmsg->msg.pack_msg();

    memset(static_cast<void*>(&xmsg->xreq), 0, sizeof(xio_msg));

    vmsg_sglist_set_nents(&xmsg->xreq.out, 0);
    xmsg->xreq.out.header.iov_base = (void*)xmsg->s_msg.c_str();
    xmsg->xreq.out.header.iov_len = xmsg->s_msg.length();
    queue_.push(xmsg);
    xio_context_stop_loop(ctx);
    return 0;
}

int
NetworkXioClient::on_response(xio_session *session __attribute__((unused)),
                              xio_msg *reply,
                              int last_in_rxq __attribute__((unused)))
{
    NetworkXioMsg i_msg;
    i_msg.unpack_msg(static_cast<const char*>(reply->in.header.iov_base),
                     reply->in.header.iov_len);
    xio_msg_s *xio_msg = reinterpret_cast<xio_msg_s*>(i_msg.opaque());

    ovs_xio_aio_complete_request(const_cast<void*>(xio_msg->opaque),
                                 i_msg.retval(),
                                 i_msg.errval());

    reply->in.header.iov_base = NULL;
    reply->in.header.iov_len = 0;
    vmsg_sglist_set_nents(&reply->in, 0);
    xio_release_response(reply);
    delete xio_msg;
    return 0;
}

} //namespace volumedriverfs
