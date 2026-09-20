const cleanThreadPart = (value, fallback) => {
  const cleaned = String(value ?? "").replace(/\s+/g, " ").replace(/@/g, "＠").trim();
  return cleaned || fallback;
};

const truncate = (value, max) => {
  const text = String(value ?? "");
  return text.length > max ? `${text.slice(0, max - 1)}…` : text;
};

const buildThreadName = (application) => {
  const status = cleanThreadPart(application.status, "pending");
  const displayName = cleanThreadPart(application.display_name, "Application");
  const publicId = cleanThreadPart(application.public_id, "unknown");
  const prefix = `${status.charAt(0).toUpperCase()}${status.slice(1)} - `;
  const suffix = ` - ${publicId}`;
  const maxDisplayLength = Math.max(1, 100 - prefix.length - suffix.length);
  return `${prefix}${displayName.slice(0, maxDisplayLength)}${suffix}`;
};

export const buildDiscordPayload = (application) => ({
  content: "New application received.",
  thread_name: buildThreadName(application),
  embeds: [{
    title: `${application.type} application — ${truncate(application.display_name, 70)}`,
    color: 0xd9a441,
    timestamp: application.created_at || new Date().toISOString(),
    fields: [
      { name: "Reference", value: truncate(application.public_id, 1024), inline: true },
      { name: "Type", value: truncate(application.type, 1024), inline: true },
      { name: "Status", value: truncate(application.status, 1024), inline: true },
      { name: "Email", value: truncate(application.email, 1024), inline: true },
      { name: "Discord username", value: truncate(application.discord_handle, 1024), inline: true },
      { name: "Timezone", value: truncate(application.timezone || "Not provided", 1024), inline: true },
      { name: "Availability", value: truncate(application.availability || "Not provided", 1024), inline: false },
      { name: "Experience", value: truncate(application.experience, 1024), inline: false },
      { name: "Motivation", value: truncate(application.motivation, 1024), inline: false },
      ...Object.entries(application.answers || {}).map(([key, value]) => ({
        name: key,
        value: truncate(typeof value === "boolean" ? (value ? "Yes" : "No") : value, 1024),
        inline: true,
      })),
    ].slice(0, 25),
  }],
  allowed_mentions: { parse: [] },
});

export const buildReviewUpdatePayload = (application) => {
  const status = cleanThreadPart(application.status, "updated");
  const publicId = cleanThreadPart(application.public_id, "unknown");
  const reviewNote = truncate(String(application.review_note ?? "").trim(), 1800);
  return {
    content: status === "more_info_requested"
      ? `More information requested for application ${publicId}:\n\n${reviewNote}`
      : `Application ${publicId} is now ${status}.`,
    allowed_mentions: { parse: [] },
  };
};

export const postDiscordMessage = async (webhookUrl, payload, threadId = "", fetchImpl = fetch) => {
  if (!webhookUrl) {
    return null;
  }

  const url = new URL(webhookUrl);
  url.searchParams.set("wait", "true");
  if (threadId) {
    url.searchParams.set("thread_id", threadId);
  }
  const response = await fetchImpl(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    throw new Error(`Discord notification returned ${response.status}`);
  }
  if (typeof response.json !== "function") {
    return null;
  }
  return response.json().catch(() => null);
};

export const notifyDiscord = async (application, webhookUrl, fetchImpl = fetch) =>
  postDiscordMessage(webhookUrl, buildDiscordPayload(application), "", fetchImpl);

export const scheduleDiscordNotification = (
  application,
  webhookUrl,
  waitUntil,
  fetchImpl = fetch,
  onError = console.error,
  onSuccess = () => {},
) => {
  const task = notifyDiscord(application, webhookUrl, fetchImpl).then(onSuccess).catch(onError);
  if (waitUntil) {
    waitUntil(task);
  } else {
    void task;
  }
};
