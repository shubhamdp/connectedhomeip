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
#include <app/server-cluster/OptionalAttributeSet.h>
#include <clusters/TemperatureMeasurement/Attributes.h>
#include <clusters/TemperatureMeasurement/Metadata.h>

namespace chip::app::Clusters {

namespace TemperatureMeasurementDetail {

struct Traits
{
    using ValueType = int16_t;

    static constexpr ClusterId kClusterId    = TemperatureMeasurement::Id;
    static constexpr uint32_t kClusterRevision = TemperatureMeasurement::kRevision;

    static constexpr AttributeId kMeasuredValueId    = TemperatureMeasurement::Attributes::MeasuredValue::Id;
    static constexpr AttributeId kMinMeasuredValueId = TemperatureMeasurement::Attributes::MinMeasuredValue::Id;
    static constexpr AttributeId kMaxMeasuredValueId = TemperatureMeasurement::Attributes::MaxMeasuredValue::Id;
    static constexpr AttributeId kToleranceId        = TemperatureMeasurement::Attributes::Tolerance::Id;
    static constexpr AttributeId kClusterRevisionId  = TemperatureMeasurement::Attributes::ClusterRevision::Id;
    static constexpr AttributeId kFeatureMapId       = TemperatureMeasurement::Attributes::FeatureMap::Id;

    // Spec-defined range bounds
    static constexpr ValueType kMinMeasuredValueRangeMin = -27315;
    static constexpr ValueType kMinMeasuredValueRangeMax = 32766;
    // For temperature, max has no separate upper bound beyond the type limit
    static constexpr ValueType kMaxMeasuredValueRangeMax = 32767;
    static constexpr uint16_t kMaxTolerance              = 2048;

    static bool ValidateMeasuredValue(ValueType /* value */) { return true; }

    static Span<const DataModel::AttributeEntry> GetMandatoryMetadata()
    {
        return Span<const DataModel::AttributeEntry>(TemperatureMeasurement::Attributes::kMandatoryMetadata);
    }

    static Span<const DataModel::AttributeEntry> GetBaseOptionalAttributes()
    {
        static const DataModel::AttributeEntry sOptional[] = {
            TemperatureMeasurement::Attributes::Tolerance::kMetadataEntry,
        };
        return Span<const DataModel::AttributeEntry>(sOptional);
    }
};

} // namespace TemperatureMeasurementDetail

class TemperatureMeasurementCluster : public MeasurementClusterBase<TemperatureMeasurementDetail::Traits>
{
public:
    using OptionalAttributeSet = app::OptionalAttributeSet<TemperatureMeasurement::Attributes::Tolerance::Id>;

    struct StartupConfiguration
    {
        DataModel::Nullable<int16_t> minMeasuredValue{};
        DataModel::Nullable<int16_t> maxMeasuredValue{};
        uint16_t tolerance{};
    };

    TemperatureMeasurementCluster(EndpointId endpointId, const OptionalAttributeSet & optionalAttributeSet,
                                  const StartupConfiguration & config);

    // Public SetMeasuredValueRange (Temperature has this public, unlike Flow/Humidity)
    using MeasurementClusterBase::SetMeasuredValueRange;
};

} // namespace chip::app::Clusters
