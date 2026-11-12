from .game_config import GAME,APP_ID,data_file
"""Condition-driven achievement tracking. Unknown data never passes a predicate."""
from pathlib import Path
from collections import Counter
import json,math,re
from .baltic_delivery_progress import RULES as PLACES

CAT=json.loads((data_file('tracking_catalogue.json')).read_text(encoding='utf-8'))
UNITS=CAT['units'];CARGO=CAT['cargos']
HISTORY='Оценка по журналу; история может быть неполной. Отсутствие записи не доказывает невыполнение.'
SAVED='Накопительные данные выбранного сохранения; они могут отличаться от общего прогресса Steam.'

def number(value):
    try:
        n=float(value)
        return n if math.isfinite(n) else None
    except (ValueError,TypeError):return None

def all3(values):
    values=list(values)
    return False if False in values else None if None in values else True

def any3(values):
    values=list(values)
    return True if True in values else None if None in values else False

def compare(value,other,operator='eq'):
    if value is None:return None
    return {'eq':lambda:value==other,'le':lambda:value<=other,'ge':lambda:value>=other}[operator]()

def city_name(c):return PLACES['city_labels'].get(c,c)
def cargo_name(c):return CARGO.get(c,{}).get('name',c)
def country_name(c):return CAT['country_labels'].get(c,c)
def company_name(c):return CAT.get('companies',{}).get(c,c.replace('_',' ').title())

def row(uid,name,current=None,target=1,**extra):
    return dict(id=uid,name=name,current=current,target=target,**extra)

def report(title,items,target,mode='saved',**extra):
    known=[r for r in items if r['current'] is not None]
    value=sum(min(r['target'],max(0,r['current'])) for r in known) if known else None
    return dict(kind='tracking',title=title,items=items,target=target,observed=value,mode=mode,
                notice=SAVED if mode=='saved' else HISTORY if mode in ('history','sequence','series') else '',
                available=bool(known),**extra)

def delivery_history(blocks):
    logs=[u['attrs'] for u in blocks if u['class']=='delivery_log']
    if len(logs)!=1 or logs[0].get('version')!=1 or not isinstance(logs[0].get('entries'),list):return None,0
    units={};duplicate=set()
    for u in blocks:
        if u['class']=='delivery_log_entry':
            if u['id'] in units:duplicate.add(u['id'])
            units[u['id']]=u['attrs']
    jobs=[];seen=set();bad=0
    for ref in logs[0]['entries']:
        if not isinstance(ref,str):bad+=1;jobs.append(None);continue
        if ref in seen:continue
        seen.add(ref);p=units.get(ref,{}).get('params')
        if ref in duplicate or not isinstance(p,list) or len(p)!=24 or not all(isinstance(x,str) for x in p):
            bad+=1;jobs.append(None);continue
        if p[18]=='freerm':continue
        if p[18] not in ('compn','quick','on_compn','spec_oversize'):
            bad+=1;jobs.append(None);continue
        source=re.fullmatch(r'company\.volatile\.([a-z0-9_]+)\.([a-z0-9_]+)',p[1])
        dest=re.fullmatch(r'company\.volatile\.([a-z0-9_]+)\.([a-z0-9_]+)',p[2])
        remaining=number(p[8]);damage=number(p[7]);park=number(p[11]);auto=number(p[10])
        paid=number(p[5]);distance=number(p[6]);cargo=p[3].removeprefix('cargo.') if re.fullmatch(r'cargo\.[a-z0-9_]+',p[3]) else None
        valid=bool(paid is not None and paid>0 and distance is not None and distance>0 and cargo)
        j={'id':ref,'time':number(p[0]),'source':source[1]+'.'+source[2] if source else None,
           'target':dest[1]+'.'+dest[2] if dest else None,'source_city':source[2] if source else None,
           'target_city':dest[2] if dest else None,'cargo':cargo,'valid':valid,'type':p[18],
           'damage':damage if damage is not None and 0<=damage<=1 else None,
           'late':None if remaining is None or (p[18]=='on_compn' and remaining not in (-1,1)) else remaining<0,
           'remaining':remaining,'fines':number(p[14]),'distance':distance,'price':paid,
           'urgency':number(p[9]),'autoparking':bool(auto) if auto in (0,1) else None,
           'parking':park,'mass':number(p[22]),'xp':number(p[4]),'route':p[19] or None}
        jobs.append(j)
    # A sequence is reconstructed only when chronological ordering is known.
    times=[j['time'] for j in jobs if j is not None]
    if any(t is None for t in times) or times!=sorted(times):
        # Do not sort across gaps: the order of unsupported records is unknown.
        bad+=1
        for j in jobs:
            if j is not None:j['order_unknown']=True
    return jobs,bad

