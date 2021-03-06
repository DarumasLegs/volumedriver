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

#include "Scrubber.h"
#include "ScrubberAdapter.h"
#include "ScrubWork.h"
#include "ScrubReply.h"

namespace scrubbing
{

using namespace volumedriver;
namespace fs = boost::filesystem;

const uint64_t
ScrubberAdapter::region_size_exponent_default = 25;

const float
ScrubberAdapter::fill_ratio_default = 0.9;

const bool
ScrubberAdapter::apply_immediately_default = false;

// Might wanna change this.
const bool
ScrubberAdapter::verbose_scrubbing_default = true;

ScrubReply
ScrubberAdapter::scrub(const ScrubWork& scrub_work,
                       const fs::path& scratch_dir,
                       const uint64_t region_size_exponent,
                       const float fill_ratio,
                       const bool apply_immediately,
                       const bool verbose_scrubbing)
{
    ScrubberArgs scrubber_args;

    scrubber_args.backend_config = scrub_work.backend_config_->clone();
    scrubber_args.name_space = scrub_work.ns_.str();
    scrubber_args.scratch_dir = scratch_dir;
    scrubber_args.snapshot_name = scrub_work.snapshot_name_;
    scrubber_args.region_size_exponent = region_size_exponent;
    scrubber_args.sco_size = scrub_work.sco_size_;
    scrubber_args.cluster_size_exponent = scrub_work.cluster_exponent_;
    scrubber_args.fill_ratio = fill_ratio;
    scrubber_args.apply_immediately = apply_immediately;

    Scrubber scrubber(scrubber_args,
                      verbose_scrubbing);

    scrubber();

    return ScrubReply(scrub_work.ns_,
                      scrub_work.snapshot_name_,
                      scrubber.getScrubbingResultName());
}

}
