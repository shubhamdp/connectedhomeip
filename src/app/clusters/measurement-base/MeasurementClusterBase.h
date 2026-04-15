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

#include <app/data-model-provider/MetadataTypes.h>
#include <app/server-cluster/DefaultServerCluster.h>
#include <app/server-cluster/OptionalAttributeSet.h>
#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
#include <protocols/interaction_model/StatusCode.h>

namespace chip {
namespace app {
namespace Clusters {

/// Non-template base class for measurement clusters. Compiled once to save flash.
///
/// Handles everything that does NOT depend on the measurement value type:
///   - ReadAttribute() with full switch statement (type-independent encoding)
///   - Attribute list building (Attributes() override)
///   - Optional attribute set storage and management
///   - Tolerance storage and encoding
///   - Virtual extension points for derived attributes (e.g., Pressure cluster extras)
///
/// The three type-dependent attributes (MeasuredValue, MinMeasuredValue, MaxMeasuredValue)
/// are encoded via thin pure virtual methods overridden in the template layer.
class MeasurementClusterCommon : public DefaultServerCluster
{
public:
    /// Parameters that do not depend on the value type.
    /// Concrete clusters only need to provide optionalAttributeSet and tolerance.
    struct CommonConfig
    {
        AttributeSet optionalAttributeSet;
        uint16_t tolerance = 0;
    };

    /// Full internal configuration including attribute IDs (populated by template layer from Traits).
    struct InternalConfig
    {
        AttributeSet optionalAttributeSet;
        uint16_t tolerance = 0;

        // Attribute IDs for this cluster
        AttributeId measuredValueAttrId    = 0;
        AttributeId minMeasuredValueAttrId = 0;
        AttributeId maxMeasuredValueAttrId = 0;
        AttributeId toleranceAttrId        = 0;
        AttributeId clusterRevisionAttrId  = 0;
        AttributeId featureMapAttrId       = 0;

        uint32_t clusterRevision = 0;
    };

    MeasurementClusterCommon(const ConcreteClusterPath & path, const InternalConfig & config);

    // ServerClusterInterface overrides
    CHIP_ERROR Attributes(const ConcreteClusterPath & path, ReadOnlyBufferBuilder<DataModel::AttributeEntry> & builder) override;
    DataModel::ActionReturnStatus ReadAttribute(const DataModel::ReadAttributeRequest & request,
                                                AttributeValueEncoder & encoder) override;

protected:
    const AttributeSet mOptionalAttributeSet;
    uint16_t mTolerance;

    // Attribute IDs stored at construction time
    AttributeId mMeasuredValueAttrId;
    AttributeId mMinMeasuredValueAttrId;
    AttributeId mMaxMeasuredValueAttrId;
    AttributeId mToleranceAttrId;
    AttributeId mClusterRevisionAttrId;
    AttributeId mFeatureMapAttrId;
    uint32_t mClusterRevision;

    /// Pure virtual: encode the typed MeasuredValue (Nullable<int16_t> or Nullable<uint16_t>)
    virtual DataModel::ActionReturnStatus EncodeMeasuredValue(AttributeValueEncoder & encoder) = 0;
    /// Pure virtual: encode the typed MinMeasuredValue
    virtual DataModel::ActionReturnStatus EncodeMinMeasuredValue(AttributeValueEncoder & encoder) = 0;
    /// Pure virtual: encode the typed MaxMeasuredValue
    virtual DataModel::ActionReturnStatus EncodeMaxMeasuredValue(AttributeValueEncoder & encoder) = 0;

    /// Extension point for derived classes with extra attributes.
    /// Default returns UnsupportedAttribute.
    virtual DataModel::ActionReturnStatus ReadDerivedAttribute(const DataModel::ReadAttributeRequest & request,
                                                               AttributeValueEncoder & encoder);

    /// Extension point: return a span of optional attribute metadata entries for derived-class attributes
    /// beyond the base Tolerance. Default returns an empty span.
    virtual Span<const DataModel::AttributeEntry> GetDerivedOptionalAttributes() const;

    /// Extension point: return the mandatory metadata array for this cluster.
    /// Each concrete cluster must provide its own kMandatoryMetadata from its generated Metadata.h.
    virtual Span<const DataModel::AttributeEntry> GetMandatoryMetadata() const = 0;

    /// Extension point: return the base optional attribute metadata (Tolerance).
    /// Each concrete cluster must provide its own Tolerance::kMetadataEntry from its generated Metadata.h.
    virtual Span<const DataModel::AttributeEntry> GetBaseOptionalAttributes() const = 0;
};

/// Template layer on top of MeasurementClusterCommon. Minimal per-type code.
///
/// Provides:
///   - Nullable<ValueType> storage for MeasuredValue, MinMeasuredValue, MaxMeasuredValue
///   - Thin EncodeMeasuredValue/Min/Max overrides (one-liners)
///   - SetMeasuredValue() with type-safe range validation
///   - SetMeasuredValueRange() for runtime range updates
///   - Constructor validation of range bounds
///   - Getters for all three nullable values
///
/// Traits must provide:
///   - using ValueType = int16_t or uint16_t
///   - static constexpr ClusterId kClusterId
///   - static constexpr uint32_t kClusterRevision
///   - Attribute ID constants: kMeasuredValueId, kMinMeasuredValueId, kMaxMeasuredValueId,
///     kToleranceId, kClusterRevisionId, kFeatureMapId
///   - static constexpr ValueType kMinMeasuredValueRangeMin (lowest allowed min)
///   - static constexpr ValueType kMinMeasuredValueRangeMax (highest allowed min)
///   - static constexpr ValueType kMaxMeasuredValueRangeMax (highest allowed max)
///   - static constexpr uint16_t kMaxTolerance
///   - static Span<const DataModel::AttributeEntry> GetMandatoryMetadata()
///   - static Span<const DataModel::AttributeEntry> GetBaseOptionalAttributes()
///   - Optional: static bool ValidateMeasuredValue(ValueType value) - extra validation beyond min/max range
///     (e.g., humidity's absolute max of 10000). Default returns true if not provided.
template <typename Traits>
class MeasurementClusterBase : public MeasurementClusterCommon
{
public:
    using ValueType = typename Traits::ValueType;

