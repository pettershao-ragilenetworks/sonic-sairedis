#include "SaiObjectCollection.h"
#include "Globals.h"

#include "sai_serialize.h"

using namespace saimeta;

void SaiObjectCollection::clear()
{
    SWSS_LOG_ENTER();

    m_objects.clear();
}

bool SaiObjectCollection::objectExists(
        _In_ const std::string& key) const
{
    SWSS_LOG_ENTER();

    bool exists = m_objects.find(key) != m_objects.end();

    SWSS_LOG_DEBUG("%s %s", key.c_str(), exists ? "exists" : "missing");

    return exists;
}

bool SaiObjectCollection::objectExists(
        _In_ const sai_object_meta_key_t& metaKey) const
{
    SWSS_LOG_ENTER();

    std::string key = sai_serialize_object_meta_key(metaKey);

    return objectExists(key);
}

void SaiObjectCollection::createObject(
        _In_ const sai_object_meta_key_t& metaKey)
{
    SWSS_LOG_ENTER();

    auto obj = std::make_shared<SaiObject>(metaKey);

    auto& key = obj->getStrMetaKey();

    if (objectExists(key))
    {
        SWSS_LOG_THROW("FATAL: object %s already exists", key.c_str());
    }

    SWSS_LOG_DEBUG("creating object %s", key.c_str());

    m_objects[key] = obj;
}

void SaiObjectCollection::removeObject(
        _In_ const sai_object_meta_key_t& metaKey)
{
    SWSS_LOG_ENTER();

    std::string key = sai_serialize_object_meta_key(metaKey);

    if (!objectExists(key))
    {
        SWSS_LOG_THROW("FATAL: object %s doesn't exist", key.c_str());
    }

    SWSS_LOG_DEBUG("removing object %s", key.c_str());

    m_objects.erase(key);
}

void SaiObjectCollection::setObjectAttr(
        _In_ const sai_object_meta_key_t& metaKey,
        _In_ const sai_attr_metadata_t& md,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    std::string key = sai_serialize_object_meta_key(metaKey);

    if (!objectExists(key))
    {
        SWSS_LOG_THROW("FATAL: object %s doesn't exist", key.c_str());
    }

    META_LOG_DEBUG(md, "set attribute %d on %s", attr->id, key.c_str());

    m_objects[key]->setAttr(&md, attr);
}

std::shared_ptr<SaiAttrWrapper> SaiObjectCollection::getObjectAttr(
        _In_ const sai_object_meta_key_t& meta_key,
        _In_ sai_attr_id_t id)
{
    SWSS_LOG_ENTER();

    std::string key = sai_serialize_object_meta_key(meta_key);

    /*
     * We can't throw if object don't exists, since we can call this function
     * on create API, and then previous object will not exists, of couse we
     * should make exists check before.
     */

    auto it = m_objects.find(key);

    if (it == m_objects.end())
    {
        SWSS_LOG_ERROR("object key %s not found", key.c_str());

        return nullptr;
    }

    return it->second->getAttr(id);
}

std::vector<std::shared_ptr<SaiObject>> SaiObjectCollection::getObjectsByObjectType(
        _In_ sai_object_type_t objectType)
{
    SWSS_LOG_ENTER();

    std::vector<std::shared_ptr<SaiObject>> vec;

    for (auto& kvp: m_objects)
    {
        if (kvp.second->getObjectType() == objectType)
            vec.push_back(kvp.second);
    }

    return vec;
}

std::shared_ptr<SaiObject> SaiObjectCollection::getObject(
        _In_ const sai_object_meta_key_t& metaKey) const
{
    SWSS_LOG_ENTER();

    std::string key = sai_serialize_object_meta_key(metaKey);

    if (!objectExists(key))
    {
        SWSS_LOG_THROW("FATAL: object %s doesn't exist", key.c_str());
    }

    return m_objects.at(key);
}

