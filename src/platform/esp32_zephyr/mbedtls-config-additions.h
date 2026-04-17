/*
 * Additional mbedTLS config for ESP32 Zephyr Matter platform.
 * These defines are needed by CHIP crypto but not automatically
 * enabled by Zephyr's config-tls-generic.h dependency chain.
 */

#ifndef MBEDTLS_OID_C
#define MBEDTLS_OID_C
#endif

#ifndef MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_PARSE_C
#endif

#ifndef MBEDTLS_PK_C
#define MBEDTLS_PK_C
#endif

#ifndef MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_ASN1_WRITE_C
#endif

#ifndef MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#endif

#ifndef MBEDTLS_X509_USE_C
#define MBEDTLS_X509_USE_C
#endif

#ifndef MBEDTLS_X509_CREATE_C
#define MBEDTLS_X509_CREATE_C
#endif

#ifndef MBEDTLS_BASE64_C
#define MBEDTLS_BASE64_C
#endif
