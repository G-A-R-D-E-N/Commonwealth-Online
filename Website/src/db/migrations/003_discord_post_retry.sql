-- Delivery state for posting an application to the Discord review forum.
-- Rows without discord_message_id are retried until discord_post_attempts reaches the cap.

ALTER TABLE applications ADD COLUMN discord_post_attempts INTEGER NOT NULL DEFAULT 0;
ALTER TABLE applications ADD COLUMN discord_post_next_attempt_at TEXT;
