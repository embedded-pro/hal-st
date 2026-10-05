import itertools

import pytest

from hal_st_validation.pairwise import combinations, covered_pairs, full_product, pairwise

MATRIX = {
    "mode": ["edge", "center"],
    "div": [1, 2, 4, 8, 16, 32, 64],
    "freq": [100, 1000, 10000, 20000, 50000, 200000],
    "duty": [0, 12.5, 50, 90, 100],
    "sync": [0, 1],
}


def all_pairs(dimensions, valid=lambda values: True):
    names = list(dimensions)
    result = set()
    for combination in full_product(dimensions, valid):
        for first, second in itertools.combinations(names, 2):
            result.add((first, combination[first], second, combination[second]))
    return result


def test_full_product_is_the_cartesian_product():
    result = full_product(MATRIX)
    assert len(result) == 2 * 7 * 6 * 5 * 2
    assert result[0] == {"mode": "edge", "div": 1, "freq": 100, "duty": 0, "sync": 0}
    assert len({tuple(combination.values()) for combination in result}) == len(result)


def test_pairwise_covers_every_pair():
    result = pairwise(MATRIX)
    assert covered_pairs(result, list(MATRIX)) == all_pairs(MATRIX)
    assert len(result) < len(full_product(MATRIX)) / 10
    assert len(result) >= 7 * 6, "at least the product of the two largest dimensions"


def test_pairwise_is_deterministic_and_ordered():
    first = pairwise(MATRIX)
    assert first == pairwise(MATRIX)
    assert all(list(combination) == list(MATRIX) for combination in first)


def test_pairwise_respects_constraints():
    matrix = {"baud": [600, 9600, 921600], "parity": ["none", "even", "odd"], "stop": [1, 2], "variant": ["interrupt", "dma", "sync"]}

    def valid(values):
        return values.get("variant") != "sync" or (values.get("parity", "none") == "none" and values.get("stop", 1) == 1)

    result = pairwise(matrix, valid)
    assert all(valid(combination) for combination in result)
    assert covered_pairs(result, list(matrix)) == all_pairs(matrix, valid)
    assert {"baud": 921600, "parity": "none", "stop": 1, "variant": "sync"} in result


def test_constraint_needing_backtracking():
    """A greedy choice for `a` can make `c` impossible; the pair is still covered through the fallback search."""
    matrix = {"a": [0, 1], "b": [0, 1], "c": [0, 1]}

    def valid(values):
        if "a" in values and "c" in values:
            return values["a"] == values["c"]
        return True

    result = pairwise(matrix, valid)
    assert covered_pairs(result, list(matrix)) == all_pairs(matrix, valid)


def test_small_and_degenerate_matrices():
    assert pairwise({}) == [{}]
    assert pairwise({"only": [1, 2, 3]}) == [{"only": 1}, {"only": 2}, {"only": 3}]
    assert pairwise({"a": [1, 2], "b": [3]}) == [{"a": 1, "b": 3}, {"a": 2, "b": 3}]
    assert pairwise({"a": [1, 2], "b": []}) == []
    two = {"x": [1, 2, 3], "y": ["p", "q"]}
    assert pairwise(two) == full_product(two), "two dimensions: pairwise is the full product"


def test_unhashable_values():
    matrix = {"dead": [250, [500, 1000]], "inversion": [[0, 0], [1, 1]], "sync": [0, 1]}
    result = pairwise(matrix)
    assert len(covered_pairs(result, list(matrix))) == 12


@pytest.mark.parametrize("depth", ["quick", "full"])
def test_combinations_by_depth(depth):
    result = combinations(MATRIX, depth)
    expected = pairwise(MATRIX) if depth == "quick" else full_product(MATRIX)
    assert result == expected
    with pytest.raises(ValueError):
        combinations(MATRIX, "deep")