def endpoint(j,role,key):
    if key=='city':return j.get(role+'_city')
    if key=='country':return PLACES['city_countries'].get(j.get(role+'_city'))
    if key=='company':
        token=j.get(role)
        return token.split('.')[0] if token else None
    return None

def match(uid,j,role=None,depth=0):
    if depth>40 or uid not in UNITS or j is None or not j['valid']:return None
    u=UNITS[uid];a=u['attrs'];typ=u['class'].removeprefix('achievement_delivery_')
    if u['class']=='achievement_unique_cargos':return match(a['condition'],j,role,depth+1)
    if typ=='each':return all3(match(c,j,role,depth+1) for c in a['list'])
    if typ=='any':return any3(match(c,j,role,depth+1) for c in a['list'])
    if typ=='not':
        result=match(a['condition'],j,role,depth+1);return None if result is None else not result
    if typ in ('count','sequence'):
        return match(a['condition'],j,role,depth+1) if 'condition' in a else any3(match(c,j,role,depth+1) for c in a['list'])
    r=a.get('role',role)
    if typ in ('company','point_city','point_country','city','country'):
        if r not in ('source','target'):return None
        checks=[]
        for field,key in [('company_name','company'),('city_name','city'),('country_name','country')]:
            if field in a:checks.append(compare(endpoint(j,r,key),a[field]))
        return all3(checks) if checks else None
    if typ=='cargo':
        checks=[]
        if 'cargo_name' in a:checks.append(compare(j['cargo'],a['cargo_name']))
        if 'adr' in a:
            adr=CARGO.get(j['cargo'],{}).get('adr')
            checks.append(None if adr is None else adr>0 if a['adr']=='any_class' else adr==a['adr'])
        return all3(checks) if checks else None
    if typ=='cargo_damage':return compare(j['damage'],a['damage_max'],'le')
    if typ=='late':return compare(j['late'],a['late'])
    if typ=='route':return compare(j['route'],a['route_name'])
    if typ=='unload_difficulty':return compare(j['parking'],{'hard':3,'medium':2,'easy':1}.get(a['difficulty']))
    return None

def describe(uid,depth=0):
    from .achievement_guidance import describe_condition
    return describe_condition(uid,depth=depth)

def evidence(j):
    return f"{city_name(j['source_city']) if j['source_city'] else '—'} → {city_name(j['target_city']) if j['target_city'] else '—'} · {cargo_name(j['cargo'])}"

