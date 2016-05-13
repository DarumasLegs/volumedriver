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

#ifndef __NETWORK_XIO_SERVER_H_
#define __NETWORK_XIO_SERVER_H_

#include <map>
#include <tuple>
#include <memory>
#include <libxio.h>

#include "FileSystem.h"
#include "NetworkXioWorkQueue.h"
#include "NetworkXioIOHandler.h"
#include "NetworkXioRequest.h"

namespace volumedriverfs
{

MAKE_EXCEPTION(FailedBindXioServer, fungi::IOException);
MAKE_EXCEPTION(FailedCreateXioContext, fungi::IOException);
MAKE_EXCEPTION(FailedCreateXioMempool, fungi::IOException);
MAKE_EXCEPTION(FailedCreateEventfd, fungi::IOException);
MAKE_EXCEPTION(FailedRegisterEventHandler, fungi::IOException);

class NetworkXioServer
{
public:
    NetworkXioServer(FileSystem& fs,
                     const std::string& uri,
                     size_t snd_rcv_queue_depth);

    ~NetworkXioServer();

    NetworkXioServer(const NetworkXioServer&) = delete;

    NetworkXioServer&
    operator=(const NetworkXioServer&) = delete;

    int
    on_request(xio_session *session,
               xio_msg *req,
               int last_in_rxq,
               void *cb_user_context);

    int
    on_session_event(xio_session *session,
                     xio_session_event_data *event_data);

    int
    on_new_session(xio_session *session,
                   xio_new_session_req *req);

    int
    on_msg_send_complete(xio_session *session,
                         xio_msg *msg,
                         void *cb_user_context);

    int
    on_msg_error(xio_session *session,
                 xio_status error,
                 xio_msg_direction direction,
                 xio_msg *msg);

    void
    run();

    void
    shutdown();

    void
    xio_send_reply(NetworkXioRequest *req);

    void
    evfd_stop_loop(int fd, int events, void *data);

    static void
    xio_destroy_ctx_shutdown(xio_context *ctx);
private:
    DECLARE_LOGGER("NetworkXioServer");

    FileSystem& fs_;
    std::string uri_;
    bool stopping;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped;
    EventFD evfd;

    NetworkXioWorkQueuePtr wq_;

    std::shared_ptr<xio_context> ctx;
    std::shared_ptr<xio_server> server;
    std::shared_ptr<xio_mempool> xio_mpool;

    int
    create_session_connection(xio_session *session,
                              xio_session_event_data *event_data);

    void
    destroy_session_connection(xio_session *session,
                               xio_session_event_data *event_data);

    NetworkXioRequest*
    allocate_request(NetworkXioClientData *cd, xio_msg *xio_req);

    void
    deallocate_request(NetworkXioRequest *req);

    void
    free_request(NetworkXioRequest *req);

    NetworkXioClientData*
    allocate_client_data();
};

} //namespace

#endif //__NETWORK_XIO_SERVER_H_
