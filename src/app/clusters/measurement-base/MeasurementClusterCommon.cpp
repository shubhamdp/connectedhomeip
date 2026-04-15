/*
 *    Copyright (c) 2026 Project CHIP Authors
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

#include <app/clusters/measurement-base/MeasurementClusterBase.h>
#include <app/server-cluster/AttributeListBuilder.h>

namespace chip {
namespace app {
namespace Clusters {

MeasurementClusterCommon::MeasurementClusterCommon(const ConcreteClusterPath & path, const InternalConfig & config) :
    DefaultServerCluster(path), mOptionalAttributeSet(config.optionalAttributeSet), mTolerance(config.tolerance),
    mMeasuredValueAttrId(config.measuredValueAttrId), mMinMeasuredValueAttrId(config.minMeasuredValueAttrId),
    mMaxMeasuredValueAttrId(config.maxMeasuredValueAttrId), mToleranceAttrId(config.toleranceAttrId),
    mClusterRevisionAttrId(config.clusterRevisionAttrId), mFeatureMapAttrId(config.featureMapAttrId),
    mClusterRevision(config.clusterRevision)
{}

CHIP_ERROR MeasurementClusterCommon::Attributes(const ConcreteClusterPath & path,
                                                ReadOnlyBufferBuilder<DataModel::AttributeEntry> & builder)
{
    AttributeListBuilder listBuilder(builder);

    // Get the base optional attributes (Tolerance) from the concrete cluster
    Span<const DataModel::AttributeEntry> baseOptional = GetBaseOptionalAttributes();

    // Common case: only base optional attributes (no derived overrides).
    // Uses the efficient AttributeSet-based Append that filters by enabled bits.
    return listBuilder.Append(GetMandatoryMetadata(), baseOptional, mOptionalAttributeSet);
}

DataModel::ActionReturnStatus MeasurementClusterCommon::ReadAttribute(const DataModel::ReadAttributeRequest & request,
                                                                      AttributeValueEncoder & encoder)
{
    AttributeId attrId = request.path.mAttributeId;

    if (attrId == mClusterRevisionAttrId)
    {
        return encoder.Encode(mClusterRevision);
    }
    if (attrId == mFeatureMapAttrId)
    {
        return encoder.Encode<uint32_t>(0);
    }
    if (attrId == mMeasuredValueAttrId)
    {
        return EncodeMeasuredValue(encoder);
    }
    if (attrId == mMinMeasuredValueAttrId)
    {
        return EncodeMinMeasuredValue(encoder);
    }
    if (attrId == mMaxMeasuredValueAttrId)
    {
        return EncodeMaxMeasuredValue(encoder);
    }
    if (attrId == mToleranceAttrId)
    {
        return encoder.Encode(mTolerance);
    }
    return ReadDerivedAttribute(request, encoder);
}

DataModel::ActionReturnStatus MeasurementClusterCommon::ReadDerivedAttribute(const DataModel::ReadAttributeRequest & request,
                                                                              AttributeValueEncoder & encoder)
{
    return Protocols::InteractionModel::Status::UnsupportedAttribute;
}

Span<const DataModel::AttributeEntry> MeasurementClusterCommon::GetDerivedOptionalAttributes() const
{
    return {};
}

} // namespace Clusters
} // namespace app
} // namespace chip
