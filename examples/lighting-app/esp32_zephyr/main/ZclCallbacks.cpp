/*
 *    Copyright (c) 2024 Project CHIP Authors
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

#include "AppTask.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::OnOff;

void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value)
{
    ClusterId clusterId     = attributePath.mClusterId;
    AttributeId attributeId = attributePath.mAttributeId;

    if (clusterId == OnOff::Id && attributeId == OnOff::Attributes::OnOff::Id)
    {
        ChipLogProgress(Zcl, "Cluster OnOff: attribute OnOff set to %u", *value);
        AppTask::Instance().SetLight(*value, AppTask::Instance().GetLightLevel());
    }
    else if (clusterId == LevelControl::Id && attributeId == LevelControl::Attributes::CurrentLevel::Id)
    {
        ChipLogProgress(Zcl, "Cluster LevelControl: attribute CurrentLevel set to %u", *value);
        if (AppTask::Instance().IsLightOn())
        {
            AppTask::Instance().SetLight(true, *value);
        }
        else
        {
            ChipLogDetail(Zcl, "LED is off. Try to use move-to-level-with-on-off instead of move-to-level");
        }
    }
}

void emberAfOnOffClusterInitCallback(EndpointId endpoint)
{
    Protocols::InteractionModel::Status status;
    bool storedValue;

    status = Attributes::OnOff::Get(endpoint, &storedValue);
    if (status == Protocols::InteractionModel::Status::Success)
    {
        AppTask::Instance().SetLight(storedValue, AppTask::Instance().GetLightLevel());
    }

    AppTask::Instance().UpdateClusterState();
}
