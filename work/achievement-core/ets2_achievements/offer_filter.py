"""Pure offer matching; native market integration must supply verified fields.

Outcome-only conditions remain unknown until delivery. History estimates are
not confirmations: callers must explicitly supply confirmed condition IDs.
"""
from . import tracking


def _match(uid, offer, confirmed, depth=0):
    if uid in confirmed:
        return False
    if depth > 40 or uid not in tracking.UNITS:
        return None
    unit = tracking.UNITS[uid]
    attrs = unit['attrs']
    kind = unit['class'].removeprefix('achievement_delivery_')
    if kind == 'sequence':
        # An unknown prior step must not be invented as completed. Expose all
        # unconfirmed steps as candidates until ordering is established.
        return tracking.any3(_match(child, offer, confirmed, depth+1)
                             for child in attrs.get('list', []))
    if kind in ('each', 'any'):
        operation = tracking.all3 if kind == 'each' else tracking.any3
        return operation(_match(child, offer, confirmed, depth+1)
                         for child in attrs['list'])
    if kind == 'count':
        return _match(attrs['condition'], offer, confirmed, depth+1)
    return tracking.match(uid, offer)


def select_offers(offers, planned, *, confirmed=None, completed=()):
    """Return a union with per-offer goal attribution; never intersect goals.

    An empty effective plan restores all offers. Unsupported rules explicitly
    disable this prototype rather than pretending all markets are filtered.
    Input collections and the achievement catalogue are never mutated.
    """
    offers = list(offers)
    confirmed = confirmed or {}
    planned = list(dict.fromkeys(goal for goal in planned if goal not in completed))
    rules = {}
    unsupported = []
    for goal in planned:
        definition = tracking.CAT['goals'].get(goal)
        if not definition:
            unsupported.append(goal)
            continue
        unit = tracking.UNITS[definition['unit']]
        delivery_classes = {'achievement_delivery', 'achievement_delivery_log_data',
                            'achievement_each_company_data', 'achievement_each_delivery_point',
                            'achievement_each_cargo_data', 'achievement_cargo_group_data',
                            'achievement_truck_brand_data', 'achievement_deliver_cargo_data'}
        if unit['class'] not in delivery_classes:
            continue
        if unit['class'] == 'achievement_delivery' and 'condition' in unit['attrs']:
            rules[goal] = ('condition', unit['attrs']['condition'])
        elif unit['class'] == 'achievement_each_company_data':
            rules[goal] = ('targets', tuple(unit['attrs'].get('targets', [])))
        else:
            unsupported.append(goal)
    if unsupported:
        return dict(active=False, reason='unsupported_rules', unsupported=unsupported,
                    offers=[dict(offer=o, goals=[], uncertain=[]) for o in offers])
    if not rules:
        return dict(active=False, reason='no_delivery_goals', unsupported=[],
                    offers=[dict(offer=o, goals=[], uncertain=[]) for o in offers])
    result = []
    for original in offers:
        # Offer inputs must never masquerade as successful completed deliveries.
        offer = dict(valid=True, source=None, target=None, source_city=None,
                     target_city=None, cargo=None, damage=None, late=None,
                     parking=None, route=None)
        offer.update({key: value for key, value in original.items()
                      if key not in ('valid', 'damage', 'late', 'parking')})
        goals, uncertain = [], []
        for goal, (kind, rule) in rules.items():
            done = set(confirmed.get(goal, ()))
            if kind == 'condition':
                outcome = _match(rule, offer, done)
            else:
                remaining = [target for target in rule
                             if target not in done and 'target:'+target not in done]
                outcome = (False if not remaining else None if offer['target'] is None
                           else offer['target'] in remaining)
            if outcome is not False:
                goals.append(goal)
                if outcome is None:
                    uncertain.append(goal)
        if goals:
            result.append(dict(offer=original, goals=goals, uncertain=uncertain))
    return dict(active=True, reason='matches' if result else 'no_matching_offers',
                unsupported=[], offers=result)
