const cleanThreadPart = (value, fallback) => {
  const cleaned = String(value ?? "").replace(/\s+/g, " ").replace(/@/g, "＠").trim();
  return cleaned || fallback;
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
  content: `New ${application.type} application: ${application.display_name} (${application.public_id})`,
  thread_name: buildThreadName(application),
  allowed_mentions: { parse: [] },
});

export const notifyDiscord = async (application, webhookUrl, fetchImpl = fetch) => {
  if (!webhookUrl) {
    return;
  }

  const response = await fetchImpl(webhookUrl, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(buildDiscordPayload(application)),
  });
  if (!response.ok) {
    throw new Error(`Discord notification returned ${response.status}`);
  }
};

export const scheduleDiscordNotification = (
  application,
  webhookUrl,
  waitUntil,
  fetchImpl = fetch,
  onError = console.error,
) => {
  const task = notifyDiscord(application, webhookUrl, fetchImpl).catch(onError);
  if (waitUntil) {
    waitUntil(task);
  } else {
    void task;
  }
};
