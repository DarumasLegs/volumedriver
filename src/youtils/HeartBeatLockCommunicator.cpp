// This file is dual licensed GPLv2 and Apache 2.0.
// Active license depends on how it is used.
//
// Copyright 2016 iNuron NV
//
// // GPL //
// This file is part of OpenvStorage.
//
// OpenvStorage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenvStorage. If not, see <http://www.gnu.org/licenses/>.
//
// // Apache 2.0 //
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Catchers.h"
#include "HeartBeatLockCommunicator.h"

#include <boost/thread.hpp>

namespace youtils
{

HeartBeatLockCommunicator::HeartBeatLockCommunicator(GlobalLockStorePtr lock_store,
                                   const boost::posix_time::time_duration connection_timeout,
                                   const boost::posix_time::time_duration interrupt_timeout)
    : lock_store_(lock_store)
    , lock_(connection_timeout,
            interrupt_timeout)
{}

bool
HeartBeatLockCommunicator::lock_exists()
{
    return lock_store_->exists();
}

void
HeartBeatLockCommunicator::freeLock()
{
    lock_.hasLock(false);
    overwriteLock();
}

HeartBeatLock
HeartBeatLockCommunicator::getLock()
{
    LOG_INFO(name() << ": getting the lock and updating the tag");

    HeartBeatLock lock(lock_);
    std::tie(lock, tag_) = lock_store_->read();

    LOG_INFO(name() << ": new tag " << tag_);
    return lock;
}

bool
HeartBeatLockCommunicator::overwriteLock()
{
    LOG_INFO(name() << ": overwriting the lock with tag " << tag_);
    try
    {
        tag_ = lock_store_->write(lock_,
                                  tag_);

        LOG_INFO(name() << ": overwrote the lock");
        return true;
    }
    CATCH_STD_ALL_EWHAT({
            LOG_INFO(name() <<
                     ": exception while overwriting the lock: " << EWHAT);
            return false;
        });
}

bool
HeartBeatLockCommunicator::putLock()
{
    LOG_INFO(name() << ": putting the lock");

    try
    {
        tag_ = lock_store_->write(lock_,
                                  boost::none);

        LOG_INFO(name() << ": put the lock");
        return true;
    }
    CATCH_STD_ALL_EWHAT({
            LOG_WARN(name() <<
                     ": exception while putting the lock: " << EWHAT);
            return false;
        });
}

bool
HeartBeatLockCommunicator::tryToAcquireLock()
{
    LOG_INFO(name() << ": trying to acquire the lock");
    try
    {
        HeartBeatLock l(getLock());
        if(not l.hasLock())
        {
            LOG_INFO(name() << ": got the lock");
            return overwriteLock();
        }
        else
        {
            boost::this_thread::sleep(l.get_timeout());
            LOG_INFO(name() <<
                     ": trying to steal the lock with tag " << tag_);
            return overwriteLock();
        }
    }
    CATCH_STD_ALL_EWHAT({
            LOG_WARN(name() <<
                     ": exception while trying to acquire the lock " << EWHAT);
            return false;
        });
}

bool
HeartBeatLockCommunicator::refreshLock(MilliSeconds max_wait_time)
{
    LOG_INFO(name() << ": refreshing the lock");

    ++lock_;
    uint64_t count = 0;
    Clock::time_point start_of_update = Clock::now();

    while (count++ < max_update_retries)
    {
        LOG_INFO(name() <<
                 ": once more into the breach dear friends (retry " <<
                 count << ")");

        if (overwriteLock())
        {
            LOG_INFO(name() <<
                     ": lock was successfully refreshed");
            return true;
        }
        else
        {
            LOG_WARN(name() << ": failed to refresh lock");
            try
            {
                boost::optional<HeartBeatLock> local_lock;
                const int sleep_seconds = 1;

                while(true)
                {
                    try
                    {
                        LOG_INFO(name() <<
                                 ": reading the lock to see whether we're still owning it");
                        local_lock = (getLock());
                        break;
                    }
                    catch (...)
                    {
                        Clock::duration update_duration =
                            Clock::now() - start_of_update;
                        if (update_duration + boost::chrono::seconds(sleep_seconds) > max_wait_time)
                        {
                            throw;
                        }
                        else
                        {
                            LOG_WARN(name() <<
                                     ": reading the lock failed, we have " <<
                                     boost::chrono::duration_cast<boost::chrono::seconds>(max_wait_time - update_duration) << " left");
                        }
                    }
                    //boost::this_thread::sleep uses get_system_time() and might hang on system time reset
                    sleep(sleep_seconds);
                }

                VERIFY(local_lock);
                if(local_lock->different_owner(lock_))
                {
                    LOG_WARN(name() << ": lost the lock");
                    return false;
                }
            }
            CATCH_STD_ALL_EWHAT({
                    LOG_WARN(name() <<
                             ": exception getting the lock: " << EWHAT);
                    return false;
                });
        }
    }

    LOG_ERROR(name() << ": ran out of retries");
    return false;
}

const std::string&
HeartBeatLockCommunicator::name() const
{
    return lock_store_->name();
}

}

// Local Variables: **
// mode: c++ **
// End: **
