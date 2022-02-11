/*
 *   Copyright (c) 2021 Project CHIP Authors
 *   All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#include <commands/example/ExampleCredentialIssuerCommands.h>

CHIP_ERROR ExampleCredentialIssuerCommands::InitializeCredentialsIssuer(chip::PersistentStorageDelegate & storage)
{
    return mOpCredsIssuer.Initialize(storage);
}

CHIP_ERROR ExampleCredentialIssuerCommands::SetupDeviceAttestation(chip::Controller::SetupParams & setupParams)
{
    chip::Credentials::SetDeviceAttestationCredentialsProvider(chip::Credentials::Examples::GetExampleDACProvider());

    // TODO: Replace testingRootStore with a AttestationTrustStore that has the necessary official PAA roots available
    const chip::Credentials::AttestationTrustStore * testingRootStore = chip::Credentials::GetTestAttestationTrustStore();
    setupParams.deviceAttestationVerifier = chip::Credentials::GetDefaultDACVerifier(testingRootStore);

    return CHIP_NO_ERROR;
}

chip::Controller::OperationalCredentialsDelegate * ExampleCredentialIssuerCommands::GetCredentialIssuer()
{
    return &mOpCredsIssuer;
}

CHIP_ERROR ExampleCredentialIssuerCommands::GenerateControllerNOCChain(chip::NodeId nodeId,
                                                                       chip::FabricId fabricId,
                                                                       chip::Crypto::P256Keypair & keypair,
                                                                       chip::MutableByteSpan & rcac,
                                                                       chip::MutableByteSpan & icac,
                                                                      chip::MutableByteSpan & noc)
{
    return mOpCredsIssuer.GenerateNOCChainAfterValidation(nodeId, fabricId, keypair.Pubkey(), rcac, icac, noc);
}
