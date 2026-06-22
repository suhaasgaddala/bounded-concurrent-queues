# Legacy OrbitQueue Repository Retirement Checklist

This checklist is the final gate before retiring or deleting the legacy
[`suhaasgaddala/OrbitQueue`](https://github.com/suhaasgaddala/OrbitQueue)
repository. Completing the parity migration does not authorize deletion by
itself. Do not delete or rename either repository until every required manual
item below has an explicit decision.

## Verified Successor Migration State

- [x] The `v1-parity-migration` branch was merged into `main` at parity commit
  `88b87f08a4fe749a30077c599de22c1f6d54e5a4`.
- [x] GitHub CI passed on the parity branch in Debug and Release:
  [run 27945657111](https://github.com/suhaasgaddala/orbitqueue-v2/actions/runs/27945657111).
- [x] GitHub CI passed on merged `main` in Debug and Release:
  [run 27945692814](https://github.com/suhaasgaddala/orbitqueue-v2/actions/runs/27945692814).
- [x] The current README presents Bounded Concurrent Queues for C++20 as the
  maintained project and labels the original prototype as historical.
- [x] Historical v1 artifacts are preserved under `docs/legacy`, including the
  original license notice, both images, and the full factual v1 context audit.
- [x] The optional Boost.Lockfree benchmark is documented and
  `ORBITQUEUE_ENABLE_BOOST_BENCHMARKS` defaults to `OFF`.
- [x] Old unsafe APIs and algorithms are intentionally excluded, including raw
  callback writes, caller-managed ring indices, unchecked payload copies, and
  the unverified per-slot version protocol.
- [x] The complete feature disposition is recorded in
  [`v1_parity_audit.md`](v1_parity_audit.md).

## Required Manual Preservation Checks

- [ ] Back up the local v1 clone independently before deletion. A Git bundle is
  preferred because it preserves every ref and Git object:

  ```sh
  legacy_clone="<legacy-clone>"
  bundle_path="<backup-directory>/OrbitQueue-v1-all.bundle"
  git -C "$legacy_clone" bundle create "$bundle_path" --all
  git bundle verify "$bundle_path"
  ```

- [ ] Manually check GitHub issues, pull requests, releases, wiki pages, stars,
  forks, discussions, repository settings, deploy keys, secrets, webhooks, and
  external integrations for v1.

  Read-only snapshot on 2026-06-22: v1 was not archived; issues were disabled;
  wiki was enabled; discussions were disabled; zero stars, zero forks, no open
  issues, no listed pull requests, and no listed releases were observed. This
  snapshot does not prove that wiki content, settings, deleted/hidden records,
  or external integrations are absent.

- [ ] Inventory external links, badges, package references, bookmarks, and
  documentation that still point to `https://github.com/suhaasgaddala/OrbitQueue`.
- [ ] Decide whether v1 should be deleted or archived. Archiving first is the
  lower-risk option because it preserves URLs and history while making the
  repository read-only.
- [ ] Rename the successor GitHub repository to
  `bounded-concurrent-queues` after the identity branch is reviewed and merged.
- [ ] Decide whether the remote `v1-parity-migration` branch should remain as an
  audit trail after the merge.

## Final Replacement Verification

- [ ] From a clean directory outside the working checkout, clone the final
  GitHub URL and verify configure, build, and tests:

  ```sh
  git clone https://github.com/suhaasgaddala/bounded-concurrent-queues.git
  cmake -S bounded-concurrent-queues -B bounded-concurrent-queues/build \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build bounded-concurrent-queues/build --parallel
  ctest --test-dir bounded-concurrent-queues/build --output-on-failure
  ```

- [ ] Confirm the clean clone resolves the expected final commit and local
  safety tag policy.
- [ ] Confirm the latest `main` GitHub CI run is still passing after this
  checklist is committed and pushed.
- [ ] Record the chosen v1 disposition, repository-name decision, backup path,
  reviewer, and decision date below.

## Final Decision Record

| Field | Decision |
| --- | --- |
| V1 disposition: archive or delete | Pending |
| Successor repository rename | Pending: `bounded-concurrent-queues` |
| V1 backup location | Pending |
| GitHub/manual review completed by | Pending |
| Final clean-clone verification commit | Pending |
| Decision date | Pending |

Deletion remains blocked while any required manual item is unchecked.
