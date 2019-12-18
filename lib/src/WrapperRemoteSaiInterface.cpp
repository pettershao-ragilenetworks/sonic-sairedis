#include "WrapperRemoteSaiInterface.h"

#include "swss/logger.h"
#include "meta/sai_serialize.h"

#include "SwitchContainer.h"
#include "VirtualObjectIdManager.h"
#include "Recorder.h"

#include <memory>

using namespace sairedis;

// TODO to be moved to members
extern std::shared_ptr<SwitchContainer>            g_switchContainer;
extern std::shared_ptr<VirtualObjectIdManager>     g_virtualObjectIdManager;
extern std::shared_ptr<Recorder>                   g_recorder;

void clear_oid_values(
        _In_ sai_object_type_t object_type,
        _In_ uint32_t attr_count,
        _Out_ sai_attribute_t *attr_list);

WrapperRemoteSaiInterface::WrapperRemoteSaiInterface(
        _In_ std::shared_ptr<RemoteSaiInterface> impl):
    m_implementation(impl)
{
    SWSS_LOG_ENTER();

    // empty
}

sai_status_t WrapperRemoteSaiInterface::create(
        _In_ sai_object_type_t objectType,
        _Out_ sai_object_id_t* objectId,
        _In_ sai_object_id_t switchId,
        _In_ uint32_t attr_count,
        _In_ const sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    *objectId = SAI_NULL_OBJECT_ID;

    // TODO allocating new object ID should be inside implementation but here
    // we need it also for recording, if we move recording inside
    // implementation then we don't need wrapper

    // on create vid is put in db by syncd
    *objectId = g_virtualObjectIdManager->allocateNewObjectId(objectType, switchId);

    if (*objectId == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_ERROR("failed to create %s, with switch id: %s",
                sai_serialize_object_type(objectType).c_str(),
                sai_serialize_object_id(switchId).c_str());

        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }

    // objectId must be already allocated when we want to record operation

    g_recorder->recordGenericCreate(objectType, *objectId, attr_count, attr_list);

    auto status = m_implementation->create(objectType, objectId, switchId, attr_count, attr_list);

    g_recorder->recordGenericCreateResponse(status, *objectId);

    if (objectType == SAI_OBJECT_TYPE_SWITCH && status == SAI_STATUS_SUCCESS)
    {
        /*
         * When doing CREATE operation user may want to update notification
         * pointers, since notifications can be defined per switch we need to
         * update them.
         *
         * TODO: should be moved inside to redis_generic_create
         */

        auto sw = std::make_shared<Switch>(*objectId, attr_count, attr_list);

        g_switchContainer->insert(sw);
    }

    return status;
}

sai_status_t WrapperRemoteSaiInterface::remove(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId)
{
    SWSS_LOG_ENTER();

    g_recorder->recordGenericRemove(objectType, objectId);

    auto status = m_implementation->remove(objectType, objectId);

    g_recorder->recordGenericRemoveResponse(status);

    if (objectType == SAI_OBJECT_TYPE_SWITCH && status == SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_NOTICE("removing switch id %s", sai_serialize_object_id(objectId).c_str());

        g_virtualObjectIdManager->releaseObjectId(objectId);

        // TODO do we need some more actions here ? to clean all
        // objects that are in the same switch that were snooped
        // inside metadata ? should that be metadata job?

        // remove switch from container
        g_switchContainer->removeSwitch(objectId);
    }

    return status;
}

sai_status_t WrapperRemoteSaiInterface::set(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ const sai_attribute_t *attr)
{
    SWSS_LOG_ENTER();

    g_recorder->recordGenericSet(objectType, objectId, attr);

    auto status = m_implementation->set(objectType, objectId, attr);

    g_recorder->recordGenericSetResponse(status);

    if (objectType == SAI_OBJECT_TYPE_SWITCH && status == SAI_STATUS_SUCCESS)
    {
        auto sw = g_switchContainer->getSwitch(objectId);

        if (!sw)
        {
            SWSS_LOG_THROW("failed to find switch %s in container",
                    sai_serialize_object_id(objectId).c_str());
        }

        /*
         * When doing SET operation user may want to update notification
         * pointers.
         */
        sw->updateNotifications(1, attr);
    }

    return status;
}

