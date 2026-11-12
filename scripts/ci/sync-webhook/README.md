# Private CI sync webhook

`scripts/ci/sync-webhook/worker.mjs` receives signed events from the private repository and requests the existing public CI sync. Private GitHub Actions remain disabled; the public sync still controls export, secret scanning, and CI status reporting.

1. Create a fine-grained GitHub token restricted to `G-A-R-D-E-N/Commonwealth-Online` with Actions read and write permission. Store it as the Worker secret `PUBLIC_CI_DISPATCH_TOKEN`.
2. Generate a separate high-entropy webhook secret. Store it as the Worker secret `WEBHOOK_SECRET` and configure the same value on a GitHub webhook for `G-A-R-D-E-N/Commonwealth-Online-Private`.
3. Deploy this directory as a Cloudflare Worker and point the webhook to its HTTPS URL ending in `/github`. Use JSON delivery and subscribe to push and pull request events.
4. Check the webhook's ping delivery, then update a test PR and confirm a `public-ci-sync.yml` workflow dispatch on the public repository.

The Worker returns an error if dispatch fails. GitHub does not automatically redeliver failed webhook deliveries; inspect and redeliver failures in the repository's webhook settings. The existing scheduled sync remains a fallback. A successful dispatch queues the public workflow but does not bypass GitHub's runner queue.
