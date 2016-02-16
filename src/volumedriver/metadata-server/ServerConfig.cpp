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

#include "ServerConfig.h"

#include <youtils/StreamUtils.h>

namespace metadata_server
{

namespace fs = boost::filesystem;
namespace vd = volumedriver;

ServerConfig::ServerConfig(const volumedriver::MDSNodeConfig& ncfg,
                           const boost::filesystem::path& db,
                           const boost::filesystem::path& scratch,
                           const RocksConfig& rocks_cfg)
    : node_config(ncfg)
    , db_path(db)
    , scratch_path(scratch)
    , rocks_config(rocks_cfg)
{}

bool
ServerConfig::operator==(const ServerConfig& other) const
{
    return node_config == other.node_config and
        db_path == other.db_path and
        scratch_path == other.scratch_path and
        rocks_config == other.rocks_config;
}

bool
ServerConfig::operator!=(const ServerConfig& other) const
{
    return not operator==(other);
}

bool
ServerConfig::conflicts(const ServerConfig& other) const
{
    return node_config == other.node_config or
        db_path == other.db_path or
        scratch_path == other.scratch_path;
}

std::ostream&
operator<<(std::ostream& os,
           const ServerConfig& cfg)
{
    return os <<
        "ServerConfig{addr=" << cfg.node_config <<
        ",db=" << cfg.db_path <<
        ",scratch=" << cfg.scratch_path <<
        ",rocks_config=" << cfg.rocks_config <<
        "}";
}

std::ostream&
operator<<(std::ostream& os,
           const ServerConfigs& cfgs)
{
    return youtils::StreamUtils::stream_out_sequence(os,
                                                     cfgs);
}

}

namespace initialized_params
{

namespace mds = metadata_server;

#define KEY(key, str)                           \
    const std::string                           \
    PropertyTreeVectorAccessor<mds::ServerConfig>::key(str)

KEY(db_path_key, "db_directory");
KEY(scratch_path_key, "scratch_directory");
KEY(db_threads_key, "rocksdb_threads");
KEY(write_cache_size_key, "rocksdb_write_cache_size");
KEY(read_cache_size_key, "rocksdb_read_cache_size");
KEY(enable_wal_key, "rocksdb_enable_wal");
KEY(data_sync_key, "rocksdb_data_sync");
KEY(max_write_buffer_number_key, "rocksdb_max_write_buffer_number");
KEY(min_write_buffer_number_to_merge_key, "rocksdb_min_write_buffer_number_to_merge");
KEY(num_levels_key, "rocksdb_num_levels");
KEY(level0_file_num_compaction_trigger_key, "rocksdb_level0_file_num_compaction_trigger");
KEY(level0_slowdown_writes_trigger_key, "rocksdb_level0_slowdown_writes_trigger");
KEY(level0_stop_writes_trigger_key, "rocksdb_level0_stop_writes_trigger");
KEY(target_file_size_base_key, "rocksdb_target_file_size_base");
KEY(max_bytes_for_level_base_key, "rocksdb_max_bytes_for_level_base");
KEY(compaction_style_key, "rocksdb_compaction_style");

#undef KEY
}
