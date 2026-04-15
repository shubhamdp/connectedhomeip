/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
#pragma once

#include <app/clusters/measurement-base/MeasurementClusterBase.h>
#include <clusters/RelativeHumidityMeasurement/Attributes.h>
#include <clusters/RelativeHumidityMeasurement/Metadata.h>

namespace chip::app::Clusters {

struct RelativeHumidityMeasurementTraits
{
    using ValueType = uint16_t;

    static constexpr ClusterId kClusterId   = RelativeHumidityMeasurement::Id;
    static constexpr uint32_t kRevision     = RelativeHumidityMeasurement::kRevision;
    static constexpr uint16_t kMaxTolerance = 2048;

    static constexpr uint16_t kMinValueFloor   = 0;
    static constexpr uint16_t kMinValueCeiling = 9999;
    static constexpr uint16_t kMaxValueCeiling = 10000;

    static constexpr auto & kMandatoryMetadata = RelativeHumidityMeasurement::Attributes::kMandatoryMetadata;

    static const DataModel::AttributeEntry & GetToleranceMetadataEntry()
    {
        return RelativeHumidityMeasurement::Attributes::Tolerance::kMetadataEntry;
    }

    static bool ValidateMeasuredValue(uint16_t value) { return value <= 10000; }
};

class RelativeHumidityMeasurementCluster : public MeasurementClusterBase<RelativeHumidityMeasurementTraits>
{
public:
    using Base = MeasurementClusterBase<RelativeHumidityMeasurementTraits>;
    using Base::Base;
};

} // namespace chip::app::Clusters
