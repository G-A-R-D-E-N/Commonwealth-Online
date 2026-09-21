(() => {
  const link = document.createElement("a");
  link.className = "co-discord-chat-toggle";
  link.href = "https://discord.gg/GyfxYG2gzH";
  link.target = "_blank";
  link.rel = "noopener noreferrer";
  link.setAttribute("aria-label", "Open Commonwealth Online Discord");
  link.textContent = "Discord";
  document.body.append(link);
})();