def condition_progress(uid,jobs):
    u=UNITS[uid];a=u['attrs'];t=u['class'];items=[]
    if t=='achievement_delivery_sequence':
        ordered=a.get('in_order',False);cursor=0
        for child in a['list']:
            node=UNITS[child];p=node['attrs'];target=p.get('count',1) if node['class']=='achievement_delivery_count' else 1
            pred=p['condition'] if node['class']=='achievement_delivery_count' else child
            found=[];unknown=0;end=cursor
            for index in range(cursor if ordered else 0,len(jobs or [])):
                j=jobs[index]
                if ordered and (j is None or j.get('order_unknown')):unknown+=1;continue
                m=match(pred,j)
                if m is True:
                    found.append(j);end=index+1
                    if ordered and len(found)>=target:break
                elif m is None:unknown+=1
            blocked=ordered and len(items)>0 and items[-1]['current']!=items[-1]['target']
            value=None if jobs is None or not found else min(len(found),target)
            if blocked:value=None;found=[]
            friendly={'.ba_industry.lvr':'Локомотивные заводы','.ba_industry.renat':'Мебельные заводы',
                      '.ba_industry.paper':'Бумажные фабрики','.gr_marble.marb_blck':'Мраморные блоки',
                      '.gr_marble.marb_slab':'Неповреждённые мраморные плиты'}
            items.append(row(child,friendly.get(child,describe(pred)),value,target,uncertain=unknown,blocked=blocked,
                             requirements=describe(pred) if child in friendly else None,evidence=[evidence(j) for j in found[:8]]))
            if ordered and not blocked and len(found)>=target:cursor=end
        return items,'sequence' if ordered else 'history'
    if t=='achievement_unique_cargos':
        found={}
        for j in jobs or []:
            if match(a['condition'],j) is True and j['cargo']:found.setdefault(j['cargo'],j)
        return [row(c,cargo_name(c),1,evidence=[evidence(j)]) for c,j in sorted(found.items())],'history'
    if t=='achievement_unique_delivery_points':
        key=a['unique'];r=a['role'];counts=Counter()
        if key=='company_instance' and 'company_instances' in u:
            candidates=set(u['company_instances']);found={}
            for job in jobs or []:
                if match(a['condition'],job) is not True:continue
                points=set()
                for role in ('source','target'):
                    condition=a.get(role+'_collect_condition')
                    if condition and job[role] in candidates and match(condition,job) is True:
                        points.add(job[role])
                # A delivery involving two eligible farms does not identify
                # which farm the game credited. Keep those items unknown.
                if len(points)==1:found.setdefault(points.pop(),job)
            items=[]
            for token in sorted(candidates):
                company,city=token.split('.',1);job=found.get(token)
                items.append(row(token,company_name(company)+' · '+city_name(city),1 if job else None,
                                 evidence=[evidence(job)] if job else []))
            return items,'history'
        for j in jobs or []:
            if match(a['condition'],j) is True:
                value=endpoint(j,r,key)
                if value:counts[value]+=1
        candidates=sorted((set(PLACES['city_countries'].values())-{'norway','halloween'})|set(counts)) if key=='country' else sorted(counts)
        return [row(v,country_name(v),1 if counts[v] else None,evidence=[f'Доставок в истории: {counts[v]}'] if counts[v] else []) for v in candidates],'history'
    target=a.get('count',1);pred=a.get('condition',uid)
    matches=[j for j in jobs or [] if match(pred,j) is True]
    return [row(uid,describe(pred),min(len(matches),target) if matches else None,target,evidence=[evidence(j) for j in matches[-8:]])],'history'

def log_match(a,j,reverse=False):
    if j is None or not j['valid']:return None
    checks=[]
    for field,role,key in [('source_cities','source','city'),('target_cities','target','city')]:
        if field in a:
            value=endpoint(j,('target' if role=='source' else 'source') if reverse else role,key)
            checks.append(None if value is None else value in a[field])
    for field,r in [('source_companies','source'),('target_companies','target')]:
        if field in a:checks.append(None if j[r] is None else j[r] in a[field])
    if 'cargos' in a:checks.append(None if j['cargo'] is None else j['cargo'] in a['cargos'])
    for field,prop,op in [('damage_max','damage','le'),('fines_max','fines','le'),('distance_min','distance','ge'),('price','price','ge'),('remaining_time_max','remaining','le'),('late','late','eq'),('autoparking','autoparking','eq')]:
        if field in a:checks.append(compare(j[prop],a[field],op))
    if 'difficulty' in a:checks.append(compare(j['parking'],{'hard':3,'medium':2,'easy':1}.get(a['difficulty'])))
    if 'urgency' in a:checks.append(compare(j['urgency'],{'normal':0,'important':1,'urgent':2}.get(a['urgency'])))
    return all3(checks)

