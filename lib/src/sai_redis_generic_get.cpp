#include "sai_redis.h"
#include "meta/sai_serialize.h"
#include "meta/saiattributelist.h"

std::string getSelectResultAsString(int result)
{
    SWSS_LOG_ENTER();

    std::string res;

    switch (result)
    {
        case swss::Select::ERROR:
            res = "ERROR";
            break;

        case swss::Select::TIMEOUT:
            res = "TIMEOUT";
            break;

        default:
            SWSS_LOG_WARN("non recognized select result: %d", result);
            res = std::to_string(result);
            break;
    }

    return res;
}

void clear_oid_values(
        _In_ sai_object_type_t object_type,
        _In_ uint32_t attr_count,
        _Out_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    for (uint32_t i = 0; i < attr_count; i++)
    {
        sai_attribute_t &attr = attr_list[i];

        auto meta = sai_metadata_get_attr_metadata(object_type, attr.id);

        if (meta == NULL)
        {
            SWSS_LOG_THROW("unable to get metadata for object type %x, attribute %d", object_type, attr.id);
        }

        sai_object_list_t list = { .count = 0, .list = NULL };

        switch (meta->attrvaluetype)
        {
            case SAI_ATTR_VALUE_TYPE_OBJECT_ID:
                attr.value.oid = SAI_NULL_OBJECT_ID;
                break;

            case SAI_ATTR_VALUE_TYPE_OBJECT_LIST:
                list = attr.value.objlist;
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_ID:
                attr.value.aclfield.data.oid = SAI_NULL_OBJECT_ID;
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_FIELD_DATA_OBJECT_LIST:
                if (attr.value.aclfield.enable)
                    list = attr.value.aclfield.data.objlist;
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_ID:
                    attr.value.aclaction.parameter.oid = SAI_NULL_OBJECT_ID;
                break;

            case SAI_ATTR_VALUE_TYPE_ACL_ACTION_DATA_OBJECT_LIST:
                if (attr.value.aclaction.enable)
                    list = attr.value.aclaction.parameter.objlist;
                break;

            default:

                /*
                 * If in future new attribute with object id will be added this
                 * will make sure that we will need to add handler here.
                 */

                /*
                 * TODO: Add metadata field for this.
                 */

                if (meta->allowedobjecttypeslength > 0)
                {
                    SWSS_LOG_THROW("attribute %s is object id, but not processed, FIXME", meta->attridname);
                }

                break;
        }

        for (uint32_t idx = 0; idx < list.count; ++idx)
        {
            /*
             * We can do that since list is pointer so actual data will be
             * changed in user buffer.
             */

            list.list[idx] = SAI_NULL_OBJECT_ID;
        }
    }
}
