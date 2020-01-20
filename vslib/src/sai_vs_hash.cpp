#include "sai_vs.h"

VS_GENERIC_QUAD(HASH,hash);

const sai_hash_api_t vs_hash_api = {

    VS_GENERIC_QUAD_API(hash)
};
