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

#ifndef NETWORK_XIO_WORKQUEUE_H_
#define NETWORK_XIO_WORKQUEUE_H_

#include <youtils/IOException.h>

#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <map>
#include <chrono>

#include <boost/thread/lock_guard.hpp>
#include <youtils/SpinLock.h>

#include "NetworkXioRequest.h"

namespace volumedriverfs
{

MAKE_EXCEPTION(WorkQueueThreadsException, Exception);

class NetworkXioWorkQueue
{
public:
    NetworkXioWorkQueue(const std::string& name, xio_context *ctx_)
    : name_(name)
    , nr_threads_(0)
    , nr_queued_work(0)
    , protection_period_(5000)
    , wq_open_sessions_(0)
    , stopping(false)
    , stopped(false)
    , ctx(ctx_)
    {
        using namespace std;
        int ret = create_workqueue_threads(thread::hardware_concurrency());
        if (ret < 0)
        {
            throw WorkQueueThreadsException("cannot create worker threads");
        }
    }

    ~NetworkXioWorkQueue()
    {
        shutdown();
    }

    void
    shutdown()
    {
        if (not stopped)
        {
            stopping = true;
            while (nr_threads_)
            {
                inflight_cond.notify_all();
                ::usleep(500);
            }
            stopped = true;
        }
    }

    void
    work_schedule(Work *work)
    {
        nr_queued_work++;
        inflight_lock.lock();
        if (need_to_grow())
        {
            create_workqueue_threads(nr_threads_ * 2);
        }
        inflight_list.push_back(work);
        inflight_lock.unlock();
        inflight_cond.notify_one();
    }

    void
    open_sessions_inc()
    {
        wq_open_sessions_++;
    }

    void
    open_sessions_dec()
    {
        wq_open_sessions_--;
    }

    static void*
    wq_routine(void *arg)
    {
        NetworkXioWorkQueue *obj = reinterpret_cast<NetworkXioWorkQueue*>(arg);
        if (obj == NULL)
        {
            assert(obj != NULL);
            return NULL;
        }
        return obj->worker_routine(arg);
    }

    Work*
    get_finished()
    {
        finished_lock.lock();
        NetworkXioRequest *req = finished.front();
        finished.pop_front();
        finished_lock.unlock();
        return req;
    }

    bool
    is_finished_empty()
    {
        std::lock_guard<std::mutex> lock_(finished_lock);
        return finished.empty();
    }
private:
    DECLARE_LOGGER("NetworkXioWorkQueue");

    std::string name_;
    std::atomic<size_t> nr_threads_;

    std::condition_variable inflight_cond;
    std::mutex inflight_lock;
    std::list<Work*> inflight_list;

    std::mutex finished_lock;
    std::list<NetworkXioRequest*> finished;

    std::atomic<size_t> nr_queued_work;

    std::map<std::thread::id, std::thread> wq_threads_;
    std::mutex wq_threads_lock_;
    std::chrono::steady_clock::time_point thread_life_period_;
    uint64_t protection_period_;
    std::atomic<uint64_t> wq_open_sessions_;

    bool stopping;
    bool stopped;
    xio_context *ctx;
    mutable fungi::SpinLock ctx_lock;

    std::chrono::steady_clock::time_point
    get_time_point()
    {
        return std::chrono::steady_clock::now();
    }

    size_t
    get_max_wq_depth()
    {
        return std::thread::hardware_concurrency() + 2 * wq_open_sessions_;
    }

    bool
    need_to_grow()
    {
        if ((nr_threads_ < nr_queued_work) &&
                (nr_threads_ * 2 <= get_max_wq_depth()))
        {
            thread_life_period_ = get_time_point() +
                std::chrono::milliseconds(protection_period_);
            return true;
        }
        return false;
    }

    bool
    need_to_shrink()
    {
        if (nr_queued_work < nr_threads_ / 2)
        {
            return thread_life_period_ <= get_time_point();
        }
        thread_life_period_ = get_time_point() +
            std::chrono::milliseconds(protection_period_);
        return false;
    }

    int
    create_workqueue_threads(size_t nr_threads)
    {
        while (nr_threads_ < nr_threads)
        {
            int ret;
            pthread_t thread;
            ret = pthread_create(&thread,
                                 NULL,
                                 &NetworkXioWorkQueue::wq_routine,
                                 this);
            if (ret != 0)
            {
                LOG_ERROR("cannot create worker thread");
                return -1;
            }
            pthread_setname_np(thread, name_.c_str());
            nr_threads_++;
        }
        return 0;
    }

    void*
    worker_routine(void *arg)
    {
        NetworkXioWorkQueue *wq = reinterpret_cast<NetworkXioWorkQueue*>(arg);

        Work *work;
        while (true)
        {
            std::unique_lock<std::mutex> lock_(wq->inflight_lock);
            if (wq->need_to_shrink())
            {
                wq->nr_threads_--;
                lock_.unlock();
                pthread_detach(pthread_self());
                break;
            }
retry:
            if (wq->inflight_list.empty())
            {
                wq->inflight_cond.wait(lock_);
                if (wq->stopping)
                {
                    wq->nr_threads_--;
                    pthread_detach(pthread_self());
                    break;
                }
                goto retry;
            }
            work = wq->inflight_list.front();
            wq->inflight_list.pop_front();
            lock_.unlock();
            if (work->func)
            {
                work->func(work);
            }
            wq->finished_lock.lock();
            wq->finished.push_back(req);
            wq->finished_lock.unlock();
            wq->nr_queued_work--;
            xio_context_stop_loop(wq->ctx);
        }
        pthread_exit(NULL);
    }
};

typedef std::shared_ptr<NetworkXioWorkQueue> NetworkXioWorkQueuePtr;

} //namespace

#endif //NETWORK_XIO_WORKQUEUE_H_