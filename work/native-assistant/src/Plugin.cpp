#include "GameConfig.h"
#include "scssdk_telemetry.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "Overlay.h"
#include "TelemetryCapture.h"
#include <cstring>
#include <initializer_list>
static scs_log_t logger=nullptr;
static SCSAPI_VOID event(const scs_event_t type,const void* info,const scs_context_t) {
    if(type==SCS_TELEMETRY_EVENT_paused)AssistantPaused(true);
    if(type==SCS_TELEMETRY_EVENT_started)AssistantPaused(false);
    if(type==SCS_TELEMETRY_EVENT_configuration&&info) {
        const auto* value=static_cast<const scs_telemetry_configuration_t*>(info);
        try {auto payload=aa::attributes(value->attributes).dump();AssistantConfiguration(value->id,payload.c_str());} catch(...) {}
    }
    if(type==SCS_TELEMETRY_EVENT_gameplay&&info) {
        const auto* value=static_cast<const scs_telemetry_gameplay_event_t*>(info);
        try {auto payload=aa::attributes(value->attributes).dump();AssistantGameplay(value->id,payload.c_str());} catch(...) {}
    }
}
SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version,const scs_telemetry_init_params_t* const params) {
    if(version!=SCS_TELEMETRY_VERSION_1_01)return SCS_RESULT_unsupported;
    if(!params)return SCS_RESULT_invalid_parameter;
    const auto* p=static_cast<const scs_telemetry_init_params_v101_t*>(params);
    logger=p->common.log;
    if(std::strcmp(p->common.game_id,aa::game::SdkId)!=0)return SCS_RESULT_unsupported;
    AssistantPaused(true);
    for(auto type:{SCS_TELEMETRY_EVENT_paused,SCS_TELEMETRY_EVENT_started,SCS_TELEMETRY_EVENT_configuration,SCS_TELEMETRY_EVENT_gameplay}) {
        if(p->register_for_event(type,event,nullptr)!=SCS_RESULT_ok){
            for(auto registered:{SCS_TELEMETRY_EVENT_paused,SCS_TELEMETRY_EVENT_started,SCS_TELEMETRY_EVENT_configuration,SCS_TELEMETRY_EVENT_gameplay})p->unregister_from_event(registered);
            return SCS_RESULT_generic_error;
        }
    }
    if(!StartAssistant()) {
        for(auto registered:{SCS_TELEMETRY_EVENT_paused,SCS_TELEMETRY_EVENT_started,SCS_TELEMETRY_EVENT_configuration,SCS_TELEMETRY_EVENT_gameplay})p->unregister_from_event(registered);
        if(logger)logger(SCS_LOG_TYPE_error,"Achievement Assistant: could not start native overlay.");
        return SCS_RESULT_generic_error;
    }
    if(logger)logger(SCS_LOG_TYPE_message,aa::game::HintTitle);
    return SCS_RESULT_ok;
}
SCSAPI_VOID scs_telemetry_shutdown() { StopAssistant();logger=nullptr; }
