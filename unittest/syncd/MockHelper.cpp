#include "VidManager.h"
#include "swss/dbconnector.h"
#include "swss/table.h"

namespace test_syncd {
    sai_object_type_t mock_objectTypeQuery_result;
    void mockVidManagerObjectTypeQuery(sai_object_type_t mock_result)
    {
        SWSS_LOG_ENTER();
        mock_objectTypeQuery_result = mock_result;
    }
}
