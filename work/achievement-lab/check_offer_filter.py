"""Checks plan union semantics against real extracted delivery definitions."""
from pathlib import Path
import sys
base=Path(__file__).resolve().parents[1]
sys.path.insert(0, str(base/('achievement-core' if (base/'achievement-core').exists() else 'core')))
from ets2_achievements.offer_filter import select_offers

ship = dict(id='ship', target='ocean_sol.vigo', target_city='vigo', cargo='apples')
nuclear = dict(id='nuclear', target='nucleon.alban', target_city='alban', cargo='apples')
wrong = dict(id='wrong', target='other.vigo', target_city='vigo', cargo='apples')
offers = [ship, nuclear, wrong]
ids = lambda result: [row['offer']['id'] for row in result['offers']]
assert ids(select_offers(offers, ['ib_shipyards'])) == ['ship']
assert ids(select_offers(offers, ['ib_shipyards', 'sc_visit_cities'])) == ['ship']
assert not select_offers(offers, ['sc_visit_cities'])['active']
assert ids(select_offers(offers, ['ib_shipyards', 'fr_nucplants'])) == ['ship', 'nuclear']
assert ids(select_offers(offers, ['fr_nucplants'])) == ['nuclear']
assert ids(select_offers(offers, [])) == ['ship', 'nuclear', 'wrong']
assert select_offers([wrong], ['ib_shipyards'])['reason'] == 'no_matching_offers'
assert ids(select_offers(offers, ['ib_shipyards'], confirmed={
    'ib_shipyards': ['.ib_shipyards.vigo']})) == []
unknown = dict(id='unknown', cargo='apples')
assert ids(select_offers([unknown], ['ib_shipyards'])) == ['unknown']
assert select_offers([unknown], ['ib_shipyards'])['offers'][0]['uncertain']
assert ids(select_offers(offers, ['ib_shipyards'], completed=['ib_shipyards'])) == ['ship', 'nuclear', 'wrong']
assert offers == [ship, nuclear, wrong]
print('PASS: real shipyard/company rules; union, goal removal, empty-plan restore, empty result, confirmed exclusion, unknown candidates, completed exclusion, immutable offers.')
