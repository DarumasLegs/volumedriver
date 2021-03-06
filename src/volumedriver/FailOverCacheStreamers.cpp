// Copyright 2015 iNuron NV
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

#include "FailOverCacheStreamers.h"
#include "VolumeConfig.h"

#include "failovercache/fungilib/WrapByteArray.h"
#include "failovercache/fungilib/Socket.h"

namespace
{

DECLARE_LOGGER("FailOverCacheStreamers");

}

namespace volumedriver
{

fungi::IOBaseStream&
checkStreamOK(fungi::IOBaseStream& stream,
              const std::string& ex)
{
    stream >> fungi::IOBaseStream::cork;
    uint32_t res;
    stream >> res;
    if(res != volumedriver::Ok)
    {
        LOG_ERROR("Protocol Error: *Not* Ok returned in " << ex);
        throw fungi::IOException(ex.c_str());
    }
    return stream;
}

fungi::IOBaseStream&
operator<<(fungi::IOBaseStream& stream, const CommandData<Register>& data)
{
    stream << fungi::IOBaseStream::cork;
    OUT_ENUM(stream, Register);
    stream << data.ns_;
    stream << static_cast<uint32_t>(data.clustersize_);
    stream << fungi::IOBaseStream::uncork;
    return checkStreamOK(stream, "Register");
}

fungi::IOBaseStream&
operator>>(fungi::IOBaseStream& stream, CommandData<Register>& data)
{
    stream >> data.ns_;
    stream >> data.clustersize_;
    return stream;
}

fungi::IOBaseStream&
operator<<(fungi::IOBaseStream& stream, const CommandData<AddEntries>& data)
{
    if (not data.entries_.empty())
    {
        VERIFY(data.entries_.front().cli_.sco() == data.entries_.back().cli_.sco());
    }

    bool isRDMA = stream.isRdma();
    stream << fungi::IOBaseStream::cork;
    OUT_ENUM(stream, AddEntries);
    const size_t wsize = data.entries_.size();
    stream << wsize;

    if (isRDMA) // make small but finished packets with RDMA
    {
        stream << fungi::IOBaseStream::uncork;
    }

    for (std::vector<FailOverCacheEntry>::const_iterator it = data.entries_.begin();
        it!= data.entries_.end();
        ++it)
    {
        if (isRDMA) // make small but finished packets with RDMA
        {
            stream << fungi::IOBaseStream::cork;
        }
        stream << it->cli_;
        stream << it->lba_;
        TODO("ArneT: funky cast...");
        stream << fungi::WrapByteArray((byte*) it->buffer_, it->size_);
        if (isRDMA) // make small but finished packets with RDMA
        {
            stream << fungi::IOBaseStream::uncork;
        }
    }
    stream << fungi::IOBaseStream::uncork;
    return checkStreamOK(stream, "AddEntries");
}

fungi::IOBaseStream&
operator>>(fungi::IOBaseStream& stream, CommandData<AddEntries>& data)
{
    size_t count = 0;
    stream >> count;

    data.entries_.reserve(count);

    uint64_t cluster_size = 0;
    uint8_t* ptr = nullptr;

    for(size_t i = 0; i < count; ++i)
    {
        ClusterLocation cli;
        stream >> cli;

        uint64_t lba;
        stream >> lba;

        int64_t sz;
        stream >> sz;
        VERIFY(sz != 0);

        if (ptr == nullptr)
        {
            VERIFY(data.buf_ == nullptr);

            cluster_size = sz;
            data.buf_ = std::make_unique<uint8_t[]>(cluster_size * count);
            ptr = data.buf_.get();
        }
        else
        {
            VERIFY(static_cast<int64_t>(cluster_size) == sz);
            VERIFY(data.buf_ != nullptr);
        }

        stream.readIntoByteArray(ptr,
                                 cluster_size);
        data.entries_.emplace_back(cli,
                                   lba,
                                   ptr,
                                   cluster_size);
        ptr += cluster_size;
    }

    if (not data.entries_.empty())
    {
        VERIFY(data.entries_.front().cli_.sco() == data.entries_.back().cli_.sco());
    }

    return stream;
}

fungi::IOBaseStream&
operator<<(fungi::IOBaseStream& stream, const CommandData<Flush>& /*data*/)
{
    stream << fungi::IOBaseStream::cork;
    OUT_ENUM(stream, Flush);
    stream << fungi::IOBaseStream::uncork;
    return checkStreamOK(stream,"Flush");
}

fungi::IOBaseStream&
operator>>(fungi::IOBaseStream& stream, CommandData<Flush>& /*data*/)
{
    return stream;
}

fungi::IOBaseStream&
operator<<(fungi::IOBaseStream& stream, const CommandData<Clear>& /*data*/)
{
    stream << fungi::IOBaseStream::cork;
    OUT_ENUM(stream, Clear);
    stream << fungi::IOBaseStream::uncork;
    return checkStreamOK(stream, "Clear");
}

FailOverCacheEntry::FailOverCacheEntry(ClusterLocation cli,
                                       uint64_t lba,
                                       const uint8_t* buffer,
                                       uint32_t size)
    : cli_(cli)
    , lba_(lba)
    , size_(size)
    , buffer_(buffer)
{
}

}

// Local Variables: **
// mode: c++ **
// End: **