def log_progress(aid,a,jobs):
    target=a.get('target',1);matches=[];tail=[];best=[];current=[];unknown=0
    for j in jobs or []:
        passed=log_match(a,j)
        if a.get('consecutive') and j is not None and j.get('order_unknown'):passed=None
        if passed is True:
            matches.append(j);current.append(j)
            if len(current)>len(best):best=list(current)
        else:
            if passed is None:unknown+=1
            current=[]
    tail=current
    if 'total_income_min' in a:
        window=tail[-target:];income=sum(j['price'] for j in window)
        items=[row('jobs','Доставок в текущей серии',len(window) if window else None,target),row('income','Доход текущей серии, $',income if window else None,a['total_income_min'])]
        return report('Серия и суммарный доход',items,target,'series',aggregate='none',summary='Число доставок и доход должны выполняться одновременно в последнем окне серии.')
    if 'total_cargo_mass_min' in a:
        window=tail[-target:];mass=sum(j['mass'] for j in window if j['mass'] is not None)
        known=bool(window) and all(j['mass'] is not None and j['mass']>=0 for j in window)
        items=[row('jobs','Доставок в текущей серии',len(window) if window else None,target),
               row('mass','Масса текущей серии, т',mass/1000 if known else None,a['total_cargo_mass_min']/1000)]
        r=report('Серия и суммарная масса',items,target,'series');r['observed']=len(window) if window else None
        r['aggregate']='none'
        r['summary']='Оба условия должны выполняться одновременно: число доставок и масса последнего окна серии.'
        r['next_step']='Нужны одновременно нужное число последовательных доставок и суммарная масса. Учитывается последнее окно серии, а не все грузы за всё время.'
        return r
    consecutive=a.get('consecutive',False) and target>1
    selected=tail if consecutive else matches
    items=[row(aid,'Текущая серия' if consecutive else 'Подходящие доставки',min(len(selected),target) if selected else None,target,evidence=[evidence(j) for j in selected[-8:]])]
    r=report('Серия доставок' if consecutive else 'Доставки по условию',items,target,'series' if consecutive else 'history',uncertain_records=unknown)
    if consecutive:r['best_observed']=min(len(best),target);r['next_step']='Для серии выполняйте подходящие доставки подряд. Неподходящая или неразобранная запись прерывает восстановленную серию; свободная поездка её не прерывает.'
    if 'autoparking' in a:r['caveat']='Учёт автоматической парковки экспериментальный и требует игровой проверки.'
    return r

def transport_docks(blocks,economy):
    progress=next((u['attrs'] for u in blocks if u['id']==economy.get('game_progress') and u['class']=='game_progress'),{})
    refs=[progress.get(k) for k in ('generic_transports','undamaged_transports','clean_transports')]
    counts=Counter();known=False
    for ref in refs:
        records=[u['attrs'] for u in blocks if u['class']=='transport_data' and u['id']==ref]
        if len(records)!=1:continue
        a=records[0];d=a.get('docks');n=a.get('count_per_dock')
        if not isinstance(d,list) or not isinstance(n,list) or len(d)!=len(n):continue
        if not all(isinstance(k,str) and isinstance(v,int) and v>=0 for k,v in zip(d,n)):continue
        known=True
        for k,v in zip(d,n):counts[k]+=v
    # Partial arrays can prove presence, never absence across all categories.
    return counts,known

def company_progress(a,jobs,docks,known):
    items=[]
    for role,field in [('source','sources'),('target','targets')]:
        for token in a.get(field,[]):
            company,city=token.split('.',1);found=[]
            if role=='target' and not a.get('cargos') and known:
                n=docks[token];mode='saved'
            else:
                found=[j for j in jobs or [] if j and j['valid'] and j[role]==token and (not a.get('cargos') or j['cargo'] in a['cargos'])]
                n=len(found);mode='history'
            items.append(row(role+':'+token,('Откуда: ' if role=='source' else 'Куда: ')+company_name(company)+' · '+city_name(city),1 if n else None,
                             deliveries=n,evidence=[evidence(j) for j in found[-5:]]))
    r=report('Предприятия по условию',items,a.get('target',1),mode if items else 'history')
    if a.get('cargos'):r['next_step']='Подходящие грузы: '+', '.join(cargo_name(c) for c in a['cargos'])+'. Разные филиалы считаются отдельными пунктами.'
    return r

