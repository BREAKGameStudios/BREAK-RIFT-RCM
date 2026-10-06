#!/usr/bin/env python3

import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry, Resource


ROOT = Path(__file__).resolve().parent
REPOSITORY_ROOT = ROOT.parents[1]
SCHEMA_ROOT = REPOSITORY_ROOT / "schemas" / "v1"


def read_json(path: Path):
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def build_registry() -> Registry:
    registry = Registry()

    for schema_path in sorted(SCHEMA_ROOT.glob("*.schema.json")):
        schema = read_json(schema_path)
        schema_id = schema.get("$id")

        if not schema_id:
            raise ValueError(f"Schema has no $id: {schema_path}")

        registry = registry.with_resource(
            schema_id,
            Resource.from_contents(schema),
        )

    return registry


def main() -> int:
    cases = read_json(ROOT / "cases.json")
    registry = build_registry()
    failures = []

    for case in cases:
        schema_path = SCHEMA_ROOT / case["schema"]
        instance_path = ROOT / case["instance"]
        schema = read_json(schema_path)
        instance = read_json(instance_path)
        validator = Draft202012Validator(
            schema,
            registry=registry,
            format_checker=FormatChecker(),
        )
        errors = sorted(validator.iter_errors(instance), key=lambda item: list(item.path))
        actual_valid = not errors
        expected_valid = bool(case["valid"])

        if actual_valid == expected_valid:
            print(f"PASS {case['name']}")
            continue

        detail = "; ".join(error.message for error in errors) or "unexpectedly valid"
        failures.append(f"{case['name']}: {detail}")
        print(f"FAIL {case['name']}: {detail}")

    if failures:
        print(f"\n{len(failures)} protocol test(s) failed.", file=sys.stderr)
        return 1

    print(f"\n{len(cases)} protocol tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
