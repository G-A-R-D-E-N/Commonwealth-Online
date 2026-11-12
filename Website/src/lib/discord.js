"use strict";

const {
  ActionRowBuilder,
  ButtonBuilder,
  ButtonStyle,
  ChannelType,
  Client,
  EmbedBuilder,
  Events,
  GatewayIntentBits,
  ModalBuilder,
  TextInputBuilder,
  TextInputStyle,
} = require("discord.js");

const config = require("../config");
const {
  claimApplication,
  getApplicationByPublicId,
  listUnpostedApplications,
  recordDiscordPostFailure,
  releaseApplication,
  resetDiscordPostAttempts,
  setApplicationStatus,
  setDiscordMessageId,
} = require("./applications");
const { sendMoreInfoRequest } = require("./mailcow");

const RETRY_INTERVAL_MS = 5 * 60_000;
const RETRY_BATCH = 50;
const EMBED_FIELD_MAX = 1024;
const ANSWER_CHUNK_CHARACTERS = 900;

const discordConfigured = () =>
  Boolean(
    config.discord.botToken &&
      Object.values(config.discord.applicationsForumChannelIds).some(Boolean)
  );

const membershipConfigured = () => Boolean(config.discord.botToken && config.discord.guildId);

const forumChannelId = (application) =>
  config.discord.applicationsForumChannelIds[application.type] ||
  config.discord.applicationsForumChannelIds.default;

const truncate = (value, max) => {
  const text = String(value ?? "");
  return text.length > max ? `${text.slice(0, max - 1)}…` : text;
};

const forumTagName = (status) => (status === "accepted" || status === "rejected" ? status : "pending");

const titleStatus = (status) =>
  String(status || "pending")
    .replace(/_/g, " ")
    .replace(/\b\w/g, (letter) => letter.toUpperCase());

const applicationTitle = (application) => {
  const prefix = `${titleStatus(application.status)} - `;
  const suffix = ` - ${application.publicId}`;
  return `${prefix}${truncate(application.displayName, 100 - prefix.length - suffix.length)}${suffix}`;
};

const forumTagId = (channel, status) =>
  channel?.availableTags?.find((tag) => tag.name.toLowerCase() === forumTagName(status))?.id;

const updateApplicationThread = async (thread, application) => {
  const tagId = forumTagId(thread.parent, application.status);
  await Promise.all([
    thread.setName(applicationTitle(application)),
    thread.setAppliedTags(tagId ? [tagId] : []),
  ]);
};

const isLongAnswer = (field) => String(field.displayValue).length > EMBED_FIELD_MAX;

const answerMessages = (application) =>
  (application.fields || [])
    .filter((field) => field.displayValue && isLongAnswer(field))
    .flatMap((field) => {
      const chunks = String(field.displayValue).match(new RegExp(`[\\s\\S]{1,${ANSWER_CHUNK_CHARACTERS}}`, "gu"));
      return chunks.map((chunk, index) => `**${field.label}** (${index + 1}/${chunks.length})\n${chunk}`);
    });

const applicationEmbed = (application) => {
  const fields = [
    { name: "Reference", value: application.publicId, inline: true },
    { name: "Type", value: application.typeLabel, inline: true },
    { name: "Status", value: application.status, inline: true },
  ];

  for (const field of application.fields || []) {
    if (field.key === "displayName" || !field.displayValue) {
      continue;
    }
    fields.push({
      name: truncate(field.label, 256),
      value: isLongAnswer(field) ? "The full answer is in the thread below." : String(field.displayValue),
      inline: String(field.displayValue).length < 48,
    });
  }

  return new EmbedBuilder()
    .setColor(application.status === "accepted" ? 0x57f287 : application.status === "rejected" ? 0xed4245 : 0xd9a441)
    .setTitle(`${application.typeLabel} — ${truncate(application.displayName, 70)}`)
    .setTimestamp(new Date(application.createdAt))
    .setFields(fields.slice(0, 25));
};

const reviewButtons = (application) =>
  new ActionRowBuilder().addComponents(
    new ButtonBuilder()
      .setCustomId(`application:approve:${application.publicId}`)
      .setLabel("Approve")
      .setStyle(ButtonStyle.Success),
    new ButtonBuilder()
      .setCustomId(`application:reject:${application.publicId}`)
      .setLabel("Reject")
      .setStyle(ButtonStyle.Danger),
    new ButtonBuilder()
      .setCustomId(`application:more-info:${application.publicId}`)
      .setLabel("More Information")
      .setStyle(ButtonStyle.Primary)
  );

let client;

