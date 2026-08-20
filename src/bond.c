#include "bond.h"

#include <stdbool.h>
#include <string.h>

enum {
    BOND_OK = 0,
    BOND_BAD_INPUT = -1,
    BOND_BAD_SIZE = -2,
    BOND_OUTPUT_SMALL = -3,
    BOND_INPUT_UNDERFLOW = -4,
    BOND_OUTPUT_OVERFLOW = -5,
    BOND_BAD_MATCH = -6,
    BOND_BAD_CHECKSUM = -7,
};

typedef struct {
    const uint8_t *packed;
    size_t input;
    uint8_t *output;
    size_t output_length;
    size_t destination;
    uint32_t checksum;
    uint32_t bits;
    int error;
} BondState;

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static uint32_t read_long(BondState *state)
{
    if (state->input < 4) {
        state->error = BOND_INPUT_UNDERFLOW;
        return 0;
    }
    state->input -= 4;
    return read_be32(state->packed + state->input);
}

static unsigned shift_bit(BondState *state)
{
    unsigned carry = state->bits & 1;
    state->bits >>= 1;
    if (!state->bits && !state->error) {
        uint32_t reservoir = read_long(state);
        state->checksum ^= reservoir;
        carry = reservoir & 1;
        state->bits = UINT32_C(0x80000000) | (reservoir >> 1);
    }
    return carry;
}

static uint32_t get_bits(BondState *state, unsigned count)
{
    uint32_t value = 0;
    while (count-- && !state->error)
        value = (value << 1) | shift_bit(state);
    return value;
}

static void literal(BondState *state, size_t count)
{
    if (count > state->destination) {
        state->error = BOND_OUTPUT_OVERFLOW;
        return;
    }
    while (count-- && !state->error)
        state->output[--state->destination] = (uint8_t)get_bits(state, 8);
}

static void copy_match(BondState *state, size_t count, size_t offset)
{
    if (count > state->destination) {
        state->error = BOND_OUTPUT_OVERFLOW;
        return;
    }
    while (count-- && !state->error) {
        size_t destination = --state->destination;
        size_t source = destination + offset;
        if (source <= destination || source >= state->output_length) {
            state->error = BOND_BAD_MATCH;
            return;
        }
        state->output[destination] = state->output[source];
    }
}

int bs_bond_output_size(const uint8_t *packed, size_t packed_size,
                        size_t *output_size)
{
    if (!packed || !output_size || packed_size < 12 || (packed_size & 1))
        return BOND_BAD_INPUT;
    size_t size = read_be32(packed + packed_size - 4);
    if (!size || size > 16 * 1024 * 1024)
        return BOND_BAD_SIZE;
    *output_size = size;
    return BOND_OK;
}

int bs_bond_depack(const uint8_t *packed, size_t packed_size,
                   uint8_t *output, size_t output_capacity,
                   size_t *output_size)
{
    size_t length = 0;
    int error = bs_bond_output_size(packed, packed_size, &length);
    if (error) return error;
    if (!output || output_capacity < length) return BOND_OUTPUT_SMALL;

    BondState state = {
        .packed = packed,
        .input = packed_size,
        .output = output,
        .output_length = length,
        .destination = length,
    };
    memset(output, 0, length);
    (void)read_long(&state);          /* unpacked length footer */
    state.checksum = read_long(&state);
    state.bits = read_long(&state);
    state.checksum ^= state.bits;

    while (state.destination && !state.error) {
        if (shift_bit(&state)) {
            unsigned selector = get_bits(&state, 2);
            if (selector < 2) {
                copy_match(&state, selector + 3,
                           get_bits(&state, 9 + selector));
            } else if (selector == 2) {
                size_t count = get_bits(&state, 8) + 1;
                copy_match(&state, count, get_bits(&state, 12));
            } else {
                literal(&state, get_bits(&state, 8) + 9);
            }
        } else if (shift_bit(&state)) {
            copy_match(&state, 2, get_bits(&state, 8));
        } else {
            literal(&state, get_bits(&state, 3) + 1);
        }
    }
    if (state.error) return state.error;
    if (state.checksum) return BOND_BAD_CHECKSUM;
    if (output_size) *output_size = length;
    return BOND_OK;
}

const char *bs_bond_error(int error)
{
    switch (error) {
    case BOND_OK: return "ok";
    case BOND_BAD_INPUT: return "invalid or unaligned input";
    case BOND_BAD_SIZE: return "implausible output size";
    case BOND_OUTPUT_SMALL: return "output buffer too small";
    case BOND_INPUT_UNDERFLOW: return "input underflow";
    case BOND_OUTPUT_OVERFLOW: return "output overflow";
    case BOND_BAD_MATCH: return "invalid backward match";
    case BOND_BAD_CHECKSUM: return "checksum mismatch";
    default: return "unknown BOND error";
    }
}
