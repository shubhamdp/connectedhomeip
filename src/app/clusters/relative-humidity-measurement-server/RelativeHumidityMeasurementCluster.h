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
#include <app/server-cluster/OptionalAttributeSet.h>
#include <clusters/RelativeHumidityMeasurement/Attributes.h>
#include <clusters/RelativeHumidityMeasurement/Metadata.h>

namespace chip::app::Clusters {

namespace RelativeHumidityMeasurementDetail {

// Spec-defined upper bound for MeasuredValue and MaxMeasuredValue
inline constexpr uint16_t kMeasuredValueMax = 10000;

struct Traits
{
    using ValueType = uint16_t;

    static constexpr ClusterId kClusterId    = RelativeHumidityMeasurement::Id;
    static constexpr uint32_t kClusterRevision = RelativeHumidityMeasurement::kRevision;

    static constexpr AttributeId kMeasuredValueId    = RelativeHumidityMeasurement::Attributes::MeasuredValue::Id;
    static constexpr AttributeId kMinMeasuredValueId = RelativeHumidityMeasurement::Attributes::MinMeasuredValue::Id;
    static constexpr AttributeId kMaxMeasuredValueId = RelativeHumidityMeasurement::Attributes::MaxMeasuredValue::Id;
    static constexpr AttributeId kToleranceId        = RelativeHumidityMeasurement::Attributes::Tolerance::Id;
    static constexpr AttributeId kClusterRevisionId  = RelativeHumidityMeasurement::Attributes::ClusterRevision::Id;
    static constexpr AttributeId kFeatureMapId       = RelativeHumidityMeasurement::Attributes::FeatureMap::Id;

    // Spec-defined range bounds
    static constexpr ValueType kMinMeasuredValueRangeMin = 0;
    static constexpr ValueType kMinMeasuredValueRangeMax = 9999;
    static constexpr ValueType kMaxMeasuredValueRangeMax = 10000;
    static constexpr uint16_t kMaxTolerance              = 2048;

    // Humidity has an absolute max of 10000 for MeasuredValue
    static bool ValidateMeasuredValue(ValueType value) { return value <= kMeasuredValueMax; }

    static Span<const DataModel::AttributeEntry> GetMandatoryMetadata()
    {
        return Span<const DataModel::AttributeEntry>(RelativeHumidityMeasurement::Attributes::kMandatoryMetadata);
    }

    static Span<const DataModel::AttributeEntry> GetBaseOptionalAttributes()
    {
        static const DataModel::AttributeEntry sOptional[] = {
            RelativeHumidityMeasurement::Attributes::Tolerance::kMetadataEntry,
        };
        return Span<const DataModel::AttributeEntry>(sOptional);
    }
};

} // namespace RelativeHumidityMeasurementDetail

class RelativeHumidityMeasurementCluster : public MeasurementClusterBase<RelativeHumidityMeasurementDetail::Traits>
{
public:
    using OptionalAttributeSet = app::OptionalAttributeSet<RelativeHumidityMeasurement::Attributes::Tolerance::Id>;

    struct Config
    {
        Config() : minMeasuredValue(), maxMeasuredValue(), mOptionalAttributeSet(), mTolerance(0) {}

        Config & WithTolerance(uint16_t value)
        {
            mTolerance = value;
            mOptionalAttributeSet.template Set<RelativeHumidityMeasurement::Attributes::Tolerance::Id>();
            return *this;
        }

        DataModel::Nullable<uint16_t> minMeasuredValue;
        DataModel::Nullable<uint16_t> maxMeasuredValue;
        OptionalAttributeSet mOptionalAttributeSet;
        uint16_t mTolerance;
    };

    explicit RelativeHumidityMeasurementCluster(EndpointId endpointId);
    RelativeHumidityMeasurementCluster(EndpointId endpointId, const Config & config);
};

} // namespace chip::app::Clusters
