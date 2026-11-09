"use strict";

const assert = require("node:assert/strict");
const { execFileSync } = require("node:child_process");
const path = require("node:path");

const trustProxyFor = (value) => {
  const env = { ...process.env, TRUST_PROXY: value };
  const output = execFileSync(
    process.execPath,
    ["-e", "process.stdout.write(JSON.stringify(require('./src/config').trustProxy))"],
    { cwd: path.resolve(__dirname, ".."), env, encoding: "utf8" }
  );
  return JSON.parse(output);
};

assert.equal(trustProxyFor("0"), false, "TRUST_PROXY=0 must turn proxy trust off");
assert.equal(trustProxyFor(""), false, "an empty TRUST_PROXY must leave proxy trust off");
assert.equal(trustProxyFor("1"), 1, "TRUST_PROXY=1 must trust one hop");
assert.equal(trustProxyFor("2"), 2, "TRUST_PROXY=2 must trust two hops");

console.log("config trust proxy tests passed");
