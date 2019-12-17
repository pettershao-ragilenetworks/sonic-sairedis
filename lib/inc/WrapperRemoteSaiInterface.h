#pragma once

#include "RemoteSaiInterface.h"

#include <memory>

namespace sairedis
{
    /**
     * @brief Wrapper remote SAI interface.
     *
     * Class will wrap actual SAI implementation and it will provide recording
     * and other operations required on top of actual SAI interface
     * implementation.
     * 
     * At this points implementation can be done in any way.
     *
     * Wrapper should be used as metadata argument to make sure that all
     * arguments passed to wrapper are valid and correct.
     */
    class WrapperRemoteSaiInterface:
        public RemoteSaiInterface
    {
        public:

            WrapperRemoteSaiInterface(
                    _In_ std::shared_ptr<RemoteSaiInterface> impl);

            virtual ~WrapperRemoteSaiInterface() = default;

        public: // SAI interface overrides

            virtual sai_status_t remove(
                    _In_ sai_object_type_t objectType,
                    _In_ sai_object_id_t objectId) override;

        private:

            std::shared_ptr<RemoteSaiInterface> m_implementation;
    };
}
