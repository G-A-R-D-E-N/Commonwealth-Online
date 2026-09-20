"use strict";

const assert = require("node:assert/strict");
const { TYPES, TYPE_IDS, getType } = require("../src/lib/applications");

assert.deepEqual(Object.keys(TYPES), TYPE_IDS);
assert.deepEqual(TYPE_IDS, ["team", "beta"]);

for (const typeId of TYPE_IDS) {
  const form = getType(typeId);
  assert.equal(form.id, typeId);
  assert.ok(form.title);
  assert.ok(form.fields.length > 0);
  assert.equal(new Set(form.fields.map((field) => field.key)).size, form.fields.length);
  assert.ok(form.fields.some((field) => field.key === "displayName" && field.required));
  assert.ok(form.fields.some((field) => field.key === "email" && field.type === "email"));
  assert.ok(form.fields.some((field) => field.key === "discordHandle" && field.required));
  assert.ok(form.fields.some((field) => field.key === "ageConfirmed" && field.type === "checkbox"));
}

assert.equal(getType("missing"), null);
assert.ok(TYPES.team.fields.some((field) => field.key === "role" && field.options.length > 0));
assert.ok(TYPES.beta.fields.some((field) => field.key === "gameEdition" && field.options.length > 0));

console.log("application form checks passed");
