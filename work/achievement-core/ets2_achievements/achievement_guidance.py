"""Visible instructions from installed rules; independent of saved completion."""
import json
from .game_config import GAME,data_file
CAT=json.loads(data_file('tracking_catalogue.json').read_text(encoding='utf-8'))
GEO=json.loads(data_file('baltic_delivery_rules.json').read_text(encoding='utf-8'))
LOCATIONS=json.loads(data_file('company_locations.json').read_text(encoding='utf-8'))['companies']
UNITS=CAT['units']
_extra=data_file('untracked_goal_definitions.json')
EXTRA_GOALS=json.loads(_extra.read_text(encoding='utf-8'))['goals'] if _extra.is_file() else {}

def fmt(value):return f'{value:g}' if isinstance(value,(int,float)) else str(value)
def country_name(value):return CAT['country_labels'].get(value,value.replace('_',' ').title())
def city_name(value):return GEO['city_labels'].get(value,value.replace('_',' ').title())
def cargo_name(value):return CAT['cargos'].get(value,{}).get('name',value.replace('_',' ').title())
def company_name(value):return {'scania_fac':'Завод Scania','volvo_fac':'Завод Volvo Trucks'}.get(value,CAT.get('companies',{}).get(value,value.replace('_',' ').title()))
def city_place(value):
    country=GEO['city_countries'].get(value)
    return city_name(value)+(f' ({country_name(country)})' if country else '')
def company_place(company,city=None,country=None):
    name=company_name(company)
    if city:return name+' · '+city_place(city)
    cities=[c for c in LOCATIONS.get(company,[]) if not country or GEO['city_countries'].get(c)==country]
    if cities:return name+' — '+', '.join(city_place(c) for c in sorted(cities,key=city_name))
    return name+(' · '+country_name(country) if country else ' (город предприятия пока не расшифрован)')
def depot(value):
    company,sep,city=value.partition('.')
    return company_place(company,city if sep else None)

def describe_condition(uid,role=None,depth=0):
    if depth>40 or uid not in UNITS:return 'Часть условия пока не расшифрована; см. описание достижения.'
    unit=UNITS[uid];a=unit['attrs'];typ=unit['class'].removeprefix('achievement_delivery_')
    def child(c):return describe_condition(c,role,depth+1)
    if unit['class']=='achievement_unique_cargos':return child(a['condition'])+f"; разных видов груза: {a.get('count',1)}"
    if unit['class']=='achievement_unique_delivery_points':
        noun={'country':'стран','city':'городов','company':'предприятий','company_instance':'ферм'}.get(a.get('unique'),'пунктов')
        locations=[child(a[key]) for key in ('source_collect_condition','target_collect_condition') if key in a]
        direction='; ('+' ИЛИ '.join(locations)+')' if locations else ''
        return child(a['condition'])+direction+f"; разных {noun}: {a.get('count',1)}"
    if typ in ('each','any','sequence'):
        parts=list(dict.fromkeys(child(c) for c in a['list']))
        if typ=='each':return '; '.join('('+child(c)+')' if UNITS[c]['class']=='achievement_delivery_any' else child(c) for c in dict.fromkeys(a['list']))
        if typ=='any':return ' ИЛИ '.join('('+p+')' for p in parts) if len(parts)>1 else parts[0]
        return ('По порядку: ' if a.get('in_order') else 'Все пункты: ')+' → '.join(parts)
    if typ=='count':return child(a['condition'])+f"; доставок: {a.get('count',1)}"
    if typ=='not':return 'Исключить: '+child(a['condition'])
    if typ in ('company','city','country','point_city','point_country'):
        role=a.get('role',role);prefix={'source':'Откуда: ','target':'Куда: '}.get(role,'Пункт: ')
        if 'company_name' in a:place=company_place(a['company_name'],a.get('city_name'),a.get('country_name'))
        elif 'city_name' in a:place=city_place(a['city_name'])
        elif 'country_name' in a:place=country_name(a['country_name'])
        else:place='место пока не расшифровано'
        return prefix+place
    if typ=='cargo':
        parts=[cargo_name(a['cargo_name'])] if 'cargo_name' in a else []
        if 'adr' in a:parts.append('любой опасный груз ADR' if a['adr']=='any_class' else 'ADR, класс '+fmt(a['adr']))
        return 'Груз: '+', '.join(parts)
    if typ=='cargo_damage':return 'Без повреждений груза' if not a['damage_max'] else 'Повреждение груза ≤ '+fmt(a['damage_max']*100)+'%'
    if typ=='late':return 'Без опоздания' if not a['late'] else 'С опозданием'
    if typ=='fines':return 'Без штрафов' if not a.get('fines_max',0) else 'Штрафы ≤ '+fmt(a['fines_max'])
    if typ=='unload_difficulty':return 'Парковка: '+{'hard':'сложная','medium':'средняя','easy':'простая'}.get(a['difficulty'],a['difficulty'])
    if typ=='route':
        routes=json.loads(data_file('special_route_rules.json').read_text(encoding='utf-8'))
        item=next((p for p in routes['items'] if p['id']==a['route_name']),None)
        return 'Спецмаршрут: '+(item['name'] if item else 'название маршрута пока не расшифровано')
    return 'Часть условия пока не расшифрована; см. описание достижения.'

