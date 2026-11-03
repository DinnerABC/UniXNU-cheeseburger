// SPDX-License-Identifier: MIT
// Cross-build witness for the native C API's IOKit/C++ call boundary.
#include "../ConstantUploads.h"
extern "C" air_constant_result air_constant_cpp_client(const air_constant_layout *layout,
                                                       const air_constant_binding *bindings,
                                                       uint8_t *destination, size_t bytes) {
    return air_pack_constants(layout, bindings, destination, bytes);
}
