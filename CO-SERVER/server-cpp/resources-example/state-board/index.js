const PIN_KEY = "pins";
const MAXIMUM_PINS = 8;

CO.commands.register("board", (args, playerId) => {
	const opened = CO.views.open(playerId, "board.html");
	if (!opened.ok) {
		CO.chat.send(playerId, `No board: ${opened.error}`);
	}
});

CO.views.onMessage(({ playerId, data }) => {
	if (!data || typeof data.action !== "string") {
		return;
	}
	if (data.action === "close") {
		CO.views.close(playerId);
		return;
	}
	if (data.action !== "pin") {
		return;
	}
	const pins = CO.state.get("global")[PIN_KEY];
	const pin = { playerId, text: String(data.text ?? "").slice(0, 80), at: Date.now() };
	const written = CO.state.set("global", PIN_KEY, [pin].concat(Array.isArray(pins) ? pins : []).slice(0, MAXIMUM_PINS));
	if (!written.ok) {
		CO.chat.send(playerId, `The board refused the pin: ${written.error}`);
	}
});
