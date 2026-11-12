"use strict";

const assert = require("node:assert/strict");
const Database = require("better-sqlite3");

const { runMigrations } = require("../src/db");

const memory = new Database(":memory:");
runMigrations(memory);
require.cache[require.resolve("../src/db")] = { exports: { getDb: () => memory, initDb: () => memory } };

const config = require("../src/config");
const applications = require("../src/lib/applications");
const { handleButton } = require("../src/lib/discord");

const REVIEWER_ROLE = "reviewer-role";
config.discord.reviewerRoleId = REVIEWER_ROLE;
config.discord.roles.team = "team-role";

const newApplication = (displayName) =>
  applications.createApplication(
    {
      type: "team",
      source: "web",
      displayName,
      email: "applicant@example.com",
      discordId: "111111111111111111",
      discordHandle: "Applicant",
      motivation: "A motivation long enough to store.",
    },
    {}
  );

const reviewerInteraction = (customId, tag, addRole = async () => {}) => {
  const replies = [];
  return {
    replies,
    customId,
    user: { tag },
    guild: { members: { fetch: async () => ({ roles: { cache: new Set([REVIEWER_ROLE]), add: addRole } }) } },
    message: { edit: async () => {}, channel: { isThread: () => false } },
    reply: async (body) => replies.push(body.content),
    deferReply: async () => {},
    editReply: async (body) => replies.push(body),
  };
};

const statusOf = (application) => applications.getApplicationById(application.id).status;

const dueIds = () => applications.listUnpostedApplications().map((row) => row.id);

(async () => {
  {
    const application = newApplication("Concurrent Decision");
    let openRole;
    const roleGate = new Promise((resolve) => {
      openRole = resolve;
    });
    const approve = reviewerInteraction(`application:approve:${application.publicId}`, "Mod#1", () => roleGate);
    const reject = reviewerInteraction(`application:reject:${application.publicId}`, "Mod#2");

    const approving = handleButton(approve);
    await handleButton(reject);
    assert.equal(statusOf(application), "reviewing", "the first decision holds the application while its role is assigned");
    assert.deepEqual(reject.replies, ["Another reviewer is already deciding this application."], "the second reviewer is refused");

    openRole();
    await approving;
    assert.equal(statusOf(application), "accepted");
    assert.deepEqual(approve.replies, ["Application approved and role assigned."]);
  }

  {
    const application = newApplication("Role Failure");
    const failing = reviewerInteraction(`application:approve:${application.publicId}`, "Mod#1", async () => {
      throw new Error("Missing Permissions");
    });
    await handleButton(failing);
    assert.equal(statusOf(application), "pending", "a failed role assignment releases the claim");
    assert.deepEqual(failing.replies, ["Could not assign the role: Missing Permissions"]);

    const retry = reviewerInteraction(`application:approve:${application.publicId}`, "Mod#1");
    await handleButton(retry);
    assert.equal(statusOf(application), "accepted", "a released application can be decided again");
  }

  {
    const application = newApplication("Retry Later");
    assert.ok(dueIds().includes(application.id), "a new unposted application is due");

    applications.recordDiscordPostFailure(application.id);
    assert.ok(!dueIds().includes(application.id), "a failed post waits for its backoff");
    const waitMinutes = memory
      .prepare("SELECT (julianday(discord_post_next_attempt_at) - julianday('now')) * 1440 AS minutes FROM applications WHERE id = ?")
      .get(application.id).minutes;
    assert.ok(waitMinutes > 4 && waitMinutes <= 5.01, "the first retry waits five minutes");

    memory.prepare("UPDATE applications SET discord_post_next_attempt_at = datetime('now', '-1 minute') WHERE id = ?").run(application.id);
    assert.ok(dueIds().includes(application.id), "the post is retried once the backoff has passed");

    applications.setDiscordMessageId(application.id, "thread-1");
    assert.ok(!dueIds().includes(application.id), "a posted application is not retried");
  }

  {
    const application = newApplication("Attempt Cap");
    for (let attempt = 0; attempt < 12; attempt += 1) {
      applications.recordDiscordPostFailure(application.id);
    }
    memory.prepare("UPDATE applications SET discord_post_next_attempt_at = NULL WHERE id = ?").run(application.id);
    assert.ok(!dueIds().includes(application.id), "the attempt cap stops retries");

    applications.resetDiscordPostAttempts();
    assert.ok(dueIds().includes(application.id), "a capped application is posted again after the bot logs in");
  }

  console.log("application discord tests passed");
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
