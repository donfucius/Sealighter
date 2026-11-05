"""Compare full/slim JSONL captures; exits nonzero on acceptance failure.

Use --same-records for replay of identical EVENT_RECORDs, where the record sets
must match exactly. Independent live runs cannot: the subscribed providers see
the whole machine, so measured drift between two unslied runs of the same sample
and window was 5-12% of records. Live comparisons therefore bound the total
drift and require that no substantial bucket disappears; both rules are skipped
under --same-records in favour of exact equality.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

OMIT = {"activity_id", "event_name", "event_flags"}
REQUIRED = {"event_id", "event_opcode", "event_version", "process_id", "thread_id",
            "timestamp", "trace_name", "provider_name", "task_name"}
# A bucket this large in either capture and absent from the other is coverage
# loss, not noise: across three live pairs the largest vanished bucket had 2 rows,
# while the sample's own Dynomon buckets run 11 and 7, so 5 separates the two.
VANISHED_BUCKET_FLOOR = 5
MAX_DRIFT_PERCENT = 20.0


def load(path):
    records = []
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        record = json.loads(line)
        if not isinstance(record, dict) or not REQUIRED <= record.get("header", {}).keys():
            raise ValueError(f"{path}:{number}: missing required header keys")
        records.append(record)
    if not records:
        raise ValueError(f"{path}: empty capture")
    return records


def distribution(records):
    return Counter((r["header"]["provider_name"], r["header"]["task_name"],
                    r["header"]["event_id"], r["header"]["trace_name"]) for r in records)


def compare_distributions(full, slim, same_records):
    """Exact for identical replay, bounded drift plus a vanished-bucket floor for live runs."""
    before, after = distribution(full), distribution(slim)
    if same_records:
        if before != after:
            raise ValueError("provider/task/event/trace counts differ")
        return 0.0
    keys = set(before) | set(after)
    lost = sorted(k for k in keys if before[k] >= VANISHED_BUCKET_FLOOR and not after[k])
    gained = sorted(k for k in keys if after[k] >= VANISHED_BUCKET_FLOOR and not before[k])
    if lost or gained:
        raise ValueError(
            "provider/task/event/trace counts differ: a substantial bucket did not survive "
            f"slimming: {lost + gained}")
    drift = sum(abs(before[k] - after[k]) for k in keys)
    total = max(sum(before.values()), sum(after.values()), 1)
    percent = 100 * drift / total
    if percent > MAX_DRIFT_PERCENT:
        raise ValueError(
            f"provider/task/event/trace counts differ: {percent:.1f}% of records moved, over the "
            f"{MAX_DRIFT_PERCENT:.1f}% a live capture of this workload is allowed")
    return percent


def errors(records, typed):
    result = Counter()
    for r in records:
        header = r["header"]
        names = ([k for k, v in (r.get("property_types") or {}).items() if v in {"ERROR", "OTHER"}]
                 if typed else r.get("parse_errors", []))
        for name in names:
            result[(header["provider_name"], header["event_id"], name)] += 1
    return result


def coverage(records, typed):
    """Per (provider, event_id): rows that carry the diagnostic over rows in that bucket.

    Compared between captures as a ratio, because two live runs of one workload
    hold different numbers of Sysmon rows; a reduction that silently swallowed a
    decode failure would drop the ratio while the total stayed put.
    """
    flagged, total = Counter(), Counter()
    for r in records:
        bucket = (r["header"]["provider_name"], r["header"]["event_id"])
        total[bucket] += 1
        if errors([r], typed):
            flagged[bucket] += 1
    return {b: round(flagged[b] / total[b], 3) for b in flagged}


def normalized(record):
    record = json.loads(json.dumps(record))
    record.pop("property_types", None)
    record.pop("parse_errors", None)
    for key in OMIT:
        record["header"].pop(key, None)
    # Precision applies to the header and FILETIME/SYSTEMTIME properties only.
    for obj in (record["header"], record.get("properties", {})):
        for key, value in obj.items():
            if isinstance(value, str):
                obj[key] = re.sub(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\.\d{3}Z$", r"\1Z", value)
    return record


def verify(full_path, slim_path, same_records=False, channel_path=None):
    full, slim = load(full_path), load(slim_path)
    drift_percent = compare_distributions(full, slim, same_records)
    if not all("property_types" in r or "raw" in r for r in full):
        raise ValueError("baseline must contain property_types (except raw records)")
    for r in slim:
        if "property_types" in r or OMIT & r["header"].keys():
            raise ValueError("slim capture still contains omitted metadata")
        if not re.fullmatch(r"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}Z", r["header"]["timestamp"]):
            raise ValueError("slim timestamp is not millisecond UTC")
        if "parse_errors" in r and not r["parse_errors"]:
            raise ValueError("empty parse_errors must be omitted")
    before_names, after_names = errors(full, True), errors(slim, False)
    if set(before_names) != set(after_names):
        raise ValueError(f"parse_errors do not match baseline ERROR/OTHER fields: "
                         f"names differ {sorted(set(before_names) ^ set(after_names))}")
    if coverage(full, True) != coverage(slim, False):
        raise ValueError("parse_errors do not match baseline ERROR/OTHER fields: "
                         f"coverage {coverage(full, True)} vs {coverage(slim, False)}")
    if same_records and [normalized(r) for r in full] != [normalized(r) for r in slim]:
        raise ValueError("record values or order changed")
    if same_records:
        for before, after in zip(full, slim):
            expected = {k for k, v in (before.get("property_types") or {}).items() if v in {"ERROR", "OTHER"}}
            if expected != set(after.get("parse_errors", [])):
                raise ValueError("per-record parse_errors differ")
    if channel_path:
        # Export the channel's JSON payloads for precisely the same run. Full
        # record multiset equality also checks per-source PID counts.
        channel = load(channel_path)
        canonical = lambda rows: Counter(json.dumps(r, sort_keys=True) for r in rows)
        if canonical(slim) != canonical(channel):
            raise ValueError("channel/file record parity failed")
    full_bytes, slim_bytes = full_path.stat().st_size, slim_path.stat().st_size
    if slim_bytes >= full_bytes:
        raise ValueError("slim capture did not reduce bytes")
    return {"records": len(slim), "full_bytes": full_bytes, "slim_bytes": slim_bytes,
            "saved_percent": round(100 * (1 - slim_bytes / full_bytes), 2),
            "distribution_drift_percent": round(drift_percent, 2),
            "diagnostic_fields": sum(errors(slim, False).values()),
            "same_record_order_checked": same_records,
            "channel_parity_checked": channel_path is not None}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("full", type=Path)
    parser.add_argument("slim", type=Path)
    parser.add_argument("--same-records", action="store_true")
    parser.add_argument("--channel-jsonl", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.full, args.slim, args.same_records, args.channel_jsonl), indent=2))
    except (ValueError, KeyError, TypeError, OSError) as exc:
        parser.exit(1, f"FAIL: {exc}\n")
