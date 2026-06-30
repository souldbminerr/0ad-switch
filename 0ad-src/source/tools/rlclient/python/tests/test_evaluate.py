from os import path

import zero_ad


game = zero_ad.ZeroAD("http://localhost:6000")
scriptdir = path.dirname(path.realpath(__file__))
with open(path.join(scriptdir, "..", "samples", "arcadia.json"), encoding="utf-8") as f:
    config = f.read()

with open(path.join(scriptdir, "fastactions.js"), encoding="utf-8") as f:
    fastactions = f.read()


def test_return_object():
    game.reset(config)
    result = game.evaluate('({"hello": "world"})')
    assert type(result) is dict
    assert result["hello"] == "world"


def test_return_null():
    result = game.evaluate("null")
    assert result is None


def test_return_string():
    game.reset(config)
    result = game.evaluate('"cat"')
    assert result == "cat"


def test_fastactions():
    state = game.reset(config)
    game.evaluate(fastactions)
    female_citizens = state.units(owner=1, entity_type="female_citizen")
    house_tpl = "structures/spart/house"
    len(state.units(owner=1, entity_type=house_tpl))
    x = 680
    z = 640
    build_house = zero_ad.actions.construct(female_citizens, house_tpl, x, z, autocontinue=True)
    # Check that they start building the house
    state = game.step([build_house])

    def new_house(_=None):
        return state.units(owner=1, entity_type=house_tpl)[0]

    initial_health = new_house().health(ratio=True)
    while new_house().health(ratio=True) == initial_health:
        state = game.step()

    assert new_house().health(ratio=True) >= 1.0
