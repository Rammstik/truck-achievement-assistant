#pragma once
#ifdef AA_GAME_ATS
#include "games/ats/TriggerMapTargets.h"
#else
#pragma once
namespace aa::maptint {
struct TriggerPosition {const char* goal;const char* uid;double x,y,z;};
inline constexpr TriggerPosition TriggerPositions[]={
{"be_landmarks","320320489A0B0001",47129.426,18.265625,32000.023},
{"be_border_crossings","32031E34B04B0002",49482.402,17.621094,34850.08},
{"be_landmarks","320321CCEC4B0000",56831.95,40.44922,26328.684},
{"be_border_crossings","32031D18494B0000",61317.78,17.992188,33355.676},
{"be_border_crossings","32031BB0DB8B0001",61887.133,63.835938,45713.965},
{"be_border_crossings","32031A40F6CB0001",65366.355,18.675781,44022.5},
{"be_blke_ferry","321910B4344B0001",68382.95,1.8828125,26994.93},
{"be_blke_ferry","321911803A4B0000",68081.7,1.8828125,26734.848},
{"be_border_crossings","32D1CF98360B0001",71295.61,19.589844,35060.46},
{"be_landmarks","320323C0BF4B0001",69833.14,6,39329.773},
{"be_landmarks","320324D0958B0001",69635.87,8.238281,36549.96},
{"be_landmarks","3203266CBB0B0001",75998.11,19.230469,48263.668},
{"fr_landmarks","06CAB869E4410000",-27918.172,8,31247.36},
{"fr_landmarks","06CAB478C2E10000",-34341.703,46.703125,2851.5781},
{"fr_landmarks","06CAAA81BC610000",-35704.02,33,23035.758},
{"fr_landmarks","06CAA51744C10000",-35160.875,30.636719,38475.445},
{"fr_landmarks","06CAB414EF510000",-39344.54,10.5625,5946.879},
{"fr_landmarks","06CAB8A8AD810000",-39914.72,34,14337.285},
{"it_roads_to_rome","22E4FBBC914B0000",6247.0312,23.035156,48030.086},
{"it_roads_to_rome","22E4FCD84E8B0000",7924.6875,6.96875,49326.22},
{"it_roads_to_rome","22E4FDDC278B0000",7414.6445,23.160156,50559.297},
{"it_roads_to_rome","22E4FEB05F8B0000",4860.4844,23.875,49974.945},
{"it_volcanoes","22E4F78407860001",13967.75,50,56153.582},
{"it_volcanoes","22E4F630110B0001",15570.238,20.554688,72511.125},
{"sc_oresund_bridge","5FA8E4C096060001",9356.133,23.980469,-26666.008},
};
}
#endif
