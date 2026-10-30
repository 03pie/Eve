"""对照当前 CTest 清单审核 P4 映射；只验证清单与范围，不冒充测试执行通过。"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def audit(matrix, inventory):
    """每项 P4 标签测试须唯一归属；必需场景与附加支持场景必须真实存在且未禁用。"""
    if matrix.get("formatVersion") != 1:
        raise ValueError("unsupported P4 matrix format")
    tests = {row["name"]: row for row in inventory["tests"]}
    if len(tests) != len(inventory["tests"]):
        raise ValueError("duplicate CTest names")
    properties = {name: {prop["name"]: prop["value"] for prop in row.get("properties", [])}
                  for name, row in tests.items()}
    labeled = {name for name, props in properties.items() if "p4" in props.get("LABELS", [])}
    components = matrix["components"]
    requirements = matrix["requirements"]
    for items in (components, requirements):
        identities = [item["id"] for item in items]
        if not identities or len(identities) != len(set(identities)):
            raise ValueError("empty or duplicate acceptance identities")
    support = set(matrix.get("supportTests", []))
    required = set()
    for item in requirements:
        if not item.get("tests"):
            raise ValueError("requirement has no tests: " + item["id"])
        required.update(item["tests"])
    compiled = [(item["id"], re.compile(item["pattern"])) for item in components]
    owners = {name: [identity for identity, pattern in compiled if pattern.search(name)] for name in labeled}
    selected = labeled | support | required
    errors = {
        "missingTests": sorted(selected - tests.keys()),
        "unmappedP4Tests": sorted(name for name, matches in owners.items() if not matches),
        "ambiguousP4Tests": {name: matches for name, matches in owners.items() if len(matches) > 1},
        "undeclaredSupportTests": sorted(required - labeled - support),
        "disabledTests": sorted(name for name in selected if properties.get(name, {}).get("DISABLED", False)),
        "emptyComponents": sorted(identity for identity, _ in compiled if not any(identity in ids for ids in owners.values()))
    }
    if not labeled:
        errors["missingP4Label"] = True
    return {
        "inventoryOnly": True,
        "scope": matrix["scope"],
        "verified": not any(errors.values()),
        "p4LabelCount": len(labeled),
        "supportTests": sorted(support),
        "requiredReferenceCount": len(required),
        "components": {identity: sorted(name for name, ids in owners.items() if identity in ids) for identity, _ in compiled},
        "requirements": requirements,
        "selectionRegex": "^(" + "|".join(re.escape(name) for name in sorted(selected)) + ")$",
        "errors": errors
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--matrix", type=Path, default=Path(__file__).resolve().parents[1] / "tests/acceptance/p4-matrix.json")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    matrix = json.loads(args.matrix.read_text(encoding="utf-8"))
    source = subprocess.check_output([args.ctest, "--test-dir", str(args.build_dir.resolve()), "--show-only=json-v1"],
                                     encoding="utf-8")
    result = audit(matrix, json.loads(source))
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"verified": result["verified"], "inventoryOnly": True,
                      "p4LabelCount": result["p4LabelCount"], "supportTests": result["supportTests"],
                      "requiredReferenceCount": result["requiredReferenceCount"], "errors": result["errors"]},
                     ensure_ascii=False))
    return 0 if result["verified"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
