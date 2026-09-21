"use strict";

const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const outputDir = path.join(root, "assets", "profile-icons");
const sourceCommit = "918547cc872c3288122f9d15ed0416cf33aa8bbf";
const sourceRoot = `https://raw.githubusercontent.com/CircuitBread0111/Fallout_Perk_Planner/${sourceCommit}/perk_images`;

const icons = [
  ["armorer.png", "2350c179ddc43c146adc5ef73e8b9456bc4b3423"],
  ["hacker.png", "d250a6b6b219c4fd4a27a4ad9e3a565c71a8279c"],
  ["rifleman.png", "4179ed2f2cf792272a5ef0d42760cb1c6a001667"],
  ["medic.png", "730051ccbd6e85ecb378a22a59e0140803709f01"],
  ["scrapper.png", "1e99e6c8fa074f578a734e8b0197c546118da946"],
  ["cap_collector.png", "56569e6f7c26943844b1c8037a03ba73c35de4f3"],
];

const gitBlobSha = (buffer) =>
  crypto
    .createHash("sha1")
    .update(`blob ${buffer.length}\0`)
    .update(buffer)
    .digest("hex");

const fetchIcon = async ([fileName, expectedSha]) => {
  const target = path.join(outputDir, fileName);
  if (fs.existsSync(target)) {
    const current = fs.readFileSync(target);
    if (gitBlobSha(current) === expectedSha) {
      return;
    }
  }

  const response = await fetch(`${sourceRoot}/${fileName}`);
  if (!response.ok) {
    throw new Error(`Failed to fetch ${fileName}: HTTP ${response.status}`);
  }

  const buffer = Buffer.from(await response.arrayBuffer());
  const actualSha = gitBlobSha(buffer);
  if (actualSha !== expectedSha) {
    throw new Error(`Unexpected source content for ${fileName}: ${actualSha}`);
  }

  fs.mkdirSync(outputDir, { recursive: true });
  fs.writeFileSync(target, buffer);
};

Promise.all(icons.map(fetchIcon))
  .then(() => console.log(`Fetched ${icons.length} verified profile icons`))
  .catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
