// Copyright 2015 Open vStorage NV
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

#ifndef VOLUME_H_
#define VOLUME_H_

#include "BackendTasks.h"
#include "ClusterCacheHandle.h"
#include "FailOverCacheBridge.h"
#include "FailOverCacheConfigWrapper.h"
#include "FailOverCacheProxy.h"
#include "NSIDMap.h"
#include "PerformanceCounters.h"
#include "PrefetchData.h"
#include "RestartContext.h"
#include "SCO.h"
#include "SCOAccessData.h"
#include "Snapshot.h"
#include "SnapshotName.h"
#include "TLogReader.h"
#include "VolumeConfig.h"
#include "VolumeInterface.h"
#include "VolumeFactory.h"
#include "VolumeException.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <set>

#include <boost/circular_buffer.hpp>
#include <boost/utility.hpp>

#include <youtils/Logging.h>
#include <youtils/RWLock.h>

namespace scrubbing
{
struct ScrubberResult;
}

namespace volumedrivertest
{
class MetaDataStoreTest;
}

namespace volumedriver
{
BOOLEAN_ENUM(DeleteFailOverCache);
BOOLEAN_ENUM(CleanupScrubbingOnError);
BOOLEAN_ENUM(CleanupScrubbingOnSuccess);

class SnapshotPersistor;
class ScrubberResult;
class DataStoreNG;
class SnapshotManagement;
class ClusterReadDescriptor;
class MetaDataStoreInterface;
class MetaDataBackendConfig;

struct ClusterCacheVolumeInfo
{
    ClusterCacheVolumeInfo(uint64_t hits,
                        uint64_t misses)
        : num_hits(hits)
        , num_misses(misses)
    {}

