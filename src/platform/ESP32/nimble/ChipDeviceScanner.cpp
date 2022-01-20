/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

#include "ChipDeviceScanner.h"

// #if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
// #if CONFIG_BT_NIMBLE_ENABLED

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include "blecent.h"

#define CHIPoBLE_SERVICE_UUID 0xFFF6

namespace chip {
namespace DeviceLayer {
namespace Internal {
namespace {

/// Retrieve CHIP device identification info from the device advertising data
bool NimbleGetChipDeviceInfo(const ble_hs_adv_fields & fields, chip::Ble::ChipBLEDeviceIdentificationInfo & deviceInfo)
{
    // Check for CHIP Service UUID
    for (uint8_t i = 0; i < fields.num_uuids16; i++)
    {
        if (fields.uuids16[i].value == CHIPoBLE_SERVICE_UUID)
        {
            memcpy(&deviceInfo, fields.svc_data_uuid16, sizeof(deviceInfo));
            return true;
        }
    }
    return false;
}

} // namespace

void ChipDeviceScanner::ReportDevice(const struct ble_hs_adv_fields & fields, const ble_addr_t & addr)
{
    // Just a debug print
    print_adv_fields(&fields);

    chip::Ble::ChipBLEDeviceIdentificationInfo deviceInfo;
    if (NimbleGetChipDeviceInfo(fields, deviceInfo) == false)
    {
        ChipLogDetail(Ble, "Device %s does not look like a CHIP device", addr_str(addr.val));
        return;
    }
    mDelegate->OnDeviceScanned(fields, addr, deviceInfo);
}

int ChipDeviceScanner::OnBleCentralEvent(struct ble_gap_event *event, void *arg)
{
    ChipDeviceScanner * scanner = (ChipDeviceScanner *) arg;

    switch (event->type)
    {
        case BLE_GAP_EVENT_DISC_COMPLETE:
        {
            scanner->mIsScanning = false;
            scanner->mDelegate->OnScanComplete();
            return 0;
        }

        case BLE_GAP_EVENT_DISC:
        {
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
            VerifyOrReturnError(rc == 0, 0);
            scanner->ReportDevice(fields, event->disc.addr);
            return 0;
        }
    }

    return 0;
}

CHIP_ERROR ChipDeviceScanner::StartScan(uint16_t timeout)
{
    ReturnErrorCodeIf(mIsScanning, CHIP_ERROR_INCORRECT_STATE);

    uint8_t ownAddrType;
    struct ble_gap_disc_params discParams;
    int rc;

    /* Figure out address to use while advertising. */
    rc = ble_hs_id_infer_auto(BLE_OWN_ADDR_PUBLIC, &ownAddrType);
    if (rc != 0)
    {
        ChipLogError(DeviceLayer, "ble_hs_id_infer_auto failed: %d", rc);
        return CHIP_ERROR_INTERNAL;
    }

    /* Set up discovery parameters. */
    memset(&discParams, 0, sizeof(discParams));
    
    /* Tell the controller to filter the duplicates. */
    discParams.filter_duplicates = 1;
    /* Perform passive scanning. */
    discParams.passive = 1;
    /* Use defaults for the rest of the parameters. */
    discParams.itvl = 0;
    discParams.window = 0;
    discParams.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
    discParams.limited = 0;

    /* Start the discovery process. */
    rc = ble_gap_disc(ownAddrType, (timeout * 1000), &discParams, OnBleCentralEvent, this);
    if (rc != 0)
    {
        ChipLogError(DeviceLayer, "ble_gap_disc failed: %d", rc);
        return CHIP_ERROR_INTERNAL;
    }
    mIsScanning = true;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ChipDeviceScanner::StopScan()
{
    ReturnErrorCodeIf(!mIsScanning, CHIP_NO_ERROR);

    int rc = ble_gap_disc_cancel();
    if (rc != 0)
    {
        ChipLogError(DeviceLayer, "ble_gap_disc_cancel failed: %d", rc);
        return CHIP_ERROR_INTERNAL;
    }
    mIsScanning = false;
    mDelegate->OnScanComplete();
    return CHIP_NO_ERROR;
}
    
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip

// #endif // CONFIG_BT_NIMBLE_ENABLED
// #endif // CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