def pair_progress(a,jobs):
    items=[];cursor=0;ordered=a.get('in_order',False)
    for index,(src,dst) in enumerate(zip(a['sources'],a['targets'])):
        found=[]
        for pos in range(cursor if ordered else 0,len(jobs or [])):
            j=jobs[pos]
            if j is None or (ordered and j.get('order_unknown')):continue
            forward=all3([match(src,j,'source'),match(dst,j,'target')])
            reverse=all3([match(src,j,'target'),match(dst,j,'source')]) if a.get('both_directions') else False
            if all3([any3([forward,reverse]),log_match({k:v for k,v in a.items() if k in ('damage_max','fines_max','late')},j)]) is True:
                found.append(j)
                if ordered:cursor=pos+1;break
        blocked=ordered and any(r['current']!=1 for r in items)
        items.append(row(str(index),describe(src)+(' ↔ ' if a.get('both_directions') else ' → ')+describe(dst),1 if found and not blocked else None,blocked=blocked,evidence=[evidence(j) for j in found[:5]]))
    return report('Этапы маршрута' if ordered else 'Пары городов или стран',items,a['target'],'sequence' if ordered else 'history')

def academy_progress(aid,a):
    # Scenario membership must come from chapter definitions, not scenario IDs:
    # chapter 4 contains scenarios whose IDs start with 3_.
    book='academy.book.book3' if aid=='aca_book3' else 'academy.book.book2' if aid in ('aca_pass_conditions','aca_stars','aca_stars_consecutive') else 'academy.book.book1'
    chapter_ids=CAT['books'][book]['chapters']
    if aid in ('aca_interior','aca_no_forward'):chapter_ids=['academy.chapter.book1.4']
    if aid=='aca_consecutive':chapter_ids=['academy.chapter.book1.3','academy.chapter.book1.4']
    if aid=='aca_manual_transmission':chapter_ids=['academy.chapter.book1.2','academy.chapter.book1.3','academy.chapter.book1.4']
    ids=[s for c in chapter_ids for s in CAT['chapters'][c]['scenarios']]
    if aid=='aca_first':ids=['academy.scenario.book1.1_01']
    items=[row(s,CAT['scenarios'][s]['name'],manual=True) for s in ids]
    r=report('Сценарии Академии',items,a.get('target',1),'manual')
    r['reason']='Детальный журнал Академии в выбранном сохранении не найден. Отметки сценариев не восстанавливаются из общего Steam-счётчика.'
    r['next_step']='Условия выбранного достижения нужно соблюдать в каждом отмечаемом сценарии. Отмечайте подтверждённые вами сценарии; это личные отметки, отдельно от Steam.'
    if aid=='aca_truck_driving':
        r['required_ids']=['academy.scenario.book1.3_19','academy.scenario.book1.3_20']
        r['next_step']='Нужны оба выпускных экзамена и ещё 26 разных сценариев первой книги. Личные отметки экзаменов обязательны; одним числом 28 условие не исчерпывается.'
    if a.get('consecutive'):r['series_target']=a['target'];r['next_step']='Нужна серия разных сценариев без неудач. Список хранит выполненные сценарии; длину текущей серии укажите отдельно после проверки в игре.'
    return r

COACH_SKILLS = {
    'long_duty': 'Длительные поездки',
    'tight_schedule': 'Требовательные расписания',
    'satisfaction_decrease': 'Премиум-обслуживание',
    'low_acceleration': 'Мягкая поездка',
    'satisfaction_increase': 'Любимчик пассажиров',
    'ecodriving': 'Эко-вождение',
}


def coach_history(blocks,economy):
    logs=[u['attrs'] for u in blocks if u['class']=='bus_job_log' and u['id']==economy.get('bus_job_log')]
    if len(logs)!=1 or logs[0].get('version')!=0 or not isinstance(logs[0].get('entries'),list):
        return None,None
    entries={}
    for unit in blocks:
        if unit['class']=='bus_job_log_entry':
            uid=unit['id']
            entries[uid]=None if uid in entries else unit['attrs']
    best=None
    owned_distance=None
    seen=set()
    for uid in logs[0]['entries']:
        if not isinstance(uid,str) or uid in seen:continue
        seen.add(uid)
        job=entries.get(uid)
        if not job:continue
        market=job.get('type')
        # The history stores both contract and tourist work in the company category.
        if market not in ('quick_job','company_job'):continue
        satisfaction=number(job.get('satisfaction'))
        if satisfaction is not None and 0<=satisfaction<=1:
            best=max(best or 0,satisfaction*100)
        distance=job.get('travelled_distance')
        if type(distance) is int and distance>=0:
            owned_distance=(owned_distance or 0)+(0 if market=='quick_job' else distance)
    return best,owned_distance


