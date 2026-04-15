/*
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
#include <clusters/TemperatureMeasurement/Attributes.h>
#include <clusters/TemperatureMeasurement/Metadata.h>

namespace chip::app::Clusters {

struct TemperatureMeasurementTraits
{
    using ValueType = int16_t;

    static constexpr ClusterId kClusterId   = TemperatureMeasurement::Id;
    static constexpr uint32_t kRevision     = TemperatureMeasurement::kRevision;
    static constexpr uint16_t kMaxTolerance = 2048;

    static constexpr int16_t kMinValueFloor   = -27315;
    static constexpr int16_t kMinValueCeiling = 32766;
    static constexpr int16_t kMaxValueCeiling = 32767;

    static constexpr auto & kMandatoryMetadata = TemperatureMeasurement::Attributes::kMandatoryMetadata;

    static const DataModel::AttributeEntry & GetToleranceMetadataEntry()
    {
        return TemperatureMeasurement::Attributes::Tolerance::kMetadataEntry;
    }

    static bool ValidateMeasuredValue(int16_t /* value */) { return true; }
};

class TemperatureMeasurementCluster : public MeasurementClusterBase<TemperatureMeasurementTraits>
{
public:
    using Base = MeasurementClusterBase<TemperatureMeasurementTraits>;
    using Base::Base;
};

} // namespace chip::app::Clusters
