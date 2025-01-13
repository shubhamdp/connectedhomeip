# Revoked Attestation Certificates

This directory contains test certificates, keys, and CRLs for device attestation
revocation testing scenarios. The test certificates and keys are intended to be
used for testing purposes only and should not be used in production
environments.

## Direct CRL Signing

In this approach, the CA directly signs the Certificate Revocation List (CRL).

### PAA Signed CRL

Process:

1. PAA issues the PAI
2. PAI issues the DAC
3. PAA revokes the PAI and updates the CRL

-   PAA: `Chip-Test-PAA-FFF1-Cert.[pem|der]`
-   CRL: `Chip-Test-PAA-FFF1-CRL.[der|pem]`
-   PAI(revoked): `Chip-Test-PAI-FFF1-noPID-Revoked-Cert.[pem|der]`
-   DAC(signed by revoked PAI):
    `Chip-Test-DAC-FFF1-8001-Signed-By-Revoked-PAI-Cert.[pem|der]`

### PAI Signed CRL

Process:

1. PAI issues the 3 DACs
2. PAI issues the DAC and updates the CRL

-   PAI: `Matter-Development-PAI-FFF1-noPID-Cert.[pem|der]`
-   CRL: `Matter-Development-PAI-FFF1-noPID-CRL.[pem|der]`
-   DACs(revoked):
    -   `Matter-Development-DAC-FFF1-8001-Revoked-01-Cert.[pem|der]`
    -   `Matter-Development-DAC-FFF1-8002-Revoked-02-Cert.[pem|der]`
    -   `Matter-Development-DAC-FFF1-8003-Revoked-03-Cert.[pem|der]`

## Indirect CRL Signing (delegated CRL signing)

In this approach, the CA delegates the CRL signing responsibility to a separate
entity.

Please take an example PKI ![Indirect CRL Signing](indirect/indirect-crl.png).

-   PAA: `indirect/Chip-Test-PAA-FFF1-Cert.[pem|der]`
-   PAIs:
    -   `indirect/Chip-Test-PAI-FFF1-01-Cert.[pem|der]`
        -   DAC:
            `indirect/Chip-Test-DAC-FFF1-8001-Signed-By-Test-PAI-01-Cert.[pem|der]`
    -   `indirect/Chip-Test-PAI-FFF1-02-Cert.[pem|der]`
        -   DAC:
            `indirect/Chip-Test-DAC-FFF1-8002-Signed-By-Test-PAI-02-Cert.[pem|der]`
    -   `indirect/Chip-Test-PAI-FFF1-03-Cert.[pem|der]`
-   PAA Delegate: `indirect/Chip-Test-PAA-Delegate-FFF1-Cert.[pem|der]`
-   PAA Delegated CRL: `indirect/Chip-Test-PAA-FFF1-Delegated-CRL.[pem|der]`

-   PAI Delegate Key for all PAIs:
    `indirect/Chip-Test-PAI-Delegate-FFF1-Key.pem`
-   PAI Delegates:

    -   `indirect/Chip-Test-PAI-Delegate-FFF1-01-Cert.[pem|der]`
    -   `indirect/Chip-Test-PAI-Delegate-FFF1-02-Cert.[pem|der]`
    -   `indirect/Chip-Test-PAI-Delegate-FFF1-03-Cert.[pem|der]`

-   PAI Delegated CRL: `indirect/Chip-Test-PAI-FFF1-Delegated-CRL.[pem|der]`


## Sample revocation sets
Sample revocation set for each of the above scenarios can be found in the [revocation-sets](revocation-sets) directory.

Please find below the revocation sets for the respective CA:

| CA | CRL | CRL Signer | Delegator | Revocation Set |
| -- | --- | ---------- | --------- | -------------- |
| [Chip-Test-PAA-FFF1-Cert.pem](Chip-Test-PAA-FFF1-Cert.pem) | [Chip-Test-PAA-FFF1-CRL.pem](Chip-Test-PAA-FFF1-CRL.pem) | PAA | - | [direct-revocation-set-for-paa.json](revocation-sets/direct-revocation-set-for-paa.json) |
| [Chip-Test-PAA-FFF1-Cert.pem](Chip-Test-PAA-FFF1-Cert.pem) | [indirect/Chip-Test-PAA-FFF1-Delegated-CRL.pem](Chip-Test-PAA-FFF1-Delegated-CRL.pem) | [Chip-Test-PAA-Delegate-FFF1-Cert.pem](indirect/Chip-Test-PAA-Delegate-FFF1-Cert.pem) | PAA | [chip-test-paa-delegated-fff1-revocation-set.json](revocation-sets/chip-test-paa-delegated-fff1-revocation-set.json) |
| [Matter-Development-PAI-FFF1-noPID-Cert.pem](Matter-Development-PAI-FFF1-noPID-Cert.pem) | [Matter-Development-PAI-FFF1-noPID-CRL.pem](Matter-Development-PAI-FFF1-noPID-CRL.pem) | PAI | - | [direct-revocation-set-for-pai.json](revocation-set/direct-revocation-set-for-pai.json) |
| [Chip-Test-PAI-FFF1-01-Cert.pem](indirect/Chip-Test-PAI-FFF1-01-Cert.pem) | [Chip-Test-PAI-FFF1-Delegated-CRL.pem](indirect/Chip-Test-PAI-FFF1-Delegated-CRL.pem) | [Chip-Test-PAI-Delegate-FFF1-01-Cert.pem](indirect/Chip-Test-PAI-Delegate-FFF1-01-Cert.pem) | PAI | [chip-test-pai-01-delegated-fff1-revocation-set.json](revocation-set/chip-test-pai-01-delegated-fff1-revocation-set.json) |
| [Chip-Test-PAI-FFF1-02-Cert.pem](indirect/Chip-Test-PAI-FFF1-02-Cert.pem) | [Chip-Test-PAI-FFF1-Delegated-CRL.pem](indirect/Chip-Test-PAI-FFF1-Delegated-CRL.pem) | [Chip-Test-PAI-Delegate-FFF1-02-Cert.pem](indirect/Chip-Test-PAI-Delegate-FFF1-02-Cert.pem) | PAI | [chip-test-pai-02-delegated-fff1-revocation-set.json](revocation-set/chip-test-pai-02-delegated-fff1-revocation-set.json) |
| [Chip-Test-PAI-FFF1-03-Cert.pem](indirect/Chip-Test-PAI-FFF1-03-Cert.pem) | [Chip-Test-PAI-FFF1-Delegated-CRL.pem](indirect/Chip-Test-PAI-FFF1-Delegated-CRL.pem) | [Chip-Test-PAI-Delegate-FFF1-03-Cert.pem](indirect/Chip-Test-PAI-Delegate-FFF1-03-Cert.pem) | PAI | [chip-test-pai-03-delegated-fff1-revocation-set.json](revocation-set/chip-test-pai-03-delegated-fff1-revocation-set.json) |