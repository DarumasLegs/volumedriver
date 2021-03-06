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

#include "Entry.h"
#include "VolumeConfig.h"
#include "VolumeConfigParameters.h"

#include <string.h>

#include <youtils/Assert.h>

namespace youtils
{

using namespace volumedriver;

const char* EnumTraits<VolumeConfig::WanBackupVolumeRole>::strings[] = {
    "Normal",
    "BackupBase",
    "BackupIncremental"
};

const size_t
EnumTraits<VolumeConfig::WanBackupVolumeRole>::size =
    sizeof(EnumTraits<VolumeConfig::WanBackupVolumeRole>::strings) / sizeof(char*);

}

namespace volumedriver
{

namespace yt = youtils;

const std::string
VolumeConfig::config_backend_name("volume_configuration");

VolumeConfig::VolumeConfig()
    : lba_size_(default_lba_size())
    , lba_count_(0)
    , cluster_mult_(default_cluster_multiplier())
    , sco_mult_(default_sco_multiplier())
    , readCacheEnabled_(true)
    , wan_backup_volume_role_(WanBackupVolumeRole::WanBackupNormal)
    , is_volume_template_(IsVolumeTemplate::F)
    , owner_tag_(OwnerTag(0))
{}

VolumeConfig::VolumeConfig(const CloneVolumeConfigParameters& params,
                           const VolumeConfig& parent_config)
    : VolumeConfig(params)
{
    const_cast<std::atomic<uint64_t>&>(lba_count_) = parent_config.lba_count().load();
    const_cast<LBASize&>(lba_size_) = parent_config.lba_size_;
    const_cast<ClusterMultiplier&>(cluster_mult_) = parent_config.cluster_mult_;
    const_cast<SCOMultiplier&>(sco_mult_) = parent_config.sco_mult_;
    const_cast<boost::optional<TLogMultiplier>&>(tlog_mult_) = parent_config.tlog_mult_;
    const_cast<boost::optional<SCOCacheNonDisposableFactor>&>(max_non_disposable_factor_) = parent_config.max_non_disposable_factor_;
    TODO("AR: what to do with the parent's mdstore settings? in case of arakoon we might want to reuse them.");
    verify_();
}

VolumeConfig::VolumeConfig(const VolumeConfig& other)
    : id_(other.id_)
    , ns_(other.ns_)
    , parent_ns_(other.parent_ns_)
    , parent_snapshot_(other.parent_snapshot_)
    , lba_size_(other.lba_size_)
    , lba_count_(other.lba_count_.load())
    , cluster_mult_(other.cluster_mult_)
    , sco_mult_(other.sco_mult_)
    , tlog_mult_(other.tlog_mult_)
    , max_non_disposable_factor_(other.max_non_disposable_factor_)
    , readCacheEnabled_(other.readCacheEnabled_)
    , wan_backup_volume_role_(other.wan_backup_volume_role_)
    , cluster_cache_behaviour_(other.cluster_cache_behaviour_)
    , cluster_cache_mode_(other.cluster_cache_mode_)
    , cluster_cache_limit_(other.cluster_cache_limit_)
    , metadata_cache_capacity_(other.metadata_cache_capacity_)
    , metadata_backend_config_(other.metadata_backend_config_->clone())
    , is_volume_template_(other.is_volume_template_)
    , number_of_syncs_to_ignore_(other.number_of_syncs_to_ignore_)
    , maximum_time_to_ignore_syncs_in_seconds_(other.maximum_time_to_ignore_syncs_in_seconds_)
    , owner_tag_(other.owner_tag_)
{
    verify_();
}

void
VolumeConfig::verify_()
{
    if (id_.empty())
    {
        LOG_ERROR("Empty volume IDs are not supported");
        throw fungi::IOException("Empty volume IDs are not supported");
    }

    if ((lba_count_ % cluster_mult_) != 0)
    {
        LOG_ERROR("LBA count (" << lba_count_ <<
                  ") should result in an integral number of clusters (multiplier: " <<
                  cluster_mult_ << ")");
        throw fungi::IOException("LBA count should be an integral number of clusters",
                                 id_.str().c_str());
    }

    if (cluster_cache_limit_ and
        *cluster_cache_limit_ == ClusterCount(0))
    {
        LOG_ERROR("Invalid cluster cache limit " << cluster_cache_limit_);
        throw fungi::IOException("Invalid cluster cache limit",
                                 id_.str().c_str());
    }

    // cf. OwnerTag.h: OwnerTag(0) is only allowed for internal use / backward compat
    // purposes. Callers must not pass it in.
    VERIFY(owner_tag_ != OwnerTag(0));
}

VolumeConfig&
VolumeConfig::operator=(const VolumeConfig& other)
{
    if(this != & other)
    {
        const_cast<VolumeId&>(id_) = other.id_;
        ns_ = other.ns_;
        const_cast<boost::optional<std::string>&>(parent_ns_) = other.parent_ns_;
        const_cast<SnapshotName&>(parent_snapshot_) = other.parent_snapshot_;
        const_cast<LBASize&>(lba_size_) = other.lba_size_;
        const_cast<std::atomic<uint64_t>&>(lba_count_) = other.lba_count_.load();
        const_cast<ClusterMultiplier&>(cluster_mult_) = other.cluster_mult_;
        const_cast<SCOMultiplier&>(sco_mult_) = other.sco_mult_;
        const_cast<boost::optional<TLogMultiplier>&>(tlog_mult_) = other.tlog_mult_;
        const_cast<boost::optional<SCOCacheNonDisposableFactor>&>(max_non_disposable_factor_) = other.max_non_disposable_factor_;
        const_cast<bool&>(readCacheEnabled_) = other.readCacheEnabled_;
        const_cast<WanBackupVolumeRole&>(wan_backup_volume_role_) =
            other.wan_backup_volume_role_;
        const_cast<boost::optional<ClusterCacheBehaviour>& >(cluster_cache_behaviour_) =
            other.cluster_cache_behaviour_;
        const_cast<boost::optional<ClusterCacheMode>& >(cluster_cache_mode_) =
            other.cluster_cache_mode_;
        const_cast<boost::optional<ClusterCount>& >(cluster_cache_limit_) =
            other.cluster_cache_limit_;
        const_cast<boost::optional<size_t>& >(metadata_cache_capacity_) =
            other.metadata_cache_capacity_;
        const_cast<MetaDataBackendConfigPtr&>(metadata_backend_config_) =
            other.metadata_backend_config_->clone();
        const_cast<IsVolumeTemplate&>(is_volume_template_) = other.is_volume_template_;
        number_of_syncs_to_ignore_ =
            other.number_of_syncs_to_ignore_;
        maximum_time_to_ignore_syncs_in_seconds_ =
            other.maximum_time_to_ignore_syncs_in_seconds_;
        const_cast<OwnerTag&>(owner_tag_) = other.owner_tag_;
    }

    return *this;
}

std::istream&
operator>>(std::istream& is,
           VolumeConfig::WanBackupVolumeRole& jt)
{
    return yt::EnumUtils::read_from_stream<VolumeConfig::WanBackupVolumeRole>(is,
                                                                              jt);
}

std::ostream&
operator<<(std::ostream& os,
           const VolumeConfig::WanBackupVolumeRole& jt)
{
    return yt::EnumUtils::write_to_stream<VolumeConfig::WanBackupVolumeRole>(os,
                                                                             jt);
}

}

// Local Variables: **
// mode: c++ **
// End: **
