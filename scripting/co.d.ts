interface CoPosition {
	x: number;
	y: number;
	z: number;
	cellId: string;
	worldspaceId: string;
}

interface CoPlayer {
	connectionId: number;
	id: number;
	name: string;
	profileId: string;
	factionId: string | null;
	ready: boolean;
	position?: CoPosition;
}

interface CoResult {
	ok: boolean;
	error?: string;
}

type CoEventName =
	| 'questReport'
	| 'chat'
	| 'inventoryChange'
	| 'factionAction'
	| 'settlementAction'
	| 'playerReady'
	| 'playerDeath'
	| 'playerJoined'
	| 'playerLeft'
	| 'stateBagChange';

interface CoEvent {
	name: CoEventName;
	playerId: number;
	profileId: string;
	data: Record<string, unknown>;
}

type CoEventHandler = (event: CoEvent) => boolean | void;

interface CoStateBagChange {
	op: 'set' | 'delete';
	bag: string;
	/* constraint: key is delivered without the "<resource>." prefix */
	key: string;
	value?: unknown;
}

interface CoStateChangeEvent {
	name: 'stateBagChange';
	/* constraint: bag owner for a "player:<id>" bag, 0 for the global bag */
	playerId: number;
	profileId: string;
	data: CoStateBagChange;
}

interface CoViewMessage {
	playerId: number;
	data: Record<string, unknown>;
}

interface CoWorld {
	timeHHmm?: string;
	weatherConsoleArg?: string;
	weatherFormId?: string;
	mode: "coop-story" | "free-roam";
	[key: string]: unknown;
}

interface CoCommandOptions {
	adminOnly?: boolean;
}

type CoCommandHandler = (args: string[], playerId: number) => void;

declare const console: {
	log(...args: unknown[]): void;
	info(...args: unknown[]): void;
	warn(...args: unknown[]): void;
	error(...args: unknown[]): void;
	debug(...args: unknown[]): void;
};

declare function setTimeout(handler: () => void, milliseconds: number): number;
declare function setInterval(handler: () => void, milliseconds: number): number;
declare function clearTimeout(id: number): void;
declare function clearInterval(id: number): void;

declare const CO: {
	readonly players?: {
		list(): CoPlayer[];
		get(playerId: number): CoPlayer | null;
		revive(playerId: number): CoResult;
		kill(playerId: number): CoResult;
		message(playerId: number, text: string): CoResult;
	};
	readonly chat?: {
		send(playerId: number, text: string): boolean;
		broadcast(text: string): void;
	};
	readonly commands?: {
		register(name: string, handler: CoCommandHandler, options?: CoCommandOptions): void;
	};
	readonly world?: {
		get(): CoWorld;
		policy(): Record<string, unknown>;
		set(policy: Record<string, unknown>): CoResult;
	};
	readonly events?: {
		on(name: CoEventName, handler: CoEventHandler): number;
		off(token: number): boolean;
	};
	readonly state?: {
		/* constraint: keys are stored as <resource>.<key>, so get/set/delete only reach this resource's state */
		get(bag: string): Record<string, unknown>;
		/* constraint: needs state.read; removing the token with CO.events.off needs events.subscribe */
		onChange(handler: (event: CoStateChangeEvent) => boolean | void): number;
		set(bag: string, key: string, value: unknown): CoResult;
		delete(bag: string, key: string): CoResult;
	};
	readonly factions?: {
		list(): Record<string, unknown>[];
		disband(factionId: string): CoResult;
		promote(factionId: string, profileId: string): CoResult;
		kick(factionId: string, profileId: string): CoResult;
	};
	readonly quests?: {
		status(): Record<string, unknown>;
		mode(mode: string): CoResult;
		gate(quest: string, stage: number, release: boolean): CoResult;
		solo(profileId: string, quest: string, solo: boolean): CoResult;
	};
	readonly settlements?: {
		get(zoneId: number): { buildZoneId: number; ownerProfileId: string };
		setOwner(zoneId: number, profileId: string): CoResult;
	};
	readonly views?: {
		open(playerId: number, view: string): CoResult;
		close(playerId: number): CoResult;
		onMessage(handler: (message: CoViewMessage) => void): void;
	};
};
