"""Meaningful condition checks for the full-catalogue tracker."""
from pathlib import Path
import sys,copy,json,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'achievement-core'))
from ets2_achievements.tracking import *
from ets2_achievements.save_reader import text_sii,load_save
from ets2_achievements.viewpoint_progress import CATALOGUE as VIEWPOINTS,viewpoint_details

def raw(uid,src='other.paris',dst='other.strasbourg',cargo='petrol',damage='0',remaining='10',time=1,kind='compn',mass='50000',park='3',pay='1000',fine='0',auto='0'):
    p=['0']*24;p[0]=str(time);p[1]='company.volatile.'+src;p[2]='company.volatile.'+dst;p[3]='cargo.'+cargo
    p[4]='1000';p[5]=pay;p[6]='100';p[7]=damage;p[8]=remaining;p[10]=auto;p[11]=park;p[14]=fine;p[18]=kind;p[22]=mass
    return {'class':'delivery_log_entry','id':uid,'attrs':{'params':p}}

def history(*units):return delivery_history([{'class':'delivery_log','id':'log','attrs':{'version':1,'entries':[u['id'] for u in units]}},*units])[0]
def details(*units):return tracking_details([{'class':'delivery_log','id':'log','attrs':{'version':1,'entries':[u['id'] for u in units]}},*units],{},set())

assert len(CAT['goals'])==114 and len(tracking_details([],{},set()))==114
assert all(u['attrs'].get('condition') in UNITS for u in UNITS.values() if 'condition' in u['attrs'])
assert all(s in CAT['scenarios'] for c in CAT['chapters'].values() for s in c['scenarios'])
assert not any('//' in s for c in CAT['chapters'].values() for s in c['scenarios'])
assert all(not CAT['scenarios'][s]['name'].startswith('aca_') for c in CAT['chapters'].values() for s in c['scenarios'])
assert PLACES['city_countries']['pecs']==PLACES['city_countries']['budapest']=='hungary'
assert details(raw('a','other.pskov','other.kaliningrad'))['ba_to_russia']['observed'] is None
assert details(raw('a','other.kaliningrad','other.pskov'))['ba_to_russia']['observed']==1
# Mediterranean delivery filters: ADR, exact factory/city, and source versus target.
root=UNITS[CAT['goals']['ib_solar_power_plants']['unit']]['attrs']['condition'];leaf=UNITS[root]['attrs']['list'][0]
j=history(raw('a',dst='engeron.badajoz'))[0]
assert match(leaf,j) is True
assert match(leaf,history(raw('a',dst='engeron.badajoz',cargo='roof_tiles'))[0]) is False
assert match(leaf,history(raw('a',dst='engeron.mod_city'))[0]) is False
assert match(leaf,history(raw('a',src='engeron.badajoz',dst='other.paris'))[0]) is False
assert match(leaf,history(raw('a',dst='engeron.badajoz',cargo='unknown_adr'))[0]) is None
# Marble blocks have no zero-damage condition; slabs do.
r=details(raw('a','rock_eater.athens',cargo='marb_blck',damage='.1'),raw('b','rock_eater.athens',cargo='marb_slab',damage='.1',time=2),raw('c','rock_eater.athens',cargo='marb_slab',time=3))['gr_marble']
assert [i['current'] for i in r['items']]==[1,1]
# Two jobs per industrial category, not six jobs at one factory.
r=details(*(raw(str(i),dst='lvr.riga',time=i) for i in range(6)))['ba_industry']
assert r['observed']==2 and [i['current'] for i in r['items']]==[2,None,None]
# Country uniqueness: Norway excluded, repeated Sweden is one country.
r=details(raw('a','other.alta','other.stockholm','salm_fillet'),raw('b','other.tromso','other.goteborg','salm_fillet',time=2),raw('c','other.alta','other.oslo','salm_fillet',time=3))['pc_salm_fillet']
assert r['observed']==1
# Ordered chains: a future leg done early does not count; reverse direction fails.
r=details(raw('a','other.kiruna','other.svolvaer'),raw('b','other.ivalo','other.kiruna',time=2))['pc_polar_circle']
assert [i['current'] for i in r['items']]==[1,None]
r=details(raw('a','other.kiruna','other.ivalo'))['pc_polar_circle'];assert r['observed'] is None
r=details(raw('a','other.ivalo','other.kiruna'),raw('b','other.kiruna','other.svolvaer',time=2,remaining='-1'))['pc_polar_circle'];assert r['observed']==1
r=details(raw('a','other.ivalo','other.kiruna'),raw('b','other.kiruna','other.svolvaer',time=2))['pc_polar_circle'];assert r['observed']==2
# Series expose current tail and historical best separately; free roam is ignored.
r=details(raw('a'),raw('b',time=2),raw('c',time=3,damage='.01'),raw('d',time=4))['5_jobs_in_a_row']
assert r['observed']==1 and r['best_observed']==2
r=details(raw('a'),raw('b',time=2,kind='freerm'),raw('c',time=3))['5_jobs_in_a_row'];assert r['observed']==2
r=details(raw('a',auto='1'))['5_jobs_in_a_row'];assert r['observed'] is None and 'caveat' in r
# Mass uses the last N jobs, not a lifetime sum or the whole growing streak.
r=details(*(raw(str(i),cargo='silo',time=i,mass='70000') for i in range(4)))['st_mass']
assert [i['current'] for i in r['items']]==[3,210]
# Duplicate refs are not new jobs, orphan units do not count, malformed records fail closed.
a=raw('a');units=[{'class':'delivery_log','id':'log','attrs':{'version':1,'entries':['a','a']}},a,raw('orphan')]
assert len(delivery_history(units)[0])==1
units[-1]['id']='a';assert delivery_history(units)[1]==1
r=details(raw('a',time=2),raw('b',time=1))['5_jobs_in_a_row'];assert r['observed'] is None
assert all3([False,None]) is False and any3([True,None]) is True and any3([False,None]) is None
# A discovery UID is not an achievement-trigger flag; Steam count is not spread across points.
r=tracking_details([],{'discovered_items':[2514406638413545473]},set())['it_volcanoes']
assert all(i['current'] is None for i in r['items'])
assert len(tracking_details([],{},set())['it_roads_to_rome']['items'])==4
# Iceland: only completed stages in order, and only distinct eligible farms.
ring=[raw('a','other.hofn','other.akureyri'),raw('b','other.reykjavik','other.hofn',time=2),
      raw('c','other.hofn','other.akureyri',damage='.1',time=3),
      raw('d','other.hofn','other.akureyri',remaining='-1',time=4),
      raw('e','other.hofn','other.akureyri',time=5),raw('f','other.akureyri','other.reykjavik',time=6)]
