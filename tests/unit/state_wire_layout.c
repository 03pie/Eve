/* Independent C11 layout proof for the optional extension, not a C++ typedef. */
#include <cordis_abi/state.h>
#include <stddef.h>
_Static_assert(sizeof(void*) == 8, "state ABI matrix is x64");
_Static_assert(sizeof(CordisStateSchemaV1) == 144, "schema layout");
_Static_assert(offsetof(CordisStateSchemaV1, id) == 16, "schema bytes");
_Static_assert(sizeof(CordisStateBufferV1) == 32, "buffer layout");
_Static_assert(offsetof(CordisStateBufferV1, data) == 8, "buffer pointer");
_Static_assert(offsetof(CordisStateBufferV1, size) == 24, "buffer length");
_Static_assert(sizeof(CordisStateApiV1) == 48, "extension table layout");
_Static_assert(offsetof(CordisStateApiV1, describe) == 16, "extension prefix");
int stateWireLayout(void)
{
    return 1;
}
