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
//

#include <youtils/Assert.h>
#include <youtils/Catchers.h>

#include "NetworkXioIOHandler.h"
#include "NetworkXioProtocol.h"

namespace volumedriverfs
{

static inline void
pack_msg(NetworkXioRequest *req)
{
    NetworkXioMsg o_msg(req->op);
    o_msg.retval(req->retval);
    o_msg.errval(req->errval);
    o_msg.opaque(req->opaque);
    req->s_msg= o_msg.pack_msg();
}

void
NetworkXioIOHandler::handle_open(NetworkXioRequest *req,
                                 const std::string& volume_name)
{
    LOG_DEBUG("trying to open volume with name '" << volume_name  << "'");

    req->op = NetworkXioMsgOpcode::OpenRsp;
    if (handle_)
    {
        LOG_ERROR("volume '" << volume_name <<
                  "' is already open for this session");
        req->retval = -1;
        req->errval = EIO;
        pack_msg(req);
        return;
    }

    const FrontendPath p("/" + volume_name +
                         fs_.vdisk_format().volume_suffix());

    try
    {
        fs_.open(p, O_RDWR, handle_);
        volume_name_ = volume_name;
        req->retval = 0;
        req->errval = 0;
    }
    catch (const HierarchicalArakoon::DoesNotExistException&)
    {
        LOG_ERROR("volume '" << volume_name << "' doesn't exist");
        req->retval = -1;
        req->errval = ENOENT;
    }
    catch (...)
    {
        LOG_ERROR("failed to open volume '" << volume_name << "'");
        req->retval = -1;
        req->errval = EIO;
    }
    pack_msg(req);
}

void
NetworkXioIOHandler::handle_close(NetworkXioRequest *req)
{
    req->op = NetworkXioMsgOpcode::CloseRsp;
    if (handle_)
    {
        fs_.release(std::move(handle_));
        req->retval = 0;
        req->errval = 0;
    }
    else
    {
        LOG_ERROR("failed to close volume '" << volume_name_ << "'");
        req->retval = -1;
        req->errval = EIO;
    }
    pack_msg(req);
}

void
NetworkXioIOHandler::handle_read(NetworkXioRequest *req,
                                 size_t size,
                                 uint64_t offset)
{
    req->op = NetworkXioMsgOpcode::ReadRsp;
    if (not handle_)
    {
        req->retval = -1;
        req->errval = EIO;
        pack_msg(req);
        return;
    }

    int ret = xio_mempool_alloc(req->cd->mpool, size, &req->reg_mem);
    if (ret < 0)
    {
        ret = xio_mem_alloc(size, &req->reg_mem);
        if (ret < 0)
        {
            LOG_ERROR("cannot allocate requested buffer, size: " << size);
            req->retval = -1;
            req->errval = ENOMEM;
            pack_msg(req);
            return;
        }
        req->from_pool = false;
    }

    req->data = req->reg_mem.addr;
    req->data_len = size;
    req->size = size;
    req->offset = offset;
    try
    {
       bool eof = false;
       fs_.read(*handle_,
                req->size,
                static_cast<char*>(req->data),
                req->offset,
                eof);
       req->retval = req->size;
       req->errval = 0;
    }
    CATCH_STD_ALL_EWHAT({
       LOG_ERROR("read I/O error: " << EWHAT);
       req->retval = -1;
       req->errval = EIO;
    });
    pack_msg(req);
}

void
NetworkXioIOHandler::handle_write(NetworkXioRequest *req,
                                  size_t size,
                                  uint64_t offset)
{
    req->op = NetworkXioMsgOpcode::WriteRsp;
    if (not handle_)
    {
        req->retval = -1;
        req->errval = EIO;
        pack_msg(req);
        return;
    }

    if (req->data_len < size)
    {
       LOG_ERROR("data buffer size is smaller than the requested write size"
                 " for volume '" << volume_name_ << "'");
       req->retval = -1;
       req->errval = EIO;
       pack_msg(req);
       return;
    }

    req->size = size;
    req->offset = offset;
    bool sync = false;
    try
    {
       fs_.write(*handle_,
                 req->size,
                 static_cast<char*>(req->data),
                 req->offset,
                 sync);
       req->retval = req->size;
       req->errval = 0;
    }
    CATCH_STD_ALL_EWHAT({
       LOG_ERROR("write I/O error: " << EWHAT);
       req->retval = -1;
       req->errval = EIO;
    });
    pack_msg(req);
}

void
NetworkXioIOHandler::handle_flush(NetworkXioRequest *req)
{
    req->op = NetworkXioMsgOpcode::FlushRsp;
    if (not handle_)
    {
        req->retval = -1;
        req->errval = EIO;
        pack_msg(req);
        return;
    }

    LOG_TRACE("Flushing");
    try
    {
       fs_.fsync(*handle_, false);
       req->retval = 0;
       req->errval = 0;
    }
    CATCH_STD_ALL_EWHAT({
       LOG_ERROR("flush I/O error: " << EWHAT);
       req->retval = -1;
       req->errval = EIO;
    });
    pack_msg(req);
}

void
NetworkXioIOHandler::handle_error(NetworkXioRequest *req, int errval)
{
    req->op = NetworkXioMsgOpcode::ErrorRsp;
    req->retval = -1;
    req->errval = errval;
    pack_msg(req);
}

void
NetworkXioIOHandler::process_request(Work * work)
{
    NetworkXioRequest *req = get_container_of(work, NetworkXioRequest, work);
    xio_msg *xio_req = req->xio_req;
    xio_iovec_ex *isglist = vmsg_sglist(&xio_req->in);
    int inents = vmsg_sglist_nents(&xio_req->in);

    req->cd->refcnt++;
    NetworkXioMsg i_msg(NetworkXioMsgOpcode::Noop);
    try
    {
        i_msg.unpack_msg(static_cast<char*>(xio_req->in.header.iov_base),
                         xio_req->in.header.iov_len);
    }
    catch (...)
    {
        LOG_ERROR("cannot unpack message");
        handle_error(req, EBADMSG);
        return;
    }

    req->opaque = i_msg.opaque();
    switch (i_msg.opcode())
    {
    case NetworkXioMsgOpcode::OpenReq:
    {
        handle_open(req,
                    i_msg.volume_name());
        break;
    }
    case NetworkXioMsgOpcode::CloseReq:
    {
        handle_close(req);
        break;
    }
    case NetworkXioMsgOpcode::ReadReq:
    {
        handle_read(req,
                    i_msg.size(),
                    i_msg.offset());
        break;
    }
    case NetworkXioMsgOpcode::WriteReq:
    {
        if (inents >= 1)
        {
            req->data = isglist[0].iov_base;
            req->data_len = isglist[0].iov_len;
            handle_write(req, i_msg.size(), i_msg.offset());
        }
        else
        {
            LOG_ERROR("inents is smaller than 1, cannot proceed with write I/O");
            req->op = NetworkXioMsgOpcode::WriteRsp;
            req->retval = -1;
            req->errval = EIO;
        }
        break;
    }
    case NetworkXioMsgOpcode::FlushReq:
    {
        handle_flush(req);
        break;
    }
    default:
        LOG_ERROR("Unknown command");
        handle_error(req, EIO);
        return;
    };
}

void
NetworkXioIOHandler::handle_request(NetworkXioRequest *req)
{
    req->work.func = std::bind(&NetworkXioIOHandler::process_request,
                               this,
                               &req->work);
    wq_->work_schedule(&req->work);
}

} //namespace volumedriverfs