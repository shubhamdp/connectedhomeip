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

#include <app/server-cluster/AttributeListBuilder.h>
#include <app/server-cluster/DefaultServerCluster.h>
#include <app/server-cluster/OptionalAttributeSet.h>
#include <clusters/shared/GlobalIds.h>
#include <lib/support/Span.h>

namespace chip::app::Clusters {

// Common attribute IDs shared by all measurement clusters (spec-defined, same numeric values)
namespace MeasurementClusterAttributes {
static constexpr AttributeId kMeasuredValueId    = 0x00000000;
static constexpr AttributeId kMinMeasuredValueId = 0x00000001;
static constexpr AttributeId kMaxMeasuredValueId = 0x00000002;
static constexpr AttributeId kToleranceId        = 0x00000003;
} // namespace MeasurementClusterAttributes

/**
 * A Traits struct must provide:
 *
 *   using ValueType = int16_t or uint16_t;
 *
 *   static constexpr ClusterId kClusterId;
 *   static constexpr uint32_t kRevision;
 *   static constexpr uint16_t kMaxTolerance;  // max tolerance value (typically 2048)
 *
 *   // Spec-defined range bounds for MinMeasuredValue
 *   static constexpr ValueType kMinValueFloor;    // lowest allowed MinMeasuredValue
 *   static constexpr ValueType kMinValueCeiling;  // highest allowed MinMeasuredValue
 *
 *   // Spec-defined upper bound for MaxMeasuredValue
 *   static constexpr ValueType kMaxValueCeiling;  // highest allowed MaxMeasuredValue
 *
 *   // Attribute metadata
 *   static constexpr auto & kMandatoryMetadata;
 *
 *   // Tolerance metadata entry
 *   static const DataModel::AttributeEntry & GetToleranceMetadataEntry();
 *
 *   // Additional validation for SetMeasuredValue (e.g., humidity caps at 10000).
 *   // Return false to reject the value.
 *   static bool ValidateMeasuredValue(ValueType value);
 */

template <typename Traits>
class MeasurementClusterBase : public DefaultServerCluster
{
public:
    using ValueType          = typename Traits::ValueType;
    using OptionalAttributes = app::OptionalAttributeSet<MeasurementClusterAttributes::kToleranceId>;

    struct Config
    {
        DataModel::Nullable<ValueType> minMeasuredValue;
        DataModel::Nullable<ValueType> maxMeasuredValue;
        uint16_t tolerance    = 0;
        bool hasTolerance     = false;

        Config & WithTolerance(uint16_t value)
        {
            tolerance    = value;
            hasTolerance = true;
            return *this;
        }
    };

    explicit MeasurementClusterBase(EndpointId endpointId) : MeasurementClusterBase(endpointId, Config{}) {}

    MeasurementClusterBase(EndpointId endpointId, const Config & config) :
        DefaultServerCluster({ endpointId, Traits::kClusterId })
    {
        if (!config.minMeasuredValue.IsNull())
        {
            VerifyOrDie(config.minMeasuredValue.Value() >= Traits::kMinValueFloor);
            VerifyOrDie(config.minMeasuredValue.Value() <= Traits::kMinValueCeiling);

            if (!config.maxMeasuredValue.IsNull())
            {
                VerifyOrDie(config.maxMeasuredValue.Value() >= config.minMeasuredValue.Value() + 1);
            }
        }

        if (!config.maxMeasuredValue.IsNull())
        {
            VerifyOrDie(config.maxMeasuredValue.Value() <= Traits::kMaxValueCeiling);
        }

        if (config.hasTolerance)
        {
            VerifyOrDie(config.tolerance <= Traits::kMaxTolerance);
            mOptionalAttributeSet.template Set<MeasurementClusterAttributes::kToleranceId>();
        }

        mMinMeasuredValue = config.minMeasuredValue;
        mMaxMeasuredValue = config.maxMeasuredValue;
        mTolerance        = config.tolerance;
    }

    // ServerClusterInterface overrides
    DataModel::ActionReturnStatus ReadAttribute(const DataModel::ReadAttributeRequest & request,
                                                AttributeValueEncoder & encoder) override
    {
        switch (request.path.mAttributeId)
        {
        case Globals::Attributes::ClusterRevision::Id:
            return encoder.Encode(Traits::kRevision);
        case Globals::Attributes::FeatureMap::Id:
            return encoder.Encode<uint32_t>(0);
        case MeasurementClusterAttributes::kMeasuredValueId:
            return encoder.Encode(mMeasuredValue);
        case MeasurementClusterAttributes::kMinMeasuredValueId:
            return encoder.Encode(mMinMeasuredValue);
        case MeasurementClusterAttributes::kMaxMeasuredValueId:
            return encoder.Encode(mMaxMeasuredValue);
        case MeasurementClusterAttributes::kToleranceId:
            return encoder.Encode(mTolerance);
        default:
            return ReadDerivedAttribute(request, encoder);
        }
    }

    CHIP_ERROR Attributes(const ConcreteClusterPath & path, ReadOnlyBufferBuilder<DataModel::AttributeEntry> & builder) override
    {
        AttributeListBuilder listBuilder(builder);

        const DataModel::AttributeEntry baseOptionalAttributes[] = {
            Traits::GetToleranceMetadataEntry(),
        };

        return listBuilder.Append(Span(Traits::kMandatoryMetadata), Span(baseOptionalAttributes), mOptionalAttributeSet);
    }

    CHIP_ERROR SetMeasuredValue(DataModel::Nullable<ValueType> measuredValue)
    {
        if (!measuredValue.IsNull())
        {
            if (!Traits::ValidateMeasuredValue(measuredValue.Value()))
            {
                return CHIP_IM_GLOBAL_STATUS(ConstraintError);
            }

            if (!mMinMeasuredValue.IsNull())
            {
                VerifyOrReturnError(measuredValue.Value() >= mMinMeasuredValue.Value(), CHIP_IM_GLOBAL_STATUS(ConstraintError));
            }

            if (!mMaxMeasuredValue.IsNull())
            {
                VerifyOrReturnError(measuredValue.Value() <= mMaxMeasuredValue.Value(), CHIP_IM_GLOBAL_STATUS(ConstraintError));
            }
        }

        SetAttributeValue(mMeasuredValue, measuredValue, MeasurementClusterAttributes::kMeasuredValueId);
        return CHIP_NO_ERROR;
    }

    DataModel::Nullable<ValueType> GetMeasuredValue() const { return mMeasuredValue; }
    DataModel::Nullable<ValueType> GetMinMeasuredValue() const { return mMinMeasuredValue; }
    DataModel::Nullable<ValueType> GetMaxMeasuredValue() const { return mMaxMeasuredValue; }

protected:
    /// Extension point for derived clusters with extra attributes.
    virtual DataModel::ActionReturnStatus ReadDerivedAttribute(const DataModel::ReadAttributeRequest & request,
                                                               AttributeValueEncoder & encoder)
    {
        return Protocols::InteractionModel::Status::UnsupportedAttribute;
    }

    OptionalAttributes mOptionalAttributeSet;
    DataModel::Nullable<ValueType> mMeasuredValue{};
    DataModel::Nullable<ValueType> mMinMeasuredValue{};
    DataModel::Nullable<ValueType> mMaxMeasuredValue{};
    uint16_t mTolerance{};
};

} // namespace chip::app::Clusters