def coach_progress(a,economy,history):
    target=a['target']
    kind=a['type']
    if kind=='skills':
        items=[]
        for key,label in COACH_SKILLS.items():
            rank=economy.get(key)
            items.append(row(key,label,int(rank>0) if type(rank) is int and rank>=0 else None))
        return report('Навыки водителя автобуса',items,target)
    if kind=='jobs_consecutive':
        title='Серия автобусных рейсов'
        return report(title,[row(kind,title,None,target)],target,'steam',source='Локальный кэш Steam')
    satisfaction,distance=history
    title,value=(('Лучшая удовлетворённость пассажиров, %',satisfaction) if kind=='job_satisfaction'
                 else ('Пробег на своём автобусе по журналу, км',distance))
    result=report(title,[row(kind,title,value,target)],target,'history',source='Журнал автобусных рейсов выбранного профиля')
    result['observed']=value
    return result


def tracking_details(blocks,economy,existing):
    jobs,bad=delivery_history(blocks);docks,docks_known=transport_docks(blocks,economy)
    player=next((u['attrs'] for u in blocks if u['class']=='player' and u['id']==economy.get('player')),None)
    # Synthetic/legacy saves may expose just one player without a reference.
    if player is None:
        candidates=[u['attrs'] for u in blocks if u['class']=='player'];player=candidates[0] if len(candidates)==1 else {}
    refs=economy.get('garages');garages=[u for u in blocks if u['class']=='garage' and isinstance(refs,list) and u['id'] in refs]
    owned=[u for u in garages if u['attrs'].get('status') in (1,2,3)]
    coach_jobs=coach_history(blocks,economy) if GAME=='ets2' else (None,None)
    result={}
    for aid,g in CAT['goals'].items():
        if aid in existing:continue
        a=UNITS[g['unit']]['attrs'];cls=g['class'];target=a.get('target',1);r=None
        if cls=='achievement_delivery':
            items,mode=condition_progress(a['condition'],jobs);r=report('Пункты доставки',items,target,mode)
            if mode=='sequence':r['next_step']='Выполняйте этапы сверху вниз в указанном направлении. Доставка, сделанная до предыдущего этапа, этот этап не закрывает.'
        elif cls=='achievement_each_company_data':r=company_progress(a,jobs,docks,docks_known)
        elif cls=='achievement_each_delivery_point':r=pair_progress(a,jobs)
        elif cls=='achievement_delivery_log_data':r=log_progress(aid,a,jobs)
        elif cls=='achievement_academy':r=academy_progress(aid,a)
        elif cls=='achievement_bus':r=coach_progress(a,economy,coach_jobs)
        elif cls=='achievement_deliver_cargo_data':
            r=log_progress(aid,{'target_companies':a['targets'],**({'cargos':[a['cargo']]} if 'cargo' in a else {})},jobs)
        elif cls=='achievement_garages_data':
            cities=a.get('cities')
            if a.get('filter')=='hq_country':
                country=PLACES['city_countries'].get(player.get('hq_city'));cities=[c for c,v in PLACES['city_countries'].items() if v==country] if country else []
            if cities:
                items=[]
                by_city={u['id'].removeprefix('garage.'):u['attrs'] for u in garages}
                for city in cities:
                    data=by_city.get(city);status=data.get('status') if data else None
                    v=None if status not in (0,1,2,3) else int(status==3 if a.get('level')=='large' else status>0)
                    items.append(row(city,city_name(city),v))
                r=report('Нужные гаражи',items,target)
            elif isinstance(refs,list) and a.get('filter')!='hq_country':r=report('Гаражи в собственности',[row('owned','Всего гаражей',len(owned),target)],target,owned_cities=[city_name(u['id'].removeprefix('garage.')) for u in owned])
        elif cls=='achievement_limit_data':
            current=a['current'];value=None;items=[]
            if current=='get_cargo_type_count':
                values=economy.get('transported_cargo_types')
                if isinstance(values,list):
                    items=[row(c,cargo_name(c),1) for c in sorted(set(values))]
                    r=report('Перевезённые виды груза',items,target)
                    r['observed']=len(set(values))
            elif current=='get_company_count' and docks_known:
                values=sorted({c.split('.')[0] for c,n in docks.items() if n>0})
                r=report('Компании с выполненными доставками',[row(c,company_name(c),1) for c in values],target)
            elif current=='get_explored_km':
                value=number(player.get('discovary_distance'))
                r=report('Открытая протяжённость дорог',[row('km','Исследовано, км',value,target)],target)
            elif current=='trucks_bought_online':
                value=number(economy.get('trucks_bought_online'));r=report('Покупки через интернет',[row('trucks','Грузовики, купленные онлайн',value,target)],target)
            elif current=='get_unlocked_recruitments_count_current_map':
                values=economy.get('unlocked_recruitments')
                if isinstance(values,list):r=report('Открытые кадровые агентства',[row(c,city_name(c),1) for c in sorted(set(values))],target,caveat='Список текущего профиля. При смене карты Steam может хранить другой максимум; удалённые агентства отдельно не проверяются.')
            elif current=='get_used_ports_count':
                logs=[u['attrs'] for u in blocks if u['class']=='ferry_log'];frefs=logs[0].get('entries') if len(logs)==1 else None
                if isinstance(frefs,list):
                    ports={u['attrs']['ferry'] for u in blocks if u['class']=='ferry_log_entry' and u['id'] in frefs and u['attrs'].get('use_count',0)>0 and isinstance(u['attrs'].get('ferry'),str)}
                    r=report('Порты с записью об отправлении',[row(c,city_name(c),1) for c in sorted(ports)],target,'history')
        elif cls=='achievement_cargo_group_data':
            types=economy.get('transported_cargo_types')
            if isinstance(types,list):
                seen={g for c in types for g in CARGO.get(c,{}).get('groups',[])}
                labels={'containers':'Контейнеры','machinery':'Техника','liquid':'Жидкости','construction':'Строительные грузы','refrigerated':'Рефрижераторные грузы','gooseneck':'Низкорамные грузы','bulk':'Сыпучие грузы','oversize':'Негабаритные грузы','adr':'ADR','livestock':'Скот','inloader':'Стекло / инлоадер'}
                r=report('Группы перевезённых грузов',[row(k,v,1 if k in seen else None) for k,v in labels.items()],target,'history',caveat='Группы восстановлены по определениям грузов; соответствие внутреннему счётчику типов прицепа требует игровой проверки.')
        elif cls=='achievement_xp_data':
            if aid=='jobs_20k_xp':
                window=[];best=0
                for j in jobs or []:
                    if j is None or not j['valid'] or j['xp'] is None or j.get('order_unknown'):window=[];continue
                    window.append(j)
                    while window and sum(x['distance'] for x in window)>a['sum_distance_max']:window.pop(0)
                    best=max(best,sum(x['xp'] for x in window))
                r=report('Опыт в окне последовательных доставок',[row('xp','Максимум XP при сумме до 10 000 км',best if jobs else None,target)],target,'history',next_step='Наберите 20 000 XP за последовательные доставки суммарной длиной менее 10 000 км. Показан лучший найденный отрезок истории.')
            else:
                r=report('Опыт за парковку',[row('parking_xp','XP за парковку тяжёлых грузов',None,target,manual=True)],target,'steam',reason='Этот счётчик читается из Steam. Отдельные доставки не разбиваются по парковочному XP: общий опыт доставки для этого не подходит.',next_step='Выбирайте парковку при доставках Heavy Cargo. Дождитесь сохранения игрой и обновления кэша Steam.')
        elif cls=='achievement_profit_data':
            if GAME=='ats' and a.get('garage_size')=='large':
                large=[u for u in owned if u['attrs'].get('status')==3]
                r=report('Большие гаражи',[row('large','Гаражей максимального размера',len(large) if isinstance(refs,list) else None,a.get('garage_count',1))],a.get('garage_count',1))
            elif 'productivity_min' in a or 'avg_productivity_min' in a:
                large=[u for u in owned if u['attrs'].get('status')==3]
                items=[row(u['id'],city_name(u['id'].removeprefix('garage.')),100*number(u['attrs']['productivity']) if number(u['attrs'].get('productivity')) is not None else None,a.get('productivity_min',a.get('avg_productivity_min',100))) for u in large]
                r=report('Производительность больших гаражей, %',items,a['garage_count'],'saved',next_step=f"Нужно больших гаражей: {a['garage_count']}. Проверка игры происходит при смене недели.")
                r['aggregate']='none'
                r['observed']=sum(i['current'] is not None and i['current']>=i['target'] for i in items) if 'productivity_min' in a else None
                if 'avg_productivity_min' in a:r['summary']=f"Больших гаражей: {len(large)}; средняя производительность: {sum(i['current'] for i in items)/len(items):.1f}%." if items and all(i['current'] is not None for i in items) else 'Недостаточно данных для средней производительности.'
            elif 'avg_daily_profit_min' in a:r=report('Средняя дневная прибыль',[row('profit','Средняя прибыль в день, €',None,a['avg_daily_profit_min'],manual=True)],a['avg_daily_profit_min'],'manual',reason='Расчёт среднего игрового дохода по всем журналам ещё не подтверждён; текущее состояние счёта не является прибылью.',next_step='Сверьте среднюю дневную прибыль в управлении компанией. Проверка достижения происходит при смене дня.')
            else:r=report('Водители с максимальными навыками',[row('male','Мужчины',None,10,manual=True),row('female','Женщины',None,10,manual=True)],20,'manual',reason='Пол и максимальные навыки должны быть сверены с определениями водителей; общий уровень компании не заменяет этот подсчёт.')
        elif cls=='achievement_trigger_data':r=trigger_progress(aid,a)
        elif cls in ('achievement_data','achievement_ferry_data','achievement_position_data','achievement_paintjob_data'):
            r=report('Условие получения',[row(aid,'Действие выполнено',None,manual=True)],1,'manual',reason='Отдельная историческая отметка действия в доступных полях не определена. Факт получения берётся из Steam.')
        if r is None:r=report('Проверка условия',[row(aid,'Выполнено по условию игры',None,target,manual=True)],target,'manual',reason='В доступном сохранении нет подтверждённого источника подробного прогресса.')
        if cls!='achievement_bus' and r.get('mode') in ('history','sequence','series'):
            r['unreadable_records']=bad
            if jobs is None:r['reason']='Поддерживаемый журнал доставок отсутствует. Пункты остаются неизвестными.'
        r['rule_version']=CAT['game_version'];r['rule_class']=cls
        result[aid]=r
    return result

