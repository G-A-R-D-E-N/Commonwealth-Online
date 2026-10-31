const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const SOURCE = fs.readFileSync(__dirname + "/prompt-glyphs.js", "utf8");
const AMBER = { r: 201, g: 168, b: 74 };

const PIXELS = [
  [255, 255, 255, 255],
  [0, 0, 0, 255],
  [128, 64, 32, 255],
  [10, 20, 30, 1]
];

function runGlyphs(pixels, glyphSrc) {
  const state = { created: 0, drawn: 0, dataUrls: 0, tinted: null };
  const element = { src: "", getAttribute: () => glyphSrc };

  function Image() {
    state.created += 1;
    const image = this;
    Object.defineProperty(image, "naturalWidth", { value: 2 });
    Object.defineProperty(image, "naturalHeight", { value: 2 });
    Object.defineProperty(image, "src", {
      set(value) {
        image.loadedFrom = value;
        if (image.onload) {
          image.onload();
        }
      }
    });
  }

  const canvas = {
    width: 0,
    height: 0,
    getContext: () => ({
      drawImage: () => {
        state.drawn += 1;
      },
      getImageData: () => ({ data: new Uint8ClampedArray(pixels.flat()) }),
      putImageData: (imageData) => {
        state.tinted = imageData.data;
      }
    }),
    toDataURL: () => {
      state.dataUrls += 1;
      return "data:image/png;base64,stub";
    }
  };

  const sandbox = {
    window: {},
    document: {
      documentElement: {},
      querySelectorAll: () => [element],
      createElement: () => canvas
    },
    getComputedStyle: () => ({ getPropertyValue: () => "#c9a84a" }),
    Image,
    console
  };
  vm.runInNewContext(SOURCE, sandbox);
  sandbox.window.CO_PromptGlyphs.hydrate();

  return { state, element };
}

{
  const { state, element } = runGlyphs(PIXELS, "glyph.png");

  assert.equal(state.created, 1, "one glyph image is tinted per data-glyph-src element");
  assert.equal(state.drawn, 1);
  assert.equal(state.dataUrls, 1);
  assert.equal(element.src, "data:image/png;base64,stub", "the tinted data URL replaces the source");

  const tinted = Array.from(state.tinted);
  assert.deepEqual(tinted.slice(0, 4), [AMBER.r, AMBER.g, AMBER.b, 255], "white glyph pixels take the amber tint");
  assert.deepEqual(tinted.slice(4, 8), [0, 0, 0, 255], "black glyph pixels stay black");
  assert.deepEqual(tinted.slice(12, 16), [10, 20, 30, 0], "pixels below the alpha floor keep their colour and lose alpha");

  const luminance = (128 + 64 + 32) / (3 * 255);
  assert.deepEqual(tinted.slice(8, 12), [
    Math.round(luminance * AMBER.r),
    Math.round(luminance * AMBER.g),
    Math.round(luminance * AMBER.b),
    255
  ], "edge pixels scale the amber tint by luminance");
}

{
  const { state, element } = runGlyphs(PIXELS, null);

  assert.equal(state.created, 0, "an element without data-glyph-src is left alone");
  assert.equal(element.src, "");
}