sai_status_t WrapperRemoteSaiInterface::get(
        _In_ sai_object_type_t objectType,
        _In_ sai_object_id_t objectId,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
{
    SWSS_LOG_ENTER();

    clear_oid_values(objectType, attr_count, attr_list);

    g_recorder->recordGenericGet(objectType, objectId, attr_count, attr_list);

    auto status = m_implementation->get(objectType, objectId, attr_count, attr_list);

    g_recorder->recordGenericGetResponse(status, objectType, attr_count, attr_list);

    return status;
}

#define DECLARE_REMOVE_ENTRY(OT,ot)                 \
sai_status_t WrapperRemoteSaiInterface::remove(     \
        _In_ const sai_ ## ot ## _t* ot)            \
{                                                   \
    SWSS_LOG_ENTER();                               \
    g_recorder->recordRemove(ot);                   \
    auto status = m_implementation->remove(ot);     \
    g_recorder->recordGenericRemoveResponse(status);\
    return status;                                  \
}

DECLARE_REMOVE_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_REMOVE_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_REMOVE_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_REMOVE_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_REMOVE_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_REMOVE_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_REMOVE_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_REMOVE_ENTRY(NAT_ENTRY,nat_entry);

#define DECLARE_CREATE_ENTRY(OT,ot)                                         \
sai_status_t WrapperRemoteSaiInterface::create(                             \
        _In_ const sai_ ## ot ## _t* ot,                                    \
        _In_ uint32_t attr_count,                                           \
        _In_ const sai_attribute_t *attr_list)                              \
{                                                                           \
    SWSS_LOG_ENTER();                                                       \
    g_recorder->recordCreate(ot, attr_count, attr_list);                    \
    auto status = m_implementation->create(ot, attr_count, attr_list);      \
    g_recorder->recordGenericCreateResponse(status);                        \
    return status;                                                          \
}

DECLARE_CREATE_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_CREATE_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_CREATE_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_CREATE_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_CREATE_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_CREATE_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_CREATE_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_CREATE_ENTRY(NAT_ENTRY,nat_entry);


#define DECLARE_SET_ENTRY(OT,ot)                                            \
sai_status_t WrapperRemoteSaiInterface::set(                                \
        _In_ const sai_ ## ot ## _t* ot,                                    \
        _In_ const sai_attribute_t *attr)                                   \
{                                                                           \
    SWSS_LOG_ENTER();                                                       \
    g_recorder->recordSet(ot, attr);                                        \
    auto status = m_implementation->set(ot, attr);                          \
    g_recorder->recordGenericSetResponse(status);                           \
    return status;                                                          \
}

DECLARE_SET_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_SET_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_SET_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_SET_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_SET_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_SET_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_SET_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_SET_ENTRY(NAT_ENTRY,nat_entry);

    /*
     * Since user may reuse buffers, then oid list buffers maybe not cleared
     * and contain some garbage, let's clean them so we send all oids as null to
     * syncd.
     */

#define DECLARE_GET_ENTRY(OT,ot)                                            \
sai_status_t WrapperRemoteSaiInterface::get(                                \
        _In_ const sai_ ## ot ## _t* ot,                                    \
        _In_ uint32_t attr_count,                                           \
        _Inout_ sai_attribute_t *attr_list)                                 \
{                                                                           \
    SWSS_LOG_ENTER();                                                       \
    clear_oid_values(SAI_OBJECT_TYPE_ ## OT, attr_count, attr_list);        \
    g_recorder->recordGet(ot, attr_count, attr_list);                       \
    auto status = m_implementation->get(ot, attr_count, attr_list);         \
    g_recorder->recordGenericGetResponse(                                   \
            status,                                                         \
            SAI_OBJECT_TYPE_ ## OT,                                         \
            attr_count,                                                     \
            attr_list);                                                     \
    return status;                                                          \
}

DECLARE_GET_ENTRY(FDB_ENTRY,fdb_entry);
DECLARE_GET_ENTRY(INSEG_ENTRY,inseg_entry);
DECLARE_GET_ENTRY(IPMC_ENTRY,ipmc_entry);
DECLARE_GET_ENTRY(L2MC_ENTRY,l2mc_entry);
DECLARE_GET_ENTRY(MCAST_FDB_ENTRY,mcast_fdb_entry);
DECLARE_GET_ENTRY(NEIGHBOR_ENTRY,neighbor_entry);
DECLARE_GET_ENTRY(ROUTE_ENTRY,route_entry);
DECLARE_GET_ENTRY(NAT_ENTRY,nat_entry);


sai_status_t WrapperRemoteSaiInterface::flushFdbEntries(
        _In_ sai_object_id_t switchId,
        _In_ uint32_t attrCount,
        _In_ const sai_attribute_t *attrList)
{
    SWSS_LOG_ENTER();

    g_recorder->recordFlushFdbEntries(switchId, attrCount, attrList);

    auto status = m_implementation->flushFdbEntries(switchId, attrCount, attrList);

    g_recorder->recordFlushFdbEntriesResponse(status);

    return status;
}