def delivery_guidance(a):
    result=[]
    if 'cargos' in a:result.append(('Груз',', '.join(cargo_name(c) for c in a['cargos'])))
    elif 'cargo' in a:result.append(('Груз',cargo_name(a['cargo'])))
    else:result.append(('Груз','Отдельного ограничения по виду груза в правиле нет; учитывайте описание достижения и доступные заказы предприятий.'))
    for role,label in [('source','Откуда'),('target','Куда')]:
        companies=a.get(role+'_companies',a.get('sources' if role=='source' else 'targets'))
        cities=a.get(role+'_cities')
        places=[depot(c) for c in companies] if companies else [city_place(c) for c in cities] if cities else []
        result.append((label,'; '.join(places) if places else 'Любой пункт; отдельного ограничения в правиле нет.'))
    extra=[]
    if 'distance_min' in a:extra.append('длина доставки не менее '+fmt(a['distance_min'])+' км')
    if 'price' in a:extra.append('оплата не менее '+fmt(a['price'])+(' $' if GAME=='ats' else ' €'))
    if 'urgency' in a:extra.append('срочность: '+{'urgent':'срочная','important':'важная','normal':'обычная'}.get(a['urgency'],a['urgency']))
    if 'remaining_time_max' in a:extra.append('при завершении до срока остаётся не более '+fmt(a['remaining_time_max'])+' мин')
    if 'damage_max' in a:extra.append('без повреждений груза' if not a['damage_max'] else 'повреждение груза ≤ '+fmt(a['damage_max']*100)+'%')
    if 'late' in a:extra.append('без опоздания' if not a['late'] else 'с опозданием')
    if 'fines_max' in a:extra.append('без штрафов' if not a['fines_max'] else 'штрафы ≤ '+fmt(a['fines_max']))
    if 'autoparking' in a:extra.append('автоматическая парковка' if a['autoparking'] else 'парковка вручную')
    if 'difficulty' in a:extra.append('парковка: '+{'hard':'сложная','medium':'средняя','easy':'простая'}.get(a['difficulty'],a['difficulty']))
    if extra:result.append(('Условия доставки','; '.join(extra)+'.'))
    target=a.get('target',1)
    result.append(('Количество','Доставок: '+str(target)+(' подряд.' if a.get('consecutive') and target>1 else '. Повторные доставки по этому условию засчитываются.' if target>1 else '.')))
    if 'total_cargo_mass_min' in a:result.append(('Масса серии','Не менее '+fmt(a['total_cargo_mass_min']/1000)+' т суммарно за последние '+str(target)+' подходящих доставок подряд.'))
    if 'total_income_min' in a:result.append(('Доход серии','Не менее '+fmt(a['total_income_min'])+' $ суммарно за последние '+str(target)+' подходящих доставок подряд.'))
    return result