assert [i['current'] for i in details(*ring[:4])['is_ringroad']['items']]==[1,None,None]
assert details(*ring)['is_ringroad']['observed']==3
sheep=details(raw('a','vikingabyli.hofn',cargo='live_sheep'),
              raw('b',dst='vikingabyli.hofn',cargo='live_sheep',time=2),
              raw('c',dst='vikingabyli.selfoss',cargo='live_sheep',time=3),
              raw('d',dst='vikingabyli.reykjavik',time=4),
              raw('e','vikingabyli.borgarnes','vikingabyli.thorshofn','live_sheep',time=5))['is_sheep']
assert sheep['observed']==2 and len(sheep['items'])==6
assert {i['id'] for i in sheep['items'] if i['current']==1}=={'vikingabyli.hofn','vikingabyli.selfoss'}
viewpoint_ids=[int(i['id']) for i in VIEWPOINTS['rules']['is_cutscenes']['items']]
views=viewpoint_details({'discovered_cutscene_items':viewpoint_ids[:2],
                         'discovered_cutscene_items_states':[2,1]})['is_cutscenes']
assert (views['total'],views['target'],views['completed'],views['unknown'])==(11,8,1,1)
# The coach log uses a separate economy reference and stores satisfaction as a fraction.
def coach_entry(uid,market,distance,satisfaction,flags=1):
    return {'class':'bus_job_log_entry','id':uid,'attrs':{'type':market,'travelled_distance':distance,
                                                       'satisfaction':satisfaction,'flags':flags}}

coach_economy={'bus_job_log':'coachlog','long_duty':2,'tight_schedule':0}
coach_log={'class':'bus_job_log','id':'coachlog','attrs':{'version':0,'entries':['quick']}}
quick=coach_entry('quick','quick_job',11,0.8497899770736694)
coach=tracking_details([coach_log,quick],coach_economy,set())
assert round(coach['bus_job_satisfaction']['observed'],2)==84.98
assert coach['bus_jobs_owned_distance']['observed']==0
assert [i['current'] for i in coach['bus_skills']['items']]==[1,0,None,None,None,None]
assert coach['bus_jobs_consecutive']['mode']=='steam' and coach['bus_jobs_consecutive']['observed'] is None
coach_log['attrs']['entries']+=['owned','owned','unknown','missing']
owned=coach_entry('owned','company_job',340,0.95,flags=0)
unknown=coach_entry('unknown','unsupported_market',5000,1)
coach=tracking_details([coach_log,quick,owned,unknown],coach_economy,set())
assert coach['bus_jobs_owned_distance']['observed']==340 and coach['bus_job_satisfaction']['observed']==95
assert coach_history([coach_log,quick,owned],{})==(None,None)
coach_log['attrs']['version']=2
assert coach_history([coach_log,quick,owned],coach_economy)==(None,None)
# Chapter membership comes from official chapter lists, not ID prefixes.
r=tracking_details([],{},set())['aca_interior']
assert len(r['items'])==8 and all(CAT['scenarios'][i['id']]['chapter']=='academy.chapter.book1.4' for i in r['items'])
assert 'academy.scenario.book1.3_05' in {i['id'] for i in r['items']}
assert len(tracking_details([],{},set())['aca_truck_driving']['required_ids'])==2
# Text save parity: indexed arrays reordered, gaps/duplicate indices rejected, scalar hex floats supported.
t='''SiiNunit
{
economy : e {
 visited_cities: 2
 visited_cities[1]: paris
 visited_cities[0]: strasbourg
 transported_cargo_types: 0
 player: p
}
player : p {
 discovary_distance: &447a0000
 hq_city: paris
}
}'''
b=text_sii(t);assert b[0]['attrs']['visited_cities']==['strasbourg','paris'] and b[0]['attrs']['transported_cargo_types']==[]
assert b[1]['attrs']['discovary_distance']==1000
assert 'visited_cities' not in text_sii(t.replace('[1]','[0]'))[0]['attrs']
before=copy.deepcopy(b);tracking_details(b,b[0]['attrs'],set());assert b==before
json.dumps(tracking_details(b,b[0]['attrs'],set()),allow_nan=False)
print('PASS: 114 goals, including coach job history, owned distance and skills, plus existing delivery, map and save regressions.')