const findGuildMemberByUsername = async (username) => {
  if (!membershipConfigured() || !client?.isReady()) {
    return { ok: false, unavailable: true };
  }
  const handle = String(username || "").trim().toLowerCase();
  if (handle.length < 2) {
    return { ok: true, member: null };
  }
  try {
    const guild = await client.guilds.fetch(config.discord.guildId);
    const members = await guild.members.search({ query: handle, limit: 100 });
    const found = members.find((member) => member.user.username.toLowerCase() === handle);
    return {
      ok: true,
      member: found ? { id: found.user.id, username: found.user.username, send: (content) => found.send(content) } : null,
    };
  } catch (error) {
    return { ok: false, unavailable: true, error: error.message };
  }
};

// Discord API error 50007: the user does not accept DMs from this bot.
const DISCORD_CANNOT_DM = 50007;

const sendVerificationCode = async (member, code, minutes) => {
  try {
    await member.send(
      `Your Commonwealth Online application code is **${code}**. It expires in ${minutes} minutes.\n` +
        "If you did not start an application on the Commonwealth Online website, ignore this message."
    );
    return { ok: true };
  } catch (error) {
    return { ok: false, blocked: error?.code === DISCORD_CANNOT_DM, error: error?.message };
  }
};

const postingIds = new Set();

const postAnswerMessages = async (thread, application) => {
  for (const content of answerMessages(application)) {
    await thread.send({ content, allowedMentions: { parse: [] } });
  }
};

const postApplication = async (application) => {
  if (!client?.isReady() || application.discordMessageId || postingIds.has(application.id)) {
    return { ok: false, skipped: true };
  }

  postingIds.add(application.id);
  try {
    const channel = await client.channels.fetch(forumChannelId(application));
    if (!channel || channel.type !== ChannelType.GuildForum) {
      recordDiscordPostFailure(application.id);
      return { ok: false, error: "The configured application channel is not a forum channel." };
    }
    const thread = await channel.threads.create({
      name: applicationTitle(application),
      appliedTags: forumTagId(channel, application.status) ? [forumTagId(channel, application.status)] : [],
      message: { embeds: [applicationEmbed(application)], components: [reviewButtons(application)] },
    });
    setDiscordMessageId(application.id, thread.id);
    await postAnswerMessages(thread, application);
    return { ok: true, messageId: thread.id };
  } catch (error) {
    recordDiscordPostFailure(application.id);
    return { ok: false, error: error.message };
  } finally {
    postingIds.delete(application.id);
  }
};

const updateReviewMessage = async (message, application, complete = false) => {
  await message.edit({
    embeds: [applicationEmbed(application)],
    components: complete ? [] : [reviewButtons(application)],
  });
  if (message.channel?.isThread?.()) {
    await updateApplicationThread(message.channel, application);
  }
};

const syncApplicationPost = async (application) => {
  if (!client?.isReady() || !application.discordMessageId) {
    return { ok: false, skipped: true };
  }

  try {
    const thread = await client.channels.fetch(application.discordMessageId);
    if (!thread?.isThread?.()) {
      return { ok: false, error: "The application post is not a forum thread." };
    }
    await updateReviewMessage(await thread.fetchStarterMessage(), application, ["accepted", "rejected"].includes(application.status));
    return { ok: true };
  } catch (error) {
    return { ok: false, error: error.message };
  }
};

const reviewer = async (interaction) => {
  if (!config.discord.reviewerRoleId || !interaction.guild) {
    return false;
  }
  try {
    const member = await interaction.guild.members.fetch(interaction.user.id);
    return member.roles.cache.has(config.discord.reviewerRoleId);
  } catch {
    return false;
  }
};

const applicationId = (customId, action) => {
  const prefix = `application:${action}:`;
  return customId.startsWith(prefix) ? customId.slice(prefix.length) : null;
};

const activeApplication = (publicId) => {
  const application = getApplicationByPublicId(publicId);
  return application && !["accepted", "rejected"].includes(application.status) ? application : null;
};

const assignApplicantRole = async (interaction, application) => {
  if (!application.discordId) {
    return null;
  }
  const roleId = config.discord.roles[application.type];
  if (!roleId) {
    return `No accepted ${application.type} role is configured.`;
  }
  try {
    const member = await interaction.guild.members.fetch(application.discordId);
    await member.roles.add(roleId, `Accepted application ${application.publicId}`);
    return null;
  } catch (error) {
    return `Could not assign the role: ${error.message}`;
  }
};

