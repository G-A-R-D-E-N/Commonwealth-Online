export const buildDiscordPayload = (application) => ({
  content: `New ${application.type} application: ${application.display_name} (${application.public_id})`,
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
