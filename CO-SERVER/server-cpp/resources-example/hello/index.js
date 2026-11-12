CO.commands.register("hello", (args, playerId) => {
	const self = CO.players.get(playerId);
	CO.chat.send(playerId, `Hello ${self ? self.name : "stranger"}, ${CO.players.list().length} online`);
});

CO.commands.register("time", (args, playerId) => {
	CO.chat.send(playerId, `Server time ${CO.world.get().timeHHmm ?? "unset"}`);
});