const handleButton = async (interaction) => {
  const [, action, publicId] = interaction.customId.split(":");
  if (!publicId || !["approve", "reject", "more-info"].includes(action)) {
    return;
  }
  if (!(await reviewer(interaction))) {
    return interaction.reply({ content: "You do not have the application reviewer role.", ephemeral: true });
  }
  const application = activeApplication(publicId);
  if (!application) {
    return interaction.reply({ content: "This application has already been closed.", ephemeral: true });
  }
  if (action === "more-info") {
    return interaction.showModal(
      new ModalBuilder()
        .setCustomId(`application:more-info:${publicId}`)
        .setTitle("Request more information")
        .addComponents(
          new ActionRowBuilder().addComponents(
            new TextInputBuilder()
              .setCustomId("requirements")
              .setLabel("What information is required?")
              .setStyle(TextInputStyle.Paragraph)
              .setRequired(true)
              .setMaxLength(2000)
          )
        )
    );
  }

  await interaction.deferReply({ ephemeral: true });
  if (!claimApplication(application, interaction.user.tag)) {
    return interaction.editReply("Another reviewer is already deciding this application.");
  }
  const roleFailure = action === "approve" ? await assignApplicantRole(interaction, application) : null;
  if (roleFailure) {
    releaseApplication(application);
    return interaction.editReply(roleFailure);
  }

  const updated = setApplicationStatus(application.id, action === "approve" ? "accepted" : "rejected", interaction.user.tag);
  await updateReviewMessage(interaction.message, updated, true);
  return interaction.editReply(
    action === "approve"
      ? application.discordId
        ? "Application approved and role assigned."
        : `Application approved. Assign the role manually to ${application.discordHandle}.`
      : "Application rejected."
  );
};

const handleMoreInfo = async (interaction) => {
  const publicId = applicationId(interaction.customId, "more-info");
  if (!publicId) {
    return;
  }
  if (!(await reviewer(interaction))) {
    return interaction.reply({ content: "You do not have the application reviewer role.", ephemeral: true });
  }
  const application = activeApplication(publicId);
  if (!application) {
    return interaction.reply({ content: "This application has already been closed.", ephemeral: true });
  }
  await interaction.deferReply({ ephemeral: true });
  if (!claimApplication(application, interaction.user.tag)) {
    return interaction.editReply("Another reviewer is already deciding this application.");
  }
  const sent = await sendMoreInfoRequest(application, interaction.fields.getTextInputValue("requirements").trim());
  if (!sent.ok) {
    releaseApplication(application);
    return interaction.editReply(sent.skipped ? "Mailcow is not configured." : `Could not send email: ${sent.error}`);
  }
  const updated = setApplicationStatus(application.id, "more_info_requested", interaction.user.tag);
  if (interaction.message) {
    await updateReviewMessage(interaction.message, updated);
  }
  return interaction.editReply("More-information email sent.");
};

const syncPendingApplications = async () => {
  for (const application of listUnpostedApplications(RETRY_BATCH)) {
    const result = await postApplication(application);
    if (!result.ok && !result.skipped) {
      console.error(`[discord] Application post failed: ${result.error}`);
    }
  }
};

let retryTimer;

const startDiscordBot = async () => {
  if (!(discordConfigured() || membershipConfigured()) || client) {
    return;
  }
  client = new Client({ intents: [GatewayIntentBits.Guilds, GatewayIntentBits.GuildMembers] });
  client.once(Events.ClientReady, () => {
    console.log(`[discord] Logged in as ${client.user.tag}`);
    resetDiscordPostAttempts();
    void syncPendingApplications();
  });
  client.on(Events.InteractionCreate, (interaction) => {
    if (interaction.isButton()) {
      void handleButton(interaction).catch((error) => console.error(`[discord] Review failed: ${error.message}`));
    } else if (interaction.isModalSubmit()) {
      void handleMoreInfo(interaction).catch((error) => console.error(`[discord] Email request failed: ${error.message}`));
    }
  });
  try {
    await client.login(config.discord.botToken);
    retryTimer = setInterval(() => {
      void syncPendingApplications().catch((error) => console.error(`[discord] Retry failed: ${error.message}`));
    }, RETRY_INTERVAL_MS);
  } catch (error) {
    console.error(`[discord] Login failed: ${error.message}`);
    client.destroy();
    client = undefined;
  }
};

const stopDiscordBot = () => {
  clearInterval(retryTimer);
  retryTimer = undefined;
  client?.destroy();
  client = undefined;
};

module.exports = {
  answerMessages,
  applicationEmbed,
  applicationTitle,
  forumTagName,
  discordConfigured,
  membershipConfigured,
  findGuildMemberByUsername,
  sendVerificationCode,
  postApplication,
  syncApplicationPost,
  handleButton,
  startDiscordBot,
  stopDiscordBot,
};
