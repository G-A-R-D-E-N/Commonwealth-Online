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
  getApplicationByPublicId,
  listApplications,
  setApplicationStatus,
  setDiscordMessageId,
} = require("./applications");
const { sendMoreInfoRequest } = require("./mailcow");

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
      value: truncate(field.displayValue, 1024),
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

const isGuildMemberByUsername = async (username) => {
  if (!membershipConfigured() || !client?.isReady()) {
    return { ok: false, unavailable: true };
  }
  const handle = String(username || "").trim().toLowerCase();
  if (handle.length < 2) {
    return { ok: true, member: false };
  }
  try {
    const guild = await client.guilds.fetch(config.discord.guildId);
    const members = await guild.members.search({ query: handle, limit: 100 });
    return { ok: true, member: members.some((member) => member.user.username.toLowerCase() === handle) };
  } catch (error) {
    return { ok: false, unavailable: true, error: error.message };
  }
};

const postApplication = async (application) => {
  if (!client?.isReady() || application.discordMessageId) {
    return { ok: false, skipped: true };
  }

  try {
    const channel = await client.channels.fetch(forumChannelId(application));
    if (!channel || channel.type !== ChannelType.GuildForum) {
      return { ok: false, error: "The configured application channel is not a forum channel." };
    }
    const thread = await channel.threads.create({
      name: applicationTitle(application),
      appliedTags: forumTagId(channel, application.status) ? [forumTagId(channel, application.status)] : [],
      message: { embeds: [applicationEmbed(application)], components: [reviewButtons(application)] },
    });
    setDiscordMessageId(application.id, thread.id);
    return { ok: true, messageId: thread.id };
  } catch (error) {
    return { ok: false, error: error.message };
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
  if (action === "approve") {
    if (application.discordId) {
      const roleId = config.discord.roles[application.type];
      if (!roleId) {
        return interaction.editReply(`No accepted ${application.type} role is configured.`);
      }
      try {
        const member = await interaction.guild.members.fetch(application.discordId);
        await member.roles.add(roleId, `Accepted application ${application.publicId}`);
      } catch (error) {
        return interaction.editReply(`Could not assign the role: ${error.message}`);
      }
    }
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
  const sent = await sendMoreInfoRequest(application, interaction.fields.getTextInputValue("requirements").trim());
  if (!sent.ok) {
    return interaction.editReply(sent.skipped ? "Mailcow is not configured." : `Could not send email: ${sent.error}`);
  }
  const updated = setApplicationStatus(application.id, "more_info_requested", interaction.user.tag);
  if (interaction.message) {
    await updateReviewMessage(interaction.message, updated);
  }
  return interaction.editReply("More-information email sent.");
};

const syncPendingApplications = async () => {
  for (let offset = 0; ; ) {
    const pending = listApplications({ status: "pending", limit: 200, offset });
    if (!pending.ok || !pending.applications.length) {
      return;
    }
    for (const application of pending.applications) {
      const result = await postApplication(application);
      if (!result.ok && !result.skipped) {
        console.error(`[discord] Application post failed: ${result.error}`);
      }
    }
    offset += pending.applications.length;
    if (pending.applications.length < pending.limit) {
      return;
    }
  }
};

const startDiscordBot = async () => {
  if (!(discordConfigured() || membershipConfigured()) || client) {
    return;
  }
  client = new Client({ intents: [GatewayIntentBits.Guilds, GatewayIntentBits.GuildMembers] });
  client.once(Events.ClientReady, () => {
    console.log(`[discord] Logged in as ${client.user.tag}`);
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
  } catch (error) {
    console.error(`[discord] Login failed: ${error.message}`);
    client.destroy();
    client = undefined;
  }
};

const stopDiscordBot = () => {
  client?.destroy();
  client = undefined;
};

module.exports = {
  applicationEmbed,
  applicationTitle,
  forumTagName,
  discordConfigured,
  membershipConfigured,
  isGuildMemberByUsername,
  postApplication,
  syncApplicationPost,
  startDiscordBot,
  stopDiscordBot,
};
