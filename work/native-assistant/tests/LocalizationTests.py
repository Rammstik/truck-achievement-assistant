"""Exercise language policy, translated rules and the unchanged filtering data contract."""
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
import copy
import json
import re
import sys

ROOT=Path(__file__).resolve().parents[2]/'achievement-core'
sys.path.insert(0,str(ROOT))
from ets2_achievements import language,service,steam_cache
from ets2_achievements.achievement_guidance import CAT,guidance_for
from ets2_achievements.tracking import tracking_details

def main():
    with TemporaryDirectory() as folder:
        home=Path(folder)
        assert language.detect_game_language(home)=='en'
        for locale,expected in [('ru_ru','ru'),('RU-RU','ru'),('en_gb','en'),('de_de','en'),('zh_cn','en'),('','en')]:
            (home/'config.cfg').write_text(f'uset g_lang_init "{locale}"\n',encoding='utf-8')
            assert language.detect_game_language(home)==expected
        (home/'config.cfg').write_text('uset g_lang_init "ru_ru"\n',encoding='utf-8')
        (home/'game.log.txt').write_text('uset g_lang_init "ru_ru"\nuset g_lang "de_de"\n',encoding='utf-8')
        assert language.detect_game_language(home)=='en'
        (home/'game.log.txt').write_text('uset g_lang "ru_ru"\n'+'x'*3_000_000,encoding='utf-8')
        assert language.detect_game_language(home)=='ru'
        with (home/'game.log.txt').open('a',encoding='utf-8') as out:out.write('\nuset g_lang "fr_fr"\n')
        assert language.detect_game_language(home)=='en'
        assert language.resolve('ru','en')=='ru'
        assert language.resolve('en','ru')=='en'
        assert language.resolve('auto','ru')=='ru'
        assert language.resolve('invalid','de_de')=='en'

        schema={'russian':'Русское название','english':'English name'}
        def state(steam,account):
            return {'achievements':[{'id':next(iter(CAT['goals'])),'name':steam_cache.localized(schema),'action':'Без повреждений груза','target':1,'current':None}],'counts':{}}
        with patch.object(service,'accounts',return_value=[{'id':'a','name':'Личный аккаунт'}]),patch.object(service,'profiles',return_value=[]),patch.object(service,'state',side_effect=state):
            for setting,expected in [('auto','en'),('ru','ru'),('en','en')]:
                result=service.snapshot(home,home,{'account':'a','language':setting})
                assert result['language']==expected
                assert result['request']['language']==setting
                assert result['accounts'][0]['name']=='Личный аккаунт'
                assert result['achievements'][0]['name']==schema['russian' if expected=='ru' else 'english']
            # A manual choice remains authoritative after the game language changes.
            (home/'game.log.txt').write_text('uset g_lang "ru_ru"\n',encoding='utf-8')
            assert service.snapshot(home,home,{'account':'a','language':'en'})['language']=='en'
            assert service.snapshot(home,home,{'account':'a','language':'auto'})['language']=='ru'

    source={'accounts':[{'id':'a','name':'Иван'}],'profile':'p','details':{'profile':{'id':'p','name':'Мой профиль'},
        'achievements':{'goal':{'items':[{'id':'source:company.city','name':'Откуда: Завод Scania','current':1,'target':1,'blocked':False}],
        'mode':'history','notice':'По журналу: найденные записи учтены, неизвестные пункты остаются.'}},
        'map_progress':{'hq_city':'city','used_ports':['city']}},'achievements':[{'id':'goal','status':'progress'}]}
    original=copy.deepcopy(source);english=language.present_snapshot(source,'en')
    assert source==original
    assert english['accounts']==source['accounts']
    assert english['details']['profile']==source['details']['profile']
    row=english['details']['achievements']['goal']['items'][0]
    assert row['name']=='From: Scania factory'
    assert {k:v for k,v in row.items() if k!='name'}=={k:v for k,v in source['details']['achievements']['goal']['items'][0].items() if k!='name'}
    assert english['details']['map_progress']==source['details']['map_progress']
    assert english['achievements']==source['achievements']
    assert language.present_snapshot(source,'ru')==source

    # These are proper company names used by the game's English locale.
    proper_names=('Зелёные Поля OOO','Радус')
    def check(text):
        translated=language.translate(text,'en')
        for name in proper_names:translated=translated.replace(name,'')
        assert not re.search('[А-Яа-яЁё]',translated),translated
    for aid in CAT['goals']:
        for item in guidance_for(aid):
            for value in item.values():check(value)
    def visit(value):
        if isinstance(value,dict):
            for key,child in value.items():
                if isinstance(child,str) and key in language._DISPLAY:check(child)
                else:visit(child)
        elif isinstance(value,list):
            for child in value:visit(child)
    visit(tracking_details([],{},set()))
    print(f'PASS: language auto/manual policy, game-log precedence, long logs, Steam locale, identity and rule preservation, {len(CAT["goals"])} translated goal instructions.')

if __name__=='__main__':main()
