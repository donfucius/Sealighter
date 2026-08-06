# Filtering

Filters are per-provider and run **after** the kernel-side `keywords_any`/`keywords_all` and `level` checks but
**before** the event is converted to JSON — so filtering is much cheaper than parsing everything. Full user-facing
documentation: `docs/FILTERING.md`. Implementation lives in `sealighter/sealighter_controller.cpp`
(`add_filters_to_vector`, `add_filters`) and `sealighter/sealighter_predicates.h`.

## Filter lists: any_of / all_of / none_of

A provider's `filters` object has three lists. Semantics (verified in code, `add_filters`):

- `any_of` → event reported if it matches **any** filter in the list (`sealighter_any_of`).
- `all_of` → event reported only if it matches **all** filters (`sealighter_all_of`; empty list matches nothing).
- `none_of` → event reported only if it matches **none** (`sealighter_none_of`).

The three lists are combined with a final `sealighter_all_of` — i.e. **AND** between lists. Within a list, if a
filter key is an array, the array acts as a mini `any_of` (event needs to match only one array item).

If a provider has no `filters`, an unfiltered callback is attached and everything is logged.

## Filter types

### Header filters (match event header metadata)

| Key | Matches on | Notes |
| --- | --- | --- |
| `event_id_is` | Event ID | `predicates::id_is` |
| `opcode_is` | Opcode | `predicates::opcode_is` |
| `process_id_is` | PID of generating process | `predicates::process_id_is` |
| `version_is` | Event version | `predicates::version_is` |
| `activity_id_is` | Activity ID GUID string | custom `sealighter_activity_id_is`; use in `none_of` to drop events without an activity ID (NULL GUID) |
| `process_name_contains` | Image name of generating process | custom `sealighter_process_name_contains` — opens the process (`OpenProcess` + `GetProcessImageFileNameA`), so it is relatively heavy; runs as admin |

### Property filters (typed, match event properties)

These require `name`, `value`, and `type`. `type` is the property's `TDH_INTYPE` label as emitted in
`property_types` (see [Event format](../data-model/event-format.md)) and is resolved once at startup, not per event.

| Key | Semantics |
| --- | --- |
| `property_is` | Property exists, matches type and exact value. Supports `STRINGA`, `STRINGW`, `INT8`…`UINT64`, **and `GUID` (added in the current uncommitted change — not yet in docs/FILTERING.md)**. |
| `property_equals` / `property_iequals` | Equality (case-insensitive variant) — string types only |
| `property_contains` / `property_icontains` | Substring (case-insensitive variant) |
| `property_starts_with` / `property_istarts_with` | Prefix |
| `property_ends_with` / `property_iends_with` | Suffix |

Implementation: template helpers in `sealighter_controller.cpp` build `krabs::predicates` (`kpc::equals`,
`kpc::contains`, `kpc::starts_with`, `kpc::ends_with` with `std::equal_to`/`iequal_to`) or the custom
`sealighter_property_is<T>` in `sealighter_predicates.h`.

### Discovery filters (Sealighter-specific, in `sealighter_predicates.h`)

| Key | Behavior |
| --- | --- |
| `any_field_contains` | Case-insensitive search of every property name and every string field, including raw byte search for ANSI *and* wide variants of the needle in binary-ish properties (`sealighter_any_field_contains`). Great for unknown providers (see `docs/SCENARIOS.md`). |
| `max_events_total` | Report at most N events total (`sealighter_max_events_total`). |
| `max_events_id` | Report at most N events with a given ID — object form `{ "id_is": <id>, "max_events": <n> }` (`sealighter_max_events_id`). |

## How filters map to code

`add_filters_to_vector` (`sealighter_controller.cpp`) parses one JSON filter object into
`std::vector<std::shared_ptr<predicates::details::predicate_base>>`:

- `event_id_is`, `opcode_is`, `process_id_is`, `version_is` → `add_filter_to_vector_basic<T>` (scalar or array→`any_of`)
- `property_is` → `add_filter_to_vector_property_is` (typed, array→`any_of`)
- `property_*` comparers → `add_filter_to_vector_property_compare<ComparerA, ComparerW>` (STRINGA/STRINGW only)
- `max_events_total`, `max_events_id`, `any_field_contains`, `process_name_contains`, `activity_id_is` → custom classes

`add_filters` then wraps the per-provider lists into `sealighter_any_of`/`sealighter_all_of`/`sealighter_none_of`,
combines them with a final `sealighter_all_of`, and attaches the callback through a `krabs::event_filter`
(or a bare `add_on_event_callback` when there are no filters).

Parse errors in any filter (e.g. a property filter missing `name`/`value`/`type`, or a bad `type`) throw
`nlohmann::detail::exception`, which surfaces as `SEALIGHTER_ERROR_PARSE_FILTER`.

## Gotchas

- Property comparers (`property_equals` etc.) only accept `STRINGA`/`STRINGW` types; anything else raises a parse error.
- `property_is` with a `type` not in its switch (e.g. `FLOAT`, `BOOLEAN`) is silently ignored (no predicate added).
- `process_name_contains` depends on admin rights to open the target process.
- Filter evaluation order: `keywords_any`/`keywords_all` → `level` (kernel side) → filters (userland) → output.
