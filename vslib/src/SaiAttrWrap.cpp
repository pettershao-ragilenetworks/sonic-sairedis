#include "SaiAttrWrap.h"

#include "swss/logger.h"
#include "meta/sai_serialize.h"

using namespace saivs;

SaiAttrWrap::SaiAttrWrap(
        _In_ sai_object_type_t object_type,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    m_meta = sai_metadata_get_attr_metadata(object_type, attr->id);

    m_attr.id = attr->id;

    /*
     * We are making serialize and deserialize to get copy of
     * attribute, it may be a list so we need to allocate new memory.
     *
     * This copy will be used later to get previous value of attribute
     * if attribute will be updated. And if this attribute is oid list
     * then we need to release object reference count.
     */

    m_value = sai_serialize_attr_value(*m_meta, *attr, false);

    sai_deserialize_attr_value(m_value, *m_meta, m_attr, false);
}

SaiAttrWrap::~SaiAttrWrap()
{
    SWSS_LOG_ENTER();

    /*
     * On destructor we need to call free to deallocate possible
     * allocated list on constructor.
     */

    sai_deserialize_free_attribute_value(m_meta->attrvaluetype, m_attr);
}

const sai_attribute_t* SaiAttrWrap::getAttr() const
{
    SWSS_LOG_ENTER();

    return &m_attr;
}

const sai_attr_metadata_t* SaiAttrWrap::getAttrMetadata() const
{
    SWSS_LOG_ENTER();

    return m_meta;
}

const std::string& SaiAttrWrap::getAttrStrValue() const
{
    SWSS_LOG_ENTER();

    return m_value;
}

