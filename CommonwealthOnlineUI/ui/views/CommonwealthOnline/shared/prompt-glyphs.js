(function () {
  "use strict";

  var GLYPH_WHITE_THRESHOLD = 200;
  var GLYPH_BLACK_THRESHOLD = 40;
  var glyphTintCache = {};

  function getAmberTint() {
    var style = getComputedStyle(document.documentElement);
    var amber = style.getPropertyValue("--co-amber").trim() || "#c9a84a";
    var hex = amber.replace("#", "");
    if (hex.length === 3) {
      hex = hex[0] + hex[0] + hex[1] + hex[1] + hex[2] + hex[2];
    }
    return {
      r: parseInt(hex.slice(0, 2), 16) || 201,
      g: parseInt(hex.slice(2, 4), 16) || 168,
      b: parseInt(hex.slice(4, 6), 16) || 74
    };
  }

  function classifyGlyphPixel(r, g, b) {
    if (r >= GLYPH_WHITE_THRESHOLD && g >= GLYPH_WHITE_THRESHOLD && b >= GLYPH_WHITE_THRESHOLD) {
      return "white";
    }
    if (r <= GLYPH_BLACK_THRESHOLD && g <= GLYPH_BLACK_THRESHOLD && b <= GLYPH_BLACK_THRESHOLD) {
      return "black";
    }
    return "edge";
  }

  function tintGlyphImageData(imageData, color) {
    var data = imageData.data;
    for (var i = 0; i < data.length; i += 4) {
      if (data[i + 3] < 4) {
        data[i + 3] = 0;
        continue;
      }
      var kind = classifyGlyphPixel(data[i], data[i + 1], data[i + 2]);
      if (kind === "white") {
        data[i] = color.r;
        data[i + 1] = color.g;
        data[i + 2] = color.b;
      } else if (kind === "black") {
        data[i] = 0;
        data[i + 1] = 0;
        data[i + 2] = 0;
      } else {
        var lum = (data[i] + data[i + 1] + data[i + 2]) / (3 * 255);
        data[i] = Math.round(lum * color.r);
        data[i + 1] = Math.round(lum * color.g);
        data[i + 2] = Math.round(lum * color.b);
      }
    }
    return imageData;
  }

  function tintGlyphImage(url, color, done) {
    var key = url + "|" + color.r + "," + color.g + "," + color.b;
    if (glyphTintCache[key]) {
      done(glyphTintCache[key]);
      return;
    }
    var img = new Image();
    img.onload = function () {
      var canvas = document.createElement("canvas");
      canvas.width = img.naturalWidth;
      canvas.height = img.naturalHeight;
      var ctx = canvas.getContext("2d");
      if (!ctx) {
        done(null);
        return;
      }
      ctx.drawImage(img, 0, 0);
      try {
        ctx.putImageData(
          tintGlyphImageData(ctx.getImageData(0, 0, canvas.width, canvas.height), color),
          0,
          0
        );
      } catch (err) {
        done(null);
        return;
      }
      var dataUrl = canvas.toDataURL("image/png");
      glyphTintCache[key] = dataUrl;
      done(dataUrl);
    };
    img.onerror = function () {
      done(null);
    };
    img.src = url;
  }

  function hydrate() {
    var color = getAmberTint();
    var imgs = document.querySelectorAll(".prompt__glyph-img[data-glyph-src]");
    for (var i = 0; i < imgs.length; i++) {
      (function (img) {
        var src = img.getAttribute("data-glyph-src");
        if (!src) {
          return;
        }
        tintGlyphImage(src, color, function (dataUrl) {
          if (dataUrl) {
            img.src = dataUrl;
          }
        });
      })(imgs[i]);
    }
  }

  window.CO_PromptGlyphs = {
    hydrate: hydrate
  };
})();
