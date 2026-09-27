#ifndef PEA_EAX_H
#define PEA_EAX_H

#include <stddef.h>
#include <stdint.h>

#define PEA_CRYPTO_SALT_LEN   12
#define PEA_CRYPTO_TAG_LEN    16
#define PEA_CRYPTO_HDR_LEN    16
#define PEA_CRYPTO_CASCADE_HDR_LEN 48
#define PEA_CRYPTO_CASCADE_TAG_LEN 48
#define PEA_EAX_SALT_LEN      PEA_CRYPTO_SALT_LEN
#define PEA_EAX_TAG_LEN       PEA_CRYPTO_TAG_LEN
#define PEA_EAX_HDR_LEN       PEA_CRYPTO_HDR_LEN
#define PEA_KEYFILE_MAX       2048
#define PEA_CRYPTO_PBKDF2_ITER 1000u

typedef struct pea_crypto pea_crypto;

pea_crypto *pea_crypto_new(void);
void        pea_crypto_free(pea_crypto *c);

int pea_crypto_init_encrypt(pea_crypto *c, int algo,
                            const uint8_t *pw, size_t pwlen,
                            uint8_t niter, uint8_t *subhdr);
int pea_crypto_init_decrypt(pea_crypto *c, int algo,
                            const uint8_t *pw, size_t pwlen,
                            uint8_t niter, const uint8_t *subhdr);
int pea_crypto_encrypt(pea_crypto *c, void *buf, size_t n);
int pea_crypto_decrypt(pea_crypto *c, void *buf, size_t n);
int pea_crypto_final(pea_crypto *c, uint8_t *tag);

int pea_make_pw_material(const char *password, const char *keyfile,
                         const uint8_t hdr[10], const uint8_t shdr[10],
                         uint8_t **out, size_t *outlen);

#endif
