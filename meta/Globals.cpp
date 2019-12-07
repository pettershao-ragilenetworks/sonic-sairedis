#include "Globals.h"

#include "sai_serialize.h"

using namespace saimeta;

std::string Globals::getAttrInfo(
        _In_ const sai_attr_metadata_t& md)
{
    SWSS_LOG_ENTER();

    /*
     * Attribute name will contain object type as well so we don't need to
     * serialize object type separately.
     */

    return std::string(md.attridname) + ":" + sai_serialize_attr_value_type(md.attrvaluetype);
}


