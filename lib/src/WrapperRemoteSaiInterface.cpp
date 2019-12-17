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

WrapperRemoteSaiInterface::WrapperRemoteSaiInterface(
        _In_ std::shared_ptr<RemoteSaiInterface> impl):
    m_implementation(impl)
{
    SWSS_LOG_ENTER();

    // empty
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

