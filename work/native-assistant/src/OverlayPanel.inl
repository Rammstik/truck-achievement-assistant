// Panel layout; included inside Overlay.cpp's private namespace.
std::string list_reveal_id;
ImVec4 status_color(const std::string& status) {
    if(status=="done")return ImVec4(.40f,.83f,.65f,1);
    if(status=="progress")return ImVec4(.95f,.74f,.39f,1);
    return ImVec4(.61f,.70f,.77f,1);
}
void source_information(const json& info,const json& achievement) {
    const auto mode=string_value(info,"mode");
    const bool estimated=info.value("estimated",false)||mode=="history"||mode=="sequence"||mode=="series";
    ImGui::Spacing();
    if(ImGui::CollapsingHeader(estimated?tr("Источник и точность · оценка по журналу"):tr("Источник и точность данных"))) {
        const auto group=string_value(achievement,"group");if(!group.empty())dim(tr("Набор: ")+group);
        for(const char* key:{"notice","history_notice","reason","caveat","source"}) {
            const auto value=string_value(info,key);if(!value.empty())dim(value);
        }
        if(estimated)dim(tr("История может быть неполной. Отсутствие записи не означает невыполнение."));
    }
}
void panel_settings(const json& current) {
    if(ImGui::IsPopupOpen("settings"))ImGui::SetNextWindowSize(ImVec2(std::min(540.f,ImGui::GetIO().DisplaySize.x-48),0));
    if(!ImGui::BeginPopup("settings"))return;
    ImGui::SeparatorText(tr("Язык"));
    const auto languageMode=string_value(preferences,"language","auto");
    const char* languageValues[]={"auto","ru","en"};
    const char* languageLabels[]={tr("Язык игры (автоматически)"),"Русский","English"};
    int selectedLanguage=languageMode=="ru"?1:languageMode=="en"?2:0;
    ImGui::SetNextItemWidth(-1);
    if(ImGui::Combo("##language",&selectedLanguage,languageLabels,3)) {
        preferences["language"]=languageValues[selectedLanguage];
        aa::i18n::SetLanguage(selectedLanguage==0?string_value(current,"game_language",aa::i18n::DefaultGameLanguage()):languageValues[selectedLanguage]);
        save_control();
    }
    dim(tr("Выбранный язык сохраняется между запусками."));ImGui::Spacing();
    ImGui::SeparatorText(tr("Профиль и данные"));
    const auto account=string_value(current,"account",string_value(preferences,"account"));
    const auto profile=string_value(current,"profile");std::string accountLabel=tr("Выберите аккаунт"),profileLabel=tr(aa::game::ProfilePrompt);
    if(current.contains("accounts"))for(const auto& a:current["accounts"])if(string_value(a,"id")==account)accountLabel=string_value(a,"name");
    if(current.contains("profiles"))for(const auto& a:current["profiles"])if(string_value(a,"id")==profile)profileLabel=string_value(a,"name")+(string_value(a,"storage")=="steam"?" · Steam Cloud":tr(" · локальный"));
    dim(tr("Аккаунт Steam"));ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##account",accountLabel.c_str())) {
        if(current.contains("accounts"))for(const auto& a:current["accounts"]) {
            const auto id=string_value(a,"id");
            if(ImGui::Selectable(string_value(a,"name").c_str(),id==account)) {
                preferences["account"]=id;preferences["profile"]="";selected_id.clear();save_control();
                std::lock_guard lock(data_mutex);model={{"account",id},{"profile",""},{"accounts",current["accounts"]},{"profiles",json::array()},{"achievements",json::array()},{"notice",tr("Чтение выбранного аккаунта…")}};
            }
        }ImGui::EndCombo();
    }
    ImGui::Spacing();dim(tr("Игровой профиль"));ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##profile",profileLabel.c_str())) {
        if(current.contains("profiles"))for(const auto& a:current["profiles"]) {
            const auto id=string_value(a,"id");ImGui::PushID(id.c_str());
            const auto label=string_value(a,"name")+(string_value(a,"storage")=="steam"?" · Steam Cloud":tr(" · локальный"));
            if(ImGui::Selectable(label.c_str(),id==profile)) {
                preferences["account"]=account;preferences["profile"]=id;selected_id.clear();save_control();
                std::lock_guard lock(data_mutex);model["profile"]=id;model.erase("details");model.erase("details_error");model["notice"]=tr("Чтение выбранного профиля…");
            }ImGui::PopID();
        }ImGui::EndCombo();
    }
    ImGui::Spacing();ImGui::SeparatorText(tr("Окно помощника"));
    bool onStart=preferences.value("open_on_start",false);
    if(ImGui::Checkbox(tr("Открывать панель при запуске"),&onStart)){preferences["open_on_start"]=onStart;save_control();}
    dim(tr("Ctrl+F8 открывает и закрывает панель без паузы. Пока она открыта, мышь и клавиатура управляют помощником."));
    if(current.contains("details")) {
        ImGui::Spacing();dim(tr("Последнее сохранение: ")+string_value(current["details"],"save_name"));
        dim(tr("Пункты обновляются после сохранения игры."));
    }
    ImGui::EndPopup();
}
void panel_diagnostics() {
    if(ImGui::IsPopupOpen("diagnostics"))ImGui::SetNextWindowSize(ImVec2(std::min(640.f,ImGui::GetIO().DisplaySize.x-48),0));
    if(!ImGui::BeginPopup("diagnostics"))return;
    ImGui::SeparatorText(tr("Диагностика"));
    const auto probe=aa::mapprobe::Status(),tint=aa::maptint::Status(),jobs=aa::jobprobe::Status();
    text(tr("Карта: ")+string_value(probe,"reason"));
    if(probe.value("installed",false)) {
        if(ImGui::Button(tr("Собрать данные карты · 90 с")))aa::mapprobe::Arm();
        dim(probe.value("recording",false)?tr("Идёт запись карты"):tr("Запись карты выключена"));
        dim(tr("Снимков: ")+number(probe["written"]));
    }
    dim(tr("Целей карты: ")+number(tint["targets"])+tr(" · выделений: ")+number(tint["tinted"]));
    dim(tr("Заказы: ")+string_value(jobs,"reason"));
    dim(tr("Исключено предложений: ")+number(jobs["rejected_by_plan"])+tr(" · обновлений: ")+number(jobs["refresh_requests"]));
    {std::lock_guard lock(event_mutex);dim("SDK: "+(last_event.empty()?std::string(tr("подключён")):last_event)+tr(" · событий: ")+std::to_string(telemetry_written));
     if(!telemetry_error.empty())text(telemetry_error);if(telemetry_dropped)text(tr("Пропущено событий: ")+std::to_string(telemetry_dropped));}
    ImGui::EndPopup();
}
void draw_panel() {
    json current;std::string error;{std::lock_guard lock(data_mutex);current=model;error=runtime_error;}
    auto& io=ImGui::GetIO();io.MouseDrawCursor=visible;
    if(!visible) {
        if(GetTickCount64()-first_render<15000) {
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x*.5f,18),ImGuiCond_Always,ImVec2(.5f,0));
            ImGui::SetNextWindowSize(ImVec2(std::min(680.f,std::max(240.f,io.DisplaySize.x-32)),88),ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(.97f);
            ImGui::Begin("AA hint",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::TextUnformatted(tr(aa::game::HintTitle));ImGui::Spacing();dim(tr("Ctrl+F8 — открыть или закрыть панель"));ImGui::End();
        }return;
    }
    const float maxW=std::max(320.f,io.DisplaySize.x-32),maxH=std::max(320.f,io.DisplaySize.y-32);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(760.f,maxW),std::min(560.f,maxH)),ImVec2(maxW,maxH));
    ImGui::SetNextWindowPos(ImVec2(16,16),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(1160.f,maxW),std::min(820.f,maxH)),ImGuiCond_FirstUseEver);
    if(!ImGui::Begin(tr(aa::game::PanelTitle),&visible,ImGuiWindowFlags_NoCollapse)){ImGui::End();return;}
    if(heading_font)ImGui::PushFont(heading_font);ImGui::TextUnformatted(tr("Достижения"));if(heading_font)ImGui::PopFont();
    const float controls=ImGui::GetWindowContentRegionMax().x-270;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX()+190,controls));
    if(ImGui::Button(tr("Обновить"),ImVec2(108,32)))save_control();
    ImGui::SameLine();if(ImGui::Button(tr("Настройки"),ImVec2(138,32)))ImGui::OpenPopup("settings");
    panel_settings(current);
    std::string profileLabel=tr("Выберите профиль в настройках");
    if(current.contains("profiles"))for(const auto& p:current["profiles"])if(string_value(p,"id")==string_value(current,"profile"))profileLabel=string_value(p,"name")+(string_value(p,"storage")=="steam"?" · Steam Cloud":tr(" · локальный профиль"));
    dim(profileLabel);
    if(!error.empty())ImGui::TextColored(ImVec4(1,.65f,.45f,1),"%s",error.c_str());
    if(current.contains("details_error"))text(string_value(current,"details_error"));
    if(current.contains("language")&&string_value(current,"language")!=aa::i18n::Language()) {
        dim(tr("Обновление языка…"));ImGui::End();return;
    }
    if(current.contains("counts")) {
        const auto& c=current["counts"];
        ImGui::TextColored(status_color("progress"),tr("В процессе: %s"),number(c["progress"]).c_str());ImGui::SameLine(0,22);
        ImGui::TextColored(status_color("done"),tr("Получено: %s"),number(c["done"]).c_str());ImGui::SameLine(0,22);dim(tr("Не получено: ")+number(c["locked"]));
    }
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    const char* achievementFilters[]={tr("Неполученные"),tr("В процессе"),tr("Мой план"),tr("Полученные"),tr("Все достижения")};
    ImGui::SetNextItemWidth(218);if(ImGui::Combo("##show",&filter,achievementFilters,5)){preferences["achievement_view"]=filter;save_control();}
    ImGui::SameLine();ImGui::SetNextItemWidth(-44);ImGui::InputTextWithHint("##search",tr("Поиск по названию или условию…"),search,sizeof search);
    ImGui::SameLine();if(ImGui::Button("X##clear",ImVec2(32,0)))search[0]=0;
    bool cityFilter=preferences.value("filter_unvisited_cities",false);
    if(ImGui::Checkbox(tr("Подбирать заказы в непосещённые города"),&cityFilter)){preferences["filter_unvisited_cities"]=cityFilter;save_control();}
    if(ImGui::IsItemHovered()){ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*25);text(tr("Только для городских достижений в плане. Выключено — можно взять дальний заказ и посетить нужный город по пути. Подсветка карты сохраняется."));ImGui::PopTextWrapPos();ImGui::EndTooltip();}
    const auto filterState=aa::offers::Status();
    if(ImGui::GetContentRegionAvail().x>950)ImGui::SameLine(0,28);
    const auto nativeJobs=aa::jobprobe::Status(),nativeMap=aa::mapprobe::Status();
    if(!nativeMap.value("installed",false)&&string_value(nativeMap,"reason")!="disabled") {
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.98f,.72f,.43f,1));
        text(tr("Подсветка карты временно недоступна. Подробности в диагностике."));
        ImGui::PopStyleColor();
    }
    if(filterState.value("active",false)&&!nativeJobs.value("active",false)) {
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.98f,.72f,.43f,1));text(tr("Отбор заказов сейчас недоступен. План сохранён; подробности — в диагностике."));ImGui::PopStyleColor();
    }else if(!string_value(filterState,"unsupported").empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.98f,.72f,.43f,1));text(tr("Отбор заказов недоступен для: ")+string_value(filterState,"unsupported"));ImGui::PopStyleColor();
    }else if(filterState.value("active",false))ImGui::TextColored(ImVec4(.40f,.83f,.65f,1),tr("Заказы по плану · целей: %s"),number(filterState["goals"]).c_str());
    else dim(tr("Заказы без ограничений плана"));
    ImGui::Spacing();
    std::vector<const json*> entries;size_t planned=0;const auto query=lower(search);
    if(current.contains("achievements"))for(const auto& a:current["achievements"]) {
        const auto status=string_value(a,"status");const bool inPlan=pinned(plan_key(current,a));if(inPlan)++planned;
        if((filter==0&&status=="done")||(filter==1&&status!="progress")||(filter==2&&!inPlan)||(filter==3&&status!="done"))continue;
        if(!query.empty()&&lower(string_value(a,"name")+" "+string_value(a,"description")).find(query)==std::wstring::npos)continue;
        entries.push_back(&a);
    }
    bool selectedVisible=false;for(const auto* a:entries)if(string_value(*a,"id")==selected_id)selectedVisible=true;
    if(!selectedVisible&&!entries.empty()){selected_id=string_value(*entries.front(),"id");preferences["selected_id"]=selected_id;}
    const float bodyH=std::max(160.f,ImGui::GetContentRegionAvail().y-44);
    if(ImGui::BeginTable("layout",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV,ImVec2(0,bodyH))) {
        ImGui::TableSetupColumn(tr("Список"),ImGuiTableColumnFlags_WidthStretch,.38f);ImGui::TableSetupColumn(tr("Подробности"),ImGuiTableColumnFlags_WidthStretch,.62f);
        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::BeginChild("list",ImVec2(0,bodyH-8),true);
        dim(tr("Достижения · ")+std::to_string(entries.size()));ImGui::Spacing();
        for(const auto* item:entries) {
            const auto& a=*item;const auto id=string_value(a,"id"),name=string_value(a,"name"),status=string_value(a,"status");
            const bool isSelected=selected_id==id,inPlan=pinned(plan_key(current,a));const auto start=ImGui::GetCursorScreenPos();
            const float width=ImGui::GetContentRegionAvail().x,textW=std::max(80.f,width-36);const auto titleSize=ImGui::CalcTextSize(name.c_str(),nullptr,false,textW);
            const float rowH=titleSize.y+42;ImGui::PushID(id.c_str());
            if(ImGui::Selectable("##row",isSelected,0,ImVec2(0,rowH))){selected_id=id;preferences["selected_id"]=id;save_control();}
            auto* draw=ImGui::GetWindowDrawList();
            if(isSelected)draw->AddRectFilled(ImVec2(start.x,start.y+4),ImVec2(start.x+3,start.y+rowH-4),IM_COL32(238,185,96,255),2);
            draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),ImVec2(start.x+12,start.y+7),IM_COL32(235,241,245,255),name.c_str(),nullptr,textW);
            const auto detail=status_name(status)+(a["current"].is_null()?"":" · "+number(a["current"])+" / "+number(a["target"]))+(inPlan?tr(" · в плане"):"");
            draw->AddText(ImGui::GetFont(),ImGui::GetFontSize()-2,ImVec2(start.x+12,start.y+titleSize.y+13),ImGui::ColorConvertFloat4ToU32(status_color(status)),detail.c_str());
            if(isSelected&&list_reveal_id!=selected_id){ImGui::SetScrollHereY(.35f);list_reveal_id=selected_id;}
            ImGui::PopID();
        }
        if(entries.empty()){text(tr("Здесь пока нет достижений."));dim(tr("Измените запрос или выбранный список."));if(ImGui::Button(tr("Показать все"))){search[0]=0;filter=4;}}
        ImGui::EndChild();ImGui::TableNextColumn();ImGui::BeginChild("details",ImVec2(0,bodyH-8),true);
        const json* selected=nullptr;for(const auto* a:entries)if(string_value(*a,"id")==selected_id)selected=a;
        if(selected) {
            const auto& a=*selected;const auto key=plan_key(current,a);const bool inPlan=pinned(key);
            if(ImGui::BeginTable("detail_header",2,ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn(tr("Название"),ImGuiTableColumnFlags_WidthStretch,1);
                ImGui::TableSetupColumn(tr("План"),ImGuiTableColumnFlags_WidthFixed,164);
                ImGui::TableNextRow();ImGui::TableNextColumn();
                if(heading_font)ImGui::PushFont(heading_font);text(string_value(a,"name"));if(heading_font)ImGui::PopFont();
                ImGui::TableNextColumn();
                if(!inPlan){ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.91f,.69f,.34f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(1,.78f,.42f,1));ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.08f,.12f,.16f,1));}
                if(ImGui::Button(inPlan?tr("Убрать из плана"):tr("Добавить в план"),ImVec2(164,36))){preferences["plan"][key]=!inPlan;save_control();}
                if(!inPlan)ImGui::PopStyleColor(3);ImGui::EndTable();
            }
            const auto progressText=status_name(string_value(a,"status"))+(a["current"].is_number()?" · "+number(a["current"])+" / "+number(a["target"]):"");
            ImGui::TextColored(status_color(string_value(a,"status")),"%s",progressText.c_str());
            if(a["current"].is_number()&&a["target"].is_number()&&a["target"].get<double>()>0) {
                const auto progress=std::clamp(a["current"].get<double>()/a["target"].get<double>(),0.0,1.0);
                ImGui::ProgressBar(float(progress),ImVec2(-1,5),"");
            }
            ImGui::Spacing();
            json info=json::object();if(current.contains("details")&&current["details"].contains("achievements")){const auto i=current["details"]["achievements"].find(selected_id);if(i!=current["details"]["achievements"].end())info=*i;}
            if(inPlan&&string_value(info,"kind")=="visited_cities"&&!cityFilter)dim(tr("Города отмечены на карте. Отбор заказов по городам выключен."));
            render_details(a,info);source_information(info,a);
        }else {text(tr("Выберите достижение"));dim(tr("Слева — список, справа — условия и оставшиеся пункты. Добавляйте нужные достижения в план."));}
        ImGui::EndChild();ImGui::EndTable();
    }
    ImGui::Separator();
    if(ImGui::SmallButton(tr("Диагностика")))ImGui::OpenPopup("diagnostics");ImGui::SameLine(0,18);
    const auto tint=aa::maptint::Status();dim(tr("В плане: ")+std::to_string(planned)+tr(" · На карте: ")+number(tint["targets"]));
    ImGui::SameLine();const float versionX=ImGui::GetWindowContentRegionMax().x-180;if(ImGui::GetCursorPosX()<versionX)ImGui::SetCursorPosX(versionX);dim(aa::game::Footer);
    panel_diagnostics();ImGui::End();
}
