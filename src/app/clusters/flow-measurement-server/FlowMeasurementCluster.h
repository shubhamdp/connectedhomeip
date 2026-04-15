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
#include <clusters/FlowMeasurement/Attributes.h>
#include <clusters/FlowMeasurement/Metadata.h>

namespace chip::app::Clusters {

namespace FlowMeasurementDetail {

struct Traits
{
    using ValueType = uint16_t;

    static constexpr ClusterId kClusterId    = FlowMeasurement::Id;
    static constexpr uint32_t kClusterRevision = FlowMeasurement::kRevision;

    static constexpr AttributeId kMeasuredValueId    = FlowMeasurement::Attributes::MeasuredValue::Id;
    static constexpr AttributeId kMinMeasuredValueId = FlowMeasurement::Attributes::MinMeasuredValue::Id;
    static constexpr AttributeId kMaxMeasuredValueId = FlowMeasurement::Attributes::MaxMeasuredValue::Id;
    static constexpr AttributeId kToleranceId        = FlowMeasurement::Attributes::Tolerance::Id;
    static constexpr AttributeId kClusterRevisionId  = FlowMeasurement::Attributes::ClusterRevision::Id;
    static constexpr AttributeId kFeatureMapId       = FlowMeasurement::Attributes::FeatureMap::Id;

    // Spec-defined range bounds
    static constexpr ValueType kMinMeasuredValueRangeMin = 0;
    static constexpr ValueType kMinMeasuredValueRangeMax = 65533;
    static constexpr ValueType kMaxMeasuredValueRangeMax = 65534;
    static constexpr uint16_t kMaxTolerance              = 2048;

    static bool ValidateMeasuredValue(ValueType /* value */) { return true; }

    static Span<const DataModel::AttributeEntry> GetMandatoryMetadata()
    {
        return Span<const DataModel::AttributeEntry>(FlowMeasurement::Attributes::kMandatoryMetadata);
    }

    static Span<const DataModel::AttributeEntry> GetBaseOptionalAttributes()
    {
        static const DataModel::AttributeEntry sOptional[] = {
            FlowMeasurement::Attributes::Tolerance::kMetadataEntry,
        };
        return Span<const DataModel::AttributeEntry>(sOptional);
    }
};

} // namespace FlowMeasurementDetail

class FlowMeasurementCluster : public MeasurementClusterBase<FlowMeasurementDetail::Traits>
{
public:
    using OptionalAttributeSet = app::OptionalAttributeSet<FlowMeasurement::Attributes::Tolerance::Id>;

    struct Config
    {
        Config() : minMeasuredValue(), maxMeasuredValue(), mOptionalAttributeSet(), mTolerance(0) {}

        Config & WithTolerance(uint16_t value)
        {
            mTolerance = value;
            mOptionalAttributeSet.template Set<FlowMeasurement::Attributes::Tolerance::Id>();
            return *this;
        }

        DataModel::Nullable<uint16_t> minMeasuredValue;
        DataModel::Nullable<uint16_t> maxMeasuredValue;
        OptionalAttributeSet mOptionalAttributeSet;
        uint16_t mTolerance;
    };

    explicit FlowMeasurementCluster(EndpointId endpointId);
    FlowMeasurementCluster(EndpointId endpointId, const Config & config);

protected:
    using MeasurementClusterBase::SetMeasuredValueRange;
};

} // namespace chip::app::Clusters