    uint64_t num_hits;
    uint64_t num_misses;
};

class Volume
    : public VolumeInterface
{
    friend class VolumeTestSetup;
    friend class VolManagerTestSetup;
    friend class ErrorHandlingTest;
    friend class ::volumedrivertest::MetaDataStoreTest;
    friend void FailOverCacheBridge::run();

    friend void backend_task::WriteTLog::run(int);

public:
    VolumeFailOverState
    getVolumeFailOverState() const
    {
        return failoverstate_;
    }

    Volume(const VolumeConfig&,
           const OwnerTag,
           std::unique_ptr<SnapshotManagement>,
           std::unique_ptr<DataStoreNG>,
           std::unique_ptr<MetaDataStoreInterface>,
           NSIDMap nsidmap,
           const std::atomic<unsigned>& foc_throttle_usecs,
           bool& readOnlymode);

    Volume(const Volume&) = delete;

    Volume&
    operator=(const Volume&) = delete;

    ~Volume();

    const NSIDMap&
    getNSIDMap()
    {
        return nsidmap_;
    }

    uint64_t LBA2Addr(const uint64_t) const;
    ClusterAddress addr2CA(const uint64_t) const;
    uint64_t getSize() const;
    uint64_t getLBASize() const;
    uint64_t getLBACount() const;

    void
    updateReadActivity(time_t diff);

    void
    dumpDebugData();

    ClusterMultiplier
    getClusterMultiplier() const
    {
        std::lock_guard<decltype(config_lock_)> g(config_lock_);
        return config_.cluster_mult_;
    }

    uint64_t
    getSCOSize() const
    {
        std::lock_guard<decltype(config_lock_)> g(config_lock_);
        return config_.getSCOSize();
    }

    void
    processFailOverCacheEntry(ClusterLocation /*cli*/,
                              uint64_t /*lba*/,
                              const byte* /*buf*/,
                              int64_t /*size*/);


    /** @exception IOException */
    void
    validateIOLength(uint64_t lba, uint64_t len) const;

    /** @exception IOException */
    void
    validateIOAlignment(uint64_t lba, uint64_t len) const;

    /** @exception IOException, MetaDataStoreException */
    void
    write(uint64_t lba, const uint8_t *buf, uint64_t len);

   /** @exception IOException, MetaDataStoreException */
    void
    read(uint64_t lba, uint8_t *buf, uint64_t len);

    /** @exception IOException */
    void
    sync();

    void
    resize(uint64_t clusters);

    TLogID
    scheduleBackendSync();

    void
    localRestart();

    Volume*
    newVolume();

    Volume*
    backend_restart(const CloneTLogs& restartTLogs,
                    const SCONumber restartSCO,
                    const IgnoreFOCIfUnreachable,
                    const boost::optional<youtils::UUID>& last_snapshot_cork);

    void
    createSnapshot(const
                   SnapshotName& name,
                   const SnapshotMetaData& metadata = SnapshotMetaData(),
                   const UUID& = UUID());

    bool
    snapshotExists(const SnapshotName&) const;

    bool
    checkSnapshotUUID(const SnapshotName&,
                      const volumedriver::UUID&) const;

    void
    listSnapshots(std::list<SnapshotName>&) const;

    Snapshot
    getSnapshot(const SnapshotName&) const;

    void
    deleteSnapshot(const SnapshotName&);

    void
    setFailOverCacheConfig(const boost::optional<FailOverCacheConfig>& config);

    boost::optional<FailOverCacheConfig>
    getFailOverCacheConfig();

    fs::path
    getTempTLogPath() const;

    // VolumeInterface
    FailOverCacheBridge*
    getFailOver() override final
    {
        return failover_.get();
    }

    virtual const BackendInterfacePtr&
    getBackendInterface(const SCOCloneID id = SCOCloneID(0)) const override final
    {
        return nsidmap_.get(id);
    }

    virtual VolumeInterface*
    getVolumeInterface() override final
    {
        return this;
    }

    virtual void halt() override final;

    backend::Namespace
    getNamespace() const override final;

    virtual VolumeConfig
    get_config() const override final;

    virtual const VolumeId
    getName() const override final;

    virtual ClusterSize
    getClusterSize() const override final
    {
        return clusterSize_;
    }

    virtual void
    SCOWrittenToBackendCallback(uint64_t file_size,
                                boost::chrono::microseconds write_time) override final;

    virtual fs::path
    saveSnapshotToTempFile() override final;

    virtual void
    tlogWrittenToBackendCallback(const TLogID& tid,
                             const SCO sconame) override final;

    virtual SCOMultiplier
    getSCOMultiplier() const override final
    {
        std::lock_guard<decltype(config_lock_)> g(config_lock_);
        return config_.sco_mult_;
    }

    virtual boost::optional<TLogMultiplier>
    getTLogMultiplier() const override final
    {
        std::lock_guard<decltype(config_lock_)> g(config_lock_);
        return config_.tlog_mult_;
    }

    TLogMultiplier
    getEffectiveTLogMultiplier() const;

    virtual boost::optional<SCOCacheNonDisposableFactor>
    getSCOCacheMaxNonDisposableFactor() const override final
    {
        std::lock_guard<decltype(config_lock_)> g(config_lock_);
        return config_.max_non_disposable_factor_;
    }

    virtual DataStoreNG*
    getDataStore() override final
    {
        return dataStore_.get();
    }

    virtual void
    removeUpToFromFailOverCache(const SCO sconame) override final;

    virtual void
    checkState(const std::string& tlogname) override final;

    virtual void
    cork(const youtils::UUID&) override final;

    virtual void
    unCorkAndTrySync(const youtils::UUID&) override final;

    virtual void
    metaDataBackendConfigHasChanged(const MetaDataBackendConfig& cfg) override final;

    // End VolumeInterface

    void
    setSCOMultiplier(SCOMultiplier sco_mult);

    void
    setTLogMultiplier(const boost::optional<TLogMultiplier>& tlog_mult);

    void
    setSCOCacheMaxNonDisposableFactor(const boost::optional<SCOCacheNonDisposableFactor>& max_non_disposable_factor);

    MetaDataStoreInterface*
    getMetaDataStore()
    {
        return metaDataStore_.get();
    }

    void
    restoreSnapshot(const SnapshotName& name);

    void
    cloneFromParentSnapshot(const youtils::UUID& parent_snap_uuid,
                            const CloneTLogs& clone_tlogs);

    const SnapshotManagement&
    getSnapshotManagement() const;

    uint64_t
    getSnapshotBackendSize(const SnapshotName&);

    uint64_t
    getCurrentBackendSize() const;

    uint64_t
    getTotalBackendSize() const;

    void
    destroy(DeleteLocalData,
            RemoveVolumeCompletely,
            ForceVolumeDeletion);

    bool
    is_halted() const;

    void
    getScrubbingWork(std::vector<std::string>& scrubbing_work_units,
                     const boost::optional<SnapshotName>& start_snap,
                     const boost::optional<SnapshotName>& end_snap) const;


    void
    applyScrubbingWork(const std::string& scrubbing_result,
                       const CleanupScrubbingOnError = CleanupScrubbingOnError::F,
                       const CleanupScrubbingOnSuccess = CleanupScrubbingOnSuccess::T);


    SnapshotName
    getParentSnapName() const;

    uint64_t
    VolumeDataStoreWriteUsed() const;

    uint64_t
    VolumeDataStoreReadUsed() const;

    uint64_t
    getCacheHits() const;

    uint64_t
    getCacheMisses() const;

    uint64_t
    getNonSequentialReads() const;

    uint64_t
    getClusterCacheHits() const;

    uint64_t
    getClusterCacheMisses() const;

    uint64_t
    getTLogUsed() const;

    uint64_t
    getSnapshotSCOCount(const SnapshotName& = SnapshotName());

    // Y42 should be made const
    bool
    isSyncedToBackend() const;

    bool
    isSyncedToBackendUpTo(const SnapshotName&) const;

    bool
    isSyncedToBackendUpTo(const TLogID&) const;

    void
    setFOCTimeout(uint32_t timeout);

    bool
    checkConsistency();

    double
    readActivity() const;

    PrefetchData&
    getPrefetchData()
    {
        return prefetch_data_;
    }

    void
    startPrefetch(SCONumber last_sco_number = 0);

    ClusterCacheVolumeInfo
    getClusterCacheVolumeInfo() const;

    void
    set_cluster_cache_behaviour(const boost::optional<ClusterCacheBehaviour>& behaviour);

    boost::optional<ClusterCacheBehaviour>
    get_cluster_cache_behaviour() const
    {
        std::lock_guard<decltype(config_lock_)> g(config_lock_);
        return config_.cluster_cache_behaviour_;
    }

    void
    set_cluster_cache_mode(const boost::optional<ClusterCacheMode>& mode);

    boost::optional<ClusterCacheMode>
    get_cluster_cache_mode() const
    {
        std::lock_guard<decltype(config_lock_)> g(config_lock_);
        return config_.cluster_cache_mode_;
    }

    void
    set_cluster_cache_limit(const boost::optional<ClusterCount>& limit);

    boost::optional<ClusterCount>
    get_cluster_cache_limit() const
    {
        std::lock_guard<decltype(config_lock_)> g(config_lock_);
        return config_.cluster_cache_limit_;
    }

    ClusterCacheMode
    effective_cluster_cache_mode() const;

    ClusterCacheBehaviour
    effective_cluster_cache_behaviour() const;

    OwnerTag
    getOwnerTag()
    {
        std::lock_guard<decltype(config_lock_)> g(config_lock_);
        return config_.owner_tag_;
    }

    void
    setSyncIgnore(uint64_t number_of_syncs_to_ignore,
                  uint64_t maximum_time_to_ignore_syncs_in_seconds);

    void
    getSyncIgnore(uint64_t& number_of_syncs_to_ignore,
                  uint64_t& maximum_time_to_ignore_syncs_in_seconds) const;
    bool
    isCacheOnWrite() const;

    bool
    isCacheOnRead() const;

    ClusterCacheHandle
    getClusterCacheHandle() const;

    // Maximum number of times to try to sync to the FOC
    const static uint32_t max_num_retries = 6000;

    const bool mdstore_was_rebuilt_;

    IsVolumeTemplate
    isVolumeTemplate() const
    {
        std::lock_guard<decltype(config_lock_)> g(config_lock_);
        return config_.isVolumeTemplate();
    }

    void
    setAsTemplate();

    void
    updateMetaDataBackendConfig(const MetaDataBackendConfig& cfg);

    void
    check_and_fix_failovercache();

private:
    DECLARE_LOGGER("Volume");

    // protects access to config_
    // Modifications to it are typically done under the rwlock but some are done
    // while holding it in shared mode and read accesses happen outside the rwlock,
    // hence the need for another lock.
    mutable fungi::SpinLock config_lock_;

    void
    setVolumeFailOverState(const VolumeFailOverState instate)
    {
        failoverstate_ = instate;
        //std::swap(instate, failoverstate_);
         //     return instate;
    }

    // LOCKING:
    // - rwlock allows concurrent reads / one write
    // - write_lock_ is required to prevent write throttling from delaying reads,
    // -> lock order: write_lock_ before rwlock
    typedef boost::mutex lock_type;
    mutable lock_type write_lock_;

    uint64_t intCeiling(uint64_t x, uint64_t y) {
        //rounds x up to nearest multiple of y
        return x % y ?  (x / y + 1) * y : x;
    }

    mutable fungi::RWLock rwlock_;

    bool halted_;

    // eventually we need accessors
    std::unique_ptr<DataStoreNG> dataStore_;

    std::unique_ptr<FailOverCacheBridge> failover_;

    std::unique_ptr<MetaDataStoreInterface> metaDataStore_;

    uint64_t caMask_; // to be used with LBAs!

    const ClusterSize clusterSize_;

    VolumeConfig config_;

    FailOverCacheConfigWrapper foc_config_wrapper_;

    std::unique_ptr<SnapshotManagement> snapshotManagement_;

    const unsigned volOffset_;

    NSIDMap nsidmap_;

    VolumeFailOverState failoverstate_;
    std::string last_tlog_not_on_failover_;

    std::atomic<uint64_t> readcounter_;

    double read_activity_;
    PrefetchData prefetch_data_;
    std::unique_ptr<boost::thread> prefetch_thread_;
    // volume_readcache_id_t read_cache_id_;
    std::vector<ClusterLocation> cluster_locations_;

    fungi::SpinLock volumeStateSpinLock_;

    //reference to readonlymode defined on volmanager
    bool& readOnlyMode;

    const std::atomic<unsigned>& datastore_throttle_usecs_;
    const std::atomic<unsigned>& foc_throttle_usecs_;

    std::atomic<uint64_t> readCacheHits_;
    std::atomic<uint64_t> readCacheMisses_;
    bool has_dumped_debug_data;

    uint64_t number_of_syncs_;
    uint64_t total_number_of_syncs_;
    boost::optional<ClusterCacheHandle> cluster_cache_handle_;
    youtils::wall_timer2 sync_wall_timer_;

    void
    processReloc_(const TLogName &relocName, bool deletions);

    void
    executeDeletions_(TLogReader &);

    void
    writeClusters_(uint64_t addr,
                   const uint8_t* buf,
                   uint64_t bufsize);

    void
    readClusters_(uint64_t addr,
                  uint8_t* buf,
                  uint64_t bufsize);

    fs::path
    getCurrentTLogPath_() const;

    static bool
    checkSCONamesConsistency_(const std::vector<SCO>& names);

    bool
    checkTLogsConsistency_(CloneTLogs& ctl) const;

    void
    replayFOC_(FailOverCacheProxy&);

    void
    normalizeSAPs_(SCOAccessData::VectorType& sadv);

    // expects normalized SAPs and will rebase them against this voldrivers VRAs
    void
    startPrefetch_(const SCOAccessData::VectorType& sadv,
                   SCONumber last_sco_number);

    void
    stopPrefetch_();

    void
    localRestartDataStore_(SCONumber lastSCOInBackend,
                           ClusterLocation lastClusterLocationNotInBackend);

    void
    checkNotHalted_() const;

    void
    checkNotReadOnly_() const;

    void
    writeClusterMetaData_(ClusterAddress ca,
                          const ClusterLocationAndHash& loc);

    void
    replayClusterFromFailOverCache_(ClusterAddress ca,
                                    const ClusterLocation& loc,
                                    const uint8_t* buf);

    void
    writeClusterToFailOverCache_(const ClusterLocation& loc,
                                 uint64_t lba,
                                 const uint8_t* buf,
                                 unsigned& throttled_usecs);

    void
    writeConfigToBackend_(const VolumeConfig& cfg);

    void
    writeFailOverCacheConfigToBackend_();

    void
    sync_(AppendCheckSum append_chksum);

    void
    cleanupScrubbingOnError_(const scrubbing::ScrubberResult&, const std::string&);

    void
    throttle_(unsigned throttle_usecs) const;

    fs::path
    ensureDebugDataDirectory_();

    void
    check_cork_match_();

    void
    applyScrubbing(const std::string& res_name,
                   const std::string& ns,
                   const CleanupScrubbingOnError,
                   const CleanupScrubbingOnSuccess = CleanupScrubbingOnSuccess::T,
                   const PrefetchVolumeData = PrefetchVolumeData::F);

    using UpdateFun = std::function<void(VolumeConfig& cfg)>;

    void
    update_config_(const UpdateFun& fun = [](VolumeConfig&){});

    std::tuple<uint64_t, uint64_t>
    getSyncSettings();

    void
    maybe_update_owner_tag_(const OwnerTag);

    void
    register_with_cluster_cache_(OwnerTag otag);

    void
    deregister_from_cluster_cache_(OwnerTag otag);

    void
    reregister_with_cluster_cache_(OwnerTag old,
                                   OwnerTag new_);

    void
    reregister_with_cluster_cache_();

    void
    update_cluster_cache_limit_();

    void
    setSCOCacheLimitMax(uint64_t max,
                        const backend::Namespace& nsname);

    void
    setFailOverCache_(const FailOverCacheConfig&);

    void
    setNoFailOverCache_();

    TLogID
    scheduleBackendSync_();
};

} // namespace volumedriver

#endif // !VOLUME_H_

// Local Variables: **
// mode: c++ **
// End: **
