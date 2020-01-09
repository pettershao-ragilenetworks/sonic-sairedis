#include "sai_vs.h"
#include "sai_vs_internal.h"

sai_status_t sai_dbg_generate_dump(
        _In_ const char *dump_file_name) 
{
    MUTEX();
    SWSS_LOG_ENTER();
    VS_CHECK_API_INITIALIZED();

    return SAI_STATUS_SUCCESS;
}