    MeasurementClusterBase(EndpointId endpointId, const CommonConfig & commonConfig,
                           DataModel::Nullable<ValueType> minMeasuredValue,
                           DataModel::Nullable<ValueType> maxMeasuredValue) :
        MeasurementClusterCommon({ endpointId, Traits::kClusterId },
                                 InternalConfig{
                                     .optionalAttributeSet    = commonConfig.optionalAttributeSet,
                                     .tolerance               = commonConfig.tolerance,
                                     .measuredValueAttrId     = Traits::kMeasuredValueId,
                                     .minMeasuredValueAttrId  = Traits::kMinMeasuredValueId,
                                     .maxMeasuredValueAttrId  = Traits::kMaxMeasuredValueId,
                                     .toleranceAttrId         = Traits::kToleranceId,
                                     .clusterRevisionAttrId   = Traits::kClusterRevisionId,
                                     .featureMapAttrId        = Traits::kFeatureMapId,
                                     .clusterRevision         = Traits::kClusterRevision,
                                 })
    {
        ValidateConstructorBounds(minMeasuredValue, maxMeasuredValue);

        mMinMeasuredValue = minMeasuredValue;
        mMaxMeasuredValue = maxMeasuredValue;
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

        SetAttributeValue(mMeasuredValue, measuredValue, Traits::kMeasuredValueId);
        return CHIP_NO_ERROR;
    }

    DataModel::Nullable<ValueType> GetMeasuredValue() const { return mMeasuredValue; }
    DataModel::Nullable<ValueType> GetMinMeasuredValue() const { return mMinMeasuredValue; }
    DataModel::Nullable<ValueType> GetMaxMeasuredValue() const { return mMaxMeasuredValue; }

protected:
    DataModel::Nullable<ValueType> mMeasuredValue{};
    DataModel::Nullable<ValueType> mMinMeasuredValue{};
    DataModel::Nullable<ValueType> mMaxMeasuredValue{};

    CHIP_ERROR SetMeasuredValueRange(DataModel::Nullable<ValueType> minMeasuredValue,
                                     DataModel::Nullable<ValueType> maxMeasuredValue)
    {
        if (!minMeasuredValue.IsNull())
        {
            VerifyOrReturnError(minMeasuredValue.Value() >= Traits::kMinMeasuredValueRangeMin &&
                                    minMeasuredValue.Value() <= Traits::kMinMeasuredValueRangeMax,
                                CHIP_IM_GLOBAL_STATUS(ConstraintError));

            if (!maxMeasuredValue.IsNull())
            {
                VerifyOrReturnError(maxMeasuredValue.Value() >= minMeasuredValue.Value() + 1,
                                    CHIP_IM_GLOBAL_STATUS(ConstraintError));
            }
        }

        if (!maxMeasuredValue.IsNull())
        {
            VerifyOrReturnError(maxMeasuredValue.Value() <= Traits::kMaxMeasuredValueRangeMax,
                                CHIP_IM_GLOBAL_STATUS(ConstraintError));
        }

        SetAttributeValue(mMinMeasuredValue, minMeasuredValue, Traits::kMinMeasuredValueId);
        SetAttributeValue(mMaxMeasuredValue, maxMeasuredValue, Traits::kMaxMeasuredValueId);
        return CHIP_NO_ERROR;
    }

    // MeasurementClusterCommon overrides -- thin virtual encode methods (one-liners)
    DataModel::ActionReturnStatus EncodeMeasuredValue(AttributeValueEncoder & encoder) override { return encoder.Encode(mMeasuredValue); }
    DataModel::ActionReturnStatus EncodeMinMeasuredValue(AttributeValueEncoder & encoder) override { return encoder.Encode(mMinMeasuredValue); }
    DataModel::ActionReturnStatus EncodeMaxMeasuredValue(AttributeValueEncoder & encoder) override { return encoder.Encode(mMaxMeasuredValue); }

    // MeasurementClusterCommon overrides -- metadata
    Span<const DataModel::AttributeEntry> GetMandatoryMetadata() const override { return Traits::GetMandatoryMetadata(); }
    Span<const DataModel::AttributeEntry> GetBaseOptionalAttributes() const override { return Traits::GetBaseOptionalAttributes(); }

private:
    void ValidateConstructorBounds(const DataModel::Nullable<ValueType> & minMeasuredValue,
                                   const DataModel::Nullable<ValueType> & maxMeasuredValue)
    {
        if (!minMeasuredValue.IsNull())
        {
            VerifyOrDie(minMeasuredValue.Value() >= Traits::kMinMeasuredValueRangeMin &&
                        minMeasuredValue.Value() <= Traits::kMinMeasuredValueRangeMax);

            if (!maxMeasuredValue.IsNull())
            {
                VerifyOrDie(maxMeasuredValue.Value() >= minMeasuredValue.Value() + 1);
            }
        }

        if (!maxMeasuredValue.IsNull())
        {
            VerifyOrDie(maxMeasuredValue.Value() <= Traits::kMaxMeasuredValueRangeMax);
        }

        VerifyOrDie(!mOptionalAttributeSet.IsSet(Traits::kToleranceId) || mTolerance <= Traits::kMaxTolerance);
    }
};

} // namespace Clusters
} // namespace app
} // namespace chip
