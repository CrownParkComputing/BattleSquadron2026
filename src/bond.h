#ifndef BATTLE_SQUADRON_RECOMP_BOND_H
#define BATTLE_SQUADRON_RECOMP_BOND_H

#include <stddef.h>
#include <stdint.h>

/* Native translation of LOADER:$AB46.  The routine consumes a BOND stream
 * backwards and writes the exact unpacked image into output. */
int bs_bond_output_size(const uint8_t *packed, size_t packed_size,
                        size_t *output_size);
int bs_bond_depack(const uint8_t *packed, size_t packed_size,
                   uint8_t *output, size_t output_capacity,
                   size_t *output_size);
const char *bs_bond_error(int error);

#endif
