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
