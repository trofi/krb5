#include "k5-int.h"
#include "des_int.h"
#include "rsa-md4.h"
#include "keyhash_provider.h"

#define CONFLENGTH 8

static mit_des_cblock mit_des_zeroblock[8] = {0,0,0,0,0,0,0,0};

static void
k5_md4des_hash_size(size_t *output)
{
    *output = CONFLENGTH+RSA_MD4_CKSUM_LENGTH;
}

/* des-cbc(xorkey, conf | rsa-md4(conf | data)) */

/* this could be done in terms of the md4 and des providers, but
   that's less efficient, and there's no need for this to be generic */

static krb5_error_code
k5_md4des_hash(krb5_const krb5_keyblock *key, krb5_const krb5_data *ivec,
	       krb5_const krb5_data *input, krb5_data *output)
{
    krb5_error_code ret;
    krb5_data data;
    krb5_MD4_CTX ctx;
    unsigned char conf[CONFLENGTH];
    unsigned char xorkey[8];
    int i;
    mit_des_key_schedule schedule;

    if (key->length != 8)
	return(KRB5_BAD_KEYSIZE);
    if (ivec)
	return(KRB5_CRYPTO_INTERNAL);
    if (output->length != (CONFLENGTH+RSA_MD4_CKSUM_LENGTH))
	return(KRB5_CRYPTO_INTERNAL);

    /* create the confouder */

    data.length = CONFLENGTH;
    data.data = conf;
    if (ret = krb5_c_random_make_octets(/* XXX */ 0, &data))
	return(ret);

    /* create and schedule the encryption key */

    memcpy(xorkey, key->contents, sizeof(xorkey));
    for (i=0; i<sizeof(xorkey); i++)
	xorkey[i] ^= 0xf0;
    
    switch (ret = mit_des_key_sched(xorkey, schedule)) {
    case -1:
	return(KRB5DES_BAD_KEYPAR);
    case -2:
	return(KRB5DES_WEAK_KEY);
    }

    /* hash the confounder, then the input data */

    krb5_MD4Init(&ctx);
    krb5_MD4Update(&ctx, conf, CONFLENGTH);
    krb5_MD4Update(&ctx, input->data, input->length);
    krb5_MD4Final(&ctx);

    /* construct the buffer to be encrypted */

    memcpy(output->data, conf, CONFLENGTH);
    memcpy(output->data+CONFLENGTH, ctx.digest, RSA_MD4_CKSUM_LENGTH);

    /* encrypt it, in place.  this has a return value, but it's
       always zero.  */

    mit_des_cbc_encrypt((krb5_pointer) output->data,
			(krb5_pointer) output->data, output->length,
			schedule, (char *) mit_des_zeroblock, 1);

    return(0);
}

static krb5_error_code
k5_md4des_verify(krb5_const krb5_keyblock *key, krb5_const krb5_data *ivec,
		 krb5_const krb5_data *input, krb5_const krb5_data *hash,
		 krb5_boolean *valid)
{
    krb5_error_code ret;
    krb5_data data;
    krb5_MD4_CTX ctx;
    unsigned char plaintext[CONFLENGTH+RSA_MD4_CKSUM_LENGTH];
    unsigned char xorkey[8];
    int i;
    mit_des_key_schedule schedule;

    if (key->length != 8)
	return(KRB5_BAD_KEYSIZE);
    if (ivec)
	return(KRB5_CRYPTO_INTERNAL);
    if (hash->length != (CONFLENGTH+RSA_MD4_CKSUM_LENGTH))
	return(KRB5_CRYPTO_INTERNAL);

    /* create and schedule the encryption key */

    memcpy(xorkey, key->contents, sizeof(xorkey));
    for (i=0; i<sizeof(xorkey); i++)
	xorkey[i] ^= 0xf0;
    
    switch (ret = mit_des_key_sched(xorkey, schedule)) {
    case -1:
	return(KRB5DES_BAD_KEYPAR);
    case -2:
	return(KRB5DES_WEAK_KEY);
    }

    /* decrypt it.  this has a return value, but it's always zero.  */

    mit_des_cbc_encrypt((krb5_pointer) hash->data,
			(krb5_pointer) plaintext, sizeof(plaintext),
			schedule, (char *) mit_des_zeroblock, 0);

    /* hash the confounder, then the input data */

    krb5_MD4Init(&ctx);
    krb5_MD4Update(&ctx, plaintext, CONFLENGTH);
    krb5_MD4Update(&ctx, input->data, input->length);
    krb5_MD4Final(&ctx);

    /* compare the decrypted hash to the computed one */

    *valid =
	(memcmp(plaintext+CONFLENGTH, ctx.digest, RSA_MD4_CKSUM_LENGTH) == 0);

    memset(plaintext, 0, sizeof(plaintext));

    return(0);
}

struct krb5_keyhash_provider krb5_keyhash_md4des = {
    k5_md4des_hash_size,
    k5_md4des_hash,
    k5_md4des_verify
};
