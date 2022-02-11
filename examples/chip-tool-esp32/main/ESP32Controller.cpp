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
#include <ESP32Controller.h>

#include <controller/CHIPDeviceControllerFactory.h>
#include <lib/support/ScopedBuffer.h>
#include <lib/support/CodeUtils.h>

using DeviceControllerFactory = chip::Controller::DeviceControllerFactory;

namespace {

chip::FabricIndex kDefaultFabricIndex = 1;
chip::FabricId kDefaultFabricId = 1;

} // namespace

CHIP_ERROR ESP32Controller::Init(void)
{
    ReturnLogErrorOnFailure(mFabricStorage.Initialize(&mDefaultStorage));

    chip::Controller::FactoryInitParams factoryInitParams;
    factoryInitParams.fabricStorage = &mFabricStorage;
    factoryInitParams.listenPort = 5600; // TODO: make configurable
    ReturnLogErrorOnFailure(DeviceControllerFactory::GetInstance().Init(factoryInitParams));

    return InitializeController();
}

CHIP_ERROR ESP32Controller::InitializeController()
{
    chip::Platform::ScopedMemoryBuffer<uint8_t> noc;
    chip::Platform::ScopedMemoryBuffer<uint8_t> icac;
    chip::Platform::ScopedMemoryBuffer<uint8_t> rcac;

    chip::Controller::SetupParams commissionerParams;
    ReturnLogErrorOnFailure(mCredIssuerCmds->SetupDeviceAttestation(commissionerParams));
    chip::Credentials::SetDeviceAttestationVerifier(commissionerParams.deviceAttestationVerifier);

    VerifyOrReturnError(noc.Alloc(chip::Controller::kMaxCHIPDERCertLength), CHIP_ERROR_NO_MEMORY);
    VerifyOrReturnError(icac.Alloc(chip::Controller::kMaxCHIPDERCertLength), CHIP_ERROR_NO_MEMORY);
    VerifyOrReturnError(rcac.Alloc(chip::Controller::kMaxCHIPDERCertLength), CHIP_ERROR_NO_MEMORY);

    chip::MutableByteSpan nocSpan(noc.Get(), chip::Controller::kMaxCHIPDERCertLength);
    chip::MutableByteSpan icacSpan(icac.Get(), chip::Controller::kMaxCHIPDERCertLength);
    chip::MutableByteSpan rcacSpan(rcac.Get(), chip::Controller::kMaxCHIPDERCertLength);

    chip::Crypto::P256Keypair ephemeralKey;
    ReturnLogErrorOnFailure(ephemeralKey.Initialize());

    // TODO - OpCreds should only be generated for pairing command
    //        store the credentials in persistent storage, and
    //        generate when not available in the storage.
    ReturnLogErrorOnFailure(mCredIssuerCmds->InitializeCredentialsIssuer(mCommissionerStorage));
    ReturnLogErrorOnFailure(mCredIssuerCmds->GenerateControllerNOCChain(mCommissionerStorage.GetLocalNodeId(), kDefaultFabricId, ephemeralKey, rcacSpan, icacSpan, nocSpan));

    commissionerParams.storageDelegate                = &mCommissionerStorage;
    commissionerParams.fabricIndex                    = kDefaultFabricIndex;
    commissionerParams.operationalCredentialsDelegate = mCredIssuerCmds->GetCredentialIssuer();
    commissionerParams.ephemeralKeypair               = &ephemeralKey;
    commissionerParams.controllerRCAC                 = rcacSpan;
    commissionerParams.controllerICAC                 = icacSpan;
    commissionerParams.controllerNOC                  = nocSpan;
    commissionerParams.controllerVendorId             = chip::VendorId::TestVendor1;

    ReturnLogErrorOnFailure(DeviceControllerFactory::GetInstance().SetupCommissioner(commissionerParams, mCommissioner));

    return CHIP_NO_ERROR;
}

CHIP_ERROR ESP32Controller::ShutdownCommissioner(void)
{
    return mCommissioner.Shutdown();
}
