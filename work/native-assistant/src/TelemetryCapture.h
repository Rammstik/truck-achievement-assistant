#pragma once
#include "scssdk_telemetry.h"
#include "json.hpp"
#include <cmath>
namespace aa {
inline nlohmann::json attributes(const scs_named_value_t* values) {
    auto out=nlohmann::json::array();
    if(!values)return out;
    for(size_t i=0;i<1024&&values[i].name;++i) {
        const auto& a=values[i];const auto& v=a.value;nlohmann::json value;
        switch(v.type) {
        case SCS_VALUE_TYPE_bool:value=(v.value_bool.value!=0);break;
        case SCS_VALUE_TYPE_s32:value=v.value_s32.value;break;
        case SCS_VALUE_TYPE_u32:value=v.value_u32.value;break;
        case SCS_VALUE_TYPE_s64:value=std::to_string(v.value_s64.value);break;
        case SCS_VALUE_TYPE_u64:value=std::to_string(v.value_u64.value);break;
        case SCS_VALUE_TYPE_float:if(std::isfinite(v.value_float.value))value=v.value_float.value;break;
        case SCS_VALUE_TYPE_double:if(std::isfinite(v.value_double.value))value=v.value_double.value;break;
        case SCS_VALUE_TYPE_string:if(v.value_string.value)value=std::string(v.value_string.value);break;
        default:break;
        }
        out.push_back({{"name",a.name},{"index",a.index},{"type",v.type},{"value",value}});
    }
    return out;
}
}
