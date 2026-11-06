# Authenticated approvals: implementation contract, not an implemented service

The local `decide --by` command is deliberately a trusted-operator mechanism.
Changing its label to a username or adding an unsigned OS UID would not make
checkpoint records tamper-proof or prove a human approved them. This proposal
defines a separate service boundary rather than relabeling the existing feature.

## Trust and deployment

Run the executor and approval service under different OS/service accounts.
Operators authenticate to the approval service using an existing OIDC provider.
The executor must not hold signing keys, identity-provider admin credentials,
operator session cookies, or write access to the approval database/policy.
Operators must not have write access to executor binaries, policies or running
job checkpoints. Host administrators remain trusted; this is not protection
against a malicious administrator or compromised executor host.

The service is the authoritative decision store. Local `--by` labels are only
display text and cannot grant service permissions. The authorization policy maps
verified issuer/subject pairs or approved groups to project/workflow scopes;
default deny. Self-approval may be prohibited where separation of duties is needed.

## Protocol

1. Executor registers a request over authenticated TLS with a unique run ID,
   request ID, workflow/source identity digest, exact UTF-8 candidate bytes/digest,
   requested scope and expiration. Registration is idempotent for identical bytes;
   conflicting reuse is rejected. Candidate bytes are immutable after registration.
2. Operator logs in through OIDC authorization-code flow with PKCE, using an
   established maintained implementation. Validate issuer, audience, nonce, state
   and redirect URI. No tokens in browser local storage or URLs. Cookies require
   Secure/HttpOnly/SameSite and mutation endpoints require CSRF protection.
3. An authorized operator sees the exact candidate and source/workflow context.
   Approval/rejection requires an explicit action and reason. Content is displayed
   as inert text, with limits; embedded links/instructions cannot submit decisions.
4. A database transaction changes `pending` to one terminal decision using a
   compare-and-set revision. Stale, expired, mismatched-scope, duplicate/conflicting
   decisions fail. Record immutable issuer/subject, policy version, time and reason.
5. Executor retrieves a signed decision envelope. Use an established JOSE/crypto
   library; do not invent canonicalization or implement signature primitives.
   Pin trusted issuer/public keys and allowed algorithms. Bind audience, run ID,
   request ID, workflow digest, candidate digest, scope, decision, issue/expiry times
   and unique decision ID. Unknown keys/algorithms and unavailable verification fail closed.
6. Persist the verified envelope and consumed decision ID atomically with gate
   progress. Resume revalidates the envelope and binding. A decision for another
   run/output/scope must never release a gate. Retrieval outage leaves the workflow
   paused; it must never fall back to local unsigned `decide` for a service-backed gate.

The service needs a specified key-rotation/revocation policy. Decide whether
revocation affects already-consumed decisions; record that policy explicitly.
Approval expiration is checked before release, not retroactively after a completed
operation. Exactly-once external side effects still require target idempotency.

## Required interfaces and tests

- `POST /requests`: executor authentication, payload size bounds, immutable binding.
- `GET /requests/{id}`: authorized reviewer/executor scope checks, no cross-project reads.
- `POST /requests/{id}/decision`: operator authorization, CSRF, revision, reason and expiry.
- `GET /requests/{id}/receipt`: scoped executor access, signed terminal envelope.
- Audit export: access controlled, secrets excluded, retention/deletion policy defined.

Acceptance tests must cover wrong issuer/audience/key/algorithm, altered bytes,
expired and cross-run receipts, replay, parallel decisions, group removal,
unauthorized scopes, CSRF/session fixation, logout, clock skew, key rotation,
service outages, crash between receipt verification and checkpoint write, and
resumption after executor restart. A second reviewer should examine the security
boundary before any external exposure.

## Information required before deployment

Select an identity-provider tenant, its existing operator groups, service host and
TLS/domain, storage/backup location, key management, reviewer policy and audit
retention. No such tenant or hosting boundary has been configured for Vora here.
Creating a network service without these choices would not deliver authenticated
production approval. The local-only development restriction remains active.
