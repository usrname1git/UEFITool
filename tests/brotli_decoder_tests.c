/* Include the production decoder to exercise its static block-switch guards
   without adding test hooks to the vendored code. */
#include "../common/brotli/dec/decode.c"

int testBrotliBlockSwitch(void)
{
    BrotliDecoderState state;
    HuffmanCode typeTrees[3 * BROTLI_HUFFMAN_MAX_SIZE_258];
    HuffmanCode lengthTrees[3 * BROTLI_HUFFMAN_MAX_SIZE_26];
    int failures = 0;
    int type, safe, count;
    memset(&state, 0, sizeof(state));
    state.block_type_trees = typeTrees;
    state.block_len_trees = lengthTrees;
    for (count = 0; count <= 1; count++) {
        for (type = 0; type < 3; type++) {
            state.num_block_types[type] = (brotli_reg_t)count;
            for (safe = 0; safe <= 1; safe++)
                if (DecodeBlockTypeAndLength(safe, &state, type) != BROTLI_DECODER_ERROR_FORMAT_BLOCK_SWITCH)
                    failures++;
        }
        for (safe = 0; safe <= 1; safe++) {
            if (DecodeLiteralBlockSwitchInternal(safe, &state) != BROTLI_DECODER_ERROR_FORMAT_BLOCK_SWITCH)
                failures++;
            if (DecodeCommandBlockSwitchInternal(safe, &state) != BROTLI_DECODER_ERROR_FORMAT_BLOCK_SWITCH)
                failures++;
            if (DecodeDistanceBlockSwitchInternal(safe, &state) != BROTLI_DECODER_ERROR_FORMAT_BLOCK_SWITCH)
                failures++;
        }
    }
    return failures;
}
