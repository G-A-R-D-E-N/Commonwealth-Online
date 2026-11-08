import { answerMessages, buildDiscordPayload, postDiscordMessage } from "./discord.mjs";

export const LEASE_SECONDS = 300;
export const BATCH_SIZE = 5;

const recordFailure = async (admin, application, onError) => {
  const { error } = await admin.rpc("record_discord_post_failure", { p_public_id: application.public_id });
  if (error) {
    onError(error);
  }
};

export const deliverApplication = async (admin, application, webhookUrl, onError, fetchImpl = fetch) => {
  let threadId;
  try {
    const message = await postDiscordMessage(webhookUrl, buildDiscordPayload(application), "", fetchImpl);
    threadId = String(message?.channel_id || "").trim();
    if (!threadId) {
      throw new Error("Discord did not return a thread id");
    }
    const { error } = await admin
      .from("applications")
      .update({ discord_thread_id: threadId, discord_post_next_attempt_at: null })
      .eq("public_id", application.public_id);
    if (error) {
      throw error;
    }
  } catch (error) {
    onError(error);
    await recordFailure(admin, application, onError);
    return false;
  }

  try {
    for (const content of answerMessages(application)) {
      await postDiscordMessage(webhookUrl, { content, allowed_mentions: { parse: [] } }, threadId, fetchImpl);
    }
  } catch (error) {
    onError(error);
  }
  return true;
};

export const deliverDue = async (admin, webhookUrl, onError, options = {}, fetchImpl = fetch) => {
  if (!webhookUrl) {
    return 0;
  }
  const { data, error } = await admin.rpc("claim_discord_posts", {
    p_public_id: options.publicId ?? null,
    p_limit: options.limit ?? BATCH_SIZE,
    p_lease_seconds: LEASE_SECONDS,
  });
  if (error) {
    onError(error);
    return 0;
  }
  const results = await Promise.all(
    (data || []).map((application) => deliverApplication(admin, application, webhookUrl, onError, fetchImpl)),
  );
  return results.filter(Boolean).length;
};
