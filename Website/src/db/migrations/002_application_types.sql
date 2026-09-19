-- Split applications into team-join and beta-tester submissions.
-- Type-specific answers (role, game edition, agreements, ...) are stored as JSON
-- in `answers` so the Discord bot can read the full form without schema churn.

ALTER TABLE applications ADD COLUMN type TEXT NOT NULL DEFAULT 'team';
ALTER TABLE applications ADD COLUMN answers TEXT;

CREATE INDEX IF NOT EXISTS idx_applications_type
  ON applications (type, status, created_at DESC);