TRIGGERS={
 'fr_landmarks':['Каркассон','Мост Бротонн','Танк «Тигр»','Пейра-ле-Шато','Замок Вантадур','Замок Юссе'],
 'it_volcanoes':['Этна','Везувий'],
 'it_roads_to_rome':['Северный подъезд к Риму','Восточный подъезд к Риму','Юго-восточный подъезд к Риму','Западный подъезд к Риму'],
 'be_landmarks':['Железные Ворота, Румыния','Замок Бран, Румыния','Пляж Поморие, Болгария','Стена героев в Варне, Болгария','Озеро Кючюкчекмедже, Турция'],
 'be_border_crossings':['Видин ↔ Калафат','Русе ↔ Джурджу','Капитан-Андреево ↔ Капыкуле','Лесово ↔ Хамзабейли','Дуранкулак ↔ Вама-Веке'],
 'be_blke_ferry':['Дунайская речная переправа'],
 'sc_oresund_bridge':['Мост Эресунн между Копенгагеном и Мальмё']}

def trigger_progress(aid,a):
    names=TRIGGERS.get(aid,[]) if GAME=='ets2' else [p['name'] for p in json.loads(data_file('trigger_rules.json').read_text(encoding='utf-8')).get(aid,[])]
    return report('Места и проезды',[row(str(i),name,None,manual=True) for i,name in enumerate(names)],a.get('target',1),'manual',
                  reason='Steam сообщает общее число, но не отдельные места. Флаг активации каждого дорожного триггера из сохранения пока не определён.',
                  next_step='Личные отметки помогут исключать уже проверенные места. Проезд рядом и открытая дорога сами по себе не подтверждают срабатывание достижения.')
