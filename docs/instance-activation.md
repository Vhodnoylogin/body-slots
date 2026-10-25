# Atomic requested activation changes

`BodySlots_QueryInstanceActivationInterface(1, sizeof(InstanceActivationApi))` is a separate additive table for changing a bounded set of existing instances owned by one writer. Its table and `ActivationChange` are each 24 bytes. The existing InstanceApi48, all published record layouts and saved formats remain unchanged. No VRIK API copy or legacy change is needed for consumers that do not invoke the new table.

Requested activation is transient provider intent. It does not prove worn equipment, available body binding, visible models, enabled effective placement or interaction eligibility. An effectively disabled Current template can still be requested active. Eligibility and presentation remain separate consumers of the existing inherited/configured views. This service creates no instances, equipment identity, handles, definitions, saved activation or model state.

## Contract

Pass one live writer, a non-null nonempty patch of at most 256 changes, and exact nonzero save-generation/view revisions. Every row initializes its exact `structSize`, supplies an existing nonzero handle, the exact nonzero instance revision and desired flags of zero or `kInstanceActive`. Duplicate handles and duplicate resolved canonical keys are invalid. Rows are patches, not a replacement list; unmentioned instances remain untouched. Every row is checked against the pre-batch state, including rows whose desired flags are already set.

All desired-active rows require the original pinned authored template to be Current, including unchanged-active requests. Missing/Changed templates return `Unavailable`; activation never silently repins them. Desired-inactive rows may target retained Missing/Changed records, including an inactive no-op. Deactivation bypasses only template readiness; writer authority and all CAS checks remain mandatory. An equipment provider can explicitly rebind through the existing UpsertInstance contract.

Initialize the committed `InstanceView` with its existing exact size/version and zero reserved field. Success returns the immediate post-commit header, whose count is the total runtime instance count. Recopy inherited or configured records pinned to its returned scope/view to obtain changed entry revisions. A later concurrent mutation may make that recopy conflict; retry from a fresh observation. Unlike reader conflict behavior, every failure of this mutation leaves the entire output untouched, including Conflict. The caller must supply stable accessible nonoverlapping input/output storage and must not concurrently mutate it.

Malformed pointer/count/flags/zero-handle/duplicates return `InvalidArgument`; unsupported row/output headers return `UnsupportedVersion`. Non-writer/foreign ownership returns `Unauthorized`, missing runtime handles `NotFound`, and stale/zero scope/view/entry revisions `Conflict`. Revision or view exhaustion returns `LimitExceeded`. No failure commits a prefix. Error precedence across multiple independently invalid inputs is not a separate promise.

## Ownership and revisions

The Registry copies bounded input before acquiring its existing mutex. It checks writer authority, exact scope/view and active-template pins, then the private InstanceStore prepares a pure candidate with every row's pre-batch ownership/revision checked. Resolved indices are reused for canonical-key comparisons and application. The candidate lives on the heap; no engine callback or second authority is introduced. Only the existing private Commit publishes the resulting store.

One changed batch advances view exactly once. Each changed row consumes one value from the existing global instance-revision sequence; unchanged rows keep their revisions and every handle remains unchanged. This means a revision need not equal its previous value plus one. The existing maximum sequence sentinel is preserved and all required values plus the view increment are preflighted before publication. A fully valid no-op preserves every counter, even if mutation counters are exhausted. Validation and active-template requirements are still enforced before deciding no-op.

Authored registry/configuration revisions and both saved customization stores are unchanged. Existing inherited/configured readers observe the same requested flags and one new view under the same registry lock. Runtime release/reset semantics are unchanged: writer release removes its instances and save-scope reset invalidates old handles. No batch activation state is serialized.

## Offline checks

```text
python tools/build.py --sdk-root <public SDK> --output <new external directory>
```

The actual production BodySlots DLL is queried by `instance_activation_tests.cpp`, covering negotiation, independent ownership, mixed changes/no-op, output and state sentinels, stale/missing templates, enabled-versus-active semantics, unchanged saved overrides and concurrent inherited reads. `instance_activation_store_tests.cpp` uses test-only private access to the actual store for view/global-revision exhaustion, exact remaining sequence capacity, no-op at exhaustion, duplicate keys and pure candidate publication. It is not a replacement implementation or a public test ABI.

`activation-manifest.json` pins the DLL, compiler, public headers, production sources, fixtures and build argv/exit/timeout limits. Raw statuses/CAS values and sentinel results stay in the external build log. The existing synthetic SKSE host separately verifies plugin lifecycle/save behavior. These offline checks do not qualify Skyrim, actual equipment or rendered presentation.
