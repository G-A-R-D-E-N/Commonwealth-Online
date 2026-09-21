(() => {
  const server = "1512018618680999976";
  const channel = "1512018620060794982";
  const frameId = "co-discord-chat-frame";

  const button = document.createElement("button");
  button.type = "button";
  button.className = "co-discord-chat-toggle";
  button.setAttribute("aria-controls", "co-discord-chat-panel");
  button.setAttribute("aria-expanded", "false");
  button.textContent = "Discord Chat";

  const panel = document.createElement("section");
  panel.id = "co-discord-chat-panel";
  panel.className = "co-discord-chat-panel";
  panel.setAttribute("aria-label", "Commonwealth Online Discord chat");
  panel.hidden = true;

  const header = document.createElement("div");
  header.className = "co-discord-chat-header";

  const title = document.createElement("strong");
  title.textContent = "Commonwealth Online";

  const close = document.createElement("button");
  close.type = "button";
  close.className = "co-discord-chat-close";
  close.setAttribute("aria-label", "Close Discord chat");
  close.textContent = "Close";

  const frame = document.createElement("iframe");
  frame.id = frameId;
  frame.className = "co-discord-chat-frame";
  frame.title = "Commonwealth Online Discord";
  frame.loading = "lazy";
  frame.allow = "clipboard-write; fullscreen";
  frame.referrerPolicy = "no-referrer";

  header.append(title, close);
  panel.append(header, frame);
  document.body.append(button, panel);

  const setOpen = (open) => {
    if (open && !frame.src) {
      frame.src = `https://e.widgetbot.io/channels/${server}/${channel}`;
    }

    panel.hidden = !open;
    button.setAttribute("aria-expanded", String(open));
    button.textContent = open ? "Hide Discord" : "Discord Chat";
  };

  button.addEventListener("click", () => {
    setOpen(panel.hidden);
  });

  close.addEventListener("click", () => {
    setOpen(false);
    button.focus();
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && !panel.hidden) {
      setOpen(false);
      button.focus();
    }
  });
})();
