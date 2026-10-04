"""Test matrices: the full cartesian product (`--depth full`) or a deterministic pairwise subset (`--depth quick`).

A matrix is an ordered mapping of dimension name to its values. `valid` receives a (possibly partial) assignment
of dimension name to value and returns False only when that assignment can never be part of a valid combination,
so the same predicate prunes partial and complete combinations.
"""

from __future__ import annotations

import itertools
from collections.abc import Callable, Iterator, Mapping, Sequence
from typing import Any, Literal

Depth = Literal["quick", "full"]
Combination = dict[str, Any]
Predicate = Callable[[Mapping[str, Any]], bool]

DEPTHS: tuple[Depth, ...] = ("quick", "full")


def _always(_: Mapping[str, Any]) -> bool:
    return True


def full_product(dimensions: Mapping[str, Sequence[Any]], valid: Predicate = _always) -> list[Combination]:
    names = list(dimensions)
    result = []
    for values in itertools.product(*(dimensions[name] for name in names)):
        combination = dict(zip(names, values))
        if valid(combination):
            result.append(combination)
    return result


def _pairs(sizes: Sequence[int]) -> Iterator[tuple[int, int, int, int]]:
    for first, second in itertools.combinations(range(len(sizes)), 2):
        for a in range(sizes[first]):
            for b in range(sizes[second]):
                yield first, a, second, b


def pairwise(dimensions: Mapping[str, Sequence[Any]], valid: Predicate = _always) -> list[Combination]:
    """Combinations covering every pair of values of every two dimensions that some valid combination can hold.

    Greedy and deterministic: each round seeds a combination with the first uncovered pair and then assigns the
    remaining dimensions in order, picking the value that covers the most uncovered pairs (the first on ties).
    A seed pair that cannot be completed into a valid combination is dropped.
    """
    names = list(dimensions)
    values = [list(dimensions[name]) for name in names]
    if any(not options for options in values):
        return []
    if len(names) < 2:
        return full_product(dimensions, valid)

    sizes = [len(options) for options in values]
    uncovered = dict.fromkeys(_pairs(sizes))

    def assignment(indices: Mapping[int, int]) -> Combination:
        return {names[dim]: values[dim][index] for dim, index in sorted(indices.items())}

    def gain(indices: Mapping[int, int], dim: int, index: int) -> int:
        count = 0
        for other, other_index in indices.items():
            key = (other, other_index, dim, index) if other < dim else (dim, index, other, other_index)
            count += key in uncovered
        return count

    result: list[Combination] = []
    seen: set[tuple[int, ...]] = set()
    while uncovered:
        first, a, second, b = next(iter(uncovered))
        indices = {first: a, second: b}
        if not valid(assignment(indices)):
            del uncovered[(first, a, second, b)]
            continue
        complete = True
        for dim in range(len(names)):
            if dim in indices:
                continue
            best = None
            best_gain = -1
            for index in range(sizes[dim]):
                candidate = {**indices, dim: index}
                if not valid(assignment(candidate)):
                    continue
                score = gain(indices, dim, index)
                if score > best_gain:
                    best, best_gain = index, score
            if best is None:
                complete = False
                break
            indices[dim] = best
        if not complete:
            fallback = _complete(sizes, {first: a, second: b}, lambda candidate: valid(assignment(candidate)))
            if fallback is None:
                del uncovered[(first, a, second, b)]
                continue
            indices = fallback
        for x, y in itertools.combinations(range(len(names)), 2):
            uncovered.pop((x, indices[x], y, indices[y]), None)
        key = tuple(indices[dim] for dim in range(len(names)))
        if key not in seen:
            seen.add(key)
            result.append(assignment(indices))
    return result


_SEARCH_LIMIT = 200_000


def _complete(sizes: Sequence[int], fixed: Mapping[int, int], valid: Callable[[Mapping[int, int]], bool]) -> dict[int, int] | None:
    """First valid combination holding `fixed` in product order, when the greedy choice ran into a dead end."""
    free = [dim for dim in range(len(sizes)) if dim not in fixed]
    for tried, choice in enumerate(itertools.product(*(range(sizes[dim]) for dim in free))):
        if tried >= _SEARCH_LIMIT:
            return None
        candidate = {**fixed, **dict(zip(free, choice))}
        if valid(candidate):
            return candidate
    return None


def combinations(dimensions: Mapping[str, Sequence[Any]], depth: Depth = "quick", valid: Predicate = _always) -> list[Combination]:
    if depth not in DEPTHS:
        raise ValueError(f"depth must be one of {DEPTHS}, not {depth!r}")
    return full_product(dimensions, valid) if depth == "full" else pairwise(dimensions, valid)


def covered_pairs(combinations: Sequence[Mapping[str, Any]], names: Sequence[str]) -> set[tuple[str, Any, str, Any]]:
    """Every (name, value, name, value) pair held by some combination; values must be hashable or reprs are used."""
    result: set[tuple[str, Any, str, Any]] = set()
    for combination in combinations:
        for first, second in itertools.combinations(names, 2):
            result.add((first, _key(combination[first]), second, _key(combination[second])))
    return result


def _key(value: Any) -> Any:
    try:
        hash(value)
    except TypeError:
        return repr(value)
    return value