def guidance_for(aid):
    if aid not in CAT['goals']:return [{'label':'Подробности','text':'Условие есть в новой версии игры, но подробная поддержка этой цели пока не добавлена. Используйте описание Steam.' if aid in EXTRA_GOALS else 'В установленной игре ещё нет подробных правил этой цели; используйте описание Steam.'}]
    goal=CAT['goals'][aid];u=UNITS[goal['unit']];a=u['attrs'];typ=u['class'];target=a.get('target',1);rows=[]
    if typ in ('achievement_delivery_log_data','achievement_deliver_cargo_data'):
        rows=delivery_guidance(a)
        if aid.startswith('st_'):rows.insert(0,('Рынок','Заказы Special Transport с сопровождением. Обычные заказы с похожим грузом не заменяют спецдоставку.'))
    elif typ=='achievement_delivery':
        node=UNITS[a['condition']];p=node['attrs']
        if aid=='st_all_routes':rows=[('Маршруты',f'Выполнить {target} разных маршрутов Special Transport из списка ниже. Повтор того же маршрута не добавляет новый пункт.')]
        elif node['class']=='achievement_delivery_sequence':
            quotas=[UNITS[c]['attrs'].get('count',1) for c in p['list']]
            count_text=(f"Разных пунктов: {target} из {len(p['list'])}. Повторы одного пункта не заменяют разные." if all(n==1 for n in quotas) and 1<target<=len(quotas) else f"Всего доставок: {target}; отдельная норма каждой категории указана в строках ниже.")
            rows=[('Пункты',count_text+' Груз, отправление и назначение указаны для каждого пункта отдельно.'),('Порядок','Строго сверху вниз. Доставка для следующего этапа, выполненная раньше предыдущего, его не закрывает.' if p.get('in_order') else 'Пункты можно выполнять в любом порядке.')]
        else:rows=[('Что засчитывается',describe_condition(a['condition']))]
        if aid!='st_all_routes':rows.append(('Свободный выбор','Если в пункте не указаны отправитель, получатель или вид груза, эта часть заказа отдельно не ограничена.'))
    elif typ=='achievement_each_company_data':
        if 'cargos' in a:rows.append(('Груз',', '.join(cargo_name(c) for c in a['cargos'])))
        else:rows.append(('Груз','Выбирайте груз из описания достижения выше. Доступные виды также зависят от предприятия; отдельный перечень в этом правиле не задан.'))
        for role,key,label in [('source','sources','Откуда'),('target','targets','Куда')]:
            rows.append((label,'; '.join(depot(v) for v in a[key]) if a.get(key) else 'Любой пункт; отдельного ограничения в правиле нет.'))
        rows.append(('Разные пункты',f'Требуется {target} разных филиалов из списка. Повтор того же филиала не закрывает новый пункт.'))
    elif typ=='achievement_each_delivery_point':
        rows=[('Направление','Оба направления каждой пары допустимы.' if a.get('both_directions') else 'Только указанное направление от отправления к назначению.'),('Порядок','Строго сверху вниз; ранее выполненные будущие этапы не закрываются.' if a.get('in_order') else 'Пары можно выполнять в любом порядке.')]
        extra=[]
        if 'damage_max' in a:extra.append('без повреждений груза' if not a['damage_max'] else 'повреждение ≤ '+fmt(a['damage_max']*100)+'%')
        if a.get('late') is False:extra.append('без опоздания')
        if a.get('fines_max')==0:extra.append('без штрафов')
        if extra:rows.append(('Каждая доставка','; '.join(extra)+'.'))
    elif typ=='achievement_visit_city_data':
        rows=[('Что засчитывается',f'Посетить {target} разных городов из списка. Доставка не обязательна: можно проехать через нужный город по пути.'),('Заказы по городам','Дополнительный отбор заказов включается отдельным переключателем. Подсветка карты действует при добавлении цели в план.')]
    elif typ=='achievement_each_cargo_data':rows=[('Разные грузы',f'Доставить {target} разных видов из списка ниже. Повтор того же вида не добавляет новый пункт.'),('Маршрут','Отправление и назначение отдельно не ограничены.')]
    elif typ=='achievement_limit_data':
        text={'get_cargo_type_count':f'Доставить {target} разных видов груза; повтор одного вида не добавляет новый пункт.','get_company_count':f'Выполнить доставки в {target} разных компаний; филиалы одной компании не являются разными компаниями.','get_company_group_count':f'Доставить грузы в {target} разных групп компаний; подробный учёт групп пока недоступен.','get_explored_km':f'Исследовать {target} км дорог. Процент карты и общий пробег грузовика не заменяют эту величину.','trucks_bought_online':f'Купить грузовик через интернет, а не при личном посещении дилера. Покупок: {target}.','get_unlocked_recruitments_count_current_map':f'Открыть {target} разных кадровых агентств на карте. Уже известные города исключаются из подсветки.','get_used_ports_count':f'Использовать {target} разных портов для отправления. Повтор одного порта не добавляет новый пункт.','get_jobs_travelled_distance':f'Пройти {target} км при выполнении доставок.','get_jobs_total_reward':f'Заработать {target} $ за выполненные доставки.'}.get(a.get('current'),'Подробный источник этого счётчика пока не расшифрован; используйте условие Steam.')
        rows=[('Что засчитывается',text)]
    elif typ=='achievement_garages_data':
        location='; '.join(city_place(c) for c in a['cities']) if a.get('cities') else 'Все города страны вашей штаб-квартиры.' if a.get('filter')=='hq_country' else 'Любые города.'
        rows=[('Где',location),('Гаражи','Купить и увеличить до максимального размера.' if a.get('level')=='large' else f'Иметь гаражи в {target} разных городах.')]
    elif typ=='achievement_profit_data':
        if 'productivity_min' in a:rows=[('Гаражи',f"Свои большие гаражи: {a['garage_count']}; у каждого производительность не ниже {fmt(a['productivity_min'])}%."),('Проверка','Игра проверяет условие при смене недели.')]
        elif 'avg_productivity_min' in a:rows=[('Гаражи',f"Свои большие гаражи: {a['garage_count']}; средняя производительность не ниже {fmt(a['avg_productivity_min'])}%."),('Проверка','Игра проверяет условие при смене недели.')]
        elif 'avg_daily_profit_min' in a:rows=[('Прибыль',f"Средняя дневная прибыль не менее {fmt(a['avg_daily_profit_min'])} €. Текущий остаток денег не является дневной прибылью."),('Проверка','Игра проверяет условие при смене дня.')]
        elif a.get('driver_gender')=='each':rows=[('Водители',f"Нанять не менее {a['driver_count_min']} мужчин и {a['driver_count_min']} женщин; у каждого все навыки должны быть максимальными.")]
        elif a.get('garage_size')=='large':rows=[('Гаражи',f"Иметь {a.get('garage_count',1)} гараж максимального размера.")]
    elif typ=='achievement_truck_brand_data':rows=[('Грузовик','Собственный грузовик, а не выданный для быстрого заказа.'),('Пробег',f"Не менее {a['distance']} км на каждой из {target} разных марок при выполнении доставок. Доступные марки: "+', '.join({'daf':'DAF','man':'MAN','mercedes':'Mercedes-Benz'}.get(b.removeprefix('vehicle.'),b.removeprefix('vehicle.').title()) for b in a['brands'])+'.')]
    elif typ=='achievement_xp_data':
        rows=([('Опыт',f"Набрать {target} XP за последовательные доставки суммарной длиной менее {a['sum_distance_max']} км.")] if 'sum_distance_max' in a else [('Груз',', '.join(cargo_name(c) for c in a.get('cargos',[]))),('Опыт',f'Набрать {target} XP именно за парковку этих грузов. Общий XP доставки не заменяет парковочный.')])
    elif typ=='achievement_cargo_group_data':rows=[('Что засчитывается',f'Перевезти грузы {target} разных групп прицепов. Разные грузы одной группы не заменяют разные группы.')]
    elif typ=='achievement_ferry_data':rows=[('Переправа','Транспортировка на поезде через терминал.' if a.get('ferry_type')=='train' else 'Морская переправа на пароме.' if a.get('ferry_type')=='ferry' else ' ↔ '.join(city_place(a[k]) for k in ['endpoint_a','endpoint_b'] if k in a))]
    elif typ=='achievement_position_data':rows=[('Куда','Быстрое перемещение в гараж вашей штаб-квартиры; обычная поездка туда не заменяет быстрое перемещение.')]
    elif typ=='achievement_paintjob_data':rows=[('Где','Мастерская покраски.'),('Что сделать','Выбрать многоцветную металлическую окраску и изменить её цвет.' if a.get('flipflake') else 'Изменить цвет грузовика.')]
    elif typ=='achievement_trigger_data':rows=[('Действие','Запустить обзор в каждой нужной обзорной точке.' if aid.endswith('cutscenes') else 'Посетить указанные места и проехать через соответствующие дорожные триггеры; одного открытия дороги рядом недостаточно.'),('Отметки','Места перечислены ниже и подсвечиваются по плану. Неизвестный статус не означает, что место уже пройдено.')]
    elif typ=='achievement_data':
        text={'use_rest_stop':'Отдохнуть в разрешённом месте стоянки.','use_gas_station':'Заправить грузовик на АЗС.','use_gas_stat':'Заправить грузовик на АЗС.','use_weigh_stat':'Пройти взвешивание на станции весового контроля.','buy_truck':'Купить грузовик у дилера.'}.get(aid)
        if text:rows=[('Действие',text)]
    elif typ=='achievement_academy':rows=[('Где','Режим «Академия вождения»; сценарии перечислены ниже.'),('Условия','Соблюдайте условие достижения в каждом нужном сценарии. Общее число Steam не подтверждает выполнение каждого сценария.')]
    if not rows:rows=[('Подробности','Дополнительные условия пока не расшифрованы; ориентируйтесь на описание достижения выше.')]
    return [{'label':label,'text':value} for label,value in rows if value]
